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
    RefPtr<AudioParamRenderData> listener_position_x, RefPtr<AudioParamRenderData> listener_position_y, RefPtr<AudioParamRenderData> listener_position_z,
    RefPtr<AudioParamRenderData> listener_forward_x, RefPtr<AudioParamRenderData> listener_forward_y, RefPtr<AudioParamRenderData> listener_forward_z,
    RefPtr<AudioParamRenderData> listener_up_x, RefPtr<AudioParamRenderData> listener_up_y, RefPtr<AudioParamRenderData> listener_up_z,
    float cone_inner_angle, float cone_outer_angle, float cone_outer_gain)
{
    if (!isfinite(ref_distance) || !isfinite(max_distance) || !isfinite(rolloff_factor) || !isfinite(cone_inner_angle) || !isfinite(cone_outer_angle) || !isfinite(cone_outer_gain)
        || ref_distance <= 0 || max_distance <= 0 || rolloff_factor < 0 || cone_inner_angle < 0 || cone_outer_angle < 0 || cone_outer_gain < 0 || cone_outer_gain > 1)
        return Error::from_errno(EINVAL);
    cone_inner_angle = clamp(cone_inner_angle, 0.0f, 360.0f);
    cone_outer_angle = clamp(cone_outer_angle, cone_inner_angle, 360.0f);
    return adopt_nonnull_ref_or_enomem(new (nothrow) PannerRenderData(distance_model, ref_distance, max_distance, rolloff_factor,
        move(position_x), move(position_y), move(position_z), move(orientation_x), move(orientation_y), move(orientation_z),
        move(listener_position_x), move(listener_position_y), move(listener_position_z),
        move(listener_forward_x), move(listener_forward_y), move(listener_forward_z),
        move(listener_up_x), move(listener_up_y), move(listener_up_z),
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
    auto listener_x = m_listener_position_x ? m_listener_position_x->value_at_time(time) : 0.0f;
    auto listener_y = m_listener_position_y ? m_listener_position_y->value_at_time(time) : 0.0f;
    auto listener_z = m_listener_position_z ? m_listener_position_z->value_at_time(time) : 0.0f;
    auto listener_forward_x = m_listener_forward_x ? m_listener_forward_x->value_at_time(time) : 0.0f;
    auto listener_forward_y = m_listener_forward_y ? m_listener_forward_y->value_at_time(time) : 0.0f;
    auto listener_forward_z = m_listener_forward_z ? m_listener_forward_z->value_at_time(time) : -1.0f;
    auto listener_up_x = m_listener_up_x ? m_listener_up_x->value_at_time(time) : 0.0f;
    auto listener_up_y = m_listener_up_y ? m_listener_up_y->value_at_time(time) : 1.0f;
    auto listener_up_z = m_listener_up_z ? m_listener_up_z->value_at_time(time) : 0.0f;
    if (!isfinite(x) || !isfinite(y) || !isfinite(z) || !isfinite(orientation_x) || !isfinite(orientation_y) || !isfinite(orientation_z)
        || !isfinite(listener_x) || !isfinite(listener_y) || !isfinite(listener_z)
        || !isfinite(listener_forward_x) || !isfinite(listener_forward_y) || !isfinite(listener_forward_z)
        || !isfinite(listener_up_x) || !isfinite(listener_up_y) || !isfinite(listener_up_z))
        return;

    auto relative_x = static_cast<double>(x) - listener_x;
    auto relative_y = static_cast<double>(y) - listener_y;
    auto relative_z = static_cast<double>(z) - listener_z;
    auto distance = sqrt(relative_x * relative_x + relative_y * relative_y + relative_z * relative_z);
    auto forward_length = sqrt(static_cast<double>(listener_forward_x) * listener_forward_x
        + static_cast<double>(listener_forward_y) * listener_forward_y
        + static_cast<double>(listener_forward_z) * listener_forward_z);
    auto up_length = sqrt(static_cast<double>(listener_up_x) * listener_up_x
        + static_cast<double>(listener_up_y) * listener_up_y
        + static_cast<double>(listener_up_z) * listener_up_z);
    if (forward_length <= 1e-9 || up_length <= 1e-9)
        return;
    auto forward_x = static_cast<double>(listener_forward_x) / forward_length;
    auto forward_y = static_cast<double>(listener_forward_y) / forward_length;
    auto forward_z = static_cast<double>(listener_forward_z) / forward_length;
    auto up_x = static_cast<double>(listener_up_x) / up_length;
    auto up_y = static_cast<double>(listener_up_y) / up_length;
    auto up_z = static_cast<double>(listener_up_z) / up_length;
    auto right_x = forward_y * up_z - forward_z * up_y;
    auto right_y = forward_z * up_x - forward_x * up_z;
    auto right_z = forward_x * up_y - forward_y * up_x;
    auto right_length = sqrt(right_x * right_x + right_y * right_y + right_z * right_z);
    if (right_length <= 1e-9)
        return;
    right_x /= right_length;
    right_y /= right_length;
    right_z /= right_length;
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
        auto direction_dot = (static_cast<double>(orientation_x) * -relative_x
            + static_cast<double>(orientation_y) * -relative_y
            + static_cast<double>(orientation_z) * -relative_z) / (orientation_length * distance);
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

    auto pan = distance > 1e-6 ? clamp((relative_x * right_x + relative_y * right_y + relative_z * right_z) / distance, -1.0, 1.0) : 0.0;
    auto angle = (pan + 1.0) * AK::Pi<double> / 4.0;
    auto mono = (left + right) * 0.5f * static_cast<float>(gain);
    left = mono * static_cast<float>(cos(angle));
    right = mono * static_cast<float>(sin(angle));
}

}
