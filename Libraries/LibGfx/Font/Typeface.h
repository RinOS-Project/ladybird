/*
 * Copyright (c) 2022, the SerenityOS developers.
 *
 * SPDX-License-Identifier: BSD-2-Clause
 */

#pragma once

#include <AK/HashMap.h>
#include <AK/OwnPtr.h>
#include <AK/QuickSort.h>
#include <AK/RefCounted.h>
#include <AK/Vector.h>
#include <LibGfx/Font/FontData.h>
#include <LibGfx/Font/FontVariationSettings.h>
#include <LibGfx/Forward.h>
#include <LibGfx/ShapeFeature.h>

#define POINTS_PER_INCH 72.0f
#define DEFAULT_DPI 96

class SkTypeface;
struct hb_blob_t;
struct hb_face_t;

namespace Gfx {

class Font;

struct ScaledFontMetrics {
    float ascender { 0 };
    float descender { 0 };
    float line_gap { 0 };
    float x_height { 0 };

    float height() const
    {
        return ascender + descender;
    }
};

// A platform-independent outline representation used by the RinOS software
// painter. Coordinates are expressed in the typeface's design units, with a
// positive Y axis pointing up as specified by TrueType. Backends which do not
// expose outlines simply return an empty Optional from glyph_outline().
struct GlyphOutlineCommand {
    enum class Type : u8 {
        MoveTo,
        LineTo,
        QuadraticCurveTo,
        Close,
    };

    Type type { Type::Close };
    float x { 0 };
    float y { 0 };
    float control_x { 0 };
    float control_y { 0 };
};

struct TypefaceDesignMetrics {
    i16 ascender { 0 };
    i16 descender { 0 };
    i16 line_gap { 0 };
    i16 x_height { 0 };
    u16 advance_of_ascii_zero { 0 };
};

struct FontCacheKey {
    float point_size;
    Vector<FontVariationAxis> axes;
    Gfx::ShapeFeatures shape_features;

    bool operator==(FontCacheKey const& other) const
    {
        return point_size == other.point_size && axes == other.axes && shape_features == other.shape_features;
    }

    unsigned hash() const
    {
        auto h = pair_int_hash(bit_cast<u32>(point_size), axes.size());
        for (auto const& axis : axes)
            h = pair_int_hash(h, pair_int_hash(axis.tag.to_u32(), bit_cast<u32>(axis.value)));
        h = pair_int_hash(h, Traits<Gfx::ShapeFeatures>::hash(shape_features));
        return h;
    }
};

class Typeface : public RefCounted<Typeface> {
public:
    static ErrorOr<NonnullRefPtr<Typeface>> try_load_from_resource(Core::Resource const&, u32 ttc_index = 0);
    static ErrorOr<NonnullRefPtr<Typeface>> try_load_from_font_data(NonnullOwnPtr<Gfx::FontData>, u32 ttc_index = 0);
    static ErrorOr<NonnullRefPtr<Typeface>> try_load_from_temporary_memory(ReadonlyBytes bytes, u32 ttc_index = 0);
    static ErrorOr<NonnullRefPtr<Typeface>> try_load_from_externally_owned_memory(ReadonlyBytes bytes, u32 ttc_index = 0);

    virtual ~Typeface();

    virtual u32 glyph_count() const = 0;
    virtual u16 units_per_em() const = 0;
    virtual u32 glyph_id_for_code_point(u32 code_point) const = 0;
    virtual FlyString const& family() const = 0;
    virtual u16 weight() const = 0;
    virtual u16 width() const = 0;
    virtual u8 slope() const = 0;

    // Return design-unit metrics and an advance only when the platform
    // typeface has authoritative data. Callers retain their conservative
    // fallback metrics for the built-in PSF face and other outline-less
    // typefaces.
    virtual TypefaceDesignMetrics design_metrics() const { return {}; }
    virtual Optional<u16> glyph_advance(u32) const { return {}; }
    virtual Optional<Vector<GlyphOutlineCommand>> glyph_outline(u32) const { return {}; }
    virtual bool has_glyph_outlines() const { return false; }

    [[nodiscard]] NonnullRefPtr<Font> font(float point_size, FontVariationSettings const& variations = {}, Gfx::ShapeFeatures const& shape_features = {}) const;

    hb_face_t* harfbuzz_typeface() const;

    template<typename T>
    bool fast_is() const = delete;

    virtual bool has_platform_typeface() const { return false; }

protected:
    Typeface();

    virtual ReadonlyBytes buffer() const = 0;
    virtual u32 ttc_index() const = 0;

private:
    OwnPtr<FontData> m_font_data;

    mutable HashMap<FontCacheKey, NonnullRefPtr<Font>> m_fonts;
    mutable hb_blob_t* m_harfbuzz_blob { nullptr };
    mutable hb_face_t* m_harfbuzz_face { nullptr };
};

}

template<>
struct AK::Traits<Gfx::FontCacheKey> : public AK::DefaultTraits<Gfx::FontCacheKey> {
    static unsigned hash(Gfx::FontCacheKey const& key)
    {
        return key.hash();
    }
};
