/*
 * Copyright (c) 2026, RinOS contributors
 *
 * SPDX-License-Identifier: BSD-2-Clause
 */

#include <LibCore/Platform/ProcessStatistics.h>
#include <LibTest/TestCase.h>
#include <limits.h>
#include <unistd.h>

TEST_CASE(process_statistics_refreshes_self_and_resets_on_pid_reuse)
{
    Core::Platform::ProcessStatistics statistics;
    statistics.processes.append(make<Core::Platform::ProcessInfo>(getpid()));
    auto& self = *statistics.processes[0];

    MUST(Core::Platform::update_process_statistics(statistics));
    EXPECT(self.memory_usage_available);
    EXPECT(self.memory_usage_bytes > 0);
    EXPECT(!self.cpu_percent_available);

    usleep(100'000);
    MUST(Core::Platform::update_process_statistics(statistics));
    EXPECT(self.memory_usage_available);
    EXPECT(self.cpu_percent_available);

    // Simulate the same PID being observed with a different procfs start-time
    // identity. The first sample of the replacement must establish a baseline.
    ++self.process_start_time_ticks;
    MUST(Core::Platform::update_process_statistics(statistics));
    EXPECT(self.memory_usage_available);
    EXPECT(!self.cpu_percent_available);

    usleep(100'000);
    MUST(Core::Platform::update_process_statistics(statistics));
    EXPECT(self.cpu_percent_available);
}

TEST_CASE(process_statistics_do_not_keep_values_for_exited_pids)
{
    Core::Platform::ProcessStatistics statistics;
    statistics.processes.append(make<Core::Platform::ProcessInfo>(static_cast<pid_t>(INT_MAX)));
    auto& exited = *statistics.processes[0];
    exited.cpu_percent = 98.0f;
    exited.memory_usage_bytes = 123456;
    exited.cpu_percent_available = true;
    exited.memory_usage_available = true;

    MUST(Core::Platform::update_process_statistics(statistics));
    EXPECT(!exited.cpu_percent_available);
    EXPECT(!exited.memory_usage_available);
    EXPECT_EQ(exited.cpu_percent, 0.0f);
    EXPECT_EQ(exited.memory_usage_bytes, 0u);
    EXPECT(!exited.has_process_identity);
}
