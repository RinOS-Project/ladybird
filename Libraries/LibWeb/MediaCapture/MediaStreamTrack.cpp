/*
 * Copyright (c) 2026, RinOS contributors
 *
 * SPDX-License-Identifier: BSD-2-Clause
 */

#include <LibWeb/Bindings/Intrinsics.h>
#include <LibWeb/MediaCapture/MediaStreamTrack.h>

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
    if (m_audio_stream < 0)
        return;

    auto audio_stream = m_audio_stream;
    m_audio_stream = -1;
    (void)rin_audio_service_stream_destroy(audio_stream);
}

int MediaStreamTrack::read_audio(void* samples, uint32_t bytes)
{
    if (!m_enabled || !is_live())
        return 0;

    auto result = rin_audio_service_capture_stream_read(m_audio_stream, samples, bytes);
    if (result < 0)
        stop();
    return result;
}

}
