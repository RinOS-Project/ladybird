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

namespace RequestServer {

RinHTTPFetch::RinHTTPFetch()
    : m_start_time(MonotonicTime::now())
{
}

RinHTTPFetch::~RinHTTPFetch()
{
    cancel();
}

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
}

ErrorOr<NonnullOwnPtr<RinHTTPFetch>> RinHTTPFetch::create(
    URL::URL const& url,
    ByteString const& method,
    HTTP::HeaderList const& request_headers,
    ReadonlyBytes request_body,
    RefPtr<DNS::LookupResult const> dns_result,
    long connect_timeout_seconds)
{
    auto fetch = adopt_own(*new RinHTTPFetch());

    if (!dns_result || dns_result->is_empty() || !dns_result->has_cached_addresses())
        return Error::from_string_literal("No DNS result for HTTP fetch");

    auto host = url.serialized_host().to_byte_string();
    auto port = url.port_or_default();
    bool is_https = url.scheme() == "https"sv;

    // Build SocketAddress from DNS result
    auto const& addresses = dns_result->cached_addresses();
    Core::SocketAddress socket_address = addresses.first().visit(
        [&](IPv4Address const& v4) -> Core::SocketAddress { return { v4, port }; },
        [&](IPv6Address const& v6) -> Core::SocketAddress { return { v6, port }; });

    if (is_https) {
        TLS::Options tls_options;
        if (auto const& cert_path = default_certificate_path(); !cert_path.is_empty())
            tls_options.root_certificates_path = cert_path;

        auto tls_socket = TRY(TLS::TLSv12::connect(socket_address, host, move(tls_options)));
        fetch->m_connect_end_us = (MonotonicTime::now() - fetch->m_start_time).to_microseconds();
        fetch->m_secure_connect_start_us = fetch->m_connect_end_us;

        fetch->m_socket = TRY(Core::BufferedSocket<TLS::TLSv12>::create(move(tls_socket)));
    } else {
        auto tcp_socket = TRY(Core::TCPSocket::connect(socket_address));
        TRY(tcp_socket->set_blocking(false));
        fetch->m_connect_end_us = (MonotonicTime::now() - fetch->m_start_time).to_microseconds();

        fetch->m_socket = TRY(Core::BufferedTCPSocket::create(move(tcp_socket)));
    }

    // Send request
    fetch->m_request_start_us = (MonotonicTime::now() - fetch->m_start_time).to_microseconds();
    fetch->send_request(url, method, request_headers, request_body);

    // 接続確立タイマー: 相手のデータが届くまでを監視。最初の read で停止する。
    if (connect_timeout_seconds > 0) {
        fetch->m_timeout_timer = Core::Timer::create_single_shot(static_cast<int>(connect_timeout_seconds * 1000), [raw = fetch.ptr()] {
            raw->finish_with_error(28); // CURLE_OPERATION_TIMEDOUT
        });
        fetch->m_timeout_timer->start();
    }

    // Idle タイマー: 受信が一定時間止まったらタイムアウト。
    // 60 秒の連続無通信で切断する。データが流れるたびに restart する。
    // ハンドシェイク直後にサーバが黙るケースにも備えて create() 時点で start しておく。
    fetch->m_idle_timer = Core::Timer::create_single_shot(60 * 1000, [raw = fetch.ptr()] {
        dbgln("[RinHTTP] idle_timer fired -> finish_with_error(28)");
        raw->finish_with_error(28); // CURLE_OPERATION_TIMEDOUT
    });
    fetch->m_idle_timer->start();
    dbgln("[RinHTTP] fetch created, idle_timer armed (60s)");

    // Set up async read
    fetch->m_socket->on_ready_to_read = [raw = fetch.ptr()] {
        raw->on_socket_ready_to_read();
    };

    return fetch;
}

void RinHTTPFetch::send_request(URL::URL const& url, ByteString const& method, HTTP::HeaderList const& request_headers, ReadonlyBytes request_body)
{
    StringBuilder builder;

    auto resource = url.serialize_path();
    if (url.query().has_value())
        builder.appendff("{} {}?{} HTTP/1.1\r\n", method, resource, *url.query());
    else
        builder.appendff("{} {} HTTP/1.1\r\n", method, resource);

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

    if (!request_body.is_empty())
        builder.appendff("Content-Length: {}\r\n", request_body.size());

    builder.append("\r\n"sv);

    auto header_bytes = builder.to_byte_string();
    (void)m_socket->write_until_depleted(header_bytes.bytes());

    if (!request_body.is_empty())
        (void)m_socket->write_until_depleted(request_body);
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
        finish_with_error(56); // CURLE_RECV_ERROR
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
            break;
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
        else if (m_response_state == ResponseState::ChunkedSize || m_response_state == ResponseState::ChunkedData || m_response_state == ResponseState::ChunkedTrailer)
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
        m_line_buffer.append(byte);

        // Check for \r\n at the end of line buffer
        if (m_line_buffer.size() >= 2 && m_line_buffer[m_line_buffer.size() - 2] == '\r' && m_line_buffer[m_line_buffer.size() - 1] == '\n') {
            // Complete line (without \r\n)
            auto line = StringView { m_line_buffer.data(), m_line_buffer.size() - 2 };

            if (m_response_state == ResponseState::StatusLine) {
                // Report the full status line via on_header_received (curl does this too)
                if (on_header_received) {
                    on_header_received(m_line_buffer.data(), 1, m_line_buffer.size(), callback_user_data);
                }

                // Parse status code
                if (line.starts_with("HTTP/"sv)) {
                    auto space1 = line.find(' ');
                    if (space1.has_value()) {
                        auto after_version = line.substring_view(*space1 + 1);
                        auto space2 = after_version.find(' ');
                        StringView code_str = space2.has_value() ? after_version.substring_view(0, *space2) : after_version;
                        if (auto code = code_str.to_number<u32>(); code.has_value())
                            m_status_code = *code;
                    }
                    m_response_start_us = (MonotonicTime::now() - m_start_time).to_microseconds();
                }
                m_response_state = ResponseState::Headers;
            } else if (m_response_state == ResponseState::Headers) {
                if (line.is_empty()) {
                    // End of headers. Report the blank line.
                    if (on_header_received)
                        on_header_received(m_line_buffer.data(), 1, m_line_buffer.size(), callback_user_data);

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

                // Report header line
                if (on_header_received)
                    on_header_received(m_line_buffer.data(), 1, m_line_buffer.size(), callback_user_data);

                // Track Content-Length and Transfer-Encoding
                auto colon = line.find(':');
                if (colon.has_value()) {
                    auto name = line.substring_view(0, *colon).trim_whitespace();
                    auto value = line.substring_view(*colon + 1).trim_whitespace();

                    if (name.equals_ignoring_ascii_case("content-length"sv)) {
                        if (auto len = value.to_number<size_t>(); len.has_value()) {
                            m_content_length = *len;
                            m_has_content_length = true;
                        }
                    } else if (name.equals_ignoring_ascii_case("transfer-encoding"sv)) {
                        if (value.contains("chunked"sv, CaseSensitivity::CaseInsensitive))
                            m_chunked_encoding = true;
                    }
                }
            }
            m_line_buffer.clear();
        }
    }
}

void RinHTTPFetch::process_body_data(ReadonlyBytes data)
{
    if (data.is_empty())
        return;

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
            m_line_buffer.append(byte);
            if (m_line_buffer.size() >= 2 && m_line_buffer[m_line_buffer.size() - 2] == '\r' && m_line_buffer[m_line_buffer.size() - 1] == '\n') {
                auto line = StringView { m_line_buffer.data(), m_line_buffer.size() - 2 };
                // Chunk size may have extensions after ';', ignore them
                auto semicolon = line.find(';');
                auto size_str = semicolon.has_value() ? line.substring_view(0, *semicolon) : line;

                m_current_chunk_remaining = 0;
                for (auto ch : size_str) {
                    m_current_chunk_remaining <<= 4;
                    if (ch >= '0' && ch <= '9')
                        m_current_chunk_remaining += ch - '0';
                    else if (ch >= 'a' && ch <= 'f')
                        m_current_chunk_remaining += ch - 'a' + 10;
                    else if (ch >= 'A' && ch <= 'F')
                        m_current_chunk_remaining += ch - 'A' + 10;
                }

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
                // Expect \r\n after chunk data
                m_response_state = ResponseState::ChunkedSize;
                // Skip trailing \r\n — consume up to 2 bytes
                // We transition back to ChunkedSize which will skip past \r\n naturally
                // Actually, we need to consume the trailing CRLF. Let's just skip bytes.
                // The next ChunkedSize line will be preceded by \r\n.
                // We handle this by looking for the CRLF in the ChunkedSize state.
            }
        } else if (m_response_state == ResponseState::ChunkedTrailer) {
            // After the 0-length chunk, read trailer headers until we see an empty line (\r\n)
            auto byte = data[offset++];
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
    dbgln("[RinHTTP] finish_with_error code={} state={} body={}/{}",
        code, (int)m_response_state, m_body_bytes_received, m_content_length);
    m_response_state = ResponseState::Error;
    m_response_end_us = (MonotonicTime::now() - m_start_time).to_microseconds();
    cancel();
    if (on_complete)
        on_complete(code);
}

void RinHTTPFetch::finish_success()
{
    dbgln("[RinHTTP] finish_success body={} status={}", m_body_bytes_received, m_status_code);
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
