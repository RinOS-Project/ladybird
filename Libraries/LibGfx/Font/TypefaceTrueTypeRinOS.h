/*
 * Copyright (c) 2026, RinOS contributors
 *
 * SPDX-License-Identifier: BSD-2-Clause
 */

#pragma once

#include <AK/ByteBuffer.h>
#include <AK/FlyString.h>
#include <LibGfx/Font/Typeface.h>

namespace Gfx {

// A bounded native TrueType `glyf` reader for the freestanding RinOS build.
// It intentionally accepts only SFNT TrueType outlines: CFF/OTTO, variable,
// colour, bitmap-only and malformed fonts are rejected at load time instead
// of being represented by the PSF fallback as if they had loaded.
class TypefaceTrueTypeRinOS final : public Typeface {
public:
    static ErrorOr<NonnullRefPtr<TypefaceTrueTypeRinOS>> try_load(ReadonlyBytes, u32 ttc_index);

    virtual ~TypefaceTrueTypeRinOS() override;

    virtual u32 glyph_count() const override;
    virtual u16 units_per_em() const override;
    virtual u32 glyph_id_for_code_point(u32) const override;
    virtual FlyString const& family() const override;
    virtual u16 weight() const override;
    virtual u16 width() const override;
    virtual u8 slope() const override;
    virtual TypefaceDesignMetrics design_metrics() const override;
    virtual Optional<u16> glyph_advance(u32) const override;
    virtual Optional<Vector<GlyphOutlineCommand>> glyph_outline(u32) const override;
    virtual bool has_glyph_outlines() const override { return true; }

private:
    struct TableView {
        u32 offset { 0 };
        u32 length { 0 };
    };

    struct Point {
        float x { 0 };
        float y { 0 };
        bool on_curve { false };
    };

    using Contour = Vector<Point>;

    // Composite TrueType glyphs may attach a component point to a point in an
    // earlier component. Preserve that topology until the whole glyph is
    // resolved; command-only output cannot represent those point references.
    struct DecodedOutline {
        Vector<Contour> contours;
    };

    struct Transform {
        float xx { 1 };
        float xy { 0 };
        float yx { 0 };
        float yy { 1 };
        float tx { 0 };
        float ty { 0 };
    };

    explicit TypefaceTrueTypeRinOS(ByteBuffer&&);

    bool parse(u32 ttc_index);
    bool select_cmap();
    Optional<u32> glyph_offset(u32 glyph_id) const;
    Optional<u16> cmap_format4_glyph(u32 code_point) const;
    Optional<u16> cmap_format12_glyph(u32 code_point) const;
    bool decode_glyph(u32 glyph_id, Transform const&, DecodedOutline&, u32& point_budget, u32 depth, u32& component_budget) const;
    bool decode_simple_glyph(ReadonlyBytes, i16 contour_count, Transform const&, DecodedOutline&, u32& point_budget) const;
    bool decode_composite_glyph(ReadonlyBytes, Transform const&, DecodedOutline&, u32& point_budget, u32 depth, u32& component_budget) const;
    bool append_outline_commands(DecodedOutline const&, Vector<GlyphOutlineCommand>&, u32& command_budget) const;
    bool outline_point_at(DecodedOutline const&, u32 point_index, Point&) const;
    bool translate_outline(DecodedOutline&, float dx, float dy) const;
    bool append_command(Vector<GlyphOutlineCommand>&, GlyphOutlineCommand, u32& command_budget) const;
    bool transform_point(Transform const&, float x, float y, float& transformed_x, float& transformed_y) const;

    virtual ReadonlyBytes buffer() const override;
    virtual u32 ttc_index() const override;

    ByteBuffer m_bytes;
    TableView m_cmap;
    TableView m_head;
    TableView m_hhea;
    TableView m_hmtx;
    TableView m_loca;
    TableView m_glyf;
    u32 m_cmap_format { 0 };
    u32 m_glyph_count { 0 };
    u32 m_number_of_h_metrics { 0 };
    u16 m_units_per_em { 0 };
    u16 m_weight { 400 };
    u16 m_width { 5 };
    u8 m_slope { 0 };
    i16 m_ascender { 0 };
    i16 m_descender { 0 };
    i16 m_line_gap { 0 };
    i16 m_x_height { 0 };
    u16 m_advance_of_ascii_zero { 0 };
    u16 m_index_to_loc_format { 0 };
    u32 m_ttc_index { 0 };
    FlyString m_family;
};

}
