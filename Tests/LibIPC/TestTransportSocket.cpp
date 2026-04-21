/*
 * Copyright (c) 2026, The Ladybird developers
 *
 * SPDX-License-Identifier: BSD-2-Clause
 */

#include <AK/Atomic.h>
#include <AK/Function.h>
#include <AK/MemoryStream.h>
#include <AK/Optional.h>
#include <AK/Time.h>
#include <LibCore/AnonymousBuffer.h>
#include <LibCore/EventLoop.h>
#include <LibCore/Socket.h>
#include <LibCore/System.h>
#include <LibGfx/Bitmap.h>
#include <LibGfx/ShareableBitmap.h>
#include <LibIPC/Decoder.h>
#include <LibIPC/Encoder.h>
#include <LibIPC/Message.h>
#include <LibIPC/TransportSocket.h>
#include <LibTest/TestCase.h>

using namespace AK::TimeLiterals;

static void spin_until(Core::EventLoop& loop, Function<bool()> condition, AK::Duration timeout = 2000_ms)
{
    i64 const timeout_ms = timeout.to_milliseconds();
    for (i64 elapsed_ms = 0; elapsed_ms < timeout_ms; elapsed_ms += 5) {
        (void)loop.pump(Core::EventLoop::WaitMode::PollForEvents);
        if (condition())
            return;
        MUST(Core::System::sleep_ms(5));
    }

    FAIL("Timed out waiting for condition");
}

struct TransportPair {
    NonnullOwnPtr<IPC::TransportSocket> sender;
    NonnullOwnPtr<IPC::TransportSocket> receiver;
};

static TransportPair create_transport_pair()
{
    int fds[2] = {};
    MUST(Core::System::socketpair(AF_LOCAL, SOCK_STREAM, 0, fds));

    auto sender_socket = TRY_OR_FAIL(Core::LocalSocket::adopt_fd(fds[0]));
    auto receiver_socket = TRY_OR_FAIL(Core::LocalSocket::adopt_fd(fds[1]));

    MUST(sender_socket->set_blocking(false));
    MUST(receiver_socket->set_blocking(false));

    return {
        TRY_OR_FAIL(IPC::TransportSocket::from_socket(move(sender_socket))),
        TRY_OR_FAIL(IPC::TransportSocket::from_socket(move(receiver_socket))),
    };
}

template<typename T>
static T roundtrip_over_transport(T const& value)
{
    auto pair = create_transport_pair();

    IPC::MessageBuffer buffer;
    IPC::Encoder encoder(buffer);
    TRY_OR_FAIL(encoder.encode(value));

    auto data = buffer.take_data();
    auto attachments = buffer.take_attachments();
    pair.sender->post_message(data, attachments);

    pair.receiver->wait_until_readable();

    Optional<T> decoded_value;
    auto should_shutdown = pair.receiver->read_as_many_messages_as_possible_without_blocking([&](auto&& message) {
        FixedMemoryStream stream { ReadonlyBytes { message.bytes.data(), message.bytes.size() } };
        auto message_attachments = move(message.attachments);
        IPC::Decoder decoder(stream, message_attachments);
        decoded_value = TRY_OR_FAIL(decoder.decode<T>());
    });

    EXPECT_EQ(should_shutdown, IPC::TransportSocket::ShouldShutdown::No);
    EXPECT(decoded_value.has_value());

    return move(decoded_value.release_value());
}

static bool buffers_have_same_contents(Core::AnonymousBuffer const& left, Core::AnonymousBuffer const& right)
{
    if (left.size() != right.size())
        return false;
    if (left.size() == 0)
        return true;
    return __builtin_memcmp(left.data<u8>(), right.data<u8>(), left.size()) == 0;
}

TEST_CASE(read_hook_is_notified_on_peer_hangup)
{
    Core::EventLoop loop;

    int fds[2] = {};
    MUST(Core::System::socketpair(AF_LOCAL, SOCK_STREAM, 0, fds));

    auto reader_socket = TRY_OR_FAIL(Core::LocalSocket::adopt_fd(fds[0]));
    auto peer_socket = TRY_OR_FAIL(Core::LocalSocket::adopt_fd(fds[1]));

    MUST(reader_socket->set_blocking(false));
    MUST(peer_socket->set_blocking(false));

    IPC::TransportSocket transport(move(reader_socket));

    IGNORE_USE_IN_ESCAPING_LAMBDA Atomic<bool> observed_shutdown = false;

    transport.set_up_read_hook([&] {
        auto should_shutdown = transport.read_as_many_messages_as_possible_without_blocking([](auto&&) {
        });
        if (should_shutdown == IPC::TransportSocket::ShouldShutdown::Yes)
            observed_shutdown.store(true, AK::MemoryOrder::memory_order_relaxed);
    });

    peer_socket->close();

    spin_until(loop, [&] {
        return observed_shutdown.load(AK::MemoryOrder::memory_order_relaxed);
    });

    EXPECT(observed_shutdown.load(AK::MemoryOrder::memory_order_relaxed));
}

TEST_CASE(read_hook_is_notified_when_io_thread_exits_on_close)
{
    Core::EventLoop loop;

    int fds[2] = {};
    MUST(Core::System::socketpair(AF_LOCAL, SOCK_STREAM, 0, fds));

    auto reader_socket = TRY_OR_FAIL(Core::LocalSocket::adopt_fd(fds[0]));
    auto peer_socket = TRY_OR_FAIL(Core::LocalSocket::adopt_fd(fds[1]));

    MUST(reader_socket->set_blocking(false));
    MUST(peer_socket->set_blocking(false));

    IPC::TransportSocket transport(move(reader_socket));

    IGNORE_USE_IN_ESCAPING_LAMBDA Atomic<bool> observed_shutdown = false;

    transport.set_up_read_hook([&] {
        auto should_shutdown = transport.read_as_many_messages_as_possible_without_blocking([](auto&&) {
        });
        if (should_shutdown == IPC::TransportSocket::ShouldShutdown::Yes)
            observed_shutdown.store(true, AK::MemoryOrder::memory_order_relaxed);
    });

    transport.close();

    spin_until(loop, [&] {
        return observed_shutdown.load(AK::MemoryOrder::memory_order_relaxed);
    });

    EXPECT(observed_shutdown.load(AK::MemoryOrder::memory_order_relaxed));
}

TEST_CASE(anonymous_buffer_roundtrips_with_fd_backing)
{
    auto buffer = TRY_OR_FAIL(Core::AnonymousBuffer::create_with_size(128 * KiB));
    for (size_t i = 0; i < buffer.size(); ++i)
        buffer.data<u8>()[i] = static_cast<u8>(i % 251);

    auto roundtripped = roundtrip_over_transport(buffer);

    EXPECT(roundtripped.is_valid());
    EXPECT_EQ(roundtripped.size(), buffer.size());
    EXPECT(buffers_have_same_contents(buffer, roundtripped));
#ifndef AK_OS_WINDOWS
    EXPECT(roundtripped.fd() >= 0);
#endif
}

TEST_CASE(shareable_bitmap_roundtrips_with_fd_backing)
{
    auto bitmap = TRY_OR_FAIL(Gfx::Bitmap::create_shareable(Gfx::BitmapFormat::BGRA8888, Gfx::AlphaType::Premultiplied, { 128, 96 }));
    for (int y = 0; y < bitmap->height(); ++y) {
        auto* row = bitmap->scanline_u8(y);
        for (int x = 0; x < bitmap->width(); ++x) {
            auto offset = static_cast<size_t>(x) * sizeof(u32);
            row[offset + 0] = static_cast<u8>(x);
            row[offset + 1] = static_cast<u8>(y);
            row[offset + 2] = static_cast<u8>(x ^ y);
            row[offset + 3] = 0xff;
        }
    }

    auto roundtripped = roundtrip_over_transport(bitmap->to_shareable_bitmap());

    EXPECT(roundtripped.is_valid());
    EXPECT_EQ(roundtripped.bitmap()->size(), bitmap->size());
    EXPECT_EQ(roundtripped.bitmap()->format(), bitmap->format());
    EXPECT_EQ(roundtripped.bitmap()->alpha_type(), bitmap->alpha_type());
    EXPECT_EQ(roundtripped.bitmap()->size_in_bytes(), bitmap->size_in_bytes());
    EXPECT_EQ(__builtin_memcmp(roundtripped.bitmap()->scanline_u8(0), bitmap->scanline_u8(0), bitmap->size_in_bytes()), 0);
#ifndef AK_OS_WINDOWS
    EXPECT(roundtripped.bitmap()->anonymous_buffer().fd() >= 0);
#endif
}

TEST_CASE(two_large_anonymous_buffers_roundtrip_without_inline_payload_copy)
{
    static constexpr size_t buffer_size = static_cast<size_t>(800 * 437 * sizeof(u32));

    Array<Core::AnonymousBuffer, 2> buffers {
        TRY_OR_FAIL(Core::AnonymousBuffer::create_with_size(buffer_size)),
        TRY_OR_FAIL(Core::AnonymousBuffer::create_with_size(buffer_size)),
    };

    for (size_t i = 0; i < buffers[0].size(); ++i) {
        buffers[0].data<u8>()[i] = static_cast<u8>(i % 251);
        buffers[1].data<u8>()[i] = static_cast<u8>((i * 7) % 251);
    }

    auto roundtripped = roundtrip_over_transport(buffers);

    for (size_t i = 0; i < buffers.size(); ++i) {
        EXPECT(roundtripped[i].is_valid());
        EXPECT_EQ(roundtripped[i].size(), buffers[i].size());
        EXPECT(buffers_have_same_contents(buffers[i], roundtripped[i]));
#ifndef AK_OS_WINDOWS
        EXPECT(roundtripped[i].fd() >= 0);
#endif
    }
}
