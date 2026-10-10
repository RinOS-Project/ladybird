/*
 * Copyright (c) 2026, RinOS contributors
 *
 * SPDX-License-Identifier: BSD-2-Clause
 */

#pragma once

#include <LibWeb/Bindings/MediaStreamTrackPrototype.h>
#include <LibWeb/DOM/EventTarget.h>
#include <LibWeb/Page/Page.h>
#include <LibWeb/WebIDL/Types.h>
#include <LibGfx/Forward.h>

#include <rinruntime/rin_audio_service_client.h>

namespace Web::MediaCapture {

class MediaStreamTrack final : public DOM::EventTarget {
    WEB_PLATFORM_OBJECT(MediaStreamTrack, DOM::EventTarget);
    GC_DECLARE_ALLOCATOR(MediaStreamTrack);

public:
    static GC::Ref<MediaStreamTrack> create(JS::Realm&, int audio_stream, String id, String label);
    static GC::Ref<MediaStreamTrack> create_video(
        JS::Realm&, PageClient&, u64 camera_session,
        CameraCaptureProfile const&, String id, String label);

    virtual ~MediaStreamTrack() override;

    String kind() const { return m_is_video ? "video"_string : "audio"_string; }
    String id() const { return m_id; }
    String label() const { return m_label; }
    bool enabled() const { return m_enabled; }
    void set_enabled(bool enabled) { m_enabled = enabled; }
    bool muted() const { return m_muted; }
    String ready_state() const { return is_live() ? "live"_string : "ended"_string; }
    void stop();
    bool revoke_capture();

    // Internal capture consumer hook. The returned byte count is the number
    // of PCM bytes read from the authenticated Audio Service ring.
    int read_audio(void* samples, uint32_t bytes);
    bool is_live() const
    {
        return !m_capture_revoked &&
               (m_is_video ? m_camera_session_id != 0u : m_audio_stream >= 0);
    }
    RefPtr<Gfx::ImmutableBitmap> read_video_frame();

private:
    explicit MediaStreamTrack(JS::Realm&);

    virtual void initialize(JS::Realm&) override;

    String m_id;
    String m_label;
    int m_audio_stream { -1 };
    PageClient* m_page_client { nullptr };
    CameraCaptureProfile m_camera_profile;
    u64 m_camera_session_id { 0u };
    u64 m_last_camera_sequence { 0u };
    u64 m_pending_camera_sequence { 0u };
    u32 m_pending_camera_total_size { 0u };
    u32 m_pending_camera_offset { 0u };
    u32 m_pending_camera_format { 0u };
    u32 m_pending_camera_width { 0u };
    u32 m_pending_camera_height { 0u };
    Vector<u8> m_pending_camera_frame;
    RefPtr<Gfx::ImmutableBitmap> m_current_camera_frame;
    bool m_is_video { false };
    bool m_enabled { true };
    bool m_muted { false };
    bool m_capture_revoked { false };
};

}
