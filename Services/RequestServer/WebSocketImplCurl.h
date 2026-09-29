/*
 * Copyright (c) 2025, Andrew Kaster <andrew@ladybird.org>
 *
 * SPDX-License-Identifier: BSD-2-Clause
 */

#pragma once

#include <AK/MemoryStream.h>
#include <LibCore/Forward.h>
#include <LibWebSocket/Impl/WebSocketImpl.h>

typedef void CURL;
typedef void CURLM;
struct curl_slist;

namespace RequestServer {

class WebSocketImplCurl final : public WebSocket::WebSocketImpl {
public:
    virtual ~WebSocketImplCurl() override;

    static NonnullRefPtr<WebSocketImplCurl> create(CURLM*);

    virtual void connect(WebSocket::ConnectionInfo const&) override;
    virtual bool can_read_line() override;
    virtual ErrorOr<ByteString> read_line(size_t) override;
    virtual ErrorOr<ByteBuffer> read(int max_size) override;
    virtual bool send(ReadonlyBytes) override;
    virtual bool eof() override;
    virtual void discard_connection() override;

    virtual bool handshake_complete_when_connected() const override { return true; }

    bool did_connect();

private:
    explicit WebSocketImplCurl(CURLM*);

    static size_t on_header_received(char*, size_t, size_t, void*);
    size_t process_header(ReadonlyBytes);
    void reset_response_headers();
    bool response_headers_valid() const;
    void read_from_socket();

    CURLM* m_multi_handle { nullptr };
    CURL* m_easy_handle { nullptr };
    RefPtr<Core::Notifier> m_read_notifier;
    RefPtr<Core::Notifier> m_error_notifier;
    Vector<curl_slist*> m_curl_string_lists;
    AllocatingMemoryStream m_read_buffer;
    ByteString m_websocket_key;
    Vector<ByteString> m_requested_protocols;
    Vector<ByteString> m_requested_extensions;
    ByteString m_response_status;
    bool m_response_header_valid { false };
    bool m_response_headers_complete { false };
    bool m_response_upgrade_seen { false };
    bool m_response_connection_seen { false };
    bool m_response_accept_seen { false };
    bool m_response_protocol_header_seen { false };
    bool m_response_protocol_seen { false };
    bool m_response_extensions_header_seen { false };
};

}
