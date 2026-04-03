/*
 * Copyright (c) 2020, Andreas Kling <andreas@ladybird.org>
 *
 * SPDX-License-Identifier: BSD-2-Clause
 */

#include <AK/ByteBuffer.h>
#include <LibGfx/Bitmap.h>
#include <LibGfx/ShareableBitmap.h>
#include <LibGfx/Size.h>
#include <LibIPC/Decoder.h>
#include <LibIPC/Encoder.h>
#include <LibIPC/File.h>

namespace Gfx {

ShareableBitmap::ShareableBitmap() = default;

ShareableBitmap::ShareableBitmap(NonnullRefPtr<Bitmap> bitmap, Tag)
    : m_bitmap(move(bitmap))
{
}

ShareableBitmap::~ShareableBitmap() = default;

ShareableBitmap::ShareableBitmap(ShareableBitmap const&) = default;
ShareableBitmap::ShareableBitmap(ShareableBitmap&&) = default;

ShareableBitmap& ShareableBitmap::operator=(ShareableBitmap const&) = default;
ShareableBitmap& ShareableBitmap::operator=(ShareableBitmap&&) = default;

bool ShareableBitmap::is_valid() const { return m_bitmap; }

Bitmap const* ShareableBitmap::bitmap() const { return m_bitmap; }
Bitmap* ShareableBitmap::bitmap() { return m_bitmap; }

}

namespace IPC {

template<>
ErrorOr<void> encode(Encoder& encoder, Gfx::ShareableBitmap const& shareable_bitmap)
{
    TRY(encoder.encode(shareable_bitmap.is_valid()));
    if (!shareable_bitmap.is_valid())
        return {};

    auto& bitmap = *shareable_bitmap.bitmap();
#if defined(AK_OS_RINOS)
    TRY(encoder.encode(bitmap.size()));
    TRY(encoder.encode(static_cast<u32>(bitmap.format())));
    TRY(encoder.encode(static_cast<u32>(bitmap.alpha_type())));
    TRY(encoder.encode_size(bitmap.size_in_bytes()));
    TRY(encoder.append(bitmap.scanline_u8(0), bitmap.size_in_bytes()));
#else
    TRY(encoder.encode(TRY(IPC::File::clone_fd(bitmap.anonymous_buffer().fd()))));
    TRY(encoder.encode(bitmap.size()));
    TRY(encoder.encode(static_cast<u32>(bitmap.format())));
    TRY(encoder.encode(static_cast<u32>(bitmap.alpha_type())));
#endif
    return {};
}

template<>
ErrorOr<Gfx::ShareableBitmap> decode(Decoder& decoder)
{
    if (auto valid = TRY(decoder.decode<bool>()); !valid)
        return Gfx::ShareableBitmap {};

#if defined(AK_OS_RINOS)
    auto size = TRY(decoder.decode<Gfx::IntSize>());

    auto raw_bitmap_format = TRY(decoder.decode<u32>());
    if (!Gfx::is_valid_bitmap_format(raw_bitmap_format))
        return Error::from_string_literal("IPC: Invalid Gfx::ShareableBitmap format");
    auto bitmap_format = static_cast<Gfx::BitmapFormat>(raw_bitmap_format);

    auto raw_alpha_type = TRY(decoder.decode<u32>());
    if (!Gfx::is_valid_alpha_type(raw_alpha_type))
        return Error::from_string_literal("IPC: Invalid Gfx::ShareableBitmap alpha type");
    auto alpha_type = static_cast<Gfx::AlphaType>(raw_alpha_type);

    auto data_size = TRY(decoder.decode_size());
    auto expected_size = Gfx::Bitmap::size_in_bytes(Gfx::Bitmap::minimum_pitch(size.width(), bitmap_format), size.height());
    if (data_size != expected_size)
        return Error::from_string_literal("IPC: Invalid Gfx::ShareableBitmap payload size");

    auto raw_data = TRY(ByteBuffer::create_uninitialized(data_size));
    if (data_size > 0)
        TRY(decoder.decode_into(raw_data.bytes()));
    auto bitmap = TRY(Gfx::Bitmap::create_with_raw_data(bitmap_format, alpha_type, raw_data.bytes(), size));
#else
    auto anon_file = TRY(decoder.decode<IPC::File>());
    auto size = TRY(decoder.decode<Gfx::IntSize>());

    auto raw_bitmap_format = TRY(decoder.decode<u32>());
    if (!Gfx::is_valid_bitmap_format(raw_bitmap_format))
        return Error::from_string_literal("IPC: Invalid Gfx::ShareableBitmap format");
    auto bitmap_format = static_cast<Gfx::BitmapFormat>(raw_bitmap_format);

    auto raw_alpha_type = TRY(decoder.decode<u32>());
    if (!Gfx::is_valid_alpha_type(raw_alpha_type))
        return Error::from_string_literal("IPC: Invalid Gfx::ShareableBitmap alpha type");
    auto alpha_type = static_cast<Gfx::AlphaType>(raw_alpha_type);

    auto buffer = TRY(Core::AnonymousBuffer::create_from_anon_fd(anon_file.take_fd(), Gfx::Bitmap::size_in_bytes(Gfx::Bitmap::minimum_pitch(size.width(), bitmap_format), size.height())));
    auto bitmap = TRY(Gfx::Bitmap::create_with_anonymous_buffer(bitmap_format, alpha_type, move(buffer), size));
#endif

    return Gfx::ShareableBitmap { move(bitmap), Gfx::ShareableBitmap::ConstructWithKnownGoodBitmap };
}

}
