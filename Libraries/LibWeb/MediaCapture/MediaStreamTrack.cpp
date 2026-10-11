/*
 * Copyright (c) 2026, RinOS contributors
 *
 * SPDX-License-Identifier: BSD-2-Clause
 */

#include <LibWeb/Bindings/Intrinsics.h>
#include <LibWeb/MediaCapture/MediaStreamTrack.h>
#include <LibGfx/Bitmap.h>
#include <LibGfx/ImmutableBitmap.h>
#include <LibGfx/ImageFormats/ImageDecoder.h>
#include <rin/web/webcontent_protocol.h>
#include "webcontent_client_policy.h"
#include <string.h>

namespace Web::MediaCapture {

static void scrub_camera_bytes(Vector<u8>& bytes)
{
    volatile u8* data = bytes.data();
    for (size_t index = 0; index < bytes.size(); ++index)
        data[index] = 0u;
    bytes.clear();
}

class CameraBufferScrubber {
public:
    explicit CameraBufferScrubber(Vector<u8>& bytes)
        : m_bytes(bytes)
    {
    }

    ~CameraBufferScrubber() { scrub_camera_bytes(m_bytes); }

private:
    Vector<u8>& m_bytes;
};

GC_DEFINE_ALLOCATOR(MediaStreamTrack);

GC::Ref<MediaStreamTrack> MediaStreamTrack::create(JS::Realm& realm, int audio_stream, String id, String label)
{
    auto track = realm.create<MediaStreamTrack>(realm);
    track->m_audio_stream = audio_stream;
    track->m_id = move(id);
    track->m_label = move(label);
    return track;
}

GC::Ref<MediaStreamTrack> MediaStreamTrack::create_video(
    JS::Realm& realm, PageClient& page_client, u64 camera_session,
    CameraCaptureProfile const& profile, String id, String label)
{
    auto track = realm.create<MediaStreamTrack>(realm);
    track->m_page_client = &page_client;
    track->m_camera_session_id = camera_session;
    track->m_camera_profile = profile;
    track->m_is_video = true;
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
    if (m_is_video) {
        if (m_camera_session_id == 0u)
            return true;
        auto* page_client = m_page_client;
        auto session_id = m_camera_session_id;
        m_page_client = nullptr;
        m_camera_session_id = 0u;
        scrub_camera_bytes(m_pending_camera_frame);
        m_current_camera_frame = nullptr;
        return page_client != nullptr &&
               page_client->page_did_close_camera_capture(session_id);
    }
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

static u8 clamp_camera_channel(int value)
{
    if (value < 0)
        return 0u;
    if (value > 255)
        return 255u;
    return static_cast<u8>(value);
}

static RefPtr<Gfx::ImmutableBitmap> decode_camera_frame(
    ReadonlyBytes bytes, u32 format, u32 width, u32 height)
{
    if (width == 0u || height == 0u || width > 16384u || height > 16384u)
        return nullptr;
    if (format == RIN_WEBCONTENT_CAMERA_FORMAT_MJPEG) {
        auto decoder_or_error = Gfx::ImageDecoder::try_create_for_raw_bytes(
            bytes, ByteString("image/jpeg"));
        if (decoder_or_error.is_error() || !decoder_or_error.value())
            return nullptr;
        auto decoder = decoder_or_error.release_value();
        if (decoder->size().width() != static_cast<int>(width) ||
            decoder->size().height() != static_cast<int>(height))
            return nullptr;
        auto frame_or_error = decoder->frame(0u);
        if (frame_or_error.is_error())
            return nullptr;
        return Gfx::ImmutableBitmap::create(frame_or_error.release_value().image);
    }

    u64 const pixels = static_cast<u64>(width) * height;
    if ((format == RIN_WEBCONTENT_CAMERA_FORMAT_RGBX8888 &&
         bytes.size() != pixels * 4u) ||
        (format == RIN_WEBCONTENT_CAMERA_FORMAT_NV12 &&
         (((width | height) & 1u) != 0u ||
          bytes.size() != pixels + pixels / 2u)))
        return nullptr;
    if (format != RIN_WEBCONTENT_CAMERA_FORMAT_RGBX8888 &&
        format != RIN_WEBCONTENT_CAMERA_FORMAT_NV12)
        return nullptr;

    auto bitmap_or_error = Gfx::Bitmap::create(
        Gfx::BitmapFormat::BGRx8888,
        { static_cast<int>(width), static_cast<int>(height) });
    if (bitmap_or_error.is_error())
        return nullptr;
    auto bitmap = bitmap_or_error.release_value();
    auto const* data = bytes.data();
    if (format == RIN_WEBCONTENT_CAMERA_FORMAT_RGBX8888) {
        for (u32 y = 0u; y < height; ++y) {
            for (u32 x = 0u; x < width; ++x) {
                size_t const offset = (static_cast<size_t>(y) * width + x) * 4u;
                bitmap->set_pixel(static_cast<int>(x), static_cast<int>(y),
                    Gfx::Color(data[offset], data[offset + 1u],
                               data[offset + 2u]));
            }
        }
    } else {
        auto const* uv_plane = data + pixels;
        for (u32 y = 0u; y < height; ++y) {
            for (u32 x = 0u; x < width; ++x) {
                size_t const pixel_index = static_cast<size_t>(y) * width + x;
                size_t const chroma_index =
                    static_cast<size_t>(y / 2u) * width + (x & ~1u);
                int const raw_luma = static_cast<int>(data[pixel_index]) - 16;
                int const luma = raw_luma < 0 ? 0 : raw_luma;
                int const u = static_cast<int>(uv_plane[chroma_index]) - 128;
                int const v = static_cast<int>(uv_plane[chroma_index + 1u]) - 128;
                int const red = (298 * luma + 409 * v + 128) >> 8;
                int const green = (298 * luma - 100 * u - 208 * v + 128) >> 8;
                int const blue = (298 * luma + 516 * u + 128) >> 8;
                bitmap->set_pixel(static_cast<int>(x), static_cast<int>(y),
                    Gfx::Color(clamp_camera_channel(red),
                               clamp_camera_channel(green),
                               clamp_camera_channel(blue)));
            }
        }
    }
    return Gfx::ImmutableBitmap::create(bitmap);
}

RefPtr<Gfx::ImmutableBitmap> MediaStreamTrack::read_video_frame()
{
    if (!is_live() || !m_is_video || m_page_client == nullptr)
        return nullptr;

    Vector<u8> wire_chunk;
    CameraBufferScrubber wire_chunk_scrubber(wire_chunk);
    const i32 read_result = m_page_client->page_did_read_camera_capture(
        m_camera_session_id, wire_chunk);
    if (read_result == 1)
        return nullptr;
    if (read_result != 0 ||
        wire_chunk.size() < sizeof(RinWebContentCameraCaptureChunkV1)) {
        stop();
        return nullptr;
    }

    RinWebContentCameraCaptureChunkV1 chunk {};
    memcpy(&chunk, wire_chunk.data(), sizeof(chunk));
    auto const* payload = wire_chunk.data() + sizeof(chunk);
    const size_t payload_size = wire_chunk.size() - sizeof(chunk);
    if (!rin_webcontent_client_camera_capture_chunk_valid(
            &chunk, payload, static_cast<u32>(wire_chunk.size())) ||
        chunk.format != m_camera_profile.format ||
        chunk.width != m_camera_profile.width ||
        chunk.height != m_camera_profile.height ||
        chunk.total_size > m_camera_profile.max_frame_size) {
        stop();
        return nullptr;
    }

    bool const frame_start =
        (chunk.flags & RIN_WEBCONTENT_CAMERA_CHUNK_FRAME_START) != 0u;
    if (frame_start) {
        if (chunk.sequence <= m_last_camera_sequence) {
            stop();
            return nullptr;
        }
        m_pending_camera_sequence = chunk.sequence;
        m_pending_camera_total_size = chunk.total_size;
        m_pending_camera_offset = 0u;
        m_pending_camera_format = chunk.format;
        m_pending_camera_width = chunk.width;
        m_pending_camera_height = chunk.height;
        scrub_camera_bytes(m_pending_camera_frame);
        if (m_pending_camera_frame.try_ensure_capacity(chunk.total_size).is_error()) {
            stop();
            return nullptr;
        }
    }
    if (m_pending_camera_sequence == 0u ||
        chunk.sequence != m_pending_camera_sequence ||
        chunk.total_size != m_pending_camera_total_size ||
        chunk.offset != m_pending_camera_offset ||
        chunk.format != m_pending_camera_format ||
        chunk.width != m_pending_camera_width ||
        chunk.height != m_pending_camera_height ||
        m_pending_camera_frame.try_append(payload, payload_size).is_error()) {
        stop();
        return nullptr;
    }
    m_pending_camera_offset += static_cast<u32>(payload_size);
    if ((chunk.flags & RIN_WEBCONTENT_CAMERA_CHUNK_FRAME_END) == 0u)
        return nullptr;
    if (m_pending_camera_offset != m_pending_camera_total_size) {
        stop();
        return nullptr;
    }

    ReadonlyBytes frame_bytes {
        m_pending_camera_frame.data(), m_pending_camera_frame.size()
    };
    auto bitmap = decode_camera_frame(frame_bytes, m_pending_camera_format,
                                      m_pending_camera_width, m_pending_camera_height);
    m_last_camera_sequence = m_pending_camera_sequence;
    m_pending_camera_sequence = 0u;
    scrub_camera_bytes(m_pending_camera_frame);
    if (!bitmap)
        return nullptr;
    m_current_camera_frame = bitmap;
    return bitmap;
}

}
