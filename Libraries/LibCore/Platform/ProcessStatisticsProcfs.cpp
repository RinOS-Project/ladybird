/*
 * Copyright (c) 2024, Andrew Kaster <akaster@serenityos.org>
 * Copyright (c) 2026, RinOS contributors
 *
 * SPDX-License-Identifier: BSD-2-Clause
 */

#include <AK/ByteBuffer.h>
#include <AK/String.h>
#include <AK/StringView.h>
#include <LibCore/File.h>
#include <LibCore/Platform/ProcessStatistics.h>
#include <unistd.h>

namespace Core::Platform {

static ErrorOr<ByteBuffer> read_proc_file(StringView path)
{
    auto file = TRY(Core::File::open(path, Core::File::OpenMode::Read));
    return file->read_until_eof();
}

static bool parse_unsigned(StringView token, u64& value)
{
    if (token.is_empty())
        return false;

    auto parsed = token.to_number<u64>();
    if (!parsed.has_value())
        return false;

    value = parsed.value();
    return true;
}

static bool add_without_overflow(u64 left, u64 right, u64& result)
{
    if (right > NumericLimits<u64>::max() - left)
        return false;

    result = left + right;
    return true;
}

static bool parse_system_statistics(StringView contents, u64& total_ticks, u64& online_cpus)
{
    bool found_aggregate_cpu = false;
    total_ticks = 0;
    online_cpus = 0;

    for (auto line : contents.split_view('\n')) {
        auto fields = line.split_view(' ');
        if (fields.is_empty())
            continue;

        auto label = fields[0];
        if (label == "cpu"sv) {
            if (found_aggregate_cpu)
                return false;
            found_aggregate_cpu = true;

            // Linux /proc/stat exports user, nice, system, idle, iowait, irq,
            // softirq, steal, guest and guest_nice. Guest time is already
            // accounted for in user/nice and must not be added a second time.
            u64 parsed_counters = 0;
            u64 aggregate = 0;
            for (size_t index = 1; index < fields.size() && parsed_counters < 8; ++index) {
                u64 counter;
                if (!parse_unsigned(fields[index], counter) ||
                    !add_without_overflow(aggregate, counter, aggregate))
                    return false;
                ++parsed_counters;
            }
            if (parsed_counters < 4)
                return false;
            total_ticks = aggregate;
            continue;
        }

        if (label.starts_with("cpu"sv) && label.length() > 3) {
            auto cpu_index = label.substring_view(3).to_number<u32>();
            if (cpu_index.has_value()) {
                if (online_cpus == NumericLimits<u64>::max())
                    return false;
                ++online_cpus;
            }
        }
    }

    if (!found_aggregate_cpu)
        return false;

    if (online_cpus == 0) {
        auto configured_online_cpus = sysconf(_SC_NPROCESSORS_ONLN);
        if (configured_online_cpus <= 0)
            return false;
        online_cpus = static_cast<u64>(configured_online_cpus);
    }

    return true;
}

struct ParsedProcessStat {
    u64 user_ticks { 0 };
    u64 system_ticks { 0 };
    u64 start_time_ticks { 0 };
    u64 resident_pages { 0 };
};

static bool parse_process_statistics(StringView line, pid_t expected_pid, ParsedProcessStat& parsed)
{
    auto pid_separator = line.find(' ');
    if (!pid_separator.has_value())
        return false;

    auto parsed_pid = line.substring_view(0, pid_separator.value()).to_number<u64>();
    if (!parsed_pid.has_value() || parsed_pid.value() != static_cast<u64>(expected_pid))
        return false;

    // The comm field may contain spaces and parentheses. The final closing
    // parenthesis is the only reliable delimiter before fields 3 through 24.
    auto comm_end = line.find_last(')');
    if (!comm_end.has_value() || comm_end.value() <= pid_separator.value())
        return false;

    auto fields = line.substring_view(comm_end.value() + 1).split_view(' ');
    // Field 3 is state. utime/stime are 14/15, starttime is 22, and rss is 24.
    if (fields.size() < 22)
        return false;

    return parse_unsigned(fields[11], parsed.user_ticks) &&
        parse_unsigned(fields[12], parsed.system_ticks) &&
        parse_unsigned(fields[19], parsed.start_time_ticks) &&
        parse_unsigned(fields[21], parsed.resident_pages);
}

static void clear_process_statistics(ProcessInfo& process)
{
    process.cpu_percent = 0.0f;
    process.memory_usage_bytes = 0;
    process.cpu_percent_available = false;
    process.memory_usage_available = false;
    process.time_spent_in_process = 0;
    process.process_start_time_ticks = 0;
    process.has_process_identity = false;
    process.has_cpu_sample = false;
}

ErrorOr<void> update_process_statistics(ProcessStatistics& statistics)
{
    for (auto& process : statistics.processes) {
        process->cpu_percent = 0.0f;
        process->memory_usage_bytes = 0;
        process->cpu_percent_available = false;
        process->memory_usage_available = false;
    }

    auto page_size = sysconf(_SC_PAGESIZE);
    if (page_size <= 0)
        return Error::from_string_literal("RinOS returned an invalid process-statistics page size");

    auto system_stat_buffer = TRY(read_proc_file("/proc/stat"sv));
    auto system_stat = StringView { system_stat_buffer.data(), system_stat_buffer.size() };
    u64 total_ticks;
    u64 online_cpus;
    if (!parse_system_statistics(system_stat, total_ticks, online_cpus))
        return Error::from_string_literal("Failed to parse /proc/stat process counters");

    auto const previous_total_ticks = statistics.total_time_scheduled;
    auto const total_ticks_decreased = total_ticks < previous_total_ticks;
    auto const total_tick_delta = total_ticks_decreased ? 0 : total_ticks - previous_total_ticks;
    statistics.total_time_scheduled = total_ticks;

    for (auto& process : statistics.processes) {
        auto path = TRY(String::formatted("/proc/{}/stat", process->pid));
        auto process_stat_file = Core::File::open(path, Core::File::OpenMode::Read);
        if (process_stat_file.is_error()) {
            auto error = process_stat_file.release_error();
            clear_process_statistics(*process);
            // A process can exit between the process-manager snapshot and this
            // read. That is a normal race; other procfs failures are surfaced.
            if (error.is_errno() && (error.code() == ENOENT || error.code() == ESRCH))
                continue;
            return error;
        }

        auto process_stat_buffer = TRY(process_stat_file.release_value()->read_until_eof());
        auto process_stat = StringView { process_stat_buffer.data(), process_stat_buffer.size() };
        ParsedProcessStat parsed;
        if (!parse_process_statistics(process_stat, process->pid, parsed)) {
            clear_process_statistics(*process);
            return Error::from_string_literal("Failed to parse process fields in /proc/<pid>/stat");
        }

        auto const page_size_u64 = static_cast<u64>(page_size);
        if (parsed.resident_pages > NumericLimits<u64>::max() / page_size_u64) {
            clear_process_statistics(*process);
            return Error::from_string_literal("Process RSS exceeds the supported byte range");
        }

        u64 process_ticks;
        if (!add_without_overflow(parsed.user_ticks, parsed.system_ticks, process_ticks)) {
            clear_process_statistics(*process);
            return Error::from_string_literal("Process CPU counters overflowed");
        }

        auto const same_process = process->has_process_identity &&
            process->process_start_time_ticks == parsed.start_time_ticks;
        auto const previous_cpu_sample = process->has_cpu_sample && same_process && !total_ticks_decreased;

        process->memory_usage_bytes = parsed.resident_pages * page_size_u64;
        process->memory_usage_available = true;

        if (!same_process || !process->has_cpu_sample || total_ticks_decreased) {
            process->process_start_time_ticks = parsed.start_time_ticks;
            process->has_process_identity = true;
            process->time_spent_in_process = process_ticks;
            process->has_cpu_sample = true;
            continue;
        }

        if (total_tick_delta == 0)
            continue;

        if (process_ticks < process->time_spent_in_process) {
            // Counter reset or PID reuse without a distinguishable procfs
            // start time: take a fresh baseline and do not publish a spike.
            process->process_start_time_ticks = parsed.start_time_ticks;
            process->time_spent_in_process = process_ticks;
            process->has_process_identity = true;
            process->has_cpu_sample = true;
            continue;
        }

        auto const process_tick_delta = process_ticks - process->time_spent_in_process;
        process->time_spent_in_process = process_ticks;
        process->cpu_percent = static_cast<float>(
            static_cast<double>(process_tick_delta) * 100.0 * static_cast<double>(online_cpus) /
            static_cast<double>(total_tick_delta));
        process->cpu_percent_available = true;
        process->has_process_identity = true;
        process->has_cpu_sample = previous_cpu_sample;
    }

    return {};
}

}
