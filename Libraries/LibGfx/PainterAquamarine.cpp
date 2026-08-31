/*
 * PainterAquamarine - aquamarine-backed Painter for RinOS
 * SPDX-License-Identifier: BSD-2-Clause
 */

#include <AK/QuickSort.h>
#include <AK/TypeCasts.h>
#include <AK/Vector.h>
#include <LibGfx/Bitmap.h>
#include <LibGfx/ImmutableBitmap.h>
#include <LibGfx/PathAquamarine.h>
#include <LibGfx/PainterAquamarine.h>
#include <LibGfx/PaintStyle.h>
#include <LibGfx/PaintingSurface.h>
#include <math.h>
#include <stdint.h>
#include <stdlib.h>

extern "C" {
#include <aquamarine.h>
}

namespace Gfx {

static AqColor to_aq_color(Color c)
{
    return AQ_RGBA(c.red(), c.green(), c.blue(), c.alpha());
}

static Color with_global_alpha(Color color, float global_alpha)
{
    if (!isfinite(global_alpha) || global_alpha <= 0.0f)
        return color.with_alpha(0);

    auto alpha = min(global_alpha, 1.0f);
    return color.with_alpha(static_cast<u8>(roundf(static_cast<float>(color.alpha()) * alpha)));
}

static bool supports_source_over(CompositingAndBlendingOperator compositing_and_blending_operator)
{
    return compositing_and_blending_operator == CompositingAndBlendingOperator::Normal
        || compositing_and_blending_operator == CompositingAndBlendingOperator::SourceOver;
}

static Optional<AqRect> to_aq_rect(FloatRect const& rect)
{
    float values[] = { rect.x(), rect.y(), rect.width(), rect.height() };
    for (auto value : values) {
        if (!isfinite(value) || static_cast<double>(value) < static_cast<double>(INT32_MIN) || static_cast<double>(value) > static_cast<double>(INT32_MAX))
            return {};
    }
    if (rect.width() <= 0 || rect.height() <= 0)
        return {};
    return AQ_RECT(
        static_cast<int32_t>(rect.x()),
        static_cast<int32_t>(rect.y()),
        static_cast<int32_t>(rect.width()),
        static_cast<int32_t>(rect.height()));
}

static Optional<uint8_t> to_aq_alpha(float global_alpha)
{
    if (!isfinite(global_alpha) || global_alpha < 0)
        return {};
    if (global_alpha >= 1.0f)
        return 255;
    return static_cast<uint8_t>(roundf(global_alpha * 255.0f));
}

static bool is_axis_aligned_rectangle(PathImplAquamarine const& path_impl)
{
    if (path_impl.contours().size() != 1 || !path_impl.contours()[0].closed || path_impl.contours()[0].points.size() != 4)
        return false;
    auto const& points = path_impl.contours()[0].points;
    auto bounds = path_impl.bounding_box();
    if (bounds.is_empty() || bounds.width() <= 0 || bounds.height() <= 0)
        return false;
    bool saw_top = false;
    bool saw_bottom = false;
    bool saw_left = false;
    bool saw_right = false;
    for (auto const& point : points) {
        if (!isfinite(point.x()) || !isfinite(point.y()))
            return false;
        bool on_left = point.x() == bounds.x();
        bool on_right = point.x() == bounds.right();
        bool on_top = point.y() == bounds.y();
        bool on_bottom = point.y() == bounds.bottom();
        if ((!on_left && !on_right) || (!on_top && !on_bottom))
            return false;
        saw_left |= on_left;
        saw_right |= on_right;
        saw_top |= on_top;
        saw_bottom |= on_bottom;
    }
    return saw_left && saw_right && saw_top && saw_bottom;
}

static Optional<Color> solid_color_for_paint_style(PaintStyle const& paint_style, float global_alpha)
{
    if (auto const* solid_color = as_if<SolidColorPaintStyle>(paint_style))
        return with_global_alpha(solid_color->color(), global_alpha);
    return {};
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

static AqSurface* bitmap_to_aq_surface(Bitmap const& bmp)
{
    ensure_aquamarine_allocator();

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

static PathImplAquamarine const& aq_path(Path const& path)
{
    return static_cast<PathImplAquamarine const&>(path.impl());
}

struct ScanlineIntersection {
    float x { 0 };
    int winding { 0 };
};

static void collect_intersections(Vector<ScanlineIntersection>& intersections, PathImplAquamarine const& path_impl, float scan_y)
{
    for (auto const& contour : path_impl.contours()) {
        if (contour.points.size() < 2)
            continue;
        size_t edge_count = contour.closed ? contour.points.size() : contour.points.size() - 1;
        for (size_t index = 0; index < edge_count; ++index) {
            auto const& start = contour.points[index];
            auto const& end = contour.points[(index + 1) % contour.points.size()];
            if ((start.y() <= scan_y && end.y() > scan_y) || (end.y() <= scan_y && start.y() > scan_y)) {
                float x = start.x() + (scan_y - start.y()) * (end.x() - start.x()) / (end.y() - start.y());
                intersections.append({ x, end.y() > start.y() ? 1 : -1 });
            }
        }
    }
}

static void fill_flattened_path(AqSurface* surface, Path const& path, Color color, WindingRule winding_rule)
{
    auto const& path_impl = aq_path(path);
    auto bounds = path.bounding_box();
    if (bounds.is_empty())
        return;

    int min_y = static_cast<int>(floorf(bounds.y()));
    int max_y = static_cast<int>(ceilf(bounds.bottom()));
    auto aq_color = to_aq_color(color);

    for (int y = min_y; y < max_y; ++y) {
        Vector<ScanlineIntersection> intersections;
        collect_intersections(intersections, path_impl, static_cast<float>(y) + 0.5f);
        if (intersections.is_empty())
            continue;

        quick_sort(intersections, [](auto const& a, auto const& b) { return a.x < b.x; });
        if (winding_rule == WindingRule::EvenOdd) {
            for (size_t index = 0; index + 1 < intersections.size(); index += 2) {
                int x0 = static_cast<int>(ceilf(intersections[index].x));
                int x1 = static_cast<int>(floorf(intersections[index + 1].x));
                if (x1 >= x0)
                    aq_fill_rect(surface, x0, y, x1 - x0 + 1, 1, aq_color);
            }
            continue;
        }

        int winding = 0;
        Optional<float> span_start;
        for (auto const& intersection : intersections) {
            int previous_winding = winding;
            winding += intersection.winding;
            if (previous_winding == 0 && winding != 0) {
                span_start = intersection.x;
            } else if (previous_winding != 0 && winding == 0 && span_start.has_value()) {
                int x0 = static_cast<int>(ceilf(span_start.value()));
                int x1 = static_cast<int>(floorf(intersection.x));
                if (x1 >= x0)
                    aq_fill_rect(surface, x0, y, x1 - x0 + 1, 1, aq_color);
                span_start.clear();
            }
        }
    }
}

static void stroke_flattened_path(AqSurface* surface, Path const& path, Color color, float thickness)
{
    auto const& path_impl = aq_path(path);
    auto aq_color = to_aq_color(color);
    int radius = max(0, static_cast<int>(roundf(thickness * 0.5f)));

    for (auto const& contour : path_impl.contours()) {
        if (contour.points.size() < 2)
            continue;
        size_t edge_count = contour.closed ? contour.points.size() : contour.points.size() - 1;
        for (size_t index = 0; index < edge_count; ++index) {
            auto const& start = contour.points[index];
            auto const& end = contour.points[(index + 1) % contour.points.size()];
            float dx = end.x() - start.x();
            float dy = end.y() - start.y();
            float length = sqrtf(dx * dx + dy * dy);
            if (length <= 0.001f)
                continue;

            float nx = -dy / length;
            float ny = dx / length;
            int half = max(0, static_cast<int>(roundf(thickness * 0.5f)));
            for (int offset = -half; offset <= half; ++offset) {
                int x0 = static_cast<int>(roundf(start.x() + nx * offset));
                int y0 = static_cast<int>(roundf(start.y() + ny * offset));
                int x1 = static_cast<int>(roundf(end.x() + nx * offset));
                int y1 = static_cast<int>(roundf(end.y() + ny * offset));
                aq_line_aa(surface, x0, y0, x1, y1, aq_color);
            }

            if (radius > 0) {
                aq_fill_circle(surface, static_cast<int>(roundf(start.x())), static_cast<int>(roundf(start.y())), radius, aq_color);
                aq_fill_circle(surface, static_cast<int>(roundf(end.x())), static_cast<int>(roundf(end.y())), radius, aq_color);
            }
        }
    }
}

struct PainterAquamarine::Impl {
    RefPtr<PaintingSurface> painting_surface;
    RefPtr<Bitmap> target_bitmap;
    AqSurface* aq_surf { nullptr };
    AffineTransform transform;

    struct State {
        AqRect clip;
        AffineTransform transform;
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

    auto* s = m_impl->aq_surf;
    for (int32_t row = y; row < y + h && row < s->height; ++row) {
        if (row < 0)
            continue;
        uint8_t* p = aq_surface_row(s, row);
        for (int32_t col = x; col < x + w && col < s->width; ++col) {
            if (col < 0)
                continue;
            if (s->format == AQ_FORMAT_BGRA32) {
                uint8_t* px = p + col * 4;
                px[0] = c.b;
                px[1] = c.g;
                px[2] = c.r;
                px[3] = c.a;
            } else if (s->format == AQ_FORMAT_RGBA32) {
                uint8_t* px = p + col * 4;
                px[0] = c.r;
                px[1] = c.g;
                px[2] = c.b;
                px[3] = c.a;
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

void PainterAquamarine::draw_bitmap(FloatRect const& dst_rect, ImmutableBitmap const& src_bitmap, IntRect const& src_rect, ScalingMode scaling_mode, Optional<Filter> filter, float global_alpha, CompositingAndBlendingOperator compositing_and_blending_operator)
{
    if (!m_impl->aq_surf)
        return;
    // Aquamarine currently has no image-filter pipeline. Refuse filtered
    // content instead of silently rendering a different image.
    if (filter.has_value())
        return;
    bool copy = compositing_and_blending_operator == CompositingAndBlendingOperator::Copy;
    if (!copy && !supports_source_over(compositing_and_blending_operator))
        return;
    auto alpha = to_aq_alpha(global_alpha);
    if (!alpha.has_value())
        return;
    auto aq_dst = to_aq_rect(dst_rect);
    if (!aq_dst.has_value())
        return;
    auto bmp = src_bitmap.bitmap();
    if (!bmp)
        return;
    auto* src_surf = bitmap_to_aq_surface(*bmp);
    if (!src_surf)
        return;

    AqRect aq_src = AQ_RECT(src_rect.x(), src_rect.y(), src_rect.width(), src_rect.height());
    bool bilinear = scaling_mode == ScalingMode::Bilinear || scaling_mode == ScalingMode::BilinearMipmap;
    aq_blit_scaled_alpha(m_impl->aq_surf, aq_dst.value(), src_surf, aq_src, alpha.value(), bilinear, copy);
    aq_surface_destroy(src_surf);
}

void PainterAquamarine::stroke_path(Path const& path, Color color, float thickness)
{
    if (!m_impl->aq_surf)
        return;
    auto transformed_path = m_impl->transform.is_identity() ? path.clone() : path.copy_transformed(m_impl->transform);
    stroke_flattened_path(m_impl->aq_surf, transformed_path, color, max(thickness, 1.0f));
}

void PainterAquamarine::stroke_path(Path const& path, Color color, float thickness, float blur_radius, CompositingAndBlendingOperator compositing_and_blending_operator, Path::CapStyle cap_style, Path::JoinStyle join_style, float miter_limit, Vector<float> const& dash_array, float dash_offset)
{
    // The flattened Aquamarine stroke has round caps/joins and no dash or
    // blur pipeline. Do not silently substitute those semantics.
    if (blur_radius != 0.0f || !isfinite(blur_radius) || !supports_source_over(compositing_and_blending_operator)
        || cap_style != Path::CapStyle::Round || join_style != Path::JoinStyle::Round
        || !dash_array.is_empty() || !isfinite(miter_limit) || !isfinite(dash_offset))
        return;
    stroke_path(path, color, thickness);
}

void PainterAquamarine::stroke_path(Path const& path, PaintStyle const& paint_style, Optional<Filter> filter, float thickness, float global_alpha, CompositingAndBlendingOperator compositing_and_blending_operator)
{
    // Canvas' ordinary fill and stroke styles are SolidColorPaintStyle. Do not
    // turn them into a successful no-op merely because this backend has no
    // shader implementation for the more complex paint styles yet.
    if (filter.has_value() || !supports_source_over(compositing_and_blending_operator))
        return;

    auto color = solid_color_for_paint_style(paint_style, global_alpha);
    if (!color.has_value())
        return;

    stroke_path(path, color.value(), thickness);
}

void PainterAquamarine::stroke_path(Path const& path, PaintStyle const& paint_style, Optional<Filter> filter, float thickness, float global_alpha, CompositingAndBlendingOperator compositing_and_blending_operator, Path::CapStyle const& cap_style, Path::JoinStyle const& join_style, float miter_limit, Vector<float> const& dash_array, float dash_offset)
{
    if (filter.has_value() || !supports_source_over(compositing_and_blending_operator))
        return;
    auto color = solid_color_for_paint_style(paint_style, global_alpha);
    if (!color.has_value())
        return;
    stroke_path(path, color.value(), thickness, 0.0f, compositing_and_blending_operator, cap_style, join_style, miter_limit, dash_array, dash_offset);
}

void PainterAquamarine::fill_path(Path const& path, Color color, WindingRule winding_rule)
{
    if (!m_impl->aq_surf)
        return;
    auto transformed_path = m_impl->transform.is_identity() ? path.clone() : path.copy_transformed(m_impl->transform);
    fill_flattened_path(m_impl->aq_surf, transformed_path, color, winding_rule);
}

void PainterAquamarine::fill_path(Path const& path, Color color, WindingRule winding_rule, float blur_radius, CompositingAndBlendingOperator compositing_and_blending_operator)
{
    if (blur_radius != 0.0f || !isfinite(blur_radius) || !supports_source_over(compositing_and_blending_operator))
        return;
    fill_path(path, color, winding_rule);
}

void PainterAquamarine::fill_path(Path const& path, PaintStyle const& paint_style, Optional<Filter> filter, float global_alpha, CompositingAndBlendingOperator compositing_and_blending_operator, WindingRule winding_rule)
{
    if (filter.has_value() || !supports_source_over(compositing_and_blending_operator))
        return;

    auto color = solid_color_for_paint_style(paint_style, global_alpha);
    if (!color.has_value())
        return;

    fill_path(path, color.value(), winding_rule);
}

void PainterAquamarine::set_transform(AffineTransform const& transform)
{
    m_impl->transform = transform;
}

void PainterAquamarine::save()
{
    if (!m_impl->aq_surf)
        return;
    m_impl->state_stack.append({ aq_surface_get_clip(m_impl->aq_surf), m_impl->transform });
}

void PainterAquamarine::restore()
{
    if (!m_impl->aq_surf || m_impl->state_stack.is_empty())
        return;
    auto state = m_impl->state_stack.take_last();
    aq_surface_set_clip(m_impl->aq_surf, state.clip);
    m_impl->transform = state.transform;
}

void PainterAquamarine::clip(Path const& path, WindingRule winding_rule)
{
    if (!m_impl->aq_surf)
        return;
    if (winding_rule != WindingRule::Nonzero && winding_rule != WindingRule::EvenOdd) {
        aq_surface_set_clip(m_impl->aq_surf, AQ_RECT(0, 0, 0, 0));
        return;
    }
    auto transformed_path = m_impl->transform.is_identity() ? path.clone() : path.copy_transformed(m_impl->transform);
    auto const& impl = static_cast<PathImplAquamarine const&>(transformed_path.impl());
    if (!is_axis_aligned_rectangle(impl)) {
        // AqSurface exposes a rectangular clip only. A bounding-box clip for
        // arbitrary geometry would reveal pixels outside the requested path.
        aq_surface_set_clip(m_impl->aq_surf, AQ_RECT(0, 0, 0, 0));
        return;
    }
    auto bounds = transformed_path.bounding_box().to_type<int>();
    aq_surface_set_clip(m_impl->aq_surf, AQ_RECT(bounds.x(), bounds.y(), bounds.width(), bounds.height()));
}

void PainterAquamarine::reset()
{
    if (!m_impl->aq_surf)
        return;
    aq_surface_reset_clip(m_impl->aq_surf);
    m_impl->state_stack.clear();
    m_impl->transform = {};
}

}
