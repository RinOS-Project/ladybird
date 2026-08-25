/*
 * Copyright (c) 2022, the SerenityOS developers.
 * Copyright (c) 2023, Sam Atkins <atkinssj@serenityos.org>
 *
 * SPDX-License-Identifier: BSD-2-Clause
 */

#include <AK/ByteBuffer.h>
#include <AK/IntegralMath.h>
#include <LibCompress/Zlib.h>
#include <LibCore/Resource.h>
#include <LibGfx/Font/WOFF/Loader.h>
#include <LibGfx/FourCC.h>

namespace WOFF {

// https://www.w3.org/TR/WOFF/#WOFFHeader
struct [[gnu::packed]] Header {
    BigEndian<u32> signature;        // 0x774F4646 'wOFF'
    BigEndian<u32> flavor;           // The "sfnt version" of the input font.
    BigEndian<u32> length;           // Total size of the WOFF file.
    BigEndian<u16> num_tables;       // Number of entries in directory of font tables.
    BigEndian<u16> reserved;         // Reserved; set to zero.
    BigEndian<u32> total_sfnt_size;  // Total size needed for the uncompressed font data, including
                                     // the sfnt header, directory, and font tables (including padding).
    BigEndian<u16> major_version;    // Major version of the WOFF file.
    BigEndian<u16> minor_version;    // Minor version of the WOFF file.
    BigEndian<u32> meta_offset;      // Offset to metadata block, from beginning of WOFF file.
    BigEndian<u32> meta_length;      // Length of compressed metadata block.
    BigEndian<u32> meta_orig_length; // Uncompressed size of metadata block.
    BigEndian<u32> priv_offset;      // Offset to private data block, from beginning of WOFF file.
    BigEndian<u32> priv_length;      // Length of private data block.
};
static_assert(AssertSize<Header, 44>());

// https://www.w3.org/TR/WOFF/#TableDirectory
struct [[gnu::packed]] TableDirectoryEntry {
    Gfx::FourCC tag;              // 4-byte sfnt table identifier.
    BigEndian<u32> offset;        // Offset to the data, from beginning of WOFF file.
    BigEndian<u32> comp_length;   // Length of the compressed data, excluding padding.
    BigEndian<u32> orig_length;   // Length of the uncompressed table, excluding padding.
    BigEndian<u32> orig_checksum; // Checksum of the uncompressed table.
};
static_assert(AssertSize<TableDirectoryEntry, 20>());

}

template<>
class AK::Traits<WOFF::Header> : public DefaultTraits<WOFF::Header> {
public:
    static constexpr bool is_trivially_serializable() { return true; }
};

template<>
class AK::Traits<WOFF::TableDirectoryEntry> : public DefaultTraits<WOFF::TableDirectoryEntry> {
public:
    static constexpr bool is_trivially_serializable() { return true; }
};

namespace WOFF {

static constexpr u32 WOFF_SIGNATURE = 0x774F4646;

static bool range_is_within(u32 offset, u32 length, u32 total_length)
{
    return offset <= total_length && length <= total_length - offset;
}

static bool ranges_overlap(u32 left_offset, u32 left_length, u32 right_offset, u32 right_length)
{
    auto left_end = static_cast<u64>(left_offset) + left_length;
    auto right_end = static_cast<u64>(right_offset) + right_length;
    return left_offset < right_end && right_offset < left_end;
}

static u32 table_checksum(ReadonlyBytes bytes)
{
    u32 sum = 0;
    for (size_t offset = 0; offset < bytes.size(); offset += 4) {
        u32 word = 0;
        for (size_t byte_index = 0; byte_index < 4; ++byte_index) {
            word <<= 8;
            if (byte_index < bytes.size() - offset)
                word |= bytes[offset + byte_index];
        }
        sum += word;
    }
    return sum;
}

static u16 pow_2_less_than_or_equal(u16 x)
{
    VERIFY(x > 0);
    VERIFY(x < 32769);
    return 1 << (sizeof(u16) * 8 - count_leading_zeroes_safe<u16>(x - 1));
}

ErrorOr<NonnullRefPtr<Gfx::Typeface>> try_load_from_resource(Core::Resource const& resource, unsigned index)
{
    return try_load_from_bytes(resource.data(), index);
}

using Uint8 = u8;
using Uint16 = BigEndian<u16>;
using Int16 = BigEndian<i16>;
using Uint32 = BigEndian<u32>;
using Int32 = BigEndian<i32>;

// https://learn.microsoft.com/en-us/typography/opentype/spec/otff#table-directory
// Table Directory (known as the Offset Table in ISO-IEC 14496-22:2019)
struct [[gnu::packed]] TableDirectory {
    Uint32 sfnt_version;
    Uint16 num_tables;     // Number of tables.
    Uint16 search_range;   // (Maximum power of 2 <= numTables) x 16.
    Uint16 entry_selector; // Log2(maximum power of 2 <= numTables).
    Uint16 range_shift;    // NumTables x 16 - searchRange.
};
static_assert(AssertSize<TableDirectory, 12>());

using Offset16 = BigEndian<u16>;
using Offset32 = BigEndian<u32>;

// https://learn.microsoft.com/en-us/typography/opentype/spec/otff#table-directory
struct [[gnu::packed]] TableRecord {
    Gfx::FourCC table_tag; // Table identifier.
    Uint32 checksum;       // CheckSum for this table.
    Offset32 offset;       // Offset from beginning of TrueType font file.
    Uint32 length;         // Length of this table.
};
static_assert(AssertSize<TableRecord, 16>());

ErrorOr<NonnullRefPtr<Gfx::Typeface>> try_load_from_bytes(ReadonlyBytes buffer, unsigned int index)
{
    FixedMemoryStream stream(buffer);
    auto header = TRY(stream.read_value<Header>());

    // The signature field in the WOFF header MUST contain the "magic number" 0x774F4646. If the field does not contain this value, user agents MUST reject the file as invalid.
    if (header.signature != WOFF_SIGNATURE)
        return Error::from_string_literal("Invalid WOFF signature");
    // The flavor field corresponds to the "sfnt version" field found at the beginning of an sfnt file,
    // indicating the type of font data contained. Although only fonts of type 0x00010000 (the version number 1.0 as a 16.16 fixed-point value, indicating TrueType glyph data)
    // and 0x4F54544F (the tag 'OTTO', indicating CFF glyph data) are widely supported at present,
    // it is not an error in the WOFF file if the flavor field contains a different value,
    // indicating a WOFF-packaged version of a different sfnt flavor.
    // (The value 0x74727565 'true' has been used for some TrueType-flavored fonts on Mac OS, for example.)
    // Whether client software will actually support other types of sfnt font data is outside the scope of the WOFF specification, which simply describes how the sfnt is repackaged for Web use.

    auto expected_total_sfnt_size = static_cast<u64>(sizeof(TableDirectory))
        + static_cast<u64>(header.num_tables) * sizeof(TableRecord);
    if (header.length != buffer.size() || header.length > 10 * MiB)
        return Error::from_string_literal("Invalid WOFF length");
    if (header.num_tables == 0 || header.num_tables > NumericLimits<u16>::max() / 16)
        return Error::from_string_literal("Invalid WOFF numTables");
    if (header.reserved != 0)
        return Error::from_string_literal("Invalid WOFF reserved field");
    auto directory_end = static_cast<u64>(sizeof(Header))
        + static_cast<u64>(header.num_tables) * sizeof(TableDirectoryEntry);
    if (directory_end > header.length)
        return Error::from_string_literal("Truncated WOFF table directory");
    if ((header.meta_length == 0) != (header.meta_offset == 0)
        || (header.meta_length == 0) != (header.meta_orig_length == 0))
        return Error::from_string_literal("Invalid WOFF meta block offset");
    if (header.meta_length != 0
        && (header.meta_offset < directory_end
            || !range_is_within(header.meta_offset, header.meta_length, header.length)))
        return Error::from_string_literal("Truncated WOFF meta block");
    if ((header.priv_length == 0) != (header.priv_offset == 0))
        return Error::from_string_literal("Invalid WOFF private block offset");
    if (header.priv_length != 0
        && (header.priv_offset < directory_end
            || !range_is_within(header.priv_offset, header.priv_length, header.length)))
        return Error::from_string_literal("Truncated WOFF private block");
    if (header.meta_length != 0 && header.priv_length != 0
        && ranges_overlap(header.meta_offset, header.meta_length, header.priv_offset, header.priv_length))
        return Error::from_string_literal("Overlapping WOFF metadata blocks");
    if (header.total_sfnt_size < expected_total_sfnt_size)
        return Error::from_string_literal("Invalid WOFF total sfnt size");
    if (header.total_sfnt_size > 10 * MiB)
        return Error::from_string_literal("Uncompressed font is more than 10 MiB");
    auto font_buffer = TRY(ByteBuffer::create_zeroed(header.total_sfnt_size));

    u16 search_range = pow_2_less_than_or_equal(header.num_tables);
    TableDirectory table_directory {
        .sfnt_version = header.flavor,
        .num_tables = header.num_tables,
        .search_range = search_range * 16,
        .entry_selector = AK::log2(search_range),
        .range_shift = header.num_tables * 16 - search_range * 16,
    };
    font_buffer.overwrite(0, &table_directory, sizeof(table_directory));

    struct SourceRange {
        u32 offset;
        u32 length;
    };
    Vector<SourceRange> source_ranges;
    TRY(source_ranges.try_ensure_capacity(header.num_tables));

    size_t font_buffer_offset = sizeof(TableDirectory) + header.num_tables * sizeof(TableRecord);
    for (size_t i = 0; i < header.num_tables; ++i) {
        auto entry = TRY(stream.read_value<TableDirectoryEntry>());
        auto table_offset = static_cast<u32>(entry.offset);
        auto compressed_length = static_cast<u32>(entry.comp_length);
        auto original_length = static_cast<u32>(entry.orig_length);
        auto aligned_original_length = (static_cast<u64>(original_length) + 3u) & ~static_cast<u64>(3u);

        expected_total_sfnt_size += aligned_original_length;
        if (expected_total_sfnt_size > header.total_sfnt_size)
            return Error::from_string_literal("Invalid WOFF total sfnt size");
        if (compressed_length > original_length
            || table_offset < directory_end
            || !range_is_within(table_offset, compressed_length, header.length))
            return Error::from_string_literal("Truncated WOFF table");
        for (auto const& source_range : source_ranges) {
            if (ranges_overlap(table_offset, compressed_length, source_range.offset, source_range.length))
                return Error::from_string_literal("Overlapping WOFF tables");
        }
        if ((header.meta_length != 0
                && ranges_overlap(table_offset, compressed_length, header.meta_offset, header.meta_length))
            || (header.priv_length != 0
                && ranges_overlap(table_offset, compressed_length, header.priv_offset, header.priv_length)))
            return Error::from_string_literal("WOFF table overlaps metadata");
        TRY(source_ranges.try_append({ table_offset, compressed_length }));
        if (font_buffer_offset > font_buffer.size()
            || original_length > font_buffer.size() - font_buffer_offset)
            return Error::from_string_literal("Uncompressed WOFF table too big");
        if (compressed_length < original_length) {
            auto compressed_data_stream = make<FixedMemoryStream>(buffer.slice(table_offset, compressed_length));
            auto decompressor = TRY(Compress::ZlibDecompressor::create(move(compressed_data_stream)));
            auto decompressed = TRY(ByteBuffer::create_uninitialized(original_length));
            TRY(decompressor->read_until_filled(decompressed.bytes()));
            if (!decompressor->is_eof())
                return Error::from_string_literal("Invalid decompressed WOFF table length");
            if (table_checksum(decompressed.bytes()) != entry.orig_checksum)
                return Error::from_string_literal("Invalid decompressed WOFF table checksum");
            font_buffer.overwrite(font_buffer_offset, decompressed.data(), original_length);
        } else {
            auto table = buffer.slice(table_offset, original_length);
            if (table_checksum(table) != entry.orig_checksum)
                return Error::from_string_literal("Invalid WOFF table checksum");
            font_buffer.overwrite(font_buffer_offset, table.data(), original_length);
        }

        size_t table_directory_offset = sizeof(TableDirectory) + i * sizeof(TableRecord);
        TableRecord table_record {
            .table_tag = entry.tag,
            .checksum = entry.orig_checksum,
            .offset = font_buffer_offset,
            .length = original_length,
        };
        font_buffer.overwrite(table_directory_offset, &table_record, sizeof(table_record));

        font_buffer_offset += aligned_original_length;
    }

    if (header.total_sfnt_size != expected_total_sfnt_size || font_buffer_offset != font_buffer.size())
        return Error::from_string_literal("Invalid WOFF total sfnt size");

    auto font_data = Gfx::FontData::create_from_byte_buffer(move(font_buffer));
    return TRY(Gfx::Typeface::try_load_from_font_data(move(font_data), index));
}

}
