/*
 * Copyright (c) 2026, Ben Eidson <b.e.eidson@gmail.com>
 *
 * SPDX-License-Identifier: BSD-2-Clause
 */

#include <AK/Math.h>
#include <LibWeb/WebAudio/PeriodicWaveRenderData.h>
#include <errno.h>
#include <math.h>

namespace Web::WebAudio {

ErrorOr<NonnullRefPtr<PeriodicWaveRenderData>> PeriodicWaveRenderData::create(ReadonlySpan<float> real, ReadonlySpan<float> imag, bool normalize)
{
    constexpr size_t max_harmonics = 4096;
    if (real.size() != imag.size() || real.size() < 2 || real.size() > max_harmonics)
        return Error::from_errno(EINVAL);

    Vector<float> real_copy;
    Vector<float> imag_copy;
    TRY(real_copy.try_append(real.data(), real.size()));
    TRY(imag_copy.try_append(imag.data(), imag.size()));

    float normalization = 1.0f;
    double coefficient_bound = 0.0;
    for (size_t index = 1; index < real.size(); ++index) {
        if (!isfinite(real[index]) || !isfinite(imag[index]))
            return Error::from_errno(EINVAL);
        if (normalize)
            coefficient_bound += fabs(static_cast<double>(real[index])) + fabs(static_cast<double>(imag[index]));
    }
    if (normalize) {
        if (!isfinite(coefficient_bound))
            return Error::from_errno(EINVAL);
        if (coefficient_bound > 1.0)
            normalization = static_cast<float>(1.0 / coefficient_bound);
    }

    return adopt_nonnull_ref_or_enomem(new (nothrow) PeriodicWaveRenderData(move(real_copy), move(imag_copy), normalization));
}

float PeriodicWaveRenderData::sample_at(double phase) const
{
    if (!isfinite(phase))
        return 0.0f;
    phase -= floor(phase);
    auto const angle = 2.0 * AK::Pi<double> * phase;
    double sample = 0.0;
    for (size_t harmonic = 1; harmonic < m_real.size(); ++harmonic)
        sample += static_cast<double>(m_real[harmonic]) * cos(angle * harmonic)
            + static_cast<double>(m_imag[harmonic]) * sin(angle * harmonic);
    sample *= m_normalization;
    if (!isfinite(sample))
        return 0.0f;
    return clamp(static_cast<float>(sample), -1.0f, 1.0f);
}

}
