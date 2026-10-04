/*
 * Copyright (c) 2021, Dex♪ <dexes.ttp@gmail.com>
 *
 * SPDX-License-Identifier: BSD-2-Clause
 */

#include <LibRequests/RequestClient.h>
#include <LibRequests/WebSocket.h>

namespace Requests {

static void clear_client_certificate_bytes(ByteBuffer& bytes)
{
    volatile u8* data = bytes.data();
    for (size_t index = 0; index < bytes.size(); ++index)
        data[index] = 0;
    bytes.clear();
}

WebSocket::WebSocket(RequestClient& client, u64 websocket_id, URL::URL url)
    : m_client(client)
    , m_websocket_id(websocket_id)
    , m_url(move(url))
{
}

WebSocket::ReadyState WebSocket::ready_state()
{
    return m_ready_state;
}

void WebSocket::set_ready_state(ReadyState ready_state)
{
    m_ready_state = ready_state;
}

ByteString WebSocket::subprotocol_in_use()
{
    return m_subprotocol;
}

void WebSocket::set_subprotocol_in_use(ByteString subprotocol)
{
    m_subprotocol = move(subprotocol);
}

void WebSocket::send(ReadonlyBytes binary_or_text_message, bool is_text)
{
    if (!m_client)
        return;
    m_client->async_websocket_send(m_websocket_id, is_text, binary_or_text_message);
}

void WebSocket::send(StringView text_message)
{
    send(text_message.bytes(), true);
}

void WebSocket::close(u16 code, ByteString reason)
{
    if (!m_client)
        return;
    m_client->async_websocket_close(m_websocket_id, code, move(reason));
}

void WebSocket::did_open(Badge<RequestClient>)
{
    if (on_open)
        on_open();
}

void WebSocket::did_receive(Badge<RequestClient>, ByteBuffer data, bool is_text)
{
    if (on_message)
        on_message(WebSocket::Message { move(data), is_text });
}

void WebSocket::did_error(Badge<RequestClient>, i32 error_code)
{
    if (on_error)
        on_error((WebSocket::Error)error_code);
}

void WebSocket::did_close(Badge<RequestClient>, u16 code, ByteString reason, bool was_clean)
{
    if (on_close)
        on_close(code, move(reason), was_clean);
}

void WebSocket::did_request_certificates(Badge<RequestClient>)
{
    if (!m_client)
        return;
    CertificateAndSignerCapability result;
    if (on_certificate_requested) {
#if defined(__cpp_exceptions) || defined(__EXCEPTIONS)
        try {
#endif
            result = on_certificate_requested();
#if defined(__cpp_exceptions) || defined(__EXCEPTIONS)
        } catch (...) {
            dbgln("WebSocket: certificate callback threw");
            return;
        }
#endif
    } else {
        if (!m_client->provide_client_certificate(
                m_url, result.connection_generation, result.certificate_list,
                result.signer_capability))
            return;
    }
    if (!m_client) {
        clear_client_certificate_bytes(result.certificate_list);
        clear_client_certificate_bytes(result.signer_capability);
        return;
    }
    if (!m_client->set_websocket_certificate(
            {}, *this, result.connection_generation,
            move(result.certificate_list), move(result.signer_capability)))
        dbgln("WebSocket: set_certificate failed");
}

void WebSocket::detach_from_client(Badge<RequestClient>)
{
    m_client = nullptr;
}

}
