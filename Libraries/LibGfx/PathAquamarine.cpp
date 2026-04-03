/*
 * Copyright (c) 2026, RinOS contributors
 *
 * SPDX-License-Identifier: BSD-2-Clause
 */

#include <AK/StringBuilder.h>
#include <AK/Utf16String.h>
#include <LibGfx/PathAquamarine.h>
#include <LibGfx/TextLayout.h>
#include <math.h>

namespace Gfx {

static constexpr int s_curve_subdivisions = 16;

NonnullOwnPtr<PathImplAquamarine> PathImplAquamarine::create()
{
    return adopt_own(*new PathImplAquamarine());
}

PathImplAquamarine::PathImplAquamarine() = default;
PathImplAquamarine::PathImplAquamarine(PathImplAquamarine const& other) = default;
PathImplAquamarine::~PathImplAquamarine() = default;

void PathImplAquamarine::clear()
{
    m_contours.clear();
    m_last_point = {};
    m_last_move_to = {};
    m_has_current_point = false;
}

PathImplAquamarine::Contour& PathImplAquamarine::ensure_current_contour()
{
    if (m_contours.is_empty() || m_contours.last().closed)
        m_contours.append({});
    return m_contours.last();
}

void PathImplAquamarine::move_to(Gfx::FloatPoint const& point)
{
    auto& contour = ensure_current_contour();
    if (!contour.points.is_empty())
        m_contours.append({});
    auto& current = ensure_current_contour();
    current.points.append(point);
    m_last_point = point;
    m_last_move_to = point;
    m_has_current_point = true;
}

void PathImplAquamarine::line_to(Gfx::FloatPoint const& point)
{
    if (!m_has_current_point) {
        move_to(point);
        return;
    }

    auto& contour = ensure_current_contour();
    if (contour.points.is_empty())
        contour.points.append(m_last_point);
    if (contour.points.last() != point)
        contour.points.append(point);
    m_last_point = point;
}

void PathImplAquamarine::close()
{
    if (m_contours.is_empty())
        return;
    auto& contour = m_contours.last();
    if (contour.points.size() >= 2)
        contour.closed = true;
    m_last_point = m_last_move_to;
}

void PathImplAquamarine::append_sampled_curve(Function<FloatPoint(float)> const& sampler)
{
    for (int index = 1; index <= s_curve_subdivisions; ++index) {
        float t = static_cast<float>(index) / static_cast<float>(s_curve_subdivisions);
        line_to(sampler(t));
    }
}

void PathImplAquamarine::elliptical_arc_to(FloatPoint point, FloatSize radii, float, bool large_arc, bool sweep)
{
    if (!m_has_current_point) {
        move_to(point);
        return;
    }

    auto start = m_last_point;
    auto delta = point - start;
    auto length = sqrtf(delta.x() * delta.x() + delta.y() * delta.y());
    if (length <= 0.001f) {
        line_to(point);
        return;
    }

    auto midpoint = FloatPoint { (start.x() + point.x()) * 0.5f, (start.y() + point.y()) * 0.5f };
    auto normal = FloatPoint { -delta.y() / length, delta.x() / length };
    float arc_height = max(radii.width(), radii.height());
    if (arc_height <= 0.0f)
        arc_height = length * 0.25f;
    if (large_arc)
        arc_height *= 1.5f;
    if (!sweep)
        arc_height = -arc_height;
    auto control = midpoint + normal * arc_height;
    quadratic_bezier_curve_to(control, point);
}

void PathImplAquamarine::arc_to(FloatPoint point, float radius, bool large_arc, bool sweep)
{
    elliptical_arc_to(point, { radius, radius }, 0.0f, large_arc, sweep);
}

void PathImplAquamarine::quadratic_bezier_curve_to(FloatPoint through, FloatPoint point)
{
    if (!m_has_current_point) {
        move_to(point);
        return;
    }

    auto start = m_last_point;
    append_sampled_curve([&](float t) {
        float mt = 1.0f - t;
        return FloatPoint {
            mt * mt * start.x() + 2.0f * mt * t * through.x() + t * t * point.x(),
            mt * mt * start.y() + 2.0f * mt * t * through.y() + t * t * point.y(),
        };
    });
}

void PathImplAquamarine::cubic_bezier_curve_to(FloatPoint c1, FloatPoint c2, FloatPoint p2)
{
    if (!m_has_current_point) {
        move_to(p2);
        return;
    }

    auto start = m_last_point;
    append_sampled_curve([&](float t) {
        float mt = 1.0f - t;
        float mt2 = mt * mt;
        float t2 = t * t;
        return FloatPoint {
            mt2 * mt * start.x() + 3.0f * mt2 * t * c1.x() + 3.0f * mt * t2 * c2.x() + t2 * t * p2.x(),
            mt2 * mt * start.y() + 3.0f * mt2 * t * c1.y() + 3.0f * mt * t2 * c2.y() + t2 * t * p2.y(),
        };
    });
}

void PathImplAquamarine::append_rectangle(FloatRect const& rect)
{
    if (rect.is_empty())
        return;
    move_to(rect.top_left());
    line_to({ rect.right(), rect.y() });
    line_to(rect.bottom_right());
    line_to({ rect.x(), rect.bottom() });
    close();
}

void PathImplAquamarine::text(Utf8View const& text, Font const& font)
{
    auto utf16 = Utf16String::from_utf8_without_validation(text.as_string());
    this->text(utf16.utf16_view(), font);
}

void PathImplAquamarine::text(Utf16View const& text, Font const& font)
{
    auto glyphs = shape_text({ 0.0f, font.pixel_metrics().ascent }, 0.0f, text, font, GlyphRun::TextType::Common);
    glyph_run(*glyphs);
}

void PathImplAquamarine::glyph_run(GlyphRun const& glyph_run)
{
    auto line_height = max(glyph_run.font().pixel_metrics().line_spacing(), 1.0f);
    for (auto const& glyph : glyph_run.glyphs()) {
        auto width = max(glyph.glyph_width, 1.0f);
        append_rectangle({ glyph.position.x(), glyph.position.y(), width, line_height });
    }
}

void PathImplAquamarine::offset(Gfx::FloatPoint const& delta)
{
    for (auto& contour : m_contours) {
        for (auto& point : contour.points)
            point.translate_by(delta);
    }
    m_last_point.translate_by(delta);
    m_last_move_to.translate_by(delta);
}

void PathImplAquamarine::append_path(Gfx::Path const& other)
{
    auto const& other_impl = static_cast<PathImplAquamarine const&>(other.impl());
    for (auto const& contour : other_impl.contours())
        m_contours.append(contour);
    if (!other_impl.is_empty()) {
        m_last_point = other_impl.last_point();
        m_has_current_point = true;
    }
}

void PathImplAquamarine::intersect(Gfx::Path const& other)
{
    auto intersection = bounding_box().intersected(other.bounding_box());
    clear();
    if (!intersection.is_empty())
        append_rectangle(intersection);
}

bool PathImplAquamarine::is_empty() const
{
    for (auto const& contour : m_contours) {
        if (!contour.points.is_empty())
            return false;
    }
    return true;
}

Gfx::FloatPoint PathImplAquamarine::last_point() const
{
    return m_last_point;
}

Gfx::FloatRect PathImplAquamarine::bounding_box() const
{
    bool has_point = false;
    float min_x = 0;
    float min_y = 0;
    float max_x = 0;
    float max_y = 0;

    for (auto const& contour : m_contours) {
        for (auto const& point : contour.points) {
            if (!has_point) {
                min_x = max_x = point.x();
                min_y = max_y = point.y();
                has_point = true;
            } else {
                min_x = min(min_x, point.x());
                min_y = min(min_y, point.y());
                max_x = max(max_x, point.x());
                max_y = max(max_y, point.y());
            }
        }
    }

    if (!has_point)
        return {};
    return { min_x, min_y, max_x - min_x, max_y - min_y };
}

void PathImplAquamarine::set_fill_type(Gfx::WindingRule winding_rule)
{
    m_fill_type = winding_rule;
}

bool PathImplAquamarine::contains(FloatPoint point, Gfx::WindingRule winding_rule) const
{
    float const scan_y = point.y();
    int winding = 0;
    bool inside_even_odd = false;

    for (auto const& contour : m_contours) {
        if (contour.points.size() < 2)
            continue;

        for (size_t index = 0; index < contour.points.size(); ++index) {
            auto const& start = contour.points[index];
            auto const& end = contour.points[(index + 1) % contour.points.size()];
            if (!contour.closed && index + 1 == contour.points.size())
                break;
            if ((start.y() <= scan_y && end.y() > scan_y) || (end.y() <= scan_y && start.y() > scan_y)) {
                float hit_x = start.x() + (scan_y - start.y()) * (end.x() - start.x()) / (end.y() - start.y());
                if (hit_x > point.x()) {
                    inside_even_odd = !inside_even_odd;
                    winding += end.y() > start.y() ? 1 : -1;
                }
            }
        }
    }

    if (winding_rule == WindingRule::EvenOdd)
        return inside_even_odd;
    return winding != 0;
}

NonnullOwnPtr<PathImpl> PathImplAquamarine::clone() const
{
    return adopt_own(*new PathImplAquamarine(*this));
}

NonnullOwnPtr<PathImpl> PathImplAquamarine::copy_transformed(Gfx::AffineTransform const& transform) const
{
    auto transformed = adopt_own(*new PathImplAquamarine(*this));
    for (auto& contour : transformed->m_contours) {
        for (auto& point : contour.points)
            point.transform_by(transform);
    }
    transformed->m_last_point.transform_by(transform);
    transformed->m_last_move_to.transform_by(transform);
    return transformed;
}

Optional<FloatPoint> PathImplAquamarine::point_along_first_contour(float distance) const
{
    for (auto const& contour : m_contours) {
        if (contour.points.size() < 2)
            continue;

        float remaining = distance;
        for (size_t index = 0; index + 1 < contour.points.size(); ++index) {
            auto const& start = contour.points[index];
            auto const& end = contour.points[index + 1];
            auto delta = end - start;
            float length = sqrtf(delta.x() * delta.x() + delta.y() * delta.y());
            if (length <= 0.001f)
                continue;
            if (remaining <= length) {
                float t = remaining / length;
                return start + delta * t;
            }
            remaining -= length;
        }
    }
    return {};
}

NonnullOwnPtr<PathImpl> PathImplAquamarine::place_text_along(Utf8View const& text, Font const& font) const
{
    auto utf16 = Utf16String::from_utf8_without_validation(text.as_string());
    return place_text_along(utf16.utf16_view(), font);
}

NonnullOwnPtr<PathImpl> PathImplAquamarine::place_text_along(Utf16View const& text, Font const& font) const
{
    auto result = adopt_own(*new PathImplAquamarine(*this));
    auto glyphs = shape_text({ 0.0f, font.pixel_metrics().ascent }, 0.0f, text, font, GlyphRun::TextType::Common);

    float cursor = 0.0f;
    for (auto const& glyph : glyphs->glyphs()) {
        auto placement = point_along_first_contour(cursor);
        if (!placement.has_value())
            break;
        result->append_rectangle({ placement->x(), placement->y(), max(glyph.glyph_width, 1.0f), max(font.pixel_metrics().line_spacing(), 1.0f) });
        cursor += max(glyph.glyph_width, 1.0f);
    }

    return result;
}

String PathImplAquamarine::to_svg_string() const
{
    StringBuilder builder;
    for (auto const& contour : m_contours) {
        if (contour.points.is_empty())
            continue;
        builder.appendff("M {} {}", contour.points[0].x(), contour.points[0].y());
        for (size_t index = 1; index < contour.points.size(); ++index)
            builder.appendff(" L {} {}", contour.points[index].x(), contour.points[index].y());
        if (contour.closed)
            builder.append(" Z"sv);
        builder.append(' ');
    }
    return MUST(builder.to_string());
}

}
