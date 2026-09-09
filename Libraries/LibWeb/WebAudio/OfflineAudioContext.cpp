/*
 * Copyright (c) 2024, Shannon Booth <shannon@serenityos.org>
 * Copyright (c) 2025, Ben Eidson <b.e.eidson@gmail.com>
 *
 * SPDX-License-Identifier: BSD-2-Clause
 */

#include <AK/Array.h>
#include <AK/AtomicRefCounted.h>
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

class OfflineRenderState final : public AtomicRefCounted<OfflineRenderState> {
public:
    struct BufferSource {
        RefPtr<AudioBufferRenderData> buffer;
        double start_time { 0.0 };
        double offset { 0.0 };
        Optional<double> duration;
        Optional<double> stop_time;
        float playback_rate { 1.0f };
        float detune { 0.0f };
        RefPtr<AudioParamRenderData> playback_rate_automation;
        RefPtr<AudioParamRenderData> detune_automation;
        AudioParamID playback_rate_param_id { 0 };
        AudioParamID detune_param_id { 0 };
        bool loop { false };
        double loop_start { 0.0 };
        double loop_end { 0.0 };
        NodeID node_id { 0 };
        bool ended_reported { false };
    };
    struct Oscillator {
        double start_time { 0.0 };
        Optional<double> stop_time;
        float frequency { 440.0f };
        float detune { 0.0f };
        RefPtr<AudioParamRenderData> frequency_automation;
        RefPtr<AudioParamRenderData> detune_automation;
        AudioParamID frequency_param_id { 0 };
        AudioParamID detune_param_id { 0 };
        RefPtr<PeriodicWaveRenderData> periodic_wave;
        OscillatorWaveform waveform { OscillatorWaveform::Sine };
        NodeID node_id { 0 };
        bool ended_reported { false };
    };
    struct ConstantSource {
        double start_time { 0.0 };
        Optional<double> stop_time;
        float offset { 1.0f };
        RefPtr<AudioParamRenderData> offset_automation;
        AudioParamID offset_param_id { 0 };
        NodeID node_id { 0 };
        bool ended_reported { false };
    };
    struct NodeConnection {
        NodeID source_node_id { 0 };
        NodeID destination_node_id { 0 };
        AudioNodeRenderKind source_kind { AudioNodeRenderKind::Unknown };
        AudioNodeRenderKind destination_kind { AudioNodeRenderKind::Unknown };
        u32 output_index { 0 };
        u32 input_index { 0 };
        RefPtr<AudioParamRenderData> gain_automation;
        AudioParamID gain_param_id { 0 };
        RefPtr<BiquadFilterRenderData> biquad;
        AudioParamID biquad_frequency_param_id { 0 };
        AudioParamID biquad_detune_param_id { 0 };
        AudioParamID biquad_q_param_id { 0 };
        AudioParamID biquad_gain_param_id { 0 };
        RefPtr<AnalyserRenderData> analyser;
        RefPtr<AudioParamRenderData> stereo_panner_automation;
        AudioParamID stereo_panner_param_id { 0 };
        RefPtr<DelayRenderData> delay;
        AudioParamID delay_param_id { 0 };
        RefPtr<PannerRenderData> panner;
        AudioParamID panner_position_x_param_id { 0 };
        AudioParamID panner_position_y_param_id { 0 };
        AudioParamID panner_position_z_param_id { 0 };
        AudioParamID panner_orientation_x_param_id { 0 };
        AudioParamID panner_orientation_y_param_id { 0 };
        AudioParamID panner_orientation_z_param_id { 0 };
        AudioParamID listener_position_x_param_id { 0 };
        AudioParamID listener_position_y_param_id { 0 };
        AudioParamID listener_position_z_param_id { 0 };
        AudioParamID listener_forward_x_param_id { 0 };
        AudioParamID listener_forward_y_param_id { 0 };
        AudioParamID listener_forward_z_param_id { 0 };
        AudioParamID listener_up_x_param_id { 0 };
        AudioParamID listener_up_y_param_id { 0 };
        AudioParamID listener_up_z_param_id { 0 };
        RefPtr<DynamicsCompressorRenderData> compressor;
        AudioParamID compressor_threshold_param_id { 0 };
        AudioParamID compressor_knee_param_id { 0 };
        AudioParamID compressor_ratio_param_id { 0 };
        AudioParamID compressor_attack_param_id { 0 };
        AudioParamID compressor_release_param_id { 0 };
        float x1[2] { 0, 0 };
        float x2[2] { 0, 0 };
        float y1[2] { 0, 0 };
        float y2[2] { 0, 0 };
    };
    struct ParamConnection {
        NodeID source_node_id { 0 };
        AudioParamID destination_param_id { 0 };
        u32 output_index { 0 };
    };

    Vector<BufferSource> buffer_sources;
    Vector<Oscillator> oscillators;
    Vector<ConstantSource> constant_sources;
    Vector<NodeConnection> node_connections;
    Vector<ParamConnection> param_connections;
    bool initialized { false };
    u32 next_frame { 0 };
};

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
    m_rendering_promise = promise;

    // 7. Otherwise, in the case that the buffer was successfully constructed, begin offline rendering.
    // Rendering starts on a media task so suspend() can publish a target before
    // the first render quantum is consumed.
    queue_a_media_element_task(GC::create_function(heap(), [promise, this]() {
        begin_offline_rendering(promise);
    }));

    // 8. Append promise to [[pending promises]].
    m_pending_promises.append(promise);

    // 9. Return promise.
    return promise;
}

void OfflineAudioContext::begin_offline_rendering(GC::Ref<WebIDL::Promise> promise)
{
    if (!m_render_state) {
        auto state = adopt_ref_if_nonnull(new (nothrow) OfflineRenderState());
        if (!state) {
            auto error = WebIDL::OperationError::create(realm(), "Unable to allocate offline render state"_utf16);
            WebIDL::reject_promise(realm(), promise, error);
            return;
        }
        m_render_state = state;
    }
    auto& state = *m_render_state;
    auto& buffer_sources = state.buffer_sources;
    auto& oscillators = state.oscillators;
    auto& constant_sources = state.constant_sources;
    auto& node_connections = state.node_connections;

    if (!state.initialized) {
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
                    .frequency_param_id = start.frequency_param_id,
                    .detune_param_id = start.detune_param_id,
                    .periodic_wave = start.periodic_wave,
                    .waveform = start.waveform,
                    .node_id = start.node_id,
                });
            },
            [&](StartConstantSource const& start) {
                constant_sources.append({
                    .start_time = start.when,
                    .offset = start.offset,
                    .offset_automation = start.offset_automation,
                    .offset_param_id = start.offset_param_id,
                    .node_id = start.node_id,
                });
            },
            [&](UpdateOscillatorWaveform const& update) {
                for (auto& oscillator : oscillators) {
                    if (oscillator.node_id != update.node_id)
                        continue;
                    oscillator.periodic_wave = update.periodic_wave;
                    oscillator.waveform = update.waveform;
                }
            },
            [&](StartBufferSource const& start) {
                buffer_sources.append({
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
                for (auto& source : constant_sources) {
                    if (source.node_id == stop.node_id)
                        source.stop_time = stop.when;
                }
            },
            [&](ConnectNode const& connect) {
                node_connections.remove_all_matching([&](auto const& existing) {
                    return existing.source_node_id == connect.source_node_id
                        && existing.destination_node_id == connect.destination_node_id
                        && existing.output_index == connect.output_index
                        && existing.input_index == connect.input_index;
                });
                node_connections.append({
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
                node_connections.remove_all_matching([&](auto const& existing) {
                    return existing.source_node_id == disconnect.source_node_id
                        && existing.destination_node_id == disconnect.destination_node_id
                        && (disconnect.output_index == DisconnectNode::wildcard_index
                            || existing.output_index == disconnect.output_index)
                        && (disconnect.input_index == DisconnectNode::wildcard_index
                            || existing.input_index == disconnect.input_index);
                });
            },
            [&](ConnectParam const& connect) {
                param_connections.remove_all_matching([&](auto const& existing) {
                    return existing.source_node_id == connect.source_node_id
                        && existing.destination_param_id == connect.destination_param_id
                        && existing.output_index == connect.output_index;
                });
                param_connections.append({
                    .source_node_id = connect.source_node_id,
                    .destination_param_id = connect.destination_param_id,
                    .output_index = connect.output_index,
                });
            },
            [&](DisconnectParam const& disconnect) {
                param_connections.remove_all_matching([&](auto const& existing) {
                    return existing.source_node_id == disconnect.source_node_id
                        && existing.destination_param_id == disconnect.destination_param_id
                        && existing.output_index == disconnect.output_index;
                });
            },
            [&](UpdateAudioParam const& update) {
                for (auto& source : buffer_sources) {
                    if (source.playback_rate_param_id == update.param_id)
                        source.playback_rate_automation = update.render_data;
                    if (source.detune_param_id == update.param_id)
                        source.detune_automation = update.render_data;
                }
                for (auto& oscillator : oscillators) {
                    if (oscillator.frequency_param_id == update.param_id)
                        oscillator.frequency_automation = update.render_data;
                    if (oscillator.detune_param_id == update.param_id)
                        oscillator.detune_automation = update.render_data;
                }
                for (auto& source : constant_sources) {
                    if (source.offset_param_id == update.param_id)
                        source.offset_automation = update.render_data;
                }
                for (auto& connection : node_connections) {
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
        state.initialized = true;
    }

    Vector<GC::Ref<JS::Float32Array>> output_channels;
    output_channels.ensure_capacity(m_number_of_channels);
    for (u32 channel = 0; channel < m_number_of_channels; ++channel)
        output_channels.append(MUST(m_rendered_buffer->get_channel_data(channel)));

    auto const output_rate = static_cast<double>(sample_rate());
    auto const frame_count = m_rendered_buffer->length();
    auto const render_start = state.next_frame;
    auto render_end = min(frame_count, render_start + BaseAudioContext::render_quantum_size());
    Optional<u32> suspend_frame;
    if (m_pending_suspend_time.has_value()) {
        auto requested_frame = static_cast<u32>(ceil(max(m_pending_suspend_time.value(), 0.0) * output_rate));
        suspend_frame = min(requested_frame, frame_count);
        render_end = min(render_end, suspend_frame.value());
    }
    for (u32 frame = render_start; frame < render_end; ++frame) {
        auto now = frame / output_rate;
        Array<float, BaseAudioContext::MAX_NUMBER_OF_CHANNELS> mixed_samples { };
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
        // Snapshot all direct source -> AudioParam edges before evaluating
        // source parameters.  Without this bounded pre-pass, two source
        // nodes modulating one parameter produced output that depended on
        // vector iteration order (and therefore differed from online audio).
        struct PrecollectedSource {
            NodeID node_id { 0 };
            float left { 0 };
            float right { 0 };
        };
        Array<PrecollectedSource, 256> precollected_source_nodes { };
        auto collect_direct_source_modulation = [&](NodeID node_id, float source_left, float source_right) {
            for (auto const& param_connection : param_connections) {
                if (param_connection.source_node_id == node_id && param_connection.output_index == 0)
                    add_param_modulation(param_connection.destination_param_id, (source_left + source_right) * 0.5f);
            }
            if (precollected_source_nodes.size() < 256)
                precollected_source_nodes.append({ node_id, source_left, source_right });
        };
        for (auto const& source : buffer_sources) {
            if (now < source.start_time || source.stop_time.has_value() && now >= source.stop_time.value()
                || source.duration.has_value() && now - source.start_time >= source.duration.value()
                || !source.buffer || source.buffer->frame_count() == 0)
                continue;
            auto rate = max(static_cast<double>(source.playback_rate), 0.0) * pow(2.0, static_cast<double>(source.detune) / 1200.0);
            auto source_frame = source.offset * source.buffer->sample_rate() + (now - source.start_time) * rate * source.buffer->sample_rate();
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
            auto sample = sample_at(0);
            auto right = source.buffer->channel_count() == 1 ? sample : sample_at(1);
            collect_direct_source_modulation(source.node_id, sample, right);
        }
        for (auto const& oscillator : oscillators) {
            if (now < oscillator.start_time || oscillator.stop_time.has_value() && now >= oscillator.stop_time.value())
                continue;
            auto frequency = static_cast<double>(oscillator.frequency) * pow(2.0, static_cast<double>(oscillator.detune) / 1200.0);
            if (!isfinite(frequency))
                continue;
            auto phase = fmod((now - oscillator.start_time) * frequency, 1.0);
            if (phase < 0)
                phase += 1.0;
            float sample = 0.0f;
            if (oscillator.periodic_wave) {
                sample = oscillator.periodic_wave->sample_at(phase);
            } else {
                switch (oscillator.waveform) {
                case OscillatorWaveform::Sine: sample = static_cast<float>(sin(phase * 2.0 * AK::Pi<double>)); break;
                case OscillatorWaveform::Square: sample = phase < 0.5 ? 1.0f : -1.0f; break;
                case OscillatorWaveform::Sawtooth: sample = static_cast<float>(2.0 * phase - 1.0); break;
                case OscillatorWaveform::Triangle: sample = static_cast<float>(1.0 - 4.0 * fabs(phase - 0.5)); break;
                }
            }
            collect_direct_source_modulation(oscillator.node_id, sample, sample);
        }
        for (auto const& source : constant_sources) {
            if (now < source.start_time || source.stop_time.has_value() && now >= source.stop_time.value())
                continue;
            auto sample = source.offset;
            if (isfinite(sample))
                collect_direct_source_modulation(source.node_id, sample, sample);
        }
        struct MergerFrame {
            NodeID node_id { 0 };
            Array<float, BaseAudioContext::MAX_NUMBER_OF_CHANNELS> channels { };
            bool used { false };
            bool emitted { false };
        };
        Array<MergerFrame, 32> merger_frames { };

        auto mix_source = [&](auto&& self, NodeID source_node_id, float sample, Optional<float> right_sample, u32 depth) -> void {
            if (depth > 32)
                return;
            auto right = right_sample.value_or(sample);
            bool direct_source_was_precollected = false;
            for (auto const& precollected : precollected_source_nodes) {
                if (precollected.node_id == source_node_id) {
                    direct_source_was_precollected = true;
                    for (auto const& param_connection : param_connections) {
                        if (param_connection.source_node_id == source_node_id && param_connection.output_index == 0)
                            add_param_modulation(param_connection.destination_param_id,
                                ((sample - precollected.left) + (right - precollected.right)) * 0.5f);
                    }
                    break;
                }
            }
            if (!direct_source_was_precollected) {
                for (auto const& param_connection : param_connections) {
                    if (param_connection.source_node_id == source_node_id && param_connection.output_index == 0)
                        add_param_modulation(param_connection.destination_param_id, (sample + right) * 0.5f);
                }
            }
            for (auto& connection : node_connections) {
                if (connection.source_node_id != source_node_id)
                    continue;
                auto routed_sample = sample;
                auto routed_right = right;
                if (connection.source_kind == AudioNodeRenderKind::ChannelSplitter) {
                    routed_sample = connection.output_index == 0 ? sample : connection.output_index == 1 ? right : 0.0f;
                    routed_right = routed_sample;
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
                        merger->channels[connection.input_index] += routed_sample;
                    }
                    continue;
                }
                if (connection.destination_kind == AudioNodeRenderKind::ChannelSplitter) {
                    self(self, connection.destination_node_id, routed_sample, routed_right, depth + 1);
                    continue;
                }
                if (connection.destination_kind == AudioNodeRenderKind::Destination) {
                    for (u32 channel = 0; channel < m_number_of_channels; ++channel)
                        mixed_samples[channel] += channel == 1 ? routed_right : routed_sample;
                    continue;
                }
                if (connection.destination_kind == AudioNodeRenderKind::Gain) {
                    auto gain = (connection.gain_automation ? connection.gain_automation->value_at_time(now) : 1.0f) + param_modulation(connection.gain_param_id);
                    if (isfinite(gain))
                        self(self, connection.destination_node_id, routed_sample * gain, routed_right * gain, depth + 1);
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
                    self(self, connection.destination_node_id, process(routed_sample, 0), process(routed_right, 1), depth + 1);
                    continue;
                }
                if (connection.destination_kind == AudioNodeRenderKind::Analyser && connection.analyser) {
                    connection.analyser->push_frame(routed_sample, routed_right);
                    self(self, connection.destination_node_id, routed_sample, routed_right, depth + 1);
                    continue;
                }
                if (connection.destination_kind == AudioNodeRenderKind::StereoPanner) {
                    auto pan = (connection.stereo_panner_automation ? connection.stereo_panner_automation->value_at_time(now) : 0.0f) + param_modulation(connection.stereo_panner_param_id);
                    if (!isfinite(pan))
                        continue;
                    pan = clamp(pan, -1.0f, 1.0f);
                    auto angle = (static_cast<double>(pan) + 1.0) * AK::Pi<double> / 4.0;
                    auto mono = (routed_sample + routed_right) * 0.5f;
                    self(self, connection.destination_node_id, mono * static_cast<float>(cos(angle)), mono * static_cast<float>(sin(angle)), depth + 1);
                    continue;
                }
                if (connection.destination_kind == AudioNodeRenderKind::Delay && connection.delay) {
                    connection.delay->process(routed_sample, routed_right, now, param_modulation(connection.delay_param_id));
                    self(self, connection.destination_node_id, routed_sample, routed_right, depth + 1);
                    continue;
                }
                if (connection.destination_kind == AudioNodeRenderKind::Panner && connection.panner) {
                    connection.panner->process(routed_sample, routed_right, now,
                        param_modulation(connection.panner_position_x_param_id), param_modulation(connection.panner_position_y_param_id), param_modulation(connection.panner_position_z_param_id),
                        param_modulation(connection.panner_orientation_x_param_id), param_modulation(connection.panner_orientation_y_param_id), param_modulation(connection.panner_orientation_z_param_id),
                        param_modulation(connection.listener_position_x_param_id), param_modulation(connection.listener_position_y_param_id), param_modulation(connection.listener_position_z_param_id),
                        param_modulation(connection.listener_forward_x_param_id), param_modulation(connection.listener_forward_y_param_id), param_modulation(connection.listener_forward_z_param_id),
                        param_modulation(connection.listener_up_x_param_id), param_modulation(connection.listener_up_y_param_id), param_modulation(connection.listener_up_z_param_id));
                    self(self, connection.destination_node_id, routed_sample, routed_right, depth + 1);
                    continue;
                }
                if (connection.destination_kind == AudioNodeRenderKind::DynamicsCompressor && connection.compressor) {
                    connection.compressor->process_stereo(routed_sample, routed_right, now, static_cast<float>(output_rate),
                        param_modulation(connection.compressor_threshold_param_id), param_modulation(connection.compressor_knee_param_id),
                        param_modulation(connection.compressor_ratio_param_id), param_modulation(connection.compressor_attack_param_id),
                        param_modulation(connection.compressor_release_param_id));
                    self(self, connection.destination_node_id, routed_sample, routed_right, depth + 1);
                }
            }
        };

        for (auto& source : buffer_sources) {
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
                auto loop_end = source.loop_end > source.loop_start ? clamp(source.loop_end * source.buffer->sample_rate(), loop_start, source_length) : source_length;
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
                mix_source(mix_source, source.node_id, sample, { }, 0);
            } else {
                mix_source(mix_source, source.node_id, sample_at(0), sample_at(1), 0);
            }
        }

        for (auto& oscillator : oscillators) {
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

        for (auto& source : constant_sources) {
            if (now < source.start_time)
                continue;
            if (source.stop_time.has_value() && now >= source.stop_time.value()) {
                if (!source.ended_reported)
                    source.ended_reported = queue_source_ended(source.node_id);
                continue;
            }
            auto sample = (source.offset_automation ? source.offset_automation->value_at_time(now) : source.offset) + param_modulation(source.offset_param_id);
            if (isfinite(sample))
                mix_source(mix_source, source.node_id, sample, { }, 0);
        }

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

        for (u32 channel = 0; channel < m_number_of_channels; ++channel)
            output_channels[channel]->data()[frame] = clamp(mixed_samples[channel], -1.0f, 1.0f);
    }
    state.next_frame = render_end;
    set_current_time(render_end / output_rate);

    if (suspend_frame.has_value() && render_end >= suspend_frame.value() && render_end < frame_count) {
        set_control_state(Bindings::AudioContextState::Suspended);
        set_rendering_state(Bindings::AudioContextState::Suspended);
        auto suspend_promise = m_pending_suspend_promise;
        queue_a_media_element_task(GC::create_function(heap(), [suspend_promise, this]() {
            if (suspend_promise)
                WebIDL::resolve_promise(this->realm(), suspend_promise, JS::js_undefined());
            m_pending_suspend_time.clear();
            m_pending_suspend_promise = nullptr;
            if (suspend_promise)
                m_pending_promises.remove_first_matching([&suspend_promise](auto& pending_promise) {
                    return pending_promise == suspend_promise;
                });
            m_pending_suspend_promise = nullptr;
        }));
        return;
    }

    if (render_end < frame_count) {
        queue_a_media_element_task(GC::create_function(heap(), [promise, this]() {
            begin_offline_rendering(promise);
        }));
        return;
    }

    // 4: Once the rendering is complete, queue a media element task to execute the following steps:
    queue_a_media_element_task(GC::create_function(heap(), [promise, this]() {
        HTML::TemporaryExecutionContext context(this->realm(), HTML::TemporaryExecutionContext::CallbacksEnabled::Yes);

        // Source completion notifications are produced while rendering and
        // dispatched on the control thread before the offline completion
        // event, preserving WebAudio event ordering.
        dispatch_source_ended_events();

        // 4.1 Resolve the promise created by startRendering() with [[rendered buffer]].
        WebIDL::resolve_promise(this->realm(), promise, this->m_rendered_buffer);
        m_render_state = nullptr;

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

    if (!m_rendering_started || state() != Bindings::AudioContextState::Suspended || !m_render_state) {
        return WebIDL::create_rejected_promise_from_exception(realm, WebIDL::InvalidStateError::create(realm, "Offline rendering has not started"_utf16));
    }
    if (!m_render_state || m_render_state->next_frame >= m_rendered_buffer->length()) {
        return WebIDL::create_rejected_promise_from_exception(realm, WebIDL::InvalidStateError::create(realm, "Offline rendering has completed"_utf16));
    }

    m_pending_promises.append(promise);
    m_pending_suspend_time.clear();
    m_pending_suspend_promise = nullptr;
    set_control_state(Bindings::AudioContextState::Running);
    set_rendering_state(Bindings::AudioContextState::Running);
    queue_a_media_element_task(GC::create_function(heap(), [promise, this]() {
        if (m_rendering_promise)
            begin_offline_rendering(*m_rendering_promise);
        WebIDL::resolve_promise(this->realm(), promise, JS::js_undefined());
        m_pending_promises.remove_first_matching([&promise](auto& pending_promise) {
            return pending_promise == promise;
        });
        if (m_render_state && m_render_state->next_frame < m_rendered_buffer->length()) {
            if (!m_pending_promises.is_empty()) {
                auto render_promise = m_pending_promises.first();
                queue_a_media_element_task(GC::create_function(heap(), [render_promise, this]() {
                    begin_offline_rendering(render_promise);
                }));
            }
        }
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
    if (!m_rendering_started || state() != Bindings::AudioContextState::Running) {
        return WebIDL::create_rejected_promise_from_exception(realm, WebIDL::InvalidStateError::create(realm, "Offline rendering has not started"_utf16));
    }
    if (m_pending_suspend_time.has_value()) {
        return WebIDL::create_rejected_promise_from_exception(realm, WebIDL::InvalidStateError::create(realm, "An offline suspend is already pending"_utf16));
    }

    m_pending_promises.append(promise);
    m_pending_suspend_time = suspend_time;
    m_pending_suspend_promise = promise;
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
    visitor.visit(m_pending_suspend_promise);
}

}
