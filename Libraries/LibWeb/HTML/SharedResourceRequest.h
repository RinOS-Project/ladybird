/*
 * Copyright (c) 2023, Andreas Kling <andreas@ladybird.org>
 *
 * SPDX-License-Identifier: BSD-2-Clause
 */

#pragma once

#include <LibGC/Function.h>
#include <LibGC/Ptr.h>
#include <LibJS/Heap/Cell.h>
#include <LibURL/URL.h>
#include <LibWeb/DOM/DocumentLoadEventDelayer.h>
#include <LibWeb/Forward.h>

namespace Web::HTML {

class SharedResourceRequest final : public JS::Cell {
    GC_CELL(SharedResourceRequest, JS::Cell);
    GC_DECLARE_ALLOCATOR(SharedResourceRequest);

public:
    static constexpr bool OVERRIDES_FINALIZE = true;

    [[nodiscard]] static GC::Ref<SharedResourceRequest> get_or_create(JS::Realm&, GC::Ref<Page>, URL::URL const&);
    // Image requests carry a CORS mode and origin in addition to their URL.
    // They must not reuse the document's URL-only resource cache, since doing
    // so can incorrectly turn an opaque fetch into an origin-clean image.
    [[nodiscard]] static GC::Ref<SharedResourceRequest> create_uncached(JS::Realm&, GC::Ref<Page>, URL::URL const&);

    virtual ~SharedResourceRequest() override;

    URL::URL const& url() const { return m_url; }

    [[nodiscard]] GC::Ptr<DecodedImageData> image_data() const;
    bool is_origin_clean() const { return m_origin_clean; }

    [[nodiscard]] GC::Ptr<Fetch::Infrastructure::FetchController> fetch_controller();
    void set_fetch_controller(GC::Ptr<Fetch::Infrastructure::FetchController>);

    void fetch_resource(JS::Realm&, GC::Ref<Fetch::Infrastructure::Request>);

    void add_callbacks(Function<void()> on_finish, Function<void()> on_fail);

    bool is_fetching() const;
    bool needs_fetching() const;

private:
    explicit SharedResourceRequest(GC::Ref<Page>, URL::URL, GC::Ref<DOM::Document>, bool is_document_cached);

    virtual void finalize() override;
    virtual void visit_edges(JS::Cell::Visitor&) override;

    void handle_successful_fetch(URL::URL const&, StringView mime_type, ByteBuffer data);
    void handle_failed_fetch();
    void handle_successful_resource_load();

    enum class State {
        New,
        Fetching,
        Finished,
        Failed,
    };

    State m_state { State::New };

    GC::Ref<Page> m_page;

    struct Callbacks {
        GC::Ptr<GC::Function<void()>> on_finish;
        GC::Ptr<GC::Function<void()>> on_fail;
    };
    Vector<Callbacks> m_callbacks;

    URL::URL m_url;
    GC::Ptr<DecodedImageData> m_image_data;
    bool m_origin_clean { false };
    GC::Ptr<Fetch::Infrastructure::FetchController> m_fetch_controller;

    GC::Ptr<DOM::Document> m_document;
    bool m_is_document_cached { false };

    Optional<DOM::DocumentLoadEventDelayer> m_load_event_delayer;
};

}
