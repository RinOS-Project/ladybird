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
                JS::TypeError::create(realm, "Serial filters must not be empty or oversized"sv));
        for (auto const& filter : *options.filters) {
            if (!filter.usb_vendor_id.has_value() && !filter.usb_product_id.has_value() &&
                !filter.bluetooth_service_class_id.has_value())
                return WebIDL::create_rejected_promise_from_exception(realm,
                    JS::TypeError::create(realm, "A serial filter must select a device"sv));
            if (filter.bluetooth_service_class_id.has_value())
                return WebIDL::create_rejected_promise_from_exception(realm,
                    WebIDL::NotSupportedError::create(realm, "Bluetooth serial is not supported"_utf16));
        }
    }
    /* The authenticated RinOS device portal is the only source of physical
     * ports.  Until that portal is attached to this WebContent instance,
     * expose the specified Web Serial failure instead of inventing a port or
     * leaking a path string. */
    return WebIDL::create_rejected_promise_from_exception(realm,
        WebIDL::NotFoundError::create(realm, "No serial device is available"_utf16));
}

// https://wicg.github.io/serial/#getports-method
GC::Ref<WebIDL::Promise> Serial::get_ports()
{
    auto& realm = this->realm();
    if (HTML::is_non_secure_context(HTML::relevant_settings_object(*this)))
        return WebIDL::create_rejected_promise_from_exception(realm,
            WebIDL::SecurityError::create(realm, "Web Serial requires a secure context"_utf16));
    return WebIDL::create_resolved_promise(realm, JS::Array::create(realm, 0));
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
