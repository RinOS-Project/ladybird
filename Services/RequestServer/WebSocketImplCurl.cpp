/*
 * Copyright (c) 2025, Andrew Kaster <andrew@ladybird.org>
 *
 * SPDX-License-Identifier: BSD-2-Clause
 */

#include <AK/Base64.h>
#include <AK/Random.h>
#include <LibCrypto/Hash/HashManager.h>
#include <RequestServer/CURL.h>
#include <RequestServer/ConnectionFromClient.h>
#include <RequestServer/WebSocketImplCurl.h>

namespace RequestServer {

NonnullRefPtr<WebSocketImplCurl> WebSocketImplCurl::create(CURLM* multi_handle)
{
    return adopt_ref(*new WebSocketImplCurl(multi_handle));
}

WebSocketImplCurl::WebSocketImplCurl(CURLM* multi_handle)
    : m_multi_handle(multi_handle)
{
    m_curl_transfer_context.owner = this;
}

WebSocketImplCurl::~WebSocketImplCurl()
{
    if (m_read_notifier)
        m_read_notifier->close();
    if (m_error_notifier)
        m_error_notifier->close();

    if (m_easy_handle) {
        curl_multi_remove_handle(m_multi_handle, m_easy_handle);
        curl_easy_cleanup(m_easy_handle);
    }

    for (auto* list : m_curl_string_lists) {
        curl_slist_free_all(list);
    }
}

void WebSocketImplCurl::connect(WebSocket::ConnectionInfo const& info)
{
    VERIFY(!m_easy_handle);
    VERIFY(on_connected);
    VERIFY(on_connection_error);
    VERIFY(on_ready_to_read);

    m_easy_handle = curl_easy_init();
    if (!m_easy_handle) {
        dbgln("WebSocketImplCurl::connect: curl_easy_init failed");
        on_connection_error();
        return;
    }

    auto set_option = [this](auto option, auto value) -> bool {
        auto result = curl_easy_setopt(m_easy_handle, option, value);
        if (result == CURLE_OK)
            return true;
        dbgln("WebSocketImplCurl::connect: Failed to set curl option {}={}: {}", to_underlying(option), value, curl_easy_strerror(result));
        return false;
    };

    if (!set_option(CURLOPT_PRIVATE, &m_curl_transfer_context) ||
        !set_option(CURLOPT_WS_OPTIONS, CURLWS_RAW_MODE) ||
        !set_option(CURLOPT_CONNECT_ONLY, 2) || // WebSocket mode
        !set_option(CURLOPT_HEADERFUNCTION, &WebSocketImplCurl::on_header_received) ||
        !set_option(CURLOPT_HEADERDATA, this)) {
        on_connection_error();
        return;
    }

    reset_response_headers();
    m_requested_protocols = info.protocols();
    m_requested_extensions = info.extensions();

    u8 nonce_data[16];
    fill_with_random(nonce_data);
    auto websocket_key = encode_base64({ nonce_data, sizeof(nonce_data) });
    if (websocket_key.is_error()) {
        dbgln("WebSocketImplCurl::connect: failed to allocate Sec-WebSocket-Key");
        on_connection_error();
        return;
    }
    m_websocket_key = websocket_key.release_value().to_byte_string();

    auto const& url = info.url();
    if (!set_option(CURLOPT_URL, url.to_byte_string().characters()) ||
        !set_option(CURLOPT_PORT, url.port_or_default())) {
        on_connection_error();
        return;
    }

    if (auto root_certs = info.root_certificates_path(); root_certs.has_value() &&
        !set_option(CURLOPT_CAINFO, root_certs->characters())) {
        on_connection_error();
        return;
    }

    auto const origin_header = ByteString::formatted("Origin: {}", info.origin());
    curl_slist* curl_headers = curl_slist_append(nullptr, origin_header.characters());
    if (!curl_headers) {
        dbgln("WebSocketImplCurl::connect: failed to allocate Origin header");
        on_connection_error();
        return;
    }

    auto* appended_headers = curl_slist_append(curl_headers, ByteString::formatted("Sec-WebSocket-Key: {}", m_websocket_key).characters());
    if (!appended_headers) {
        curl_slist_free_all(curl_headers);
        dbgln("WebSocketImplCurl::connect: failed to allocate Sec-WebSocket-Key header");
        on_connection_error();
        return;
    }
    curl_headers = appended_headers;

    for (auto const& [name, value] : info.headers().headers()) {
        if (name.equals_ignoring_ascii_case("Sec-WebSocket-Key"sv)) {
            curl_slist_free_all(curl_headers);
            dbgln("WebSocketImplCurl::connect: refusing a caller-supplied Sec-WebSocket-Key");
            on_connection_error();
            return;
        }
        // curl will discard headers with empty values unless we pass the header name followed by a semicolon.
        ByteString header_string;
        if (value.is_empty())
            header_string = ByteString::formatted("{};", name);
        else
            header_string = ByteString::formatted("{}: {}", name, value);
        auto* appended_headers = curl_slist_append(curl_headers, header_string.characters());
        if (!appended_headers) {
            curl_slist_free_all(curl_headers);
            dbgln("WebSocketImplCurl::connect: failed to allocate request header");
            on_connection_error();
            return;
        }
        curl_headers = appended_headers;
    }

    if (auto const& protocols = info.protocols(); !protocols.is_empty()) {
        StringBuilder protocol_builder;
        protocol_builder.append("Sec-WebSocket-Protocol: "sv);
        protocol_builder.append(ByteString::join(","sv, protocols));
        auto* appended_headers = curl_slist_append(curl_headers, protocol_builder.to_byte_string().characters());
        if (!appended_headers) {
            curl_slist_free_all(curl_headers);
            dbgln("WebSocketImplCurl::connect: failed to allocate protocol header");
            on_connection_error();
            return;
        }
        curl_headers = appended_headers;
    }

    if (auto const& extensions = info.extensions(); !extensions.is_empty()) {
        StringBuilder protocol_builder;
        protocol_builder.append("Sec-WebSocket-Extensions: "sv);
        protocol_builder.append(ByteString::join(","sv, extensions));
        auto* appended_headers = curl_slist_append(curl_headers, protocol_builder.to_byte_string().characters());
        if (!appended_headers) {
            curl_slist_free_all(curl_headers);
            dbgln("WebSocketImplCurl::connect: failed to allocate extension header");
            on_connection_error();
            return;
        }
        curl_headers = appended_headers;
    }

    if (!set_option(CURLOPT_HTTPHEADER, curl_headers)) {
        curl_slist_free_all(curl_headers);
        on_connection_error();
        return;
    }
    m_curl_string_lists.append(curl_headers);

    if (auto const& dns_info = info.dns_result(); dns_info.has_value()) {
        auto* resolve_list = curl_slist_append(nullptr, build_curl_resolve_list(*dns_info, url.serialized_host(), url.port_or_default()).characters());
        if (!resolve_list) {
            dbgln("WebSocketImplCurl::connect: failed to allocate DNS resolve list");
            on_connection_error();
            return;
        }
        if (!set_option(CURLOPT_RESOLVE, resolve_list)) {
            curl_slist_free_all(resolve_list);
            on_connection_error();
            return;
        }
        m_curl_string_lists.append(resolve_list);
    }

    CURLMcode const err = curl_multi_add_handle(m_multi_handle, m_easy_handle);
    if (err != CURLM_OK) {
        dbgln("WebSocketImplCurl::connect: failed to add curl handle: {}", curl_multi_strerror(err));
        on_connection_error();
        return;
    }
}

bool WebSocketImplCurl::can_read_line()
{
    VERIFY_NOT_REACHED();
}

ErrorOr<ByteBuffer> WebSocketImplCurl::read(int max_size)
{
    auto buffer = TRY(ByteBuffer::create_uninitialized(max_size));
    auto const read_bytes = TRY(m_read_buffer.read_some(buffer));
    return buffer.slice(0, read_bytes.size());
}

ErrorOr<ByteString> WebSocketImplCurl::read_line(size_t)
{
    VERIFY_NOT_REACHED();
}

bool WebSocketImplCurl::send(ReadonlyBytes bytes)
{
    size_t sent = 0;
    CURLcode result = CURLE_OK;
    do {
        sent = 0;
        result = curl_easy_send(m_easy_handle, bytes.data(), bytes.size(), &sent);
        bytes = bytes.slice(sent);
    } while (bytes.size() > 0 && (result == CURLE_OK || result == CURLE_AGAIN));

    return result == CURLE_OK;
}

bool WebSocketImplCurl::eof()
{
    return m_read_buffer.is_eof();
}

size_t WebSocketImplCurl::on_header_received(char* buffer, size_t size, size_t count, void* user_data)
{
    if (count != 0 && size > NumericLimits<size_t>::max() / count)
        return 0;

    auto* websocket = static_cast<WebSocketImplCurl*>(user_data);
    return websocket->process_header({ reinterpret_cast<u8 const*>(buffer), size * count });
}

size_t WebSocketImplCurl::process_header(ReadonlyBytes bytes)
{
    constexpr size_t maximum_header_line_size = 64 * 1024;
    if (bytes.size() > maximum_header_line_size)
        return 0;

    auto line = ByteString::copy(bytes);
    if (line.is_error())
        return 0;

    auto trimmed_line = line.value().trim_whitespace();
    if (trimmed_line.starts_with("HTTP/"sv)) {
        reset_response_headers();
        auto parts = trimmed_line.split(' ');
        if (parts.size() < 2) {
            m_response_header_valid = false;
            return bytes.size();
        }
        m_response_status = parts[1];
        m_response_header_valid = m_response_status == "101";
        return bytes.size();
    }

    if (trimmed_line.is_empty()) {
        if (m_response_status == "101") {
            m_response_headers_complete = true;
            if (!response_headers_valid())
                m_response_header_valid = false;
        }
        return bytes.size();
    }

    if (m_response_status != "101")
        return bytes.size();

    auto parts = line.value().split(':');
    if (parts.size() < 2) {
        m_response_header_valid = false;
        return bytes.size();
    }

    auto header_name = parts[0].trim_whitespace();
    auto header_value = parts[1].trim_whitespace();
    if (header_name.equals_ignoring_ascii_case("Upgrade"sv)) {
        bool const valid = header_value.equals_ignoring_ascii_case("websocket"sv);
        m_response_upgrade_seen |= valid;
        m_response_header_valid &= valid;
    } else if (header_name.equals_ignoring_ascii_case("Connection"sv)) {
        bool connection_has_upgrade = false;
        for (auto const& token : header_value.split(',')) {
            if (token.trim_whitespace().equals_ignoring_ascii_case("Upgrade"sv)) {
                connection_has_upgrade = true;
                break;
            }
        }
        m_response_connection_seen |= connection_has_upgrade;
        m_response_header_valid &= connection_has_upgrade;
    } else if (header_name.equals_ignoring_ascii_case("Sec-WebSocket-Accept"sv)) {
        auto expected_content = ByteString::formatted("{}258EAFA5-E914-47DA-95CA-C5AB0DC85B11", m_websocket_key);
        Crypto::Hash::Manager hash;
        hash.initialize(Crypto::Hash::HashKind::SHA1);
        hash.update(expected_content);
        auto expected_sha1 = hash.digest();
        auto expected_accept = encode_base64({ expected_sha1.immutable_data(), expected_sha1.data_length() });
        bool const valid = !expected_accept.is_error() && header_value == expected_accept.value().to_byte_string();
        m_response_accept_seen |= valid;
        m_response_header_valid &= valid;
    } else if (header_name.equals_ignoring_ascii_case("Sec-WebSocket-Protocol"sv)) {
        m_response_protocol_header_seen = true;
        bool protocol_is_supported = false;
        for (auto const& protocol : m_requested_protocols) {
            if (header_value.equals_ignoring_ascii_case(protocol)) {
                protocol_is_supported = true;
                break;
            }
        }
        m_response_protocol_seen |= protocol_is_supported;
        m_response_header_valid &= protocol_is_supported;
    } else if (header_name.equals_ignoring_ascii_case("Sec-WebSocket-Extensions"sv)) {
        m_response_extensions_header_seen = true;
        if (m_requested_extensions.is_empty())
            m_response_header_valid = false;
        for (auto const& extension : header_value.split(',')) {
            bool supported = false;
            for (auto const& requested : m_requested_extensions) {
                if (extension.trim_whitespace().equals_ignoring_ascii_case(requested)) {
                    supported = true;
                    break;
                }
            }
            if (!supported)
                m_response_header_valid = false;
        }
    }

    return bytes.size();
}

void WebSocketImplCurl::reset_response_headers()
{
    m_response_status = {};
    m_response_header_valid = true;
    m_response_headers_complete = false;
    m_response_upgrade_seen = false;
    m_response_connection_seen = false;
    m_response_accept_seen = false;
    m_response_protocol_header_seen = false;
    m_response_protocol_seen = false;
    m_response_extensions_header_seen = false;
}

bool WebSocketImplCurl::response_headers_valid() const
{
    return m_response_header_valid && m_response_status == "101" && m_response_upgrade_seen && m_response_connection_seen
        && m_response_accept_seen
        && (!m_response_protocol_header_seen || (!m_requested_protocols.is_empty() && m_response_protocol_seen))
        && (!m_response_extensions_header_seen || !m_requested_extensions.is_empty());
}

void WebSocketImplCurl::discard_connection()
{
    if (m_read_notifier) {
        m_read_notifier->close();
        m_read_notifier = nullptr;
    }
    if (m_error_notifier) {
        m_error_notifier->close();
        m_error_notifier = nullptr;
    }
    if (m_easy_handle) {
        curl_multi_remove_handle(m_multi_handle, m_easy_handle);
        curl_easy_cleanup(m_easy_handle);
        m_easy_handle = nullptr;
    }
}

void WebSocketImplCurl::read_from_socket()
{
    bool received_data = false;

    // "Wait on the socket only if curl_easy_recv returns CURLE_AGAIN. The reason for this is libcurl or the SSL
    // library may internally cache some data, therefore you should call curl_easy_recv until all data is read which
    // would include any cached data."
    for (;;) {
        u8 buffer[65536];
        size_t nread = 0;
        CURLcode const result = curl_easy_recv(m_easy_handle, buffer, sizeof(buffer), &nread);
        if (result == CURLE_AGAIN)
            break;

        if (result != CURLE_OK) {
            dbgln("Failed to read from WebSocket: {}", curl_easy_strerror(result));
            // Process any successfully buffered data (e.g., the server's close frame) before reporting the error. This
            // handles cases where the server drops the connection immediately after sending the close frame, causing a
            // subsequent curl_easy_recv call to fail.
            if (received_data)
                on_ready_to_read();
            on_connection_error();
            return;
        }

        // "Reading exactly 0 bytes indicates a closed connection." which
        // may be part of the closing handshake

        received_data = true;
        if (0 == nread)
            break;

        if (auto const err = m_read_buffer.write_until_depleted({ buffer, nread }); err.is_error()) {
            on_connection_error();
            return;
        }
    }

    if (received_data)
        on_ready_to_read();
}

bool WebSocketImplCurl::did_connect()
{
    if (!m_response_headers_complete || !response_headers_valid())
        return false;

    curl_socket_t socket_fd = CURL_SOCKET_BAD;
    auto res = curl_easy_getinfo(m_easy_handle, CURLINFO_ACTIVESOCKET, &socket_fd);
    if (res != CURLE_OK || socket_fd == CURL_SOCKET_BAD)
        return false;

    m_read_notifier = Core::Notifier::construct(socket_fd, Core::Notifier::Type::Read);
    m_read_notifier->on_activation = [this] {
        read_from_socket();
    };
    m_error_notifier = Core::Notifier::construct(socket_fd, Core::Notifier::Type::Error | Core::Notifier::Type::HangUp);
    m_error_notifier->on_activation = [this] {
        on_connection_error();
    };

    on_connected();

    // There may be data waiting for us already (e.g. if the server sends us data immediately upon opening a WebSocket),
    // so try reading immediately.
    read_from_socket();

    return true;
}

}
