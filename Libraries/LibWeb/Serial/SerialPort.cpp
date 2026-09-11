/*
 * Copyright (c) 2025, Edwin Hoksberg <mail@edwinhoksberg.nl>
 *
 * SPDX-License-Identifier: BSD-2-Clause
 */

#include <AK/ByteBuffer.h>
#include <LibCore/Timer.h>
#include <LibWeb/Bindings/Intrinsics.h>
#include <LibWeb/HTML/EventNames.h>
#include <LibWeb/Serial/SerialPort.h>
#include <LibWeb/Streams/ReadableStreamOperations.h>
#include <LibWeb/Streams/WritableStreamOperations.h>
#include <LibWeb/WebIDL/AbstractOperations.h>
#include <LibWeb/WebIDL/Promise.h>
#include <LibJS/Runtime/Object.h>

#include "../../../../../src/apps/common/rin_web_serial_portal.h"

namespace Web::Serial {

SerialPort::SerialPort(JS::Realm& realm)
    : DOM::EventTarget(realm)
{
}

GC::Ref<SerialPort> SerialPort::create(JS::Realm& realm)
{
    return realm.create<SerialPort>(realm);
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

    auto self = GC::Ref { *this };
    auto readable = realm.create<Streams::ReadableStream>(realm);
    auto readable_pull = GC::create_function(realm.heap(), [self, readable]() {
        auto& stream_realm = self->realm();
        auto buffer_or_error = ByteBuffer::create_uninitialized(self->m_buffer_size);
        if (buffer_or_error.is_error())
            return WebIDL::create_rejected_promise(stream_realm,
                JS::TypeError::create(stream_realm, "Unable to allocate the serial read buffer"sv));

        auto buffer = buffer_or_error.release_value();
        size_t count = 0;
        auto read_result = rin_web_serial_read(self->m_handle, buffer.data(), buffer.size(), &count);
        if (read_result == RIN_SERIAL_EAGAIN) {
            if (!self->m_read_poll_timer) {
                self->m_read_poll_timer = Core::Timer::create_repeating(25, [self] {
                    self->poll_readable();
                });
                self->m_read_poll_timer->start();
            }
            return WebIDL::create_resolved_promise(stream_realm, JS::js_undefined());
        }
        if (read_result != RIN_SERIAL_OK)
            return WebIDL::create_rejected_promise(stream_realm,
                JS::TypeError::create(stream_realm, "Serial read failed"sv));
        if (count == 0)
            return WebIDL::create_resolved_promise(stream_realm, JS::js_undefined());

        buffer.resize(count);
        if (auto enqueue_result = readable->pull_from_bytes(move(buffer)); enqueue_result.is_error())
            return WebIDL::create_rejected_promise(stream_realm,
                JS::TypeError::create(stream_realm, "Serial read stream rejected data"sv));
        return WebIDL::create_resolved_promise(stream_realm, JS::js_undefined());
    });
    auto readable_cancel = GC::create_function(realm.heap(), [self](JS::Value) {
        self->m_read_fatal = true;
        return WebIDL::create_resolved_promise(self->realm(), JS::js_undefined());
    });
    readable->set_up_with_byte_reading_support(readable_pull, readable_cancel,
                                               self->m_buffer_size);

    auto writable_start = GC::create_function(realm.heap(), []() -> WebIDL::ExceptionOr<JS::Value> {
        return JS::js_undefined();
    });
    auto writable_write = GC::create_function(realm.heap(), [self](JS::Value chunk) {
        auto& stream_realm = self->realm();
        if (!WebIDL::is_buffer_source_type(chunk))
            return WebIDL::create_rejected_promise(stream_realm,
                JS::TypeError::create(stream_realm, "Serial writes require a BufferSource"sv));

        auto bytes_or_error = WebIDL::get_buffer_source_copy(chunk.as_object());
        if (bytes_or_error.is_error())
            return WebIDL::create_rejected_promise(stream_realm,
                JS::TypeError::create(stream_realm, "Unable to copy serial write data"sv));

        auto bytes = bytes_or_error.release_value();
        if (bytes.is_empty())
            return WebIDL::create_resolved_promise(stream_realm, JS::js_undefined());

        size_t offset = 0;
        while (offset < bytes.size()) {
            size_t written = 0;
            auto write_result = rin_web_serial_write(self->m_handle,
                bytes.data() + offset, bytes.size() - offset, &written);
            if (write_result != RIN_SERIAL_OK || written == 0 ||
                written > bytes.size() - offset)
                return WebIDL::create_rejected_promise(stream_realm,
                    JS::TypeError::create(stream_realm, "Serial write failed"sv));
            offset += written;
        }
        return WebIDL::create_resolved_promise(stream_realm, JS::js_undefined());
    });
    auto writable_close = GC::create_function(realm.heap(), [self]() {
        return WebIDL::create_resolved_promise(self->realm(), JS::js_undefined());
    });
    auto writable_abort = GC::create_function(realm.heap(), [self](JS::Value) {
        self->m_write_fatal = true;
        return WebIDL::create_resolved_promise(self->realm(), JS::js_undefined());
    });
    auto writable_size = GC::create_function(realm.heap(), [](JS::Value) {
        return JS::normal_completion(JS::Value(1));
    });

    m_readable = readable;
    m_writable = MUST(Streams::create_writable_stream(realm, writable_start,
        writable_write, writable_close, writable_abort, self->m_buffer_size,
        writable_size));
    return WebIDL::create_resolved_promise(realm, JS::js_undefined());
}

void SerialPort::poll_readable()
{
    if (m_state != SerialPortState::Opened || !m_connected || !m_readable) {
        if (m_read_poll_timer)
            m_read_poll_timer->stop();
        return;
    }

    RinSerialWaitResultV1 readiness {};
    auto wait_result = rin_web_serial_wait(
        m_handle, RIN_SERIAL_WAIT_READABLE | RIN_SERIAL_WAIT_HANGUP |
            RIN_SERIAL_WAIT_ERROR,
        &readiness);
    if (wait_result == RIN_SERIAL_EAGAIN)
        return;
    if (wait_result != RIN_SERIAL_OK ||
        (readiness.events & (RIN_SERIAL_WAIT_HANGUP | RIN_SERIAL_WAIT_ERROR)) != 0u) {
        m_read_fatal = true;
        if (m_read_poll_timer)
            m_read_poll_timer->stop();
        m_readable->error(JS::TypeError::create(
            realm(), "Serial read readiness failed"sv));
        return;
    }
    if ((readiness.events & RIN_SERIAL_WAIT_READABLE) == 0u)
        return;

    auto buffer_or_error = ByteBuffer::create_uninitialized(m_buffer_size);
    if (buffer_or_error.is_error()) {
        m_read_fatal = true;
        if (m_read_poll_timer)
            m_read_poll_timer->stop();
        m_readable->error(JS::TypeError::create(
            realm(), "Unable to allocate the serial read buffer"sv));
        return;
    }
    auto buffer = buffer_or_error.release_value();
    size_t count = 0;
    auto read_result = rin_web_serial_read(m_handle, buffer.data(), buffer.size(), &count);
    if (read_result == RIN_SERIAL_EAGAIN)
        return;
    if (read_result != RIN_SERIAL_OK || count > buffer.size()) {
        m_read_fatal = true;
        if (m_read_poll_timer)
            m_read_poll_timer->stop();
        m_readable->error(JS::TypeError::create(
            realm(), "Serial read failed"sv));
        return;
    }
    if (count == 0u)
        return;

    buffer.resize(count);
    if (auto enqueue_result = m_readable->pull_from_bytes(move(buffer)); enqueue_result.is_error()) {
        m_read_fatal = true;
        if (m_read_poll_timer)
            m_read_poll_timer->stop();
        m_readable->error(JS::TypeError::create(
            realm(), "Serial read stream rejected data"sv));
        return;
    }
    if (m_read_poll_timer) {
        m_read_poll_timer->stop();
        m_read_poll_timer = nullptr;
    }
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
    if (m_read_poll_timer) {
        m_read_poll_timer->stop();
        m_read_poll_timer = nullptr;
    }
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
