/*
 * Copyright (c) 2026, RinOS contributors
 *
 * SPDX-License-Identifier: BSD-2-Clause
 */

#pragma once

#include <AK/Atomic.h>
#include <AK/Function.h>
#include <AK/Queue.h>
#include <LibMedia/Audio/PlaybackStream.h>
#include <LibThreading/ConditionVariable.h>
#include <LibThreading/Mutex.h>

namespace Audio {

class PlaybackStreamRinOS final : public PlaybackStream {
public:
    static NonnullRefPtr<CreatePromise> create(OutputState, u32 target_latency_ms, AudioDataRequestCallback&&);

    virtual ~PlaybackStreamRinOS() override;
    virtual SampleSpecification sample_specification() const override;
    virtual void set_underrun_callback(Function<void()>) override;
    virtual NonnullRefPtr<Core::ThreadedPromise<AK::Duration>> resume() override;
    virtual NonnullRefPtr<Core::ThreadedPromise<void>> drain_buffer_and_suspend() override;
    virtual NonnullRefPtr<Core::ThreadedPromise<void>> discard_buffer_and_suspend() override;
    virtual AK::Duration total_time_played() const override;
    virtual NonnullRefPtr<Core::ThreadedPromise<void>> set_volume(double) override;

private:
    class InternalState final : public AtomicRefCounted<InternalState> {
    public:
        InternalState(u32 target_latency_ms, AudioDataRequestCallback&&);

        void set_handle(int);
        int handle() const;
        ErrorOr<void> check_is_running() const;
        void enqueue(Function<void()>&&);
        void set_playing(bool);
        void set_underrun_callback(Function<void()>&&);
        void thread_loop();
        void exit();

    private:
        void render_one_chunk();

        Atomic<int> m_handle { -1 };
        Atomic<bool> m_exit { false };
        Threading::Mutex m_mutex;
        Threading::ConditionVariable m_wake_condition { m_mutex };
        Queue<Function<void()>> m_tasks;
        bool m_playing { false };
        u32 m_target_latency_frames { 0 };
        AudioDataRequestCallback m_data_request_callback;
        Function<void()> m_underrun_callback;
        u64 m_last_kernel_underrun_count { 0 };
    };

    explicit PlaybackStreamRinOS(NonnullRefPtr<InternalState> state)
        : m_state(move(state))
    {
    }

    NonnullRefPtr<InternalState> m_state;
};

}
