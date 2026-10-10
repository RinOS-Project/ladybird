/*
 * Copyright (c) 2026, RinOS contributors
 *
 * SPDX-License-Identifier: BSD-2-Clause
 */

#include <LibWeb/Bindings/Intrinsics.h>
#include <LibWeb/MediaCapture/MediaStream.h>

namespace Web::MediaCapture {

GC_DEFINE_ALLOCATOR(MediaStream);

GC::Ref<MediaStream> MediaStream::create(JS::Realm& realm, GC::Ref<MediaStreamTrack> track)
{
    auto stream = realm.create<MediaStream>(realm);
    if (track->kind() == "video"_string)
        stream->m_video_tracks.append(track);
    else
        stream->m_audio_tracks.append(track);
    return stream;
}

GC::Ref<MediaStream> MediaStream::create(
    JS::Realm& realm, Vector<GC::Ref<MediaStreamTrack>> tracks)
{
    auto stream = realm.create<MediaStream>(realm);
    for (auto& track : tracks) {
        if (track->kind() == "video"_string)
            stream->m_video_tracks.append(track);
        else
            stream->m_audio_tracks.append(track);
    }
    return stream;
}

MediaStream::MediaStream(JS::Realm& realm)
    : DOM::EventTarget(realm)
{
}

void MediaStream::initialize(JS::Realm& realm)
{
    WEB_SET_PROTOTYPE_FOR_INTERFACE(MediaStream);
    Base::initialize(realm);
}

void MediaStream::visit_edges(Cell::Visitor& visitor)
{
    Base::visit_edges(visitor);
    for (auto& track : m_audio_tracks)
        visitor.visit(track);
    for (auto& track : m_video_tracks)
        visitor.visit(track);
}

bool MediaStream::active() const
{
    for (auto const& track : m_audio_tracks) {
        if (track->is_live())
            return true;
    }
    for (auto const& track : m_video_tracks) {
        if (track->is_live())
            return true;
    }
    return false;
}

Vector<GC::Ref<MediaStreamTrack>> MediaStream::get_audio_tracks() const
{
    return m_audio_tracks;
}

Vector<GC::Ref<MediaStreamTrack>> MediaStream::get_video_tracks() const
{
    return m_video_tracks;
}

Vector<GC::Ref<MediaStreamTrack>> MediaStream::get_tracks() const
{
    Vector<GC::Ref<MediaStreamTrack>> tracks;
    tracks.ensure_capacity(m_audio_tracks.size() + m_video_tracks.size());
    tracks.extend(m_audio_tracks);
    tracks.extend(m_video_tracks);
    return tracks;
}

}
