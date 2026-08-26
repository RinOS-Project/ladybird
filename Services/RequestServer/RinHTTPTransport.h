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
#include <AK/HashMap.h>
#include <AK/NonnullOwnPtr.h>
#include <AK/OwnPtr.h>
#include <AK/Time.h>
#include <AK/Vector.h>
#include <LibCore/Socket.h>
#include <LibCore/Timer.h>
#include <LibDNS/Resolver.h>
#include <LibHTTP/HeaderList.h>
#include <LibRequests/NetworkError.h>
#include <LibRequests/RequestTimingInfo.h>
#include <LibURL/URL.h>

namespace RequestServer {

// Stage 3-C: HTTP keep-alive \u63a5\u7d9a\u30d7\u30fc\u30eb\u3002scheme://host:port \u3054\u3068\u306b LRU \u3067 socket \u3092\u4fdd\u6301\u3057\u3001
// \u540c\u4e00 origin \u3078\u306e\u9023\u7d9a\u30ea\u30af\u30a8\u30b9\u30c8\u3067 TCP + TLS \u30cf\u30f3\u30c9\u30b7\u30a7\u30a4\u30af\u3092\u7701\u304f\u3002
class RinHTTPConnectionPool {
    AK_MAKE_NONCOPYABLE(RinHTTPConnectionPool);
    AK_MAKE_NONMOVABLE(RinHTTPConnectionPool);

public:
    static RinHTTPConnectionPool& the();

    // Pool key: "scheme://host:port"
    static ByteString make_key(URL::URL const& url);

    // \u30d7\u30fc\u30eb\u304b\u3089 socket \u3092\u53d6\u308a\u51fa\u3059\u3002\u306a\u3051\u308c\u3070 nullptr\u3002
    OwnPtr<Core::BufferedSocketBase> take(ByteString const& key);

    // \u30ec\u30b9\u30dd\u30f3\u30b9\u5b8c\u4e86\u5f8c\u306e socket \u3092\u30d7\u30fc\u30eb\u306b\u7f6e\u304f\u3002
    void put(ByteString const& key, OwnPtr<Core::BufferedSocketBase> socket);

    // \u7279\u5b9a\u30ad\u30fc\u306e\u5168\u63a5\u7d9a\u3092\u7834\u68c4 (\u30b5\u30fc\u30d0\u304b\u3089\u30a8\u30e9\u30fc\u304c\u8fd4\u3063\u305f\u3068\u304d\u306a\u3069)\u3002
    void evict_all(ByteString const& key);

private:
    RinHTTPConnectionPool() = default;

    struct PooledSocket {
        OwnPtr<Core::BufferedSocketBase> socket;
        RefPtr<Core::Timer> idle_timer;
    };
    HashMap<ByteString, Vector<PooledSocket>> m_pools;
    static constexpr size_t MAX_PER_HOST = 4;
    static constexpr int IDLE_TIMEOUT_MS = 30000;
};

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

    ErrorOr<void> send_request(URL::URL const& url, ByteString const& method, HTTP::HeaderList const& request_headers, ReadonlyBytes request_body);
    void on_socket_ready_to_read();
    void process_line_buffered(ReadonlyBytes data);
    void process_body_data(ReadonlyBytes data);
    void process_chunked_data(ReadonlyBytes data);
    void finish_with_error(int code);
    void finish_success();

    OwnPtr<Core::BufferedSocketBase> m_socket;
    RefPtr<Core::Timer> m_timeout_timer;  // 接続確立までのタイムアウト
    RefPtr<Core::Timer> m_idle_timer;     // 接続後の連続無通信タイムアウト
    bool m_connect_timer_stopped { false };

    enum class ResponseState {
        StatusLine,
        Headers,
        Body,
        ChunkedSize,
        ChunkedData,
        ChunkedDataTerminator,
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
    u8 m_chunk_data_terminator_bytes { 0 };

    MonotonicTime m_start_time;
    i64 m_connect_end_us { 0 };
    i64 m_secure_connect_start_us { 0 };
    i64 m_request_start_us { 0 };
    i64 m_response_start_us { 0 };
    i64 m_response_end_us { 0 };

    // Stage 3-C: keep-alive \u306e\u305f\u3081\u306e\u30e1\u30bf\u30c7\u30fc\u30bf\u3002
    // \u30ec\u30b9\u30dd\u30f3\u30b9\u304c\u6b63\u5e38\u7d42\u4e86\u3057\u305f\u3068\u304d\u306b\u30d7\u30fc\u30eb\u3078\u623b\u3059\u306e\u306b\u4f7f\u3046\u3002
    ByteString m_pool_key;                // \u30d7\u30fc\u30eb\u30ad\u30fc (\u7a7a\u306a\u3089\u5bfe\u8c61\u5916)
    bool m_reused_from_pool { false };    // \u30d7\u30fc\u30eb\u304b\u3089\u8d77\u3053\u3057\u305f socket \u304b
    bool m_response_connection_close { false }; // \u30b5\u30fc\u30d0\u304c Connection: close \u3092\u8fd4\u3057\u305f\u304b
};

}
