/*
 * Copyright (c) 2023, Luke Wilde <lukew@serenityos.org>
 *
 * SPDX-License-Identifier: BSD-2-Clause
 */

#pragma once

#include <AK/Variant.h>
#include <AK/RefPtr.h>
#include <LibWeb/Bindings/AudioContextPrototype.h>
#include <LibWeb/HighResolutionTime/DOMHighResTimeStamp.h>
#include <LibMedia/Audio/PlaybackStream.h>
#include <LibWeb/WebAudio/BaseAudioContext.h>
#include <LibWeb/WebAudio/AudioBufferRenderData.h>
#include <LibWeb/WebAudio/AnalyserRenderData.h>
#include <LibWeb/WebAudio/DelayRenderData.h>
#include <LibWeb/WebAudio/DynamicsCompressorRenderData.h>
#include <LibWeb/WebAudio/MediaElementAudioSourceNode.h>
#include <LibWeb/WebAudio/PeriodicWaveRenderData.h>

namespace Web::WebAudio {

struct AudioContextOptions {
    Variant<Bindings::AudioContextLatencyCategory, double> latency_hint = Bindings::AudioContextLatencyCategory::Interactive;
    Optional<float> sample_rate;
};

struct AudioTimestamp {
    double context_time { 0 };
    double performance_time { 0 };
};

// https://webaudio.github.io/web-audio-api/#AudioContext
class AudioContext final : public BaseAudioContext {
    WEB_PLATFORM_OBJECT(AudioContext, BaseAudioContext);
    GC_DECLARE_ALLOCATOR(AudioContext);

public:
    static WebIDL::ExceptionOr<GC::Ref<AudioContext>> construct_impl(JS::Realm&, Optional<AudioContextOptions> const& context_options = {});

    virtual ~AudioContext() override;

    double base_latency() const { return m_base_latency; }
    double output_latency() const { return m_output_latency; }
    WebIDL::UnsignedLong output_channel_count() const
    {
        return m_playback_stream ? m_playback_stream->sample_specification().channel_count() : 2;
    }
    AudioTimestamp get_output_timestamp();
    WebIDL::ExceptionOr<GC::Ref<WebIDL::Promise>> resume();
    WebIDL::ExceptionOr<GC::Ref<WebIDL::Promise>> suspend();
    WebIDL::ExceptionOr<GC::Ref<WebIDL::Promise>> close();

    WebIDL::ExceptionOr<GC::Ref<MediaElementAudioSourceNode>> create_media_element_source(GC::Ptr<HTML::HTMLMediaElement>);

private:
    explicit AudioContext(JS::Realm& realm)
        : BaseAudioContext(realm)
    {
    }

    virtual void initialize(JS::Realm&) override;
    virtual void visit_edges(Cell::Visitor&) override;

    double m_base_latency { 0 };
    double m_output_latency { 0 };

    bool m_allowed_to_start = true;
    Vector<GC::Ref<WebIDL::Promise>> m_pending_resume_promises;
    bool m_suspended_by_user = false;
    RefPtr<Audio::PlaybackStream> m_playback_stream;
    bool m_backend_start_pending { false };
    struct ActiveAudioSource {
        NodeID node_id { 0 };
        RefPtr<AudioBufferRenderData> buffer;
        double start_time { 0.0 };
        double offset { 0.0 };
        Optional<double> duration;
        Optional<double> stop_time;
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
    struct ActiveOscillator {
        NodeID node_id { 0 };
        double start_time { 0.0 };
        Optional<double> stop_time;
        float frequency { 440.0f };
        float detune { 0.0f };
        RefPtr<AudioParamRenderData> frequency_automation;
        RefPtr<AudioParamRenderData> detune_automation;
        AudioParamID frequency_param_id { 0 };
        AudioParamID detune_param_id { 0 };
        RefPtr<PeriodicWaveRenderData> periodic_wave;
        OscillatorWaveform waveform { OscillatorWaveform::Sine };
    };
    struct ActiveConstantSource {
        NodeID node_id { 0 };
        double start_time { 0.0 };
        Optional<double> stop_time;
        float offset { 1.0f };
        RefPtr<AudioParamRenderData> offset_automation;
        AudioParamID offset_param_id { 0 };
    };
    struct NodeConnection {
        NodeID source_node_id { 0 };
        NodeID destination_node_id { 0 };
        AudioNodeRenderKind destination_kind { AudioNodeRenderKind::Unknown };
        RefPtr<AudioParamRenderData> gain_automation;
        AudioParamID gain_param_id { 0 };
        RefPtr<BiquadFilterRenderData> biquad;
        AudioParamID biquad_frequency_param_id { 0 };
        AudioParamID biquad_detune_param_id { 0 };
        AudioParamID biquad_q_param_id { 0 };
        AudioParamID biquad_gain_param_id { 0 };
        RefPtr<AnalyserRenderData> analyser;
        RefPtr<AudioParamRenderData> stereo_panner_automation;
        AudioParamID stereo_panner_param_id { 0 };
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
        float x1[2] { 0, 0 };
        float x2[2] { 0, 0 };
        float y1[2] { 0, 0 };
        float y2[2] { 0, 0 };
    };
    Vector<ActiveAudioSource> m_active_audio_sources;
    Vector<ActiveOscillator> m_active_oscillators;
    Vector<ActiveConstantSource> m_active_constant_sources;
    Vector<NodeConnection> m_node_connections;
    u64 m_render_frame_position { 0 };
    u32 m_output_sample_rate { 48'000 };

    bool start_rendering_audio_graph();
    void render_audio(Span<float>);
};

}
