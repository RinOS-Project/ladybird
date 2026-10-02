/*
 * Copyright (c) 2023, Andrew Kaster <akaster@serenityos.org>
 *
 * SPDX-License-Identifier: BSD-2-Clause
 */

#include <LibWeb/Worker/WebWorkerClient.h>

namespace Web::HTML {

void WebWorkerClient::die()
{
    notify_worker_crash();
}

void WebWorkerClient::notify_worker_crash()
{
    // A clean close is acknowledged by the service before it tears down this
    // transport. An abrupt helper exit instead reaches the owning
    // WorkerAgent through this connection's close handler. Notify only for
    // the latter, and only once.
    if (m_worker_closed_normally || m_worker_close_requested
        || m_worker_crash_notified)
        return;

    m_worker_crash_notified = true;
    if (on_worker_crash)
        on_worker_crash();
}

void WebWorkerClient::begin_close()
{
    m_worker_close_requested = true;
}

void WebWorkerClient::did_close_worker()
{
    if (m_worker_closed_normally)
        return;

    m_worker_closed_normally = true;
    if (on_worker_close)
        on_worker_close();
}

void WebWorkerClient::did_fail_loading_worker_script()
{
    if (on_worker_script_load_failure)
        on_worker_script_load_failure();
}

void WebWorkerClient::did_set_cookie(URL::URL url, HTTP::Cookie::ParsedCookie cookie, HTTP::Cookie::Source source)
{
    if (on_set_cookie)
        on_set_cookie(url, cookie, source);
}

Messages::WebWorkerClient::DidRequestCookieResponse WebWorkerClient::did_request_cookie(URL::URL url, HTTP::Cookie::Source source)
{
    if (on_request_cookie)
        return on_request_cookie(url, source);
    return HTTP::Cookie::VersionedCookie {};
}

Messages::WebWorkerClient::RequestHttpCookieOwnerResponse WebWorkerClient::request_http_cookie_owner(
    u32 operation, ByteString request_url, ByteString origin,
    ByteString cookie_data, u32 policy)
{
    if (on_request_http_cookie_owner)
        return on_request_http_cookie_owner(operation, move(request_url),
            move(origin), move(cookie_data), policy);
    return { false, false, 0, {} };
}

Messages::WebWorkerClient::RequestServiceWorkerOwnerResponse WebWorkerClient::request_service_worker_owner(
    u32 operation, ByteString client_url, ByteString origin,
    ByteString script_url, ByteString scope, u32 update_via_cache)
{
    if (on_request_service_worker_owner)
        return on_request_service_worker_owner(operation, move(client_url),
            move(origin), move(script_url), move(scope), update_via_cache);
    return { false, false, 0, 0, {}, {}, {} };
}

Messages::WebWorkerClient::RequestWorkerAgentResponse WebWorkerClient::request_worker_agent(Web::Bindings::AgentType worker_type)
{
    if (on_request_worker_agent)
        return on_request_worker_agent(worker_type);
    return { IPC::TransportHandle {}, IPC::TransportHandle {}, IPC::TransportHandle {} };
}

WebWorkerClient::WebWorkerClient(NonnullOwnPtr<IPC::Transport> transport)
    : IPC::ConnectionToServer<WebWorkerClientEndpoint, WebWorkerServerEndpoint>(*this, move(transport))
{
}

}
