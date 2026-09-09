/*
 * Copyright (c) 2026, Ben Eidson <b.e.eidson@gmail.com>
 *
 * SPDX-License-Identifier: BSD-2-Clause
 */

#pragma once

#include <AK/AtomicRefCounted.h>
#include <AK/ErrorOr.h>
#include <AK/Vector.h>
#include <LibThreading/Mutex.h>

namespace Web::WebAudio {

// Bounded, mutex-protected mono history shared by the render callback and JS API.
class AnalyserRenderData final : public AtomicRefCounted<AnalyserRenderData> {
public:
    static ErrorOr<NonnullRefPtr<AnalyserRenderData>> create(size_t frame_count);

    size_t frame_count() const { return m_history.size(); }
    void push_frame(float left, float right);
    Vector<float> snapshot() const;

private:
    explicit AnalyserRenderData(size_t frame_count)
        : m_history(frame_count)
    {
    }

    mutable Threading::Mutex m_mutex;
    Vector<float> m_history;
    size_t m_next_frame { 0 };
    size_t m_written_frames { 0 };
};

}
