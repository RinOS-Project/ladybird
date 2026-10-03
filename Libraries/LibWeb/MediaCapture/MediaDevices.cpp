/*
 * Copyright (c) 2026, RinOS contributors
 *
 * SPDX-License-Identifier: BSD-2-Clause
 */

#include <LibJS/Runtime/Error.h>
#include <LibWeb/Bindings/Intrinsics.h>
#include <LibWeb/Crypto/Crypto.h>
#include <LibWeb/DOM/Document.h>
#include <LibWeb/HTML/BrowsingContext.h>
#include <LibWeb/HTML/Scripting/Environments.h>
#include <LibWeb/HTML/Window.h>
#include <LibWeb/MediaCapture/MediaDevices.h>
#include <LibWeb/Page/Page.h>
#include <LibWeb/WebIDL/DOMException.h>
#include <LibWeb/WebIDL/Promise.h>

#include <rinruntime/rin_audio_service_client.h>

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

static WebIDL::ExceptionOr<GC::Ref<MediaStream>> start_audio_capture(
    JS::Realm& realm, Web::PageClient& page_client, URL::Origin const& origin)
{
    RinAudioServiceDeviceListV1 devices {};
    if (rin_audio_service_client_devices(&devices) != 0)
        return WebIDL::NotAllowedError::create(realm, "Audio Service access was denied"_utf16);

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
        return WebIDL::NotFoundError::create(realm, "No permitted audio input is available"_utf16);

    auto audio_stream = rin_audio_service_capture_stream_create_for_device(
        48000u, 1u, RIN_AUDIO_SERVICE_FORMAT_S16LE,
        RIN_AUDIO_SERVICE_MIN_RING_FRAMES, selected_input_device);
    if (audio_stream < 0)
        return WebIDL::NotAllowedError::create(realm, "Audio capture policy rejected the request"_utf16);

    if (rin_audio_service_stream_start(audio_stream) != 0) {
        (void)rin_audio_service_stream_destroy(audio_stream);
        return WebIDL::NotAllowedError::create(realm, "Audio capture could not be started"_utf16);
    }

    auto track_id = Crypto::generate_random_uuid();
    auto track = MediaStreamTrack::create(realm, audio_stream, move(track_id), "RinOS Audio Input"_string);
    if (!page_client.page_did_create_microphone_track(*track, origin)) {
        track->stop();
        return WebIDL::NotAllowedError::create(
            realm, "Microphone capture could not be registered for revocation"_utf16);
    }
    return MediaStream::create(realm, track);
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

    auto* window = as_if<HTML::Window>(realm.global_object());
    if (window == nullptr || window->browsing_context() == nullptr)
        return WebIDL::create_rejected_promise_from_exception(realm,
            WebIDL::NotAllowedError::create(realm, "Microphone permission requires an active browsing context"_utf16));
    auto& document = window->associated_document();
    if (!document.is_fully_active())
        return WebIDL::create_rejected_promise_from_exception(realm,
            WebIDL::InvalidStateError::create(realm, "Document is not fully active"_utf16));
    if (!document.is_allowed_to_use_feature(DOM::PolicyControlledFeature::Microphone))
        return WebIDL::create_rejected_promise_from_exception(realm,
            WebIDL::NotAllowedError::create(realm, "Permissions Policy blocks microphone capture"_utf16));

    GC::Ptr<HTML::Window> request_window = window;
    auto request_origin = document.origin();
    auto permission_promise = WebIDL::create_promise(realm);
    window->page().client().page_did_request_microphone_permission(
        *permission_promise, document);
    return WebIDL::upon_fulfillment(*permission_promise,
        GC::create_function(realm.heap(), [this, request_window, request_origin](JS::Value allowed) -> WebIDL::ExceptionOr<JS::Value> {
            auto& permission_realm = this->realm();
            if (!allowed.is_boolean() || !allowed.as_bool())
                return JS::throw_completion(WebIDL::NotAllowedError::create(
                    permission_realm, "Microphone permission was denied"_utf16));

            if (!request_window ||
                !request_window->associated_document().is_fully_active() ||
                !request_window->associated_document().is_allowed_to_use_feature(
                    DOM::PolicyControlledFeature::Microphone) ||
                !request_window->associated_document().origin().is_same_origin(request_origin))
                return JS::throw_completion(WebIDL::NotAllowedError::create(
                    permission_realm, "Microphone request document is no longer authorized"_utf16));

            auto stream_or_error = start_audio_capture(
                permission_realm, request_window->page().client(), request_origin);
            if (stream_or_error.is_error())
                return stream_or_error.release_error();
            return JS::Value(stream_or_error.release_value().ptr());
        }));
}

}
