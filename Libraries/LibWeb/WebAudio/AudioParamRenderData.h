/*
 * Copyright (c) 2026, RinOS contributors
 *
 * SPDX-License-Identifier: BSD-2-Clause
 */

#pragma once

#include <AK/AtomicRefCounted.h>
#include <AK/Error.h>
#include <AK/Optional.h>
#include <AK/RefPtr.h>
#include <AK/Vector.h>
#include <LibWeb/Export.h>

namespace Web::WebAudio {

// Immutable, non-GC snapshot of an AudioParam automation timeline. Rendering
// callbacks use this object instead of entering the JavaScript heap.
class WEB_API AudioParamRenderData final : public AtomicRefCounted<AudioParamRenderData> {
public:
    enum class EventType : u8 {
        SetValue,
        LinearRamp,
        ExponentialRamp,
        SetTarget,
        SetValueCurve,
    };

    struct Event {
        EventType type { EventType::SetValue };
        double time { 0 };
        float value { 0 };
        float start_value { 0 };
        float time_constant { 0 };
        double duration { 0 };
        Vector<float> curve;
    };

    static ErrorOr<NonnullRefPtr<AudioParamRenderData>> create(float current_value, Vector<Event>&& events);

    float value_at_time(double) const;

private:
    AudioParamRenderData(float current_value, Vector<Event>&& events)
        : m_current_value(current_value)
        , m_events(move(events))
    {
    }

    float evaluate_event(Event const&, double) const;

    float m_current_value { 0 };
    Vector<Event> m_events;
};

}
