/*
 * Copyright (c) 2024, Jamie Mansfield <jmansfield@cadixdev.org>
 *
 * SPDX-License-Identifier: BSD-2-Clause
 */

#pragma once

#include <AK/Badge.h>
#include <LibGC/RootVector.h>
#include <LibWeb/DOM/EventTarget.h>
#include <LibWeb/HTML/TextTrack.h>

namespace Web::HTML {

class HTMLMediaElement;

class TextTrackList final : public DOM::EventTarget {
    WEB_PLATFORM_OBJECT(TextTrackList, DOM::EventTarget);
    GC_DECLARE_ALLOCATOR(TextTrackList);

public:
    static GC::Ref<TextTrackList> create(JS::Realm&);
    virtual ~TextTrackList() override;

    size_t length() const;

    GC::Ref<TextTrack> at(size_t index) const { return m_text_tracks.at(index); }

    GC::Ptr<TextTrack> get_track_by_id(StringView id) const;
    bool contains(TextTrack const&) const;
    void add_track(Badge<HTMLMediaElement>, GC::Ref<TextTrack>, HTMLMediaElement&);
    bool remove_track(Badge<HTMLMediaElement>, TextTrack&);
    size_t index_of(TextTrack const&) const;

    void set_onchange(WebIDL::CallbackType*);
    WebIDL::CallbackType* onchange();

    void set_onaddtrack(WebIDL::CallbackType*);
    WebIDL::CallbackType* onaddtrack();

    void set_onremovetrack(WebIDL::CallbackType*);
    WebIDL::CallbackType* onremovetrack();

private:
    TextTrackList(JS::Realm&);

    virtual void initialize(JS::Realm&) override;
    virtual void visit_edges(Visitor&) override;

    virtual JS::ThrowCompletionOr<Optional<JS::PropertyDescriptor>> internal_get_own_property(JS::PropertyKey const& property_name) const override;

    Vector<GC::Ref<TextTrack>> m_text_tracks;
};

}
