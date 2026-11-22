/*
 * Copyright (c) 2023, Tim Flynn <trflynn89@serenityos.org>
 *
 * SPDX-License-Identifier: BSD-2-Clause
 */

#include "FileWatcher.h"
#include <AK/ByteString.h>
#include <AK/Debug.h>
#include <AK/LexicalPath.h>
#include <AK/Vector.h>
#include <LibCore/Notifier.h>
#include <errno.h>
#include <limits.h>
#include <string.h>
#include <sys/inotify.h>
#include <sys/ioctl.h>
#include <unistd.h>

namespace Core {

static constexpr unsigned file_watcher_flags_to_inotify_flags(FileWatcherFlags flags)
{
    unsigned result = 0;

    if (has_flag(flags, FileWatcherFlags::Nonblock))
        result |= IN_NONBLOCK;
    if (has_flag(flags, FileWatcherFlags::CloseOnExec))
        result |= IN_CLOEXEC;

    return result;
}

static ErrorOr<Vector<FileWatcherEvent>> get_events_from_fd(int fd, HashMap<unsigned, ByteString> const& wd_to_path)
{
    static constexpr auto max_event_size = sizeof(inotify_event) + NAME_MAX + 1;

    // Note from INOTIFY(7) man page:
    //
    //     Some systems cannot read integer variables if they are not properly aligned. On other
    //     systems, incorrect alignment may decrease performance. Hence, the buffer used for reading
    //     from the inotify file descriptor should have the same alignment as inotify_event.
    alignas(alignof(inotify_event)) Array<u8, max_event_size * 4> buffer;
    ssize_t rc;
    do {
        rc = ::read(fd, buffer.data(), buffer.size());
    } while (rc < 0 && errno == EINTR);

    if (rc < 0) {
        if (errno == EAGAIN || errno == EWOULDBLOCK)
            return Vector<FileWatcherEvent> {};
        dbgln_if(FILE_WATCHER_DEBUG, "get_event_from_fd: Reading from wd {} failed: {}", fd, strerror(errno));
        return Error::from_errno(errno);
    }

    Vector<FileWatcherEvent> results;
    size_t offset = 0;
    while (offset < static_cast<size_t>(rc)) {
        auto const remaining = static_cast<size_t>(rc) - offset;
        if (remaining < sizeof(inotify_event)) {
            warnln("get_events_from_fd: Truncated inotify header from wd {}", fd);
            break;
        }

        auto const* event = reinterpret_cast<inotify_event const*>(buffer.data() + offset);
        if (event->len > remaining - sizeof(inotify_event)) {
            warnln("get_events_from_fd: Truncated inotify name from wd {}", fd);
            break;
        }
        auto const event_size = sizeof(inotify_event) + event->len;
        offset += event_size;

        if ((event->mask & IN_Q_OVERFLOW) != 0) {
            FileWatcherEvent overflow_event;
            overflow_event.type = FileWatcherEvent::Type::QueueOverflow;
            TRY(results.try_append(move(overflow_event)));
            continue;
        }

        if ((event->mask & IN_IGNORED) != 0)
            continue;

        auto it = wd_to_path.find(event->wd);
        if (it == wd_to_path.end()) {
            dbgln_if(FILE_WATCHER_DEBUG, "get_events_from_fd: Got an event for a non-existent wd {}?!", event->wd);
            continue;
        }

        FileWatcherEvent result;
        auto const& path = it->value;

        if ((event->mask & (IN_CREATE | IN_MOVED_TO)) != 0)
            result.type |= FileWatcherEvent::Type::ChildCreated;
        if ((event->mask & (IN_DELETE | IN_MOVED_FROM)) != 0)
            result.type |= FileWatcherEvent::Type::ChildDeleted;
        if ((event->mask & IN_DELETE_SELF) != 0)
            result.type |= FileWatcherEvent::Type::Deleted;
        if ((event->mask & IN_MODIFY) != 0)
            result.type |= FileWatcherEvent::Type::ContentModified;
        if ((event->mask & IN_ATTRIB) != 0)
            result.type |= FileWatcherEvent::Type::MetadataModified;

        if (result.type == FileWatcherEvent::Type::Invalid) {
            warnln("Unknown event type {:x} returned by the watch_file descriptor for {}", event->mask, path);
            continue;
        }

        if (event->len > 0) {
            auto const name_length = strnlen(event->name, event->len);
            if (name_length == event->len) {
                warnln("get_events_from_fd: Missing NUL terminator in inotify name from wd {}", fd);
                continue;
            }
            StringView child_name { event->name, name_length };
            result.event_path = LexicalPath::join(path, child_name).string();
        } else {
            result.event_path = path;
        }

        TRY(results.try_append(move(result)));
    }

    for (auto const& event : results)
        dbgln_if(FILE_WATCHER_DEBUG, "get_events_from_fd: got event from wd {} on '{}' type {}", fd, event.event_path, event.type);
    return results;
}

ErrorOr<NonnullRefPtr<FileWatcher>> FileWatcher::create(FileWatcherFlags flags)
{
    auto watcher_fd = ::inotify_init1(file_watcher_flags_to_inotify_flags(flags | FileWatcherFlags::CloseOnExec));
    if (watcher_fd < 0)
        return Error::from_errno(errno);

    auto notifier = Notifier::construct(watcher_fd, Notifier::Type::Read);
    return adopt_nonnull_ref_or_enomem(new (nothrow) FileWatcher(watcher_fd, move(notifier)));
}

FileWatcher::FileWatcher(int watcher_fd, NonnullRefPtr<Notifier> notifier)
    : FileWatcherBase(watcher_fd)
    , m_notifier(move(notifier))
{
    m_notifier->on_activation = [this] {
        auto events = get_events_from_fd(m_notifier->fd(), m_wd_to_path);
        if (events.is_error()) {
            dbgln_if(FILE_WATCHER_DEBUG, "on_ready_to_read: {}", events.error());
            return;
        }
        for (auto const& event : events.value()) {

            if (has_flag(event.type, FileWatcherEvent::Type::Deleted)) {
                auto result = remove_watch(event.event_path);
                if (result.is_error()) {
                    dbgln_if(FILE_WATCHER_DEBUG, "on_ready_to_read: {}", result.error());
                }
            }

            on_change(event);
        }
    };
}

FileWatcher::~FileWatcher() = default;

ErrorOr<bool> FileWatcherBase::add_watch(ByteString path, FileWatcherEvent::Type event_mask)
{
    if (m_path_to_wd.find(path) != m_path_to_wd.end()) {
        dbgln_if(FILE_WATCHER_DEBUG, "add_watch: path '{}' is already being watched", path);
        return false;
    }

    unsigned inotify_mask = 0;

    if (has_flag(event_mask, FileWatcherEvent::Type::ChildCreated))
        inotify_mask |= IN_CREATE | IN_MOVED_TO;
    if (has_flag(event_mask, FileWatcherEvent::Type::ChildDeleted))
        inotify_mask |= IN_DELETE | IN_MOVED_FROM;
    if (has_flag(event_mask, FileWatcherEvent::Type::Deleted))
        inotify_mask |= IN_DELETE_SELF;
    if (has_flag(event_mask, FileWatcherEvent::Type::ContentModified))
        inotify_mask |= IN_MODIFY;
    if (has_flag(event_mask, FileWatcherEvent::Type::MetadataModified))
        inotify_mask |= IN_ATTRIB;
    if (has_flag(event_mask, FileWatcherEvent::Type::DoNotFollowLink))
        inotify_mask |= IN_DONT_FOLLOW;

    int watch_descriptor = ::inotify_add_watch(m_watcher_fd, path.characters(), inotify_mask);
    if (watch_descriptor < 0)
        return Error::from_errno(errno);

    m_path_to_wd.set(path, watch_descriptor);
    m_wd_to_path.set(watch_descriptor, path);

    dbgln_if(FILE_WATCHER_DEBUG, "add_watch: watching path '{}' on InodeWatcher {} wd {}", path, m_watcher_fd, watch_descriptor);
    return true;
}

ErrorOr<bool> FileWatcherBase::remove_watch(ByteString path)
{
    auto it = m_path_to_wd.find(path);
    if (it == m_path_to_wd.end()) {
        dbgln_if(FILE_WATCHER_DEBUG, "remove_watch: path '{}' is not being watched", path);
        return false;
    }

    m_path_to_wd.remove(it);
    m_wd_to_path.remove(it->value);

    if (::inotify_rm_watch(m_watcher_fd, it->value) < 0)
        return Error::from_errno(errno);

    dbgln_if(FILE_WATCHER_DEBUG, "remove_watch: stopped watching path '{}' on InodeWatcher {}", path, m_watcher_fd);
    return true;
}

}
