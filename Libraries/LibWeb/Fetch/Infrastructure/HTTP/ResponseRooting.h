/*
 * Copyright (c) 2026, OpenAI
 *
 * SPDX-License-Identifier: BSD-2-Clause
 */

#pragma once

#include <AK/Optional.h>
#include <LibGC/Ptr.h>
#include <LibJS/Heap/Cell.h>
#include <LibWeb/Fetch/Infrastructure/HTTP/Responses.h>

namespace Web::Fetch::Infrastructure {

class ResponseReferenceHolder final : public JS::Cell {
    GC_CELL(ResponseReferenceHolder, JS::Cell);
    GC_DECLARE_ALLOCATOR(ResponseReferenceHolder);

public:
    [[nodiscard]] static GC::Ref<ResponseReferenceHolder> create(GC::Ref<Response> response, GC::Ref<Response> internal_response)
    {
        return response->heap().allocate<ResponseReferenceHolder>(response, internal_response);
    }

    [[nodiscard]] GC::Ref<Response> response() const { return m_response; }
    [[nodiscard]] GC::Ref<Response> internal_response() const { return m_internal_response; }

private:
    ResponseReferenceHolder(GC::Ref<Response> response, GC::Ref<Response> internal_response)
        : m_response(response)
        , m_internal_response(internal_response)
    {
    }

    virtual void visit_edges(JS::Cell::Visitor& visitor) override
    {
        Base::visit_edges(visitor);
        visitor.visit(m_response);
        visitor.visit(m_internal_response);
    }

    GC::Ref<Response> m_response;
    GC::Ref<Response> m_internal_response;
};

using RootedResponseReferences = GC::Ref<ResponseReferenceHolder>;
using OptionalRootedResponseReferences = Optional<RootedResponseReferences>;

[[nodiscard]] inline RootedResponseReferences root_response_references(GC::Ref<Response> response, GC::Ref<Response> internal_response)
{
    return ResponseReferenceHolder::create(response, internal_response);
}

[[nodiscard]] inline OptionalRootedResponseReferences root_response_references_if_present(GC::Ptr<Response> response, GC::Ptr<Response> internal_response)
{
    if (!response)
        return {};

    VERIFY(internal_response);
    return root_response_references(*response, *internal_response);
}

[[nodiscard]] inline RootedResponseReferences root_response_references(GC::Ref<Response> response)
{
    // Use this only while `response` is already live in the current scope. Async callers must capture the
    // public/internal pair explicitly instead of unwrapping a filtered response later.
    auto internal_response = response->unsafe_response();
    return root_response_references(response, internal_response);
}

}
