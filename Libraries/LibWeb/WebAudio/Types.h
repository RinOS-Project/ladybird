/*
 * Copyright (c) 2026, Ben Eidson <b.e.eidson@gmail.com>
 *
 * SPDX-License-Identifier: BSD-2-Clause
 */

#pragma once

#include <AK/DistinctNumeric.h>

namespace Web::WebAudio {

// Stable identifier for AudioNode instances within a BaseAudioContext.
AK_TYPEDEF_DISTINCT_NUMERIC_GENERAL(u64, NodeID, CastToUnderlying);
// Stable identifier for AudioParam instances within a BaseAudioContext.
AK_TYPEDEF_DISTINCT_NUMERIC_GENERAL(u64, AudioParamID, CastToUnderlying);

enum class AudioNodeRenderKind : u8 {
    Unknown,
    Gain,
    Biquad,
    Analyser,
    StereoPanner,
    Delay,
    DynamicsCompressor,
    Panner,
    Destination,
};

enum class PannerDistanceModel : u8 {
    Linear,
    Inverse,
    Exponential,
};

}
