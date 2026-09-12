/*
 * Copyright (c) 2026, RinOS contributors
 *
 * SPDX-License-Identifier: BSD-2-Clause
 */

#include <LibWeb/Bindings/Intrinsics.h>
#include <LibWeb/HTML/Scripting/Environments.h>
#include <LibWeb/MediaCapture/MediaDevices.h>
#include <LibWeb/WebIDL/DOMException.h>
#include <LibWeb/WebIDL/Promise.h>

#include "../../../../../libs/rinruntime/include/rinruntime/rin_audio_service_client.h"

namespace Web::MediaCapture {

GC_DEFINE_ALLOCATOR(MediaDevices);

MediaDevices::MediaDevices(JS::Realm& realm)
    : DOM::EventTarget(realm)
{
}

void MediaDevices::initialize(JS::Realm& realm)
{
    WEB_SET_PROTOTYPE_FOR_INTERFACE(MediaDevices);
    Base::initialize(realm);
}

GC::Ref<WebIDL::Promise> MediaDevices::get_user_media(MediaStreamConstraints const& constraints)
{
    auto& realm = this->realm();

    if (HTML::is_non_secure_context(HTML::relevant_settings_object(*this)))
        return WebIDL::create_rejected_promise_from_exception(realm,
            WebIDL::SecurityError::create(realm, "getUserMedia requires a secure context"_utf16));

    if (!constraints.audio && !constraints.video)
        return WebIDL::create_rejected_promise_from_exception(realm,
            JS::throw_completion(JS::TypeError::create(realm, "At least one media constraint must be requested"sv)));

    if (constraints.video)
        return WebIDL::create_rejected_promise_from_exception(realm,
            WebIDL::NotSupportedError::create(realm, "RinOS camera capture is not implemented"_utf16));

    RinAudioServiceDeviceListV1 devices {};
    if (rin_audio_service_client_devices(&devices) != 0)
        return WebIDL::create_rejected_promise_from_exception(realm,
            WebIDL::NotAllowedError::create(realm, "Audio Service access was denied"_utf16));

    uint64_t selected_input_device = 0;
    bool has_available_input = false;
    for (uint32_t index = 0; index < devices.device_count; ++index) {
        auto const& device = devices.devices[index];
        if (device.kind == RIN_MEDIA_AUDIO_DEVICE_INPUT && device.available != 0) {
            has_available_input = true;
            if (selected_input_device == 0)
                selected_input_device = device.device_id;
            if (index == devices.default_input_index) {
                selected_input_device = device.device_id;
                break;
            }
        }
    }
    if (!has_available_input)
        return WebIDL::create_rejected_promise_from_exception(realm,
            WebIDL::NotFoundError::create(realm, "No permitted audio input is available"_utf16));

    auto audio_stream = rin_audio_service_capture_stream_create_for_device(
        48000u, 1u, RIN_AUDIO_SERVICE_FORMAT_S16LE,
        RIN_AUDIO_SERVICE_MIN_RING_FRAMES, selected_input_device);
    if (audio_stream < 0)
        return WebIDL::create_rejected_promise_from_exception(realm,
            WebIDL::NotAllowedError::create(realm, "Audio capture policy rejected the request"_utf16));

    if (rin_audio_service_stream_start(audio_stream) != 0) {
        (void)rin_audio_service_stream_destroy(audio_stream);
        return WebIDL::create_rejected_promise_from_exception(realm,
            WebIDL::NotAllowedError::create(realm, "Audio capture could not be started"_utf16));
    }

    auto track_id = MUST(String::formatted("rinos-audio-track-{}", audio_stream));
    auto track = MediaStreamTrack::create(realm, audio_stream, move(track_id), "RinOS Audio Input"_string);
    auto stream = MediaStream::create(realm, track);
    return WebIDL::create_resolved_promise(realm, stream);
}

}
