/*
 * Copyright (c) 2026, RinOS contributors
 *
 * SPDX-License-Identifier: BSD-2-Clause
 */

#pragma once

#include <AK/AtomicRefCounted.h>
#include <AK/RefPtr.h>
#include <LibWeb/Export.h>
#include <LibWeb/WebAudio/AudioParamRenderData.h>

namespace Web::WebAudio {

enum class BiquadFilterKind : u8 {
    Lowpass,
    Highpass,
    Bandpass,
    Notch,
    Allpass,
    Peaking,
    Lowshelf,
    Highshelf,
};

struct BiquadFilterCoefficients {
    float b0 { 1.0f };
    float b1 { 0.0f };
    float b2 { 0.0f };
    float a1 { 0.0f };
    float a2 { 0.0f };
};

class WEB_API BiquadFilterRenderData final : public AtomicRefCounted<BiquadFilterRenderData> {
public:
    static ErrorOr<NonnullRefPtr<BiquadFilterRenderData>> create(
        BiquadFilterKind,
        float frequency,
        float detune,
        float q,
        float gain,
        RefPtr<AudioParamRenderData> frequency_automation,
        RefPtr<AudioParamRenderData> detune_automation,
        RefPtr<AudioParamRenderData> q_automation,
        RefPtr<AudioParamRenderData> gain_automation);

    BiquadFilterCoefficients coefficients_at_time(double, float sample_rate) const;

private:
    BiquadFilterRenderData(BiquadFilterKind type, float frequency, float detune, float q, float gain,
        RefPtr<AudioParamRenderData> frequency_automation,
        RefPtr<AudioParamRenderData> detune_automation,
        RefPtr<AudioParamRenderData> q_automation,
        RefPtr<AudioParamRenderData> gain_automation)
        : m_type(type)
        , m_frequency(frequency)
        , m_detune(detune)
        , m_q(q)
        , m_gain(gain)
        , m_frequency_automation(move(frequency_automation))
        , m_detune_automation(move(detune_automation))
        , m_q_automation(move(q_automation))
        , m_gain_automation(move(gain_automation))
    {
    }

    BiquadFilterKind m_type;
    float m_frequency;
    float m_detune;
    float m_q;
    float m_gain;
    RefPtr<AudioParamRenderData> m_frequency_automation;
    RefPtr<AudioParamRenderData> m_detune_automation;
    RefPtr<AudioParamRenderData> m_q_automation;
    RefPtr<AudioParamRenderData> m_gain_automation;
};

}
