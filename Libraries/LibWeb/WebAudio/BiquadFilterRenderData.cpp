/*
 * Copyright (c) 2026, RinOS contributors
 *
 * SPDX-License-Identifier: BSD-2-Clause
 */

#include <AK/Math.h>
#include <AK/StdLibExtras.h>
#include <LibWeb/WebAudio/BiquadFilterRenderData.h>
#include <math.h>

namespace Web::WebAudio {

ErrorOr<NonnullRefPtr<BiquadFilterRenderData>> BiquadFilterRenderData::create(
    BiquadFilterKind type,
    float frequency,
    float detune,
    float q,
    float gain,
    RefPtr<AudioParamRenderData> frequency_automation,
    RefPtr<AudioParamRenderData> detune_automation,
    RefPtr<AudioParamRenderData> q_automation,
    RefPtr<AudioParamRenderData> gain_automation)
{
    return adopt_nonnull_ref_or_enomem(new (nothrow) BiquadFilterRenderData(type, frequency, detune, q, gain,
        move(frequency_automation), move(detune_automation), move(q_automation), move(gain_automation)));
}

BiquadFilterCoefficients BiquadFilterRenderData::coefficients_at_time(double time, float sample_rate, float frequency_modulation, float detune_modulation, float q_modulation, float gain_modulation) const
{
    BiquadFilterCoefficients coefficients;
    auto frequency = (m_frequency_automation ? m_frequency_automation->value_at_time(time) : m_frequency) + frequency_modulation;
    auto detune = (m_detune_automation ? m_detune_automation->value_at_time(time) : m_detune) + detune_modulation;
    auto q = (m_q_automation ? m_q_automation->value_at_time(time) : m_q) + q_modulation;
    auto gain = (m_gain_automation ? m_gain_automation->value_at_time(time) : m_gain) + gain_modulation;
    if (!isfinite(frequency) || !isfinite(detune) || !isfinite(q) || !isfinite(gain) || !isfinite(sample_rate) || sample_rate <= 0)
        return coefficients;

    auto effective_frequency = clamp(frequency * pow(2.0f, detune / 1200.0f), 0.0f, sample_rate / 2.0f);
    auto q_value = max(abs(q), 1e-8f);
    auto gain_factor = pow(10.0f, gain / 40.0f);
    auto omega = 2.0f * AK::Pi<float> * effective_frequency / sample_rate;
    auto cosine = cos(omega);
    auto sine = sin(omega);
    auto alpha = sine / (2.0f * q_value);
    auto shelf_alpha = sine * 0.5f * AK::sqrt(2.0f);
    auto two_sqrt_gain_alpha = 2.0f * AK::sqrt(gain_factor) * shelf_alpha;

    float b0 = 0;
    float b1 = 0;
    float b2 = 0;
    float a0 = 1;
    float a1 = 0;
    float a2 = 0;
    switch (m_type) {
    case BiquadFilterKind::Lowpass:
        b0 = (1 - cosine) / 2;
        b1 = 1 - cosine;
        b2 = b0;
        a0 = 1 + alpha;
        a1 = -2 * cosine;
        a2 = 1 - alpha;
        break;
    case BiquadFilterKind::Highpass:
        b0 = (1 + cosine) / 2;
        b1 = -(1 + cosine);
        b2 = b0;
        a0 = 1 + alpha;
        a1 = -2 * cosine;
        a2 = 1 - alpha;
        break;
    case BiquadFilterKind::Bandpass:
        b0 = sine / 2;
        b2 = -sine / 2;
        a0 = 1 + alpha;
        a1 = -2 * cosine;
        a2 = 1 - alpha;
        break;
    case BiquadFilterKind::Notch:
        b0 = 1;
        b1 = -2 * cosine;
        b2 = 1;
        a0 = 1 + alpha;
        a1 = -2 * cosine;
        a2 = 1 - alpha;
        break;
    case BiquadFilterKind::Allpass:
        b0 = 1 - alpha;
        b1 = -2 * cosine;
        b2 = 1 + alpha;
        a0 = 1 + alpha;
        a1 = -2 * cosine;
        a2 = 1 - alpha;
        break;
    case BiquadFilterKind::Peaking:
        b0 = 1 + alpha * gain_factor;
        b1 = -2 * cosine;
        b2 = 1 - alpha * gain_factor;
        a0 = 1 + alpha / gain_factor;
        a1 = -2 * cosine;
        a2 = 1 - alpha / gain_factor;
        break;
    case BiquadFilterKind::Lowshelf:
        b0 = gain_factor * ((gain_factor + 1) - (gain_factor - 1) * cosine + two_sqrt_gain_alpha);
        b1 = 2 * gain_factor * ((gain_factor - 1) - (gain_factor + 1) * cosine);
        b2 = gain_factor * ((gain_factor + 1) - (gain_factor - 1) * cosine - two_sqrt_gain_alpha);
        a0 = (gain_factor + 1) + (gain_factor - 1) * cosine + two_sqrt_gain_alpha;
        a1 = -2 * ((gain_factor - 1) + (gain_factor + 1) * cosine);
        a2 = (gain_factor + 1) + (gain_factor - 1) * cosine - two_sqrt_gain_alpha;
        break;
    case BiquadFilterKind::Highshelf:
        b0 = gain_factor * ((gain_factor + 1) + (gain_factor - 1) * cosine + two_sqrt_gain_alpha);
        b1 = -2 * gain_factor * ((gain_factor - 1) + (gain_factor + 1) * cosine);
        b2 = gain_factor * ((gain_factor + 1) + (gain_factor - 1) * cosine - two_sqrt_gain_alpha);
        a0 = (gain_factor + 1) - (gain_factor - 1) * cosine + two_sqrt_gain_alpha;
        a1 = 2 * ((gain_factor - 1) - (gain_factor + 1) * cosine);
        a2 = (gain_factor + 1) - (gain_factor - 1) * cosine - two_sqrt_gain_alpha;
        break;
    }

    if (!isfinite(a0) || a0 == 0)
        return coefficients;
    coefficients.b0 = b0 / a0;
    coefficients.b1 = b1 / a0;
    coefficients.b2 = b2 / a0;
    coefficients.a1 = a1 / a0;
    coefficients.a2 = a2 / a0;
    if (!isfinite(coefficients.b0) || !isfinite(coefficients.b1) || !isfinite(coefficients.b2) || !isfinite(coefficients.a1) || !isfinite(coefficients.a2))
        return { };
    return coefficients;
}

}
