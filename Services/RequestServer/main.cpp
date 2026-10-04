/*
 * Copyright (c) 2018-2020, Andreas Kling <andreas@ladybird.org>
 * Copyright (c) 2023, Andrew Kaster <akaster@serenityos.org>
 *
 * SPDX-License-Identifier: BSD-2-Clause
 */

#include <AK/ByteString.h>
#include <AK/Format.h>
#include <AK/ScopeGuard.h>
#include <AK/StringView.h>
#include <AK/Vector.h>
#include <LibCore/ArgsParser.h>
#include <LibCore/EventLoop.h>
#include <LibCore/Process.h>
#include <LibCore/System.h>
#include <LibCore/Timer.h>
#include <LibHTTP/Cache/DiskCache.h>
#include <LibIPC/SingleServer.h>
#include <LibMain/Main.h>
#include <RequestServer/ConnectionFromClient.h>
#include <RequestServer/Resolver.h>
#include <RequestServer/ResourceSubstitutionMap.h>

#if defined(AK_OS_WINDOWS)
#    include <AK/Windows.h>
#endif

namespace RequestServer {

OwnPtr<ResourceSubstitutionMap> g_resource_substitution_map;

}

#if defined(AK_OS_RINOS)
extern "C" int rin_service_should_stop(void);
#endif

#ifndef AK_OS_WINDOWS
static void handle_signal(int signal)
{
    VERIFY(signal == SIGINT || signal == SIGTERM);
    Core::EventLoop::current().quit(0);
}
#else
static volatile LONG s_windows_quit_requested = 0;

static BOOL WINAPI handle_console_control(DWORD control_type)
{
    switch (control_type) {
    case CTRL_C_EVENT:
    case CTRL_BREAK_EVENT:
    case CTRL_CLOSE_EVENT:
    case CTRL_LOGOFF_EVENT:
    case CTRL_SHUTDOWN_EVENT:
        InterlockedExchange(&s_windows_quit_requested, 1);
        return TRUE;
    default:
        return FALSE;
    }
}
#endif

ErrorOr<int> ladybird_main(Main::Arguments arguments)
{
    AK::set_rich_debug_enabled(true);

    Vector<ByteString> certificates;
    StringView mach_server_name;
    StringView http_disk_cache_mode;
    StringView resource_map_path;
    bool wait_for_debugger = false;

    Core::ArgsParser args_parser;
    args_parser.add_option(certificates, "Path to a certificate file", "certificate", 'C', "certificate");
    args_parser.add_option(mach_server_name, "Mach server name", "mach-server-name", 0, "mach_server_name");
    args_parser.add_option(http_disk_cache_mode, "HTTP disk cache mode", "http-disk-cache-mode", 0, "mode");
    args_parser.add_option(resource_map_path, "Path to JSON file mapping URLs to local files", "resource-map", 0, "path");
    args_parser.add_option(wait_for_debugger, "Wait for debugger", "wait-for-debugger");
    args_parser.parse(arguments);

    if (wait_for_debugger)
        Core::Process::wait_for_debugger_and_break();

#if defined(AK_OS_RINOS)
    // RinOS production transports must use the platform RCA bundle. Ignore
    // caller-provided certificate paths so a browser helper cannot silently
    // replace the system trust policy with an ad-hoc bundle.
    certificates.clear();
    certificates.append("/System/Trust/roots.rinca"sv);
#endif

    RequestServer::set_default_certificate_paths(move(certificates));

    if (!resource_map_path.is_empty()) {
        auto map = RequestServer::ResourceSubstitutionMap::load_from_file(resource_map_path);
        if (map.is_error())
            warnln("Unable to load resource substitution map from '{}': {}", resource_map_path, map.error());
        else
            RequestServer::g_resource_substitution_map = map.release_value();
    }

#if !defined(AK_OS_WINDOWS)
    MUST(Core::System::signal(SIGPIPE, SIG_IGN));
#endif

    Core::EventLoop event_loop;
#ifndef AK_OS_WINDOWS
    Core::EventLoop::register_signal(SIGINT, handle_signal);
    Core::EventLoop::register_signal(SIGTERM, handle_signal);
#else
    if (!SetConsoleCtrlHandler(&handle_console_control, TRUE))
        return Error::from_windows_error();
    ScopeGuard unregister_console_control_handler = [] {
        (void)SetConsoleCtrlHandler(&handle_console_control, FALSE);
    };
    auto windows_stop_timer = Core::Timer::create_repeating(50, [&event_loop] {
        if (InterlockedCompareExchange(&s_windows_quit_requested, 0, 0) != 0)
            event_loop.quit(0);
    });
    windows_stop_timer->start();
#endif

    Optional<HTTP::DiskCache> disk_cache;

    if (http_disk_cache_mode != "disabled"sv) {
        auto mode = TRY([&]() -> ErrorOr<HTTP::DiskCache::Mode> {
            if (http_disk_cache_mode == "enabled"sv)
                return HTTP::DiskCache::Mode::Normal;
            if (http_disk_cache_mode == "partitioned"sv)
                return HTTP::DiskCache::Mode::Partitioned;
            if (http_disk_cache_mode == "testing"sv)
                return HTTP::DiskCache::Mode::Testing;
            return Error::from_string_literal("Unrecognized disk cache mode");
        }());

        if (auto cache = HTTP::DiskCache::create(mode); cache.is_error())
            warnln("Unable to create disk cache: {}", cache.error());
        else
            disk_cache = cache.release_value();
    }

    // Connections are stored on the stack to ensure they are destroyed before static destruction begins. This prevents
    // crashes from notifiers trying to unregister from already-destroyed thread data during process exit.
    RequestServer::ConnectionFromClient::ConnectionMap connections;

    auto client = TRY(IPC::take_over_accepted_client_from_system_server<RequestServer::ConnectionFromClient>(
        mach_server_name,
        RequestServer::ConnectionFromClient::IsPrimaryConnection::Yes, connections, disk_cache));

#if defined(AK_OS_RINOS)
    // RequestServer is a WebContent-owned helper. It inherits the parent's
    // service slot for identity checks, but only WebContent may report health
    // for that slot. Poll the stop gate without issuing a child health report.
    auto service_lifecycle_timer = Core::Timer::create_repeating(50, [&event_loop] {
        // The explicit true result is a stop request; a no-service/error
        // result must not terminate this helper.
        if (rin_service_should_stop() == 1)
            event_loop.quit(0);
    });
    service_lifecycle_timer->start();
#endif

    auto result = event_loop.exec();
#if defined(AK_OS_WINDOWS)
    windows_stop_timer->stop();
#endif
    return result;
}
