/*
 * Copyright (c) 2024, Bar Yemini <bar.ye651@gmail.com>
 *
 * SPDX-License-Identifier: BSD-2-Clause
 */

#include <AK/Math.h>
#include <LibWeb/Bindings/AudioParamPrototype.h>
#include <LibWeb/Bindings/BiquadFilterNodePrototype.h>
#include <LibWeb/Bindings/Intrinsics.h>
#include <LibWeb/WebAudio/AudioNode.h>
#include <LibWeb/WebAudio/AudioParam.h>
#include <LibWeb/WebAudio/BaseAudioContext.h>
#include <LibWeb/WebAudio/BiquadFilterNode.h>
#include <math.h>

namespace Web::WebAudio {

GC_DEFINE_ALLOCATOR(BiquadFilterNode);

BiquadFilterNode::BiquadFilterNode(JS::Realm& realm, GC::Ref<BaseAudioContext> context, BiquadFilterOptions const& options)
    : AudioNode(realm, context)
    , m_type(options.type)
    , m_frequency(AudioParam::create(realm, context, options.frequency, 0, context->nyquist_frequency(), Bindings::AutomationRate::ARate))
    , m_detune(AudioParam::create(realm, context, options.detune, -1200 * AK::log2(NumericLimits<float>::max()), 1200 * AK::log2(NumericLimits<float>::max()), Bindings::AutomationRate::ARate))
    , m_q(AudioParam::create(realm, context, options.q, NumericLimits<float>::lowest(), NumericLimits<float>::max(), Bindings::AutomationRate::ARate))
    , m_gain(AudioParam::create(realm, context, options.gain, NumericLimits<float>::lowest(), 40 * AK::log10(NumericLimits<float>::max()), Bindings::AutomationRate::ARate))
{
}

BiquadFilterNode::~BiquadFilterNode() = default;

// https://webaudio.github.io/web-audio-api/#dom-biquadfilternode-type
void BiquadFilterNode::set_type(Bindings::BiquadFilterType type)
{
    m_type = type;
}

// https://webaudio.github.io/web-audio-api/#dom-biquadfilternode-type
Bindings::BiquadFilterType BiquadFilterNode::type() const
{
    return m_type;
}

// https://webaudio.github.io/web-audio-api/#dom-biquadfilternode-frequency
GC::Ref<AudioParam> BiquadFilterNode::frequency() const
{
    return m_frequency;
}

// https://webaudio.github.io/web-audio-api/#dom-biquadfilternode-detune
GC::Ref<AudioParam> BiquadFilterNode::detune() const
{
    return m_detune;
}

// https://webaudio.github.io/web-audio-api/#dom-biquadfilternode-q
GC::Ref<AudioParam> BiquadFilterNode::q() const
{
    return m_q;
}

// https://webaudio.github.io/web-audio-api/#dom-biquadfilternode-gain
GC::Ref<AudioParam> BiquadFilterNode::gain() const
{
    return m_gain;
}

// https://webaudio.github.io/web-audio-api/#dom-biquadfilternode-getfrequencyresponse
WebIDL::ExceptionOr<void> BiquadFilterNode::get_frequency_response(GC::Root<WebIDL::BufferSource> const& frequency_hz, GC::Root<WebIDL::BufferSource> const& mag_response, GC::Root<WebIDL::BufferSource> const& phase_response)
{
    auto& vm = this->vm();
    if (!is<JS::Float32Array>(*frequency_hz->raw_object())
        || !is<JS::Float32Array>(*mag_response->raw_object())
        || !is<JS::Float32Array>(*phase_response->raw_object()))
        return vm.throw_completion<JS::TypeError>(JS::ErrorType::NotAnObjectOfType, "Float32Array");

    auto& frequencies = static_cast<JS::Float32Array&>(*frequency_hz->raw_object());
    auto& magnitudes = static_cast<JS::Float32Array&>(*mag_response->raw_object());
    auto& phases = static_cast<JS::Float32Array&>(*phase_response->raw_object());
    if (frequencies.viewed_array_buffer()->is_detached()
        || magnitudes.viewed_array_buffer()->is_detached()
        || phases.viewed_array_buffer()->is_detached())
        return vm.throw_completion<JS::TypeError>(JS::ErrorType::DetachedArrayBuffer);
    if (frequencies.data().size() != magnitudes.data().size()
        || frequencies.data().size() != phases.data().size())
        return WebIDL::IndexSizeError::create(realm(), "Frequency and response arrays must have the same length"_utf16);

    auto const sample_rate = context()->sample_rate();
    if (!isfinite(sample_rate) || sample_rate <= 0)
        return WebIDL::InvalidStateError::create(realm(), "Audio context has no valid sample rate"_utf16);

    auto const parameter_frequency = frequency()->value();
    auto const parameter_detune = detune()->value();
    auto const parameter_q = q()->value();
    auto const parameter_gain = gain()->value();
    if (!isfinite(parameter_frequency) || !isfinite(parameter_detune) || !isfinite(parameter_q) || !isfinite(parameter_gain))
        return WebIDL::InvalidStateError::create(realm(), "BiquadFilter parameters must be finite"_utf16);
    auto const frequency_scale = pow(2.0f, parameter_detune / 1200.0f);
    auto const effective_frequency = clamp(parameter_frequency * frequency_scale, 0.0f, sample_rate / 2.0f);
    auto const q_value = max(abs(parameter_q), 1e-8f);
    auto const gain_factor = pow(10.0f, parameter_gain / 40.0f);
    auto const parameter_omega = 2.0f * AK::Pi<float> * effective_frequency / sample_rate;
    auto const parameter_cosine = cos(parameter_omega);
    auto const parameter_sine = sin(parameter_omega);
    auto const alpha = parameter_sine / (2.0f * q_value);
    auto const shelf_alpha = parameter_sine * 0.5f * AK::sqrt(2.0f);
    auto const two_sqrt_gain_alpha = 2.0f * AK::sqrt(gain_factor) * shelf_alpha;

    auto const count = frequencies.data().size();
    for (size_t i = 0; i < count; ++i) {
        auto input_frequency = frequencies.data()[i];
        if (!isfinite(input_frequency)) {
            if (i < magnitudes.data().size())
                magnitudes.data()[i] = 0;
            if (i < phases.data().size())
                phases.data()[i] = 0;
            continue;
        }

        float b0 = 0;
        float b1 = 0;
        float b2 = 0;
        float a0 = 1;
        float a1 = 0;
        float a2 = 0;
        switch (type()) {
        case Bindings::BiquadFilterType::Lowpass:
            b0 = (1 - parameter_cosine) / 2;
            b1 = 1 - parameter_cosine;
            b2 = b0;
            a0 = 1 + alpha;
            a1 = -2 * parameter_cosine;
            a2 = 1 - alpha;
            break;
        case Bindings::BiquadFilterType::Highpass:
            b0 = (1 + parameter_cosine) / 2;
            b1 = -(1 + parameter_cosine);
            b2 = b0;
            a0 = 1 + alpha;
            a1 = -2 * parameter_cosine;
            a2 = 1 - alpha;
            break;
        case Bindings::BiquadFilterType::Bandpass:
            b0 = parameter_sine / 2;
            b1 = 0;
            b2 = -parameter_sine / 2;
            a0 = 1 + alpha;
            a1 = -2 * parameter_cosine;
            a2 = 1 - alpha;
            break;
        case Bindings::BiquadFilterType::Notch:
            b0 = 1;
            b1 = -2 * parameter_cosine;
            b2 = 1;
            a0 = 1 + alpha;
            a1 = -2 * parameter_cosine;
            a2 = 1 - alpha;
            break;
        case Bindings::BiquadFilterType::Allpass:
            b0 = 1 - alpha;
            b1 = -2 * parameter_cosine;
            b2 = 1 + alpha;
            a0 = 1 + alpha;
            a1 = -2 * parameter_cosine;
            a2 = 1 - alpha;
            break;
        case Bindings::BiquadFilterType::Peaking:
            b0 = 1 + alpha * gain_factor;
            b1 = -2 * parameter_cosine;
            b2 = 1 - alpha * gain_factor;
            a0 = 1 + alpha / gain_factor;
            a1 = -2 * parameter_cosine;
            a2 = 1 - alpha / gain_factor;
            break;
        case Bindings::BiquadFilterType::Lowshelf:
            b0 = gain_factor * ((gain_factor + 1) - (gain_factor - 1) * parameter_cosine + two_sqrt_gain_alpha);
            b1 = 2 * gain_factor * ((gain_factor - 1) - (gain_factor + 1) * parameter_cosine);
            b2 = gain_factor * ((gain_factor + 1) - (gain_factor - 1) * parameter_cosine - two_sqrt_gain_alpha);
            a0 = (gain_factor + 1) + (gain_factor - 1) * parameter_cosine + two_sqrt_gain_alpha;
            a1 = -2 * ((gain_factor - 1) + (gain_factor + 1) * parameter_cosine);
            a2 = (gain_factor + 1) + (gain_factor - 1) * parameter_cosine - two_sqrt_gain_alpha;
            break;
        case Bindings::BiquadFilterType::Highshelf:
            b0 = gain_factor * ((gain_factor + 1) + (gain_factor - 1) * parameter_cosine + two_sqrt_gain_alpha);
            b1 = -2 * gain_factor * ((gain_factor - 1) + (gain_factor + 1) * parameter_cosine);
            b2 = gain_factor * ((gain_factor + 1) + (gain_factor - 1) * parameter_cosine - two_sqrt_gain_alpha);
            a0 = (gain_factor + 1) - (gain_factor - 1) * parameter_cosine + two_sqrt_gain_alpha;
            a1 = 2 * ((gain_factor - 1) - (gain_factor + 1) * parameter_cosine);
            a2 = (gain_factor + 1) - (gain_factor - 1) * parameter_cosine - two_sqrt_gain_alpha;
            break;
        }

        auto const normalization = 1.0f / a0;
        b0 *= normalization;
        b1 *= normalization;
        b2 *= normalization;
        a1 *= normalization;
        a2 *= normalization;

        auto const response_frequency = 2.0f * AK::Pi<float> * abs(input_frequency) / sample_rate;
        auto const response_cosine = cos(response_frequency);
        auto const response_sine = sin(response_frequency);
        auto const response_cosine2 = cos(2.0f * response_frequency);
        auto const response_sine2 = sin(2.0f * response_frequency);
        auto const numerator_real = b0 + b1 * response_cosine + b2 * response_cosine2;
        auto const numerator_imaginary = -(b1 * response_sine + b2 * response_sine2);
        auto const denominator_real = 1 + a1 * response_cosine + a2 * response_cosine2;
        auto const denominator_imaginary = -(a1 * response_sine + a2 * response_sine2);
        auto const denominator_magnitude_squared = denominator_real * denominator_real + denominator_imaginary * denominator_imaginary;
        if (i < magnitudes.data().size())
            magnitudes.data()[i] = denominator_magnitude_squared > 0 ? AK::sqrt((numerator_real * numerator_real + numerator_imaginary * numerator_imaginary) / denominator_magnitude_squared) : 0;
        if (i < phases.data().size())
            phases.data()[i] = atan2(numerator_imaginary, numerator_real) - atan2(denominator_imaginary, denominator_real);
    }
    return { };
}

WebIDL::ExceptionOr<GC::Ref<BiquadFilterNode>> BiquadFilterNode::create(JS::Realm& realm, GC::Ref<BaseAudioContext> context, BiquadFilterOptions const& options)
{
    return construct_impl(realm, context, options);
}

// https://webaudio.github.io/web-audio-api/#dom-biquadfilternode-biquadfilternode
WebIDL::ExceptionOr<GC::Ref<BiquadFilterNode>> BiquadFilterNode::construct_impl(JS::Realm& realm, GC::Ref<BaseAudioContext> context, BiquadFilterOptions const& options)
{
    // When the constructor is called with a BaseAudioContext c and an option object option, the user agent
    // MUST initialize the AudioNode this, with context and options as arguments.
    auto node = realm.create<BiquadFilterNode>(realm, context, options);

    // Default options for channel count and interpretation
    // https://webaudio.github.io/web-audio-api/#BiquadFilterNode
    AudioNodeDefaultOptions default_options;
    default_options.channel_count_mode = Bindings::ChannelCountMode::Max;
    default_options.channel_interpretation = Bindings::ChannelInterpretation::Speakers;
    default_options.channel_count = 2;
    // FIXME: Set tail-time to yes

    TRY(node->initialize_audio_node_options(options, default_options));

    return node;
}

void BiquadFilterNode::initialize(JS::Realm& realm)
{
    WEB_SET_PROTOTYPE_FOR_INTERFACE(BiquadFilterNode);
    Base::initialize(realm);
}

void BiquadFilterNode::visit_edges(Cell::Visitor& visitor)
{
    Base::visit_edges(visitor);
    visitor.visit(m_frequency);
    visitor.visit(m_detune);
    visitor.visit(m_q);
    visitor.visit(m_gain);
}

}
