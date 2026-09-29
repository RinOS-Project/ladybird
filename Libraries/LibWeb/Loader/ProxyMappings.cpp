/*
 * Copyright (c) 2022, Ali Mohammad Pur <mpfard@serenityos.org>
 *
 * SPDX-License-Identifier: BSD-2-Clause
 */

#include "ProxyMappings.h"
#include <LibURL/Parser.h>

Web::ProxyMappings& Web::ProxyMappings::the()
{
    static ProxyMappings instance {};
    return instance;
}

Web::ProxyMappings::ProxyMappings()
{
#if defined(AK_OS_RINOS)
    // WebContent applies a validated networkd snapshot on the event loop.
    // Until then, requests must not silently bypass the system route.
    m_system_proxy.type = Core::ProxyData::Type::Blocked;
#endif
}

Core::ProxyData Web::ProxyMappings::proxy_for_url(URL::URL const& url) const
{
    auto url_string = url.to_byte_string();
    for (auto& it : m_mappings) {
        if (url_string.matches(it.key)) {
            auto maybe_url = URL::Parser::basic_parse(m_proxies[it.value]);
            if (!maybe_url.has_value()) {
                dbgln("Failed to parse proxy URL: {}", m_proxies[it.value]);
                continue;
            }
            auto result = Core::ProxyData::parse_url(maybe_url.value());
            if (result.is_error()) {
                dbgln("Failed to parse proxy URL: {}", m_proxies[it.value]);
                continue;
            }
            return result.release_value();
        }
    }

#if defined(AK_OS_RINOS)
    return m_system_proxy;
#else
    return {};
#endif
}

void Web::ProxyMappings::set_mappings(Vector<ByteString> proxies, OrderedHashMap<ByteString, size_t> mappings)
{
    m_proxies = move(proxies);
    m_mappings = move(mappings);

    dbgln("Proxy mappings updated: proxies: {}", m_proxies);
}
