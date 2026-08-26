/*
 * Copyright (c) 2026, RinOS contributors
 *
 * SPDX-License-Identifier: BSD-2-Clause
 */

#include <AK/ByteBuffer.h>
#include <AK/NumericLimits.h>
#include <LibGfx/Font/Font.h>
#include <LibGfx/Font/TypefaceTrueTypeRinOS.h>
#include <math.h>

namespace Gfx {

static constexpr u32 maximum_font_bytes = 64 * MiB;
static constexpr u32 maximum_tables = 256;
static constexpr u32 maximum_glyphs = 65535;
static constexpr u32 maximum_contours = 16384;
static constexpr u32 maximum_points = 131072;
static constexpr u32 maximum_outline_commands = 131072;
static constexpr u32 maximum_composite_depth = 16;
static constexpr u32 maximum_composite_components = 64;
static constexpr i32 maximum_design_coordinate = 1048576;

static constexpr u32 make_tag(char a, char b, char c, char d)
{
    return (static_cast<u32>(static_cast<u8>(a)) << 24)
        | (static_cast<u32>(static_cast<u8>(b)) << 16)
        | (static_cast<u32>(static_cast<u8>(c)) << 8)
        | static_cast<u32>(static_cast<u8>(d));
}

static bool read_u16(ReadonlyBytes bytes, size_t offset, u16& value)
{
    if (offset > bytes.size() || 2 > bytes.size() - offset)
        return false;
    value = (static_cast<u16>(bytes[offset]) << 8) | bytes[offset + 1];
    return true;
}

static bool read_i16(ReadonlyBytes bytes, size_t offset, i16& value)
{
    u16 unsigned_value;
    if (!read_u16(bytes, offset, unsigned_value))
        return false;
    value = static_cast<i16>(unsigned_value);
    return true;
}

static bool read_u32(ReadonlyBytes bytes, size_t offset, u32& value)
{
    if (offset > bytes.size() || 4 > bytes.size() - offset)
        return false;
    value = (static_cast<u32>(bytes[offset]) << 24)
        | (static_cast<u32>(bytes[offset + 1]) << 16)
        | (static_cast<u32>(bytes[offset + 2]) << 8)
        | bytes[offset + 3];
    return true;
}

static bool slice_within(ReadonlyBytes bytes, u32 offset, u32 length, ReadonlyBytes& result)
{
    if (offset > bytes.size() || length > bytes.size() - offset)
        return false;
    result = bytes.slice(offset, length);
    return true;
}

TypefaceTrueTypeRinOS::TypefaceTrueTypeRinOS(ByteBuffer&& bytes)
    : m_bytes(move(bytes))
    , m_family(FlyString::from_utf8_without_validation("RinOS TrueType"sv.bytes()))
{
}

TypefaceTrueTypeRinOS::~TypefaceTrueTypeRinOS() = default;

ErrorOr<NonnullRefPtr<TypefaceTrueTypeRinOS>> TypefaceTrueTypeRinOS::try_load(ReadonlyBytes bytes, u32 ttc_index)
{
    if (bytes.is_empty() || bytes.size() > maximum_font_bytes)
        return Error::from_string_literal("TrueType font size is outside the RinOS limit");

    auto owned_bytes = TRY(ByteBuffer::copy(bytes));
    auto typeface = adopt_ref(*new TypefaceTrueTypeRinOS(move(owned_bytes)));
    if (!typeface->parse(ttc_index))
        return Error::from_string_literal("Malformed or unsupported TrueType font");
    return typeface;
}

bool TypefaceTrueTypeRinOS::parse(u32 requested_ttc_index)
{
    auto bytes = m_bytes.bytes();
    u32 sfnt_offset = 0;
    u32 scaler_type;
    u16 table_count;
    struct TableRecord {
        u32 tag;
        TableView view;
    };
    Vector<TableRecord> records;

    if (bytes.size() < 12)
        return false;
    if (bytes.slice(0, 4) == "ttcf"sv.bytes()) {
        u32 font_count;
        if (!read_u32(bytes, 8, font_count) || font_count == 0
            || font_count > (bytes.size() - 12) / 4
            || requested_ttc_index >= font_count
            || !read_u32(bytes, 12 + requested_ttc_index * 4, sfnt_offset)
            || sfnt_offset > bytes.size() || 12 > bytes.size() - sfnt_offset)
            return false;
    } else if (requested_ttc_index != 0) {
        return false;
    }

    if (!read_u32(bytes, sfnt_offset, scaler_type)
        || (scaler_type != 0x00010000 && scaler_type != make_tag('t', 'r', 'u', 'e'))
        || !read_u16(bytes, sfnt_offset + 4, table_count)
        || table_count == 0 || table_count > maximum_tables
        || sfnt_offset > bytes.size() || 12u + static_cast<u32>(table_count) * 16u > bytes.size() - sfnt_offset)
        return false;

    if (records.try_ensure_capacity(table_count).is_error())
        return false;
    for (u32 index = 0; index < table_count; ++index) {
        auto record_offset = sfnt_offset + 12u + index * 16u;
        u32 tag;
        u32 offset;
        u32 length;
        if (!read_u32(bytes, record_offset, tag)
            || !read_u32(bytes, record_offset + 8, offset)
            || !read_u32(bytes, record_offset + 12, length)
            || offset > bytes.size() || length > bytes.size() - offset)
            return false;
        for (auto const& existing : records) {
            if (existing.tag == tag)
                return false;
        }
        if (records.try_append({ tag, { offset, length } }).is_error())
            return false;
    }

    auto find_table = [&records](u32 tag) -> Optional<TableView> {
        for (auto const& record : records) {
            if (record.tag == tag)
                return record.view;
        }
        return {};
    };
    auto cmap = find_table(make_tag('c', 'm', 'a', 'p'));
    auto head = find_table(make_tag('h', 'e', 'a', 'd'));
    auto hhea = find_table(make_tag('h', 'h', 'e', 'a'));
    auto hmtx = find_table(make_tag('h', 'm', 't', 'x'));
    auto loca = find_table(make_tag('l', 'o', 'c', 'a'));
    auto glyf = find_table(make_tag('g', 'l', 'y', 'f'));
    auto maxp = find_table(make_tag('m', 'a', 'x', 'p'));
    if (!cmap.has_value() || !head.has_value() || !hhea.has_value()
        || !hmtx.has_value() || !loca.has_value() || !glyf.has_value()
        || !maxp.has_value() || head->length < 54 || hhea->length < 36 || maxp->length < 6)
        return false;

    m_cmap = cmap.value();
    m_head = head.value();
    m_hhea = hhea.value();
    m_hmtx = hmtx.value();
    m_loca = loca.value();
    m_glyf = glyf.value();

    ReadonlyBytes head_bytes;
    ReadonlyBytes hhea_bytes;
    ReadonlyBytes maxp_bytes;
    i16 index_to_loc_format;
    u16 number_of_h_metrics;
    u16 glyph_count;
    if (!slice_within(bytes, m_head.offset, m_head.length, head_bytes)
        || !slice_within(bytes, m_hhea.offset, m_hhea.length, hhea_bytes)
        || !slice_within(bytes, maxp->offset, maxp->length, maxp_bytes)
        || !read_u16(head_bytes, 18, m_units_per_em)
        || !read_i16(head_bytes, 50, index_to_loc_format)
        || !read_i16(hhea_bytes, 4, m_ascender)
        || !read_i16(hhea_bytes, 6, m_descender)
        || !read_i16(hhea_bytes, 8, m_line_gap)
        || !read_u16(hhea_bytes, 34, number_of_h_metrics)
        || !read_u16(maxp_bytes, 4, glyph_count))
        return false;
    m_index_to_loc_format = static_cast<u16>(index_to_loc_format);
    m_number_of_h_metrics = number_of_h_metrics;
    m_glyph_count = glyph_count;
    if (m_units_per_em == 0 || m_glyph_count == 0 || m_glyph_count > maximum_glyphs
        || m_number_of_h_metrics == 0 || m_number_of_h_metrics > m_glyph_count
        || (m_index_to_loc_format != 0 && m_index_to_loc_format != 1))
        return false;

    u64 required_loca_bytes = static_cast<u64>(m_glyph_count + 1)
        * (m_index_to_loc_format == 0 ? 2u : 4u);
    u64 required_hmtx_bytes = static_cast<u64>(m_number_of_h_metrics) * 4u;
    if (required_loca_bytes > m_loca.length || required_hmtx_bytes > m_hmtx.length)
        return false;

    auto os2 = find_table(make_tag('O', 'S', '/', '2'));
    if (os2.has_value() && os2->length >= 8) {
        ReadonlyBytes os2_bytes;
        u16 width;
        if (!slice_within(bytes, os2->offset, os2->length, os2_bytes)
            || !read_u16(os2_bytes, 4, m_weight) || !read_u16(os2_bytes, 6, width))
            return false;
        if (m_weight == 0)
            m_weight = 400;
        m_width = width >= 1 && width <= 9 ? width : 5;
        if (os2_bytes.size() >= 90) {
            u16 version;
            if (!read_u16(os2_bytes, 0, version))
                return false;
            if (version >= 2 && !read_i16(os2_bytes, 86, m_x_height))
                return false;
        }
    }

    if (!select_cmap())
        return false;
    m_ttc_index = requested_ttc_index;
    if (auto zero_glyph = glyph_advance(glyph_id_for_code_point('0')); zero_glyph.has_value())
        m_advance_of_ascii_zero = zero_glyph.value();
    return true;
}

bool TypefaceTrueTypeRinOS::select_cmap()
{
    auto bytes = m_bytes.bytes();
    ReadonlyBytes cmap;
    u16 subtable_count;
    int best_score = -1;
    TableView best {};
    u32 best_format = 0;

    if (!slice_within(bytes, m_cmap.offset, m_cmap.length, cmap)
        || cmap.size() < 4 || !read_u16(cmap, 2, subtable_count)
        || 4u + static_cast<u32>(subtable_count) * 8u > cmap.size())
        return false;
    for (u32 index = 0; index < subtable_count; ++index) {
        auto record = 4u + index * 8u;
        u16 platform;
        u16 encoding;
        u32 offset;
        u16 format;
        u32 length;
        int score = -1;
        if (!read_u16(cmap, record, platform) || !read_u16(cmap, record + 2, encoding)
            || !read_u32(cmap, record + 4, offset) || offset > cmap.size()
            || 2 > cmap.size() - offset || !read_u16(cmap, offset, format))
            return false;
        if (format == 4) {
            u16 short_length;
            if (!read_u16(cmap, offset + 2, short_length) || short_length < 16
                || short_length > cmap.size() - offset)
                return false;
            length = short_length;
            if (platform == 3 && (encoding == 1 || encoding == 0))
                score = 2;
            else if (platform == 0)
                score = 1;
        } else if (format == 12) {
            if (!read_u32(cmap, offset + 4, length) || length < 16
                || length > cmap.size() - offset)
                return false;
            if (platform == 3 && encoding == 10)
                score = 4;
            else if (platform == 0)
                score = 3;
        }
        if (score > best_score) {
            best_score = score;
            best = { m_cmap.offset + offset, length };
            best_format = format;
        }
    }
    if (best_score < 0)
        return false;
    m_cmap = best;
    m_cmap_format = best_format;
    return true;
}

Optional<u32> TypefaceTrueTypeRinOS::glyph_offset(u32 glyph_id) const
{
    ReadonlyBytes loca;
    u32 offset;
    if (glyph_id > m_glyph_count || !slice_within(m_bytes.bytes(), m_loca.offset, m_loca.length, loca))
        return {};
    if (m_index_to_loc_format == 0) {
        u16 first;
        if (!read_u16(loca, glyph_id * 2u, first))
            return {};
        offset = static_cast<u32>(first) * 2u;
    } else {
        if (!read_u32(loca, glyph_id * 4u, offset))
            return {};
    }
    if (offset > m_glyf.length)
        return {};
    return offset;
}

Optional<u16> TypefaceTrueTypeRinOS::cmap_format4_glyph(u32 code_point) const
{
    ReadonlyBytes cmap;
    u16 segment_count_x2;
    u32 segment_count;
    if (code_point > 0xffffu || !slice_within(m_bytes.bytes(), m_cmap.offset, m_cmap.length, cmap)
        || !read_u16(cmap, 6, segment_count_x2) || segment_count_x2 == 0 || (segment_count_x2 & 1u) != 0)
        return {};
    segment_count = segment_count_x2 / 2u;
    if (segment_count > maximum_glyphs || 16u + segment_count * 8u > cmap.size())
        return {};
    auto end_codes = 14u;
    auto start_codes = end_codes + segment_count * 2u + 2u;
    auto deltas = start_codes + segment_count * 2u;
    auto range_offsets = deltas + segment_count * 2u;
    for (u32 index = 0; index < segment_count; ++index) {
        u16 start;
        u16 end;
        i16 delta;
        u16 range_offset;
        if (!read_u16(cmap, start_codes + index * 2u, start)
            || !read_u16(cmap, end_codes + index * 2u, end)
            || !read_i16(cmap, deltas + index * 2u, delta)
            || !read_u16(cmap, range_offsets + index * 2u, range_offset)
            || start > end)
            return {};
        if (code_point < start || code_point > end)
            continue;
        if (range_offset == 0)
            return static_cast<u16>((code_point + delta) & 0xffffu);
        auto glyph_index_offset = range_offsets + index * 2u + range_offset + (code_point - start) * 2u;
        u16 glyph_index;
        if (!read_u16(cmap, glyph_index_offset, glyph_index))
            return {};
        if (glyph_index == 0)
            return static_cast<u16>(0);
        return static_cast<u16>((glyph_index + delta) & 0xffffu);
    }
    return static_cast<u16>(0);
}

Optional<u16> TypefaceTrueTypeRinOS::cmap_format12_glyph(u32 code_point) const
{
    ReadonlyBytes cmap;
    u32 group_count;
    if (!slice_within(m_bytes.bytes(), m_cmap.offset, m_cmap.length, cmap)
        || !read_u32(cmap, 12, group_count) || group_count > maximum_glyphs
        || 16u + static_cast<u64>(group_count) * 12u > cmap.size())
        return {};
    for (u32 index = 0; index < group_count; ++index) {
        auto offset = 16u + index * 12u;
        u32 start;
        u32 end;
        u32 first_glyph;
        if (!read_u32(cmap, offset, start) || !read_u32(cmap, offset + 4, end)
            || !read_u32(cmap, offset + 8, first_glyph) || start > end)
            return {};
        if (code_point < start)
            return static_cast<u16>(0);
        if (code_point <= end) {
            u64 glyph = static_cast<u64>(first_glyph) + code_point - start;
            if (glyph >= m_glyph_count || glyph > NumericLimits<u16>::max())
                return static_cast<u16>(0);
            return static_cast<u16>(glyph);
        }
    }
    return static_cast<u16>(0);
}

u32 TypefaceTrueTypeRinOS::glyph_count() const { return m_glyph_count; }
u16 TypefaceTrueTypeRinOS::units_per_em() const { return m_units_per_em; }

u32 TypefaceTrueTypeRinOS::glyph_id_for_code_point(u32 code_point) const
{
    Optional<u16> glyph;
    if (code_point > 0x10ffffu)
        return 0;
    if (m_cmap_format == 4)
        glyph = cmap_format4_glyph(code_point);
    else if (m_cmap_format == 12)
        glyph = cmap_format12_glyph(code_point);
    if (!glyph.has_value() || glyph.value() >= m_glyph_count)
        return 0;
    return glyph.value();
}

FlyString const& TypefaceTrueTypeRinOS::family() const { return m_family; }
u16 TypefaceTrueTypeRinOS::weight() const { return m_weight; }
u16 TypefaceTrueTypeRinOS::width() const { return m_width; }
u8 TypefaceTrueTypeRinOS::slope() const { return m_slope; }

TypefaceDesignMetrics TypefaceTrueTypeRinOS::design_metrics() const
{
    return { m_ascender, m_descender, m_line_gap, m_x_height, m_advance_of_ascii_zero };
}

Optional<u16> TypefaceTrueTypeRinOS::glyph_advance(u32 glyph_id) const
{
    ReadonlyBytes hmtx;
    u16 advance;
    if (glyph_id >= m_glyph_count || !slice_within(m_bytes.bytes(), m_hmtx.offset, m_hmtx.length, hmtx))
        return {};
    auto metric_index = min(glyph_id, m_number_of_h_metrics - 1u);
    if (!read_u16(hmtx, metric_index * 4u, advance))
        return {};
    return advance;
}

bool TypefaceTrueTypeRinOS::transform_point(Transform const& transform, float x, float y, float& transformed_x, float& transformed_y) const
{
    transformed_x = transform.xx * x + transform.xy * y + transform.tx;
    transformed_y = transform.yx * x + transform.yy * y + transform.ty;
    return isfinite(transformed_x) && isfinite(transformed_y)
        && fabsf(transformed_x) <= maximum_design_coordinate
        && fabsf(transformed_y) <= maximum_design_coordinate;
}

bool TypefaceTrueTypeRinOS::append_command(Vector<GlyphOutlineCommand>& commands, GlyphOutlineCommand command, u32& command_budget) const
{
    if (command_budget == 0 || commands.try_append(command).is_error())
        return false;
    --command_budget;
    return true;
}

bool TypefaceTrueTypeRinOS::decode_simple_glyph(ReadonlyBytes glyph, i16 contour_count, Transform const& transform, DecodedOutline& outline, u32& point_budget) const
{
    Vector<u16> end_points;
    Vector<u8> flags;
    Vector<Point> points;
    size_t cursor = 10;
    u32 point_count;
    u16 instruction_length;
    i64 coordinate;

    if (contour_count <= 0 || static_cast<u32>(contour_count) > maximum_contours
        || cursor + static_cast<size_t>(contour_count) * 2u > glyph.size())
        return false;
    if (end_points.try_ensure_capacity(contour_count).is_error())
        return false;
    for (i32 index = 0; index < contour_count; ++index) {
        u16 end_point;
        if (!read_u16(glyph, cursor, end_point)
            || (index > 0 && end_point <= end_points.last()))
            return false;
        if (end_points.try_append(end_point).is_error())
            return false;
        cursor += 2;
    }
    point_count = static_cast<u32>(end_points.last()) + 1u;
    if (point_count == 0 || point_count > maximum_points || !read_u16(glyph, cursor, instruction_length))
        return false;
    cursor += 2;
    if (cursor > glyph.size() || instruction_length > glyph.size() - cursor)
        return false;
    cursor += instruction_length;

    if (flags.try_ensure_capacity(point_count).is_error() || points.try_ensure_capacity(point_count).is_error())
        return false;
    while (flags.size() < point_count) {
        u8 flag;
        if (cursor >= glyph.size())
            return false;
        flag = glyph[cursor++];
        if (flags.try_append(flag).is_error())
            return false;
        if ((flag & 0x08u) != 0u) {
            u8 repeat;
            if (cursor >= glyph.size())
                return false;
            repeat = glyph[cursor++];
            if (repeat > point_count - flags.size())
                return false;
            while (repeat-- > 0) {
                if (flags.try_append(flag).is_error())
                    return false;
            }
        }
    }

    coordinate = 0;
    for (u32 index = 0; index < point_count; ++index) {
        auto flag = flags[index];
        i32 delta = 0;
        if ((flag & 0x02u) != 0u) {
            if (cursor >= glyph.size())
                return false;
            delta = glyph[cursor++];
            if ((flag & 0x10u) == 0u)
                delta = -delta;
        } else if ((flag & 0x10u) == 0u) {
            i16 signed_delta;
            if (!read_i16(glyph, cursor, signed_delta))
                return false;
            delta = signed_delta;
            cursor += 2;
        }
        coordinate += delta;
        if (coordinate < -maximum_design_coordinate || coordinate > maximum_design_coordinate
            || points.try_append({ static_cast<float>(coordinate), 0, (flag & 1u) != 0u }).is_error())
            return false;
    }
    coordinate = 0;
    for (u32 index = 0; index < point_count; ++index) {
        auto flag = flags[index];
        i32 delta = 0;
        if ((flag & 0x04u) != 0u) {
            if (cursor >= glyph.size())
                return false;
            delta = glyph[cursor++];
            if ((flag & 0x20u) == 0u)
                delta = -delta;
        } else if ((flag & 0x20u) == 0u) {
            i16 signed_delta;
            if (!read_i16(glyph, cursor, signed_delta))
                return false;
            delta = signed_delta;
            cursor += 2;
        }
        coordinate += delta;
        if (coordinate < -maximum_design_coordinate || coordinate > maximum_design_coordinate)
            return false;
        points[index].y = static_cast<float>(coordinate);
    }
    if (cursor != glyph.size())
        return false;

    if (point_count > point_budget)
        return false;
    for (auto& point : points) {
        float transformed_x;
        float transformed_y;
        if (!transform_point(transform, point.x, point.y, transformed_x, transformed_y))
            return false;
        point.x = transformed_x;
        point.y = transformed_y;
    }

    u32 contour_start = 0;
    for (auto contour_end : end_points) {
        auto contour_size = static_cast<u32>(contour_end) - contour_start + 1u;
        Contour contour;
        if (contour.try_ensure_capacity(contour_size).is_error())
            return false;
        for (u32 index = 0; index < contour_size; ++index) {
            if (contour.try_append(points[contour_start + index]).is_error())
                return false;
        }
        if (outline.contours.try_append(move(contour)).is_error())
            return false;
        contour_start = static_cast<u32>(contour_end) + 1u;
    }
    point_budget -= point_count;
    return true;
}

bool TypefaceTrueTypeRinOS::outline_point_at(DecodedOutline const& outline, u32 point_index, Point& point) const
{
    for (auto const& contour : outline.contours) {
        if (point_index < contour.size()) {
            point = contour[point_index];
            return true;
        }
        point_index -= contour.size();
    }
    return false;
}

bool TypefaceTrueTypeRinOS::translate_outline(DecodedOutline& outline, float dx, float dy) const
{
    if (!isfinite(dx) || !isfinite(dy))
        return false;
    for (auto const& contour : outline.contours) {
        for (auto const& point : contour) {
            auto x = point.x + dx;
            auto y = point.y + dy;
            if (!isfinite(x) || !isfinite(y)
                || fabsf(x) > maximum_design_coordinate
                || fabsf(y) > maximum_design_coordinate)
                return false;
        }
    }
    for (auto& contour : outline.contours) {
        for (auto& point : contour) {
            point.x += dx;
            point.y += dy;
        }
    }
    return true;
}

bool TypefaceTrueTypeRinOS::decode_composite_glyph(ReadonlyBytes glyph, Transform const& transform, DecodedOutline& outline, u32& point_budget, u32 depth, u32& component_budget) const
{
    constexpr u16 arg_words = 0x0001;
    constexpr u16 args_are_xy = 0x0002;
    constexpr u16 round_xy_to_grid = 0x0004;
    constexpr u16 have_scale = 0x0008;
    constexpr u16 more_components = 0x0020;
    constexpr u16 have_xy_scale = 0x0040;
    constexpr u16 have_2x2 = 0x0080;
    constexpr u16 have_instructions = 0x0100;
    constexpr u16 use_my_metrics = 0x0200;
    constexpr u16 overlap_compound = 0x0400;
    constexpr u16 scaled_component_offset = 0x0800;
    constexpr u16 unscaled_component_offset = 0x1000;
    constexpr u16 known_flags = arg_words | args_are_xy | round_xy_to_grid
        | have_scale | more_components | have_xy_scale | have_2x2
        | have_instructions | use_my_metrics | overlap_compound
        | scaled_component_offset | unscaled_component_offset;
    size_t cursor = 10;
    u16 flags = 0;

    do {
        u16 component_glyph;
        i16 arg_x = 0;
        i16 arg_y = 0;
        u16 parent_point_index = 0;
        u16 component_point_index = 0;
        Transform child;
        Transform combined;
        DecodedOutline component_outline;
        if (component_budget == 0 || !read_u16(glyph, cursor, flags)
            || !read_u16(glyph, cursor + 2, component_glyph))
            return false;
        cursor += 4;
        if ((flags & ~known_flags) != 0u
            || ((flags & have_scale) != 0u && (flags & have_xy_scale) != 0u)
            || ((flags & have_scale) != 0u && (flags & have_2x2) != 0u)
            || ((flags & have_xy_scale) != 0u && (flags & have_2x2) != 0u)
            || ((flags & scaled_component_offset) != 0u
                && (flags & unscaled_component_offset) != 0u))
            return false;
        if ((flags & args_are_xy) != 0u) {
            if ((flags & arg_words) != 0u) {
                if (!read_i16(glyph, cursor, arg_x) || !read_i16(glyph, cursor + 2, arg_y))
                    return false;
                cursor += 4;
            } else {
                if (cursor > glyph.size() || 2 > glyph.size() - cursor)
                    return false;
                arg_x = static_cast<i8>(glyph[cursor]);
                arg_y = static_cast<i8>(glyph[cursor + 1]);
                cursor += 2;
            }
        } else {
            if ((flags & arg_words) != 0u) {
                if (!read_u16(glyph, cursor, parent_point_index)
                    || !read_u16(glyph, cursor + 2, component_point_index))
                    return false;
                cursor += 4;
            } else {
                if (cursor > glyph.size() || 2 > glyph.size() - cursor)
                    return false;
                parent_point_index = glyph[cursor];
                component_point_index = glyph[cursor + 1];
                cursor += 2;
            }
        }
        if ((flags & have_scale) != 0u) {
            i16 scale;
            if (!read_i16(glyph, cursor, scale))
                return false;
            child.xx = child.yy = static_cast<float>(scale) / 16384.0f;
            cursor += 2;
        } else if ((flags & have_xy_scale) != 0u) {
            i16 x_scale;
            i16 y_scale;
            if (!read_i16(glyph, cursor, x_scale) || !read_i16(glyph, cursor + 2, y_scale))
                return false;
            child.xx = static_cast<float>(x_scale) / 16384.0f;
            child.yy = static_cast<float>(y_scale) / 16384.0f;
            cursor += 4;
        } else if ((flags & have_2x2) != 0u) {
            i16 xx;
            i16 xy;
            i16 yx;
            i16 yy;
            if (!read_i16(glyph, cursor, xx) || !read_i16(glyph, cursor + 2, xy)
                || !read_i16(glyph, cursor + 4, yx) || !read_i16(glyph, cursor + 6, yy))
                return false;
            child.xx = static_cast<float>(xx) / 16384.0f;
            child.xy = static_cast<float>(xy) / 16384.0f;
            child.yx = static_cast<float>(yx) / 16384.0f;
            child.yy = static_cast<float>(yy) / 16384.0f;
            cursor += 8;
        }
        combined.xx = transform.xx * child.xx + transform.xy * child.yx;
        combined.xy = transform.xx * child.xy + transform.xy * child.yy;
        combined.yx = transform.yx * child.xx + transform.yy * child.yx;
        combined.yy = transform.yx * child.xy + transform.yy * child.yy;
        combined.tx = transform.tx;
        combined.ty = transform.ty;
        if (!isfinite(combined.xx) || !isfinite(combined.xy)
            || !isfinite(combined.yx) || !isfinite(combined.yy)
            || !isfinite(combined.tx) || !isfinite(combined.ty))
            return false;
        --component_budget;
        if (!decode_glyph(component_glyph, combined, component_outline,
                          point_budget, depth + 1u, component_budget))
            return false;
        if ((flags & args_are_xy) != 0u) {
            float component_x = arg_x;
            float component_y = arg_y;
            float dx;
            float dy;
            if ((flags & scaled_component_offset) != 0u) {
                auto scaled_x = child.xx * component_x + child.xy * component_y;
                component_y = child.yx * component_x + child.yy * component_y;
                component_x = scaled_x;
            }
            dx = transform.xx * component_x + transform.xy * component_y;
            dy = transform.yx * component_x + transform.yy * component_y;
            if ((flags & round_xy_to_grid) != 0u) {
                dx = roundf(dx);
                dy = roundf(dy);
            }
            if (!translate_outline(component_outline, dx, dy))
                return false;
        } else {
            Point parent_point;
            Point component_point;
            if (!outline_point_at(outline, parent_point_index, parent_point)
                || !outline_point_at(component_outline, component_point_index,
                                     component_point)
                || !translate_outline(component_outline,
                                      parent_point.x - component_point.x,
                                      parent_point.y - component_point.y))
                return false;
        }
        for (auto& contour : component_outline.contours) {
            if (outline.contours.try_append(move(contour)).is_error())
                return false;
        }
    } while ((flags & more_components) != 0u);

    if ((flags & have_instructions) != 0u) {
        u16 instruction_length;
        if (!read_u16(glyph, cursor, instruction_length))
            return false;
        cursor += 2;
        if (cursor > glyph.size() || instruction_length > glyph.size() - cursor)
            return false;
        cursor += instruction_length;
    }
    return cursor == glyph.size();
}

bool TypefaceTrueTypeRinOS::decode_glyph(u32 glyph_id, Transform const& transform, DecodedOutline& outline, u32& point_budget, u32 depth, u32& component_budget) const
{
    ReadonlyBytes glyf;
    auto offset = glyph_offset(glyph_id);
    auto next_offset = glyph_offset(glyph_id + 1u);
    i16 contour_count;
    if (depth > maximum_composite_depth || !offset.has_value() || !next_offset.has_value()
        || next_offset.value() < offset.value() || !slice_within(m_bytes.bytes(), m_glyf.offset, m_glyf.length, glyf))
        return false;
    if (next_offset.value() == offset.value())
        return true;
    auto glyph_length = next_offset.value() - offset.value();
    if (glyph_length < 10 || offset.value() > glyf.size() || glyph_length > glyf.size() - offset.value())
        return false;
    auto glyph = glyf.slice(offset.value(), glyph_length);
    if (!read_i16(glyph, 0, contour_count))
        return false;
    if (contour_count >= 0)
        return contour_count == 0 || decode_simple_glyph(glyph, contour_count, transform, outline, point_budget);
    return decode_composite_glyph(glyph, transform, outline, point_budget, depth, component_budget);
}

bool TypefaceTrueTypeRinOS::append_outline_commands(DecodedOutline const& outline, Vector<GlyphOutlineCommand>& commands, u32& command_budget) const
{
    for (auto const& contour : outline.contours) {
        auto contour_size = contour.size();
        auto point_at = [&contour, contour_size](u32 index) -> Point const& {
            return contour[index % contour_size];
        };
        Point start;
        u32 index;
        if (contour_size == 0)
            return false;
        if (point_at(0).on_curve) {
            start = point_at(0);
            index = 1;
        } else if (point_at(contour_size - 1).on_curve) {
            start = point_at(contour_size - 1);
            index = 0;
        } else {
            auto const& first = point_at(0);
            auto const& last = point_at(contour_size - 1);
            start = { (first.x + last.x) * 0.5f,
                      (first.y + last.y) * 0.5f, true };
            index = 0;
        }
        if (!append_command(commands,
                            { GlyphOutlineCommand::Type::MoveTo, start.x,
                              start.y }, command_budget))
            return false;
        while (index < contour_size) {
            auto const& point = point_at(index);
            if (point.on_curve) {
                if (!append_command(commands,
                                    { GlyphOutlineCommand::Type::LineTo,
                                      point.x, point.y }, command_budget))
                    return false;
                ++index;
                continue;
            }
            auto const& next = point_at(index + 1u);
            Point end = next.on_curve ? next : Point {
                (point.x + next.x) * 0.5f, (point.y + next.y) * 0.5f, true
            };
            if (!append_command(commands,
                                { GlyphOutlineCommand::Type::QuadraticCurveTo,
                                  end.x, end.y, point.x, point.y },
                                command_budget))
                return false;
            ++index;
            if (next.on_curve)
                ++index;
        }
        if (!append_command(commands, { GlyphOutlineCommand::Type::Close },
                            command_budget))
            return false;
    }
    return true;
}

Optional<Vector<GlyphOutlineCommand>> TypefaceTrueTypeRinOS::glyph_outline(u32 glyph_id) const
{
    DecodedOutline outline;
    Vector<GlyphOutlineCommand> commands;
    u32 point_budget = maximum_points;
    u32 command_budget = maximum_outline_commands;
    u32 component_budget = maximum_composite_components;
    if (glyph_id >= m_glyph_count || commands.try_ensure_capacity(64).is_error()
        || !decode_glyph(glyph_id, {}, outline, point_budget, 0,
                         component_budget)
        || !append_outline_commands(outline, commands, command_budget))
        return {};
    return commands;
}

ReadonlyBytes TypefaceTrueTypeRinOS::buffer() const { return m_bytes.bytes(); }
u32 TypefaceTrueTypeRinOS::ttc_index() const { return m_ttc_index; }

}
