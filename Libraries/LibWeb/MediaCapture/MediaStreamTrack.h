/*
 * Copyright (c) 2026, RinOS contributors
 *
 * SPDX-License-Identifier: BSD-2-Clause
 */

#pragma once

#include <LibWeb/Bindings/MediaStreamTrackPrototype.h>
#include <LibWeb/DOM/EventTarget.h>
#include <LibWeb/WebIDL/Types.h>

#include "../../../../../libs/rinruntime/include/rinruntime/rin_audio_service_client.h"

namespace Web::MediaCapture {

class MediaStreamTrack final : public DOM::EventTarget {
    WEB_PLATFORM_OBJECT(MediaStreamTrack, DOM::EventTarget);
    GC_DECLARE_ALLOCATOR(MediaStreamTrack);

public:
    static GC::Ref<MediaStreamTrack> create(JS::Realm&, int audio_stream, String id, String label);

    virtual ~MediaStreamTrack() override;

    String kind() const { return "audio"_string; }
    String id() const { return m_id; }
    String label() const { return m_label; }
    bool enabled() const { return m_enabled; }
    void set_enabled(bool enabled) { m_enabled = enabled; }
    bool muted() const { return m_muted; }
    String ready_state() const { return is_live() ? "live"_string : "ended"_string; }
    void stop();

    // Internal capture consumer hook. The returned byte count is the number
    // of PCM bytes read from the authenticated Audio Service ring.
    int read_audio(void* samples, uint32_t bytes);
    bool is_live() const { return m_audio_stream >= 0; }

private:
    explicit MediaStreamTrack(JS::Realm&);

    virtual void initialize(JS::Realm&) override;

    String m_id;
    String m_label;
    int m_audio_stream { -1 };
    bool m_enabled { true };
    bool m_muted { false };
};

}
