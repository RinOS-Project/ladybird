/*
 * Copyright (c) 2024, Shannon Booth <shannon@serenityos.org>
 * Copyright (c) 2025-2026, Ben Eidson <b.e.eidson@gmail.com>
 *
 * SPDX-License-Identifier: BSD-2-Clause
 */

#include <LibWeb/Bindings/Intrinsics.h>
#include <LibWeb/WebAudio/AudioDestinationNode.h>
#include <LibWeb/WebAudio/AudioNode.h>
#include <LibWeb/WebAudio/AnalyserNode.h>
#include <LibWeb/WebAudio/BaseAudioContext.h>
#include <LibWeb/WebAudio/BiquadFilterNode.h>
#include <LibWeb/WebAudio/ControlMessage.h>
#include <LibWeb/WebAudio/DelayNode.h>
#include <LibWeb/WebAudio/DynamicsCompressorNode.h>
#include <LibWeb/WebAudio/DynamicsCompressorRenderData.h>
#include <LibWeb/WebAudio/GainNode.h>
#include <LibWeb/WebAudio/PannerNode.h>
#include <LibWeb/WebAudio/PannerRenderData.h>
#include <LibWeb/WebAudio/StereoPannerNode.h>

namespace Web::WebAudio {

GC_DEFINE_ALLOCATOR(AudioNode);

AudioNode::AudioNode(JS::Realm& realm, GC::Ref<BaseAudioContext> context, WebIDL::UnsignedLong channel_count)
    : DOM::EventTarget(realm)
    , m_context(context)
    , m_channel_count(channel_count)
    , m_node_id(context->next_node_id({}))

{
}

AudioNode::~AudioNode() = default;

WebIDL::ExceptionOr<void> AudioNode::initialize_audio_node_options(AudioNodeOptions const& given_options, AudioNodeDefaultOptions const& default_options)
{
    // Set channel count, fallback to default if not provided
    if (given_options.channel_count.has_value()) {
        TRY(set_channel_count(given_options.channel_count.value()));
    } else {
        TRY(set_channel_count(default_options.channel_count));
    }

    // Set channel count mode, fallback to default if not provided
    if (given_options.channel_count_mode.has_value()) {
        TRY(set_channel_count_mode(given_options.channel_count_mode.value()));
    } else {
        TRY(set_channel_count_mode(default_options.channel_count_mode));
    }

    // Set channel interpretation, fallback to default if not provided
    if (given_options.channel_interpretation.has_value()) {
        TRY(set_channel_interpretation(given_options.channel_interpretation.value()));
    } else {
        TRY(set_channel_interpretation(default_options.channel_interpretation));
    }

    return {};
}

// https://webaudio.github.io/web-audio-api/#dom-audionode-connect
WebIDL::ExceptionOr<GC::Ref<AudioNode>> AudioNode::connect(GC::Ref<AudioNode> destination_node, WebIDL::UnsignedLong output, WebIDL::UnsignedLong input)
{
    AudioNodeConnection output_connection { destination_node, output, input };
    AudioNodeConnection input_connection { *this, output, input };

    // There can only be one connection between a given output of one specific node and a given input of another specific node.
    // Multiple connections with the same termini are ignored.
    for (auto const& existing_connection : m_output_connections) {
        if (existing_connection == output_connection)
            return destination_node;
    }

    // If the destination parameter is an AudioNode that has been created using another AudioContext, an InvalidAccessError MUST be thrown.
    if (m_context != destination_node->m_context) {
        return WebIDL::InvalidAccessError::create(realm(), "Cannot connect to an AudioNode in a different AudioContext"_utf16);
    }

    // The output parameter is an index describing which output of the AudioNode from which to connect.
    // If this parameter is out-of-bounds, an IndexSizeError exception MUST be thrown.
    if (output >= number_of_outputs()) {
        return WebIDL::IndexSizeError::create(realm(), Utf16String::formatted("Output index {} exceeds number of outputs", output));
    }

    // The input parameter is an index describing which input of the destination AudioNode to connect to.
    // If this parameter is out-of-bounds, an IndexSizeError exception MUST be thrown.
    if (input >= destination_node->number_of_inputs()) {
        return WebIDL::IndexSizeError::create(realm(), Utf16String::formatted("Input index '{}' exceeds number of inputs", input));
    }
    RefPtr<AudioParamRenderData> gain_automation;
    RefPtr<BiquadFilterRenderData> biquad;
    AudioParamID gain_param_id { 0 };
    AudioParamID biquad_frequency_param_id { 0 };
    AudioParamID biquad_detune_param_id { 0 };
    AudioParamID biquad_q_param_id { 0 };
    AudioParamID biquad_gain_param_id { 0 };
    RefPtr<AnalyserRenderData> analyser;
    RefPtr<DelayRenderData> delay;
    AudioParamID delay_param_id { 0 };
    RefPtr<PannerRenderData> panner;
    AudioParamID panner_position_x_param_id { 0 };
    AudioParamID panner_position_y_param_id { 0 };
    AudioParamID panner_position_z_param_id { 0 };
    RefPtr<DynamicsCompressorRenderData> compressor;
    AudioParamID compressor_threshold_param_id { 0 };
    AudioParamID compressor_knee_param_id { 0 };
    AudioParamID compressor_ratio_param_id { 0 };
    AudioParamID compressor_attack_param_id { 0 };
    AudioParamID compressor_release_param_id { 0 };
    RefPtr<AudioParamRenderData> stereo_panner_automation;
    AudioParamID stereo_panner_param_id { 0 };
    auto destination_kind = AudioNodeRenderKind::Unknown;
    if (is<GainNode>(*destination_node)) {
        destination_kind = AudioNodeRenderKind::Gain;
        auto gain = as<GainNode>(*destination_node).gain();
        gain_automation = TRY(gain->create_render_data());
        gain_param_id = gain->param_id();
    } else if (is<BiquadFilterNode>(*destination_node)) {
        destination_kind = AudioNodeRenderKind::Biquad;
        auto const& filter = as<BiquadFilterNode>(*destination_node);
        BiquadFilterKind filter_kind;
        switch (filter.type()) {
        case Bindings::BiquadFilterType::Lowpass:
            filter_kind = BiquadFilterKind::Lowpass;
            break;
        case Bindings::BiquadFilterType::Highpass:
            filter_kind = BiquadFilterKind::Highpass;
            break;
        case Bindings::BiquadFilterType::Bandpass:
            filter_kind = BiquadFilterKind::Bandpass;
            break;
        case Bindings::BiquadFilterType::Notch:
            filter_kind = BiquadFilterKind::Notch;
            break;
        case Bindings::BiquadFilterType::Allpass:
            filter_kind = BiquadFilterKind::Allpass;
            break;
        case Bindings::BiquadFilterType::Peaking:
            filter_kind = BiquadFilterKind::Peaking;
            break;
        case Bindings::BiquadFilterType::Lowshelf:
            filter_kind = BiquadFilterKind::Lowshelf;
            break;
        case Bindings::BiquadFilterType::Highshelf:
            filter_kind = BiquadFilterKind::Highshelf;
            break;
        }
        auto frequency_automation = TRY(filter.frequency()->create_render_data());
        auto detune_automation = TRY(filter.detune()->create_render_data());
        auto q_automation = TRY(filter.q()->create_render_data());
        auto gain_automation_for_filter = TRY(filter.gain()->create_render_data());
        biquad_frequency_param_id = filter.frequency()->param_id();
        biquad_detune_param_id = filter.detune()->param_id();
        biquad_q_param_id = filter.q()->param_id();
        biquad_gain_param_id = filter.gain()->param_id();
        biquad = TRY(BiquadFilterRenderData::create(filter_kind, filter.frequency()->value(), filter.detune()->value(), filter.q()->value(), filter.gain()->value(),
            move(frequency_automation), move(detune_automation), move(q_automation), move(gain_automation_for_filter)));
    } else if (is<AnalyserNode>(*destination_node)) {
        destination_kind = AudioNodeRenderKind::Analyser;
        analyser = TRY(as<AnalyserNode>(*destination_node).ensure_render_data());
    } else if (is<StereoPannerNode>(*destination_node)) {
        destination_kind = AudioNodeRenderKind::StereoPanner;
        auto pan = as<StereoPannerNode>(*destination_node).pan();
        stereo_panner_automation = TRY(pan->create_render_data());
        stereo_panner_param_id = pan->param_id();
    } else if (is<DelayNode>(*destination_node)) {
        destination_kind = AudioNodeRenderKind::Delay;
        auto const& delay_node = as<DelayNode>(*destination_node);
        auto delay_automation = TRY(delay_node.delay_time()->create_render_data());
        delay_param_id = delay_node.delay_time()->param_id();
        delay = TRY(DelayRenderData::create(delay_node.max_delay_time(), m_context->sample_rate(), move(delay_automation)));
    } else if (is<DynamicsCompressorNode>(*destination_node)) {
        destination_kind = AudioNodeRenderKind::DynamicsCompressor;
        auto const& compressor_node = as<DynamicsCompressorNode>(*destination_node);
        auto threshold_automation = TRY(compressor_node.threshold()->create_render_data());
        auto knee_automation = TRY(compressor_node.knee()->create_render_data());
        auto ratio_automation = TRY(compressor_node.ratio()->create_render_data());
        auto attack_automation = TRY(compressor_node.attack()->create_render_data());
        auto release_automation = TRY(compressor_node.release()->create_render_data());
        compressor_threshold_param_id = compressor_node.threshold()->param_id();
        compressor_knee_param_id = compressor_node.knee()->param_id();
        compressor_ratio_param_id = compressor_node.ratio()->param_id();
        compressor_attack_param_id = compressor_node.attack()->param_id();
        compressor_release_param_id = compressor_node.release()->param_id();
        compressor = TRY(DynamicsCompressorRenderData::create(
            compressor_node.threshold()->value(), compressor_node.knee()->value(), compressor_node.ratio()->value(),
            compressor_node.attack()->value(), compressor_node.release()->value(), move(threshold_automation),
            move(knee_automation), move(ratio_automation), move(attack_automation), move(release_automation)));
    } else if (is<PannerNode>(*destination_node)) {
        destination_kind = AudioNodeRenderKind::Panner;
        auto const& panner_node = as<PannerNode>(*destination_node);
        PannerDistanceModel distance_model;
        switch (panner_node.distance_model()) {
        case Bindings::DistanceModelType::Linear:
            distance_model = PannerDistanceModel::Linear;
            break;
        case Bindings::DistanceModelType::Inverse:
            distance_model = PannerDistanceModel::Inverse;
            break;
        case Bindings::DistanceModelType::Exponential:
            distance_model = PannerDistanceModel::Exponential;
            break;
        }
        auto position_x = TRY(panner_node.position_x()->create_render_data());
        auto position_y = TRY(panner_node.position_y()->create_render_data());
        auto position_z = TRY(panner_node.position_z()->create_render_data());
        panner_position_x_param_id = panner_node.position_x()->param_id();
        panner_position_y_param_id = panner_node.position_y()->param_id();
        panner_position_z_param_id = panner_node.position_z()->param_id();
        panner = TRY(PannerRenderData::create(distance_model, panner_node.ref_distance(), panner_node.max_distance(), panner_node.rolloff_factor(), move(position_x), move(position_y), move(position_z)));
    } else if (is<AudioDestinationNode>(*destination_node)) {
        destination_kind = AudioNodeRenderKind::Destination;
    }
    m_context->queue_control_message(ConnectNode {
        .source_node_id = node_id(),
        .destination_node_id = destination_node->node_id(),
        .destination_kind = destination_kind,
        .gain_automation = move(gain_automation),
        .gain_param_id = gain_param_id,
        .biquad = move(biquad),
        .biquad_frequency_param_id = biquad_frequency_param_id,
        .biquad_detune_param_id = biquad_detune_param_id,
        .biquad_q_param_id = biquad_q_param_id,
        .biquad_gain_param_id = biquad_gain_param_id,
        .analyser = move(analyser),
        .stereo_panner_automation = move(stereo_panner_automation),
        .stereo_panner_param_id = stereo_panner_param_id,
        .delay = move(delay),
        .delay_param_id = delay_param_id,
        .panner = move(panner),
        .panner_position_x_param_id = panner_position_x_param_id,
        .panner_position_y_param_id = panner_position_y_param_id,
        .panner_position_z_param_id = panner_position_z_param_id,
        .compressor = move(compressor),
        .compressor_threshold_param_id = compressor_threshold_param_id,
        .compressor_knee_param_id = compressor_knee_param_id,
        .compressor_ratio_param_id = compressor_ratio_param_id,
        .compressor_attack_param_id = compressor_attack_param_id,
        .compressor_release_param_id = compressor_release_param_id,
    });

    // Publish the JS graph only after all render-side snapshots were built.
    // A failed snapshot must not leave a connection that the renderer cannot consume.
    m_output_connections.append(output_connection);
    destination_node->m_input_connections.append(input_connection);

    return destination_node;
}

// https://webaudio.github.io/web-audio-api/#dom-audionode-connect-destinationparam-output
WebIDL::ExceptionOr<void> AudioNode::connect(GC::Ref<AudioParam> destination_param, WebIDL::UnsignedLong output)
{
    AudioParamConnection param_connection { destination_param, output };

    // There can only be one connection between a given output of one specific node and a specific AudioParam. Multiple connections
    //  with the same termini are ignored.
    for (auto const& existing_connection : m_param_connections) {
        if (existing_connection == param_connection)
            return {};
    }

    // If destinationParam belongs to an AudioNode that belongs to a BaseAudioContext that is different from the BaseAudioContext
    // that has created the AudioNode on which this method was called, an InvalidAccessError MUST be thrown.
    if (m_context != destination_param->context()) {
        return WebIDL::InvalidAccessError::create(realm(), "Cannot connect to an AudioParam in a different AudioContext"_utf16);
    }

    // The output parameter is an index describing which output of the AudioNode from which to connect.
    // If the parameter is out-of-bounds, an IndexSizeError exception MUST be thrown.
    if (output >= number_of_outputs()) {
        return WebIDL::IndexSizeError::create(realm(), Utf16String::formatted("Output index {} exceeds number of outputs", output));
    }

    // Connect node's output to destination_param.
    m_param_connections.append(param_connection);

    return {};
}

// https://webaudio.github.io/web-audio-api/#dom-audionode-disconnect
void AudioNode::disconnect()
{
    while (!m_output_connections.is_empty()) {
        auto connection = m_output_connections.take_last();
        auto destination = connection.destination_node;

        m_context->queue_control_message(DisconnectNode {
            .source_node_id = node_id(),
            .destination_node_id = destination->node_id(),
        });

        destination->m_input_connections.remove_all_matching([&](AudioNodeConnection& input_connection) {
            return input_connection.destination_node.ptr() == this;
        });
    }

    m_param_connections.clear();
}

// https://webaudio.github.io/web-audio-api/#dom-audionode-disconnect-output
WebIDL::ExceptionOr<void> AudioNode::disconnect(WebIDL::UnsignedLong output)
{
    // The output parameter is an index describing which output of the AudioNode to disconnect.
    // It disconnects all outgoing connections from the given output.
    // If this parameter is out-of-bounds, an IndexSizeError exception MUST be thrown.
    if (output >= number_of_outputs()) {
        return WebIDL::IndexSizeError::create(realm(), Utf16String::formatted("Output index {} exceeds number of outputs", output));
    }

    m_output_connections.remove_all_matching([&](AudioNodeConnection& connection) {
        if (connection.output != output)
            return false;

        m_context->queue_control_message(DisconnectNode {
            .source_node_id = node_id(),
            .destination_node_id = connection.destination_node->node_id(),
        });

        connection.destination_node->m_input_connections.remove_all_matching([&](AudioNodeConnection& reverse_connection) {
            return reverse_connection.destination_node.ptr() == this && reverse_connection.output == output;
        });

        return true;
    });

    m_param_connections.remove_all_matching([&](AudioParamConnection& connection) {
        return connection.output == output;
    });

    return {};
}

// https://webaudio.github.io/web-audio-api/#dom-audionode-disconnect-destinationnode
WebIDL::ExceptionOr<void> AudioNode::disconnect(GC::Ref<AudioNode> destination_node)
{
    // The destinationNode parameter is the AudioNode to disconnect.
    // It disconnects all outgoing connections to the given destinationNode.
    auto before = m_output_connections.size();
    m_output_connections.remove_all_matching([&](AudioNodeConnection& connection) {
        if (connection.destination_node != destination_node)
            return false;

        m_context->queue_control_message(DisconnectNode {
            .source_node_id = node_id(),
            .destination_node_id = destination_node->node_id(),
        });

        connection.destination_node->m_input_connections.remove_all_matching([&](AudioNodeConnection& reverse_connection) {
            return reverse_connection.destination_node.ptr() == this;
        });

        return true;
    });
    // If there is no connection to the destinationNode, an InvalidAccessError exception MUST be thrown.
    if (m_output_connections.size() == before) {
        return WebIDL::InvalidAccessError::create(realm(), Utf16String::formatted("No connection to given AudioNode"));
    }

    return {};
}

// https://webaudio.github.io/web-audio-api/#dom-audionode-disconnect-destinationnode-output
WebIDL::ExceptionOr<void> AudioNode::disconnect(GC::Ref<AudioNode> destination_node, WebIDL::UnsignedLong output)
{
    // The output parameter is an index describing which output of the AudioNode from which to disconnect.
    // If this parameter is out-of-bounds, an IndexSizeError exception MUST be thrown.
    if (output >= number_of_outputs()) {
        return WebIDL::IndexSizeError::create(realm(), Utf16String::formatted("Output index {} exceeds number of outputs", output));
    }

    // The destinationNode parameter is the AudioNode to disconnect.
    auto before = m_output_connections.size();
    m_output_connections.remove_all_matching([&](AudioNodeConnection& connection) {
        if (connection.destination_node != destination_node || connection.output != output)
            return false;

        m_context->queue_control_message(DisconnectNode {
            .source_node_id = node_id(),
            .destination_node_id = destination_node->node_id(),
        });

        connection.destination_node->m_input_connections.remove_all_matching([&](AudioNodeConnection& reverse_connection) {
            return reverse_connection.destination_node.ptr() == this && reverse_connection.output == output;
        });

        return true;
    });

    //  If there is no connection to the destinationNode from the given output, an InvalidAccessError exception MUST be thrown.
    if (m_output_connections.size() == before) {
        return WebIDL::InvalidAccessError::create(realm(), Utf16String::formatted("No connection from output {} to given AudioNode", output));
    }

    return {};
}

// https://webaudio.github.io/web-audio-api/#dom-audionode-disconnect-destinationnode-output-input
WebIDL::ExceptionOr<void> AudioNode::disconnect(GC::Ref<AudioNode> destination_node, WebIDL::UnsignedLong output, WebIDL::UnsignedLong input)
{
    // The output parameter is an index describing which output of the AudioNode from which to disconnect.
    // If this parameter is out-of-bounds, an IndexSizeError exception MUST be thrown.
    if (output >= number_of_outputs()) {
        return WebIDL::IndexSizeError::create(realm(), Utf16String::formatted("Output index {} exceeds number of outputs", output));
    }

    // The input parameter is an index describing which input of the destination AudioNode to disconnect.
    // If this parameter is out-of-bounds, an IndexSizeError exception MUST be thrown.
    if (input >= destination_node->number_of_inputs()) {
        return WebIDL::IndexSizeError::create(realm(), Utf16String::formatted("Input index '{}' exceeds number of inputs", input));
    }

    // The destinationNode parameter is the AudioNode to disconnect.
    auto before = m_output_connections.size();
    m_output_connections.remove_all_matching([&](AudioNodeConnection& connection) {
        if (connection.destination_node != destination_node || connection.output != output || connection.input != input)
            return false;

        m_context->queue_control_message(DisconnectNode {
            .source_node_id = node_id(),
            .destination_node_id = destination_node->node_id(),
        });

        connection.destination_node->m_input_connections.remove_all_matching([&](AudioNodeConnection& reverse_connection) {
            return reverse_connection.destination_node.ptr() == this && reverse_connection.output == output && reverse_connection.input == input;
        });

        return true;
    });

    // If there is no connection to the destinationNode from the given output to the given input, an InvalidAccessError exception MUST be thrown.
    if (m_output_connections.size() == before) {
        return WebIDL::InvalidAccessError::create(realm(), Utf16String::formatted("No connection from output {} to input {} of given AudioNode", output, input));
    }

    return {};
}

// https://webaudio.github.io/web-audio-api/#dom-audionode-disconnect-destinationparam
WebIDL::ExceptionOr<void> AudioNode::disconnect(GC::Ref<AudioParam> destination_param)
{
    // The destinationParam parameter is the AudioParam to disconnect.
    auto before = m_param_connections.size();
    m_param_connections.remove_all_matching([&](AudioParamConnection& connection) {
        return connection.destination_param == destination_param;
    });

    // If there is no connection to the destinationParam, an InvalidAccessError exception MUST be thrown.
    if (m_param_connections.size() == before) {
        return WebIDL::InvalidAccessError::create(realm(), Utf16String::formatted("No connection to given AudioParam"));
    }

    return {};
}

// https://webaudio.github.io/web-audio-api/#dom-audionode-disconnect-destinationparam-output
WebIDL::ExceptionOr<void> AudioNode::disconnect(GC::Ref<AudioParam> destination_param, WebIDL::UnsignedLong output)
{
    // The output parameter is an index describing which output of the AudioNode from which to disconnect.
    // If this parameter is out-of-bounds, an IndexSizeError exception MUST be thrown.
    if (output >= number_of_outputs()) {
        return WebIDL::IndexSizeError::create(realm(), Utf16String::formatted("Output index {} exceeds number of outputs", output));
    }
    // The destinationParam parameter is the AudioParam to disconnect.
    auto before = m_param_connections.size();
    m_param_connections.remove_all_matching([&](AudioParamConnection& connection) {
        return connection.destination_param == destination_param && connection.output == output;
    });

    // If there is no connection to the destinationParam, an InvalidAccessError exception MUST be thrown.
    if (m_param_connections.size() == before) {
        return WebIDL::InvalidAccessError::create(realm(), Utf16String::formatted("No connection from output {} to given AudioParam", output));
    }

    return {};
}

// https://webaudio.github.io/web-audio-api/#dom-audionode-channelcount
WebIDL::ExceptionOr<void> AudioNode::set_channel_count(WebIDL::UnsignedLong channel_count)
{
    // If this value is set to zero or to a value greater than the implementation’s maximum number
    // of channels the implementation MUST throw a NotSupportedError exception.
    if (channel_count == 0 || channel_count > BaseAudioContext::MAX_NUMBER_OF_CHANNELS)
        return WebIDL::NotSupportedError::create(realm(), "Invalid channel count"_utf16);

    m_channel_count = channel_count;
    return {};
}

// https://webaudio.github.io/web-audio-api/#dom-audionode-channelcountmode
WebIDL::ExceptionOr<void> AudioNode::set_channel_count_mode(Bindings::ChannelCountMode channel_count_mode)
{
    m_channel_count_mode = channel_count_mode;
    return {};
}

// https://webaudio.github.io/web-audio-api/#dom-audionode-channelcountmode
Bindings::ChannelCountMode AudioNode::channel_count_mode()
{
    return m_channel_count_mode;
}

// https://webaudio.github.io/web-audio-api/#dom-audionode-channelinterpretation
WebIDL::ExceptionOr<void> AudioNode::set_channel_interpretation(Bindings::ChannelInterpretation channel_interpretation)
{
    m_channel_interpretation = channel_interpretation;
    return {};
}

// https://webaudio.github.io/web-audio-api/#dom-audionode-channelinterpretation
Bindings::ChannelInterpretation AudioNode::channel_interpretation()
{
    return m_channel_interpretation;
}

void AudioNode::initialize(JS::Realm& realm)
{
    WEB_SET_PROTOTYPE_FOR_INTERFACE(AudioNode);
    Base::initialize(realm);
}

void AudioNode::visit_edges(Cell::Visitor& visitor)
{
    Base::visit_edges(visitor);
    visitor.visit(m_context);
    for (auto& conn : m_param_connections)
        visitor.visit(conn.destination_param);

    for (auto& conn : m_input_connections)
        visitor.visit(conn.destination_node);

    for (auto& conn : m_output_connections)
        visitor.visit(conn.destination_node);
}

}
