/*
 * Copyright (c) 2026, RinOS contributors
 *
 * SPDX-License-Identifier: BSD-2-Clause
 */

#pragma once

#include <AK/Atomic.h>
#include <AK/AtomicRefCounted.h>
#include <AK/RefPtr.h>
#include <LibWeb/Export.h>
#include <LibWeb/WebAudio/AudioParamRenderData.h>

namespace Web::WebAudio {

class WEB_API DynamicsCompressorRenderData final : public AtomicRefCounted<DynamicsCompressorRenderData> {
public:
    static ErrorOr<NonnullRefPtr<DynamicsCompressorRenderData>> create(
        float threshold,
        float knee,
        float ratio,
        float attack,
        float release,
        RefPtr<AudioParamRenderData> threshold_automation,
        RefPtr<AudioParamRenderData> knee_automation,
        RefPtr<AudioParamRenderData> ratio_automation,
        RefPtr<AudioParamRenderData> attack_automation,
        RefPtr<AudioParamRenderData> release_automation);

    float process(float input, double time, float threshold_modulation = 0, float knee_modulation = 0, float ratio_modulation = 0, float attack_modulation = 0, float release_modulation = 0) const;
    void process_stereo(float& left, float& right, double time, float sample_rate, float threshold_modulation = 0, float knee_modulation = 0, float ratio_modulation = 0, float attack_modulation = 0, float release_modulation = 0) const;
    float reduction() const;

    void update_threshold_automation(RefPtr<AudioParamRenderData> data) { m_threshold_automation = move(data); }
    void update_knee_automation(RefPtr<AudioParamRenderData> data) { m_knee_automation = move(data); }
    void update_ratio_automation(RefPtr<AudioParamRenderData> data) { m_ratio_automation = move(data); }
    void update_attack_automation(RefPtr<AudioParamRenderData> data) { m_attack_automation = move(data); }
    void update_release_automation(RefPtr<AudioParamRenderData> data) { m_release_automation = move(data); }

private:
    DynamicsCompressorRenderData(
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
        : m_threshold(threshold)
        , m_knee(knee)
        , m_ratio(ratio)
        , m_attack(attack)
        , m_release(release)
        , m_threshold_automation(move(threshold_automation))
        , m_knee_automation(move(knee_automation))
        , m_ratio_automation(move(ratio_automation))
        , m_attack_automation(move(attack_automation))
        , m_release_automation(move(release_automation))
    {
    }

    float m_threshold;
    float m_knee;
    float m_ratio;
    float m_attack;
    float m_release;
    RefPtr<AudioParamRenderData> m_threshold_automation;
    RefPtr<AudioParamRenderData> m_knee_automation;
    RefPtr<AudioParamRenderData> m_ratio_automation;
    RefPtr<AudioParamRenderData> m_attack_automation;
    RefPtr<AudioParamRenderData> m_release_automation;
    mutable float m_envelope { 0.0f };
    mutable double m_last_time { -1.0 };
    Atomic<u32> m_reduction_bits { 0 };
};

}
