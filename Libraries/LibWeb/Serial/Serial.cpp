/*
 * Copyright (c) 2025, Edwin Hoksberg <mail@edwinhoksberg.nl>
 *
 * SPDX-License-Identifier: BSD-2-Clause
 */

#include <LibCore/Timer.h>
#include <LibWeb/Bindings/Intrinsics.h>
#include <LibWeb/DOM/Event.h>
#include <LibWeb/Bindings/SerialPrototype.h>
#include <LibWeb/DOM/Document.h>
#include <LibJS/Runtime/Array.h>
#include <LibWeb/HTML/EventNames.h>
#include <LibWeb/HTML/Scripting/Environments.h>
#include <LibWeb/HTML/Scripting/TemporaryExecutionContext.h>
#include <LibWeb/HTML/Window.h>
#include <LibWeb/Platform/EventLoopPlugin.h>
#include <LibWeb/Serial/Serial.h>
#include <LibWeb/Serial/SerialPort.h>
#include <LibWeb/WebIDL/Promise.h>

#include "../../../../../public-base/libs/rinruntime/include/rinruntime/rin_web_serial_portal.h"

namespace Web::Serial {

GC_DEFINE_ALLOCATOR(Serial);

Serial::Serial(JS::Realm& realm)
    : DOM::EventTarget(realm)
{
}

void Serial::initialize(JS::Realm& realm)
{
    WEB_SET_PROTOTYPE_FOR_INTERFACE(Serial);
    Base::initialize(realm);

    auto self = GC::Ref { *this };
    m_portal_event_timer = Core::Timer::create_repeating(50, [self] {
        self->poll_portal_events();
    });
    m_portal_event_timer->start();
}

void Serial::visit_edges(Cell::Visitor& visitor)
{
    Base::visit_edges(visitor);
    for (auto& port : m_granted_ports)
        visitor.visit(port);
    visitor.visit(m_pending_request_port_promise);
}

void Serial::poll_portal_events()
{
    if (m_pending_request_port_promise &&
        m_pending_request_port_id != 0u &&
        m_pending_request_port_page_id != 0u) {
        RinWebSerialDeviceV1 selected_device {};
        const int result = rin_web_serial_request_port_poll(
            m_pending_request_port_page_id,
            m_pending_request_port_origin.c_str(),
            m_pending_request_port_id, &selected_device);
        if (result != RIN_SERIAL_EAGAIN) {
            auto promise = m_pending_request_port_promise;
            const std::string pending_origin = m_pending_request_port_origin;
            m_pending_request_port_promise = nullptr;
            m_pending_request_port_id = 0u;
            m_pending_request_port_page_id = 0u;
            m_pending_request_port_origin.clear();

            auto& request_realm = this->realm();
            auto* window = as_if<HTML::Window>(request_realm.global_object());
            if (result == RIN_SERIAL_OK) {
                std::string current_origin;
                if (window != nullptr) {
                    auto serialized = window->associated_document().origin()
                                          .serialize().to_byte_string();
                    current_origin.assign(serialized.characters(),
                                          serialized.length());
                }
                if (window == nullptr ||
                    !window->associated_document().is_fully_active() ||
                    !window->associated_document().is_allowed_to_use_feature(
                        DOM::PolicyControlledFeature::WebSerial) ||
                    current_origin != pending_origin) {
                    WebIDL::reject_promise(request_realm, *promise,
                        WebIDL::NotAllowedError::create(
                            request_realm,
                            "Serial request document is no longer authorized"_utf16));
                } else {
                    auto port = request_realm.create<SerialPort>(request_realm);
                    port->set_backend_device(selected_device);
                    m_granted_ports.append(port);
                    WebIDL::resolve_promise(request_realm, *promise,
                                            JS::Value(port.ptr()));
                }
            } else if (result == RIN_SERIAL_EPERM ||
                       result == RIN_SERIAL_EBADF) {
                WebIDL::reject_promise(request_realm, *promise,
                    WebIDL::NotAllowedError::create(
                        request_realm, "Serial device selection was cancelled or denied"_utf16));
            } else if (result == RIN_SERIAL_ENODEV) {
                WebIDL::reject_promise(request_realm, *promise,
                    WebIDL::NotFoundError::create(
                        request_realm, "Selected serial device is no longer available"_utf16));
            } else {
                WebIDL::reject_promise(request_realm, *promise,
                    WebIDL::NetworkError::create(
                        request_realm, "Serial device chooser transport failed"_utf16));
            }
        }
    }

    /* The portal client owns the authenticated snapshot and returns at most
     * one transition per poll.  Keep the Web Serial event delivery on the
     * Ladybird event-loop timer so no portal/socket callback runs user code
     * inline. */
    for (size_t index = 0; index < 8; ++index) {
        RinWebSerialEventV1 portal_event {};
        auto result = rin_web_serial_poll_event(&portal_event);
        if (result == RIN_SERIAL_EAGAIN || result == RIN_SERIAL_ENOTSUP)
            return;
        if (result != RIN_SERIAL_OK)
            return;

        for (auto& port : m_granted_ports)
            port->handle_portal_event(portal_event);

        dispatch_event(DOM::Event::create(
            realm(), portal_event.connected != 0u
                ? HTML::EventNames::connect : HTML::EventNames::disconnect));
    }
}

// https://wicg.github.io/serial/#requestport-method
WebIDL::ExceptionOr<GC::Ref<WebIDL::Promise>> Serial::request_port(SerialPortRequestOptions const options)
{
    auto& realm = this->realm();
    if (HTML::is_non_secure_context(HTML::relevant_settings_object(*this)))
        return WebIDL::create_rejected_promise_from_exception(realm,
            WebIDL::SecurityError::create(realm, "Web Serial requires a secure context"_utf16));
    auto* window = as_if<HTML::Window>(realm.global_object());
    if (window == nullptr ||
        !window->associated_document().is_allowed_to_use_feature(
            DOM::PolicyControlledFeature::WebSerial))
        return WebIDL::create_rejected_promise_from_exception(realm,
            WebIDL::NotAllowedError::create(
                realm, "The serial feature is disabled by Permissions Policy"_utf16));
    if (!is<HTML::Window>(realm.global_object()) ||
        !as<HTML::Window>(realm.global_object()).has_transient_activation())
        return WebIDL::create_rejected_promise_from_exception(realm,
            WebIDL::SecurityError::create(realm, "Web Serial requires transient user activation"_utf16));
    if (options.allowed_bluetooth_service_class_ids.has_value() &&
        !options.allowed_bluetooth_service_class_ids->is_empty())
        return WebIDL::create_rejected_promise_from_exception(realm,
            WebIDL::NotSupportedError::create(realm, "Bluetooth serial is not supported"_utf16));
    if (options.filters.has_value()) {
        if (options.filters->is_empty() || options.filters->size() > 16u)
            return WebIDL::create_rejected_promise_from_exception(realm,
                JS::throw_completion(JS::TypeError::create(realm, "Serial filters must not be empty or oversized"sv)));
        for (auto const& filter : *options.filters) {
            if (!filter.usb_vendor_id.has_value() && !filter.usb_product_id.has_value() &&
                !filter.bluetooth_service_class_id.has_value())
                return WebIDL::create_rejected_promise_from_exception(realm,
                    JS::throw_completion(JS::TypeError::create(realm, "A serial filter must select a device"sv)));
            if (filter.bluetooth_service_class_id.has_value())
                return WebIDL::create_rejected_promise_from_exception(realm,
                    WebIDL::NotSupportedError::create(realm, "Bluetooth serial is not supported"_utf16));
        }
    }
    if (m_pending_request_port_promise)
        return WebIDL::create_rejected_promise_from_exception(realm,
            WebIDL::InvalidStateError::create(
                realm, "A serial-device chooser is already open"_utf16));

    RinSerialPortalFilterV1 portal_filters[RIN_SERIAL_PORTAL_MAX_FILTERS] {};
    uint32_t filter_count = 0u;
    if (options.filters.has_value()) {
        for (auto const& filter : *options.filters) {
            auto& portal_filter = portal_filters[filter_count++];
            if (filter.usb_vendor_id.has_value()) {
                portal_filter.has_vendor_id = 1u;
                portal_filter.vendor_id = *filter.usb_vendor_id;
            }
            if (filter.usb_product_id.has_value()) {
                portal_filter.has_product_id = 1u;
                portal_filter.product_id = *filter.usb_product_id;
            }
        }
    }

    if (window == nullptr || !window->associated_document().is_fully_active())
        return WebIDL::create_rejected_promise_from_exception(realm,
            WebIDL::InvalidStateError::create(
                realm, "Serial request document is not fully active"_utf16));
    const uint64_t page_id = window->page().client().id();
    if (page_id == 0u || page_id > UINT32_MAX)
        return WebIDL::create_rejected_promise_from_exception(realm,
            WebIDL::NotAllowedError::create(
                realm, "Serial request has no authenticated page identity"_utf16));
    auto origin = window->associated_document().origin().serialize().to_byte_string();
    const std::string request_origin(origin.characters(), origin.length());
    uint64_t request_id = 0u;
    const int result = rin_web_serial_request_port_begin(
        static_cast<uint32_t>(page_id), request_origin.c_str(), 1u,
        portal_filters, filter_count, &request_id);
    if (result != RIN_SERIAL_EAGAIN || request_id == 0u) {
        if (result == RIN_SERIAL_EPERM)
            return WebIDL::create_rejected_promise_from_exception(realm,
                WebIDL::NotAllowedError::create(
                    realm, "Browser denied the serial-device chooser"_utf16));
        return WebIDL::create_rejected_promise_from_exception(realm,
            WebIDL::NetworkError::create(
                realm, "Serial device chooser could not be started"_utf16));
    }

    auto promise = WebIDL::create_promise(realm);
    m_pending_request_port_promise = promise;
    m_pending_request_port_origin = request_origin;
    m_pending_request_port_id = request_id;
    m_pending_request_port_page_id = static_cast<uint32_t>(page_id);
    return promise;
}

// https://wicg.github.io/serial/#getports-method
GC::Ref<WebIDL::Promise> Serial::get_ports()
{
    auto& realm = this->realm();
    if (HTML::is_non_secure_context(HTML::relevant_settings_object(*this)))
        return WebIDL::create_rejected_promise_from_exception(realm,
            WebIDL::SecurityError::create(realm, "Web Serial requires a secure context"_utf16));
    auto* window = as_if<HTML::Window>(realm.global_object());
    if (window == nullptr ||
        !window->associated_document().is_allowed_to_use_feature(
            DOM::PolicyControlledFeature::WebSerial))
        return WebIDL::create_rejected_promise_from_exception(realm,
            WebIDL::NotAllowedError::create(
                realm, "The serial feature is disabled by Permissions Policy"_utf16));
    if (window == nullptr || window->page().client().id() == 0u ||
        window->page().client().id() > UINT32_MAX)
        return WebIDL::create_rejected_promise_from_exception(realm,
            WebIDL::NotAllowedError::create(
                realm, "Serial request has no authenticated page identity"_utf16));
    auto origin = HTML::relevant_settings_object(*this).origin().serialize().to_byte_string();
    RinWebSerialDeviceV1 devices[RIN_WEB_SERIAL_MAX_DEVICES] {};
    uint32_t count = 0u;
    if (rin_web_serial_get_ports_for_page(
            static_cast<uint32_t>(window->page().client().id()),
            origin.characters(), devices, RIN_WEB_SERIAL_MAX_DEVICES, &count) !=
        RIN_SERIAL_OK)
        return WebIDL::create_rejected_promise_from_exception(realm,
            WebIDL::NetworkError::create(realm,
                "Serial permission lookup failed"_utf16));
    m_granted_ports.clear();
    GC::RootVector<JS::Value> values(realm.heap());
    values.ensure_capacity(count);
    for (uint32_t index = 0u; index < count; ++index) {
        auto port = realm.create<SerialPort>(realm);
        port->set_backend_device(devices[index]);
        m_granted_ports.append(port);
        values.append(JS::Value(port.ptr()));
    }
    return WebIDL::create_resolved_promise(realm,
        JS::Array::create_from(realm, values.span()));
}

// https://wicg.github.io/serial/#onconnect-attribute
void Serial::set_onconnect(WebIDL::CallbackType* event_handler)
{
    set_event_handler_attribute(HTML::EventNames::connect, event_handler);
}

WebIDL::CallbackType* Serial::onconnect()
{
    return event_handler_attribute(HTML::EventNames::connect);
}

// https://wicg.github.io/serial/#ondisconnect-attribute
void Serial::set_ondisconnect(WebIDL::CallbackType* event_handler)
{
    set_event_handler_attribute(HTML::EventNames::disconnect, event_handler);
}

WebIDL::CallbackType* Serial::ondisconnect()
{
    return event_handler_attribute(HTML::EventNames::disconnect);
}

}
