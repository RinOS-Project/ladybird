/*
 * Copyright (c) 2024, Shannon Booth <shannon@serenityos.org>
 *
 * SPDX-License-Identifier: BSD-2-Clause
 */

#pragma once

#include <AK/Error.h>
#include <AK/RefPtr.h>
#include <AK/Vector.h>
#include <LibJS/Forward.h>
#include <LibWeb/Bindings/AudioParamPrototype.h>
#include <LibWeb/Bindings/PlatformObject.h>
#include <LibWeb/WebAudio/Types.h>

namespace Web::WebAudio {

class AudioParamRenderData;

// https://webaudio.github.io/web-audio-api/#AudioParam
class AudioParam final : public Bindings::PlatformObject {
    WEB_PLATFORM_OBJECT(AudioParam, Bindings::PlatformObject);
    GC_DECLARE_ALLOCATOR(AudioParam);

public:
    enum class FixedAutomationRate {
        No,
        Yes,
    };
    static GC::Ref<AudioParam> create(JS::Realm&, GC::Ref<BaseAudioContext>, float default_value, float min_value, float max_value, Bindings::AutomationRate, FixedAutomationRate = FixedAutomationRate::No);

    virtual ~AudioParam() override;

    GC::Ref<BaseAudioContext> context() const { return m_context; }

    float value() const;
    void set_value(float);
    AudioParamID param_id() const { return m_param_id; }

    // Returns the value produced by the automation timeline at the supplied
    // audio time.  The renderer uses this instead of reading the base value
    // directly so scheduled events are observable by every node.
    float value_at_time(double) const;

    // Publish an immutable automation snapshot for a rendering control
    // message. The snapshot is safe to evaluate from a real-time callback.
    ErrorOr<NonnullRefPtr<AudioParamRenderData>> create_render_data() const;

    Bindings::AutomationRate automation_rate() const;
    WebIDL::ExceptionOr<void> set_automation_rate(Bindings::AutomationRate);

    float default_value() const;
    float min_value() const;
    float max_value() const;

    WebIDL::ExceptionOr<GC::Ref<AudioParam>> set_value_at_time(float value, double start_time);
    WebIDL::ExceptionOr<GC::Ref<AudioParam>> linear_ramp_to_value_at_time(float value, double end_time);
    WebIDL::ExceptionOr<GC::Ref<AudioParam>> exponential_ramp_to_value_at_time(float value, double end_time);
    WebIDL::ExceptionOr<GC::Ref<AudioParam>> set_target_at_time(float target, double start_time, float time_constant);
    WebIDL::ExceptionOr<GC::Ref<AudioParam>> set_value_curve_at_time(Span<float> values, double start_time, double duration);
    WebIDL::ExceptionOr<GC::Ref<AudioParam>> cancel_scheduled_values(double cancel_time);
    WebIDL::ExceptionOr<GC::Ref<AudioParam>> cancel_and_hold_at_time(double cancel_time);

private:
    enum class AutomationEventType {
        SetValue,
        LinearRamp,
        ExponentialRamp,
        SetTarget,
        SetValueCurve,
    };

    struct AutomationEvent {
        AutomationEventType type { AutomationEventType::SetValue };
        double time { 0 };
        float value { 0 };
        float start_value { 0 };
        float time_constant { 0 };
        double duration { 0 };
        Vector<float> curve;
    };

    AudioParam(JS::Realm&, GC::Ref<BaseAudioContext>, float default_value, float min_value, float max_value, Bindings::AutomationRate, FixedAutomationRate = FixedAutomationRate::No);

    void insert_event(AutomationEvent&&);
    float evaluate_event(AutomationEvent const&, double) const;
    void publish_render_update() const;

    GC::Ref<BaseAudioContext> m_context;
    AudioParamID m_param_id { 0 };

    // https://webaudio.github.io/web-audio-api/#dom-audioparam-current-value-slot
    float m_current_value { }; //  [[current value]]

    float m_default_value { };

    float m_min_value { };
    float m_max_value { };

    Bindings::AutomationRate m_automation_rate { };

    FixedAutomationRate m_fixed_automation_rate { FixedAutomationRate::No };

    Vector<AutomationEvent> m_automation_events;

    virtual void initialize(JS::Realm&) override;
    virtual void visit_edges(Cell::Visitor&) override;
};

}
