/*
 * Copyright (c) 2026, Ben Eidson <b.e.eidson@gmail.com>
 *
 * SPDX-License-Identifier: BSD-2-Clause
 */

#pragma once

#include <AK/AtomicRefCounted.h>
#include <AK/ErrorOr.h>
#include <AK/ReadonlySpan.h>
#include <AK/Vector.h>

namespace Web::WebAudio {

// Immutable, non-GC PeriodicWave data used by the render callback.
class PeriodicWaveRenderData final : public AtomicRefCounted<PeriodicWaveRenderData> {
public:
    static ErrorOr<NonnullRefPtr<PeriodicWaveRenderData>> create(ReadonlySpan<float> real, ReadonlySpan<float> imag, bool normalize);

    float sample_at(double phase) const;

private:
    PeriodicWaveRenderData(Vector<float>&& real, Vector<float>&& imag, float normalization)
        : m_real(move(real))
        , m_imag(move(imag))
        , m_normalization(normalization)
    {
    }

    Vector<float> m_real;
    Vector<float> m_imag;
    float m_normalization { 1.0f };
};

}
