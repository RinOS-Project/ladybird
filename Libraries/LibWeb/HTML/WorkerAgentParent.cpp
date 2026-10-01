/*
 * Copyright (c) 2023, Andrew Kaster <akaster@serenityos.org>
 *
 * SPDX-License-Identifier: BSD-2-Clause
 */

#include <AK/Platform.h>
#if defined(AK_OS_RINOS)
#    include <LibURL/Parser.h>
#    include <LibURL/Site.h>
#endif
#include <LibWeb/Bindings/PrincipalHostDefined.h>
#include <LibWeb/DOM/Document.h>
#include <LibWeb/DOM/Event.h>
#include <LibWeb/DOM/EventTarget.h>
#include <LibWeb/HTML/EventLoop/EventLoop.h>
#include <LibWeb/HTML/EventNames.h>
#include <LibWeb/HTML/MessagePort.h>
#include <LibWeb/HTML/Scripting/Environments.h>
#include <LibWeb/HTML/Window.h>
#include <LibWeb/HTML/Worker.h>
#include <LibWeb/HTML/WorkerAgentParent.h>
#include <LibWeb/Page/Page.h>
#include <LibWeb/Worker/WebWorkerClient.h>
#if defined(AK_OS_RINOS)
#    include <rin/web/webcontent_protocol.h>
#endif

namespace Web::HTML {

#if defined(AK_OS_RINOS)
// A worker helper is a replaceable child process. Cookie site context is
// rebound from its authenticated creator settings before the request reaches
// the Browser owner; the helper cannot assert a top-level navigation.
static ByteString trusted_worker_cookie_context(
    EnvironmentSettingsObject const& settings, URL::URL const& request_url,
    bool set_cookie_commit)
{
    auto top_level_origin = settings.top_level_origin;
    if (!top_level_origin.has_value() && settings.top_level_creation_url.has_value())
        top_level_origin = settings.top_level_creation_url->origin();

    auto partition_site = ByteString("null"sv);
    if (top_level_origin.has_value() && !top_level_origin->is_opaque())
        partition_site = URL::Site::obtain(top_level_origin.value()).serialize().to_byte_string();

    auto origin = settings.origin();
    auto origin_string = origin.serialize().to_byte_string();
    const bool has_cross_site_ancestor = settings.has_cross_site_ancestor();
    if (set_cookie_commit) {
        const bool same_site = !has_cross_site_ancestor &&
            origin.is_same_site(request_url.origin());
        return ByteString::formatted("RSC1|{}|0|0|{}\n",
            same_site ? "1"sv : "0"sv, partition_site);
    }
    return ByteString::formatted("RCX1|{}|{}|0|{}",
        origin_string, has_cross_site_ancestor ? "1"sv : "0"sv,
        partition_site);
}
#endif

GC_DEFINE_ALLOCATOR(WorkerAgentParent);

WorkerAgentParent::WorkerAgentParent(URL::URL url, WorkerOptions const& options, GC::Ptr<MessagePort> outside_port, GC::Ref<EnvironmentSettingsObject> outside_settings, GC::Ref<DOM::EventTarget> worker_event_target, Bindings::AgentType agent_type)
    : m_worker_options(options)
    , m_agent_type(agent_type)
    , m_url(move(url))
    , m_outside_port(outside_port)
    , m_outside_settings(outside_settings)
    , m_worker_event_target(worker_event_target)
{
}

void WorkerAgentParent::initialize(JS::Realm& realm)
{
    Base::initialize(realm);

    m_message_port = MessagePort::create(realm);
    m_message_port->entangle_with(*m_outside_port);

    TransferDataEncoder data_holder;
    MUST(m_message_port->transfer_steps(data_holder));

    // FIXME: Specification says this supposed to happen in step 11 of onComplete handler defined in https://html.spec.whatwg.org/multipage/workers.html#run-a-worker
    //        but that would require introducing a new IPC message type to communicate this from WebWorker to WebContent process,
    //        so let's do it here for now.
    m_outside_port->start();

    // NOTE: This blocking IPC call may launch another process.
    //    If spinning the event loop for this can cause other javascript to execute, we're in trouble.
    auto response = Bindings::principal_host_defined_page(realm).client().request_worker_agent(m_agent_type);

    auto transport_or_error = response.worker_handle.create_transport();
    if (transport_or_error.is_error()) {
        // A missing helper or a denied worker type is an observable worker-start
        // failure, not a reason to abort the WebContent process.
        queue_worker_error_event();
        return;
    }
    auto transport = transport_or_error.release_value();
    m_worker_ipc = make_ref_counted<WebWorkerClient>(move(transport));
    setup_worker_ipc_callbacks(realm);

    m_worker_ipc->async_connect_to_request_server(move(response.request_server_handle));
    m_worker_ipc->async_connect_to_image_decoder(move(response.image_decoder_handle));

    auto serialized_outside_settings = m_outside_settings->serialize();

    m_worker_ipc->async_start_worker(m_url, m_worker_options.type, m_worker_options.credentials, m_worker_options.name, move(data_holder), serialized_outside_settings, m_agent_type);
}

void WorkerAgentParent::terminate()
{
    if (m_worker_termination_requested)
        return;
    m_worker_termination_requested = true;

    // The helper owns exactly one WorkerHost. The close message stops that
    // host and exits its event loop; it is deliberately asynchronous because
    // Worker.terminate() returns undefined and must not wait for script code.
    if (m_worker_ipc) {
        m_worker_ipc->begin_close();
        m_worker_ipc->async_close_worker();
    }
}

void WorkerAgentParent::queue_worker_error_event()
{
    // Helper creation, a rejected script, and an abrupt helper exit can be
    // delivered by independent IPC/process-supervisor paths. They are all
    // terminal for this worker and must not produce duplicate error events.
    if (m_worker_terminal_event_queued)
        return;
    m_worker_terminal_event_queued = true;

    auto outside_settings = m_outside_settings;
    auto worker_event_target = m_worker_event_target;
    // See: https://html.spec.whatwg.org/multipage/workers.html#worker-processing-model,
    // onComplete handler for fetching script.
    queue_global_task(Task::Source::DOMManipulation, outside_settings->global_object(), GC::create_function(outside_settings->heap(), [outside_settings, worker_event_target]() {
        worker_event_target->dispatch_event(DOM::Event::create(outside_settings->realm(), EventNames::error));
    }));
}

void WorkerAgentParent::setup_worker_ipc_callbacks(JS::Realm& realm)
{
    // NOTE: As long as WorkerAgentParent is alive, realm and m_worker_ipc will be alive.
    m_worker_ipc->on_request_cookie = [realm = GC::RawRef<JS::Realm> { realm }](URL::URL const& url, HTTP::Cookie::Source source) {
        auto& client = Bindings::principal_host_defined_page(realm).client();
        return client.page_did_request_cookie(url, source);
    };
    m_worker_ipc->on_set_cookie = [realm = GC::RawRef<JS::Realm> { realm }](URL::URL const& url, HTTP::Cookie::ParsedCookie const& cookie, HTTP::Cookie::Source source) {
        auto& client = Bindings::principal_host_defined_page(realm).client();
        client.page_did_set_cookie(url, cookie, source);
    };
#if defined(AK_OS_RINOS)
    m_worker_ipc->on_request_http_cookie_owner = [
        realm = GC::RawRef<JS::Realm> { realm },
        outside_settings = GC::RawRef<EnvironmentSettingsObject> { *m_outside_settings }](
            u32 operation, ByteString request_url, ByteString origin,
            ByteString cookie_data, u32 policy) {
        if ((operation != RIN_WEBCONTENT_SERVICE_WORKER_OWNER_GET_HTTP_COOKIE_HEADER &&
             operation != RIN_WEBCONTENT_SERVICE_WORKER_OWNER_COMMIT_HTTP_COOKIES) ||
            request_url.is_empty() ||
            cookie_data.length() > RIN_WEBCONTENT_HTTP_COOKIE_OWNER_MAX_DATA_BYTES)
            return Messages::WebWorkerClient::RequestHttpCookieOwnerResponse { false, false, 0, {} };

        auto parsed_url = URL::Parser::basic_parse(
            StringView { request_url.characters(), request_url.length() });
        if (!parsed_url.has_value())
            return Messages::WebWorkerClient::RequestHttpCookieOwnerResponse { false, false, 0, {} };

        ByteString trusted_data;
        ByteString trusted_origin;
        if (operation == RIN_WEBCONTENT_SERVICE_WORKER_OWNER_GET_HTTP_COOKIE_HEADER) {
            if (origin != parsed_url->origin().serialize().to_byte_string())
                return Messages::WebWorkerClient::RequestHttpCookieOwnerResponse { false, false, 0, {} };
            trusted_data = trusted_worker_cookie_context(*outside_settings, *parsed_url, false);
            trusted_origin = parsed_url->origin().serialize().to_byte_string();
        } else {
            auto expected_origin = outside_settings->origin().serialize().to_byte_string();
            if (origin != expected_origin)
                return Messages::WebWorkerClient::RequestHttpCookieOwnerResponse { false, false, 0, {} };
            auto line_end = cookie_data.find('\n');
            if (!line_end.has_value() || line_end.value() + 1u >= cookie_data.length())
                return Messages::WebWorkerClient::RequestHttpCookieOwnerResponse { false, false, 0, {} };
            auto context = trusted_worker_cookie_context(*outside_settings, *parsed_url, true);
            trusted_data = ByteString::formatted("{}{}", context,
                cookie_data.substring(line_end.value() + 1u,
                    cookie_data.length() - line_end.value() - 1u));
            trusted_origin = move(expected_origin);
        }

        auto& client = Bindings::principal_host_defined_page(realm).client();
        auto response = client.request_http_cookie_owner(
            operation, move(request_url), move(trusted_origin),
            move(trusted_data), policy);
        return Messages::WebWorkerClient::RequestHttpCookieOwnerResponse {
            response.accepted, response.found, response.generation,
            move(response.data) };
    };
#endif
    m_worker_ipc->on_request_worker_agent = [realm = GC::RawRef<JS::Realm> { realm }](Web::Bindings::AgentType worker_type) -> Messages::WebWorkerClient::RequestWorkerAgentResponse {
        auto& client = Bindings::principal_host_defined_page(realm).client();
        auto response = client.request_worker_agent(worker_type);
        return { move(response.worker_handle), move(response.request_server_handle), move(response.image_decoder_handle) };
    };
    m_worker_ipc->on_worker_script_load_failure = [self = GC::Weak { *this }]() {
        if (!self)
            return;
        self->queue_worker_error_event();
    };
    m_worker_ipc->on_worker_crash = [self = GC::Weak { *this }]() {
        if (!self)
            return;
        self->queue_worker_error_event();
    };
}

void WorkerAgentParent::visit_edges(Cell::Visitor& visitor)
{
    Base::visit_edges(visitor);
    visitor.visit(m_message_port);
    visitor.visit(m_outside_port);
    visitor.visit(m_outside_settings);
    visitor.visit(m_worker_event_target);
}

}
