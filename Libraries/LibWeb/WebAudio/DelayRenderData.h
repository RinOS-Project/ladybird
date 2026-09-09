/*
 * Copyright (c) 2026, RinOS contributors
 *
 * SPDX-License-Identifier: BSD-2-Clause
 */

#pragma once

#include <AK/AtomicRefCounted.h>
#include <AK/Error.h>
#include <AK/RefPtr.h>
#include <AK/Vector.h>
#include <LibWeb/Export.h>
#include <LibWeb/WebAudio/AudioParamRenderData.h>

namespace Web::WebAudio {

// Native delay-line state owned by one render-graph edge. The ring is fully
// allocated before publication; the callback only performs bounded reads,
// writes, and interpolation.
class WEB_API DelayRenderData final : public AtomicRefCounted<DelayRenderData> {
public:
    static ErrorOr<NonnullRefPtr<DelayRenderData>> create(float max_delay_time, float sample_rate, RefPtr<AudioParamRenderData> automation);

    void process(float& left, float& right, double time);
    void update_automation(RefPtr<AudioParamRenderData> automation) { m_automation = move(automation); }

private:
    DelayRenderData(float sample_rate, size_t capacity, RefPtr<AudioParamRenderData> automation)
        : m_sample_rate(sample_rate)
        , m_left(capacity)
        , m_right(capacity)
        , m_automation(move(automation))
    {
        m_left.fill(0.0f);
        m_right.fill(0.0f);
    }

    float m_sample_rate { 0 };
    Vector<float> m_left;
    Vector<float> m_right;
    RefPtr<AudioParamRenderData> m_automation;
    size_t m_write_index { 0 };
};

}
