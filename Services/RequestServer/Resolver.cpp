/*
 * Copyright (c) 2024, Ali Mohammad Pur <mpfard@serenityos.org>
 * Copyright (c) 2025, Tim Flynn <trflynn89@ladybird.org>
 *
 * SPDX-License-Identifier: BSD-2-Clause
 */

#include <LibTLS/TLSv12.h>
#include <RequestServer/Resolver.h>

namespace RequestServer {

static Vector<ByteString> g_default_certificate_paths;

ByteString const& default_certificate_path()
{
    static ByteString empty_path;
    if (g_default_certificate_paths.is_empty())
        return empty_path;
    return g_default_certificate_paths.first();
}

void set_default_certificate_path(ByteString default_certificate_path)
{
    g_default_certificate_paths.clear();
    if (!default_certificate_path.is_empty())
        g_default_certificate_paths.append(move(default_certificate_path));
}

Vector<ByteString> const& default_certificate_paths()
{
    return g_default_certificate_paths;
}

void set_default_certificate_paths(Vector<ByteString> paths)
{
    g_default_certificate_paths.clear();
    for (auto& path : paths) {
        if (!path.is_empty())
            g_default_certificate_paths.append(move(path));
    }
}

DNSInfo& DNSInfo::the()
{
    static DNSInfo g_dns_info;
    return g_dns_info;
}

NonnullRefPtr<Resolver> Resolver::default_resolver()
{
    static WeakPtr<Resolver> g_resolver {};

    if (auto resolver = g_resolver.strong_ref())
        return *resolver;

    auto resolver = adopt_ref(*new Resolver([] -> ErrorOr<DNS::Resolver::SocketResult> {
        auto& dns_info = DNSInfo::the();

        if (!dns_info.server_address.has_value()) {
            if (!dns_info.server_hostname.has_value())
                return Error::from_string_literal("No DNS server configured");

            auto resolved = TRY(default_resolver()->dns.lookup(*dns_info.server_hostname)->await());
            if (!resolved->has_cached_addresses())
                return Error::from_string_literal("Failed to resolve DNS server hostname");

            auto address = resolved->cached_addresses().first().visit([&](auto& addr) -> Core::SocketAddress { return { addr, dns_info.port }; });
            dns_info.server_address = address;
        }

        if (dns_info.use_dns_over_tls) {
            TLS::Options options;

            options.root_certificates_paths = g_default_certificate_paths;

            return DNS::Resolver::SocketResult {
                MaybeOwned<Core::Socket>(TRY(TLS::TLSv12::connect(*dns_info.server_address, *dns_info.server_hostname, move(options)))),
                DNS::Resolver::ConnectionMode::TCP,
            };
        }

        return DNS::Resolver::SocketResult {
            MaybeOwned<Core::Socket>(TRY(Core::BufferedUDPSocket::create(TRY(Core::UDPSocket::connect(*dns_info.server_address))))),
            DNS::Resolver::ConnectionMode::UDP,
        };
    }));

    g_resolver = resolver;
    return resolver;
}

Resolver::Resolver(Function<ErrorOr<DNS::Resolver::SocketResult>()> create_socket)
    : dns(move(create_socket))
{
}

}
