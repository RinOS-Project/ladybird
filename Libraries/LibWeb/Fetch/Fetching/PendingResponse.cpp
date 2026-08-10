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

GC::Ref<PendingResponse> PendingResponse::create(GC::Ref<Infrastructure::Request> request)
{
    return GC::Heap::the().allocate<PendingResponse>(request);
}

GC::Ref<PendingResponse> PendingResponse::create(GC::Ref<Infrastructure::Request> request, GC::Ref<Infrastructure::Response> response)
{
    return GC::Heap::the().allocate<PendingResponse>(request, response);
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
    m_callback = GC::create_function(GC::Heap::the(), move(callback));
    if (m_response)
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
    VERIFY(m_response);
    Platform::EventLoopPlugin::the().deferred_invoke(GC::create_function(GC::Heap::the(), [this] {
        VERIFY(m_callback);
        VERIFY(m_rooted_response_references.has_value());
        m_callback->function()(m_rooted_response_references.value());
        m_request->remove_pending_response({}, *this);
    }));
}

}
