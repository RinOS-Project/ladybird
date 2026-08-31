/*
 * Copyright (c) 2018-2020, Andreas Kling <andreas@ladybird.org>
 *
 * SPDX-License-Identifier: BSD-2-Clause
 */

#include <LibCore/Promise.h>
#include <LibCore/System.h>
#include <LibRequests/Request.h>
#include <LibRequests/RequestClient.h>
#if defined(AK_OS_RINOS)
#    include <unistd.h>
#endif

namespace Requests {

RequestClient::RequestClient(NonnullOwnPtr<IPC::Transport> transport)
    : IPC::ConnectionToServer<RequestClientEndpoint, RequestServerEndpoint>(*this, move(transport))
{
}

RequestClient::~RequestClient() = default;

void RequestClient::die()
{
    for (auto& [id, request] : m_requests) {
        if (request)
            request->did_finish({}, {}, {}, NetworkError::RequestServerDied);
    }

    for (auto& [id, promise] : m_pending_cache_size_estimations)
        promise->reject(Error::from_string_literal("RequestServer process died"));

    m_requests.clear();
    m_streaming_request_bodies.clear();
    m_pending_cache_size_estimations.clear();
}

RefPtr<Request> RequestClient::start_request(ByteString const& method, URL::URL const& url, Optional<HTTP::HeaderList const&> request_headers, ReadonlyBytes request_body, HTTP::CacheMode cache_mode, HTTP::Cookie::IncludeCredentials include_credentials, Core::ProxyData const& proxy_data)
{
    auto request_id = m_next_request_id++;
    auto headers = request_headers.map([](auto const& headers) { return headers.headers().span(); }).value_or({});

    IPCProxy::async_start_request(request_id, method, url, headers, request_body, cache_mode, include_credentials, proxy_data);
    auto request = Request::create_from_id({}, *this, request_id);
    m_requests.set(request_id, request);
    return request;
}

RefPtr<Request> RequestClient::start_streaming_request(ByteString const& method, URL::URL const& url, Optional<HTTP::HeaderList const&> request_headers, u64 request_body_length, RequestBodySource source, HTTP::CacheMode cache_mode, HTTP::Cookie::IncludeCredentials include_credentials, Core::ProxyData const& proxy_data)
{
    static constexpr u64 max_request_body_bytes = 128u * 1024u * 1024u;
    if (request_body_length > max_request_body_bytes ||
        (request_body_length != 0u && !source))
        return nullptr;

    auto request_id = m_next_request_id++;
    auto headers = request_headers.map([](auto const& headers) { return headers.headers().span(); }).value_or({});
    m_streaming_request_bodies.set(request_id, StreamingRequestBody { move(source), request_body_length });
    IPCProxy::async_start_streaming_request(request_id, method, url, headers, request_body_length, cache_mode, include_credentials, proxy_data);

    auto request = Request::create_from_id({}, *this, request_id);
    m_requests.set(request_id, request);
    return request;
}

bool RequestClient::stop_request(Badge<Request>, Request& request)
{
    if (!m_requests.contains(request.id()))
        return false;
    return IPCProxy::stop_request(request.id());
}

void RequestClient::ensure_connection(URL::URL const& url, RequestServer::CacheLevel cache_level)
{
    auto request_id = m_next_request_id++;
    async_ensure_connection(request_id, url, cache_level);
}

bool RequestClient::set_certificate(Badge<Request>, Request& request,
                                    u64 connection_generation,
                                    ByteBuffer certificate_list,
                                    ByteBuffer signer_capability)
{
    if (!m_requests.contains(request.id()))
        return false;
    return IPCProxy::set_certificate(request.id(), connection_generation,
                                     move(certificate_list),
                                     move(signer_capability));
}

NonnullRefPtr<Core::Promise<CacheSizes>> RequestClient::estimate_cache_size_accessed_since(UnixDateTime since)
{
    auto promise = Core::Promise<CacheSizes>::construct();

    auto cache_size_estimation_id = m_next_cache_size_estimation_id++;
    m_pending_cache_size_estimations.set(cache_size_estimation_id, promise);

    async_estimate_cache_size_accessed_since(cache_size_estimation_id, since);

    return promise;
}

void RequestClient::estimated_cache_size(u64 cache_size_estimation_id, CacheSizes sizes)
{
    if (auto promise = m_pending_cache_size_estimations.take(cache_size_estimation_id); promise.has_value())
        (*promise)->resolve(sizes);
}

void RequestClient::request_started(u64 request_id, IPC::File response_file)
{
    auto request = m_requests.get(request_id);
    if (!request.has_value()) {
        warnln("Received response for non-existent request {}", request_id);
        return;
    }

    auto response_fd = response_file.take_fd();
    request.value()->set_request_fd({}, response_fd);
}

Messages::RequestClient::RequestBodyChunkResponse RequestClient::request_body_chunk(u64 request_id, u32 maximum_bytes)
{
    static constexpr u32 max_request_body_chunk_bytes = 64u * 1024u;
    auto body = m_streaming_request_bodies.get(request_id);
    if (!body.has_value() || maximum_bytes == 0u || maximum_bytes > max_request_body_chunk_bytes)
        return { ByteBuffer {}, false };

    auto& state = body.value();
    if (state.failed)
        return { ByteBuffer {}, false };
    if (state.eof)
        return { ByteBuffer {}, state.delivered == state.expected_length };

    u8 buffer[max_request_body_chunk_bytes];
    auto result = state.source(buffer, maximum_bytes);
    if (result.is_error()) {
        state.failed = true;
        dbgln("RequestClient: streaming request-body source failed: {}", result.error());
        return { ByteBuffer {}, false };
    }

    auto count = result.value();
    if (count > maximum_bytes || count > state.expected_length - min(state.delivered, state.expected_length)) {
        state.failed = true;
        return { ByteBuffer {}, false };
    }
    if (count == 0u) {
        state.eof = true;
        return { ByteBuffer {}, state.delivered == state.expected_length };
    }

    state.delivered += count;
    auto data = ByteBuffer::copy({ buffer, count });
    if (data.is_error()) {
        state.failed = true;
        return { ByteBuffer {}, false };
    }
    return { data.release_value(), false };
}

void RequestClient::request_finished(u64 request_id, u64 total_size, RequestTimingInfo timing_info, Optional<NetworkError> network_error)
{
    if (RefPtr<Request> request = m_requests.get(request_id).value_or(nullptr)) {
        request->did_finish({}, total_size, timing_info, network_error);
        m_requests.remove(request_id);
    }
    m_streaming_request_bodies.remove(request_id);
}

void RequestClient::headers_became_available(u64 request_id, Vector<HTTP::Header> response_headers, Optional<u32> status_code, Optional<String> reason_phrase)
{
    if (auto request = m_requests.get(request_id); request.has_value())
        (*request)->did_receive_headers({}, HTTP::HeaderList::create(move(response_headers)), status_code, reason_phrase);
    else
        warnln("Received headers for non-existent request {}", request_id);
}

void RequestClient::retrieve_http_cookie(int client_id, u64 request_id, URL::URL url)
{
    ::write(2, "[bridge] cookie request received\n", 34);
    String cookie;

    if (on_retrieve_http_cookie)
        cookie = on_retrieve_http_cookie(url);

    ::write(2, "[bridge] cookie response sending\n", 34);
    async_retrieved_http_cookie(client_id, request_id, cookie);
}

void RequestClient::certificate_requested(u64 request_id)
{
    if (auto request = m_requests.get(request_id); request.has_value())
        (*request)->did_request_certificates({});
}

RefPtr<WebSocket> RequestClient::websocket_connect(URL::URL const& url, ByteString const& origin, Vector<ByteString> const& protocols, Vector<ByteString> const& extensions, HTTP::HeaderList const& request_headers)
{
    auto websocket_id = m_next_websocket_id++;
    IPCProxy::async_websocket_connect(websocket_id, url, origin, protocols, extensions, request_headers.headers());
    auto connection = WebSocket::create_from_id({}, *this, websocket_id);
    m_websockets.set(websocket_id, connection);
    return connection;
}

void RequestClient::websocket_connected(u64 websocket_id)
{
    if (auto connection = m_websockets.get(websocket_id); connection.has_value())
        (*connection)->did_open({});
}

void RequestClient::websocket_received(u64 websocket_id, bool is_text, ByteBuffer data)
{
    if (auto connection = m_websockets.get(websocket_id); connection.has_value())
        (*connection)->did_receive({}, move(data), is_text);
}

void RequestClient::websocket_errored(u64 websocket_id, i32 message)
{
    if (auto connection = m_websockets.get(websocket_id); connection.has_value())
        (*connection)->did_error({}, message);
}

void RequestClient::websocket_closed(u64 websocket_id, u16 code, ByteString reason, bool clean)
{
    if (auto connection = m_websockets.get(websocket_id); connection.has_value())
        (*connection)->did_close({}, code, move(reason), clean);
}

void RequestClient::websocket_ready_state_changed(u64 websocket_id, u32 ready_state)
{
    VERIFY(ready_state <= static_cast<u32>(WebSocket::ReadyState::Closed));

    if (auto connection = m_websockets.get(websocket_id); connection.has_value())
        (*connection)->set_ready_state(static_cast<WebSocket::ReadyState>(ready_state));
}

void RequestClient::websocket_subprotocol(u64 websocket_id, ByteString subprotocol)
{
    if (auto connection = m_websockets.get(websocket_id); connection.has_value()) {
        (*connection)->set_subprotocol_in_use(move(subprotocol));
    }
}

void RequestClient::websocket_certificate_requested(u64 websocket_id)
{
    if (auto connection = m_websockets.get(websocket_id); connection.has_value())
        (*connection)->did_request_certificates({});
}

}
