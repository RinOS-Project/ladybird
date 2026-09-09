/*
 * Copyright (c) 2026, RinOS contributors
 *
 * SPDX-License-Identifier: BSD-2-Clause
 */

#include <AK/Math.h>
#include <LibWeb/WebAudio/DelayRenderData.h>
#include <errno.h>
#include <math.h>

namespace Web::WebAudio {

ErrorOr<NonnullRefPtr<DelayRenderData>> DelayRenderData::create(float max_delay_time, float sample_rate, RefPtr<AudioParamRenderData> automation)
{
    if (!isfinite(max_delay_time) || max_delay_time <= 0 || !isfinite(sample_rate) || sample_rate <= 0)
        return Error::from_errno(EINVAL);
    auto capacity = static_cast<size_t>(ceil(max_delay_time * sample_rate)) + 2;
    if (capacity < 2 || capacity > 10'000'000)
        return Error::from_errno(EINVAL);
    return adopt_nonnull_ref_or_enomem(new (nothrow) DelayRenderData(sample_rate, capacity, move(automation)));
}

void DelayRenderData::process(float& left, float& right, double time)
{
    auto delay_seconds = m_automation ? m_automation->value_at_time(time) : 0.0f;
    if (!isfinite(delay_seconds)) {
        left = 0;
        right = 0;
        return;
    }
    delay_seconds = clamp(delay_seconds, 0.0f, (m_left.size() - 2) / m_sample_rate);
    auto input_left = isfinite(left) ? left : 0.0f;
    auto input_right = isfinite(right) ? right : 0.0f;
    if (delay_seconds == 0.0f) {
        m_left[m_write_index] = input_left;
        m_right[m_write_index] = input_right;
        m_write_index = (m_write_index + 1) % m_left.size();
        left = input_left;
        right = input_right;
        return;
    }
    auto delay_frames = static_cast<double>(delay_seconds) * m_sample_rate;
    auto read_position = static_cast<double>(m_write_index) - delay_frames;
    while (read_position < 0)
        read_position += m_left.size();
    while (read_position >= m_left.size())
        read_position -= m_left.size();
    auto first = static_cast<size_t>(read_position);
    auto next = (first + 1) % m_left.size();
    auto fraction = static_cast<float>(read_position - first);
    auto delayed_left = m_left[first] + (m_left[next] - m_left[first]) * fraction;
    auto delayed_right = m_right[first] + (m_right[next] - m_right[first]) * fraction;
    m_left[m_write_index] = input_left;
    m_right[m_write_index] = input_right;
    m_write_index = (m_write_index + 1) % m_left.size();
    left = delayed_left;
    right = delayed_right;
}

}
