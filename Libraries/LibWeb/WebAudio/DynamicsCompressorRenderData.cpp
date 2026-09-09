/*
 * Copyright (c) 2026, RinOS contributors
 *
 * SPDX-License-Identifier: BSD-2-Clause
 */

#include <AK/BitCast.h>
#include <AK/Math.h>
#include <LibWeb/WebAudio/DynamicsCompressorRenderData.h>
#include <math.h>

namespace Web::WebAudio {

ErrorOr<NonnullRefPtr<DynamicsCompressorRenderData>> DynamicsCompressorRenderData::create(
    float threshold,
    float knee,
    float ratio,
    float attack,
    float release,
    RefPtr<AudioParamRenderData> threshold_automation,
    RefPtr<AudioParamRenderData> knee_automation,
    RefPtr<AudioParamRenderData> ratio_automation,
    RefPtr<AudioParamRenderData> attack_automation,
    RefPtr<AudioParamRenderData> release_automation)
{
    return adopt_nonnull_ref_or_enomem(new (nothrow) DynamicsCompressorRenderData(
        threshold, knee, ratio, attack, release,
        move(threshold_automation), move(knee_automation), move(ratio_automation),
            move(attack_automation), move(release_automation)));
}

float DynamicsCompressorRenderData::process(float input, double time) const
{
    auto left = input;
    auto right = input;
    process_stereo(left, right, time, 48'000.0f);
    return left;
}

float DynamicsCompressorRenderData::reduction() const
{
    return bit_cast<float>(m_reduction_bits.load());
}

void DynamicsCompressorRenderData::process_stereo(float& left, float& right, double time, float sample_rate) const
{
    if (!isfinite(left))
        left = 0.0f;
    if (!isfinite(right))
        right = 0.0f;

    auto threshold = m_threshold_automation ? m_threshold_automation->value_at_time(time) : m_threshold;
    auto knee = m_knee_automation ? m_knee_automation->value_at_time(time) : m_knee;
    auto ratio = m_ratio_automation ? m_ratio_automation->value_at_time(time) : m_ratio;
    auto attack = m_attack_automation ? m_attack_automation->value_at_time(time) : m_attack;
    auto release = m_release_automation ? m_release_automation->value_at_time(time) : m_release;
    if (!isfinite(threshold) || !isfinite(knee) || !isfinite(ratio) || !isfinite(attack) || !isfinite(release))
        return;

    threshold = clamp(threshold, -100.0f, 0.0f);
    knee = clamp(knee, 0.0f, 40.0f);
    ratio = clamp(ratio, 1.0f, 20.0f);
    attack = clamp(attack, 0.0f, 1.0f);
    release = clamp(release, 0.0f, 1.0f);
    sample_rate = isfinite(sample_rate) && sample_rate > 0.0f ? sample_rate : 48'000.0f;
    auto magnitude = max(abs(left), abs(right));
    auto delta_time = (m_last_time >= 0.0 && isfinite(time) && time >= m_last_time) ? time - m_last_time : 1.0 / sample_rate;
    delta_time = clamp(delta_time, 1.0 / static_cast<double>(sample_rate), 0.25);
    auto time_constant = magnitude > m_envelope ? max(static_cast<double>(attack), 1.0 / sample_rate) : max(static_cast<double>(release), 1.0 / sample_rate);
    auto smoothing = 1.0 - exp(-delta_time / time_constant);
    m_envelope += (magnitude - m_envelope) * static_cast<float>(smoothing);
    m_last_time = time;
    if (m_envelope <= 1e-12f || ratio == 1.0f) {
        m_reduction_bits.store(bit_cast<u32>(0.0f));
        return;
    }

    auto level_db = 20.0f * log10(m_envelope);
    auto compressed_db = level_db;
    // WebAudio's bounded soft-knee transition is continuous at both edges.
    if (knee > 0.0f && level_db > threshold - knee * 0.5f && level_db < threshold + knee * 0.5f) {
        auto distance = level_db - threshold + knee * 0.5f;
        compressed_db = level_db + (1.0f / ratio - 1.0f) * distance * distance / (2.0f * knee);
    } else if (level_db >= threshold + knee * 0.5f) {
        compressed_db = threshold + (level_db - threshold) / ratio;
    }
    auto gain = pow(10.0f, (compressed_db - level_db) / 20.0f);
    if (!isfinite(gain))
        return;
    gain = clamp(gain, 0.0f, 1.0f);
    m_reduction_bits.store(bit_cast<u32>(20.0f * log10(max(gain, 1e-12f))));
    left *= gain;
    right *= gain;
}

}
