/*
 * DisplayListPlayerSkia - Aquamarine raster backend for RinOS
 * Implements display list rendering using the aquamarine 2D library
 * instead of Skia.
 */

#include <LibGfx/Bitmap.h>
#include <LibGfx/ImmutableBitmap.h>
#include <LibGfx/PaintingSurface.h>
#include <LibWeb/Painting/DisplayListPlayerSkia.h>

extern "C" {
#include <aquamarine.h>
}

namespace Web::Painting {

static AqColor to_aq(Gfx::Color c)
{
    return AQ_RGBA(c.red(), c.green(), c.blue(), c.alpha());
}

// Macro to get a temporary AqSurface from the player's target bitmap.
// surface() is protected in DisplayListPlayer, accessible from member functions.
#define AQ_SURFACE_SCOPE(varname)                                              \
    auto* _bmp_ = surface().bitmap();                                          \
    if (!_bmp_) return;                                                        \
    auto* varname = aq_surface_create_from(                                    \
        reinterpret_cast<uint8_t*>(_bmp_->begin()),                            \
        _bmp_->width(), _bmp_->height(),                                       \
        static_cast<int32_t>(_bmp_->pitch()), AQ_FORMAT_BGRA32);              \
    if (!varname) return;

#define AQ_SURFACE_END(varname) aq_surface_destroy(varname);

DisplayListPlayerSkia::DisplayListPlayerSkia() = default;
DisplayListPlayerSkia::~DisplayListPlayerSkia() = default;

void DisplayListPlayerSkia::flush() { }

void DisplayListPlayerSkia::draw_glyph_run(DrawGlyphRun const&)
{
    // TODO: Text rendering requires glyph-to-bitmap rasterization + aquamarine blit
}

void DisplayListPlayerSkia::fill_rect(FillRect const& cmd)
{
    AQ_SURFACE_SCOPE(s)
    aq_fill_rect(s, cmd.rect.x(), cmd.rect.y(), cmd.rect.width(), cmd.rect.height(), to_aq(cmd.color));
    AQ_SURFACE_END(s)
}

void DisplayListPlayerSkia::draw_external_content(DrawExternalContent const&)
{
    // External content (e.g. video frames) — not yet supported
}

void DisplayListPlayerSkia::draw_scaled_immutable_bitmap(DrawScaledImmutableBitmap const& cmd)
{
    AQ_SURFACE_SCOPE(dst)

    auto& bmp = cmd.bitmap->bitmap();
    auto* src = aq_surface_create_from(
        reinterpret_cast<uint8_t*>(const_cast<Gfx::Bitmap&>(bmp).begin()),
        bmp.width(), bmp.height(),
        static_cast<int32_t>(bmp.pitch()),
        AQ_FORMAT_BGRA32);
    if (!src) { AQ_SURFACE_END(dst) return; }

    AqRect aq_src = AQ_RECT(0, 0, bmp.width(), bmp.height());
    AqRect aq_dst = AQ_RECT(cmd.dst_rect.x(), cmd.dst_rect.y(), cmd.dst_rect.width(), cmd.dst_rect.height());
    aq_blit_scaled(dst, aq_dst, src, aq_src);

    aq_surface_destroy(src);
    AQ_SURFACE_END(dst)
}

void DisplayListPlayerSkia::draw_repeated_immutable_bitmap(DrawRepeatedImmutableBitmap const& cmd)
{
    AQ_SURFACE_SCOPE(dst)

    auto& bmp = cmd.bitmap->bitmap();
    auto* src = aq_surface_create_from(
        reinterpret_cast<uint8_t*>(const_cast<Gfx::Bitmap&>(bmp).begin()),
        bmp.width(), bmp.height(),
        static_cast<int32_t>(bmp.pitch()),
        AQ_FORMAT_BGRA32);
    if (!src) { AQ_SURFACE_END(dst) return; }

    // Tile the bitmap over the dst_rect
    for (int32_t y = cmd.dst_rect.y(); y < cmd.dst_rect.bottom(); y += bmp.height()) {
        for (int32_t x = cmd.dst_rect.x(); x < cmd.dst_rect.right(); x += bmp.width()) {
            aq_blit(dst, x, y, src);
            if (!cmd.repeat.x) break;
        }
        if (!cmd.repeat.y) break;
    }

    aq_surface_destroy(src);
    AQ_SURFACE_END(dst)
}

void DisplayListPlayerSkia::add_clip_rect(AddClipRect const& cmd)
{
    AQ_SURFACE_SCOPE(s)
    aq_surface_set_clip(s, AQ_RECT(cmd.rect.x(), cmd.rect.y(), cmd.rect.width(), cmd.rect.height()));
    AQ_SURFACE_END(s)
}

void DisplayListPlayerSkia::save(Save const&) { }
void DisplayListPlayerSkia::save_layer(SaveLayer const&) { }
void DisplayListPlayerSkia::restore(Restore const&) { }

void DisplayListPlayerSkia::translate(Translate const&)
{
    // TODO: maintain translation offset for subsequent draw commands
}

void DisplayListPlayerSkia::paint_linear_gradient(PaintLinearGradient const& cmd)
{
    AQ_SURFACE_SCOPE(s)
    auto const& data = cmd.linear_gradient_data;
    if (data.color_stops.size() >= 2) {
        auto const& first = data.color_stops.first();
        auto const& last = data.color_stops.last();
        // Simplified: use first and last color stop as a vertical gradient
        aq_gradient_v(s,
            cmd.gradient_rect.x(), cmd.gradient_rect.y(),
            cmd.gradient_rect.width(), cmd.gradient_rect.height(),
            to_aq(first.color), to_aq(last.color));
    }
    AQ_SURFACE_END(s)
}

void DisplayListPlayerSkia::paint_outer_box_shadow(PaintOuterBoxShadow const& cmd)
{
    AQ_SURFACE_SCOPE(s)
    aq_box_shadow(s,
        cmd.shadow_rect.x(), cmd.shadow_rect.y(),
        cmd.shadow_rect.width(), cmd.shadow_rect.height(),
        cmd.blur_radius, 0, to_aq(cmd.color));
    AQ_SURFACE_END(s)
}

void DisplayListPlayerSkia::paint_inner_box_shadow(PaintInnerBoxShadow const& cmd)
{
    AQ_SURFACE_SCOPE(s)
    aq_box_shadow(s,
        cmd.inner_shadow_rect.x(), cmd.inner_shadow_rect.y(),
        cmd.inner_shadow_rect.width(), cmd.inner_shadow_rect.height(),
        cmd.blur_radius, 0, to_aq(cmd.color));
    AQ_SURFACE_END(s)
}

void DisplayListPlayerSkia::paint_text_shadow(PaintTextShadow const&)
{
    // TODO: Render glyph run into temporary surface, blur, blit
}

void DisplayListPlayerSkia::fill_rect_with_rounded_corners(FillRectWithRoundedCorners const& cmd)
{
    AQ_SURFACE_SCOPE(s)
    // Use the maximum corner radius for aquamarine's fill_round_rect
    int32_t r = static_cast<int32_t>(cmd.corner_radii.top_left.horizontal_radius);
    aq_fill_round_rect(s,
        cmd.rect.x(), cmd.rect.y(),
        cmd.rect.width(), cmd.rect.height(),
        r, to_aq(cmd.color));
    AQ_SURFACE_END(s)
}

void DisplayListPlayerSkia::fill_path(FillPath const&)
{
    // TODO: Path decomposition into segments + scanline fill
}

void DisplayListPlayerSkia::stroke_path(StrokePath const&)
{
    // TODO: Path decomposition into segments + aquamarine line drawing
}

void DisplayListPlayerSkia::draw_ellipse(DrawEllipse const& cmd)
{
    AQ_SURFACE_SCOPE(s)
    int32_t cx = cmd.rect.x() + cmd.rect.width() / 2;
    int32_t cy = cmd.rect.y() + cmd.rect.height() / 2;
    int32_t rx = cmd.rect.width() / 2;
    int32_t ry = cmd.rect.height() / 2;
    aq_draw_ellipse(s, cx, cy, rx, ry, to_aq(cmd.color));
    AQ_SURFACE_END(s)
}

void DisplayListPlayerSkia::fill_ellipse(FillEllipse const& cmd)
{
    AQ_SURFACE_SCOPE(s)
    int32_t cx = cmd.rect.x() + cmd.rect.width() / 2;
    int32_t cy = cmd.rect.y() + cmd.rect.height() / 2;
    int32_t rx = cmd.rect.width() / 2;
    int32_t ry = cmd.rect.height() / 2;
    aq_fill_ellipse(s, cx, cy, rx, ry, to_aq(cmd.color));
    AQ_SURFACE_END(s)
}

void DisplayListPlayerSkia::draw_line(DrawLine const& cmd)
{
    AQ_SURFACE_SCOPE(s)
    aq_line(s, cmd.from.x(), cmd.from.y(), cmd.to.x(), cmd.to.y(), to_aq(cmd.color));
    AQ_SURFACE_END(s)
}

void DisplayListPlayerSkia::apply_backdrop_filter(ApplyBackdropFilter const&)
{
    // TODO: CSS backdrop filter (blur, etc.)
}

void DisplayListPlayerSkia::draw_rect(DrawRect const& cmd)
{
    AQ_SURFACE_SCOPE(s)
    aq_draw_rect(s, cmd.rect.x(), cmd.rect.y(), cmd.rect.width(), cmd.rect.height(), to_aq(cmd.color));
    AQ_SURFACE_END(s)
}

void DisplayListPlayerSkia::paint_radial_gradient(PaintRadialGradient const& cmd)
{
    AQ_SURFACE_SCOPE(s)
    auto const& data = cmd.radial_gradient_data;
    if (data.color_stops.size() >= 2) {
        int32_t r = cmd.size.width() > cmd.size.height() ? cmd.size.width() / 2 : cmd.size.height() / 2;
        aq_gradient_radial(s,
            cmd.center.x(), cmd.center.y(), r,
            to_aq(data.color_stops.first().color),
            to_aq(data.color_stops.last().color));
    }
    AQ_SURFACE_END(s)
}

void DisplayListPlayerSkia::paint_conic_gradient(PaintConicGradient const&)
{
    // TODO: Conic gradients not directly supported by aquamarine
}

void DisplayListPlayerSkia::add_rounded_rect_clip(AddRoundedRectClip const&)
{
    // TODO: Rounded-rect clip mask
}

void DisplayListPlayerSkia::paint_nested_display_list(PaintNestedDisplayList const&)
{
    // Handled by the display list execution engine
}

void DisplayListPlayerSkia::paint_scrollbar(PaintScrollBar const&)
{
    // TODO: Custom scrollbar rendering
}

void DisplayListPlayerSkia::apply_effects(ApplyEffects const&)
{
    // TODO: CSS filter effects (blur, brightness, contrast etc.)
}

void DisplayListPlayerSkia::apply_transform(Gfx::FloatPoint, Gfx::FloatMatrix4x4 const&)
{
    // TODO: 4x4 matrix transform
}

void DisplayListPlayerSkia::add_clip_path(Gfx::Path const&)
{
    // TODO: Arbitrary path clipping
}

bool DisplayListPlayerSkia::would_be_fully_clipped_by_painter(Gfx::IntRect) const
{
    return false;
}

}
