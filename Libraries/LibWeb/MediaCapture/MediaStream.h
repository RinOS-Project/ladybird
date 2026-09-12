/*
 * Copyright (c) 2026, RinOS contributors
 *
 * SPDX-License-Identifier: BSD-2-Clause
 */

#pragma once

#include <LibWeb/Bindings/MediaStreamPrototype.h>
#include <LibWeb/DOM/EventTarget.h>
#include <LibWeb/MediaCapture/MediaStreamTrack.h>

namespace Web::MediaCapture {

class MediaStream final : public DOM::EventTarget {
    WEB_PLATFORM_OBJECT(MediaStream, DOM::EventTarget);
    GC_DECLARE_ALLOCATOR(MediaStream);

public:
    static GC::Ref<MediaStream> create(JS::Realm&, GC::Ref<MediaStreamTrack>);

    bool active() const;
    Vector<GC::Ref<MediaStreamTrack>> get_audio_tracks() const;
    Vector<GC::Ref<MediaStreamTrack>> get_tracks() const;

private:
    explicit MediaStream(JS::Realm&);

    virtual void initialize(JS::Realm&) override;
    virtual void visit_edges(Cell::Visitor&) override;

    Vector<GC::Ref<MediaStreamTrack>> m_audio_tracks;
};

}
