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
    stream->m_audio_tracks.append(track);
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
}

bool MediaStream::active() const
{
    for (auto const& track : m_audio_tracks) {
        if (track->is_live())
            return true;
    }
    return false;
}

Vector<GC::Ref<MediaStreamTrack>> MediaStream::get_audio_tracks() const
{
    return m_audio_tracks;
}

Vector<GC::Ref<MediaStreamTrack>> MediaStream::get_tracks() const
{
    return m_audio_tracks;
}

}
