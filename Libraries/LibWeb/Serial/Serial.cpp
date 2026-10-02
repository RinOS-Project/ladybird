/*
 * Copyright (c) 2025, Edwin Hoksberg <mail@edwinhoksberg.nl>
 *
 * SPDX-License-Identifier: BSD-2-Clause
 */

#include <LibCore/Timer.h>
#include <LibWeb/Bindings/Intrinsics.h>
#include <LibWeb/DOM/Event.h>
#include <LibWeb/Bindings/SerialPrototype.h>
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
}

void Serial::poll_portal_events()
{
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

    /* Never let the renderer choose the first matching device as a substitute
     * for the missing Browser-owned trusted chooser.  The Browser must return
     * an explicitly selected object before requestPort() can grant access. */
    return WebIDL::create_rejected_promise_from_exception(realm,
        WebIDL::NotAllowedError::create(realm,
            "The Browser serial-device chooser is unavailable"_utf16));
}

// https://wicg.github.io/serial/#getports-method
GC::Ref<WebIDL::Promise> Serial::get_ports()
{
    auto& realm = this->realm();
    if (HTML::is_non_secure_context(HTML::relevant_settings_object(*this)))
        return WebIDL::create_rejected_promise_from_exception(realm,
            WebIDL::SecurityError::create(realm, "Web Serial requires a secure context"_utf16));
    auto origin = HTML::relevant_settings_object(*this).origin().serialize().to_byte_string();
    RinWebSerialDeviceV1 devices[RIN_WEB_SERIAL_MAX_DEVICES] {};
    uint32_t count = 0u;
    if (rin_web_serial_get_ports(origin.characters(), devices,
                                 RIN_WEB_SERIAL_MAX_DEVICES, &count) !=
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
