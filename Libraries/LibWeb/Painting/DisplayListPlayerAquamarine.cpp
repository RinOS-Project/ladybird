/*
 * Aquamarine display-list player for RinOS.
 */

#include <AK/Math.h>
#include <LibCore/Resource.h>
#include <LibGfx/Bitmap.h>
#include <LibGfx/ImmutableBitmap.h>
#include <LibGfx/PainterAquamarine.h>
#include <LibGfx/PaintingSurface.h>
#include <LibWeb/Painting/DisplayListPlayerAquamarine.h>
#include <stdlib.h>

extern "C" {
#include <aquamarine.h>
}

namespace Web::Painting {

static AqColor to_aq(Gfx::Color c)
{
    return AQ_RGBA(c.red(), c.green(), c.blue(), c.alpha());
}

static void* aquamarine_malloc(unsigned size)
{
    return malloc(size);
}

static void aquamarine_free(void* ptr)
{
    free(ptr);
}

static void ensure_aquamarine_allocator()
{
    static bool initialized = false;
    if (initialized)
        return;
    aq_set_allocator(aquamarine_malloc, aquamarine_free);
    initialized = true;
}

static AqFont const* text_font()
{
    ensure_aquamarine_allocator();

    static AqFont const* s_font = nullptr;
    static bool attempted_load = false;

    if (attempted_load)
        return s_font ? s_font : aq_font_builtin_8x16();

    attempted_load = true;
    auto resource_or_error = Core::Resource::load_from_uri("resource://fonts/browser-ui.psf"sv);
    if (!resource_or_error.is_error()) {
        auto resource = resource_or_error.release_value();
        auto const data = resource->data();
        if (auto* loaded_font = aq_font_load_psf(data.data(), data.size()); loaded_font)
            s_font = loaded_font;
    }

    return s_font ? s_font : aq_font_builtin_8x16();
}

static void draw_codepoint_scaled(AqSurface* surface, int x, int y, u32 codepoint, AqColor color, AqFont const* font, int target_width, int target_height)
{
    if (!surface || color.a == 0 || !font)
        return;

    auto glyph_index = aq_font_lookup_glyph(font, codepoint);
    auto const* glyph = aq_font_glyph_data(font, glyph_index);
    if (!glyph)
        return;

    int const glyph_width = max(font->glyph_w, 1);
    int const glyph_height = max(font->glyph_h, 1);
    int const bytes_per_row = (glyph_width + 7) / 8;
    target_width = max(target_width, 1);
    target_height = max(target_height, 1);

    for (int dy = 0; dy < target_height; ++dy) {
        int src_row = min((dy * glyph_height) / target_height, glyph_height - 1);
        for (int dx = 0; dx < target_width; ++dx) {
            int src_col = min((dx * glyph_width) / target_width, glyph_width - 1);
            int byte_index = src_row * bytes_per_row + src_col / 8;
            int bit_index = 7 - (src_col % 8);
            if ((glyph[byte_index] & (1 << bit_index)) != 0)
                aq_put_pixel_blend(surface, x + dx, y + dy, color);
        }
    }
}

static Gfx::IntRect translated_rect(Gfx::IntRect rect, Gfx::IntPoint delta)
{
    rect.translate_by(delta);
    return rect;
}

static Gfx::FloatPoint translated_point(Gfx::FloatPoint point, Gfx::IntPoint delta)
{
    point.translate_by(static_cast<float>(delta.x()), static_cast<float>(delta.y()));
    return point;
}

// Macro to get a temporary AqSurface from the player's target bitmap.
// surface() is protected in DisplayListPlayer, accessible from member functions.
#define AQ_SURFACE_SCOPE(varname)                                              \
    ensure_aquamarine_allocator();                                             \
    auto* _bmp_ = surface().bitmap();                                          \
    if (!_bmp_) return;                                                        \
    auto* varname = aq_surface_create_from(                                    \
        reinterpret_cast<uint8_t*>(_bmp_->begin()),                            \
        _bmp_->width(), _bmp_->height(),                                       \
        static_cast<int32_t>(_bmp_->pitch()), AQ_FORMAT_BGRA32);              \
    if (!varname) return;                                                      \
    if (m_clip_rect.has_value())                                               \
        aq_surface_set_clip(varname, AQ_RECT(m_clip_rect->x(), m_clip_rect->y(), m_clip_rect->width(), m_clip_rect->height()));

#define AQ_SURFACE_END(varname) aq_surface_destroy(varname);

DisplayListPlayerAquamarine::DisplayListPlayerAquamarine() = default;
DisplayListPlayerAquamarine::~DisplayListPlayerAquamarine() = default;

void DisplayListPlayerAquamarine::flush() { }

void DisplayListPlayerAquamarine::draw_glyph_run(DrawGlyphRun const& command)
{
    AQ_SURFACE_SCOPE(s)

    auto const* font = text_font();
    auto font_pixel_height = max(1, round_to<int>(command.glyph_run->font().pixel_size()));
    auto target_height = max(1, font_pixel_height);
    auto target_width = max(1, (font->glyph_w * target_height + max(font->glyph_h, 1) - 1) / max(font->glyph_h, 1));

    for (auto const& glyph : command.glyph_run->glyphs()) {
        auto glyph_origin = translated_point(command.translation + glyph.position, m_translation);
        int draw_x = round_to<int>(glyph_origin.x());
        int draw_y = round_to<int>(glyph_origin.y());

        if (command.orientation == Gfx::Orientation::Vertical) {
            auto local_x = draw_x - command.rect.x();
            auto local_y = draw_y - command.rect.y();
            draw_x = command.rect.x() + command.rect.width() - local_y - target_width;
            draw_y = command.rect.y() + local_x;
        }

        draw_codepoint_scaled(s, draw_x, draw_y, glyph.glyph_id, to_aq(command.color), font, target_width, target_height);
    }

    AQ_SURFACE_END(s)
}

void DisplayListPlayerAquamarine::fill_rect(FillRect const& cmd)
{
    AQ_SURFACE_SCOPE(s)
    auto rect = translated_rect(cmd.rect, m_translation);
    aq_fill_rect(s, rect.x(), rect.y(), rect.width(), rect.height(), to_aq(cmd.color));
    AQ_SURFACE_END(s)
}

void DisplayListPlayerAquamarine::draw_external_content(DrawExternalContent const&)
{
    // External content (e.g. video frames) — not yet supported
}

void DisplayListPlayerAquamarine::draw_scaled_immutable_bitmap(DrawScaledImmutableBitmap const& cmd)
{
    AQ_SURFACE_SCOPE(dst)

    auto bmp = cmd.bitmap->bitmap();
    if (!bmp) {
        AQ_SURFACE_END(dst)
        return;
    }
    auto* src = aq_surface_create_from(
        const_cast<uint8_t*>(bmp->scanline_u8(0)),
        bmp->width(), bmp->height(),
        static_cast<int32_t>(bmp->pitch()),
        AQ_FORMAT_BGRA32);
    if (!src) { AQ_SURFACE_END(dst) return; }

    AqRect aq_src = AQ_RECT(0, 0, bmp->width(), bmp->height());
    AqRect aq_dst = AQ_RECT(cmd.dst_rect.x(), cmd.dst_rect.y(), cmd.dst_rect.width(), cmd.dst_rect.height());
    aq_dst.x += m_translation.x();
    aq_dst.y += m_translation.y();
    aq_blit_scaled(dst, aq_dst, src, aq_src);

    aq_surface_destroy(src);
    AQ_SURFACE_END(dst)
}

void DisplayListPlayerAquamarine::draw_repeated_immutable_bitmap(DrawRepeatedImmutableBitmap const& cmd)
{
    AQ_SURFACE_SCOPE(dst)

    auto bmp = cmd.bitmap->bitmap();
    if (!bmp) {
        AQ_SURFACE_END(dst)
        return;
    }
    auto* src = aq_surface_create_from(
        const_cast<uint8_t*>(bmp->scanline_u8(0)),
        bmp->width(), bmp->height(),
        static_cast<int32_t>(bmp->pitch()),
        AQ_FORMAT_BGRA32);
    if (!src) { AQ_SURFACE_END(dst) return; }

    // Tile the bitmap over the dst_rect
    auto dst_rect = translated_rect(cmd.dst_rect, m_translation);
    for (int32_t y = dst_rect.y(); y < dst_rect.bottom(); y += bmp->height()) {
        for (int32_t x = dst_rect.x(); x < dst_rect.right(); x += bmp->width()) {
            aq_blit(dst, x, y, src);
            if (!cmd.repeat.x) break;
        }
        if (!cmd.repeat.y) break;
    }

    aq_surface_destroy(src);
    AQ_SURFACE_END(dst)
}

void DisplayListPlayerAquamarine::add_clip_rect(AddClipRect const& cmd)
{
    auto rect = translated_rect(cmd.rect, m_translation);
    if (m_clip_rect.has_value())
        m_clip_rect = m_clip_rect->intersected(rect);
    else
        m_clip_rect = rect;
}

void DisplayListPlayerAquamarine::save(Save const&)
{
    m_saved_states.append({ m_translation, m_clip_rect });
}

void DisplayListPlayerAquamarine::save_layer(SaveLayer const&)
{
    m_saved_states.append({ m_translation, m_clip_rect });
}

void DisplayListPlayerAquamarine::restore(Restore const&)
{
    if (m_saved_states.is_empty())
        return;
    auto state = m_saved_states.take_last();
    m_translation = state.translation;
    m_clip_rect = state.clip_rect;
}

void DisplayListPlayerAquamarine::translate(Translate const& command)
{
    m_translation.translate_by(command.delta);
}

void DisplayListPlayerAquamarine::paint_linear_gradient(PaintLinearGradient const& cmd)
{
    AQ_SURFACE_SCOPE(s)
    auto rect = translated_rect(cmd.gradient_rect, m_translation);
    auto const& data = cmd.linear_gradient_data;
    if (data.color_stops.list.size() >= 2) {
        auto const& first = data.color_stops.list.first();
        auto const& last = data.color_stops.list.last();
        aq_gradient_v(s,
            rect.x(), rect.y(),
            rect.width(), rect.height(),
            to_aq(first.color), to_aq(last.color));
    }
    AQ_SURFACE_END(s)
}

void DisplayListPlayerAquamarine::paint_outer_box_shadow(PaintOuterBoxShadow const& cmd)
{
    AQ_SURFACE_SCOPE(s)
    auto shadow_rect = translated_rect(cmd.shadow_rect, m_translation);
    aq_box_shadow(s,
        shadow_rect.x(), shadow_rect.y(),
        shadow_rect.width(), shadow_rect.height(),
        cmd.blur_radius, 0, to_aq(cmd.color));
    AQ_SURFACE_END(s)
}

void DisplayListPlayerAquamarine::paint_inner_box_shadow(PaintInnerBoxShadow const& cmd)
{
    AQ_SURFACE_SCOPE(s)
    auto shadow_rect = translated_rect(cmd.inner_shadow_rect, m_translation);
    aq_box_shadow(s,
        shadow_rect.x(), shadow_rect.y(),
        shadow_rect.width(), shadow_rect.height(),
        cmd.blur_radius, 0, to_aq(cmd.color));
    AQ_SURFACE_END(s)
}

void DisplayListPlayerAquamarine::paint_text_shadow(PaintTextShadow const&)
{
    // TODO: Render glyph run into temporary surface, blur, blit
}

void DisplayListPlayerAquamarine::fill_rect_with_rounded_corners(FillRectWithRoundedCorners const& cmd)
{
    AQ_SURFACE_SCOPE(s)
    auto rect = translated_rect(cmd.rect, m_translation);
    int32_t r = static_cast<int32_t>(cmd.corner_radii.top_left.horizontal_radius);
    aq_fill_round_rect(s,
        rect.x(), rect.y(),
        rect.width(), rect.height(),
        r, to_aq(cmd.color));
    AQ_SURFACE_END(s)
}

static Gfx::Color resolve_path_color(PaintStyleOrColor const& paint_style_or_color, float opacity)
{
    if (paint_style_or_color.has<Gfx::Color>())
        return paint_style_or_color.get<Gfx::Color>().with_opacity(opacity);
    return Gfx::Color(Gfx::Color::NamedColor::Black).with_opacity(opacity);
}

void DisplayListPlayerAquamarine::fill_path(FillPath const& command)
{
    auto* bitmap = surface().bitmap();
    if (!bitmap)
        return;

    NonnullRefPtr<Gfx::Bitmap> bitmap_ref = *bitmap;
    Gfx::PainterAquamarine painter(bitmap_ref);
    auto path = command.path.clone();
    path.offset({ static_cast<float>(m_translation.x()), static_cast<float>(m_translation.y()) });
    painter.fill_path(path, resolve_path_color(command.paint_style_or_color, command.opacity), command.winding_rule);
}

void DisplayListPlayerAquamarine::stroke_path(StrokePath const& command)
{
    auto* bitmap = surface().bitmap();
    if (!bitmap)
        return;

    NonnullRefPtr<Gfx::Bitmap> bitmap_ref = *bitmap;
    Gfx::PainterAquamarine painter(bitmap_ref);
    auto path = command.path.clone();
    path.offset({ static_cast<float>(m_translation.x()), static_cast<float>(m_translation.y()) });
    painter.stroke_path(path, resolve_path_color(command.paint_style_or_color, command.opacity), command.thickness);
}

void DisplayListPlayerAquamarine::draw_ellipse(DrawEllipse const& cmd)
{
    AQ_SURFACE_SCOPE(s)
    auto rect = translated_rect(cmd.rect, m_translation);
    int32_t cx = rect.x() + rect.width() / 2;
    int32_t cy = rect.y() + rect.height() / 2;
    int32_t rx = rect.width() / 2;
    int32_t ry = rect.height() / 2;
    aq_draw_ellipse(s, cx, cy, rx, ry, to_aq(cmd.color));
    AQ_SURFACE_END(s)
}

void DisplayListPlayerAquamarine::fill_ellipse(FillEllipse const& cmd)
{
    AQ_SURFACE_SCOPE(s)
    auto rect = translated_rect(cmd.rect, m_translation);
    int32_t cx = rect.x() + rect.width() / 2;
    int32_t cy = rect.y() + rect.height() / 2;
    int32_t rx = rect.width() / 2;
    int32_t ry = rect.height() / 2;
    aq_fill_ellipse(s, cx, cy, rx, ry, to_aq(cmd.color));
    AQ_SURFACE_END(s)
}

void DisplayListPlayerAquamarine::draw_line(DrawLine const& cmd)
{
    AQ_SURFACE_SCOPE(s)
    aq_line(s, cmd.from.x() + m_translation.x(), cmd.from.y() + m_translation.y(), cmd.to.x() + m_translation.x(), cmd.to.y() + m_translation.y(), to_aq(cmd.color));
    AQ_SURFACE_END(s)
}

void DisplayListPlayerAquamarine::apply_backdrop_filter(ApplyBackdropFilter const&)
{
    // TODO: CSS backdrop filter (blur, etc.)
}

void DisplayListPlayerAquamarine::draw_rect(DrawRect const& cmd)
{
    AQ_SURFACE_SCOPE(s)
    auto rect = translated_rect(cmd.rect, m_translation);
    aq_draw_rect(s, rect.x(), rect.y(), rect.width(), rect.height(), to_aq(cmd.color));
    AQ_SURFACE_END(s)
}

void DisplayListPlayerAquamarine::paint_radial_gradient(PaintRadialGradient const& cmd)
{
    AQ_SURFACE_SCOPE(s)
    auto center = cmd.center.translated(m_translation);
    auto const& data = cmd.radial_gradient_data;
    if (data.color_stops.list.size() >= 2) {
        int32_t r = cmd.size.width() > cmd.size.height() ? cmd.size.width() / 2 : cmd.size.height() / 2;
        aq_gradient_radial(s,
            center.x(), center.y(), r,
            to_aq(data.color_stops.list.first().color),
            to_aq(data.color_stops.list.last().color));
    }
    AQ_SURFACE_END(s)
}

void DisplayListPlayerAquamarine::paint_conic_gradient(PaintConicGradient const&)
{
    // TODO: Conic gradients not directly supported by aquamarine
}

void DisplayListPlayerAquamarine::add_rounded_rect_clip(AddRoundedRectClip const& command)
{
    auto rect = translated_rect(command.border_rect, m_translation);
    if (m_clip_rect.has_value())
        m_clip_rect = m_clip_rect->intersected(rect);
    else
        m_clip_rect = rect;
}

void DisplayListPlayerAquamarine::paint_nested_display_list(PaintNestedDisplayList const&)
{
    // Handled by the display list execution engine
}

void DisplayListPlayerAquamarine::paint_scrollbar(PaintScrollBar const&)
{
    // TODO: Custom scrollbar rendering
}

void DisplayListPlayerAquamarine::apply_effects(ApplyEffects const&)
{
    // TODO: CSS filter effects (blur, brightness, contrast etc.)
}

void DisplayListPlayerAquamarine::apply_transform(Gfx::FloatPoint, Gfx::FloatMatrix4x4 const&)
{
    // TODO: 4x4 matrix transform
}

void DisplayListPlayerAquamarine::add_clip_path(Gfx::Path const& path)
{
    auto clip_path = path.clone();
    clip_path.offset({ static_cast<float>(m_translation.x()), static_cast<float>(m_translation.y()) });
    auto bounds = clip_path.bounding_box().to_type<int>();
    if (m_clip_rect.has_value())
        m_clip_rect = m_clip_rect->intersected(bounds);
    else
        m_clip_rect = bounds;
}

bool DisplayListPlayerAquamarine::would_be_fully_clipped_by_painter(Gfx::IntRect) const
{
    return false;
}

}
