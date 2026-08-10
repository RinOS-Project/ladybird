/*
 * Copyright (c) 2022, Linus Groh <linusg@serenityos.org>
 *
 * SPDX-License-Identifier: BSD-2-Clause
 */

#include <LibGC/Heap.h>
#include <LibJS/Runtime/VM.h>
#include <LibWeb/Fetch/Fetching/PendingResponse.h>
#include <LibWeb/Fetch/Infrastructure/HTTP/Requests.h>
#include <LibWeb/Platform/EventLoopPlugin.h>

namespace Web::Fetch::Fetching {

GC_DEFINE_ALLOCATOR(PendingResponse);

GC::Ref<PendingResponse> PendingResponse::create(JS::VM& vm, GC::Ref<Infrastructure::Request> request)
{
    return vm.heap().allocate<PendingResponse>(request);
}

GC::Ref<PendingResponse> PendingResponse::create(JS::VM& vm, GC::Ref<Infrastructure::Request> request, Infrastructure::RootedResponseReferences rooted_responses)
{
    auto pending_response = vm.heap().allocate<PendingResponse>(request);
    pending_response->resolve(move(rooted_responses));
    return pending_response;
}

PendingResponse::PendingResponse(GC::Ref<Infrastructure::Request> request)
    : m_request(request)
{
    m_request->add_pending_response({}, *this);
}

void PendingResponse::visit_edges(JS::Cell::Visitor& visitor)
{
    Base::visit_edges(visitor);
    visitor.visit(m_callback);
    visitor.visit(m_request);
    if (m_rooted_response_references.has_value())
        visitor.visit(*m_rooted_response_references);
}

void PendingResponse::when_loaded(Callback callback)
{
    VERIFY(!m_callback);
    m_callback = GC::create_function(heap(), move(callback));
    if (m_rooted_response_references.has_value())
        run_callback();
}

void PendingResponse::resolve(Infrastructure::RootedResponseReferences rooted_responses)
{
    VERIFY(!m_rooted_response_references.has_value());
    m_rooted_response_references = move(rooted_responses);
    if (m_callback)
        run_callback();
}

void PendingResponse::run_callback()
{
    VERIFY(m_callback);
    VERIFY(m_rooted_response_references.has_value());
    Platform::EventLoopPlugin::the().deferred_invoke(GC::create_function(heap(), [this] {
        VERIFY(m_callback);
        VERIFY(m_rooted_response_references.has_value());
        m_callback->function()(m_rooted_response_references.value());
        m_request->remove_pending_response({}, *this);
    }));
}

}
