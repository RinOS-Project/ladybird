/*
 * Copyright (c) 2023, MacDue <macdue@dueutil.tech>
 * Copyright (c) 2025, Aliaksandr Kalenik <kalenik.aliaksandr@gmail.com>
 * Copyright (c) 2025, Andreas Kling <andreas@ladybird.org>
 *
 * SPDX-License-Identifier: BSD-2-Clause
 */

#include <AK/TypeCasts.h>
#include <AK/Utf16String.h>
#include <LibGfx/Font/Font.h>
#include <LibGfx/Font/FontDatabase.h>
#ifndef AK_OS_RINOS
#include <LibGfx/Font/TypefaceSkia.h>
#endif
#include <LibGfx/TextLayout.h>

#ifndef AK_OS_RINOS
#include <core/SkFont.h>
#include <core/SkFontMetrics.h>
#include <core/SkFontTypes.h>
#endif

#include <harfbuzz/hb-ot.h>
#include <harfbuzz/hb.h>

namespace Gfx {

Font::Font(NonnullRefPtr<Typeface const> typeface, float point_width, float point_height, unsigned dpi_x, unsigned dpi_y, FontVariationSettings const variations, ShapeFeatures const& features)
    : m_typeface(move(typeface))
    , m_point_width(point_width)
    , m_point_height(point_height)
    , m_font_variation_settings(move(variations))
    , m_shape_features(features)
{
    float const units_per_em = m_typeface->units_per_em();
    m_x_scale = (point_width * dpi_x) / (POINTS_PER_INCH * units_per_em);
    m_y_scale = (point_height * dpi_y) / (POINTS_PER_INCH * units_per_em);

    m_pixel_size = m_point_height * (DEFAULT_DPI / POINTS_PER_INCH);

#ifdef AK_OS_RINOS
    auto* hb_font_p = hb_font_create(m_typeface->harfbuzz_typeface());
    hb_font_set_scale(hb_font_p, m_pixel_size * 64, m_pixel_size * 64);
    hb_font_extents_t extents;
    hb_font_get_h_extents(hb_font_p, &extents);
    FontPixelMetrics metrics;
    metrics.size = m_pixel_size;
    metrics.ascent = extents.ascender / 64.0f;
    metrics.descent = -extents.descender / 64.0f;
    metrics.line_gap = extents.line_gap / 64.0f;
    metrics.x_height = metrics.ascent * 0.5f;
    metrics.advance_of_ascii_zero = 0;
    hb_codepoint_t glyph;
    if (hb_font_get_nominal_glyph(hb_font_p, '0', &glyph)) {
        metrics.advance_of_ascii_zero = hb_font_get_glyph_h_advance(hb_font_p, glyph) / 64.0f;
    }
    hb_font_destroy(hb_font_p);
    m_pixel_metrics = metrics;
#else
    auto const* sk_typeface = as<TypefaceSkia>(*m_typeface).sk_typeface();
    SkFont const font { sk_ref_sp(sk_typeface), m_pixel_size };

    SkFontMetrics skMetrics;
    font.getMetrics(&skMetrics);

    FontPixelMetrics metrics;
    metrics.size = font.getSize();
    metrics.x_height = skMetrics.fXHeight;
    metrics.advance_of_ascii_zero = font.measureText("0", 1, SkTextEncoding::kUTF8);
    metrics.ascent = -skMetrics.fAscent;
    metrics.descent = skMetrics.fDescent;
    metrics.line_gap = skMetrics.fLeading;

    m_pixel_metrics = metrics;
#endif
}

ScaledFontMetrics Font::metrics() const
{
#ifdef AK_OS_RINOS
    auto* hb_font_p = hb_font_create(m_typeface->harfbuzz_typeface());
    hb_font_set_scale(hb_font_p, m_pixel_size * 64, m_pixel_size * 64);
    hb_font_extents_t extents;
    hb_font_get_h_extents(hb_font_p, &extents);
    ScaledFontMetrics metrics;
    metrics.ascender = extents.ascender / 64.0f;
    metrics.descender = -extents.descender / 64.0f;
    metrics.line_gap = extents.line_gap / 64.0f;
    metrics.x_height = metrics.ascender * 0.5f;
    hb_font_destroy(hb_font_p);
    return metrics;
#else
    SkFontMetrics sk_metrics;
    skia_font(1).getMetrics(&sk_metrics);

    ScaledFontMetrics metrics;
    metrics.ascender = -sk_metrics.fAscent;
    metrics.descender = sk_metrics.fDescent;
    metrics.line_gap = sk_metrics.fLeading;
    metrics.x_height = sk_metrics.fXHeight;
    return metrics;
#endif
}

float Font::width(Utf16View const& view) const { return measure_text_width(view, *this); }

float Font::glyph_width(u32 code_point) const
{
    auto string = Utf16String::from_code_point(code_point);
    return measure_text_width(string.utf16_view(), *this);
}

NonnullRefPtr<Font> Font::scaled_with_size(float point_size) const
{
    if (point_size == m_point_height && point_size == m_point_width)
        return *const_cast<Font*>(this);

    // FIXME: Should we be discarding m_font_variation_settings and m_shape_features here?
    return m_typeface->font(point_size);
}

NonnullRefPtr<Font> Font::with_size(float point_size) const
{
    return scaled_with_size(point_size);
}

float Font::pixel_size() const
{
    return m_pixel_size;
}

float Font::point_size() const
{
    return m_point_height;
}

Font::~Font()
{
    if (m_harfbuzz_font)
        hb_font_destroy(m_harfbuzz_font);
}

Font const& Font::bold_variant() const
{
    if (m_bold_variant)
        return *m_bold_variant;
    m_bold_variant = Gfx::FontDatabase::the().get(family(), point_size(), 700, Gfx::FontWidth::Normal, 0);
    if (!m_bold_variant)
        m_bold_variant = this;
    return *m_bold_variant;
}

hb_font_t* Font::harfbuzz_font() const
{
    if (!m_harfbuzz_font) {
        m_harfbuzz_font = hb_font_create(typeface().harfbuzz_typeface());
        hb_font_set_scale(m_harfbuzz_font, pixel_size() * text_shaping_resolution, pixel_size() * text_shaping_resolution);
        hb_font_set_ptem(m_harfbuzz_font, point_size());

        auto variations = m_font_variation_settings.axes;
        if (!variations.is_empty()) {
            Vector<hb_variation_t> hb_list;
            hb_list.ensure_capacity(variations.size());

            for (auto const& axis : variations) {
                hb_list.unchecked_append(hb_variation_t { axis.key.to_u32(), axis.value });
            }

            hb_font_set_variations(m_harfbuzz_font, hb_list.data(), hb_list.size());
        }
    }
    return m_harfbuzz_font;
}

#ifndef AK_OS_RINOS
SkFont Font::skia_font(float scale) const
{
    auto const& sk_typeface = as<TypefaceSkia>(*m_typeface).sk_typeface();
    auto sk_font = SkFont { sk_ref_sp(sk_typeface), pixel_size() * scale };
    sk_font.setSubpixel(true);
    return sk_font;
}
#endif

Font::ShapingCache::~ShapingCache()
{
    clear();
}

void Font::ShapingCache::clear()
{
    for (auto& it : map) {
        hb_buffer_destroy(it.value);
    }
    map.clear();
    for (auto& buffer : single_ascii_character_map) {
        if (buffer) {
            hb_buffer_destroy(buffer);
            buffer = nullptr;
        }
    }
}

static bool hb_face_has_table(hb_face_t* face, hb_tag_t tag)
{
    hb_blob_t* blob = hb_face_reference_table(face, tag);
    unsigned len = hb_blob_get_length(blob);
    hb_blob_destroy(blob);
    return len > 0;
}

bool Font::is_emoji_font() const
{
    if (m_is_emoji_font == TriState::Unknown) {
        // NOTE: This is a heuristic approach to determine if a font is an emoji font.
        //       AFAIK there is no definitive way to know this from the font data itself.

        // 1. If the family name contains "emoji", it's probably an emoji font.
        bool name_contains_emoji = family().bytes_as_string_view().contains("emoji"sv);

        // 2. Check for color font tables and absence of regular text glyphs.
        auto* hb_font = harfbuzz_font();
        hb_face_t* face = hb_font_get_face(hb_font);

        bool has_colr = hb_ot_color_has_layers(hb_font_get_face(hb_font));
        bool has_svg = hb_ot_color_has_svg(hb_font_get_face(hb_font));

        bool has_sbix = hb_face_has_table(face, HB_TAG('s', 'b', 'i', 'x'));
        bool has_cbdt = hb_face_has_table(face, HB_TAG('C', 'B', 'D', 'T'));
        bool has_cblc = hb_face_has_table(face, HB_TAG('C', 'B', 'L', 'C'));
        bool has_any_color = has_colr || has_svg || has_sbix || (has_cbdt && has_cblc);

        auto looks_like_text = [&]() {
            hb_codepoint_t uppercase_a_glyph_id = 0;
            hb_codepoint_t lowercase_a_glyph_id = 0;
            bool has_uppercase_a = hb_font_get_nominal_glyph(hb_font, 'A', &uppercase_a_glyph_id);
            bool has_lowercase_a = hb_font_get_nominal_glyph(hb_font, 'a', &lowercase_a_glyph_id);
            return has_uppercase_a && has_lowercase_a;
        }();

        m_is_emoji_font = (name_contains_emoji && !looks_like_text) || (has_any_color && !looks_like_text) ? TriState::True : TriState::False;
        return false;
    }

    return m_is_emoji_font == TriState::True;
}

}
