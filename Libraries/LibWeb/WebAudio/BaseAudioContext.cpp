/*
 * Copyright (c) 2023, Luke Wilde <lukew@serenityos.org>
 * Copyright (c) 2024, Shannon Booth <shannon@serenityos.org>
 * Copyright (c) 2024, Jelle Raaijmakers <jelle@ladybird.org>
 *
 * SPDX-License-Identifier: BSD-2-Clause
 */

#include <LibWeb/Bindings/BaseAudioContextPrototype.h>
#include <LibWeb/Bindings/Intrinsics.h>
#include <LibWeb/DOM/Document.h>
#include <LibWeb/HTML/EventLoop/EventLoop.h>
#include <LibWeb/HTML/EventNames.h>
#include <LibWeb/HTML/Scripting/ExceptionReporter.h>
#include <LibWeb/HTML/Window.h>
#include <LibWeb/WebAudio/AnalyserNode.h>
#include <LibWeb/WebAudio/AudioBuffer.h>
#include <LibWeb/WebAudio/AudioBufferSourceNode.h>
#include <LibWeb/WebAudio/AudioDestinationNode.h>
#include <LibWeb/WebAudio/BaseAudioContext.h>
#include <LibWeb/WebAudio/BiquadFilterNode.h>
#include <LibWeb/WebAudio/ChannelMergerNode.h>
#include <LibWeb/WebAudio/ControlMessageQueue.h>
#include <LibWeb/WebAudio/DynamicsCompressorNode.h>
#include <LibWeb/WebAudio/GainNode.h>
#include <LibWeb/WebAudio/OscillatorNode.h>
#include <LibWeb/WebAudio/PannerNode.h>
#include <LibWeb/WebIDL/AbstractOperations.h>
#include <LibWeb/WebIDL/Promise.h>
#include <AK/NumericLimits.h>
#include <math.h>
#include <string.h>

namespace Web::WebAudio {

namespace {

struct DecodedPcm {
    Vector<float> samples;
    u32 channels { 0 };
    u32 frames { 0 };
    u32 sample_rate { 0 };
};

static bool has_bytes(ReadonlyBytes bytes, size_t offset, size_t count)
{
    return offset <= bytes.size() && count <= bytes.size() - offset;
}

static u16 read_u16_le(ReadonlyBytes bytes, size_t offset)
{
    return static_cast<u16>(bytes[offset]) | (static_cast<u16>(bytes[offset + 1]) << 8);
}

static u32 read_u32_le(ReadonlyBytes bytes, size_t offset)
{
    return static_cast<u32>(bytes[offset])
        | (static_cast<u32>(bytes[offset + 1]) << 8)
        | (static_cast<u32>(bytes[offset + 2]) << 16)
        | (static_cast<u32>(bytes[offset + 3]) << 24);
}

static i32 read_i24_le(ReadonlyBytes bytes, size_t offset)
{
    u32 value = static_cast<u32>(bytes[offset])
        | (static_cast<u32>(bytes[offset + 1]) << 8)
        | (static_cast<u32>(bytes[offset + 2]) << 16);
    if (value & 0x00800000)
        value |= 0xff000000;
    return static_cast<i32>(value);
}

static float decode_pcm_sample(ReadonlyBytes bytes, size_t offset, u16 format, u16 bits_per_sample)
{
    if (format == 3 && bits_per_sample == 32) {
        u32 bits = read_u32_le(bytes, offset);
        float value;
        memcpy(&value, &bits, sizeof(value));
        return value;
    }
    if (format == 3 && bits_per_sample == 64) {
        u64 bits = static_cast<u64>(read_u32_le(bytes, offset))
            | (static_cast<u64>(read_u32_le(bytes, offset + 4)) << 32);
        double value;
        memcpy(&value, &bits, sizeof(value));
        return static_cast<float>(value);
    }

    switch (bits_per_sample) {
    case 8:
        return (static_cast<float>(bytes[offset]) - 128.0f) / 128.0f;
    case 16:
        return static_cast<float>(static_cast<i16>(read_u16_le(bytes, offset))) / 32768.0f;
    case 24:
        return static_cast<float>(read_i24_le(bytes, offset)) / 8388608.0f;
    case 32:
        return static_cast<float>(static_cast<i32>(read_u32_le(bytes, offset))) / 2147483648.0f;
    default:
        return 0;
    }
}

static ErrorOr<DecodedPcm> decode_wave_pcm(ReadonlyBytes bytes)
{
    if (!has_bytes(bytes, 0, 12) || memcmp(bytes.data(), "RIFF", 4) != 0 || memcmp(bytes.data() + 8, "WAVE", 4) != 0)
        return Error::from_string_literal("unsupported audio container");

    auto riff_size = read_u32_le(bytes, 4);
    if (riff_size < 4 || static_cast<u64>(riff_size) + 8 > bytes.size())
        return Error::from_string_literal("truncated RIFF container");

    Optional<u16> format;
    Optional<u16> channels;
    Optional<u16> bits_per_sample;
    Optional<u16> block_align;
    Optional<u32> sample_rate;
    ReadonlyBytes data;

    size_t offset = 12;
    auto container_end = min(bytes.size(), static_cast<size_t>(riff_size) + 8);
    while (offset < container_end) {
        if (!has_bytes(bytes, offset, 8) || offset + 8 > container_end)
            return Error::from_string_literal("truncated RIFF chunk");
        auto chunk_size = read_u32_le(bytes, offset + 4);
        auto chunk_data = offset + 8;
        if (chunk_size > container_end - chunk_data)
            return Error::from_string_literal("RIFF chunk exceeds container");

        if (memcmp(bytes.data() + offset, "fmt ", 4) == 0) {
            if (format.has_value() || chunk_size < 16)
                return Error::from_string_literal("invalid WAV format chunk");
            auto format_tag = read_u16_le(bytes, chunk_data);
            auto channel_count = read_u16_le(bytes, chunk_data + 2);
            auto rate = read_u32_le(bytes, chunk_data + 4);
            auto byte_rate = read_u32_le(bytes, chunk_data + 8);
            auto alignment = read_u16_le(bytes, chunk_data + 12);
            auto bits = read_u16_le(bytes, chunk_data + 14);

            if (format_tag == 0xfffe) {
                if (chunk_size < 40 || read_u16_le(bytes, chunk_data + 16) < 22)
                    return Error::from_string_literal("invalid extensible WAV format");
                auto subtype = read_u16_le(bytes, chunk_data + 24);
                if (subtype != 1 && subtype != 3)
                    return Error::from_string_literal("unsupported extensible WAV subtype");
                format_tag = subtype;
            }
            if (format_tag != 1 && format_tag != 3)
                return Error::from_string_literal("unsupported WAV encoding");
            if (channel_count == 0 || channel_count > BaseAudioContext::MAX_NUMBER_OF_CHANNELS || rate < BaseAudioContext::MIN_SAMPLE_RATE || rate > BaseAudioContext::MAX_SAMPLE_RATE)
                return Error::from_string_literal("WAV format is outside WebAudio limits");
            if (format_tag == 1) {
                if (bits != 8 && bits != 16 && bits != 24 && bits != 32)
                    return Error::from_string_literal("unsupported PCM width");
            } else if (bits != 32 && bits != 64) {
                return Error::from_string_literal("unsupported float width");
            }
            auto bytes_per_sample = static_cast<u32>((bits + 7) / 8);
            if (alignment != channel_count * bytes_per_sample || byte_rate != rate * alignment)
                return Error::from_string_literal("invalid WAV block geometry");
            format = format_tag;
            channels = channel_count;
            bits_per_sample = bits;
            block_align = alignment;
            sample_rate = rate;
        } else if (memcmp(bytes.data() + offset, "data", 4) == 0) {
            if (!data.is_empty())
                return Error::from_string_literal("multiple WAV data chunks are not supported");
            data = bytes.slice(chunk_data, chunk_size);
        }

        auto padded_size = static_cast<u64>(chunk_size) + (chunk_size & 1);
        if (padded_size > container_end - offset - 8)
            return Error::from_string_literal("invalid WAV chunk padding");
        offset += 8 + padded_size;
    }

    if (!format.has_value() || !channels.has_value() || !bits_per_sample.has_value() || !block_align.has_value() || !sample_rate.has_value() || data.is_empty())
        return Error::from_string_literal("WAV is missing format or data");
    if (data.size() % block_align.value() != 0)
        return Error::from_string_literal("WAV data is not frame aligned");

    auto frame_count = data.size() / block_align.value();
    if (frame_count == 0 || frame_count > NumericLimits<u32>::max())
        return Error::from_string_literal("WAV frame count is out of range");

    DecodedPcm decoded;
    decoded.channels = channels.value();
    decoded.frames = static_cast<u32>(frame_count);
    decoded.sample_rate = sample_rate.value();
    TRY(decoded.samples.try_resize(frame_count * decoded.channels));

    auto bytes_per_sample = bits_per_sample.value() / 8;
    for (u32 frame = 0; frame < decoded.frames; ++frame) {
        for (u16 channel = 0; channel < decoded.channels; ++channel) {
            auto sample_offset = static_cast<size_t>(frame) * block_align.value() + static_cast<size_t>(channel) * bytes_per_sample;
            auto value = decode_pcm_sample(data, sample_offset, format.value(), bits_per_sample.value());
            if (!isfinite(value))
                return Error::from_string_literal("WAV contains a non-finite sample");
            decoded.samples[static_cast<size_t>(frame) * decoded.channels + channel] = value;
        }
    }
    return decoded;
}

}

BaseAudioContext::BaseAudioContext(JS::Realm& realm, float sample_rate)
    : DOM::EventTarget(realm)
    , m_sample_rate(sample_rate)
    , m_listener(AudioListener::create(realm, *this))
    , m_control_message_queue(make<ControlMessageQueue>())
{
}

BaseAudioContext::~BaseAudioContext() = default;

void BaseAudioContext::initialize(JS::Realm& realm)
{
    WEB_SET_PROTOTYPE_FOR_INTERFACE(BaseAudioContext);
    Base::initialize(realm);
}

void BaseAudioContext::visit_edges(Cell::Visitor& visitor)
{
    Base::visit_edges(visitor);
    visitor.visit(m_destination);
    visitor.visit(m_pending_promises);
    visitor.visit(m_listener);
}

void BaseAudioContext::set_onstatechange(WebIDL::CallbackType* event_handler)
{
    set_event_handler_attribute(HTML::EventNames::statechange, event_handler);
}

WebIDL::CallbackType* BaseAudioContext::onstatechange()
{
    return event_handler_attribute(HTML::EventNames::statechange);
}

// https://webaudio.github.io/web-audio-api/#dom-baseaudiocontext-createanalyser
WebIDL::ExceptionOr<GC::Ref<AnalyserNode>> BaseAudioContext::create_analyser()
{
    return AnalyserNode::create(realm(), *this);
}

// https://webaudio.github.io/web-audio-api/#dom-baseaudiocontext-createbiquadfilter
WebIDL::ExceptionOr<GC::Ref<BiquadFilterNode>> BaseAudioContext::create_biquad_filter()
{
    // Factory method for a BiquadFilterNode representing a second order filter which can be configured as one of several common filter types.
    return BiquadFilterNode::create(realm(), *this);
}

// https://webaudio.github.io/web-audio-api/#dom-baseaudiocontext-createbuffer
WebIDL::ExceptionOr<GC::Ref<AudioBuffer>> BaseAudioContext::create_buffer(WebIDL::UnsignedLong number_of_channels, WebIDL::UnsignedLong length, float sample_rate)
{
    // Creates an AudioBuffer of the given size. The audio data in the buffer will be zero-initialized (silent).
    // A NotSupportedError exception MUST be thrown if any of the arguments is negative, zero, or outside its nominal range.
    return AudioBuffer::create(realm(), number_of_channels, length, sample_rate);
}

// https://webaudio.github.io/web-audio-api/#dom-baseaudiocontext-createbuffersource
WebIDL::ExceptionOr<GC::Ref<AudioBufferSourceNode>> BaseAudioContext::create_buffer_source()
{
    // Factory method for a AudioBufferSourceNode.
    return AudioBufferSourceNode::create(realm(), *this);
}

// https://webaudio.github.io/web-audio-api/#dom-baseaudiocontext-createchannelmerger
WebIDL::ExceptionOr<GC::Ref<ChannelMergerNode>> BaseAudioContext::create_channel_merger(WebIDL::UnsignedLong number_of_inputs)
{
    ChannelMergerOptions options;
    options.number_of_inputs = number_of_inputs;

    return ChannelMergerNode::create(realm(), *this, options);
}

// https://webaudio.github.io/web-audio-api/#dom-baseaudiocontext-createconstantsource
WebIDL::ExceptionOr<GC::Ref<ConstantSourceNode>> BaseAudioContext::create_constant_source()
{
    return ConstantSourceNode::create(realm(), *this);
}

// https://webaudio.github.io/web-audio-api/#dom-baseaudiocontext-createdelay
WebIDL::ExceptionOr<GC::Ref<DelayNode>> BaseAudioContext::create_delay(double max_delay_time)
{
    DelayOptions options;
    options.max_delay_time = max_delay_time;

    return DelayNode::create(realm(), *this, options);
}

// https://webaudio.github.io/web-audio-api/#dom-baseaudiocontext-createchannelsplitter
WebIDL::ExceptionOr<GC::Ref<ChannelSplitterNode>> BaseAudioContext::create_channel_splitter(WebIDL::UnsignedLong number_of_outputs)
{
    ChannelSplitterOptions options;
    options.number_of_outputs = number_of_outputs;

    return ChannelSplitterNode::create(realm(), *this, options);
}

// https://webaudio.github.io/web-audio-api/#dom-baseaudiocontext-createoscillator
WebIDL::ExceptionOr<GC::Ref<OscillatorNode>> BaseAudioContext::create_oscillator()
{
    // Factory method for an OscillatorNode.
    return OscillatorNode::create(realm(), *this);
}

// https://webaudio.github.io/web-audio-api/#dom-baseaudiocontext-createdynamicscompressor
WebIDL::ExceptionOr<GC::Ref<DynamicsCompressorNode>> BaseAudioContext::create_dynamics_compressor()
{
    // Factory method for a DynamicsCompressorNode.
    return DynamicsCompressorNode::create(realm(), *this);
}

// https://webaudio.github.io/web-audio-api/#dom-baseaudiocontext-creategain
WebIDL::ExceptionOr<GC::Ref<GainNode>> BaseAudioContext::create_gain()
{
    // Factory method for GainNode.
    return GainNode::create(realm(), *this);
}

// https://webaudio.github.io/web-audio-api/#dom-baseaudiocontext-createpanner
WebIDL::ExceptionOr<GC::Ref<PannerNode>> BaseAudioContext::create_panner()
{
    // Factory method for a PannerNode.
    return PannerNode::create(realm(), *this);
}

WebIDL::ExceptionOr<GC::Ref<PeriodicWave>> BaseAudioContext::create_periodic_wave(Vector<float> const& real, Vector<float> const& imag, Optional<PeriodicWaveConstraints> const& constraints)
{
    PeriodicWaveOptions options;
    options.real = real;
    options.imag = imag;
    if (constraints.has_value())
        options.disable_normalization = constraints->disable_normalization;

    return PeriodicWave::construct_impl(realm(), *this, options);
}

// https://webaudio.github.io/web-audio-api/#dom-baseaudiocontext-createscriptprocessor
WebIDL::ExceptionOr<GC::Ref<ScriptProcessorNode>> BaseAudioContext::create_script_processor(
    WebIDL::UnsignedLong buffer_size,
    WebIDL::UnsignedLong number_of_input_channels,
    WebIDL::UnsignedLong number_of_output_channels)
{
    // The bufferSize parameter determines the buffer size in units of sample-frames. If it’s not passed in, or if the
    // value is 0, then the implementation will choose the best buffer size for the given environment, which will be
    // constant power of 2 throughout the lifetime of the node.
    if (buffer_size == 0)
        buffer_size = ScriptProcessorNode::DEFAULT_BUFFER_SIZE;

    return ScriptProcessorNode::create(realm(), *this, buffer_size, number_of_input_channels,
        number_of_output_channels);
}

// https://webaudio.github.io/web-audio-api/#dom-baseaudiocontext-createstereopanner
WebIDL::ExceptionOr<GC::Ref<StereoPannerNode>> BaseAudioContext::create_stereo_panner()
{
    // Factory method for a StereoPannerNode.
    return StereoPannerNode::create(realm(), *this);
}

WebIDL::ExceptionOr<void> BaseAudioContext::verify_audio_options_inside_nominal_range(JS::Realm& realm, float sample_rate)
{
    if (sample_rate < MIN_SAMPLE_RATE || sample_rate > MAX_SAMPLE_RATE)
        return WebIDL::NotSupportedError::create(realm, "Sample rate is outside of allowed range"_utf16);

    return {};
}

// https://webaudio.github.io/web-audio-api/#dom-baseaudiocontext-createbuffer
WebIDL::ExceptionOr<void> BaseAudioContext::verify_audio_options_inside_nominal_range(JS::Realm& realm, WebIDL::UnsignedLong number_of_channels, WebIDL::UnsignedLong length, float sample_rate)
{
    // A NotSupportedError exception MUST be thrown if any of the arguments is negative, zero, or outside its nominal range.

    if (number_of_channels == 0)
        return WebIDL::NotSupportedError::create(realm, "Number of channels must not be '0'"_utf16);

    if (number_of_channels > MAX_NUMBER_OF_CHANNELS)
        return WebIDL::NotSupportedError::create(realm, "Number of channels is greater than allowed range"_utf16);

    if (length == 0)
        return WebIDL::NotSupportedError::create(realm, "Length of buffer must be at least 1"_utf16);

    TRY(verify_audio_options_inside_nominal_range(realm, sample_rate));

    return {};
}

void BaseAudioContext::queue_a_media_element_task(GC::Ref<GC::Function<void()>> steps)
{
    auto task = HTML::Task::create(vm(), m_media_element_event_task_source.source, HTML::current_principal_settings_object().responsible_document(), steps);
    (void)HTML::main_thread_event_loop().task_queue().add(task);
}

void BaseAudioContext::queue_control_message(ControlMessage message)
{
    m_control_message_queue->enqueue(move(message));
}

Vector<ControlMessage> BaseAudioContext::drain_control_messages()
{
    return m_control_message_queue->drain();
}

// https://webaudio.github.io/web-audio-api/#dom-baseaudiocontext-decodeaudiodata
GC::Ref<WebIDL::Promise> BaseAudioContext::decode_audio_data(GC::Root<WebIDL::BufferSource> audio_data, GC::Ptr<WebIDL::CallbackType> success_callback, GC::Ptr<WebIDL::CallbackType> error_callback)
{
    auto& realm = this->realm();

    // FIXME: When decodeAudioData is called, the following steps MUST be performed on the control thread:

    // 1. If this's relevant global object's associated Document is not fully active then return a
    //    promise rejected with "InvalidStateError" DOMException.
    auto const& associated_document = as<HTML::Window>(HTML::relevant_global_object(*this)).associated_document();
    if (!associated_document.is_fully_active()) {
        auto error = WebIDL::InvalidStateError::create(realm, "The document is not fully active."_utf16);
        return WebIDL::create_rejected_promise_from_exception(realm, error);
    }

    // 2. Let promise be a new Promise.
    auto promise = WebIDL::create_promise(realm);

    // 3. If audioData is not detached, execute the following steps:
    if (!WebIDL::is_buffer_source_detached(JS::Value(*audio_data->raw_object()))) {
        // 3.1. Append promise to [[pending promises]].
        m_pending_promises.append(promise);

        // FIXME: 3.2. Detach the audioData ArrayBuffer. If this operations throws, jump to the step 3.

        // 3.3. Queue a decoding operation to be performed on another thread.
        queue_a_decoding_operation(promise, move(audio_data), success_callback, error_callback);
    }

    // 4. If audioData is detached, execute the following error steps:
    else {
        // 4.1. Let error be a DataCloneError.
        auto error = WebIDL::DataCloneError::create(realm, "Audio data is detached."_utf16);

        // 4.2. Reject promise with error, and remove it from [[pending promises]].
        WebIDL::reject_promise(realm, promise, error);
        m_pending_promises.remove_first_matching([&promise](auto& pending_promise) {
            return pending_promise == promise;
        });

        // 4.3. Queue a media element task to invoke errorCallback with error.
        if (error_callback) {
            queue_a_media_element_task(GC::create_function(heap(), [&realm, error_callback, error] {
                auto completion = WebIDL::invoke_callback(*error_callback, {}, { { error } });
                if (completion.is_abrupt())
                    HTML::report_exception(completion, realm);
            }));
        }
    }

    // 5. Return promise.
    return promise;
}

// https://webaudio.github.io/web-audio-api/#dom-baseaudiocontext-decodeaudiodata
void BaseAudioContext::queue_a_decoding_operation(GC::Ref<JS::PromiseCapability> promise, GC::Root<WebIDL::BufferSource> audio_data, GC::Ptr<WebIDL::CallbackType> success_callback, GC::Ptr<WebIDL::CallbackType> error_callback)
{
    auto reject_decode = [this, promise, error_callback](auto message) {
        queue_a_media_element_task(GC::create_function(heap(), [this, promise, error_callback, message] {
            auto& realm = this->realm();
            auto error = WebIDL::EncodingError::create(realm, message);
            WebIDL::reject_promise(realm, promise, error);
            m_pending_promises.remove_first_matching([&promise](auto& pending_promise) {
                return pending_promise == promise;
            });
            if (error_callback) {
                auto completion = WebIDL::invoke_callback(*error_callback, {}, { { error } });
                if (completion.is_abrupt())
                    HTML::report_exception(completion, realm);
            }
        }));
    };

    auto bytes_or_error = WebIDL::get_buffer_source_copy(*audio_data->raw_object());
    if (bytes_or_error.is_error() || bytes_or_error.value().is_empty()) {
        reject_decode("Audio data is detached or unavailable."_utf16);
        return;
    }

    auto decoded_or_error = decode_wave_pcm(bytes_or_error.value().bytes());
    if (decoded_or_error.is_error()) {
        reject_decode("Only valid RIFF/WAVE PCM audio can be decoded."_utf16);
        return;
    }

    auto decoded = decoded_or_error.release_value();
    auto output_rate = sample_rate() == 0 ? decoded.sample_rate : static_cast<u32>(sample_rate());
    u64 output_frames = decoded.frames;
    if (output_rate != decoded.sample_rate)
        output_frames = (static_cast<u64>(decoded.frames) * output_rate + decoded.sample_rate / 2) / decoded.sample_rate;
    if (output_frames == 0 || output_frames > NumericLimits<u32>::max()) {
        reject_decode("Decoded audio is outside the supported duration."_utf16);
        return;
    }

    auto buffer_or_error = create_buffer(decoded.channels, static_cast<u32>(output_frames), static_cast<float>(output_rate));
    if (buffer_or_error.is_exception()) {
        reject_decode("Unable to allocate the decoded audio buffer."_utf16);
        return;
    }
    auto buffer = buffer_or_error.release_value();
    for (u32 channel = 0; channel < decoded.channels; ++channel) {
        auto output_channel = MUST(buffer->get_channel_data(channel));
        for (u32 frame = 0; frame < output_frames; ++frame) {
            double source_position = static_cast<double>(frame) * decoded.sample_rate / output_rate;
            source_position = min(source_position, static_cast<double>(decoded.frames - 1));
            auto source_frame = static_cast<u32>(source_position);
            auto next_frame = min(source_frame + 1, decoded.frames - 1);
            auto interpolation = static_cast<float>(source_position - source_frame);
            auto first = decoded.samples[static_cast<size_t>(source_frame) * decoded.channels + channel];
            auto second = decoded.samples[static_cast<size_t>(next_frame) * decoded.channels + channel];
            output_channel->data()[frame] = first + (second - first) * interpolation;
        }
    }

    queue_a_media_element_task(GC::create_function(heap(), [this, promise, success_callback, buffer] {
        auto& realm = this->realm();
        WebIDL::resolve_promise(realm, promise, buffer);
        m_pending_promises.remove_first_matching([&promise](auto& pending_promise) {
            return pending_promise == promise;
        });
        if (success_callback) {
            auto completion = WebIDL::invoke_callback(*success_callback, {}, { { buffer } });
            if (completion.is_abrupt())
                HTML::report_exception(completion, realm);
        }
    }));
}

}
