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
#include <LibWeb/HTML/CORSSettingAttribute.h>

namespace Web::HTML {

class SharedResourceRequest final : public JS::Cell {
    GC_CELL(SharedResourceRequest, JS::Cell);
    GC_DECLARE_ALLOCATOR(SharedResourceRequest);

public:
    static constexpr bool OVERRIDES_FINALIZE = true;

    [[nodiscard]] static GC::Ref<SharedResourceRequest> get_or_create(JS::Realm&, GC::Ref<Page>, URL::URL const&, CORSSettingAttribute = CORSSettingAttribute::NoCORS);

    virtual ~SharedResourceRequest() override;

    URL::URL const& url() const { return m_url; }
    CORSSettingAttribute cors_setting() const { return m_cors_setting; }
    bool is_cors_cross_origin() const { return m_is_cors_cross_origin; }

    [[nodiscard]] GC::Ptr<DecodedImageData> image_data() const;

    [[nodiscard]] GC::Ptr<Fetch::Infrastructure::FetchController> fetch_controller();
    void set_fetch_controller(GC::Ptr<Fetch::Infrastructure::FetchController>);

    void fetch_resource(JS::Realm&, GC::Ref<Fetch::Infrastructure::Request>);

    void add_callbacks(Function<void()> on_finish, Function<void()> on_fail);

    bool is_fetching() const;
    bool needs_fetching() const;

private:
    explicit SharedResourceRequest(GC::Ref<Page>, URL::URL, GC::Ref<DOM::Document>, CORSSettingAttribute);

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
    CORSSettingAttribute m_cors_setting;
    bool m_is_cors_cross_origin { false };
    GC::Ptr<DecodedImageData> m_image_data;
    GC::Ptr<Fetch::Infrastructure::FetchController> m_fetch_controller;

    GC::Ptr<DOM::Document> m_document;

    Optional<DOM::DocumentLoadEventDelayer> m_load_event_delayer;
};

}
