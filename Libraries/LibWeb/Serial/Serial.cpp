/*
 * Copyright (c) 2025, Edwin Hoksberg <mail@edwinhoksberg.nl>
 *
 * SPDX-License-Identifier: BSD-2-Clause
 */

#include <LibWeb/Bindings/Intrinsics.h>
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

#include "../../../../../src/apps/common/rin_web_serial_portal.h"

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
}

void Serial::visit_edges(Cell::Visitor& visitor)
{
    Base::visit_edges(visitor);
    for (auto& port : m_granted_ports)
        visitor.visit(port);
}

static bool serial_filter_matches(RinWebSerialDeviceV1 const& device,
                                  SerialPortRequestOptions const& options)
{
    if (!options.filters.has_value()) return true;
    for (auto const& filter : *options.filters) {
        if (filter.usb_vendor_id.has_value() &&
            device.info.vendor_id != *filter.usb_vendor_id)
            continue;
        if (filter.usb_product_id.has_value() &&
            device.info.product_id != *filter.usb_product_id)
            continue;
        if (filter.bluetooth_service_class_id.has_value()) continue;
        return true;
    }
    return false;
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
    RinWebSerialDeviceV1 devices[RIN_WEB_SERIAL_MAX_DEVICES] {};
    uint32_t count = 0u;
    int result = rin_web_serial_enumerate(devices, RIN_WEB_SERIAL_MAX_DEVICES,
                                          &count);
    if (result != RIN_SERIAL_OK && result != RIN_SERIAL_EOVERFLOW)
        return WebIDL::create_rejected_promise_from_exception(realm,
            WebIDL::NetworkError::create(realm, "Serial device enumeration failed"_utf16));
    for (uint32_t index = 0u; index < count && index < RIN_WEB_SERIAL_MAX_DEVICES;
         ++index) {
        if (!serial_filter_matches(devices[index], options)) continue;
        // Allocate through the realm so this remains compatible with LibJS
        // versions where platform objects inherit Object::create(Realm&, Object*).
        auto port = realm.create<SerialPort>(realm);
        port->set_backend_device(devices[index]);
        m_granted_ports.append(port);
        return WebIDL::create_resolved_promise(realm, port);
    }
    return WebIDL::create_rejected_promise_from_exception(realm,
        WebIDL::NotFoundError::create(realm, "No permitted serial device is available"_utf16));
}

// https://wicg.github.io/serial/#getports-method
GC::Ref<WebIDL::Promise> Serial::get_ports()
{
    auto& realm = this->realm();
    if (HTML::is_non_secure_context(HTML::relevant_settings_object(*this)))
        return WebIDL::create_rejected_promise_from_exception(realm,
            WebIDL::SecurityError::create(realm, "Web Serial requires a secure context"_utf16));
    // Array::create_from accepts JS::Value spans (or a span with a mapping
    // callback), not a vector of GC references. Convert each port explicitly
    // while the helper keeps the resulting values rooted during allocation.
    auto ports = JS::Array::create_from(
        realm, m_granted_ports.span(), [](GC::Ref<SerialPort> const& port) {
            return JS::Value(port.ptr());
        });
    return WebIDL::create_resolved_promise(realm, ports);
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
