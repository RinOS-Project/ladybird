/*
 * Copyright (c) 2023, Luke Wilde <lukew@serenityos.org>
 *
 * SPDX-License-Identifier: BSD-2-Clause
 */

#include <AK/Math.h>
#include <LibWeb/Bindings/AudioContextPrototype.h>
#include <LibWeb/Bindings/Intrinsics.h>
#include <LibWeb/DOM/Document.h>
#include <LibWeb/DOM/Event.h>
#include <LibWeb/HTML/HTMLMediaElement.h>
#include <LibWeb/HTML/MessageChannel.h>
#include <LibWeb/HTML/MessagePort.h>
#include <LibWeb/HTML/Scripting/TemporaryExecutionContext.h>
#include <LibWeb/HTML/Window.h>
#include <LibWeb/HighResolutionTime/TimeOrigin.h>
#include <LibWeb/WebAudio/AudioContext.h>
#include <LibWeb/WebAudio/AudioDestinationNode.h>
#include <LibWeb/WebAudio/ControlMessage.h>
#include <LibWeb/WebIDL/Promise.h>
#include <LibMedia/Audio/PlaybackStream.h>
#include <math.h>

namespace Web::WebAudio {

GC_DEFINE_ALLOCATOR(AudioContext);

// https://webaudio.github.io/web-audio-api/#dom-audiocontext-audiocontext
WebIDL::ExceptionOr<GC::Ref<AudioContext>> AudioContext::construct_impl(JS::Realm& realm, Optional<AudioContextOptions> const& context_options)
{
    // If the current settings object’s responsible document is NOT fully active, throw an InvalidStateError and abort these steps.
    auto& settings = HTML::current_principal_settings_object();

    // FIXME: Not all settings objects currently return a responsible document.
    //        Therefore we only fail this check if responsible document is not null.
    if (!settings.responsible_document() || !settings.responsible_document()->is_fully_active()) {
        return WebIDL::InvalidStateError::create(realm, "Document is not fully active"_utf16);
    }

    // AD-HOC: The spec doesn't currently require the sample rate to be validated here,
    //         but other browsers do perform a check and there is a WPT test that expects this.
    if (context_options.has_value() && context_options->sample_rate.has_value())
        TRY(verify_audio_options_inside_nominal_range(realm, *context_options->sample_rate));

    // 1. Let context be a new AudioContext object.
    auto context = realm.create<AudioContext>(realm);
    context->m_destination = TRY(AudioDestinationNode::construct_impl(realm, context));

    // 2. Set a [[control thread state]] to suspended on context.
    context->set_control_state(Bindings::AudioContextState::Suspended);

    // 3. Set a [[rendering thread state]] to suspended on context.
    context->set_rendering_state(Bindings::AudioContextState::Suspended);

    // FIXME: 4. Let messageChannel be a new MessageChannel.
    // FIXME: 5. Let controlSidePort be the value of messageChannel’s port1 attribute.
    // FIXME: 6. Let renderingSidePort be the value of messageChannel’s port2 attribute.
    // FIXME: 7. Let serializedRenderingSidePort be the result of StructuredSerializeWithTransfer(renderingSidePort, « renderingSidePort »).
    // FIXME: 8. Set this audioWorklet's port to controlSidePort.
    // FIXME: 9. Queue a control message to set the MessagePort on the AudioContextGlobalScope, with serializedRenderingSidePort.

    // 10. If contextOptions is given, apply the options:
    if (context_options.has_value()) {
        // 1. If sinkId is specified, let sinkId be the value of contextOptions.sinkId and run the following substeps:

        // 2. Set the internal latency of context according to contextOptions.latencyHint, as described in latencyHint.
        context_options->latency_hint.visit(
            [&](Bindings::AudioContextLatencyCategory category) {
                switch (category) {
                case Bindings::AudioContextLatencyCategory::Balanced:
                    // FIXME: Determine optimal settings for balanced.
                    break;
                case Bindings::AudioContextLatencyCategory::Interactive:
                    // FIXME: Determine optimal settings for interactive.
                    break;
                case Bindings::AudioContextLatencyCategory::Playback:
                    // FIXME: Determine optimal settings for playback.
                    break;
                default:
                    VERIFY_NOT_REACHED();
                }
            },
            [&](double latency_seconds) {
                // FIXME: Determine optimal settings for numeric latency hint.
                (void)latency_seconds;
            });

        // 3: If contextOptions.sampleRate is specified, set the sampleRate of context to this value.
        if (context_options->sample_rate.has_value()) {
            context->set_sample_rate(context_options->sample_rate.value());
        }
        // Otherwise, follow these substeps:
        else {
            // FIXME: 1. If sinkId is the empty string or a type of AudioSinkOptions, use the sample rate of the default output device. Abort these substeps.
            // FIXME: 2. If sinkId is a DOMString, use the sample rate of the output device identified by sinkId. Abort these substeps.
            // If contextOptions.sampleRate differs from the sample rate of the output device, the user agent MUST resample the audio output to match the sample rate of the output device.
            context->set_sample_rate(44100);
        }
    }

    // FIXME: 11. If context is allowed to start, send a control message to start processing.
    // FIXME: Implement control message queue to run following steps on the rendering thread
    if (context->m_allowed_to_start) {
        // FIXME: 1. Let document be the current settings object's relevant global object's associated Document.
        // FIXME: 2. Attempt to acquire system resources to use a following audio output device based on [[sink ID]] for rendering

        // 2. Set this [[rendering thread state]] to running on the AudioContext.
        context->set_rendering_state(Bindings::AudioContextState::Running);

        // 3. Queue a media element task to execute the following steps:
        context->queue_a_media_element_task(GC::create_function(context->heap(), [context]() {
            // 1. Set the state attribute of the AudioContext to "running".
            context->set_control_state(Bindings::AudioContextState::Running);

            // 2. Fire an event named statechange at the AudioContext.
            context->dispatch_event(DOM::Event::create(context->realm(), HTML::EventNames::statechange));
        }));
    }

    // 12. Return context.
    return context;
}

AudioContext::~AudioContext()
{
    if (m_source_ended_timer)
        m_source_ended_timer->stop();
}

void AudioContext::initialize(JS::Realm& realm)
{
    WEB_SET_PROTOTYPE_FOR_INTERFACE(AudioContext);
    Base::initialize(realm);
}

void AudioContext::visit_edges(Cell::Visitor& visitor)
{
    Base::visit_edges(visitor);
    visitor.visit(m_pending_resume_promises);
}

// https://www.w3.org/TR/webaudio/#dom-audiocontext-getoutputtimestamp
AudioTimestamp AudioContext::get_output_timestamp()
{
    // The rendering backend currently advances the context clock directly. Use
    // the same monotonic clock exposed to the owning global for the
    // corresponding performance timestamp; this keeps both values finite and
    // prevents an unbound zero timestamp from masquerading as output progress.
    return {
        .context_time = current_time(),
        .performance_time = HighResolutionTime::current_high_resolution_time(realm().global_object()),
    };
}

// https://www.w3.org/TR/webaudio/#dom-audiocontext-resume
WebIDL::ExceptionOr<GC::Ref<WebIDL::Promise>> AudioContext::resume()
{
    auto& realm = this->realm();

    // 1. If this's relevant global object's associated Document is not fully active then return a promise rejected with "InvalidStateError" DOMException.
    auto const& associated_document = as<HTML::Window>(HTML::relevant_global_object(*this)).associated_document();
    if (!associated_document.is_fully_active())
        return WebIDL::InvalidStateError::create(realm, "Document is not fully active"_utf16);

    // 2. Let promise be a new Promise.
    auto promise = WebIDL::create_promise(realm);

    // 3. If the [[control thread state]] on the AudioContext is closed reject the promise with InvalidStateError, abort these steps, returning promise.
    if (state() == Bindings::AudioContextState::Closed) {
        WebIDL::reject_promise(realm, promise, WebIDL::InvalidStateError::create(realm, "Audio context is already closed."_utf16));
        return promise;
    }

    // 4. Set [[suspended by user]] to false.
    m_suspended_by_user = false;

    // 5. If the context is not allowed to start, append promise to [[pending promises]] and [[pending resume promises]] and abort these steps, returning promise.
    if (!m_allowed_to_start) {
        m_pending_promises.append(promise);
        m_pending_resume_promises.append(promise);
        return promise;
    }

    // 6. Set the [[control thread state]] on the AudioContext to running.
    set_control_state(Bindings::AudioContextState::Running);

    // 7. Queue a control message to resume the AudioContext.
    // FIXME: Implement control message queue to run following steps on the rendering thread
    // FIXME: 7.1: Attempt to acquire system resources.

    // 7.2: Set the [[rendering thread state]] on the AudioContext to running.
    set_rendering_state(Bindings::AudioContextState::Running);

    if (m_playback_stream)
        (void)m_playback_stream->resume();
    start_source_ended_timer();

    // 7.3: Start rendering the audio graph.
    auto playback_stream_ready = m_playback_stream != nullptr;
    if (!start_rendering_audio_graph()) {
        // 7.4: In case of failure, queue a media element task to execute the following steps:
        queue_a_media_element_task(GC::create_function(heap(), [this]() {
            auto& realm = this->realm();
            HTML::TemporaryExecutionContext context(realm, HTML::TemporaryExecutionContext::CallbacksEnabled::Yes);

            // 7.4.1: Reject all promises from [[pending resume promises]] in order, then clear [[pending resume promises]].
            for (auto const& promise : m_pending_resume_promises) {
                WebIDL::reject_promise(realm, promise, WebIDL::InvalidStateError::create(realm, "Unable to start the audio rendering backend."_utf16));

                // 7.4.2: Additionally, remove those promises from [[pending promises]].
                m_pending_promises.remove_first_matching([&promise](auto& pending_promise) {
                    return pending_promise == promise;
                });
            }
            m_pending_resume_promises.clear();
        }));
    }

    // PlaybackStream creation is asynchronous. Keep the resume promise
    // pending until the real output endpoint is ready instead of resolving it
    // while only a policy flag has changed.
    if (!playback_stream_ready && m_backend_start_pending) {
        m_pending_promises.append(promise);
        m_pending_resume_promises.append(promise);
        return promise;
    }

    // 7.5: queue a media element task to execute the following steps:
    queue_a_media_element_task(GC::create_function(heap(), [promise, this]() {
        auto& realm = this->realm();
        HTML::TemporaryExecutionContext context(realm, HTML::TemporaryExecutionContext::CallbacksEnabled::Yes);

        // 7.5.1: Resolve all promises from [[pending resume promises]] in order.
        // 7.5.2: Clear [[pending resume promises]]. Additionally, remove those promises from
        //        [[pending promises]].
        for (auto const& pending_resume_promise : m_pending_resume_promises) {
            WebIDL::resolve_promise(realm, pending_resume_promise, JS::js_undefined());
            m_pending_promises.remove_first_matching([&pending_resume_promise](auto& pending_promise) {
                return pending_promise == pending_resume_promise;
            });
        }
        m_pending_resume_promises.clear();

        // 7.5.3: Resolve promise.
        WebIDL::resolve_promise(realm, promise, JS::js_undefined());

        // 7.5.4: If the state attribute of the AudioContext is not already "running":
        if (state() != Bindings::AudioContextState::Running) {
            // 7.5.4.1: Set the state attribute of the AudioContext to "running".
            set_control_state(Bindings::AudioContextState::Running);

            // 7.5.4.2: queue a media element task to fire an event named statechange at the AudioContext.
            queue_a_media_element_task(GC::create_function(heap(), [this]() {
                this->dispatch_event(DOM::Event::create(this->realm(), HTML::EventNames::statechange));
            }));
        }
    }));

    // 8. Return promise.
    return promise;
}

// https://www.w3.org/TR/webaudio/#dom-audiocontext-suspend
WebIDL::ExceptionOr<GC::Ref<WebIDL::Promise>> AudioContext::suspend()
{
    auto& realm = this->realm();

    // 1. If this's relevant global object's associated Document is not fully active then return a promise rejected with "InvalidStateError" DOMException.
    auto const& associated_document = as<HTML::Window>(HTML::relevant_global_object(*this)).associated_document();
    if (!associated_document.is_fully_active())
        return WebIDL::InvalidStateError::create(realm, "Document is not fully active"_utf16);

    // 2. Let promise be a new Promise.
    auto promise = WebIDL::create_promise(realm);

    // 3. If the [[control thread state]] on the AudioContext is closed reject the promise with InvalidStateError, abort these steps, returning promise.
    if (state() == Bindings::AudioContextState::Closed) {
        WebIDL::reject_promise(realm, promise, WebIDL::InvalidStateError::create(realm, "Audio context is already closed."_utf16));
        return promise;
    }

    // 4. Append promise to [[pending promises]].
    m_pending_promises.append(promise);

    // 5. Set [[suspended by user]] to true.
    m_suspended_by_user = true;

    // 6. Set the [[control thread state]] on the AudioContext to suspended.
    set_control_state(Bindings::AudioContextState::Suspended);

    // 7. Queue a control message to suspend the AudioContext.
    // FIXME: Implement control message queue to run following steps on the rendering thread
    // FIXME: 7.1: Attempt to release system resources.

    // 7.2: Set the [[rendering thread state]] on the AudioContext to suspended.
    set_rendering_state(Bindings::AudioContextState::Suspended);

    if (m_playback_stream)
        (void)m_playback_stream->drain_buffer_and_suspend();
    stop_source_ended_timer();

    // 7.3: queue a media element task to execute the following steps:
    queue_a_media_element_task(GC::create_function(heap(), [promise, this]() {
        auto& realm = this->realm();
        HTML::TemporaryExecutionContext context(realm, HTML::TemporaryExecutionContext::CallbacksEnabled::Yes);

        // 7.3.1: Resolve promise.
        WebIDL::resolve_promise(realm, promise, JS::js_undefined());

        // 7.3.2: If the state attribute of the AudioContext is not already "suspended":
        if (state() != Bindings::AudioContextState::Suspended) {
            // 7.3.2.1: Set the state attribute of the AudioContext to "suspended".
            set_control_state(Bindings::AudioContextState::Suspended);

            // 7.3.2.2: queue a media element task to fire an event named statechange at the AudioContext.
            queue_a_media_element_task(GC::create_function(heap(), [this]() {
                this->dispatch_event(DOM::Event::create(this->realm(), HTML::EventNames::statechange));
            }));
        }
    }));

    // 8. Return promise.
    return promise;
}

// https://www.w3.org/TR/webaudio/#dom-audiocontext-close
WebIDL::ExceptionOr<GC::Ref<WebIDL::Promise>> AudioContext::close()
{
    auto& realm = this->realm();

    // 1. If this's relevant global object's associated Document is not fully active then return a promise rejected with "InvalidStateError" DOMException.
    auto const& associated_document = as<HTML::Window>(HTML::relevant_global_object(*this)).associated_document();
    if (!associated_document.is_fully_active())
        return WebIDL::InvalidStateError::create(realm, "Document is not fully active"_utf16);

    // 2. Let promise be a new Promise.
    auto promise = WebIDL::create_promise(realm);

    // 3. If the [[control thread state]] flag on the AudioContext is closed reject the promise with InvalidStateError, abort these steps, returning promise.
    if (state() == Bindings::AudioContextState::Closed) {
        WebIDL::reject_promise(realm, promise, WebIDL::InvalidStateError::create(realm, "Audio context is already closed."_utf16));
        return promise;
    }

    // 4. Set the [[control thread state]] flag on the AudioContext to closed.
    set_control_state(Bindings::AudioContextState::Closed);

    // 5. Queue a control message to close the AudioContext.
    // FIXME: Implement control message queue to run following steps on the rendering thread
    // FIXME: 5.1: Attempt to release system resources.

    // 5.2: Set the [[rendering thread state]] to "suspended".
    set_rendering_state(Bindings::AudioContextState::Suspended);

    if (m_playback_stream) {
        (void)m_playback_stream->discard_buffer_and_suspend();
        m_playback_stream = nullptr;
    }
    stop_source_ended_timer();

    // FIXME: 5.3: If this control message is being run in a reaction to the document being unloaded, abort this algorithm.

    // 5.4: queue a media element task to execute the following steps:
    queue_a_media_element_task(GC::create_function(heap(), [promise, this]() {
        auto& realm = this->realm();
        HTML::TemporaryExecutionContext context(realm, HTML::TemporaryExecutionContext::CallbacksEnabled::Yes);

        // 5.4.1: Resolve promise.
        WebIDL::resolve_promise(realm, promise, JS::js_undefined());

        // 5.4.2: If the state attribute of the AudioContext is not already "closed":
        if (state() != Bindings::AudioContextState::Closed) {
            // 5.4.2.1: Set the state attribute of the AudioContext to "closed".
            set_control_state(Bindings::AudioContextState::Closed);
        }

        // 5.4.2.2: queue a media element task to fire an event named statechange at the AudioContext.
        // FIXME: Attempting to queue another task in here causes an assertion fail at Vector.h:148
        this->dispatch_event(DOM::Event::create(realm, HTML::EventNames::statechange));
    }));

    // 6. Return promise
    return promise;
}

bool AudioContext::start_rendering_audio_graph()
{
    if (m_playback_stream) {
        (void)m_playback_stream->resume();
        start_source_ended_timer();
        return true;
    }

    if (m_backend_start_pending)
        return true;

    m_backend_start_pending = true;
    GC::Ref<AudioContext> self = *this;
    auto data_callback = [self](Span<float> buffer) -> ReadonlySpan<float> {
        // Render-side state is published through the control-message queue;
        // the callback never reaches into JavaScript-owned AudioBuffer data.
        self->render_audio(buffer);
        return buffer;
    };
    auto create_promise = Audio::PlaybackStream::create(Audio::OutputState::Suspended, 100, move(data_callback));
    create_promise->when_resolved([self](auto& stream) {
        self->m_backend_start_pending = false;
        self->m_playback_stream = stream;
        self->m_output_sample_rate = stream->sample_specification().sample_rate();
        self->start_source_ended_timer();
        if (self->state() == Bindings::AudioContextState::Running)
            (void)self->m_playback_stream->resume();
        if (!self->m_pending_resume_promises.is_empty()) {
            self->queue_a_media_element_task(GC::create_function(self->heap(), [self]() {
                auto& realm = self->realm();
                HTML::TemporaryExecutionContext context(realm, HTML::TemporaryExecutionContext::CallbacksEnabled::Yes);
                for (auto const& promise : self->m_pending_resume_promises) {
                    WebIDL::resolve_promise(realm, promise, JS::js_undefined());
                    self->m_pending_promises.remove_first_matching([&promise](auto const& pending_promise) {
                        return pending_promise == promise;
                    });
                }
                self->m_pending_resume_promises.clear();
            }));
        }
    });
    create_promise->when_rejected([self](auto&) {
        self->m_backend_start_pending = false;
        if (self->state() != Bindings::AudioContextState::Closed) {
            self->set_control_state(Bindings::AudioContextState::Suspended);
            self->set_rendering_state(Bindings::AudioContextState::Suspended);
        }
        self->queue_a_media_element_task(GC::create_function(self->heap(), [self]() {
            auto& realm = self->realm();
            HTML::TemporaryExecutionContext context(realm, HTML::TemporaryExecutionContext::CallbacksEnabled::Yes);
            for (auto const& promise : self->m_pending_resume_promises) {
                WebIDL::reject_promise(realm, promise, WebIDL::InvalidStateError::create(realm, "Unable to start the audio rendering backend."_utf16));
                self->m_pending_promises.remove_first_matching([&promise](auto const& pending_promise) {
                    return pending_promise == promise;
                });
            }
            self->m_pending_resume_promises.clear();
        }));
    });
    return true;
}

void AudioContext::start_source_ended_timer()
{
    if (!m_source_ended_timer) {
        auto weak_context = GC::Weak { *this };
        m_source_ended_timer = Core::Timer::create_repeating(10, [weak_context]() mutable {
            if (auto context = weak_context.ptr())
                context->dispatch_source_ended_events();
        });
    }
    if (!m_source_ended_timer->is_active())
        m_source_ended_timer->start();
}

void AudioContext::stop_source_ended_timer()
{
    if (m_source_ended_timer)
        m_source_ended_timer->stop();
    dispatch_source_ended_events();
}

void AudioContext::render_audio(Span<float> buffer)
{
    // PlaybackStream currently exposes interleaved stereo samples to WebAudio.
    // Keep the callback bounded even if a platform backend supplies a partial
    // frame or a future backend changes its channel count.
    auto frame_count = buffer.size() / 2;
    buffer = buffer.slice(0, frame_count * 2);
    buffer.fill(0.0f);

    // Control messages are the only cross-thread publication point. Once a
    // message is drained, the immutable AudioBufferRenderData can safely be
    // read without entering the JavaScript heap from this callback.
    for (auto message : drain_control_messages()) {
        message.visit(
            [&](StartSource const&) {
                // Oscillator and other scheduled sources are not represented by
                // an immutable native snapshot; unknown scheduled sources stay
                // silent until their renderer is connected.
            },
            [&](StartOscillator const& start) {
                m_active_oscillators.append({
                    .node_id = start.node_id,
                    .start_time = start.when,
                    .frequency = start.frequency,
                    .detune = start.detune,
                    .frequency_automation = start.frequency_automation,
                    .detune_automation = start.detune_automation,
                    .frequency_param_id = start.frequency_param_id,
                    .detune_param_id = start.detune_param_id,
                    .periodic_wave = start.periodic_wave,
                    .waveform = start.waveform,
                });
            },
            [&](StartConstantSource const& start) {
                m_active_constant_sources.append({
                    .node_id = start.node_id,
                    .start_time = start.when,
                    .offset = start.offset,
                    .offset_automation = start.offset_automation,
                    .offset_param_id = start.offset_param_id,
                });
            },
            [&](UpdateOscillatorWaveform const& update) {
                for (auto& oscillator : m_active_oscillators) {
                    if (oscillator.node_id != update.node_id)
                        continue;
                    oscillator.periodic_wave = update.periodic_wave;
                    oscillator.waveform = update.waveform;
                }
            },
            [&](StartBufferSource const& start) {
                m_active_audio_sources.append({
                    .node_id = start.node_id,
                    .buffer = start.buffer,
                    .start_time = start.when,
                    .offset = start.offset,
                    .duration = start.duration,
                    .playback_rate = start.playback_rate,
                    .detune = start.detune,
                    .playback_rate_automation = start.playback_rate_automation,
                    .detune_automation = start.detune_automation,
                    .playback_rate_param_id = start.playback_rate_param_id,
                    .detune_param_id = start.detune_param_id,
                    .loop = start.loop,
                    .loop_start = start.loop_start,
                    .loop_end = start.loop_end,
                });
            },
            [&](StopSource const& stop) {
                for (auto& source : m_active_audio_sources) {
                    if (source.node_id == stop.node_id)
                        source.stop_time = stop.when;
                }
                for (auto& oscillator : m_active_oscillators) {
                    if (oscillator.node_id == stop.node_id)
                        oscillator.stop_time = stop.when;
                }
                for (auto& source : m_active_constant_sources) {
                    if (source.node_id == stop.node_id)
                        source.stop_time = stop.when;
                }
            },
            [&](ConnectNode const& connect) {
                m_node_connections.remove_all_matching([&](auto const& existing) {
                    return existing.source_node_id == connect.source_node_id
                        && existing.destination_node_id == connect.destination_node_id
                        && existing.output_index == connect.output_index
                        && existing.input_index == connect.input_index;
                });
                m_node_connections.append({
                    .source_node_id = connect.source_node_id,
                    .destination_node_id = connect.destination_node_id,
                    .source_kind = connect.source_kind,
                    .destination_kind = connect.destination_kind,
                    .output_index = connect.output_index,
                    .input_index = connect.input_index,
                    .gain_automation = connect.gain_automation,
                    .gain_param_id = connect.gain_param_id,
                    .biquad = connect.biquad,
                    .biquad_frequency_param_id = connect.biquad_frequency_param_id,
                    .biquad_detune_param_id = connect.biquad_detune_param_id,
                    .biquad_q_param_id = connect.biquad_q_param_id,
                    .biquad_gain_param_id = connect.biquad_gain_param_id,
                    .analyser = connect.analyser,
                    .stereo_panner_automation = connect.stereo_panner_automation,
                    .stereo_panner_param_id = connect.stereo_panner_param_id,
                    .delay = connect.delay,
                    .delay_param_id = connect.delay_param_id,
                    .panner = connect.panner,
                    .panner_position_x_param_id = connect.panner_position_x_param_id,
                    .panner_position_y_param_id = connect.panner_position_y_param_id,
                    .panner_position_z_param_id = connect.panner_position_z_param_id,
                    .panner_orientation_x_param_id = connect.panner_orientation_x_param_id,
                    .panner_orientation_y_param_id = connect.panner_orientation_y_param_id,
                    .panner_orientation_z_param_id = connect.panner_orientation_z_param_id,
                    .listener_position_x_param_id = connect.listener_position_x_param_id,
                    .listener_position_y_param_id = connect.listener_position_y_param_id,
                    .listener_position_z_param_id = connect.listener_position_z_param_id,
                    .listener_forward_x_param_id = connect.listener_forward_x_param_id,
                    .listener_forward_y_param_id = connect.listener_forward_y_param_id,
                    .listener_forward_z_param_id = connect.listener_forward_z_param_id,
                    .listener_up_x_param_id = connect.listener_up_x_param_id,
                    .listener_up_y_param_id = connect.listener_up_y_param_id,
                    .listener_up_z_param_id = connect.listener_up_z_param_id,
                    .compressor = connect.compressor,
                    .compressor_threshold_param_id = connect.compressor_threshold_param_id,
                    .compressor_knee_param_id = connect.compressor_knee_param_id,
                    .compressor_ratio_param_id = connect.compressor_ratio_param_id,
                    .compressor_attack_param_id = connect.compressor_attack_param_id,
                    .compressor_release_param_id = connect.compressor_release_param_id,
                });
            },
            [&](DisconnectNode const& disconnect) {
                m_node_connections.remove_all_matching([&](auto const& existing) {
                    return existing.source_node_id == disconnect.source_node_id
                        && existing.destination_node_id == disconnect.destination_node_id;
                });
            },
            [&](ConnectParam const& connect) {
                m_param_connections.remove_all_matching([&](auto const& existing) {
                    return existing.source_node_id == connect.source_node_id
                        && existing.destination_param_id == connect.destination_param_id
                        && existing.output_index == connect.output_index;
                });
                m_param_connections.append({
                    .source_node_id = connect.source_node_id,
                    .destination_param_id = connect.destination_param_id,
                    .output_index = connect.output_index,
                });
            },
            [&](DisconnectParam const& disconnect) {
                m_param_connections.remove_all_matching([&](auto const& existing) {
                    return existing.source_node_id == disconnect.source_node_id
                        && existing.destination_param_id == disconnect.destination_param_id
                        && existing.output_index == disconnect.output_index;
                });
            },
            [&](UpdateAudioParam const& update) {
                for (auto& source : m_active_audio_sources) {
                    if (source.playback_rate_param_id == update.param_id)
                        source.playback_rate_automation = update.render_data;
                    if (source.detune_param_id == update.param_id)
                        source.detune_automation = update.render_data;
                }
                for (auto& oscillator : m_active_oscillators) {
                    if (oscillator.frequency_param_id == update.param_id)
                        oscillator.frequency_automation = update.render_data;
                    if (oscillator.detune_param_id == update.param_id)
                        oscillator.detune_automation = update.render_data;
                }
                for (auto& source : m_active_constant_sources) {
                    if (source.offset_param_id == update.param_id)
                        source.offset_automation = update.render_data;
                }
                for (auto& connection : m_node_connections) {
                    if (connection.gain_param_id == update.param_id)
                        connection.gain_automation = update.render_data;
                    if (connection.biquad) {
                        if (connection.biquad_frequency_param_id == update.param_id)
                            connection.biquad->update_frequency_automation(update.render_data);
                        if (connection.biquad_detune_param_id == update.param_id)
                            connection.biquad->update_detune_automation(update.render_data);
                        if (connection.biquad_q_param_id == update.param_id)
                            connection.biquad->update_q_automation(update.render_data);
                        if (connection.biquad_gain_param_id == update.param_id)
                            connection.biquad->update_gain_automation(update.render_data);
                    }
                    if (connection.stereo_panner_param_id == update.param_id)
                        connection.stereo_panner_automation = update.render_data;
                    if (connection.delay && connection.delay_param_id == update.param_id)
                        connection.delay->update_automation(update.render_data);
                    if (connection.panner) {
                        if (connection.panner_position_x_param_id == update.param_id)
                            connection.panner->update_position_x(update.render_data);
                        if (connection.panner_position_y_param_id == update.param_id)
                            connection.panner->update_position_y(update.render_data);
                        if (connection.panner_position_z_param_id == update.param_id)
                            connection.panner->update_position_z(update.render_data);
                        if (connection.panner_orientation_x_param_id == update.param_id)
                            connection.panner->update_orientation_x(update.render_data);
                        if (connection.panner_orientation_y_param_id == update.param_id)
                            connection.panner->update_orientation_y(update.render_data);
                        if (connection.panner_orientation_z_param_id == update.param_id)
                            connection.panner->update_orientation_z(update.render_data);
                        if (connection.listener_position_x_param_id == update.param_id)
                            connection.panner->update_listener_position_x(update.render_data);
                        if (connection.listener_position_y_param_id == update.param_id)
                            connection.panner->update_listener_position_y(update.render_data);
                        if (connection.listener_position_z_param_id == update.param_id)
                            connection.panner->update_listener_position_z(update.render_data);
                        if (connection.listener_forward_x_param_id == update.param_id)
                            connection.panner->update_listener_forward_x(update.render_data);
                        if (connection.listener_forward_y_param_id == update.param_id)
                            connection.panner->update_listener_forward_y(update.render_data);
                        if (connection.listener_forward_z_param_id == update.param_id)
                            connection.panner->update_listener_forward_z(update.render_data);
                        if (connection.listener_up_x_param_id == update.param_id)
                            connection.panner->update_listener_up_x(update.render_data);
                        if (connection.listener_up_y_param_id == update.param_id)
                            connection.panner->update_listener_up_y(update.render_data);
                        if (connection.listener_up_z_param_id == update.param_id)
                            connection.panner->update_listener_up_z(update.render_data);
                    }
                    if (connection.compressor) {
                        if (connection.compressor_threshold_param_id == update.param_id)
                            connection.compressor->update_threshold_automation(update.render_data);
                        if (connection.compressor_knee_param_id == update.param_id)
                            connection.compressor->update_knee_automation(update.render_data);
                        if (connection.compressor_ratio_param_id == update.param_id)
                            connection.compressor->update_ratio_automation(update.render_data);
                        if (connection.compressor_attack_param_id == update.param_id)
                            connection.compressor->update_attack_automation(update.render_data);
                        if (connection.compressor_release_param_id == update.param_id)
                            connection.compressor->update_release_automation(update.render_data);
                    }
                }
            });
    }

    auto output_rate = max(m_output_sample_rate, 1u);
    auto const render_start_frame = m_render_frame_position;
    for (size_t frame = 0; frame < frame_count; ++frame) {
        auto now = static_cast<double>(render_start_frame + frame) / output_rate;
        auto left = 0.0f;
        auto right = 0.0f;
        struct ParamModulation {
            AudioParamID id { 0 };
            float value { 0 };
            bool used { false };
        };
        Array<ParamModulation, 1024> param_modulations { };
        auto add_param_modulation = [&](AudioParamID id, float value) {
            if (id == 0 || !isfinite(value))
                return;
            ParamModulation* free_slot = nullptr;
            for (auto& modulation : param_modulations) {
                if (modulation.used && modulation.id == id) {
                    modulation.value += value;
                    return;
                }
                if (!modulation.used && !free_slot)
                    free_slot = &modulation;
            }
            if (free_slot) {
                free_slot->id = id;
                free_slot->value = value;
                free_slot->used = true;
            }
        };
        auto param_modulation = [&](AudioParamID id) {
            if (id == 0)
                return 0.0f;
            for (auto const& modulation : param_modulations) {
                if (modulation.used && modulation.id == id)
                    return modulation.value;
            }
            return 0.0f;
        };
        struct MergerFrame {
            NodeID node_id { 0 };
            Array<float, BaseAudioContext::MAX_NUMBER_OF_CHANNELS> channels { };
            bool used { false };
            bool emitted { false };
        };
        Array<MergerFrame, 32> merger_frames { };

        auto mix_source = [&](auto&& self, NodeID source_node_id, float source_left, float source_right, u32 depth) -> void {
            if (depth > 32)
                return;
            for (auto const& param_connection : m_param_connections) {
                if (param_connection.source_node_id == source_node_id && param_connection.output_index == 0)
                    add_param_modulation(param_connection.destination_param_id, (source_left + source_right) * 0.5f);
            }
            for (auto& connection : m_node_connections) {
                if (connection.source_node_id != source_node_id)
                    continue;
                auto routed_left = source_left;
                auto routed_right = source_right;
                if (connection.source_kind == AudioNodeRenderKind::ChannelSplitter) {
                    auto split_sample = connection.output_index == 0 ? source_left : connection.output_index == 1 ? source_right : 0.0f;
                    routed_left = split_sample;
                    routed_right = split_sample;
                }
                if (connection.destination_kind == AudioNodeRenderKind::ChannelMerger) {
                    MergerFrame* merger = nullptr;
                    for (auto& candidate : merger_frames) {
                        if (candidate.used && candidate.node_id == connection.destination_node_id) {
                            merger = &candidate;
                            break;
                        }
                        if (!candidate.used && !merger)
                            merger = &candidate;
                    }
                    if (merger && connection.input_index < BaseAudioContext::MAX_NUMBER_OF_CHANNELS) {
                        merger->node_id = connection.destination_node_id;
                        merger->used = true;
                        merger->channels[connection.input_index] += routed_left;
                    }
                    continue;
                }
                if (connection.destination_kind == AudioNodeRenderKind::ChannelSplitter) {
                    self(self, connection.destination_node_id, routed_left, routed_right, depth + 1);
                    continue;
                }
                if (connection.destination_kind == AudioNodeRenderKind::Destination) {
                    left += routed_left;
                    right += routed_right;
                    continue;
                }
                if (connection.destination_kind == AudioNodeRenderKind::Gain) {
                    auto gain = (connection.gain_automation ? connection.gain_automation->value_at_time(now) : 1.0f) + param_modulation(connection.gain_param_id);
                    if (isfinite(gain))
                        self(self, connection.destination_node_id, routed_left * gain, routed_right * gain, depth + 1);
                    continue;
                }
                if (connection.destination_kind == AudioNodeRenderKind::Biquad && connection.biquad) {
                    auto coefficients = connection.biquad->coefficients_at_time(now, static_cast<float>(output_rate),
                        param_modulation(connection.biquad_frequency_param_id), param_modulation(connection.biquad_detune_param_id),
                        param_modulation(connection.biquad_q_param_id), param_modulation(connection.biquad_gain_param_id));
                    auto process = [&](float input, u32 channel) {
                        auto output = coefficients.b0 * input + coefficients.b1 * connection.x1[channel] + coefficients.b2 * connection.x2[channel]
                            - coefficients.a1 * connection.y1[channel] - coefficients.a2 * connection.y2[channel];
                        connection.x2[channel] = connection.x1[channel];
                        connection.x1[channel] = input;
                        connection.y2[channel] = connection.y1[channel];
                        connection.y1[channel] = output;
                        return output;
                    };
                    self(self, connection.destination_node_id, process(routed_left, 0), process(routed_right, 1), depth + 1);
                    continue;
                }
                if (connection.destination_kind == AudioNodeRenderKind::Analyser && connection.analyser) {
                    connection.analyser->push_frame(routed_left, routed_right);
                    self(self, connection.destination_node_id, routed_left, routed_right, depth + 1);
                    continue;
                }
                if (connection.destination_kind == AudioNodeRenderKind::StereoPanner) {
                    auto pan = (connection.stereo_panner_automation ? connection.stereo_panner_automation->value_at_time(now) : 0.0f) + param_modulation(connection.stereo_panner_param_id);
                    if (!isfinite(pan))
                        continue;
                    pan = clamp(pan, -1.0f, 1.0f);
                    auto angle = (static_cast<double>(pan) + 1.0) * AK::Pi<double> / 4.0;
                    auto mono = (routed_left + routed_right) * 0.5f;
                    self(self, connection.destination_node_id, mono * static_cast<float>(cos(angle)), mono * static_cast<float>(sin(angle)), depth + 1);
                    continue;
                }
                if (connection.destination_kind == AudioNodeRenderKind::Delay && connection.delay) {
                    connection.delay->process(routed_left, routed_right, now, param_modulation(connection.delay_param_id));
                    self(self, connection.destination_node_id, routed_left, routed_right, depth + 1);
                    continue;
                }
                if (connection.destination_kind == AudioNodeRenderKind::Panner && connection.panner) {
                    connection.panner->process(routed_left, routed_right, now,
                        param_modulation(connection.panner_position_x_param_id), param_modulation(connection.panner_position_y_param_id), param_modulation(connection.panner_position_z_param_id),
                        param_modulation(connection.panner_orientation_x_param_id), param_modulation(connection.panner_orientation_y_param_id), param_modulation(connection.panner_orientation_z_param_id),
                        param_modulation(connection.listener_position_x_param_id), param_modulation(connection.listener_position_y_param_id), param_modulation(connection.listener_position_z_param_id),
                        param_modulation(connection.listener_forward_x_param_id), param_modulation(connection.listener_forward_y_param_id), param_modulation(connection.listener_forward_z_param_id),
                        param_modulation(connection.listener_up_x_param_id), param_modulation(connection.listener_up_y_param_id), param_modulation(connection.listener_up_z_param_id));
                    self(self, connection.destination_node_id, routed_left, routed_right, depth + 1);
                    continue;
                }
                if (connection.destination_kind == AudioNodeRenderKind::DynamicsCompressor && connection.compressor) {
                    connection.compressor->process_stereo(routed_left, routed_right, now, static_cast<float>(output_rate),
                        param_modulation(connection.compressor_threshold_param_id), param_modulation(connection.compressor_knee_param_id),
                        param_modulation(connection.compressor_ratio_param_id), param_modulation(connection.compressor_attack_param_id),
                        param_modulation(connection.compressor_release_param_id));
                    self(self, connection.destination_node_id, routed_left, routed_right, depth + 1);
                }
            }
        };

        for (auto& source : m_active_audio_sources) {
            if (now < source.start_time)
                continue;
            if (source.stop_time.has_value() && now >= source.stop_time.value()) {
                if (!source.ended_reported)
                    source.ended_reported = queue_source_ended(source.node_id);
                continue;
            }
            auto elapsed = now - source.start_time;
            if (source.duration.has_value() && elapsed >= source.duration.value()) {
                if (!source.ended_reported)
                    source.ended_reported = queue_source_ended(source.node_id);
                continue;
            }
            if (!source.buffer || source.buffer->frame_count() == 0) {
                if (!source.ended_reported)
                    source.ended_reported = queue_source_ended(source.node_id);
                continue;
            }

            auto playback_rate = (source.playback_rate_automation ? source.playback_rate_automation->value_at_time(now) : source.playback_rate) + param_modulation(source.playback_rate_param_id);
            auto detune = (source.detune_automation ? source.detune_automation->value_at_time(now) : source.detune) + param_modulation(source.detune_param_id);
            if (!isfinite(playback_rate) || !isfinite(detune))
                continue;
            auto rate = max(static_cast<double>(playback_rate), 0.0) * pow(2.0, static_cast<double>(detune) / 1200.0);
            auto source_frame = source.offset * source.buffer->sample_rate() + elapsed * rate * source.buffer->sample_rate();
            auto source_length = static_cast<double>(source.buffer->frame_count());
            if (source.loop) {
                auto loop_start = clamp(source.loop_start * source.buffer->sample_rate(), 0.0, source_length);
                auto loop_end = source.loop_end > source.loop_start
                    ? clamp(source.loop_end * source.buffer->sample_rate(), loop_start, source_length)
                    : source_length;
                auto loop_length = loop_end - loop_start;
                if (loop_length > 0 && source_frame >= loop_end)
                    source_frame = loop_start + fmod(source_frame - loop_start, loop_length);
            }
            if (source_frame < 0 || source_frame >= source_length) {
                if (!source.loop && !source.ended_reported)
                    source.ended_reported = queue_source_ended(source.node_id);
                continue;
            }

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
                mix_source(mix_source, source.node_id, sample, sample, 0);
            } else {
                mix_source(mix_source, source.node_id, sample_at(0), sample_at(1), 0);
            }
        }

        for (auto& oscillator : m_active_oscillators) {
            if (now < oscillator.start_time)
                continue;
            if (oscillator.stop_time.has_value() && now >= oscillator.stop_time.value()) {
                if (!oscillator.ended_reported)
                    oscillator.ended_reported = queue_source_ended(oscillator.node_id);
                continue;
            }

            auto frequency_value = (oscillator.frequency_automation ? oscillator.frequency_automation->value_at_time(now) : oscillator.frequency) + param_modulation(oscillator.frequency_param_id);
            auto detune_value = (oscillator.detune_automation ? oscillator.detune_automation->value_at_time(now) : oscillator.detune) + param_modulation(oscillator.detune_param_id);
            if (!isfinite(frequency_value) || !isfinite(detune_value))
                continue;
            auto frequency = static_cast<double>(frequency_value) * pow(2.0, static_cast<double>(detune_value) / 1200.0);
            auto phase = fmod((now - oscillator.start_time) * frequency, 1.0);
            if (phase < 0)
                phase += 1.0;
            float sample = 0.0f;
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
            mix_source(mix_source, oscillator.node_id, sample, sample, 0);
        }

        for (auto& source : m_active_constant_sources) {
            if (now < source.start_time)
                continue;
            if (source.stop_time.has_value() && now >= source.stop_time.value()) {
                if (!source.ended_reported)
                    source.ended_reported = queue_source_ended(source.node_id);
                continue;
            }
            auto sample = (source.offset_automation ? source.offset_automation->value_at_time(now) : source.offset) + param_modulation(source.offset_param_id);
            if (isfinite(sample))
                mix_source(mix_source, source.node_id, sample, sample, 0);
        }

        // Flush bounded ChannelMerger outputs after all source inputs for this
        // frame have been collected. Repeating the pass handles a merger fed
        // by another merger without unbounded graph work.
        for (u32 pass = 0; pass < 32; ++pass) {
            bool emitted = false;
            for (auto& merger : merger_frames) {
                if (!merger.used || merger.emitted)
                    continue;
                merger.emitted = true;
                mix_source(mix_source, merger.node_id, merger.channels[0], merger.channels[1], 0);
                emitted = true;
            }
            if (!emitted)
                break;
        }

        buffer[frame * 2] = clamp(left, -1.0f, 1.0f);
        buffer[frame * 2 + 1] = clamp(right, -1.0f, 1.0f);
    }

    m_render_frame_position += frame_count;
    auto end_time = static_cast<double>(m_render_frame_position) / output_rate;
    m_active_audio_sources.remove_all_matching([&](auto& source) {
        bool finished = false;
        if (source.stop_time.has_value() && end_time >= source.stop_time.value())
            finished = true;
        if (source.duration.has_value() && end_time >= source.start_time + source.duration.value())
            finished = true;
        if (!source.loop && !source.buffer)
            finished = true;
        if (!source.loop && source.buffer) {
            auto playback_rate = source.playback_rate_automation ? source.playback_rate_automation->value_at_time(end_time) : source.playback_rate;
            auto detune = source.detune_automation ? source.detune_automation->value_at_time(end_time) : source.detune;
            auto rate = max(static_cast<double>(playback_rate), 0.0) * pow(2.0, static_cast<double>(detune) / 1200.0);
            auto end_frame = source.offset * source.buffer->sample_rate() + (end_time - source.start_time) * rate * source.buffer->sample_rate();
            finished = finished || end_frame >= source.buffer->frame_count();
        }
        if (!finished)
            return false;
        if (!source.ended_reported)
            source.ended_reported = queue_source_ended(source.node_id);
        return source.ended_reported;
    });
    m_active_oscillators.remove_all_matching([&](auto& oscillator) {
        if (!oscillator.stop_time.has_value() || end_time < oscillator.stop_time.value())
            return false;
        if (!oscillator.ended_reported)
            oscillator.ended_reported = queue_source_ended(oscillator.node_id);
        return oscillator.ended_reported;
    });
    m_active_constant_sources.remove_all_matching([&](auto& source) {
        if (!source.stop_time.has_value() || end_time < source.stop_time.value())
            return false;
        if (!source.ended_reported)
            source.ended_reported = queue_source_ended(source.node_id);
        return source.ended_reported;
    });
}

// https://webaudio.github.io/web-audio-api/#dom-audiocontext-createmediaelementsource
WebIDL::ExceptionOr<GC::Ref<MediaElementAudioSourceNode>> AudioContext::create_media_element_source(GC::Ptr<HTML::HTMLMediaElement> media_element)
{
    MediaElementAudioSourceOptions options;
    options.media_element = media_element;
    return MediaElementAudioSourceNode::create(realm(), *this, options);
}

}
