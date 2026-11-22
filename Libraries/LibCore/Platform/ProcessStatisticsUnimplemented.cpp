/*
 * Copyright (c) 2024, Andrew Kaster <akaster@serenityos.org>
 *
 * SPDX-License-Identifier: BSD-2-Clause
 */

#include <LibCore/Platform/ProcessStatistics.h>

namespace Core::Platform {

ErrorOr<void> update_process_statistics(ProcessStatistics& statistics)
{
    for (auto& process : statistics.processes) {
        process->cpu_percent = 0.0f;
        process->memory_usage_bytes = 0;
        process->cpu_percent_available = false;
        process->memory_usage_available = false;
        process->has_cpu_sample = false;
        process->has_process_identity = false;
        process->time_spent_in_process = 0;
        process->process_start_time_ticks = 0;
    }
    return Error::from_string_literal("Process statistics are unavailable on this platform");
}

}
