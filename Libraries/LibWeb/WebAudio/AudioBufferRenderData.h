/*
 * Copyright (c) 2026, RinOS contributors
 *
 * SPDX-License-Identifier: BSD-2-Clause
 */

#pragma once

#include <AK/AtomicRefCounted.h>
#include <AK/RefPtr.h>
#include <AK/Vector.h>
#include <LibWeb/Export.h>

namespace Web::WebAudio {

class AudioBuffer;

// Immutable, non-GC audio data owned by the rendering thread. AudioBuffer is a
// JavaScript object and must never be read from a real-time callback, so a
// source snapshots its PCM channels before publishing a control message.
class WEB_API AudioBufferRenderData final : public AtomicRefCounted<AudioBufferRenderData> {
public:
    static ErrorOr<NonnullRefPtr<AudioBufferRenderData>> create(AudioBuffer const&);

    u32 channel_count() const { return m_channels.size(); }
    u32 frame_count() const { return m_frame_count; }
    float sample_rate() const { return m_sample_rate; }
    float sample(u32 channel, u32 frame) const
    {
        if (channel >= m_channels.size() || frame >= m_frame_count)
            return 0;
        return m_channels[channel][frame];
    }

private:
    AudioBufferRenderData(float sample_rate, u32 frame_count)
        : m_sample_rate(sample_rate)
        , m_frame_count(frame_count)
    {
    }

    float m_sample_rate { 0 };
    u32 m_frame_count { 0 };
    Vector<Vector<float>> m_channels;
};

}
