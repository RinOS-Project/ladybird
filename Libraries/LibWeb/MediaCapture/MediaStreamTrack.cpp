/*
 * Copyright (c) 2026, RinOS contributors
 *
 * SPDX-License-Identifier: BSD-2-Clause
 */

#include <LibWeb/Bindings/Intrinsics.h>
#include <LibWeb/MediaCapture/MediaStreamTrack.h>
#include <string.h>

namespace Web::MediaCapture {

GC_DEFINE_ALLOCATOR(MediaStreamTrack);

GC::Ref<MediaStreamTrack> MediaStreamTrack::create(JS::Realm& realm, int audio_stream, String id, String label)
{
    auto track = realm.create<MediaStreamTrack>(realm);
    track->m_audio_stream = audio_stream;
    track->m_id = move(id);
    track->m_label = move(label);
    return track;
}

MediaStreamTrack::MediaStreamTrack(JS::Realm& realm)
    : DOM::EventTarget(realm)
{
}

MediaStreamTrack::~MediaStreamTrack()
{
    stop();
}

void MediaStreamTrack::initialize(JS::Realm& realm)
{
    WEB_SET_PROTOTYPE_FOR_INTERFACE(MediaStreamTrack);
    Base::initialize(realm);
}

void MediaStreamTrack::stop()
{
    (void)revoke_capture();
}

bool MediaStreamTrack::revoke_capture()
{
    m_capture_revoked = true;
    if (m_audio_stream < 0)
        return true;

    if (rin_audio_service_stream_destroy(m_audio_stream) != 0)
        return false;
    m_audio_stream = -1;
    return true;
}

int MediaStreamTrack::read_audio(void* samples, uint32_t bytes)
{
    if (!is_live())
        return 0;

    auto result = rin_audio_service_capture_stream_read(m_audio_stream, samples, bytes);
    if (result < 0) {
        stop();
        return result;
    }

    // A disabled live track continues consuming its source while exposing
    // silence, so toggling enabled does not replay microphone data that was
    // buffered while the track was disabled.
    if (!m_enabled && result > 0)
        memset(samples, 0, static_cast<size_t>(result));

    return result;
}

}
