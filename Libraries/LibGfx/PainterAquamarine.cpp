/*
 * PainterAquamarine - aquamarine-backed Painter for RinOS
 * SPDX-License-Identifier: BSD-2-Clause
 */

#include <AK/Vector.h>
#include <LibGfx/Bitmap.h>
#include <LibGfx/ImmutableBitmap.h>
#include <LibGfx/PainterAquamarine.h>
#include <LibGfx/PaintingSurface.h>

extern "C" {
#include <aquamarine.h>
}

namespace Gfx {

static AqColor to_aq_color(Color c)
{
    return AQ_RGBA(c.red(), c.green(), c.blue(), c.alpha());
}

static AqSurface* bitmap_to_aq_surface(Bitmap const& bmp)
{
    AqPixelFormat fmt = AQ_FORMAT_BGRA32;
    switch (bmp.format()) {
    case BitmapFormat::BGRA8888:
    case BitmapFormat::BGRx8888:
        fmt = AQ_FORMAT_BGRA32;
        break;
    case BitmapFormat::RGBA8888:
    case BitmapFormat::RGBx8888:
        fmt = AQ_FORMAT_RGBA32;
        break;
    default:
        fmt = AQ_FORMAT_BGRA32;
        break;
    }
    return aq_surface_create_from(
        const_cast<uint8_t*>(reinterpret_cast<uint8_t const*>(bmp.begin())),
        bmp.width(), bmp.height(),
        static_cast<int32_t>(bmp.pitch()), fmt);
}

struct PainterAquamarine::Impl {
    RefPtr<PaintingSurface> painting_surface;
    RefPtr<Bitmap> target_bitmap;
    AqSurface* aq_surf { nullptr };

    // Save/restore stack for clipping
    struct State {
        AqRect clip;
    };
    Vector<State> state_stack;

    Impl(NonnullRefPtr<PaintingSurface> surface)
        : painting_surface(surface)
        , target_bitmap(surface->bitmap())
    {
        if (target_bitmap)
            aq_surf = bitmap_to_aq_surface(*target_bitmap);
    }

    Impl(NonnullRefPtr<Bitmap> bitmap)
        : target_bitmap(bitmap)
    {
        aq_surf = bitmap_to_aq_surface(*bitmap);
    }

    ~Impl()
    {
        if (aq_surf)
            aq_surface_destroy(aq_surf);
    }
};

PainterAquamarine::PainterAquamarine(NonnullRefPtr<PaintingSurface> surface)
    : m_impl(make<Impl>(surface))
{
}

PainterAquamarine::PainterAquamarine(NonnullRefPtr<Bitmap> bitmap)
    : m_impl(make<Impl>(bitmap))
{
}

PainterAquamarine::~PainterAquamarine() = default;

void PainterAquamarine::clear_rect(FloatRect const& rect, Color color)
{
    if (!m_impl->aq_surf)
        return;
    auto c = to_aq_color(color);
    int32_t x = static_cast<int32_t>(rect.x());
    int32_t y = static_cast<int32_t>(rect.y());
    int32_t w = static_cast<int32_t>(rect.width());
    int32_t h = static_cast<int32_t>(rect.height());

    // clear = opaque overwrite, bypass blending
    auto* s = m_impl->aq_surf;
    for (int32_t row = y; row < y + h && row < s->height; ++row) {
        if (row < 0) continue;
        uint8_t* p = aq_surface_row(s, row);
        for (int32_t col = x; col < x + w && col < s->width; ++col) {
            if (col < 0) continue;
            if (s->format == AQ_FORMAT_BGRA32) {
                uint8_t* px = p + col * 4;
                px[0] = c.b; px[1] = c.g; px[2] = c.r; px[3] = c.a;
            } else if (s->format == AQ_FORMAT_RGBA32) {
                uint8_t* px = p + col * 4;
                px[0] = c.r; px[1] = c.g; px[2] = c.b; px[3] = c.a;
            }
        }
    }
}

void PainterAquamarine::fill_rect(FloatRect const& rect, Color color)
{
    if (!m_impl->aq_surf)
        return;
    aq_fill_rect(m_impl->aq_surf,
        static_cast<int32_t>(rect.x()),
        static_cast<int32_t>(rect.y()),
        static_cast<int32_t>(rect.width()),
        static_cast<int32_t>(rect.height()),
        to_aq_color(color));
}

void PainterAquamarine::draw_bitmap(FloatRect const& dst_rect, ImmutableBitmap const& src_bitmap, IntRect const& src_rect, ScalingMode, Optional<Filter>, float, CompositingAndBlendingOperator)
{
    if (!m_impl->aq_surf)
        return;
    auto bmp = src_bitmap.bitmap();
    if (!bmp)
        return;
    auto* src_surf = bitmap_to_aq_surface(*bmp);
    if (!src_surf)
        return;

    AqRect aq_src = AQ_RECT(src_rect.x(), src_rect.y(), src_rect.width(), src_rect.height());
    AqRect aq_dst = AQ_RECT(
        static_cast<int32_t>(dst_rect.x()),
        static_cast<int32_t>(dst_rect.y()),
        static_cast<int32_t>(dst_rect.width()),
        static_cast<int32_t>(dst_rect.height()));

    aq_blit_scaled(m_impl->aq_surf, aq_dst, src_surf, aq_src);
    aq_surface_destroy(src_surf);
}

void PainterAquamarine::stroke_path(Path const&, Color color, float thickness)
{
    // TODO: Path decomposition into line segments + aquamarine line drawing
    (void)color;
    (void)thickness;
}

void PainterAquamarine::stroke_path(Path const&, Color, float, float, CompositingAndBlendingOperator, Path::CapStyle, Path::JoinStyle, float, Vector<float> const&, float)
{
    // TODO: Full stroke path with dash, cap, join
}

void PainterAquamarine::stroke_path(Path const&, PaintStyle const&, Optional<Filter>, float, float, CompositingAndBlendingOperator)
{
    // TODO: PaintStyle-based stroke
}

void PainterAquamarine::stroke_path(Path const&, PaintStyle const&, Optional<Filter>, float, float, CompositingAndBlendingOperator, Path::CapStyle const&, Path::JoinStyle const&, float, Vector<float> const&, float)
{
    // TODO: Full PaintStyle stroke
}

void PainterAquamarine::fill_path(Path const&, Color color, WindingRule)
{
    // TODO: Path fill via scanline rasterization + aquamarine
    (void)color;
}

void PainterAquamarine::fill_path(Path const&, Color, WindingRule, float, CompositingAndBlendingOperator)
{
    // TODO: Fill path with blur
}

void PainterAquamarine::fill_path(Path const&, PaintStyle const&, Optional<Filter>, float, CompositingAndBlendingOperator, WindingRule)
{
    // TODO: PaintStyle fill path
}

void PainterAquamarine::set_transform(AffineTransform const&)
{
    // TODO: Transform matrix for aquamarine surface
}

void PainterAquamarine::save()
{
    if (!m_impl->aq_surf)
        return;
    m_impl->state_stack.append({ aq_surface_get_clip(m_impl->aq_surf) });
}

void PainterAquamarine::restore()
{
    if (!m_impl->aq_surf || m_impl->state_stack.is_empty())
        return;
    auto state = m_impl->state_stack.take_last();
    aq_surface_set_clip(m_impl->aq_surf, state.clip);
}

void PainterAquamarine::clip(Path const&, WindingRule)
{
    // TODO: Path-based clipping -> convert to bounding rect for now
}

void PainterAquamarine::reset()
{
    if (!m_impl->aq_surf)
        return;
    aq_surface_reset_clip(m_impl->aq_surf);
    m_impl->state_stack.clear();
}

}
