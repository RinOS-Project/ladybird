/*
 * Copyright (c) 2026, RinOS contributors
 *
 * SPDX-License-Identifier: BSD-2-Clause
 */

#include <AK/Math.h>
#include <LibWeb/WebAudio/PannerRenderData.h>
#include <math.h>

namespace Web::WebAudio {

ErrorOr<NonnullRefPtr<PannerRenderData>> PannerRenderData::create(PannerDistanceModel distance_model, float ref_distance, float max_distance, float rolloff_factor,
    RefPtr<AudioParamRenderData> position_x, RefPtr<AudioParamRenderData> position_y, RefPtr<AudioParamRenderData> position_z)
{
    if (!isfinite(ref_distance) || !isfinite(max_distance) || !isfinite(rolloff_factor) || ref_distance <= 0 || max_distance <= 0 || rolloff_factor < 0 || max_distance < ref_distance)
        return Error::from_errno(EINVAL);
    return adopt_nonnull_ref_or_enomem(new (nothrow) PannerRenderData(distance_model, ref_distance, max_distance, rolloff_factor, move(position_x), move(position_y), move(position_z)));
}

void PannerRenderData::process(float& left, float& right, double time) const
{
    auto x = m_position_x ? m_position_x->value_at_time(time) : 0.0f;
    auto y = m_position_y ? m_position_y->value_at_time(time) : 0.0f;
    auto z = m_position_z ? m_position_z->value_at_time(time) : 0.0f;
    if (!isfinite(x) || !isfinite(y) || !isfinite(z))
        return;

    auto distance = sqrt(static_cast<double>(x) * x + static_cast<double>(y) * y + static_cast<double>(z) * z);
    auto clamped_distance = clamp(distance, static_cast<double>(m_ref_distance), static_cast<double>(m_max_distance));
    double gain = 1.0;
    switch (m_distance_model) {
    case PannerDistanceModel::Linear: {
        auto span = max(static_cast<double>(m_max_distance - m_ref_distance), 1e-6);
        gain = 1.0 - static_cast<double>(m_rolloff_factor) * (clamped_distance - m_ref_distance) / span;
        break;
    }
    case PannerDistanceModel::Inverse:
        gain = m_ref_distance / (m_ref_distance + m_rolloff_factor * (clamped_distance - m_ref_distance));
        break;
    case PannerDistanceModel::Exponential:
        gain = pow(max(clamped_distance / m_ref_distance, 1.0), -static_cast<double>(m_rolloff_factor));
        break;
    }
    gain = clamp(gain, 0.0, 1.0);

    auto pan = distance > 1e-6 ? clamp(static_cast<double>(x) / distance, -1.0, 1.0) : 0.0;
    auto angle = (pan + 1.0) * AK::Pi<double> / 4.0;
    auto mono = (left + right) * 0.5f * static_cast<float>(gain);
    left = mono * static_cast<float>(cos(angle));
    right = mono * static_cast<float>(sin(angle));
}

}
