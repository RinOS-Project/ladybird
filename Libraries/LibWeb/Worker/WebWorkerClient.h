/*
 * Copyright (c) 2023, Andrew Kaster <akaster@serenityos.org>
 *
 * SPDX-License-Identifier: BSD-2-Clause
 */

#pragma once

#include <AK/Optional.h>
#include <AK/String.h>
#include <AK/Vector.h>
#include <LibHTTP/Cookie/Cookie.h>
#include <LibHTTP/Cookie/ParsedCookie.h>
#include <LibIPC/ConnectionToServer.h>
#include <LibIPC/TransportHandle.h>
#include <LibWeb/StorageAPI/StorageEndpoint.h>
#include <LibWeb/Export.h>
#include <LibWeb/Worker/WebWorkerClientEndpoint.h>
#include <LibWeb/Worker/WebWorkerServerEndpoint.h>
#include <LibWebView/StorageSetResult.h>

namespace Web::HTML {

class WEB_API WebWorkerClient final
    : public IPC::ConnectionToServer<WebWorkerClientEndpoint, WebWorkerServerEndpoint>
    , public WebWorkerClientEndpoint {
    C_OBJECT_ABSTRACT(WebWorkerClient);

public:
    explicit WebWorkerClient(NonnullOwnPtr<IPC::Transport>);

    // Worker::terminate() is fire-and-forget. Once it has requested a normal
    // close, an IPC teardown must not be reported to script as a helper crash.
    void begin_close();

    virtual void did_close_worker() override;
    virtual void did_fail_loading_worker_script() override;
    virtual void did_set_cookie(URL::URL, HTTP::Cookie::ParsedCookie, HTTP::Cookie::Source) override;
    virtual Messages::WebWorkerClient::DidRequestCookieResponse did_request_cookie(URL::URL, HTTP::Cookie::Source) override;
    virtual Messages::WebWorkerClient::DidRequestStorageItemResponse did_request_storage_item(
        Web::StorageAPI::StorageEndpointType, String, String, u64) override;
    virtual Messages::WebWorkerClient::DidSetStorageItemResponse did_set_storage_item(
        Web::StorageAPI::StorageEndpointType, String, String, String, u64) override;
    virtual void did_remove_storage_item(
        Web::StorageAPI::StorageEndpointType, String, String, u64) override;
    virtual Messages::WebWorkerClient::DidRequestStorageKeysResponse did_request_storage_keys(
        Web::StorageAPI::StorageEndpointType, String, u64) override;
    virtual void did_clear_storage(
        Web::StorageAPI::StorageEndpointType, String, u64) override;
    virtual Messages::WebWorkerClient::RequestHttpCookieOwnerResponse request_http_cookie_owner(
        u32 operation, ByteString request_url, ByteString origin,
        ByteString cookie_data, u32 policy) override;
    virtual Messages::WebWorkerClient::RequestServiceWorkerOwnerResponse request_service_worker_owner(
        u32 operation, ByteString client_url, ByteString origin,
        ByteString script_url, ByteString scope, u32 update_via_cache,
        u64 list_snapshot_id, u32 list_index) override;
    virtual Messages::WebWorkerClient::RequestWorkerAgentResponse request_worker_agent(Web::Bindings::AgentType worker_type) override;

    Function<void()> on_worker_close;
    Function<void()> on_worker_crash;
    Function<void()> on_worker_script_load_failure;
    Function<HTTP::Cookie::VersionedCookie(URL::URL const&, HTTP::Cookie::Source)> on_request_cookie;
    Function<void(URL::URL const&, HTTP::Cookie::ParsedCookie const&, HTTP::Cookie::Source)> on_set_cookie;
    Function<Optional<String>(Web::StorageAPI::StorageEndpointType, String, String, u64)> on_request_storage_item;
    Function<WebView::StorageSetResult(Web::StorageAPI::StorageEndpointType, String, String, String, u64)> on_set_storage_item;
    Function<void(Web::StorageAPI::StorageEndpointType, String, String, u64)> on_remove_storage_item;
    Function<Vector<String>(Web::StorageAPI::StorageEndpointType, String, u64)> on_request_storage_keys;
    Function<void(Web::StorageAPI::StorageEndpointType, String, u64)> on_clear_storage;
    Function<Messages::WebWorkerClient::RequestHttpCookieOwnerResponse(u32, ByteString, ByteString, ByteString, u32)> on_request_http_cookie_owner;
    Function<Messages::WebWorkerClient::RequestServiceWorkerOwnerResponse(u32, ByteString, ByteString, ByteString, ByteString, u32, u64, u32)> on_request_service_worker_owner;
    Function<Messages::WebWorkerClient::RequestWorkerAgentResponse(Web::Bindings::AgentType)> on_request_worker_agent;

private:
    virtual void die() override;
    void notify_worker_crash();

    bool m_worker_closed_normally { false };
    bool m_worker_close_requested { false };
    bool m_worker_crash_notified { false };
};

}
