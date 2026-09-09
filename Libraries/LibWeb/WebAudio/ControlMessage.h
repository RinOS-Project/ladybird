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
#include <LibWeb/WebAudio/AudioParamRenderData.h>
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
    AudioNodeRenderKind destination_kind { AudioNodeRenderKind::Unknown };
    RefPtr<AudioParamRenderData> gain_automation;
};

struct DisconnectNode {
    NodeID source_node_id { 0 };
    NodeID destination_node_id { 0 };
};

// https://webaudio.github.io/web-audio-api/#control-message
using ControlMessage = Variant<StartSource, StartOscillator, StartBufferSource, StopSource, ConnectNode, DisconnectNode>;

}
