/*
 * Copyright (c) 2024, Andrew Kaster <akaster@serenityos.org>
 *
 * SPDX-License-Identifier: BSD-2-Clause
 */

#pragma once

#include <AK/Types.h>

#if defined(AK_OS_MACH)
#    include <LibCore/MachPort.h>
#endif

namespace Core::Platform {

struct ProcessInfo {
    explicit ProcessInfo(pid_t pid)
        : pid(pid)
    {
    }

    pid_t pid { 0 };

    u64 memory_usage_bytes { 0 };
    float cpu_percent { 0.0f };
    bool memory_usage_available { false };
    bool cpu_percent_available { false };

    u64 time_spent_in_process { 0 };
    u64 process_start_time_ticks { 0 };
    bool has_process_identity { false };
    bool has_cpu_sample { false };

#if defined(AK_OS_MACH)
    Core::MachPort child_task_port;
#endif
};

}
