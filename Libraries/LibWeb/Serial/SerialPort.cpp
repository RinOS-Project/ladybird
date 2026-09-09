/*
 * Copyright (c) 2025, Edwin Hoksberg <mail@edwinhoksberg.nl>
 *
 * SPDX-License-Identifier: BSD-2-Clause
 */

#include <LibWeb/Bindings/Intrinsics.h>
#include <LibWeb/HTML/EventNames.h>
#include <LibWeb/Serial/SerialPort.h>
#include <LibWeb/WebIDL/Promise.h>

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

SerialPortInfo SerialPort::get_info() const
{
    /* Device metadata is populated only by the authenticated RinOS device
     * portal.  An unavailable physical backend therefore returns an empty,
     * non-authoritative info dictionary rather than a path or guessed IDs. */
    return {};
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
    m_buffer_size = buffer_size;
    m_state = SerialPortState::Opening;
    m_state = SerialPortState::Closed;
    m_connected = false;
    return WebIDL::create_rejected_promise_from_exception(realm,
        WebIDL::NetworkError::create(realm, "The RinOS serial device portal is unavailable"_utf16));
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
    return WebIDL::create_rejected_promise_from_exception(realm,
        WebIDL::NetworkError::create(realm, "The RinOS serial device portal is unavailable"_utf16));
}

GC::Ref<WebIDL::Promise> SerialPort::get_signals() const
{
    auto& realm = this->realm();
    if (m_state != SerialPortState::Opened)
        return WebIDL::create_rejected_promise_from_exception(realm,
            WebIDL::InvalidStateError::create(realm, "Serial port is not open"_utf16));
    return WebIDL::create_rejected_promise_from_exception(realm,
        WebIDL::NetworkError::create(realm, "The RinOS serial device portal is unavailable"_utf16));
}

GC::Ref<WebIDL::Promise> SerialPort::close()
{
    auto& realm = this->realm();
    if (m_state == SerialPortState::Closed || m_state == SerialPortState::Forgotten)
        return WebIDL::create_rejected_promise_from_exception(realm,
            WebIDL::InvalidStateError::create(realm, "Serial port is already closed"_utf16));
    m_state = SerialPortState::Closing;
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
