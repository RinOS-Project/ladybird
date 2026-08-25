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

Messages::WebWorkerClient::DidRequestCookieResponse WebWorkerClient::did_request_cookie(URL::URL url, HTTP::Cookie::Source source)
{
    if (on_request_cookie)
        return on_request_cookie(url, source);
    return HTTP::Cookie::VersionedCookie {};
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
