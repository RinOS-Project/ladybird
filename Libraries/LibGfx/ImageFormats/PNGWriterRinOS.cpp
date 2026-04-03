/*
 * Copyright (c) 2026, RinOS contributors.
 *
 * SPDX-License-Identifier: BSD-2-Clause
 */

#include <AK/ByteBuffer.h>
#include <AK/Checked.h>
#include <LibGfx/Bitmap.h>
#include <LibGfx/Color.h>
#include <LibGfx/ImageFormats/PNGWriter.h>
#include "../../../../zlib/rinz_checksum.h"

namespace Gfx {

static ErrorOr<void> append_big_endian_u32(ByteBuffer& buffer, u32 value)
{
    u8 bytes[4] = {
        static_cast<u8>((value >> 24) & 0xff),
        static_cast<u8>((value >> 16) & 0xff),
        static_cast<u8>((value >> 8) & 0xff),
        static_cast<u8>(value & 0xff),
    };
    return buffer.try_append(bytes, sizeof(bytes));
}

static ErrorOr<void> append_little_endian_u16(ByteBuffer& buffer, u16 value)
{
    u8 bytes[2] = {
        static_cast<u8>(value & 0xff),
        static_cast<u8>((value >> 8) & 0xff),
    };
    return buffer.try_append(bytes, sizeof(bytes));
}

static ErrorOr<void> append_png_chunk(ByteBuffer& png, char const type[4], ReadonlyBytes payload)
{
    TRY(append_big_endian_u32(png, static_cast<u32>(payload.size())));
    TRY(png.try_append(type, 4));
    TRY(png.try_append(payload));

    u32 crc = rinz_crc32(reinterpret_cast<u8 const*>(type), 4);
    crc = rinz_crc32_update(crc, payload.data(), payload.size());
    TRY(append_big_endian_u32(png, crc));
    return {};
}

static ErrorOr<ByteBuffer> zlib_wrap_stored_blocks(ReadonlyBytes bytes)
{
    ByteBuffer output;
    TRY(output.try_append(static_cast<u8>(0x78)));
    TRY(output.try_append(static_cast<u8>(0x01)));

    size_t offset = 0;
    bool emitted_block = false;
    do {
        auto remaining = bytes.size() - offset;
        auto block_size = remaining > 0xffff ? 0xffff : remaining;
        auto is_final = (offset + block_size) == bytes.size();

        TRY(output.try_append(static_cast<u8>(is_final ? 0x01 : 0x00)));
        TRY(append_little_endian_u16(output, static_cast<u16>(block_size)));
        TRY(append_little_endian_u16(output, static_cast<u16>(~static_cast<u16>(block_size))));
        TRY(output.try_append(bytes.data() + offset, block_size));

        offset += block_size;
        emitted_block = true;
    } while (!emitted_block || offset < bytes.size());

    TRY(append_big_endian_u32(output, rinz_adler32(bytes.data(), bytes.size())));
    return output;
}

static ErrorOr<ByteBuffer> encode_bitmap_rgba_scanlines(Bitmap const& bitmap)
{
    Checked<size_t> row_size = static_cast<size_t>(bitmap.width());
    row_size *= 4;
    row_size += 1;

    Checked<size_t> total_size = row_size.value();
    total_size *= static_cast<size_t>(bitmap.height());
    if (row_size.has_overflow() || total_size.has_overflow())
        return Error::from_string_literal("PNGWriterRinOS: Bitmap size overflow");

    auto raw_bytes = TRY(ByteBuffer::create_uninitialized(total_size.value()));
    size_t write_offset = 0;

    for (int y = 0; y < bitmap.height(); ++y) {
        raw_bytes[write_offset++] = 0;
        for (int x = 0; x < bitmap.width(); ++x) {
            auto pixel = bitmap.get_pixel(x, y);
            raw_bytes[write_offset++] = pixel.red();
            raw_bytes[write_offset++] = pixel.green();
            raw_bytes[write_offset++] = pixel.blue();
            raw_bytes[write_offset++] = pixel.alpha();
        }
    }

    return raw_bytes;
}

ErrorOr<ByteBuffer> PNGWriter::encode(Gfx::Bitmap const& bitmap, Options options)
{
    if (bitmap.width() <= 0 || bitmap.height() <= 0)
        return Error::from_string_literal("PNGWriterRinOS: Invalid bitmap size");

    auto scanline_bytes = TRY(encode_bitmap_rgba_scanlines(bitmap));
    auto idat_payload = TRY(zlib_wrap_stored_blocks(scanline_bytes.bytes()));

    ByteBuffer png;
    static constexpr u8 signature[8] = { 0x89, 0x50, 0x4e, 0x47, 0x0d, 0x0a, 0x1a, 0x0a };
    TRY(png.try_append(signature, sizeof(signature)));

    auto ihdr = TRY(ByteBuffer::create_uninitialized(13));
    ihdr[0] = static_cast<u8>((bitmap.width() >> 24) & 0xff);
    ihdr[1] = static_cast<u8>((bitmap.width() >> 16) & 0xff);
    ihdr[2] = static_cast<u8>((bitmap.width() >> 8) & 0xff);
    ihdr[3] = static_cast<u8>(bitmap.width() & 0xff);
    ihdr[4] = static_cast<u8>((bitmap.height() >> 24) & 0xff);
    ihdr[5] = static_cast<u8>((bitmap.height() >> 16) & 0xff);
    ihdr[6] = static_cast<u8>((bitmap.height() >> 8) & 0xff);
    ihdr[7] = static_cast<u8>(bitmap.height() & 0xff);
    ihdr[8] = 8;
    ihdr[9] = 6;
    ihdr[10] = 0;
    ihdr[11] = 0;
    ihdr[12] = 0;
    TRY(append_png_chunk(png, "IHDR", ihdr.bytes()));

    if (options.icc_data.has_value()) {
        auto compressed_icc = TRY(zlib_wrap_stored_blocks(*options.icc_data));
        ByteBuffer iccp;
        static constexpr char profile_name[] = "embedded profile";
        TRY(iccp.try_append(profile_name, sizeof(profile_name) - 1));
        TRY(iccp.try_append(static_cast<u8>(0)));
        TRY(iccp.try_append(static_cast<u8>(0)));
        TRY(iccp.try_append(compressed_icc.bytes()));
        TRY(append_png_chunk(png, "iCCP", iccp.bytes()));
    }

    TRY(append_png_chunk(png, "IDAT", idat_payload.bytes()));
    TRY(append_png_chunk(png, "IEND", ReadonlyBytes {}));
    return png;
}

}
