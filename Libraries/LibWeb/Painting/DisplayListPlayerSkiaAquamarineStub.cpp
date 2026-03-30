#include <LibWeb/Painting/DisplayListPlayerSkia.h>

namespace Web::Painting {

DisplayListPlayerSkia::DisplayListPlayerSkia() = default;

DisplayListPlayerSkia::~DisplayListPlayerSkia() = default;

void DisplayListPlayerSkia::flush()
{
}

void DisplayListPlayerSkia::draw_glyph_run(DrawGlyphRun const&)
{
}

void DisplayListPlayerSkia::fill_rect(FillRect const&)
{
}

void DisplayListPlayerSkia::draw_external_content(DrawExternalContent const&)
{
}

void DisplayListPlayerSkia::draw_scaled_immutable_bitmap(DrawScaledImmutableBitmap const&)
{
}

void DisplayListPlayerSkia::draw_repeated_immutable_bitmap(DrawRepeatedImmutableBitmap const&)
{
}

void DisplayListPlayerSkia::add_clip_rect(AddClipRect const&)
{
}

void DisplayListPlayerSkia::save(Save const&)
{
}

void DisplayListPlayerSkia::save_layer(SaveLayer const&)
{
}

void DisplayListPlayerSkia::restore(Restore const&)
{
}

void DisplayListPlayerSkia::translate(Translate const&)
{
}

void DisplayListPlayerSkia::paint_linear_gradient(PaintLinearGradient const&)
{
}

void DisplayListPlayerSkia::paint_outer_box_shadow(PaintOuterBoxShadow const&)
{
}

void DisplayListPlayerSkia::paint_inner_box_shadow(PaintInnerBoxShadow const&)
{
}

void DisplayListPlayerSkia::paint_text_shadow(PaintTextShadow const&)
{
}

void DisplayListPlayerSkia::fill_rect_with_rounded_corners(FillRectWithRoundedCorners const&)
{
}

void DisplayListPlayerSkia::fill_path(FillPath const&)
{
}

void DisplayListPlayerSkia::stroke_path(StrokePath const&)
{
}

void DisplayListPlayerSkia::draw_ellipse(DrawEllipse const&)
{
}

void DisplayListPlayerSkia::fill_ellipse(FillEllipse const&)
{
}

void DisplayListPlayerSkia::draw_line(DrawLine const&)
{
}

void DisplayListPlayerSkia::apply_backdrop_filter(ApplyBackdropFilter const&)
{
}

void DisplayListPlayerSkia::draw_rect(DrawRect const&)
{
}

void DisplayListPlayerSkia::paint_radial_gradient(PaintRadialGradient const&)
{
}

void DisplayListPlayerSkia::paint_conic_gradient(PaintConicGradient const&)
{
}

void DisplayListPlayerSkia::add_rounded_rect_clip(AddRoundedRectClip const&)
{
}

void DisplayListPlayerSkia::paint_nested_display_list(PaintNestedDisplayList const&)
{
}

void DisplayListPlayerSkia::paint_scrollbar(PaintScrollBar const&)
{
}

void DisplayListPlayerSkia::apply_effects(ApplyEffects const&)
{
}

void DisplayListPlayerSkia::apply_transform(Gfx::FloatPoint, Gfx::FloatMatrix4x4 const&)
{
}

void DisplayListPlayerSkia::add_clip_path(Gfx::Path const&)
{
}

bool DisplayListPlayerSkia::would_be_fully_clipped_by_painter(Gfx::IntRect) const
{
    return false;
}

}
