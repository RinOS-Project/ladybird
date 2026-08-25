/*
 * Copyright (c) 2026, RinOS contributors
 *
 * SPDX-License-Identifier: BSD-2-Clause
 */

#include <LibGfx/Font/Typeface.h>
#include <LibGfx/PathAquamarine.h>
#include <LibGfx/TextLayout.h>
#include <LibTest/TestCase.h>

namespace {

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
