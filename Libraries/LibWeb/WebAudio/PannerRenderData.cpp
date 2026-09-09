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
    RefPtr<AudioParamRenderData> position_x, RefPtr<AudioParamRenderData> position_y, RefPtr<AudioParamRenderData> position_z,
    RefPtr<AudioParamRenderData> orientation_x, RefPtr<AudioParamRenderData> orientation_y, RefPtr<AudioParamRenderData> orientation_z,
    float cone_inner_angle, float cone_outer_angle, float cone_outer_gain)
{
    if (!isfinite(ref_distance) || !isfinite(max_distance) || !isfinite(rolloff_factor) || !isfinite(cone_inner_angle) || !isfinite(cone_outer_angle) || !isfinite(cone_outer_gain)
        || ref_distance <= 0 || max_distance <= 0 || rolloff_factor < 0 || cone_inner_angle < 0 || cone_outer_angle < 0 || cone_outer_gain < 0 || cone_outer_gain > 1)
        return Error::from_errno(EINVAL);
    cone_inner_angle = clamp(cone_inner_angle, 0.0f, 360.0f);
    cone_outer_angle = clamp(cone_outer_angle, cone_inner_angle, 360.0f);
    return adopt_nonnull_ref_or_enomem(new (nothrow) PannerRenderData(distance_model, ref_distance, max_distance, rolloff_factor,
        move(position_x), move(position_y), move(position_z), move(orientation_x), move(orientation_y), move(orientation_z),
        cone_inner_angle, cone_outer_angle, cone_outer_gain));
}

void PannerRenderData::process(float& left, float& right, double time) const
{
    auto x = m_position_x ? m_position_x->value_at_time(time) : 0.0f;
    auto y = m_position_y ? m_position_y->value_at_time(time) : 0.0f;
    auto z = m_position_z ? m_position_z->value_at_time(time) : 0.0f;
    auto orientation_x = m_orientation_x ? m_orientation_x->value_at_time(time) : 1.0f;
    auto orientation_y = m_orientation_y ? m_orientation_y->value_at_time(time) : 0.0f;
    auto orientation_z = m_orientation_z ? m_orientation_z->value_at_time(time) : 0.0f;
    if (!isfinite(x) || !isfinite(y) || !isfinite(z) || !isfinite(orientation_x) || !isfinite(orientation_y) || !isfinite(orientation_z))
        return;

    auto distance = sqrt(static_cast<double>(x) * x + static_cast<double>(y) * y + static_cast<double>(z) * z);
    auto clamped_distance = max(static_cast<double>(m_ref_distance), min(distance, static_cast<double>(m_max_distance)));
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

    // The cone is evaluated against the direction from the panner toward the
    // listener (the negated source position). A zero orientation vector has
    // no meaningful facing direction, so it leaves the distance gain intact.
    auto orientation_length = sqrt(static_cast<double>(orientation_x) * orientation_x
        + static_cast<double>(orientation_y) * orientation_y
        + static_cast<double>(orientation_z) * orientation_z);
    if (orientation_length > 1e-9 && distance > 1e-9 && m_cone_outer_angle < 360.0f) {
        auto direction_dot = (static_cast<double>(orientation_x) * -x
            + static_cast<double>(orientation_y) * -y
            + static_cast<double>(orientation_z) * -z) / (orientation_length * distance);
        auto direction_angle = acos(clamp(direction_dot, -1.0, 1.0)) * 180.0 / AK::Pi<double>;
        auto inner = static_cast<double>(m_cone_inner_angle) * 0.5;
        auto outer = static_cast<double>(m_cone_outer_angle) * 0.5;
        if (direction_angle >= outer)
            gain *= m_cone_outer_gain;
        else if (direction_angle > inner) {
            auto interpolation = (direction_angle - inner) / max(outer - inner, 1e-9);
            gain *= 1.0 + (static_cast<double>(m_cone_outer_gain) - 1.0) * interpolation;
        }
    }

    auto pan = distance > 1e-6 ? clamp(static_cast<double>(x) / distance, -1.0, 1.0) : 0.0;
    auto angle = (pan + 1.0) * AK::Pi<double> / 4.0;
    auto mono = (left + right) * 0.5f * static_cast<float>(gain);
    left = mono * static_cast<float>(cos(angle));
    right = mono * static_cast<float>(sin(angle));
}

}
