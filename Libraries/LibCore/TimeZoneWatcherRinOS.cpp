/*
 * Copyright (c) 2026, RinOS contributors
 *
 * SPDX-License-Identifier: BSD-2-Clause
 */

#include <AK/Platform.h>

#if !defined(AK_OS_RINOS)
static_assert(false, "This file must only be used for RinOS");
#endif

#include <AK/Optional.h>
#include <AK/Types.h>
#include <LibCore/TimeZoneWatcher.h>
#include <LibCore/Timer.h>
#include <rin/timezone.h>
#include <string.h>

namespace Core {

static constexpr int time_zone_poll_interval_ms = 1000;

struct SystemTimeZoneSnapshot {
    u32 generation { 0 };
    char id[sizeof(RinTimeZoneInfoV1::id)] {};
};

// /System/Config is a root-owned private state directory. Poll the kernel's
// validated system-time-zone API instead of attempting an unprivileged
// filesystem watch. The shared generation also advances for per-user overrides,
// so notify only when both the generation and system zone ID have changed.
static Optional<SystemTimeZoneSnapshot> read_system_time_zone()
{
    RinTimeZoneInfoV1 info {};
    if (rin_timezone_info_get_v1(&info) != RIN_RESULT_OK)
        return {};

    if (info.struct_size != sizeof(info) || info.version != RIN_TIME_ZONE_INFO_VERSION ||
        info.generation == 0 || (info.flags & RIN_TIME_ZONE_INFO_FLAG_SYSTEM_DEFAULT) == 0)
        return {};

    auto* terminator = static_cast<u8 const*>(memchr(info.id, '\0', sizeof(info.id)));
    if (!terminator || terminator == info.id)
        return {};

    SystemTimeZoneSnapshot snapshot;
    snapshot.generation = info.generation;
    memcpy(snapshot.id, info.id, sizeof(snapshot.id));
    return snapshot;
}

class TimeZoneWatcherImpl final : public TimeZoneWatcher {
public:
    static ErrorOr<NonnullOwnPtr<TimeZoneWatcherImpl>> create()
    {
        auto watcher = adopt_own(*new TimeZoneWatcherImpl());
        auto snapshot = read_system_time_zone();
        if (!snapshot.has_value())
            return Error::from_string_literal("Unable to read the verified RinOS system time zone");

        watcher->m_last_generation = snapshot->generation;
        memcpy(watcher->m_last_id, snapshot->id, sizeof(watcher->m_last_id));
        watcher->m_poll_timer->start();
        return watcher;
    }

private:
    TimeZoneWatcherImpl()
        : m_poll_timer(Timer::create_repeating(time_zone_poll_interval_ms, [this] { poll_system_time_zone(); }))
    {
    }

    void poll_system_time_zone()
    {
        auto snapshot = read_system_time_zone();
        if (!snapshot.has_value() || snapshot->generation == m_last_generation)
            return;

        auto const system_time_zone_changed = strcmp(snapshot->id, m_last_id) != 0;
        m_last_generation = snapshot->generation;
        memcpy(m_last_id, snapshot->id, sizeof(m_last_id));
        if (system_time_zone_changed && on_time_zone_changed)
            on_time_zone_changed();
    }

    NonnullRefPtr<Timer> m_poll_timer;
    u32 m_last_generation { 0 };
    char m_last_id[sizeof(RinTimeZoneInfoV1::id)] {};
};

ErrorOr<NonnullOwnPtr<TimeZoneWatcher>> TimeZoneWatcher::create()
{
    return TimeZoneWatcherImpl::create();
}

}
