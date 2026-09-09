/*
 * Copyright (c) 2026, RinOS contributors
 *
 * SPDX-License-Identifier: BSD-2-Clause
 */

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
    if (!isfinite(input))
        return 0.0f;

    auto threshold = m_threshold_automation ? m_threshold_automation->value_at_time(time) : m_threshold;
    auto knee = m_knee_automation ? m_knee_automation->value_at_time(time) : m_knee;
    auto ratio = m_ratio_automation ? m_ratio_automation->value_at_time(time) : m_ratio;
    // The fixed-rate attack/release values are evaluated to keep a single
    // coherent snapshot boundary. The stateless bounded curve below does not
    // retain an envelope, so they intentionally do not alter the result yet.
    auto attack = m_attack_automation ? m_attack_automation->value_at_time(time) : m_attack;
    auto release = m_release_automation ? m_release_automation->value_at_time(time) : m_release;
    if (!isfinite(threshold) || !isfinite(knee) || !isfinite(ratio) || !isfinite(attack) || !isfinite(release))
        return input;

    threshold = clamp(threshold, -100.0f, 0.0f);
    knee = clamp(knee, 0.0f, 40.0f);
    ratio = clamp(ratio, 1.0f, 20.0f);
    auto magnitude = abs(input);
    if (magnitude <= 1e-12f || ratio == 1.0f)
        return input;

    auto level_db = 20.0f * log10(magnitude);
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
        return input;
    return input * clamp(gain, 0.0f, 1.0f);
}

}
