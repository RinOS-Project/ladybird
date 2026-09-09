/*
 * Copyright (c) 2024, Shannon Booth <shannon@serenityos.org>
 * Copyright (c) 2025, Ben Eidson <b.e.eidson@gmail.com>
 *
 * SPDX-License-Identifier: BSD-2-Clause
 */

#include <AK/Array.h>
#include <AK/Math.h>
#include <LibJS/Runtime/TypedArray.h>
#include <LibWeb/Bindings/Intrinsics.h>
#include <LibWeb/DOM/Document.h>
#include <LibWeb/DOM/Event.h>
#include <LibWeb/HTML/EventNames.h>
#include <LibWeb/HTML/Navigable.h>
#include <LibWeb/HTML/Scripting/TemporaryExecutionContext.h>
#include <LibWeb/HTML/Window.h>
#include <LibWeb/WebAudio/AudioBuffer.h>
#include <LibWeb/WebAudio/AudioDestinationNode.h>
#include <LibWeb/WebAudio/ControlMessage.h>
#include <LibWeb/WebAudio/OfflineAudioCompletionEvent.h>
#include <LibWeb/WebAudio/OfflineAudioContext.h>
#include <math.h>

namespace Web::WebAudio {

GC_DEFINE_ALLOCATOR(OfflineAudioContext);

// https://webaudio.github.io/web-audio-api/#dom-offlineaudiocontext-offlineaudiocontext
WebIDL::ExceptionOr<GC::Ref<OfflineAudioContext>> OfflineAudioContext::construct_impl(JS::Realm& realm, OfflineAudioContextOptions const& context_options)
{
    // AD-HOC: This spec text is currently only mentioned in the constructor overload that takes separate arguments,
    //         but these parameters should be validated for both constructors.
    // A NotSupportedError exception MUST be thrown if any of the arguments is negative, zero, or outside its nominal range.
    TRY(verify_audio_options_inside_nominal_range(realm, context_options.number_of_channels, context_options.length, context_options.sample_rate));

    // Let c be a new OfflineAudioContext object. Initialize c as follows:
    auto c = realm.create<OfflineAudioContext>(realm, context_options.number_of_channels, context_options.length, context_options.sample_rate);

    // 1. Set the [[control thread state]] for c to "suspended".
    c->set_control_state(Bindings::AudioContextState::Suspended);

    // 2. Set the [[rendering thread state]] for c to "suspended".
    c->set_rendering_state(Bindings::AudioContextState::Suspended);

    // FIXME: 3. Determine the [[render quantum size]] for this OfflineAudioContext, based on the value of the renderSizeHint:

    // 4. Construct an AudioDestinationNode with its channelCount set to contextOptions.numberOfChannels.
    c->m_destination = TRY(AudioDestinationNode::construct_impl(realm, c, context_options.number_of_channels));

    // FIXME: 5. Let messageChannel be a new MessageChannel.
    // FIXME: 6. Let controlSidePort be the value of messageChannel’s port1 attribute.
    // FIXME: 7. Let renderingSidePort be the value of messageChannel’s port2 attribute.
    // FIXME: 8. Let serializedRenderingSidePort be the result of StructuredSerializeWithTransfer(renderingSidePort, « renderingSidePort »).
    // FIXME: 9. Set this audioWorklet's port to controlSidePort.
    // FIXME: 10. Queue a control message to set the MessagePort on the AudioContextGlobalScope, with serializedRenderingSidePort.

    return c;
}

// https://webaudio.github.io/web-audio-api/#dom-offlineaudiocontext-offlineaudiocontext-numberofchannels-length-samplerate
WebIDL::ExceptionOr<GC::Ref<OfflineAudioContext>> OfflineAudioContext::construct_impl(
    JS::Realm& realm,
    WebIDL::UnsignedLong number_of_channels,
    WebIDL::UnsignedLong length,
    float sample_rate)
{
    return construct_impl(realm, { number_of_channels, length, sample_rate });
}

OfflineAudioContext::~OfflineAudioContext() = default;

// https://webaudio.github.io/web-audio-api/#dom-offlineaudiocontext-startrendering
WebIDL::ExceptionOr<GC::Ref<WebIDL::Promise>> OfflineAudioContext::start_rendering()
{
    auto& realm = this->realm();

    // 1. If this’s relevant global object’s associated Document is not fully active then return a promise rejected with "InvalidStateError" DOMException.
    auto& window = as<HTML::Window>(HTML::relevant_global_object(*this));
    auto const& associated_document = window.associated_document();

    if (!associated_document.is_fully_active()) {
        auto error = WebIDL::InvalidStateError::create(realm, "Document is not fully active"_utf16);
        return WebIDL::create_rejected_promise_from_exception(realm, error);
    }

    // AD-HOC: Not in spec explicitly, but this should account for detached iframes too. See /the-offlineaudiocontext-interface/startrendering-after-discard.html WPT.
    auto navigable = window.navigable();
    if (navigable && navigable->has_been_destroyed()) {
        auto error = WebIDL::InvalidStateError::create(realm, "The iframe has been detached"_utf16);
        return WebIDL::create_rejected_promise_from_exception(realm, error);
    }

    // 2. If the [[rendering started]] slot on the OfflineAudioContext is true, return a rejected promise with InvalidStateError, and abort these steps.
    if (m_rendering_started) {
        auto error = WebIDL::InvalidStateError::create(realm, "Rendering is already started"_utf16);
        return WebIDL::create_rejected_promise_from_exception(realm, error);
    }

    // 3. Set the [[rendering started]] slot of the OfflineAudioContext to true.
    m_rendering_started = true;
    set_control_state(Bindings::AudioContextState::Running);
    set_rendering_state(Bindings::AudioContextState::Running);

    // 4. Let promise be a new promise.
    auto promise = WebIDL::create_promise(realm);

    // 5. Create a new AudioBuffer, with a number of channels, length and sample rate equal respectively to the
    //    numberOfChannels, length and sampleRate values passed to this instance’s constructor in the contextOptions
    //    parameter.
    auto buffer_result = create_buffer(m_number_of_channels, length(), sample_rate());

    // 6. If an exception was thrown during the preceding AudioBuffer constructor call, reject promise with this exception.
    if (buffer_result.is_exception()) {
        return WebIDL::create_rejected_promise_from_exception(realm, buffer_result.exception());
    }

    // Assign this buffer to an internal slot [[rendered buffer]] in the OfflineAudioContext.
    m_rendered_buffer = buffer_result.release_value();

    // 7. Otherwise, in the case that the buffer was successfully constructed, begin offline rendering.
    begin_offline_rendering(promise);

    // 8. Append promise to [[pending promises]].
    m_pending_promises.append(promise);

    // 9. Return promise.
    return promise;
}

void OfflineAudioContext::begin_offline_rendering(GC::Ref<WebIDL::Promise> promise)
{
    struct OfflineBufferSource {
        RefPtr<AudioBufferRenderData> buffer;
        double start_time { 0.0 };
        double offset { 0.0 };
        Optional<double> duration;
        Optional<double> stop_time;
        float playback_rate { 1.0f };
        float detune { 0.0f };
        RefPtr<AudioParamRenderData> playback_rate_automation;
        RefPtr<AudioParamRenderData> detune_automation;
        bool loop { false };
        double loop_start { 0.0 };
        double loop_end { 0.0 };
        NodeID node_id { 0 };
    };
    struct OfflineOscillator {
        double start_time { 0.0 };
        Optional<double> stop_time;
        float frequency { 440.0f };
        float detune { 0.0f };
        RefPtr<AudioParamRenderData> frequency_automation;
        RefPtr<AudioParamRenderData> detune_automation;
        RefPtr<PeriodicWaveRenderData> periodic_wave;
        OscillatorWaveform waveform { OscillatorWaveform::Sine };
        NodeID node_id { 0 };
    };
    struct OfflineNodeConnection {
        NodeID source_node_id { 0 };
        NodeID destination_node_id { 0 };
        AudioNodeRenderKind destination_kind { AudioNodeRenderKind::Unknown };
        RefPtr<AudioParamRenderData> gain_automation;
        RefPtr<BiquadFilterRenderData> biquad;
        RefPtr<AnalyserRenderData> analyser;
        float x1[2] { 0, 0 };
        float x2[2] { 0, 0 };
        float y1[2] { 0, 0 };
        float y2[2] { 0, 0 };
    };

    Vector<OfflineBufferSource> buffer_sources;
    Vector<OfflineOscillator> oscillators;
    Vector<OfflineNodeConnection> node_connections;
    for (auto message : drain_control_messages()) {
        message.visit(
            [&](StartSource const&) { },
            [&](StartOscillator const& start) {
                oscillators.append({
                    .start_time = start.when,
                    .frequency = start.frequency,
                    .detune = start.detune,
                    .frequency_automation = start.frequency_automation,
                    .detune_automation = start.detune_automation,
                    .periodic_wave = start.periodic_wave,
                    .waveform = start.waveform,
                    .node_id = start.node_id,
                });
            },
            [&](StartBufferSource const& start) {
                if (!start.buffer || start.buffer->frame_count() == 0)
                    return;
                buffer_sources.append({
                    .buffer = start.buffer,
                    .start_time = start.when,
                    .offset = start.offset,
                    .duration = start.duration,
                    .playback_rate = start.playback_rate,
                    .detune = start.detune,
                    .playback_rate_automation = start.playback_rate_automation,
                    .detune_automation = start.detune_automation,
                    .loop = start.loop,
                    .loop_start = start.loop_start,
                    .loop_end = start.loop_end,
                    .node_id = start.node_id,
                });
            },
            [&](StopSource const& stop) {
                for (auto& source : buffer_sources) {
                    if (source.node_id == stop.node_id)
                        source.stop_time = stop.when;
                }
                for (auto& oscillator : oscillators) {
                    if (oscillator.node_id == stop.node_id)
                        oscillator.stop_time = stop.when;
                }
            },
            [&](ConnectNode const& connect) {
                node_connections.remove_all_matching([&](auto const& existing) {
                    return existing.source_node_id == connect.source_node_id
                        && existing.destination_node_id == connect.destination_node_id;
                });
                node_connections.append({
                    .source_node_id = connect.source_node_id,
                    .destination_node_id = connect.destination_node_id,
                    .destination_kind = connect.destination_kind,
                    .gain_automation = connect.gain_automation,
                    .biquad = connect.biquad,
                    .analyser = connect.analyser,
                });
            },
            [&](DisconnectNode const& disconnect) {
                node_connections.remove_all_matching([&](auto const& existing) {
                    return existing.source_node_id == disconnect.source_node_id
                        && existing.destination_node_id == disconnect.destination_node_id;
                });
            });
    }

    Vector<GC::Ref<JS::Float32Array>> output_channels;
    output_channels.ensure_capacity(m_number_of_channels);
    for (u32 channel = 0; channel < m_number_of_channels; ++channel)
        output_channels.append(MUST(m_rendered_buffer->get_channel_data(channel)));

    auto const output_rate = static_cast<double>(sample_rate());
    auto const frame_count = m_rendered_buffer->length();
    for (u32 frame = 0; frame < frame_count; ++frame) {
        auto now = frame / output_rate;
        Array<float, BaseAudioContext::MAX_NUMBER_OF_CHANNELS> mixed_samples { };

        auto mix_source = [&](auto&& self, NodeID source_node_id, float sample, Optional<float> right_sample, u32 depth) -> void {
            if (depth > 32)
                return;
            auto right = right_sample.value_or(sample);
            for (auto& connection : node_connections) {
                if (connection.source_node_id != source_node_id)
                    continue;
                if (connection.destination_kind == AudioNodeRenderKind::Destination) {
                    for (u32 channel = 0; channel < m_number_of_channels; ++channel)
                        mixed_samples[channel] += channel == 1 ? right : sample;
                    continue;
                }
                if (connection.destination_kind == AudioNodeRenderKind::Gain) {
                    auto gain = connection.gain_automation ? connection.gain_automation->value_at_time(now) : 1.0f;
                    if (isfinite(gain))
                        self(self, connection.destination_node_id, sample * gain, right * gain, depth + 1);
                    continue;
                }
                if (connection.destination_kind == AudioNodeRenderKind::Biquad && connection.biquad) {
                    auto coefficients = connection.biquad->coefficients_at_time(now, static_cast<float>(output_rate));
                    auto process = [&](float input, u32 channel) {
                        auto output = coefficients.b0 * input + coefficients.b1 * connection.x1[channel] + coefficients.b2 * connection.x2[channel]
                            - coefficients.a1 * connection.y1[channel] - coefficients.a2 * connection.y2[channel];
                        connection.x2[channel] = connection.x1[channel];
                        connection.x1[channel] = input;
                        connection.y2[channel] = connection.y1[channel];
                        connection.y1[channel] = output;
                        return output;
                    };
                    self(self, connection.destination_node_id, process(sample, 0), process(right, 1), depth + 1);
                    continue;
                }
                if (connection.destination_kind == AudioNodeRenderKind::Analyser && connection.analyser) {
                    connection.analyser->push_frame(sample, right);
                    self(self, connection.destination_node_id, sample, right, depth + 1);
                }
            }
        };

        for (auto const& source : buffer_sources) {
            if (now < source.start_time || (source.stop_time.has_value() && now >= source.stop_time.value()))
                continue;
            auto elapsed = now - source.start_time;
            if (source.duration.has_value() && elapsed >= source.duration.value())
                continue;
            auto playback_rate = source.playback_rate_automation ? source.playback_rate_automation->value_at_time(now) : source.playback_rate;
            auto detune = source.detune_automation ? source.detune_automation->value_at_time(now) : source.detune;
            if (!isfinite(playback_rate) || !isfinite(detune))
                continue;
            auto rate = max(static_cast<double>(playback_rate), 0.0) * pow(2.0, static_cast<double>(detune) / 1200.0);
            auto source_frame = source.offset * source.buffer->sample_rate() + elapsed * rate * source.buffer->sample_rate();
            auto source_length = static_cast<double>(source.buffer->frame_count());
            if (source.loop) {
                auto loop_start = clamp(source.loop_start * source.buffer->sample_rate(), 0.0, source_length);
                auto loop_end = source.loop_end > source.loop_start ? clamp(source.loop_end * source.buffer->sample_rate(), loop_start, source_length) : source_length;
                auto loop_length = loop_end - loop_start;
                if (loop_length > 0 && source_frame >= loop_end)
                    source_frame = loop_start + fmod(source_frame - loop_start, loop_length);
            }
            if (source_frame < 0 || source_frame >= source_length)
                continue;
            auto first_frame = static_cast<u32>(source_frame);
            auto next_frame = min(first_frame + 1, source.buffer->frame_count() - 1);
            auto fraction = static_cast<float>(source_frame - first_frame);
            auto sample_at = [&](u32 channel) {
                auto first = source.buffer->sample(channel, first_frame);
                auto next = source.buffer->sample(channel, next_frame);
                return first + (next - first) * fraction;
            };
            if (source.buffer->channel_count() == 1) {
                auto sample = sample_at(0);
                mix_source(mix_source, source.node_id, sample, { }, 0);
            } else {
                mix_source(mix_source, source.node_id, sample_at(0), sample_at(1), 0);
            }
        }

        for (auto const& oscillator : oscillators) {
            if (now < oscillator.start_time || (oscillator.stop_time.has_value() && now >= oscillator.stop_time.value()))
                continue;
            auto frequency_value = oscillator.frequency_automation ? oscillator.frequency_automation->value_at_time(now) : oscillator.frequency;
            auto detune_value = oscillator.detune_automation ? oscillator.detune_automation->value_at_time(now) : oscillator.detune;
            if (!isfinite(frequency_value) || !isfinite(detune_value))
                continue;
            auto frequency = static_cast<double>(frequency_value) * pow(2.0, static_cast<double>(detune_value) / 1200.0);
            auto phase = fmod((now - oscillator.start_time) * frequency, 1.0);
            if (phase < 0)
                phase += 1.0;
            float sample = 0;
            if (oscillator.periodic_wave) {
                sample = oscillator.periodic_wave->sample_at(phase);
            } else {
                switch (oscillator.waveform) {
                case OscillatorWaveform::Sine:
                    sample = static_cast<float>(sin(phase * 2.0 * AK::Pi<double>));
                    break;
                case OscillatorWaveform::Square:
                    sample = phase < 0.5 ? 1.0f : -1.0f;
                    break;
                case OscillatorWaveform::Sawtooth:
                    sample = static_cast<float>(2.0 * phase - 1.0);
                    break;
                case OscillatorWaveform::Triangle:
                    sample = static_cast<float>(1.0 - 4.0 * fabs(phase - 0.5));
                    break;
                }
            }
            mix_source(mix_source, oscillator.node_id, sample, { }, 0);
        }

        for (u32 channel = 0; channel < m_number_of_channels; ++channel)
            output_channels[channel]->data()[frame] = clamp(mixed_samples[channel], -1.0f, 1.0f);
    }
    set_current_time(frame_count / output_rate);

    // 4: Once the rendering is complete, queue a media element task to execute the following steps:
    queue_a_media_element_task(GC::create_function(heap(), [promise, this]() {
        HTML::TemporaryExecutionContext context(this->realm(), HTML::TemporaryExecutionContext::CallbacksEnabled::Yes);

        // 4.1 Resolve the promise created by startRendering() with [[rendered buffer]].
        WebIDL::resolve_promise(this->realm(), promise, this->m_rendered_buffer);

        // AD-HOC: Remove resolved promise from [[pending promises]]
        // https://github.com/WebAudio/web-audio-api/issues/2648
        m_pending_promises.remove_all_matching([promise](GC::Ref<WebIDL::Promise> const& p) {
            return p.ptr() == promise.ptr();
        });

        // 4.2: Queue a media element task to fire an event named complete at the OfflineAudioContext using OfflineAudioCompletionEvent
        //      whose renderedBuffer property is set to [[rendered buffer]].
        queue_a_media_element_task(GC::create_function(heap(), [this]() {
            auto event_init = OfflineAudioCompletionEventInit {
                {
                    .bubbles = false,
                    .cancelable = false,
                    .composed = false,
                },
                this->m_rendered_buffer,
            };
            auto event = MUST(OfflineAudioCompletionEvent::construct_impl(this->realm(), HTML::EventNames::complete, event_init));
            this->dispatch_event(event);
        }));
    }));
}

WebIDL::ExceptionOr<GC::Ref<WebIDL::Promise>> OfflineAudioContext::resume()
{
    auto& realm = this->realm();
    auto promise = WebIDL::create_promise(realm);

    if (!m_rendering_started) {
        return WebIDL::create_rejected_promise_from_exception(realm, WebIDL::InvalidStateError::create(realm, "Offline rendering has not started"_utf16));
    }

    m_pending_promises.append(promise);
    m_pending_suspend_time.clear();
    set_control_state(Bindings::AudioContextState::Running);
    set_rendering_state(Bindings::AudioContextState::Running);
    queue_a_media_element_task(GC::create_function(heap(), [promise, this]() {
        WebIDL::resolve_promise(this->realm(), promise, JS::js_undefined());
        m_pending_promises.remove_first_matching([&promise](auto& pending_promise) {
            return pending_promise == promise;
        });
    }));
    return promise;
}

WebIDL::ExceptionOr<GC::Ref<WebIDL::Promise>> OfflineAudioContext::suspend(double suspend_time)
{
    auto& realm = this->realm();
    auto promise = WebIDL::create_promise(realm);

    if (!isfinite(suspend_time) || suspend_time < current_time()) {
        return WebIDL::create_rejected_promise_from_exception(realm, WebIDL::SimpleException { WebIDL::SimpleExceptionType::RangeError, "suspendTime must be finite and not precede currentTime"sv });
    }
    if (!m_rendering_started) {
        return WebIDL::create_rejected_promise_from_exception(realm, WebIDL::InvalidStateError::create(realm, "Offline rendering has not started"_utf16));
    }
    if (m_pending_suspend_time.has_value()) {
        return WebIDL::create_rejected_promise_from_exception(realm, WebIDL::InvalidStateError::create(realm, "An offline suspend is already pending"_utf16));
    }

    m_pending_promises.append(promise);
    m_pending_suspend_time = suspend_time;
    set_control_state(Bindings::AudioContextState::Suspended);
    set_rendering_state(Bindings::AudioContextState::Suspended);
    queue_a_media_element_task(GC::create_function(heap(), [promise, this]() {
        WebIDL::resolve_promise(this->realm(), promise, JS::js_undefined());
        m_pending_suspend_time.clear();
        m_pending_promises.remove_first_matching([&promise](auto& pending_promise) {
            return pending_promise == promise;
        });
    }));
    return promise;
}

// https://webaudio.github.io/web-audio-api/#dom-offlineaudiocontext-length
WebIDL::UnsignedLong OfflineAudioContext::length() const
{
    // The size of the buffer in sample-frames. This is the same as the value of the length parameter for the constructor.
    return m_length;
}

// https://webaudio.github.io/web-audio-api/#dom-offlineaudiocontext-oncomplete
GC::Ptr<WebIDL::CallbackType> OfflineAudioContext::oncomplete()
{
    return event_handler_attribute(HTML::EventNames::complete);
}

// https://webaudio.github.io/web-audio-api/#dom-offlineaudiocontext-oncomplete
void OfflineAudioContext::set_oncomplete(GC::Ptr<WebIDL::CallbackType> value)
{
    set_event_handler_attribute(HTML::EventNames::complete, value);
}

OfflineAudioContext::OfflineAudioContext(JS::Realm& realm, WebIDL::UnsignedLong number_of_channels, WebIDL::UnsignedLong length, float sample_rate)
    : BaseAudioContext(realm, sample_rate)
    , m_length(length)
    , m_number_of_channels(number_of_channels)
{
}

void OfflineAudioContext::initialize(JS::Realm& realm)
{
    WEB_SET_PROTOTYPE_FOR_INTERFACE(OfflineAudioContext);
    Base::initialize(realm);
}

void OfflineAudioContext::visit_edges(Cell::Visitor& visitor)
{
    Base::visit_edges(visitor);
    visitor.visit(m_rendered_buffer);
}

}
