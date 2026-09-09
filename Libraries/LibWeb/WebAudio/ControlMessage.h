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
#include <LibWeb/WebAudio/Types.h>

namespace Web::WebAudio {

struct StartSource {
    NodeID node_id { 0 };
    double when { 0.0 };
};

struct StartBufferSource {
    NodeID node_id { 0 };
    double when { 0.0 };
    double offset { 0.0 };
    Optional<double> duration;
    RefPtr<AudioBufferRenderData> buffer;
    float playback_rate { 1.0f };
    float detune { 0.0f };
    bool loop { false };
    double loop_start { 0.0 };
    double loop_end { 0.0 };
};

struct StopSource {
    NodeID node_id { 0 };
    double when { 0.0 };
};

// FIXME: add more message types

// https://webaudio.github.io/web-audio-api/#control-message
using ControlMessage = Variant<StartSource, StartBufferSource, StopSource>;

}
