/*
 * Copyright (c) 2026, RinOS contributors
 *
 * SPDX-License-Identifier: BSD-2-Clause
 */

#pragma once

#include <AK/AtomicRefCounted.h>
#include <AK/Error.h>
#include <AK/RefPtr.h>
#include <LibWeb/Export.h>
#include <LibWeb/WebAudio/AudioParamRenderData.h>
#include <LibWeb/WebAudio/Types.h>

namespace Web::WebAudio {

class WEB_API PannerRenderData final : public AtomicRefCounted<PannerRenderData> {
public:
    static ErrorOr<NonnullRefPtr<PannerRenderData>> create(
        PannerDistanceModel distance_model,
        float ref_distance,
        float max_distance,
        float rolloff_factor,
        RefPtr<AudioParamRenderData> position_x,
        RefPtr<AudioParamRenderData> position_y,
        RefPtr<AudioParamRenderData> position_z,
        RefPtr<AudioParamRenderData> orientation_x,
        RefPtr<AudioParamRenderData> orientation_y,
        RefPtr<AudioParamRenderData> orientation_z,
        RefPtr<AudioParamRenderData> listener_position_x,
        RefPtr<AudioParamRenderData> listener_position_y,
        RefPtr<AudioParamRenderData> listener_position_z,
        RefPtr<AudioParamRenderData> listener_forward_x,
        RefPtr<AudioParamRenderData> listener_forward_y,
        RefPtr<AudioParamRenderData> listener_forward_z,
        RefPtr<AudioParamRenderData> listener_up_x,
        RefPtr<AudioParamRenderData> listener_up_y,
        RefPtr<AudioParamRenderData> listener_up_z,
        float cone_inner_angle,
        float cone_outer_angle,
        float cone_outer_gain);

    void process(float& left, float& right, double time,
        float position_x_modulation = 0, float position_y_modulation = 0, float position_z_modulation = 0,
        float orientation_x_modulation = 0, float orientation_y_modulation = 0, float orientation_z_modulation = 0,
        float listener_position_x_modulation = 0, float listener_position_y_modulation = 0, float listener_position_z_modulation = 0,
        float listener_forward_x_modulation = 0, float listener_forward_y_modulation = 0, float listener_forward_z_modulation = 0,
        float listener_up_x_modulation = 0, float listener_up_y_modulation = 0, float listener_up_z_modulation = 0) const;
    void update_position_x(RefPtr<AudioParamRenderData> value) { m_position_x = move(value); }
    void update_position_y(RefPtr<AudioParamRenderData> value) { m_position_y = move(value); }
    void update_position_z(RefPtr<AudioParamRenderData> value) { m_position_z = move(value); }
    void update_orientation_x(RefPtr<AudioParamRenderData> value) { m_orientation_x = move(value); }
    void update_orientation_y(RefPtr<AudioParamRenderData> value) { m_orientation_y = move(value); }
    void update_orientation_z(RefPtr<AudioParamRenderData> value) { m_orientation_z = move(value); }
    void update_listener_position_x(RefPtr<AudioParamRenderData> value) { m_listener_position_x = move(value); }
    void update_listener_position_y(RefPtr<AudioParamRenderData> value) { m_listener_position_y = move(value); }
    void update_listener_position_z(RefPtr<AudioParamRenderData> value) { m_listener_position_z = move(value); }
    void update_listener_forward_x(RefPtr<AudioParamRenderData> value) { m_listener_forward_x = move(value); }
    void update_listener_forward_y(RefPtr<AudioParamRenderData> value) { m_listener_forward_y = move(value); }
    void update_listener_forward_z(RefPtr<AudioParamRenderData> value) { m_listener_forward_z = move(value); }
    void update_listener_up_x(RefPtr<AudioParamRenderData> value) { m_listener_up_x = move(value); }
    void update_listener_up_y(RefPtr<AudioParamRenderData> value) { m_listener_up_y = move(value); }
    void update_listener_up_z(RefPtr<AudioParamRenderData> value) { m_listener_up_z = move(value); }

private:
    PannerRenderData(PannerDistanceModel distance_model, float ref_distance, float max_distance, float rolloff_factor,
        RefPtr<AudioParamRenderData> position_x, RefPtr<AudioParamRenderData> position_y, RefPtr<AudioParamRenderData> position_z,
        RefPtr<AudioParamRenderData> orientation_x, RefPtr<AudioParamRenderData> orientation_y, RefPtr<AudioParamRenderData> orientation_z,
        RefPtr<AudioParamRenderData> listener_position_x, RefPtr<AudioParamRenderData> listener_position_y, RefPtr<AudioParamRenderData> listener_position_z,
        RefPtr<AudioParamRenderData> listener_forward_x, RefPtr<AudioParamRenderData> listener_forward_y, RefPtr<AudioParamRenderData> listener_forward_z,
        RefPtr<AudioParamRenderData> listener_up_x, RefPtr<AudioParamRenderData> listener_up_y, RefPtr<AudioParamRenderData> listener_up_z,
        float cone_inner_angle, float cone_outer_angle, float cone_outer_gain)
        : m_distance_model(distance_model)
        , m_ref_distance(ref_distance)
        , m_max_distance(max_distance)
        , m_rolloff_factor(rolloff_factor)
        , m_position_x(move(position_x))
        , m_position_y(move(position_y))
        , m_position_z(move(position_z))
        , m_orientation_x(move(orientation_x))
        , m_orientation_y(move(orientation_y))
        , m_orientation_z(move(orientation_z))
        , m_listener_position_x(move(listener_position_x))
        , m_listener_position_y(move(listener_position_y))
        , m_listener_position_z(move(listener_position_z))
        , m_listener_forward_x(move(listener_forward_x))
        , m_listener_forward_y(move(listener_forward_y))
        , m_listener_forward_z(move(listener_forward_z))
        , m_listener_up_x(move(listener_up_x))
        , m_listener_up_y(move(listener_up_y))
        , m_listener_up_z(move(listener_up_z))
        , m_cone_inner_angle(cone_inner_angle)
        , m_cone_outer_angle(cone_outer_angle)
        , m_cone_outer_gain(cone_outer_gain)
    {
    }

    PannerDistanceModel m_distance_model { PannerDistanceModel::Inverse };
    float m_ref_distance { 1.0f };
    float m_max_distance { 10000.0f };
    float m_rolloff_factor { 1.0f };
    RefPtr<AudioParamRenderData> m_position_x;
    RefPtr<AudioParamRenderData> m_position_y;
    RefPtr<AudioParamRenderData> m_position_z;
    RefPtr<AudioParamRenderData> m_orientation_x;
    RefPtr<AudioParamRenderData> m_orientation_y;
    RefPtr<AudioParamRenderData> m_orientation_z;
    RefPtr<AudioParamRenderData> m_listener_position_x;
    RefPtr<AudioParamRenderData> m_listener_position_y;
    RefPtr<AudioParamRenderData> m_listener_position_z;
    RefPtr<AudioParamRenderData> m_listener_forward_x;
    RefPtr<AudioParamRenderData> m_listener_forward_y;
    RefPtr<AudioParamRenderData> m_listener_forward_z;
    RefPtr<AudioParamRenderData> m_listener_up_x;
    RefPtr<AudioParamRenderData> m_listener_up_y;
    RefPtr<AudioParamRenderData> m_listener_up_z;
    float m_cone_inner_angle { 360.0f };
    float m_cone_outer_angle { 360.0f };
    float m_cone_outer_gain { 0.0f };
};

}
