/*
 * Copyright (c) 2026, Ben Eidson <b.e.eidson@gmail.com>
 *
 * SPDX-License-Identifier: BSD-2-Clause
 */

#include <AK/Math.h>
#include <LibWeb/WebAudio/AnalyserRenderData.h>
#include <errno.h>
#include <math.h>

namespace Web::WebAudio {

ErrorOr<NonnullRefPtr<AnalyserRenderData>> AnalyserRenderData::create(size_t frame_count)
{
    if (frame_count < 32 || frame_count > 32768)
        return Error::from_errno(EINVAL);
    auto result = adopt_nonnull_ref_or_enomem(new (nothrow) AnalyserRenderData(frame_count));
    result.value()->m_history.fill(0.0f);
    return result;
}

void AnalyserRenderData::push_frame(float left, float right)
{
    Threading::MutexLocker locker(m_mutex);
    auto sample = (left + right) * 0.5f;
    if (!isfinite(sample))
        sample = 0.0f;
    m_history[m_next_frame] = clamp(sample, -1.0f, 1.0f);
    m_next_frame = (m_next_frame + 1) % m_history.size();
    m_written_frames = min(m_written_frames + 1, m_history.size());
}

Vector<float> AnalyserRenderData::snapshot() const
{
    Threading::MutexLocker locker(m_mutex);
    Vector<float> result;
    result.resize(m_history.size());
    result.fill(0.0f);
    auto first = (m_written_frames == m_history.size()) ? m_next_frame : 0;
    auto available = m_written_frames;
    auto output_offset = m_history.size() - available;
    for (size_t index = 0; index < available; ++index)
        result[output_offset + index] = m_history[(first + index) % m_history.size()];
    return result;
}

}
