/*
 * Copyright (c) 2025, Edwin Hoksberg <mail@edwinhoksberg.nl>
 *
 * SPDX-License-Identifier: BSD-2-Clause
 */

#include <LibWeb/Bindings/Intrinsics.h>
#include <LibWeb/HTML/EventNames.h>
#include <LibWeb/Serial/SerialPort.h>
#include <LibWeb/WebIDL/Promise.h>
#include <LibJS/Runtime/Object.h>

#include "../../../../../src/apps/common/rin_web_serial_portal.h"

namespace Web::Serial {

SerialPort::SerialPort(JS::Realm& realm)
    : DOM::EventTarget(realm)
{
}

void SerialPort::initialize(JS::Realm& realm)
{
    WEB_SET_PROTOTYPE_FOR_INTERFACE(SerialPort);
    Base::initialize(realm);
}

void SerialPort::set_backend_device(RinWebSerialDeviceV1 const& device)
{
    m_device = device;
    m_have_device = true;
}

SerialPortInfo SerialPort::get_info() const
{
    SerialPortInfo info;
    if (!m_have_device) return info;
    if (m_device.info.vendor_id != 0u) info.usb_vendor_id = m_device.info.vendor_id;
    if (m_device.info.product_id != 0u) info.usb_product_id = m_device.info.product_id;
    return info;
}

GC::Ref<WebIDL::Promise> SerialPort::open(SerialOptions options)
{
    auto& realm = this->realm();
    if (m_state != SerialPortState::Closed)
        return WebIDL::create_rejected_promise_from_exception(realm,
            WebIDL::InvalidStateError::create(realm, "Serial port is not closed"_utf16));
    auto data_bits = options.data_bits.value_or(8u);
    auto stop_bits = options.stop_bits.value_or(1u);
    auto buffer_size = options.buffer_size.value_or(255u);
    auto baud_rate = options.baud_rate.value_or(9600u);
    if (data_bits != 7u && data_bits != 8u)
        return WebIDL::create_rejected_promise_from_exception(realm,
            JS::throw_completion(JS::TypeError::create(realm, "dataBits must be 7 or 8"sv)));
    if (stop_bits != 1u && stop_bits != 2u)
        return WebIDL::create_rejected_promise_from_exception(realm,
            JS::throw_completion(JS::TypeError::create(realm, "stopBits must be 1 or 2"sv)));
    if (buffer_size == 0u || buffer_size > 4096u || baud_rate == 0u || baud_rate > 4000000u)
        return WebIDL::create_rejected_promise_from_exception(realm,
            JS::throw_completion(JS::TypeError::create(realm, "Serial options exceed the bounded backend limits"sv)));
    if (options.flow_control.value_or(Bindings::FlowControlType::None) == Bindings::FlowControlType::Hardware)
        return WebIDL::create_rejected_promise_from_exception(realm,
            WebIDL::NotSupportedError::create(realm, "Hardware flow control is unavailable"_utf16));
    if (!m_have_device)
        return WebIDL::create_rejected_promise_from_exception(realm,
            WebIDL::NotFoundError::create(realm, "Serial device is no longer available"_utf16));
    auto parity = options.parity.value_or(Bindings::ParityType::None);
    auto flow_control = options.flow_control.value_or(Bindings::FlowControlType::None);
    uint8_t parity_value = parity == Bindings::ParityType::Odd
        ? RIN_SERIAL_PARITY_ODD
        : parity == Bindings::ParityType::Even ? RIN_SERIAL_PARITY_EVEN
                                               : RIN_SERIAL_PARITY_NONE;
    uint8_t flow_value = flow_control == Bindings::FlowControlType::Hardware
        ? RIN_SERIAL_FLOW_HARDWARE : RIN_SERIAL_FLOW_NONE;
    m_buffer_size = buffer_size;
    m_state = SerialPortState::Opening;
    int result = rin_web_serial_open(m_device.capability, baud_rate, data_bits,
                                     stop_bits, parity_value, flow_value,
                                     buffer_size, &m_handle);
    if (result != RIN_SERIAL_OK) {
        m_handle = 0u;
        m_state = SerialPortState::Closed;
        m_connected = false;
        if (result == RIN_SERIAL_ENOTSUP)
            return WebIDL::create_rejected_promise_from_exception(realm,
                WebIDL::NotSupportedError::create(realm, "Requested serial configuration is not supported"_utf16));
        return WebIDL::create_rejected_promise_from_exception(realm,
            WebIDL::NetworkError::create(realm, "Serial device could not be opened"_utf16));
    }
    m_connected = true;
    m_state = SerialPortState::Opened;
    return WebIDL::create_resolved_promise(realm, JS::js_undefined());
}

GC::Ref<WebIDL::Promise> SerialPort::set_signals(SerialOutputSignals signals)
{
    auto& realm = this->realm();
    if (m_state != SerialPortState::Opened)
        return WebIDL::create_rejected_promise_from_exception(realm,
            WebIDL::InvalidStateError::create(realm, "Serial port is not open"_utf16));
    if (!signals.data_terminal_ready.has_value() && !signals.request_to_send.has_value() &&
        !signals.break_.has_value())
        return WebIDL::create_resolved_promise(realm, JS::js_undefined());
    int result = rin_web_serial_set_signals(
        m_handle, signals.data_terminal_ready.value_or(false),
        signals.request_to_send.value_or(false), signals.break_.value_or(false));
    if (result != RIN_SERIAL_OK)
        return WebIDL::create_rejected_promise_from_exception(realm,
            result == RIN_SERIAL_ENOTSUP
                ? WebIDL::NotSupportedError::create(realm, "Serial signals are not supported"_utf16)
                : WebIDL::NetworkError::create(realm, "Serial signal update failed"_utf16));
    return WebIDL::create_resolved_promise(realm, JS::js_undefined());
}

GC::Ref<WebIDL::Promise> SerialPort::get_signals() const
{
    auto& realm = this->realm();
    if (m_state != SerialPortState::Opened)
        return WebIDL::create_rejected_promise_from_exception(realm,
            WebIDL::InvalidStateError::create(realm, "Serial port is not open"_utf16));
    RinSerialStatusV1 status {};
    if (rin_web_serial_get_signals(m_handle, &status) != RIN_SERIAL_OK)
        return WebIDL::create_rejected_promise_from_exception(realm,
            WebIDL::NetworkError::create(realm, "Serial signal query failed"_utf16));
    auto signals = JS::Object::create(realm, realm.intrinsics().object_prototype());
    MUST(signals->create_data_property("dataCarrierDetect"_utf16_fly_string,
                                       JS::Value(status.dcd != 0u)));
    MUST(signals->create_data_property("clearToSend"_utf16_fly_string,
                                       JS::Value(status.cts != 0u)));
    MUST(signals->create_data_property("ringIndicator"_utf16_fly_string,
                                       JS::Value(status.ri != 0u)));
    MUST(signals->create_data_property("dataSetReady"_utf16_fly_string,
                                       JS::Value(status.dsr != 0u)));
    return WebIDL::create_resolved_promise(realm, signals);
}

GC::Ref<WebIDL::Promise> SerialPort::close()
{
    auto& realm = this->realm();
    if (m_state == SerialPortState::Closed || m_state == SerialPortState::Forgotten)
        return WebIDL::create_rejected_promise_from_exception(realm,
            WebIDL::InvalidStateError::create(realm, "Serial port is already closed"_utf16));
    m_state = SerialPortState::Closing;
    if (m_handle != 0u) {
        (void)rin_web_serial_close(m_handle);
        m_handle = 0u;
    }
    m_readable = nullptr;
    m_writable = nullptr;
    m_read_fatal = false;
    m_write_fatal = false;
    m_connected = false;
    m_state = SerialPortState::Closed;
    return WebIDL::create_resolved_promise(realm, JS::js_undefined());
}

GC::Ref<WebIDL::Promise> SerialPort::forget()
{
    auto& realm = this->realm();
    if (m_state == SerialPortState::Opened || m_state == SerialPortState::Opening ||
        m_state == SerialPortState::Closing)
        return WebIDL::create_rejected_promise_from_exception(realm,
            WebIDL::InvalidStateError::create(realm, "Close the serial port before forgetting it"_utf16));
    m_state = SerialPortState::Forgetting;
    m_connected = false;
    m_state = SerialPortState::Forgotten;
    return WebIDL::create_resolved_promise(realm, JS::js_undefined());
}

void SerialPort::visit_edges(Cell::Visitor& visitor)
{
    Base::visit_edges(visitor);
    visitor.visit(m_readable);
    visitor.visit(m_writable);
    visitor.visit(m_pending_close_promise);
}

void SerialPort::set_onconnect(WebIDL::CallbackType* event_handler)
{
    set_event_handler_attribute(HTML::EventNames::connect, event_handler);
}

WebIDL::CallbackType* SerialPort::onconnect()
{
    return event_handler_attribute(HTML::EventNames::connect);
}

void SerialPort::set_ondisconnect(WebIDL::CallbackType* event_handler)
{
    set_event_handler_attribute(HTML::EventNames::disconnect, event_handler);
}

WebIDL::CallbackType* SerialPort::ondisconnect()
{
    return event_handler_attribute(HTML::EventNames::disconnect);
}

}
