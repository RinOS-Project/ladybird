/*
 * Copyright (c) 2025-2026, Ben Eidson <b.e.eidson@gmail.com>
 *
 * SPDX-License-Identifier: BSD-2-Clause
 */

#pragma once

#include <AK/Optional.h>
#include <AK/RefPtr.h>
#include <AK/Variant.h>
#include <LibWeb/WebAudio/AudioBufferRenderData.h>
#include <LibWeb/WebAudio/AnalyserRenderData.h>
#include <LibWeb/WebAudio/AudioParamRenderData.h>
#include <LibWeb/WebAudio/BiquadFilterRenderData.h>
#include <LibWeb/WebAudio/PeriodicWaveRenderData.h>
#include <LibWeb/WebAudio/DelayRenderData.h>
#include <LibWeb/WebAudio/PannerRenderData.h>
#include <LibWeb/WebAudio/DynamicsCompressorRenderData.h>
#include <LibWeb/WebAudio/Types.h>

namespace Web::WebAudio {

struct StartSource {
    NodeID node_id { 0 };
    double when { 0.0 };
};

enum class OscillatorWaveform : u8 {
    Sine,
    Square,
    Sawtooth,
    Triangle,
};

struct StartOscillator {
    NodeID node_id { 0 };
    double when { 0.0 };
    float frequency { 440.0f };
    float detune { 0.0f };
    RefPtr<AudioParamRenderData> frequency_automation;
    RefPtr<AudioParamRenderData> detune_automation;
    AudioParamID frequency_param_id { 0 };
    AudioParamID detune_param_id { 0 };
    RefPtr<PeriodicWaveRenderData> periodic_wave;
    OscillatorWaveform waveform { OscillatorWaveform::Sine };
};

struct StartConstantSource {
    NodeID node_id { 0 };
    double when { 0.0 };
    float offset { 1.0f };
    RefPtr<AudioParamRenderData> offset_automation;
    AudioParamID offset_param_id { 0 };
};

struct UpdateOscillatorWaveform {
    NodeID node_id { 0 };
    RefPtr<PeriodicWaveRenderData> periodic_wave;
    OscillatorWaveform waveform { OscillatorWaveform::Sine };
};

struct StartBufferSource {
    NodeID node_id { 0 };
    double when { 0.0 };
    double offset { 0.0 };
    Optional<double> duration;
    RefPtr<AudioBufferRenderData> buffer;
    float playback_rate { 1.0f };
    float detune { 0.0f };
    RefPtr<AudioParamRenderData> playback_rate_automation;
    RefPtr<AudioParamRenderData> detune_automation;
    AudioParamID playback_rate_param_id { 0 };
    AudioParamID detune_param_id { 0 };
    bool loop { false };
    double loop_start { 0.0 };
    double loop_end { 0.0 };
};

struct StopSource {
    NodeID node_id { 0 };
    double when { 0.0 };
};

struct ConnectNode {
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
    RefPtr<AudioParamRenderData> stereo_panner_automation;
    AudioParamID stereo_panner_param_id { 0 };
};

struct UpdateAudioParam {
    AudioParamID param_id { 0 };
    RefPtr<AudioParamRenderData> render_data;
};

struct DisconnectNode {
    NodeID source_node_id { 0 };
    NodeID destination_node_id { 0 };
};

// https://webaudio.github.io/web-audio-api/#control-message
using ControlMessage = Variant<StartSource, StartOscillator, StartConstantSource, UpdateOscillatorWaveform, StartBufferSource, StopSource, ConnectNode, DisconnectNode, UpdateAudioParam>;

}
