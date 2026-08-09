/*
 * Copyright (c) 2026, RinOS contributors
 *
 * SPDX-License-Identifier: BSD-2-Clause
 */

#include <AK/Array.h>
#include <AK/StdLibExtras.h>
#include <LibCore/EventLoop.h>
#include <LibCore/ThreadedPromise.h>
#include <LibMedia/Audio/ChannelMap.h>
#include <LibThreading/Thread.h>
#include <apps/common/rin_audio.h>
#include <unistd.h>

#include "PlaybackStreamRinOS.h"

namespace Audio {

static constexpr u32 sample_rate = 48'000;
static constexpr u32 channel_count = 2;
static constexpr u32 render_chunk_frames = 480;
static constexpr u32 render_chunk_samples = render_chunk_frames * channel_count;

NonnullRefPtr<PlaybackStream::CreatePromise> PlaybackStream::create(OutputState initial_output_state, u32 target_latency_ms, AudioDataRequestCallback&& data_request_callback)
{
    return PlaybackStreamRinOS::create(initial_output_state, target_latency_ms, move(data_request_callback));
}

NonnullRefPtr<PlaybackStream::CreatePromise> PlaybackStreamRinOS::create(OutputState initial_state, u32 target_latency_ms, AudioDataRequestCallback&& data_request_callback)
{
    VERIFY(data_request_callback);

    auto promise = CreatePromise::construct();
    auto state = MUST(adopt_nonnull_ref_or_enomem(new (nothrow) InternalState(target_latency_ms, move(data_request_callback))));
    auto playback_stream = MUST(adopt_nonnull_ref_or_enomem(new (nothrow) PlaybackStreamRinOS(state)));

    auto thread = MUST(Threading::Thread::try_create("RinOS Audio"sv, [state, playback_stream, promise, initial_state, main_thread_event_loop = Core::EventLoop::current_weak()]() mutable {
        auto handle = rin_audio_open();
        if (handle < 0) {
            auto event_loop = main_thread_event_loop->take();
            if (event_loop.is_alive()) {
                event_loop->deferred_invoke([promise = move(promise)]() mutable {
                    promise->reject(Error::from_string_literal("RinOS audio output is unavailable"));
                });
            }
            return 1;
        }

        state->set_handle(handle);
        (void)rin_audio_control(handle, RIN_AUDIO_CTL_SET_VOLUME, 100);
        if (initial_state == OutputState::Playing) {
            state->set_playing(true);
            (void)rin_audio_control(handle, RIN_AUDIO_CTL_START, 0);
        }

        auto event_loop = main_thread_event_loop->take();
        if (!event_loop.is_alive()) {
            (void)rin_audio_close(handle);
            state->set_handle(-1);
            return 1;
        }
        event_loop->deferred_invoke([promise = move(promise), playback_stream = move(playback_stream)]() mutable {
            promise->resolve(playback_stream);
        });

        state->thread_loop();
        (void)rin_audio_control(handle, RIN_AUDIO_CTL_FLUSH, 0);
        (void)rin_audio_close(handle);
        state->set_handle(-1);
        return 0;
    }));
    thread->start();
    thread->detach();
    return promise;
}

PlaybackStreamRinOS::InternalState::InternalState(u32 target_latency_ms, AudioDataRequestCallback&& callback)
    : m_target_latency_frames(AK::clamp(target_latency_ms, 20u, 200u) * (sample_rate / 1000))
    , m_data_request_callback(move(callback))
{
}

void PlaybackStreamRinOS::InternalState::set_handle(int handle)
{
    m_handle.store(handle);
}

int PlaybackStreamRinOS::InternalState::handle() const
{
    return m_handle.load();
}

ErrorOr<void> PlaybackStreamRinOS::InternalState::check_is_running() const
{
    if (m_exit.load() || handle() < 0)
        return Error::from_string_literal("RinOS audio render thread is not running");
    return {};
}

void PlaybackStreamRinOS::InternalState::enqueue(Function<void()>&& task)
{
    Threading::MutexLocker locker { m_mutex };
    m_tasks.enqueue(move(task));
    m_wake_condition.signal();
}

void PlaybackStreamRinOS::InternalState::set_playing(bool playing)
{
    Threading::MutexLocker locker { m_mutex };
    m_playing = playing;
    m_wake_condition.signal();
}

void PlaybackStreamRinOS::InternalState::set_underrun_callback(Function<void()>&& callback)
{
    m_underrun_callback = move(callback);
}

void PlaybackStreamRinOS::InternalState::render_one_chunk()
{
    auto audio_handle = handle();
    if (audio_handle < 0)
        return;

    RinAudioStatusV1 status {};
    if (rin_audio_get_status(audio_handle, &status) < 0) {
        usleep(1000);
        return;
    }

    if (status.underrun_count != m_last_kernel_underrun_count) {
        m_last_kernel_underrun_count = status.underrun_count;
        if (m_underrun_callback)
            m_underrun_callback();
    }

    auto high_water_frames = min(m_target_latency_frames, status.capacity_frames);
    if (status.queued_frames >= high_water_frames) {
        usleep(1000);
        return;
    }

    Array<float, render_chunk_samples> float_samples {};
    Array<i16, render_chunk_samples> pcm_samples {};
    auto supplied = m_data_request_callback(float_samples.span());
    auto sample_count_to_write = min(supplied.size(), float_samples.size());
    sample_count_to_write -= sample_count_to_write % channel_count;

    if (sample_count_to_write == 0) {
        sample_count_to_write = render_chunk_samples;
        if (m_underrun_callback)
            m_underrun_callback();
    } else {
        for (size_t index = 0; index < sample_count_to_write; ++index) {
            auto sample = supplied[index];
            if (sample <= -1.0f)
                pcm_samples[index] = -32768;
            else if (sample >= 1.0f)
                pcm_samples[index] = 32767;
            else
                pcm_samples[index] = static_cast<i16>(sample * 32767.0f);
        }
    }

    auto bytes = ReadonlyBytes { reinterpret_cast<u8 const*>(pcm_samples.data()), sample_count_to_write * sizeof(i16) };
    size_t offset = 0;
    while (offset < bytes.size()) {
        auto written = rin_audio_write(audio_handle, bytes.data() + offset, bytes.size() - offset);
        if (written < 0) {
            usleep(1000);
            return;
        }
        if (written == 0) {
            usleep(1000);
            continue;
        }
        offset += static_cast<size_t>(written);
    }
}

void PlaybackStreamRinOS::InternalState::thread_loop()
{
    while (!m_exit.load()) {
        Function<void()> task;
        bool should_render = false;
        {
            Threading::MutexLocker locker { m_mutex };
            while (m_tasks.is_empty() && !m_playing && !m_exit.load())
                m_wake_condition.wait();
            if (m_exit.load())
                break;
            if (!m_tasks.is_empty())
                task = m_tasks.dequeue();
            else
                should_render = m_playing;
        }
        if (task)
            task();
        else if (should_render)
            render_one_chunk();
    }
}

void PlaybackStreamRinOS::InternalState::exit()
{
    m_exit.store(true);
    m_wake_condition.signal();
}

PlaybackStreamRinOS::~PlaybackStreamRinOS()
{
    m_state->exit();
}

SampleSpecification PlaybackStreamRinOS::sample_specification() const
{
    return SampleSpecification(sample_rate, ChannelMap::stereo());
}

void PlaybackStreamRinOS::set_underrun_callback(Function<void()> callback)
{
    m_state->enqueue([state = m_state, callback = move(callback)]() mutable {
        state->set_underrun_callback(move(callback));
    });
}

NonnullRefPtr<Core::ThreadedPromise<AK::Duration>> PlaybackStreamRinOS::resume()
{
    auto promise = Core::ThreadedPromise<AK::Duration>::create();
    if (auto result = m_state->check_is_running(); result.is_error()) {
        promise->reject(result.release_error());
        return promise;
    }
    m_state->enqueue([state = m_state, promise] {
        if (rin_audio_control(state->handle(), RIN_AUDIO_CTL_START, 0) < 0) {
            promise->reject(Error::from_string_literal("Unable to resume RinOS audio"));
            return;
        }
        state->set_playing(true);
        RinAudioStatusV1 status {};
        (void)rin_audio_get_status(state->handle(), &status);
        promise->resolve(AK::Duration::from_nanoseconds(static_cast<i64>(status.played_frames * 1'000'000'000ULL / sample_rate)));
    });
    return promise;
}

NonnullRefPtr<Core::ThreadedPromise<void>> PlaybackStreamRinOS::drain_buffer_and_suspend()
{
    auto promise = Core::ThreadedPromise<void>::create();
    if (auto result = m_state->check_is_running(); result.is_error()) {
        promise->reject(result.release_error());
        return promise;
    }
    m_state->enqueue([state = m_state, promise] {
        state->set_playing(false);
        if (rin_audio_control(state->handle(), RIN_AUDIO_CTL_DRAIN, 0) < 0) {
            promise->reject(Error::from_string_literal("Unable to drain RinOS audio"));
            return;
        }
        promise->resolve();
    });
    return promise;
}

NonnullRefPtr<Core::ThreadedPromise<void>> PlaybackStreamRinOS::discard_buffer_and_suspend()
{
    auto promise = Core::ThreadedPromise<void>::create();
    if (auto result = m_state->check_is_running(); result.is_error()) {
        promise->reject(result.release_error());
        return promise;
    }
    m_state->enqueue([state = m_state, promise] {
        state->set_playing(false);
        if (rin_audio_control(state->handle(), RIN_AUDIO_CTL_FLUSH, 0) < 0) {
            promise->reject(Error::from_string_literal("Unable to flush RinOS audio"));
            return;
        }
        promise->resolve();
    });
    return promise;
}

AK::Duration PlaybackStreamRinOS::total_time_played() const
{
    RinAudioStatusV1 status {};
    if (m_state->handle() < 0 || rin_audio_get_status(m_state->handle(), &status) < 0)
        return AK::Duration::zero();
    return AK::Duration::from_nanoseconds(static_cast<i64>(status.played_frames * 1'000'000'000ULL / sample_rate));
}

NonnullRefPtr<Core::ThreadedPromise<void>> PlaybackStreamRinOS::set_volume(double volume)
{
    auto promise = Core::ThreadedPromise<void>::create();
    if (auto result = m_state->check_is_running(); result.is_error()) {
        promise->reject(result.release_error());
        return promise;
    }
    auto percent = static_cast<uintptr_t>(AK::clamp(volume, 0.0, 1.0) * 100.0);
    m_state->enqueue([state = m_state, promise, percent] {
        if (rin_audio_control(state->handle(), RIN_AUDIO_CTL_SET_VOLUME, percent) < 0) {
            promise->reject(Error::from_string_literal("Unable to set RinOS audio volume"));
            return;
        }
        promise->resolve();
    });
    return promise;
}

}
