/*
 * Copyright (c) 2022, Linus Groh <linusg@serenityos.org>
 *
 * SPDX-License-Identifier: BSD-2-Clause
 */

#pragma once

#include <LibGC/Ptr.h>
#include <LibJS/Forward.h>
#include <LibJS/Heap/Cell.h>
#include <LibWeb/Fetch/Infrastructure/HTTP/ResponseRooting.h>
#include <LibWeb/Fetch/Infrastructure/HTTP/Responses.h>

namespace Web::Fetch::Fetching {

// Non-standard wrapper around a possibly pending Infrastructure::Response.
// This is needed to fit the asynchronous nature of ResourceLoader into the synchronous expectations
// of the Fetch spec - we run 'in parallel' as a deferred_invoke(), which is still on the main thread;
// therefore we use callbacks to run portions of the spec that require waiting for an HTTP load.
class PendingResponse : public JS::Cell {
    GC_CELL(PendingResponse, JS::Cell);
    GC_DECLARE_ALLOCATOR(PendingResponse);

public:
    using Callback = Function<void(Infrastructure::RootedResponseReferences)>;

    [[nodiscard]] static GC::Ref<PendingResponse> create(JS::VM&, GC::Ref<Infrastructure::Request>);
    [[nodiscard]] static GC::Ref<PendingResponse> create(JS::VM&, GC::Ref<Infrastructure::Request>, Infrastructure::RootedResponseReferences);

    void when_loaded(Callback);
    void resolve(Infrastructure::RootedResponseReferences);
    bool is_resolved() const { return m_rooted_response_references.has_value(); }
    [[nodiscard]] Infrastructure::OptionalRootedResponseReferences const& rooted_response_references() const { return m_rooted_response_references; }

private:
    PendingResponse(GC::Ref<Infrastructure::Request>);

    virtual void visit_edges(JS::Cell::Visitor&) override;

    void run_callback();

    GC::Ptr<GC::Function<void(Infrastructure::RootedResponseReferences)>> m_callback;
    GC::Ref<Infrastructure::Request> m_request;
    Infrastructure::OptionalRootedResponseReferences m_rooted_response_references;
};

}
