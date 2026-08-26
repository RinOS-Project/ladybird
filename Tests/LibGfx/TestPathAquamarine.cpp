/*
 * Copyright (c) 2026, RinOS contributors
 *
 * SPDX-License-Identifier: BSD-2-Clause
 */

#include <AK/Array.h>
#include <LibGfx/Font/Typeface.h>
#include <LibGfx/Font/TypefaceTrueTypeRinOS.h>
#include <LibGfx/PathAquamarine.h>
#include <LibGfx/TextLayout.h>
#include <LibTest/TestCase.h>

namespace {

static void write_u16_be(Array<u8, 335>& bytes, size_t offset, u16 value)
{
    bytes[offset] = value >> 8;
    bytes[offset + 1] = value;
}

static void write_u32_be(Array<u8, 335>& bytes, size_t offset, u32 value)
{
    bytes[offset] = value >> 24;
    bytes[offset + 1] = value >> 16;
    bytes[offset + 2] = value >> 8;
    bytes[offset + 3] = value;
}

static Array<u8, 335> make_point_matching_composite_font(bool invalid_component_point)
{
    constexpr size_t cmap_offset = 124;
    constexpr size_t head_offset = 168;
    constexpr size_t hhea_offset = 222;
    constexpr size_t hmtx_offset = 258;
    constexpr size_t loca_offset = 270;
    constexpr size_t maxp_offset = 286;
    constexpr size_t glyf_offset = 292;
    struct Table {
        u32 tag;
        u32 offset;
        u32 length;
    };
    constexpr Array<Table, 7> tables { {
        { 0x636d6170u, cmap_offset, 44u }, // cmap
        { 0x676c7966u, glyf_offset, 43u }, // glyf
        { 0x68656164u, head_offset, 54u }, // head
        { 0x68686561u, hhea_offset, 36u }, // hhea
        { 0x686d7478u, hmtx_offset, 12u }, // hmtx
        { 0x6c6f6361u, loca_offset, 16u }, // loca
        { 0x6d617870u, maxp_offset, 6u }, // maxp
    } };
    Array<u8, 335> bytes {};

    write_u32_be(bytes, 0, 0x00010000u);
    write_u16_be(bytes, 4, tables.size());
    for (size_t index = 0; index < tables.size(); ++index) {
        auto record = 12 + index * 16;
        write_u32_be(bytes, record, tables[index].tag);
        write_u32_be(bytes, record + 8, tables[index].offset);
        write_u32_be(bytes, record + 12, tables[index].length);
    }

    // A format-4 cmap with a single U+0041 -> glyph 1 mapping and a required
    // sentinel. The outline test below calls glyph 2 directly.
    write_u16_be(bytes, cmap_offset, 0);
    write_u16_be(bytes, cmap_offset + 2, 1);
    write_u16_be(bytes, cmap_offset + 4, 3);
    write_u16_be(bytes, cmap_offset + 6, 1);
    write_u32_be(bytes, cmap_offset + 8, 12);
    write_u16_be(bytes, cmap_offset + 12, 4);
    write_u16_be(bytes, cmap_offset + 14, 32);
    write_u16_be(bytes, cmap_offset + 18, 4);
    write_u16_be(bytes, cmap_offset + 20, 4);
    write_u16_be(bytes, cmap_offset + 22, 1);
    write_u16_be(bytes, cmap_offset + 26, 0x0041);
    write_u16_be(bytes, cmap_offset + 28, 0xffff);
    write_u16_be(bytes, cmap_offset + 32, 0x0041);
    write_u16_be(bytes, cmap_offset + 34, 0xffff);
    write_u16_be(bytes, cmap_offset + 36, 0xffc0);
    write_u16_be(bytes, cmap_offset + 38, 1);

    write_u16_be(bytes, head_offset + 18, 1000);
    write_u16_be(bytes, head_offset + 50, 1);
    write_u16_be(bytes, hhea_offset + 4, 800);
    write_u16_be(bytes, hhea_offset + 6, 0xff38);
    write_u16_be(bytes, hhea_offset + 34, 3);
    for (size_t index = 0; index < 3; ++index)
        write_u16_be(bytes, hmtx_offset + index * 4, 500);
    write_u32_be(bytes, loca_offset + 8, 17);
    write_u32_be(bytes, loca_offset + 12, 43);
    write_u32_be(bytes, maxp_offset, 0x00010000u);
    write_u16_be(bytes, maxp_offset + 4, 3);

    // Glyph 1 is a two-point horizontal contour from (0,0) to (100,0).
    write_u16_be(bytes, glyf_offset, 1);
    write_u16_be(bytes, glyf_offset + 10, 1);
    bytes[glyf_offset + 14] = 0x31; // on-curve, x/y unchanged
    bytes[glyf_offset + 15] = 0x33; // on-curve, positive short x, y unchanged
    bytes[glyf_offset + 16] = 100;

    // Glyph 2 first positions glyph 1 at the origin, then uses point
    // attachment: its previous point 1 and glyph 1's point 0 must coincide.
    // The second contour must therefore run from (100,0) to (200,0).
    auto composite_offset = glyf_offset + 17;
    write_u16_be(bytes, composite_offset, 0xffff);
    write_u16_be(bytes, composite_offset + 10, 0x0023); // words + XY + more
    write_u16_be(bytes, composite_offset + 12, 1);
    write_u16_be(bytes, composite_offset + 18, 0x0001); // words, point match
    write_u16_be(bytes, composite_offset + 20, 1);
    write_u16_be(bytes, composite_offset + 22, 1);
    write_u16_be(bytes, composite_offset + 24,
                 invalid_component_point ? 2 : 0);
    return bytes;
}

class TestOutlineTypeface final : public Gfx::Typeface {
public:
    static NonnullRefPtr<TestOutlineTypeface> create(bool exposes_outlines)
    {
        return adopt_ref(*new TestOutlineTypeface(exposes_outlines));
    }

    virtual u32 glyph_count() const override { return 38; }
    virtual u16 units_per_em() const override { return 1000; }
    virtual u32 glyph_id_for_code_point(u32 code_point) const override
    {
        return code_point == 'A' ? 37 : 0;
    }
    virtual FlyString const& family() const override
    {
        static FlyString const family = "Path outline test"_fly_string;
        return family;
    }
    virtual u16 weight() const override { return 400; }
    virtual u16 width() const override { return 5; }
    virtual u8 slope() const override { return 0; }
    virtual Gfx::TypefaceDesignMetrics design_metrics() const override
    {
        return { .ascender = 800, .descender = -200, .line_gap = 0, .x_height = 500, .advance_of_ascii_zero = 600 };
    }
    virtual Optional<u16> glyph_advance(u32 glyph_id) const override
    {
        return glyph_id == 37 ? Optional<u16> { 600 } : Optional<u16> {};
    }
    virtual Optional<Vector<Gfx::GlyphOutlineCommand>> glyph_outline(u32 glyph_id) const override
    {
        if (!m_exposes_outlines || glyph_id != 37)
            return {};
        Vector<Gfx::GlyphOutlineCommand> commands;
        commands.append({ Gfx::GlyphOutlineCommand::Type::MoveTo, 0, 0 });
        commands.append({ Gfx::GlyphOutlineCommand::Type::LineTo, 1000, 0 });
        commands.append({ Gfx::GlyphOutlineCommand::Type::LineTo, 500, 1000 });
        commands.append({ Gfx::GlyphOutlineCommand::Type::Close });
        return commands;
    }
    virtual bool has_glyph_outlines() const override { return m_exposes_outlines; }

private:
    explicit TestOutlineTypeface(bool exposes_outlines)
        : m_exposes_outlines(exposes_outlines)
    {
    }

    virtual ReadonlyBytes buffer() const override { return {}; }
    virtual u32 ttc_index() const override { return 0; }

    bool m_exposes_outlines { false };
};

static NonnullRefPtr<Gfx::GlyphRun> glyph_run_for(NonnullRefPtr<Gfx::Font const> font, u32 glyph_id)
{
    Vector<Gfx::DrawGlyph> glyphs;
    glyphs.append({ .position = {}, .length_in_code_units = 1, .glyph_width = 9.6f, .glyph_id = glyph_id });
    return adopt_ref(*new Gfx::GlyphRun(move(glyphs), move(font), Gfx::GlyphRun::TextType::Common, 9.6f));
}

}

TEST_CASE(glyph_run_uses_the_typeface_outline_for_the_shaped_glyph_id)
{
    auto typeface = TestOutlineTypeface::create(true);
    NonnullRefPtr<Gfx::Font const> font = typeface->font(12.0f);
    auto path = Gfx::PathImplAquamarine::create();

    path->glyph_run(*glyph_run_for(font, 37));

    EXPECT_EQ(path->contours().size(), 1u);
    EXPECT_EQ(path->contours()[0].points.size(), 3u);
    EXPECT(path->contours()[0].closed);
    EXPECT_APPROXIMATE(path->contours()[0].points[0].x(), 0.0f);
    EXPECT_APPROXIMATE(path->contours()[0].points[1].x(), 16.0f);

    // Glyph ID 65 has no outline in this typeface. It must not be treated as
    // the Unicode scalar U+0041 and substituted from the PSF UI font.
    auto missing_outline_path = Gfx::PathImplAquamarine::create();
    missing_outline_path->glyph_run(*glyph_run_for(font, 65));
    EXPECT(missing_outline_path->is_empty());
}

TEST_CASE(outline_less_typefaces_do_not_synthesize_a_bitmap_text_path)
{
    auto typeface = TestOutlineTypeface::create(false);
    NonnullRefPtr<Gfx::Font const> font = typeface->font(12.0f);
    auto path = Gfx::PathImplAquamarine::create();

    path->glyph_run(*glyph_run_for(font, 37));

    EXPECT(path->is_empty());
}

TEST_CASE(text_along_path_uses_shaped_outline_commands)
{
    auto typeface = TestOutlineTypeface::create(true);
    NonnullRefPtr<Gfx::Font const> font = typeface->font(12.0f);
    auto source = Gfx::PathImplAquamarine::create();
    source->move_to({ 0, 0 });
    source->line_to({ 100, 0 });

    auto placed = source->place_text_along("A"sv, *font);

    EXPECT(!placed->is_empty());
    auto const& placed_aquamarine = static_cast<Gfx::PathImplAquamarine const&>(*placed);
    EXPECT_EQ(placed_aquamarine.contours().size(), 1u);
    EXPECT(placed_aquamarine.contours()[0].closed);
}

TEST_CASE(truetype_point_matching_composite_preserves_component_topology)
{
    auto bytes = make_point_matching_composite_font(false);
    auto typeface_or_error = Gfx::TypefaceTrueTypeRinOS::try_load(
        { bytes.data(), bytes.size() }, 0);
    EXPECT(!typeface_or_error.is_error());
    if (typeface_or_error.is_error())
        return;
    auto outline = typeface_or_error.release_value()->glyph_outline(2);
    EXPECT(outline.has_value());
    if (!outline.has_value())
        return;
    EXPECT_EQ(outline->size(), 6u);
    EXPECT_EQ((*outline)[0].type, Gfx::GlyphOutlineCommand::Type::MoveTo);
    EXPECT_EQ((*outline)[1].type, Gfx::GlyphOutlineCommand::Type::LineTo);
    EXPECT_EQ((*outline)[3].type, Gfx::GlyphOutlineCommand::Type::MoveTo);
    EXPECT_EQ((*outline)[4].type, Gfx::GlyphOutlineCommand::Type::LineTo);
    EXPECT_APPROXIMATE((*outline)[0].x, 0.0f);
    EXPECT_APPROXIMATE((*outline)[1].x, 100.0f);
    EXPECT_APPROXIMATE((*outline)[3].x, 100.0f);
    EXPECT_APPROXIMATE((*outline)[4].x, 200.0f);
}

TEST_CASE(truetype_point_matching_composite_rejects_an_out_of_range_anchor)
{
    auto bytes = make_point_matching_composite_font(true);
    auto typeface_or_error = Gfx::TypefaceTrueTypeRinOS::try_load(
        { bytes.data(), bytes.size() }, 0);
    EXPECT(!typeface_or_error.is_error());
    if (typeface_or_error.is_error())
        return;
    EXPECT(!typeface_or_error.release_value()->glyph_outline(2).has_value());
}
