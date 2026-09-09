/*
 * Copyright (c) 2024, Shannon Booth <shannon@serenityos.org>
 *
 * SPDX-License-Identifier: BSD-2-Clause
 */

#include <LibWeb/Bindings/AudioParamPrototype.h>
#include <LibWeb/Bindings/Intrinsics.h>
#include <LibWeb/WebAudio/AudioParam.h>
#include <LibWeb/WebAudio/BaseAudioContext.h>
#include <LibWeb/WebIDL/ExceptionOr.h>
#include <math.h>

namespace Web::WebAudio {

GC_DEFINE_ALLOCATOR(AudioParam);

AudioParam::AudioParam(JS::Realm& realm, GC::Ref<BaseAudioContext> context, float default_value, float min_value, float max_value, Bindings::AutomationRate automation_rate, FixedAutomationRate fixed_automation_rate)
    : Bindings::PlatformObject(realm)
    , m_context(context)
    , m_current_value(default_value)
    , m_default_value(default_value)
    , m_min_value(min_value)
    , m_max_value(max_value)
    , m_automation_rate(automation_rate)
    , m_fixed_automation_rate(fixed_automation_rate)
{
}

GC::Ref<AudioParam> AudioParam::create(JS::Realm& realm, GC::Ref<BaseAudioContext> context, float default_value, float min_value, float max_value, Bindings::AutomationRate automation_rate, FixedAutomationRate fixed_automation_rate)
{
    return realm.create<AudioParam>(realm, context, default_value, min_value, max_value, automation_rate, fixed_automation_rate);
}

AudioParam::~AudioParam() = default;

// https://webaudio.github.io/web-audio-api/#dom-audioparam-value
// https://webaudio.github.io/web-audio-api/#simple-nominal-range
float AudioParam::value() const
{
    // Each AudioParam includes minValue and maxValue attributes that together form the simple nominal range
    // for the parameter. In effect, value of the parameter is clamped to the range [minValue, maxValue].
    return clamp(value_at_time(m_context->current_time()), min_value(), max_value());
}

// https://webaudio.github.io/web-audio-api/#dom-audioparam-value
void AudioParam::set_value(float value)
{
    m_current_value = value;
}

float AudioParam::value_at_time(double time) const
{
    if (!isfinite(time) || time < 0)
        return m_current_value;

    float current_value = m_current_value;
    double current_time = 0;
    Optional<size_t> active_event;

    for (size_t i = 0; i < m_automation_events.size(); ++i) {
        auto const& event = m_automation_events[i];

        if (time < event.time) {
            if (active_event.has_value()) {
                auto const& active = m_automation_events[active_event.value()];
                auto active_value = evaluate_event(active, current_time);
                auto span = event.time - current_time;
                if (event.type == AutomationEventType::LinearRamp && span > 0) {
                    auto fraction = (time - current_time) / span;
                    return active_value + (event.value - active_value) * static_cast<float>(fraction);
                }
                if (event.type == AutomationEventType::ExponentialRamp && span > 0 && active_value > 0 && event.value > 0) {
                    auto fraction = (time - current_time) / span;
                    return active_value * static_cast<float>(pow(event.value / active_value, fraction));
                }
                return evaluate_event(active, time);
            }

            if (event.type == AutomationEventType::LinearRamp) {
                auto span = event.time - current_time;
                if (span <= 0)
                    return event.value;
                auto fraction = (time - current_time) / span;
                return current_value + (event.value - current_value) * static_cast<float>(fraction);
            }

            if (event.type == AutomationEventType::ExponentialRamp) {
                auto span = event.time - current_time;
                if (span <= 0 || current_value <= 0 || event.value <= 0)
                    return event.value;
                auto fraction = (time - current_time) / span;
                return current_value * static_cast<float>(pow(event.value / current_value, fraction));
            }

            return current_value;
        }

        if (active_event.has_value()) {
            current_value = evaluate_event(m_automation_events[active_event.value()], event.time);
            current_time = event.time;
            active_event.clear();
        }

        switch (event.type) {
        case AutomationEventType::SetValue:
        case AutomationEventType::LinearRamp:
        case AutomationEventType::ExponentialRamp:
            current_value = event.value;
            current_time = event.time;
            break;
        case AutomationEventType::SetTarget:
        case AutomationEventType::SetValueCurve:
            active_event = i;
            current_time = event.time;
            break;
        }
    }

    if (active_event.has_value())
        return evaluate_event(m_automation_events[active_event.value()], time);
    return current_value;
}

void AudioParam::insert_event(AutomationEvent&& event)
{
    size_t index = 0;
    while (index < m_automation_events.size() && m_automation_events[index].time <= event.time)
        ++index;
    m_automation_events.insert(index, move(event));
}

float AudioParam::evaluate_event(AutomationEvent const& event, double time) const
{
    if (time <= event.time)
        return event.start_value;

    switch (event.type) {
    case AutomationEventType::SetTarget:
        return event.value + (event.start_value - event.value) * static_cast<float>(exp(-(time - event.time) / event.time_constant));
    case AutomationEventType::SetValueCurve: {
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
    case AutomationEventType::SetValue:
    case AutomationEventType::LinearRamp:
    case AutomationEventType::ExponentialRamp:
        return event.value;
    }
    VERIFY_NOT_REACHED();
}

// https://webaudio.github.io/web-audio-api/#dom-audioparam-automationrate
Bindings::AutomationRate AudioParam::automation_rate() const
{
    return m_automation_rate;
}

// https://webaudio.github.io/web-audio-api/#dom-audioparam-automationrate
WebIDL::ExceptionOr<void> AudioParam::set_automation_rate(Bindings::AutomationRate automation_rate)
{
    if (automation_rate != m_automation_rate && m_fixed_automation_rate == FixedAutomationRate::Yes)
        return WebIDL::InvalidStateError::create(realm(), "Automation rate cannot be changed"_utf16);

    m_automation_rate = automation_rate;
    return { };
}

// https://webaudio.github.io/web-audio-api/#dom-audioparam-defaultvalue
float AudioParam::default_value() const
{
    return m_default_value;
}

// https://webaudio.github.io/web-audio-api/#dom-audioparam-minvalue
float AudioParam::min_value() const
{
    return m_min_value;
}

// https://webaudio.github.io/web-audio-api/#dom-audioparam-maxvalue
float AudioParam::max_value() const
{
    return m_max_value;
}

// https://webaudio.github.io/web-audio-api/#dom-audioparam-setvalueattime
WebIDL::ExceptionOr<GC::Ref<AudioParam>> AudioParam::set_value_at_time(float value, double start_time)
{
    if (!isfinite(start_time) || start_time < 0)
        return WebIDL::SimpleException { WebIDL::SimpleExceptionType::RangeError, "startTime must be a finite non-negative value"sv };

    AutomationEvent event;
    event.type = AutomationEventType::SetValue;
    event.time = start_time;
    event.value = value;
    event.start_value = value_at_time(start_time);
    insert_event(move(event));
    return GC::Ref { *this };
}

// https://webaudio.github.io/web-audio-api/#dom-audioparam-linearramptovalueattime
WebIDL::ExceptionOr<GC::Ref<AudioParam>> AudioParam::linear_ramp_to_value_at_time(float value, double end_time)
{
    if (!isfinite(end_time) || end_time < 0)
        return WebIDL::SimpleException { WebIDL::SimpleExceptionType::RangeError, "endTime must be a finite non-negative value"sv };

    AutomationEvent event;
    event.type = AutomationEventType::LinearRamp;
    event.time = end_time;
    event.value = value;
    event.start_value = value_at_time(end_time);
    insert_event(move(event));
    return GC::Ref { *this };
}

// https://webaudio.github.io/web-audio-api/#dom-audioparam-exponentialramptovalueattime
WebIDL::ExceptionOr<GC::Ref<AudioParam>> AudioParam::exponential_ramp_to_value_at_time(float value, double end_time)
{
    if (!isfinite(end_time) || end_time < 0 || !isfinite(value) || value <= 0)
        return WebIDL::SimpleException { WebIDL::SimpleExceptionType::RangeError, "Exponential ramp requires a finite positive value and endTime"sv };

    auto start_value = value_at_time(end_time);
    if (!isfinite(start_value) || start_value <= 0)
        return WebIDL::SimpleException { WebIDL::SimpleExceptionType::RangeError, "Exponential ramp requires a positive starting value"sv };

    AutomationEvent event;
    event.type = AutomationEventType::ExponentialRamp;
    event.time = end_time;
    event.value = value;
    event.start_value = start_value;
    insert_event(move(event));
    return GC::Ref { *this };
}

// https://webaudio.github.io/web-audio-api/#dom-audioparam-settargetattime
WebIDL::ExceptionOr<GC::Ref<AudioParam>> AudioParam::set_target_at_time(float target, double start_time, float time_constant)
{
    if (!isfinite(start_time) || start_time < 0 || !isfinite(time_constant) || time_constant <= 0)
        return WebIDL::SimpleException { WebIDL::SimpleExceptionType::RangeError, "startTime must be finite and timeConstant must be positive"sv };

    AutomationEvent event;
    event.type = AutomationEventType::SetTarget;
    event.time = start_time;
    event.value = target;
    event.start_value = value_at_time(start_time);
    event.time_constant = time_constant;
    insert_event(move(event));
    return GC::Ref { *this };
}

// https://webaudio.github.io/web-audio-api/#dom-audioparam-setvaluecurveattime
WebIDL::ExceptionOr<GC::Ref<AudioParam>> AudioParam::set_value_curve_at_time(Span<float> values, double start_time, double duration)
{
    if (values.size() < 2)
        return WebIDL::InvalidStateError::create(realm(), "A value curve must contain at least two values"_utf16);
    if (!isfinite(start_time) || start_time < 0 || !isfinite(duration) || duration <= 0)
        return WebIDL::SimpleException { WebIDL::SimpleExceptionType::RangeError, "startTime must be finite and duration must be positive"sv };
    for (auto value : values) {
        if (!isfinite(value))
            return WebIDL::SimpleException { WebIDL::SimpleExceptionType::RangeError, "Value curve entries must be finite"sv };
    }

    AutomationEvent event;
    event.type = AutomationEventType::SetValueCurve;
    event.time = start_time;
    event.start_value = value_at_time(start_time);
    event.duration = duration;
    event.curve.ensure_capacity(values.size());
    for (auto value : values)
        event.curve.append(value);
    insert_event(move(event));
    return GC::Ref { *this };
}

// https://webaudio.github.io/web-audio-api/#dom-audioparam-cancelscheduledvalues
WebIDL::ExceptionOr<GC::Ref<AudioParam>> AudioParam::cancel_scheduled_values(double cancel_time)
{
    if (!isfinite(cancel_time) || cancel_time < 0)
        return WebIDL::SimpleException { WebIDL::SimpleExceptionType::RangeError, "cancelTime must be a finite non-negative value"sv };

    for (size_t i = m_automation_events.size(); i > 0; --i) {
        if (m_automation_events[i - 1].time >= cancel_time)
            m_automation_events.remove(i - 1);
    }
    return GC::Ref { *this };
}

// https://webaudio.github.io/web-audio-api/#dom-audioparam-cancelandholdattime
WebIDL::ExceptionOr<GC::Ref<AudioParam>> AudioParam::cancel_and_hold_at_time(double cancel_time)
{
    if (!isfinite(cancel_time) || cancel_time < 0)
        return WebIDL::SimpleException { WebIDL::SimpleExceptionType::RangeError, "cancelTime must be a finite non-negative value"sv };

    auto held_value = value_at_time(cancel_time);
    for (size_t i = m_automation_events.size(); i > 0; --i) {
        if (m_automation_events[i - 1].time >= cancel_time)
            m_automation_events.remove(i - 1);
    }

    AutomationEvent event;
    event.type = AutomationEventType::SetValue;
    event.time = cancel_time;
    event.value = held_value;
    event.start_value = held_value;
    insert_event(move(event));
    return GC::Ref { *this };
}

void AudioParam::initialize(JS::Realm& realm)
{
    WEB_SET_PROTOTYPE_FOR_INTERFACE(AudioParam);
    Base::initialize(realm);
}

void AudioParam::visit_edges(Cell::Visitor& visitor)
{
    Base::visit_edges(visitor);
    visitor.visit(m_context);
}

}
