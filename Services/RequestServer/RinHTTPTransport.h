/*
 * RinHTTPTransport - Direct socket-based HTTP/1.1 transport for RinOS
 * Replaces curl with LibCore sockets + LibTLS (rintls) + LibHTTP serialization.
 *
 * SPDX-License-Identifier: BSD-2-Clause
 */

#pragma once

#include <AK/ByteBuffer.h>
#include <AK/ByteString.h>
#include <AK/Function.h>
#include <AK/NonnullOwnPtr.h>
#include <AK/OwnPtr.h>
#include <AK/Time.h>
#include <LibCore/Socket.h>
#include <LibCore/Timer.h>
#include <LibDNS/Resolver.h>
#include <LibHTTP/HeaderList.h>
#include <LibRequests/NetworkError.h>
#include <LibRequests/RequestTimingInfo.h>
#include <LibURL/URL.h>

namespace RequestServer {

// Per-request async HTTP fetch over a raw TCP/TLS socket using HTTP/1.1.
// Replaces curl_easy on RinOS.
class RinHTTPFetch {
    AK_MAKE_NONCOPYABLE(RinHTTPFetch);

public:
    // Callbacks matching curl's callback model used by Request.cpp.
    // on_header_received: called for each header line (including status line).
    //   Signature matches curl CURLOPT_HEADERFUNCTION: (buffer, size, nmemb, user_data).
    Function<size_t(void* buffer, size_t size, size_t nmemb, void* user_data)> on_header_received;
    // on_data_received: called for each chunk of body data.
    //   Signature matches curl CURLOPT_WRITEFUNCTION: (buffer, size, nmemb, user_data).
    Function<size_t(void* buffer, size_t size, size_t nmemb, void* user_data)> on_data_received;
    // on_complete: the fetch finished (result_code 0 = success, nonzero = error).
    Function<void(int result_code)> on_complete;

    void* callback_user_data { nullptr };

    static ErrorOr<NonnullOwnPtr<RinHTTPFetch>> create(
        URL::URL const& url,
        ByteString const& method,
        HTTP::HeaderList const& request_headers,
        ReadonlyBytes request_body,
        RefPtr<DNS::LookupResult const> dns_result,
        long connect_timeout_seconds);

    ~RinHTTPFetch();

    u32 status_code() const { return m_status_code; }
    Requests::RequestTimingInfo timing_info() const;

    void cancel();

private:
    RinHTTPFetch();

    void send_request(URL::URL const& url, ByteString const& method, HTTP::HeaderList const& request_headers, ReadonlyBytes request_body);
    void on_socket_ready_to_read();
    void process_line_buffered(ReadonlyBytes data);
    void process_body_data(ReadonlyBytes data);
    void process_chunked_data(ReadonlyBytes data);
    void finish_with_error(int code);
    void finish_success();

    OwnPtr<Core::BufferedSocketBase> m_socket;
    RefPtr<Core::Timer> m_timeout_timer;

    enum class ResponseState {
        StatusLine,
        Headers,
        Body,
        ChunkedSize,
        ChunkedData,
        ChunkedTrailer,
        Complete,
        Error,
    };
    ResponseState m_response_state { ResponseState::StatusLine };

    // Partial line buffer for header parsing
    ByteBuffer m_line_buffer;

    u32 m_status_code { 0 };
    size_t m_content_length { 0 };
    bool m_has_content_length { false };
    bool m_chunked_encoding { false };
    size_t m_body_bytes_received { 0 };
    size_t m_current_chunk_remaining { 0 };

    MonotonicTime m_start_time;
    i64 m_connect_end_us { 0 };
    i64 m_secure_connect_start_us { 0 };
    i64 m_request_start_us { 0 };
    i64 m_response_start_us { 0 };
    i64 m_response_end_us { 0 };
};

}
