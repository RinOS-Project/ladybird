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

static unsigned file_watcher_mask_to_inotify_flags(FileWatcherEvent::Type event_mask)
{
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

    return inotify_mask;
}

static unsigned union_inotify_event_masks(Vector<FileWatcherPath> const& paths)
{
    unsigned inotify_mask = 0;
    for (auto const& path : paths)
        inotify_mask |= file_watcher_mask_to_inotify_flags(path.event_mask) & ~IN_DONT_FOLLOW;
    if (!paths.is_empty() && has_flag(paths[0].event_mask, FileWatcherEvent::Type::DoNotFollowLink))
        inotify_mask |= IN_DONT_FOLLOW;
    return inotify_mask;
}

static ErrorOr<Vector<FileWatcherEvent>> get_events_from_fd(
    int fd,
    HashMap<unsigned, Vector<FileWatcherPath>>& wd_to_paths,
    HashMap<ByteString, unsigned>& path_to_wd)
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

        if ((event->mask & IN_IGNORED) != 0) {
            auto it = wd_to_paths.find(event->wd);
            if (it != wd_to_paths.end()) {
                for (auto const& watched_path : it->value)
                    path_to_wd.remove(watched_path.path);
                wd_to_paths.remove(event->wd);
            }
            continue;
        }

        auto it = wd_to_paths.find(event->wd);
        if (it == wd_to_paths.end()) {
            dbgln_if(FILE_WATCHER_DEBUG, "get_events_from_fd: Got an event for a non-existent wd {}?!", event->wd);
            continue;
        }

        auto event_type = FileWatcherEvent::Type::Invalid;

        if ((event->mask & (IN_CREATE | IN_MOVED_TO)) != 0)
            event_type |= FileWatcherEvent::Type::ChildCreated;
        if ((event->mask & (IN_DELETE | IN_MOVED_FROM)) != 0)
            event_type |= FileWatcherEvent::Type::ChildDeleted;
        if ((event->mask & IN_DELETE_SELF) != 0)
            event_type |= FileWatcherEvent::Type::Deleted;
        if ((event->mask & IN_MODIFY) != 0)
            event_type |= FileWatcherEvent::Type::ContentModified;
        if ((event->mask & IN_ATTRIB) != 0)
            event_type |= FileWatcherEvent::Type::MetadataModified;

        if (event_type == FileWatcherEvent::Type::Invalid) {
            warnln("Unknown event type {:x} returned by the watch_file descriptor {}", event->mask, event->wd);
            continue;
        }

        StringView child_name;
        if (event->len > 0) {
            auto const name_length = strnlen(event->name, event->len);
            if (name_length == event->len) {
                warnln("get_events_from_fd: Missing NUL terminator in inotify name from wd {}", fd);
                continue;
            }
            child_name = StringView { event->name, name_length };
        }

        for (auto const& watched_path : it->value) {
            auto filtered_type = event_type & watched_path.event_mask;
            if (filtered_type == FileWatcherEvent::Type::Invalid)
                continue;

            FileWatcherEvent result;
            result.type = filtered_type;
            result.event_path = event->len > 0
                ? LexicalPath::join(watched_path.path, child_name).string()
                : watched_path.path;
            TRY(results.try_append(move(result)));
        }
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
    auto watcher = adopt_nonnull_ref_or_enomem(new (nothrow) FileWatcher(watcher_fd, move(notifier)));
    if (watcher.is_error()) {
        (void)::close(watcher_fd);
        return watcher.release_error();
    }
    return watcher.release_value();
}

FileWatcher::FileWatcher(int watcher_fd, NonnullRefPtr<Notifier> notifier)
    : FileWatcherBase(watcher_fd)
    , m_notifier(move(notifier))
{
    m_notifier->on_activation = [this] {
        auto events = get_events_from_fd(m_notifier->fd(), m_wd_to_paths, m_path_to_wd);
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

ErrorOr<NonnullOwnPtr<BlockingFileWatcher>> BlockingFileWatcher::create(FileWatcherFlags flags)
{
    auto watcher_fd = ::inotify_init1(file_watcher_flags_to_inotify_flags(flags | FileWatcherFlags::CloseOnExec));
    if (watcher_fd < 0)
        return Error::from_errno(errno);

    auto watcher = adopt_nonnull_own_or_enomem(new (nothrow) BlockingFileWatcher(watcher_fd));
    if (watcher.is_error()) {
        (void)::close(watcher_fd);
        return watcher.release_error();
    }
    return watcher.release_value();
}

BlockingFileWatcher::~BlockingFileWatcher() = default;

ErrorOr<Optional<FileWatcherEvent>> BlockingFileWatcher::wait_for_event()
{
    if (m_next_pending_event == m_pending_events.size()) {
        m_pending_events.clear();
        m_next_pending_event = 0;

        auto events = get_events_from_fd(m_watcher_fd, m_wd_to_paths, m_path_to_wd);
        if (events.is_error())
            return events.release_error();
        m_pending_events = events.release_value();
        if (m_pending_events.is_empty())
            return Optional<FileWatcherEvent> {};
    }

    auto event = move(m_pending_events[m_next_pending_event++]);
    if (has_flag(event.type, FileWatcherEvent::Type::Deleted)) {
        auto result = remove_watch(event.event_path);
        if (result.is_error())
            dbgln_if(FILE_WATCHER_DEBUG, "wait_for_event: {}", result.error());
    }
    return Optional<FileWatcherEvent> { move(event) };
}

FileWatcherBase::~FileWatcherBase()
{
    if (m_watcher_fd >= 0)
        (void)::close(m_watcher_fd);
}

ErrorOr<bool> FileWatcherBase::add_watch(ByteString path, FileWatcherEvent::Type event_mask)
{
    if (m_path_to_wd.find(path) != m_path_to_wd.end()) {
        dbgln_if(FILE_WATCHER_DEBUG, "add_watch: path '{}' is already being watched", path);
        return false;
    }

    int watch_descriptor = ::inotify_add_watch(m_watcher_fd, path.characters(), file_watcher_mask_to_inotify_flags(event_mask) | IN_MASK_ADD);
    if (watch_descriptor < 0)
        return Error::from_errno(errno);

    auto watch_it = m_wd_to_paths.find(watch_descriptor);
    if (watch_it != m_wd_to_paths.end()) {
        auto& watched_paths = watch_it->value;
        auto capacity_result = watched_paths.try_ensure_capacity(watched_paths.size() + 1);
        if (capacity_result.is_error()) {
            (void)::inotify_add_watch(m_watcher_fd, watched_paths[0].path.characters(), union_inotify_event_masks(watched_paths));
            return capacity_result.release_error();
        }

        auto path_result = m_path_to_wd.try_set(path, static_cast<unsigned>(watch_descriptor));
        if (path_result.is_error()) {
            (void)::inotify_add_watch(m_watcher_fd, watched_paths[0].path.characters(), union_inotify_event_masks(watched_paths));
            return path_result.release_error();
        }

        auto append_result = watched_paths.try_append(FileWatcherPath { path, event_mask });
        if (append_result.is_error()) {
            m_path_to_wd.remove(path);
            (void)::inotify_add_watch(m_watcher_fd, watched_paths[0].path.characters(), union_inotify_event_masks(watched_paths));
            return append_result.release_error();
        }
    } else {
        Vector<FileWatcherPath> watched_paths;
        auto append_result = watched_paths.try_append(FileWatcherPath { path, event_mask });
        if (append_result.is_error()) {
            (void)::inotify_rm_watch(m_watcher_fd, watch_descriptor);
            return append_result.release_error();
        }
        auto path_result = m_path_to_wd.try_set(path, static_cast<unsigned>(watch_descriptor));
        if (path_result.is_error()) {
            (void)::inotify_rm_watch(m_watcher_fd, watch_descriptor);
            return path_result.release_error();
        }
        auto watch_result = m_wd_to_paths.try_set(static_cast<unsigned>(watch_descriptor), move(watched_paths));
        if (watch_result.is_error()) {
            m_path_to_wd.remove(path);
            (void)::inotify_rm_watch(m_watcher_fd, watch_descriptor);
            return watch_result.release_error();
        }
    }

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

    auto watch_descriptor = it->value;
    auto watch_it = m_wd_to_paths.find(watch_descriptor);
    if (watch_it == m_wd_to_paths.end())
        return Error::from_errno(EIO);

    auto& watched_paths = watch_it->value;
    size_t removed_path_index = 0;
    while (removed_path_index < watched_paths.size() && watched_paths[removed_path_index].path != path)
        ++removed_path_index;
    if (removed_path_index == watched_paths.size())
        return Error::from_errno(EIO);

    if (watched_paths.size() == 1) {
        if (::inotify_rm_watch(m_watcher_fd, watch_descriptor) < 0 && errno != EINVAL)
            return Error::from_errno(errno);
        m_path_to_wd.remove(it);
        m_wd_to_paths.remove(watch_descriptor);
    } else {
        FileWatcherPath const* first_remaining_path = nullptr;
        unsigned remaining_mask = 0;
        for (size_t i = 0; i < watched_paths.size(); ++i) {
            if (i == removed_path_index)
                continue;
            auto const& watched_path = watched_paths[i];
            if (!first_remaining_path)
                first_remaining_path = &watched_path;
            remaining_mask |= file_watcher_mask_to_inotify_flags(watched_path.event_mask) & ~IN_DONT_FOLLOW;
        }
        if (has_flag(first_remaining_path->event_mask, FileWatcherEvent::Type::DoNotFollowLink))
            remaining_mask |= IN_DONT_FOLLOW;

        auto remaining_watch_descriptor = ::inotify_add_watch(m_watcher_fd, first_remaining_path->path.characters(), remaining_mask);
        if (remaining_watch_descriptor < 0) {
            if (errno != EINVAL)
                return Error::from_errno(errno);
            for (auto const& watched_path : watched_paths)
                m_path_to_wd.remove(watched_path.path);
            m_wd_to_paths.remove(watch_descriptor);
            return true;
        }
        if (remaining_watch_descriptor != static_cast<int>(watch_descriptor)) {
            (void)::inotify_rm_watch(m_watcher_fd, remaining_watch_descriptor);
            return Error::from_errno(EAGAIN);
        }

        m_path_to_wd.remove(it);
        watched_paths.remove(removed_path_index);
    }

    dbgln_if(FILE_WATCHER_DEBUG, "remove_watch: stopped watching path '{}' on InodeWatcher {}", path, m_watcher_fd);
    return true;
}

}
