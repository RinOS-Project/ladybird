/*
 * Copyright (c) 2026, RinOS contributors
 *
 * SPDX-License-Identifier: BSD-2-Clause
 */

#include <AK/StdLibExtras.h>
#include <LibWeb/WebAudio/AudioParamRenderData.h>
#include <math.h>

namespace Web::WebAudio {

ErrorOr<NonnullRefPtr<AudioParamRenderData>> AudioParamRenderData::create(float current_value, Vector<Event>&& events)
{
    return adopt_nonnull_ref_or_enomem(new (nothrow) AudioParamRenderData(current_value, move(events)));
}

float AudioParamRenderData::value_at_time(double time) const
{
    if (!isfinite(time) || time < 0)
        return m_current_value;

    float current_value = m_current_value;
    double current_time = 0;
    Optional<size_t> active_event;

    for (size_t i = 0; i < m_events.size(); ++i) {
        auto const& event = m_events[i];

        if (time < event.time) {
            if (active_event.has_value()) {
                auto const& active = m_events[active_event.value()];
                auto active_value = evaluate_event(active, current_time);
                auto span = event.time - current_time;
                if (event.type == EventType::LinearRamp && span > 0) {
                    auto fraction = (time - current_time) / span;
                    return active_value + (event.value - active_value) * static_cast<float>(fraction);
                }
                if (event.type == EventType::ExponentialRamp && span > 0 && active_value > 0 && event.value > 0) {
                    auto fraction = (time - current_time) / span;
                    return active_value * static_cast<float>(pow(event.value / active_value, fraction));
                }
                return evaluate_event(active, time);
            }

            if (event.type == EventType::LinearRamp) {
                auto span = event.time - current_time;
                if (span <= 0)
                    return event.value;
                auto fraction = (time - current_time) / span;
                return current_value + (event.value - current_value) * static_cast<float>(fraction);
            }

            if (event.type == EventType::ExponentialRamp) {
                auto span = event.time - current_time;
                if (span <= 0 || current_value <= 0 || event.value <= 0)
                    return event.value;
                auto fraction = (time - current_time) / span;
                return current_value * static_cast<float>(pow(event.value / current_value, fraction));
            }

            return current_value;
        }

        if (active_event.has_value()) {
            current_value = evaluate_event(m_events[active_event.value()], event.time);
            current_time = event.time;
            active_event.clear();
        }

        switch (event.type) {
        case EventType::SetValue:
        case EventType::LinearRamp:
        case EventType::ExponentialRamp:
            current_value = event.value;
            current_time = event.time;
            break;
        case EventType::SetTarget:
        case EventType::SetValueCurve:
            active_event = i;
            current_time = event.time;
            break;
        }
    }

    if (active_event.has_value())
        return evaluate_event(m_events[active_event.value()], time);
    return current_value;
}

float AudioParamRenderData::evaluate_event(Event const& event, double time) const
{
    if (event.type == EventType::SetValueCurve && !event.curve.is_empty() && time == event.time)
        return event.curve.first();
    if (time <= event.time)
        return event.start_value;

    switch (event.type) {
    case EventType::SetTarget:
        return event.value + (event.start_value - event.value) * static_cast<float>(exp(-(time - event.time) / event.time_constant));
    case EventType::SetValueCurve: {
        if (event.curve.is_empty() || event.duration <= 0)
            return event.start_value;
        auto position = (time - event.time) / event.duration;
        if (position >= 1)
            return event.curve.last();
        auto scaled = position * (event.curve.size() - 1);
        auto index = static_cast<size_t>(scaled);
        auto fraction = static_cast<float>(scaled - index);
        if (index + 1 >= event.curve.size())
            return event.curve.last();
        return event.curve[index] + (event.curve[index + 1] - event.curve[index]) * fraction;
    }
    case EventType::SetValue:
    case EventType::LinearRamp:
    case EventType::ExponentialRamp:
        return event.value;
    }
    VERIFY_NOT_REACHED();
}

}
