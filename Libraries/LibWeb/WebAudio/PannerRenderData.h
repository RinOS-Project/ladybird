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
        RefPtr<AudioParamRenderData> position_z);

    void process(float& left, float& right, double time) const;
    void update_position_x(RefPtr<AudioParamRenderData> value) { m_position_x = move(value); }
    void update_position_y(RefPtr<AudioParamRenderData> value) { m_position_y = move(value); }
    void update_position_z(RefPtr<AudioParamRenderData> value) { m_position_z = move(value); }

private:
    PannerRenderData(PannerDistanceModel distance_model, float ref_distance, float max_distance, float rolloff_factor,
        RefPtr<AudioParamRenderData> position_x, RefPtr<AudioParamRenderData> position_y, RefPtr<AudioParamRenderData> position_z)
        : m_distance_model(distance_model)
        , m_ref_distance(ref_distance)
        , m_max_distance(max_distance)
        , m_rolloff_factor(rolloff_factor)
        , m_position_x(move(position_x))
        , m_position_y(move(position_y))
        , m_position_z(move(position_z))
    {
    }

    PannerDistanceModel m_distance_model { PannerDistanceModel::Inverse };
    float m_ref_distance { 1.0f };
    float m_max_distance { 10000.0f };
    float m_rolloff_factor { 1.0f };
    RefPtr<AudioParamRenderData> m_position_x;
    RefPtr<AudioParamRenderData> m_position_y;
    RefPtr<AudioParamRenderData> m_position_z;
};

}
