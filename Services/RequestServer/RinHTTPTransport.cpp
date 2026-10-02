/*
 * RinHTTPTransport - Direct socket-based HTTP/1.1 transport for RinOS
 * SPDX-License-Identifier: BSD-2-Clause
 */

#include <AK/StringBuilder.h>
#include <LibCore/EventLoop.h>
#include <LibCore/Socket.h>
#include <LibCore/SocketAddress.h>
#include <LibTLS/TLSv12.h>
#include <RequestServer/Resolver.h>
#include <RequestServer/RinHTTPTransport.h>
#include <rinhttp/http.h>
#include <requestserver_upload_body_policy.hpp>

#include <limits.h>

#include "rinos_http_transport_policy.h"

namespace RequestServer {

#if defined(AK_OS_RINOS)
static void clear_client_certificate_capability(ByteBuffer& capability)
{
    volatile u8* bytes = capability.data();
    for (size_t index = 0; index < capability.size(); ++index)
        bytes[index] = 0;
    capability.clear();
}
#endif

static ErrorOr<void> socket_write_exact(Core::TCPSocket& socket, ReadonlyBytes bytes)
{
    size_t offset = 0;
    while (offset < bytes.size()) {
        auto written = TRY(socket.write_some(bytes.slice(offset)));
        if (written == 0)
            return Error::from_string_literal("Proxy closed while writing handshake");
        offset += written;
    }
    return {};
}

static ErrorOr<void> socket_read_exact(Core::TCPSocket& socket, Bytes bytes,
                                      int timeout_ms)
{
    size_t offset = 0;
    while (offset < bytes.size()) {
        auto ready = TRY(socket.can_read_without_blocking(timeout_ms));
        if (!ready)
            return Error::from_string_literal("SOCKS5 proxy handshake timed out");
        auto read = TRY(socket.read_some(bytes.slice(offset)));
        if (read.is_empty())
            return Error::from_string_literal("Proxy closed during handshake");
        offset += read.size();
    }
    return {};
}

static ErrorOr<void> socks5_connect(Core::TCPSocket& socket, URL::URL const& url,
                                    long connect_timeout_seconds)
{
    constexpr u8 greeting[] { 5, 1, 0 };
    u8 greeting_reply[2] {};
    int timeout_ms = -1;
    if (connect_timeout_seconds > 0) {
        auto bounded_seconds = min(connect_timeout_seconds, 2147483L);
        timeout_ms = static_cast<int>(bounded_seconds * 1000L);
    }
    TRY(socket_write_exact(socket, ReadonlyBytes { greeting, sizeof(greeting) }));
    TRY(socket_read_exact(socket, Bytes { greeting_reply, sizeof(greeting_reply) }, timeout_ms));
    if (greeting_reply[0] != 5 || greeting_reply[1] != 0)
        return Error::from_string_literal("SOCKS5 proxy rejected no-auth negotiation");

    if (!url.host().has_value())
        return Error::from_string_literal("SOCKS5 target has no host");

    u8 request[262] {};
    size_t request_size = 0;
    request[0] = 5;
    request[1] = 1;
    request[2] = 0;
    if (url.host()->has<IPv4Address>()) {
        request[3] = 1;
        auto address = url.host()->get<IPv4Address>();
        for (size_t index = 0; index < 4; ++index)
            request[4 + index] = address[index];
        request_size = 8;
    } else if (url.host()->has<IPv6Address>()) {
        request[3] = 4;
        auto address = url.host()->get<IPv6Address>();
        for (size_t index = 0; index < 8; ++index) {
            auto piece = address[index];
            request[4 + index * 2] = static_cast<u8>(piece >> 8);
            request[5 + index * 2] = static_cast<u8>(piece);
        }
        request_size = 22;
    } else {
        auto host = url.serialized_host().to_byte_string();
        if (host.is_empty() || host.length() > 255)
            return Error::from_string_literal("SOCKS5 target hostname is outside the protocol bound");
        request[3] = 3;
        request[4] = static_cast<u8>(host.length());
        __builtin_memcpy(request + 5, host.characters(), host.length());
        request_size = 5 + host.length() + 2;
    }

    auto port = url.port_or_default();
    size_t port_offset = request_size - 2;
    request[port_offset] = static_cast<u8>(port >> 8);
    request[port_offset + 1] = static_cast<u8>(port);
    TRY(socket_write_exact(socket, ReadonlyBytes { request, request_size }));

    u8 reply[4] {};
    TRY(socket_read_exact(socket, Bytes { reply, sizeof(reply) }, timeout_ms));
    if (reply[0] != 5 || reply[1] != 0 || reply[2] != 0)
        return Error::from_string_literal("SOCKS5 proxy could not connect to target");

    size_t bound_address_size = 0;
    if (reply[3] == 1)
        bound_address_size = 4;
    else if (reply[3] == 4)
        bound_address_size = 16;
    else if (reply[3] == 3) {
        u8 hostname_size = 0;
        TRY(socket_read_exact(socket, Bytes { &hostname_size, 1 }, timeout_ms));
        bound_address_size = hostname_size;
    } else {
        return Error::from_string_literal("SOCKS5 proxy returned an invalid address type");
    }
    u8 ignored_bound_address[257] {};
    TRY(socket_read_exact(socket, Bytes {
        ignored_bound_address, bound_address_size + 2 }, timeout_ms));
    return {};
}

static ErrorOr<void> http_proxy_connect(Core::TCPSocket& socket,
                                        URL::URL const& url,
                                        long connect_timeout_seconds)
{
    auto host = url.serialized_host().to_byte_string();
    auto port = url.port_or_default();
    StringBuilder builder;
    builder.appendff("CONNECT {}:{} HTTP/1.1\r\nHost: {}:{}\r\nProxy-Connection: keep-alive\r\n\r\n",
                     host, port, host, port);
    auto request = builder.to_byte_string();
    TRY(socket_write_exact(socket, request.bytes()));

    int timeout_ms = -1;
    i64 deadline_ms = 0;
    if (connect_timeout_seconds > 0) {
        auto bounded_seconds = min(connect_timeout_seconds, 2147483L);
        timeout_ms = static_cast<int>(bounded_seconds * 1000L);
        deadline_ms = MonotonicTime::now_coarse().milliseconds() + timeout_ms;
    }

    ByteBuffer response_head;
    constexpr size_t maximum_connect_response_bytes = 16u * 1024u;
    u8 byte = 0;
    while (response_head.size() < maximum_connect_response_bytes) {
        if (deadline_ms != 0) {
            auto remaining_ms = deadline_ms - MonotonicTime::now_coarse().milliseconds();
            if (remaining_ms <= 0)
                return Error::from_string_literal("HTTP proxy CONNECT timed out");
            timeout_ms = static_cast<int>(min(remaining_ms, static_cast<i64>(INT_MAX)));
        }
        auto ready = TRY(socket.can_read_without_blocking(timeout_ms));
        if (!ready)
            return Error::from_string_literal("HTTP proxy CONNECT timed out");
        auto read = TRY(socket.read_some(Bytes { &byte, 1 }));
        if (read.is_empty())
            return Error::from_string_literal("HTTP proxy closed during CONNECT");
        response_head.append(byte);
        auto size = response_head.size();
        if (size >= 4u && response_head[size - 4u] == '\r' &&
            response_head[size - 3u] == '\n' &&
            response_head[size - 2u] == '\r' &&
            response_head[size - 1u] == '\n') {
            RinHttpResponseHead parsed_response {};
            if (size < 4u ||
                rin_http_parse_response_head(response_head.data(), size - 2u,
                                             &parsed_response) != RIN_HTTP_OK ||
                parsed_response.status_code != 200u ||
                (parsed_response.has_content_length &&
                 parsed_response.content_length != 0u) ||
                parsed_response.has_transfer_encoding != 0u)
                return Error::from_string_literal("HTTP proxy rejected CONNECT");
            return {};
        }
    }
    return Error::from_string_literal("HTTP proxy CONNECT response exceeded limit");
}

static int response_connection_close(const uint8_t* data, size_t size)
{
    size_t line_start = 0;
    unsigned line_number = 0;
    while (line_start < size) {
        size_t line_end = line_start;
        while (line_end + 1 < size &&
               !(data[line_end] == '\r' && data[line_end + 1] == '\n'))
            ++line_end;
        if (line_end == size)
            break;

        if (line_number++ != 0) {
            size_t colon = line_start;
            while (colon < line_end && data[colon] != ':')
                ++colon;
            if (colon == line_end)
                return -1;
            if (rin_http_field_name_equal(
                    data + line_start, colon - line_start,
                    reinterpret_cast<const uint8_t*>("connection"), 10) !=
                RIN_HTTP_OK)
                goto next_line;

            auto has_close = rin_http_field_value_has_token(
                data + colon + 1, line_end - colon - 1,
                reinterpret_cast<const uint8_t*>("close"), 5);
            if (has_close == RIN_HTTP_OK)
                return 1;
            if (has_close != RIN_HTTP_NOT_FOUND)
                return -1;
        }

    next_line:
        line_start = line_end + 2;
    }
    return 0;
}

// ============================================================
// Stage 3-C: RinHTTPConnectionPool
// \u540c\u4e00 origin \u3078\u306e\u9023\u7d9a HTTP \u30ea\u30af\u30a8\u30b9\u30c8\u3067 TCP + TLS \u30cf\u30f3\u30c9\u30b7\u30a7\u30a4\u30af\u3092\u7701\u304f\u305f\u3081\u306e
// idle socket \u30ad\u30e3\u30c3\u30b7\u30e5\u3002scheme://host:port \u3054\u3068\u306b LRU \u3067\u6700\u5927 4 \u63a5\u7d9a\u3092\u4fdd\u6301\u3057\u3001
// 30 \u79d2\u9593\u4f7f\u308f\u308c\u306a\u3044\u3068\u81ea\u52d5\u30af\u30ed\u30fc\u30ba\u3002
// ============================================================

RinHTTPConnectionPool& RinHTTPConnectionPool::the()
{
    static RinHTTPConnectionPool s_instance;
    return s_instance;
}

ByteString RinHTTPConnectionPool::make_key(URL::URL const& url, Core::ProxyData const& proxy)
{
    auto host = url.serialized_host().to_byte_string();
    auto port = url.port_or_default();
    StringBuilder b;
    b.appendff("{}://{}:{}", url.scheme(), host, port);
    if (proxy.type == Core::ProxyData::Type::SOCKS5)
        b.appendff("|socks5://{}:{}", proxy.host_ipv4, proxy.port);
    else if (proxy.type == Core::ProxyData::Type::HTTP)
        b.appendff("|http://{}:{}", proxy.host, proxy.port);
    else if (proxy.type == Core::ProxyData::Type::Blocked)
        b.append("|blocked"sv);
    return b.to_byte_string();
}

OwnPtr<Core::BufferedSocketBase> RinHTTPConnectionPool::take(ByteString const& key)
{
    auto it = m_pools.find(key);
    if (it == m_pools.end() || it->value.is_empty())
        return {};
    auto pooled = it->value.take_last();
    if (pooled.idle_timer)
        pooled.idle_timer->stop();
    if (!pooled.socket) {
        dbgln("[RinHTTPPool] take({}): found slot but socket is null", key);
        return {};
    }
    if (pooled.socket->is_eof()) {
        dbgln("[RinHTTPPool] take({}): socket is EOF, discarding", key);
        return {};
    }
    dbgln("[RinHTTPPool] take({}): reusing idle socket (remaining={})", key, it->value.size());
    return move(pooled.socket);
}

void RinHTTPConnectionPool::put(ByteString const& key, OwnPtr<Core::BufferedSocketBase> socket)
{
    if (!socket) {
        dbgln("[RinHTTPPool] put({}): null socket, ignored", key);
        return;
    }
    if (socket->is_eof()) {
        dbgln("[RinHTTPPool] put({}): socket already EOF, not pooling", key);
        return;
    }

    // \u6d41\u308c\u3066\u304d\u305f\u30d0\u30a4\u30c8\u3092\u898b\u306a\u3044\u3088\u3046\u306b on_ready_to_read \u3092\u5916\u3059\u3002
    socket->on_ready_to_read = nullptr;

    if (m_next_entry_id == 0 || m_next_entry_id == UINT64_MAX) {
        dbgln("[RinHTTPPool] put({}): entry id exhausted, not pooling", key);
        return;
    }

    PooledSocket pooled;
    pooled.entry_id = m_next_entry_id++;
    pooled.socket = move(socket);

    auto& slots = m_pools.ensure(key);
    if (slots.size() >= MAX_PER_HOST) {
        // LRU: \u53e4\u3044\u65b9 (vector \u5148\u982d) \u3092\u7834\u68c4\u3002
        dbgln("[RinHTTPPool] put({}): pool full ({}), evicting oldest", key, slots.size());
        if (slots[0].idle_timer)
            slots[0].idle_timer->stop();
        slots.remove(0);
    }

    auto key_copy = key;
    auto entry_id = pooled.entry_id;
    pooled.idle_timer = Core::Timer::create_single_shot(IDLE_TIMEOUT_MS, [this, key_copy, entry_id]() mutable {
        auto it = m_pools.find(key_copy);
        if (it == m_pools.end())
            return;
        for (size_t index = 0; index < it->value.size(); ++index) {
            if (it->value[index].entry_id != entry_id)
                continue;
            it->value.remove(index);
            dbgln("[RinHTTPPool] idle timeout({}): dropped entry {}, remaining={}", key_copy, entry_id, it->value.size());
            if (it->value.is_empty())
                m_pools.remove(it);
            return;
        }
    });
    pooled.idle_timer->start();
    slots.append(move(pooled));
    dbgln("[RinHTTPPool] put({}): pooled, total={}", key, slots.size());
}

void RinHTTPConnectionPool::evict_all(ByteString const& key)
{
    auto it = m_pools.find(key);
    if (it == m_pools.end())
        return;
    for (auto& pooled : it->value) {
        if (pooled.idle_timer)
            pooled.idle_timer->stop();
    }
    m_pools.remove(it);
    dbgln("[RinHTTPPool] evict_all({})", key);
}

// ============================================================
// RinHTTPFetch
// ============================================================

RinHTTPFetch::RinHTTPFetch()
    : m_start_time(MonotonicTime::now())
{
}

RinHTTPFetch::~RinHTTPFetch()
{
    cancel();
}

#if defined(AK_OS_RINOS)
int RinHTTPFetch::client_certificate_provider(rintls_ctx* tls, void* opaque)
{
    auto* fetch = static_cast<RinHTTPFetch*>(opaque);
    if (fetch == nullptr || tls == nullptr ||
        !fetch->m_client_certificate_provider ||
        !fetch->m_client_certificate_signer)
        return RINTLS_ERR_CERTIFICATE;

    rintls_client_certificate_request request {};
    request.struct_size = sizeof(request);
    if (rintls_get_client_certificate_request(tls, &request) != RINTLS_OK)
        return RINTLS_ERR_CERTIFICATE;
    if (request.version != RINTLS_CLIENT_CERTIFICATE_REQUEST_VERSION ||
        request.reserved != 0u ||
        !rin_requestserver_tls_client_certificate_constraints_valid(
            request.signature_algorithms, request.signature_algorithms_size,
            request.signature_algorithms_cert,
            request.signature_algorithms_cert_size,
            request.certificate_authorities,
            request.certificate_authorities_size))
        return RINTLS_ERR_CERTIFICATE;

    u64 connection_generation = 0u;
    u16 signature_scheme = 0u;
    ByteBuffer certificate_list;
    ByteBuffer signer_capability;
    const bool provided = fetch->m_client_certificate_provider(
        request, connection_generation, signature_scheme, certificate_list,
        signer_capability);
    if (!provided || !rin_requestserver_tls_client_certificate_ipc_valid(
            fetch->m_request_id, connection_generation, certificate_list.data(),
            certificate_list.size(), signer_capability.data(),
            signer_capability.size()) ||
        !rin_requestserver_tls_signature_scheme_offered(
            request.signature_algorithms, request.signature_algorithms_size,
            signature_scheme)) {
        clear_client_certificate_capability(signer_capability);
        return RINTLS_ERR_CERTIFICATE;
    }

    rin_requestserver_tls_client_certificate_session_reset(
        &fetch->m_client_certificate_session);
    if (!rin_requestserver_tls_client_certificate_session_bind(
            &fetch->m_client_certificate_session, fetch->m_request_id,
            connection_generation, certificate_list.data(),
            certificate_list.size(), signer_capability.data(),
            signer_capability.size(),
            &RinHTTPFetch::client_certificate_session_sign, fetch) ||
        !rin_requestserver_tls_client_certificate_session_start(
            &fetch->m_client_certificate_session)) {
        clear_client_certificate_capability(signer_capability);
        return RINTLS_ERR_CERTIFICATE;
    }
    if (rintls_set_client_certificate_for_scheme(
            tls, certificate_list.data(), certificate_list.size(),
            &RinHTTPFetch::client_certificate_signer, fetch,
            signature_scheme) != RINTLS_OK) {
        rin_requestserver_tls_client_certificate_session_reset(
            &fetch->m_client_certificate_session);
        return RINTLS_ERR_CERTIFICATE;
    }
    return RINTLS_OK;
}

int RinHTTPFetch::client_certificate_session_sign(
    void* opaque, const uint8_t capability[], uint64_t request_id,
    uint64_t connection_generation, uint16_t signature_scheme,
    const uint8_t* message, size_t message_size, uint8_t* signature,
    size_t signature_capacity, size_t* signature_size)
{
    auto* fetch = static_cast<RinHTTPFetch*>(opaque);
    if (signature_size != nullptr)
        *signature_size = 0u;
    if (fetch == nullptr || request_id != fetch->m_request_id ||
        !fetch->m_client_certificate_signer || capability == nullptr ||
        message == nullptr || message_size == 0u ||
        signature == nullptr || signature_capacity == 0u ||
        signature_size == nullptr)
        return -1;

    size_t written = 0u;
    auto result = fetch->m_client_certificate_signer(
        connection_generation,
        ReadonlyBytes { capability,
                        RINRUNTIME_TLS_CLIENT_CERTIFICATE_CAPABILITY_BYTES },
        signature_scheme, ReadonlyBytes { message, message_size },
        Bytes { signature, signature_capacity }, written);
    if (result != 0 || written == 0u || written > signature_capacity) {
        __builtin_memset(
            signature, 0,
            signature_capacity < 512u ? signature_capacity : 512u);
        return -1;
    }
    *signature_size = written;
    return 0;
}

int RinHTTPFetch::client_certificate_signer(
    void* opaque, u16 signature_scheme, const u8* message,
    rin_size_t message_length, u8* signature, rin_size_t signature_capacity,
    rin_size_t* signature_length)
{
    auto* fetch = static_cast<RinHTTPFetch*>(opaque);
    if (signature_length != nullptr)
        *signature_length = 0u;
    if (fetch == nullptr || message == nullptr ||
        message_length == 0u || message_length > 16u * 1024u ||
        signature == nullptr || signature_capacity == 0u ||
        signature_capacity > 512u || signature_length == nullptr) {
        if (fetch != nullptr)
            rin_requestserver_tls_client_certificate_session_reset(
                &fetch->m_client_certificate_session);
        if (signature != nullptr && signature_capacity != 0u)
            __builtin_memset(
                signature, 0,
                signature_capacity < 512u ? signature_capacity : 512u);
        return -1;
    }

    size_t written = 0u;
    auto result = rin_requestserver_tls_client_certificate_session_sign(
        &fetch->m_client_certificate_session, signature_scheme,
        message, message_length, signature, signature_capacity, &written);
    if (result != 0) {
        rin_requestserver_tls_client_certificate_session_reset(
            &fetch->m_client_certificate_session);
        return -1;
    }
    *signature_length = written;
    return 0;
}
#endif

void RinHTTPFetch::cancel()
{
    if (m_timeout_timer)
        m_timeout_timer->stop();
    m_timeout_timer = nullptr;
    if (m_idle_timer)
        m_idle_timer->stop();
    m_idle_timer = nullptr;
    if (m_socket) {
        m_socket->on_ready_to_read = nullptr;
        m_socket = nullptr;
    }
#if defined(AK_OS_RINOS)
    rin_requestserver_tls_client_certificate_session_reset(
        &m_client_certificate_session);
#endif
}

ErrorOr<NonnullOwnPtr<RinHTTPFetch>> RinHTTPFetch::create(
    u64 request_id,
    URL::URL const& url,
    ByteString const& method,
    HTTP::HeaderList const& request_headers,
    ReadonlyBytes request_body,
    RefPtr<DNS::LookupResult const> dns_result,
    long connect_timeout_seconds,
    ClientCertificateProvider client_certificate_provider,
    ClientCertificateSigner client_certificate_signer,
    Core::ProxyData proxy_data)
{
    RequestBodySource source;
    source.expected_length = request_body.size();
    source.read = [request_body, offset = size_t { 0 }](u8* buffer,
                                                         size_t capacity) mutable
        -> ErrorOr<size_t> {
        if (!buffer || capacity == 0 || offset >= request_body.size())
            return size_t { 0 };
        auto count = min(capacity, request_body.size() - offset);
        __builtin_memcpy(buffer, request_body.data() + offset, count);
        offset += count;
        return count;
    };
    return create(request_id, url, method, request_headers, move(source),
                  dns_result, connect_timeout_seconds,
                  move(client_certificate_provider),
                  move(client_certificate_signer), move(proxy_data));
}

ErrorOr<NonnullOwnPtr<RinHTTPFetch>> RinHTTPFetch::create(
    u64 request_id,
    URL::URL const& url,
    ByteString const& method,
    HTTP::HeaderList const& request_headers,
    RequestBodySource request_body,
    RefPtr<DNS::LookupResult const> dns_result,
    long connect_timeout_seconds,
    ClientCertificateProvider client_certificate_provider,
    ClientCertificateSigner client_certificate_signer,
    Core::ProxyData proxy_data)
{
    auto fetch = adopt_own(*new RinHTTPFetch());

    fetch->m_request_id = request_id;
    fetch->m_client_certificate_provider = move(client_certificate_provider);
    fetch->m_client_certificate_signer = move(client_certificate_signer);
    fetch->m_disable_pooling = !!fetch->m_client_certificate_provider;

    auto serialized_url = url.to_byte_string();
    if (rin_http_redirect_target_valid(
            reinterpret_cast<const uint8_t*>(serialized_url.characters()),
            serialized_url.length()) != RIN_HTTP_OK)
        return Error::from_string_literal("Invalid HTTP(S) URL for RinHTTP transport");

    if (!RinRequestServerUploadPolicy::admission_valid(request_body.expected_length, !!request_body.read))
        return Error::from_string_literal("Request body exceeds RinOS limit");
    fetch->m_request_body_length = request_body.expected_length;
    fetch->m_request_body_read = move(request_body.read);

    bool using_socks5 = proxy_data.type == Core::ProxyData::Type::SOCKS5;
    bool using_http_proxy = proxy_data.type == Core::ProxyData::Type::HTTP;
    if (proxy_data.type == Core::ProxyData::Type::Blocked)
        return Error::from_string_literal("System proxy configuration is unavailable");
    if (proxy_data.type != Core::ProxyData::Type::Direct && !using_socks5 &&
        !using_http_proxy)
        return Error::from_string_literal("Unsupported proxy type");
    if (using_socks5 && (proxy_data.port == 0 || proxy_data.host_ipv4 == IPv4Address {}))
        return Error::from_string_literal("Invalid SOCKS5 proxy endpoint");
    if (using_http_proxy && (proxy_data.host.is_empty() || proxy_data.port == 0))
        return Error::from_string_literal("Invalid HTTP proxy endpoint");
    if (!using_socks5 && (!dns_result || dns_result->is_empty() || !dns_result->has_cached_addresses()))
        return Error::from_string_literal("No DNS result for HTTP fetch");

    auto host = url.serialized_host().to_byte_string();
    auto port = url.port_or_default();
    bool is_https = url.scheme() == "https"sv;

    // Stage 3-C: \u30d7\u30fc\u30eb\u304b\u3089 idle socket \u3092\u53d6\u308a\u51fa\u305b\u306a\u3044\u304b\u8a66\u3059\u3002
    fetch->m_pool_key = RinHTTPConnectionPool::make_key(url, proxy_data);
    auto pooled = fetch->m_disable_pooling
        ? OwnPtr<Core::BufferedSocketBase> {}
        : RinHTTPConnectionPool::the().take(fetch->m_pool_key);
    if (pooled) {
        dbgln("[RinHTTP] reusing pooled socket for {}", fetch->m_pool_key);
        fetch->m_socket = move(pooled);
        fetch->m_reused_from_pool = true;
        // \u30cf\u30f3\u30c9\u30b7\u30a7\u30a4\u30af\u3092\u98db\u3070\u3057\u305f\u306e\u3067 connect / secure_connect \u306f 0 \u6271\u3044\u3002
        fetch->m_connect_end_us = (MonotonicTime::now() - fetch->m_start_time).to_microseconds();
        if (is_https)
            fetch->m_secure_connect_start_us = fetch->m_connect_end_us;
    } else {
        // Build SocketAddress from DNS result
        Core::SocketAddress socket_address;
        if (using_socks5) {
            socket_address = Core::SocketAddress { proxy_data.host_ipv4, proxy_data.port };
        } else {
            auto const& addresses = dns_result->cached_addresses();
            auto destination_port = using_http_proxy ? proxy_data.port : port;
            socket_address = addresses.first().visit(
                [&](IPv4Address const& v4) -> Core::SocketAddress { return { v4, destination_port }; },
                [&](IPv6Address const& v6) -> Core::SocketAddress { return { v6, destination_port }; });
        }

        auto tcp_socket = TRY(Core::TCPSocket::connect(socket_address));
        if (using_socks5)
            TRY(socks5_connect(*tcp_socket, url, connect_timeout_seconds));
        else if (using_http_proxy && is_https)
            TRY(http_proxy_connect(*tcp_socket, url, connect_timeout_seconds));

        if (is_https) {
            TLS::Options tls_options;
            tls_options.root_certificates_paths = default_certificate_paths();
#if defined(AK_OS_RINOS)
            if (fetch->m_client_certificate_provider) {
                tls_options.client_certificate_provider =
                    &RinHTTPFetch::client_certificate_provider;
                tls_options.client_certificate_provider_opaque = fetch.ptr();
            }
#endif

            auto tls_socket = TRY(TLS::TLSv12::connect(move(tcp_socket), host, move(tls_options)));
            fetch->m_connect_end_us = (MonotonicTime::now() - fetch->m_start_time).to_microseconds();
            fetch->m_secure_connect_start_us = fetch->m_connect_end_us;

            fetch->m_socket = TRY(Core::BufferedSocket<TLS::TLSv12>::create(move(tls_socket)));
        } else {
            TRY(tcp_socket->set_blocking(false));
            fetch->m_connect_end_us = (MonotonicTime::now() - fetch->m_start_time).to_microseconds();

            fetch->m_socket = TRY(Core::BufferedTCPSocket::create(move(tcp_socket)));
        }
    }

    // Send request
    fetch->m_request_start_us = (MonotonicTime::now() - fetch->m_start_time).to_microseconds();
    TRY(fetch->send_request(url, method, request_headers,
                            using_http_proxy && !is_https));

    // 接続確立タイマー: 相手のデータが届くまでを監視。最初の read で停止する。
    if (connect_timeout_seconds > 0) {
        fetch->m_timeout_timer = Core::Timer::create_single_shot(static_cast<int>(connect_timeout_seconds * 1000), [raw = fetch.ptr()] {
            raw->finish_with_error(RIN_HTTP_TRANSPORT_RESULT_TIMEOUT);
        });
        fetch->m_timeout_timer->start();
    }

    // Idle タイマー: 受信が一定時間止まったらタイムアウト。
    // 60 秒の連続無通信で切断する。データが流れるたびに restart する。
    // ハンドシェイク直後にサーバが黙るケースにも備えて create() 時点で start しておく。
    fetch->m_idle_timer = Core::Timer::create_single_shot(60 * 1000, [raw = fetch.ptr()] {
        dbgln("[RinHTTP] idle_timer fired -> transport timeout");
        raw->finish_with_error(RIN_HTTP_TRANSPORT_RESULT_TIMEOUT);
    });
    fetch->m_idle_timer->start();
    dbgln("[RinHTTP] fetch created, idle_timer armed (60s)");

    // Set up async read
    fetch->m_socket->on_ready_to_read = [raw = fetch.ptr()] {
        raw->on_socket_ready_to_read();
    };

    return fetch;
}

ErrorOr<void> RinHTTPFetch::send_request(URL::URL const& url,
                                         ByteString const& method,
                                         HTTP::HeaderList const& request_headers,
                                         bool absolute_form)
{
    StringBuilder builder;

    auto resource = url.serialize_path();
    if (absolute_form) {
        auto host = url.serialized_host().to_byte_string();
        auto port = url.port_or_default();
        bool is_default_port = port == 80u;
        builder.appendff("{} http://{}", method, host);
        if (!is_default_port)
            builder.appendff(":{}", port);
        builder.append(resource);
        if (url.query().has_value())
            builder.appendff("?{}", *url.query());
        builder.append(" HTTP/1.1\r\n"sv);
    } else if (url.query().has_value()) {
        builder.appendff("{} {}?{} HTTP/1.1\r\n", method, resource, *url.query());
    } else {
        builder.appendff("{} {} HTTP/1.1\r\n", method, resource);
    }

    // Host header
    auto host = url.serialized_host().to_byte_string();
    auto port = url.port_or_default();
    bool is_default_port = (url.scheme() == "http"sv && port == 80) || (url.scheme() == "https"sv && port == 443);
    if (is_default_port)
        builder.appendff("Host: {}\r\n", host);
    else
        builder.appendff("Host: {}:{}\r\n", host, port);

    for (auto const& header : request_headers) {
        if (header.value.is_empty())
            builder.appendff("{}:\r\n", header.name);
        else
            builder.appendff("{}: {}\r\n", header.name, header.value);
    }

    if (!request_headers.contains("Accept-Encoding"sv))
        builder.append("Accept-Encoding: identity\r\n"sv);

    if (m_request_body_length != 0u)
        builder.appendff("Content-Length: {}\r\n", m_request_body_length);

    builder.append("\r\n"sv);

    auto header_bytes = builder.to_byte_string();
    if (auto result = m_socket->write_until_depleted(header_bytes.bytes()); result.is_error()) {
        dbgln("[RinHTTP] request header write failed: {}", result.error());
        return result.release_error();
    }

    if (m_request_body_read) {
        constexpr size_t body_chunk_bytes = 64u * 1024u;
        u8 body_chunk[body_chunk_bytes];
        size_t body_written = 0u;
        while (body_written < m_request_body_length) {
            auto remaining = m_request_body_length - body_written;
            auto requested = static_cast<size_t>(min(
                remaining, static_cast<u64>(body_chunk_bytes)));
            auto read_result = m_request_body_read(body_chunk, requested);
            if (read_result.is_error()) {
                dbgln("[RinHTTP] request body source failed: {}", read_result.error());
                return read_result.release_error();
            }
            auto count = read_result.value();
            if (!RinRequestServerUploadPolicy::chunk_result_valid(body_written,
                                                                  m_request_body_length,
                                                                  static_cast<u32>(requested),
                                                                  count)) {
                dbgln("[RinHTTP] request body source returned invalid count {}", count);
                return Error::from_string_literal("Request body source was incomplete");
            }
            if (auto result = m_socket->write_until_depleted(
                    ReadonlyBytes { body_chunk, count }); result.is_error()) {
                dbgln("[RinHTTP] request body write failed: {}", result.error());
                return result.release_error();
            }
            body_written += count;
        }

        // A declared Content-Length is not enough on its own: the producer
        // must also prove that the source reached EOF at exactly that
        // boundary. This catches a portal object that grew after its initial
        // size check instead of silently truncating the upload.
        auto eof_probe = m_request_body_read(body_chunk, 1u);
        if (eof_probe.is_error()) {
            dbgln("[RinHTTP] request body EOF probe failed: {}", eof_probe.error());
            return eof_probe.release_error();
        }
        if (RinRequestServerUploadPolicy::trailing_bytes_rejected(body_written,
                                                                  m_request_body_length,
                                                                  eof_probe.value())) {
            dbgln("[RinHTTP] request body source has bytes beyond Content-Length");
            return Error::from_string_literal("Request body source has trailing bytes");
        }
    }
    return {};
}

void RinHTTPFetch::on_socket_ready_to_read()
{
    if (m_response_state == ResponseState::Complete || m_response_state == ResponseState::Error)
        return;

    // 初回の read で「接続確立」フェーズは終了。累積タイマーを停止し、
    // 以降は idle timer だけでフロー中断を検知する。
    if (!m_connect_timer_stopped) {
        m_connect_timer_stopped = true;
        if (m_timeout_timer) {
            m_timeout_timer->stop();
            m_timeout_timer = nullptr;
        }
    }
    if (m_idle_timer)
        m_idle_timer->restart();

    if (!m_socket || m_socket->is_eof()) {
        if (m_response_state == ResponseState::Body && !m_has_content_length && !m_chunked_encoding) {
            finish_success();
            return;
        }
        finish_with_error(RIN_HTTP_TRANSPORT_RESULT_INCOMPLETE_RESPONSE);
        return;
    }

    // 1 回の ready_to_read 呼び出しで読み込む上限。これを超えたら break し、
    // EventLoop を回して下流 (RequestPipe writer / WebContent consumer) がドレインできる
    // ようにする。未処理の TCP/TLS データが残っていれば kernel から再び on_ready_to_read
    // が発火する。この上限が無いと、インナーループが永久に回って producer がメモリを
    // 二次関数的に積み上げ OOM に至る (旧実装の RequestServer クラッシュの原因)。
    constexpr size_t MAX_BYTES_PER_INVOCATION = 256 * 1024;
    constexpr int MAX_ITERATIONS_PER_INVOCATION = 16;

    u8 buf[65536];
    size_t bytes_read_this_invocation = 0;
    int iterations = 0;
    while (m_response_state != ResponseState::Complete && m_response_state != ResponseState::Error) {
        if (iterations >= MAX_ITERATIONS_PER_INVOCATION || bytes_read_this_invocation >= MAX_BYTES_PER_INVOCATION)
            break;

        iterations++;
        auto result = m_socket->read_some({ buf, sizeof(buf) });
        if (result.is_error()) {
            auto err = result.release_error();
            dbgln("[RinHTTP] read_some error at iter={}: {} (state={})",
                iterations, err, (int)m_response_state);
            if (err.is_errno() && rin_http_transport_errno_is_retryable(err.code()))
                break;
            finish_with_error(RIN_HTTP_TRANSPORT_RESULT_INCOMPLETE_RESPONSE);
            return;
        }

        auto bytes = result.value();
        if (bytes.is_empty()) {
            if (m_socket->is_eof()) {
                if (m_response_state == ResponseState::Body && !m_has_content_length && !m_chunked_encoding)
                    finish_success();
                else if (m_response_state == ResponseState::Body && m_has_content_length && m_body_bytes_received >= m_content_length)
                    finish_success();
            }
            break;
        }
        bytes_read_this_invocation += bytes.size();

        if (m_response_state == ResponseState::StatusLine || m_response_state == ResponseState::Headers)
            process_line_buffered(bytes);
        else if (m_response_state == ResponseState::Body)
            process_body_data(bytes);
        else if (m_response_state == ResponseState::ChunkedSize || m_response_state == ResponseState::ChunkedData || m_response_state == ResponseState::ChunkedDataTerminator || m_response_state == ResponseState::ChunkedTrailer)
            process_chunked_data(bytes);
    }
}

void RinHTTPFetch::process_line_buffered(ReadonlyBytes data)
{
    // Feed data into line buffer, extracting complete lines delimited by \r\n.
    // For each complete line, call the appropriate parser.
    // When headers end (empty line), leftover data is forwarded to body processing.
    size_t offset = 0;
    while (offset < data.size()) {
        auto byte = data[offset++];
        if (!rin_http_transport_line_byte_fits(m_line_buffer.size())) {
            dbgln("[RinHTTP] response line exceeds limit");
            finish_with_error(RIN_HTTP_TRANSPORT_RESULT_MALFORMED_RESPONSE);
            return;
        }
        m_line_buffer.append(byte);

        // Check for \r\n at the end of line buffer
        if (m_line_buffer.size() >= 2 && m_line_buffer[m_line_buffer.size() - 2] == '\r' && m_line_buffer[m_line_buffer.size() - 1] == '\n') {
            // Complete line (without \r\n)
            auto line = StringView { m_line_buffer.data(), m_line_buffer.size() - 2 };

            if (m_response_state == ResponseState::StatusLine) {
                if (m_response_head_buffer.size() >
                    RIN_HTTP_MAX_RESPONSE_HEADER_BYTES - m_line_buffer.size()) {
                    dbgln("[RinHTTP] response head exceeds limit");
                    finish_with_error(RIN_HTTP_TRANSPORT_RESULT_MALFORMED_RESPONSE);
                    return;
                }
                m_response_head_buffer.append(m_line_buffer);
                m_response_state = ResponseState::Headers;
            } else if (m_response_state == ResponseState::Headers) {
                if (line.is_empty()) {
                    if (m_response_head_buffer.size() < 2u) {
                        dbgln("[RinHTTP] response head is empty");
                        finish_with_error(RIN_HTTP_TRANSPORT_RESULT_MALFORMED_RESPONSE);
                        return;
                    }

                    RinHttpResponseHead parsed_head;
                    auto head_size = m_response_head_buffer.size() - 2u;
                    if (rin_http_parse_response_head(
                            m_response_head_buffer.data(), head_size,
                            &parsed_head) != RIN_HTTP_OK ||
                        (parsed_head.has_content_length &&
                         parsed_head.content_length > SIZE_MAX)) {
                        dbgln("[RinHTTP] malformed response head");
                        finish_with_error(RIN_HTTP_TRANSPORT_RESULT_MALFORMED_RESPONSE);
                        return;
                    }

                    auto connection_close = response_connection_close(
                        m_response_head_buffer.data(), head_size);
                    if (connection_close < 0) {
                        dbgln("[RinHTTP] malformed Connection response field");
                        finish_with_error(RIN_HTTP_TRANSPORT_RESULT_MALFORMED_RESPONSE);
                        return;
                    }

                    m_status_code = parsed_head.status_code;
                    m_response_connection_close = connection_close > 0;
                    if (parsed_head.has_content_length)
                        m_content_length = static_cast<size_t>(parsed_head.content_length);
                    m_has_content_length = parsed_head.has_content_length != 0;
                    m_chunked_encoding = parsed_head.transfer_encoding_chunked != 0;
                    m_response_start_us = (MonotonicTime::now() - m_start_time).to_microseconds();

                    // Keep the existing curl-compatible callback contract,
                    // but replay only after the complete head is validated.
                    size_t callback_offset = 0;
                    while (callback_offset < head_size) {
                        size_t callback_end = callback_offset;
                        while (callback_end + 1u < head_size &&
                               !(m_response_head_buffer[callback_end] == '\r' &&
                                 m_response_head_buffer[callback_end + 1u] == '\n'))
                            ++callback_end;
                        if (callback_end + 1u >= head_size)
                            break;
                        if (on_header_received)
                            on_header_received(m_response_head_buffer.data() + callback_offset,
                                               1, callback_end + 2u - callback_offset,
                                               callback_user_data);
                        callback_offset = callback_end + 2u;
                    }
                    if (on_header_received) {
                        static constexpr char blank_line[] = "\r\n";
                        on_header_received(const_cast<char*>(blank_line), 1, 2,
                                           callback_user_data);
                    }

                    m_response_state = m_chunked_encoding ? ResponseState::ChunkedSize : ResponseState::Body;
                    m_line_buffer.clear();

                    // Content-Length: 0 の場合はこの時点で完了（body データは発生しない）
                    if (m_response_state == ResponseState::Body && m_has_content_length && m_content_length == 0) {
                        finish_success();
                        return;
                    }

                    // Forward remaining data to body parser
                    if (offset < data.size()) {
                        auto remaining = data.slice(offset);
                        if (m_response_state == ResponseState::Body)
                            process_body_data(remaining);
                        else
                            process_chunked_data(remaining);
                    }
                    return;
                }

                if (m_response_head_buffer.size() >
                    RIN_HTTP_MAX_RESPONSE_HEADER_BYTES - m_line_buffer.size()) {
                    dbgln("[RinHTTP] response head exceeds limit");
                    finish_with_error(RIN_HTTP_TRANSPORT_RESULT_MALFORMED_RESPONSE);
                    return;
                }
                m_response_head_buffer.append(m_line_buffer);
            }
            m_line_buffer.clear();
        }
    }
}

void RinHTTPFetch::process_body_data(ReadonlyBytes data)
{
    if (data.is_empty())
        return;

    if (m_has_content_length &&
        !rin_http_transport_body_append_is_within_declared_length(
            m_body_bytes_received, data.size(), m_content_length)) {
        dbgln("[RinHTTP] response body exceeds declared Content-Length");
        finish_with_error(RIN_HTTP_TRANSPORT_RESULT_MALFORMED_RESPONSE);
        return;
    }

    m_body_bytes_received += data.size();

    if (on_data_received)
        on_data_received(const_cast<u8*>(data.data()), 1, data.size(), callback_user_data);

    if (m_has_content_length && m_body_bytes_received >= m_content_length)
        finish_success();
}

void RinHTTPFetch::process_chunked_data(ReadonlyBytes data)
{
    size_t offset = 0;
    while (offset < data.size() && m_response_state != ResponseState::Complete && m_response_state != ResponseState::Error) {
        if (m_response_state == ResponseState::ChunkedSize) {
            // Read chunk size line character by character
            auto byte = data[offset++];
            if (!rin_http_transport_line_byte_fits(m_line_buffer.size())) {
                dbgln("[RinHTTP] chunk-size line exceeds limit");
                finish_with_error(RIN_HTTP_TRANSPORT_RESULT_MALFORMED_RESPONSE);
                return;
            }
            m_line_buffer.append(byte);
            if (m_line_buffer.size() >= 2 && m_line_buffer[m_line_buffer.size() - 2] == '\r' && m_line_buffer[m_line_buffer.size() - 1] == '\n') {
                auto line = StringView { m_line_buffer.data(), m_line_buffer.size() - 2 };
                // Chunk size may have extensions after ';', ignore them
                auto semicolon = line.find(';');
                auto size_str = semicolon.has_value() ? line.substring_view(0, *semicolon) : line;

                if (size_str.is_empty()) {
                    dbgln("[RinHTTP] empty chunk size");
                    finish_with_error(RIN_HTTP_TRANSPORT_RESULT_MALFORMED_RESPONSE);
                    return;
                }

                size_t chunk_size = 0;
                for (auto ch : size_str) {
                    if (!rin_http_transport_hex_size_append(chunk_size, ch,
                            &chunk_size)) {
                        dbgln("[RinHTTP] invalid or overflowing chunk size");
                        finish_with_error(RIN_HTTP_TRANSPORT_RESULT_MALFORMED_RESPONSE);
                        return;
                    }
                }
                m_current_chunk_remaining = chunk_size;

                m_line_buffer.clear();

                if (m_current_chunk_remaining == 0) {
                    m_response_state = ResponseState::ChunkedTrailer;
                } else {
                    m_response_state = ResponseState::ChunkedData;
                }
            }
        } else if (m_response_state == ResponseState::ChunkedData) {
            auto available = data.size() - offset;
            auto to_consume = min(available, m_current_chunk_remaining);
            auto chunk = data.slice(offset, to_consume);
            offset += to_consume;
            m_current_chunk_remaining -= to_consume;
            m_body_bytes_received += to_consume;

            if (on_data_received)
                on_data_received(const_cast<u8*>(chunk.data()), 1, chunk.size(), callback_user_data);

            if (m_current_chunk_remaining == 0) {
                // Every non-final chunk is followed by a CRLF which is not part of
                // the next chunk-size line. Keep this as a distinct state because
                // either byte can arrive in a later socket read.
                m_chunk_data_terminator_bytes = 0;
                m_response_state = ResponseState::ChunkedDataTerminator;
            }
        } else if (m_response_state == ResponseState::ChunkedDataTerminator) {
            static constexpr u8 expected_terminator[] { '\r', '\n' };
            while (offset < data.size() && m_chunk_data_terminator_bytes < sizeof(expected_terminator)) {
                if (data[offset++] != expected_terminator[m_chunk_data_terminator_bytes]) {
                    dbgln("[RinHTTP] invalid chunk data terminator");
                    finish_with_error(RIN_HTTP_TRANSPORT_RESULT_MALFORMED_RESPONSE);
                    return;
                }
                ++m_chunk_data_terminator_bytes;
            }

            if (m_chunk_data_terminator_bytes == sizeof(expected_terminator)) {
                m_chunk_data_terminator_bytes = 0;
                m_response_state = ResponseState::ChunkedSize;
            }
        } else if (m_response_state == ResponseState::ChunkedTrailer) {
            // After the 0-length chunk, read trailer headers until we see an empty line (\r\n)
            auto byte = data[offset++];
            if (!rin_http_transport_line_byte_fits(m_line_buffer.size())) {
                dbgln("[RinHTTP] chunk trailer line exceeds limit");
                finish_with_error(RIN_HTTP_TRANSPORT_RESULT_MALFORMED_RESPONSE);
                return;
            }
            m_line_buffer.append(byte);
            if (m_line_buffer.size() >= 2 && m_line_buffer[m_line_buffer.size() - 2] == '\r' && m_line_buffer[m_line_buffer.size() - 1] == '\n') {
                auto line = StringView { m_line_buffer.data(), m_line_buffer.size() - 2 };
                m_line_buffer.clear();
                if (line.is_empty()) {
                    // End of chunked transfer
                    finish_success();
                    return;
                }
                // Trailer header — ignore for now
            }
        }
    }
}

void RinHTTPFetch::finish_with_error(int code)
{
    dbgln("[RinHTTP] finish_with_error code={} state={} body={}/{} reused={}",
        code, (int)m_response_state, m_body_bytes_received, m_content_length, m_reused_from_pool);
    m_response_state = ResponseState::Error;
    m_response_end_us = (MonotonicTime::now() - m_start_time).to_microseconds();

    // Stage 3-C: \u30d7\u30fc\u30eb\u304b\u3089\u53d6\u308a\u51fa\u3057\u305f socket \u304c\u5931\u6557\u3057\u305f\u306a\u3089\u3001
    // \u540c\u4e00\u30db\u30b9\u30c8\u306e\u4ed6\u306e idle socket \u3082\u5207\u3089\u308c\u3066\u3044\u308b\u53ef\u80fd\u6027\u304c\u9ad8\u3044\u306e\u3067\u5168\u6383\u3059\u308b\u3002
    if (m_reused_from_pool && !m_pool_key.is_empty()) {
        dbgln("[RinHTTP] pooled socket reuse failed, evicting pool for {}", m_pool_key);
        RinHTTPConnectionPool::the().evict_all(m_pool_key);
    }

    cancel();
    if (on_complete)
        on_complete(code);
}

void RinHTTPFetch::finish_success()
{
    dbgln("[RinHTTP] finish_success body={} status={} conn_close={} reused={}",
        m_body_bytes_received, m_status_code, m_response_connection_close, m_reused_from_pool);
    m_response_state = ResponseState::Complete;
    m_response_end_us = (MonotonicTime::now() - m_start_time).to_microseconds();
    if (m_timeout_timer) {
        m_timeout_timer->stop();
        m_timeout_timer = nullptr;
    }
    if (m_idle_timer) {
        m_idle_timer->stop();
        m_idle_timer = nullptr;
    }

    // Stage 3-C: \u30d7\u30fc\u30eb\u304c\u898b\u308b\u306e\u306f \"\u30ec\u30b9\u30dd\u30f3\u30b9\u30dc\u30c7\u30a3\u304c\u30ad\u30ea\u898b\u3048\u3066\u3044\u308b\" \u304b\u3064
    // \"\u30b5\u30fc\u30d0\u304c Connection: close \u3092\u9001\u3063\u3066\u3044\u306a\u3044\" \u3068\u304d\u306e\u307f\u3002
    // Content-Length \u4ed8\u304d\u304b chunked encoding \u7d42\u4e86\u3057\u305f\u5834\u5408\u306e\u307f\u5b89\u5168\u306b\u4fdd\u7559\u3067\u304d\u308b\u3002
    bool can_pool = m_socket
        && !m_response_connection_close
        && !m_pool_key.is_empty()
        && (m_has_content_length || m_chunked_encoding)
        && !m_socket->is_eof()
        && !m_disable_pooling;

    if (can_pool) {
        auto key = m_pool_key;
        auto socket = move(m_socket);
        RinHTTPConnectionPool::the().put(key, move(socket));
    }

    if (on_complete)
        on_complete(0);
}

Requests::RequestTimingInfo RinHTTPFetch::timing_info() const
{
    return Requests::RequestTimingInfo {
        .domain_lookup_start_microseconds = 0,
        .domain_lookup_end_microseconds = 0,
        .connect_start_microseconds = 0,
        .connect_end_microseconds = m_connect_end_us,
        .secure_connect_start_microseconds = m_secure_connect_start_us,
        .request_start_microseconds = m_request_start_us,
        .response_start_microseconds = m_response_start_us,
        .response_end_microseconds = m_response_end_us,
        .encoded_body_size = static_cast<i64>(m_body_bytes_received),
        .http_version_alpn_identifier = Requests::ALPNHttpVersion::Http1_1,
    };
}

}
