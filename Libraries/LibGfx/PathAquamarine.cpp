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

static constexpr size_t s_max_contours = 16384;
static constexpr size_t s_max_points = 131072;
static constexpr RinOSPathFlatten::Config s_flatten_config { 0.25f, 12 };

static bool valid_path_point(FloatPoint const& point)
{
    return RinOSPathFlatten::valid_point({ point.x(), point.y() });
}

struct TextPathPosition {
    FloatPoint point;
    FloatPoint tangent;
};

static Optional<float> first_contour_length(Vector<PathImplAquamarine::Contour> const& contours)
{
    for (auto const& contour : contours) {
        if (contour.points.size() < 2)
            continue;

        float length = 0;
        for (size_t index = 0; index + 1 < contour.points.size(); ++index) {
            auto delta = contour.points[index + 1] - contour.points[index];
            auto segment_length = sqrtf(delta.x() * delta.x() + delta.y() * delta.y());
            if (!isfinite(segment_length))
                return {};
            if (segment_length > 0.001f)
                length += segment_length;
        }
        if (!isfinite(length) || length <= 0.001f)
            return {};
        return length;
    }
    return {};
}

static Optional<TextPathPosition> first_contour_position_and_tangent(Vector<PathImplAquamarine::Contour> const& contours, float distance)
{
    if (!isfinite(distance) || distance < 0)
        return {};

    for (auto const& contour : contours) {
        if (contour.points.size() < 2)
            continue;

        float remaining = distance;
        for (size_t index = 0; index + 1 < contour.points.size(); ++index) {
            auto const& start = contour.points[index];
            auto const& end = contour.points[index + 1];
            auto delta = end - start;
            auto length = sqrtf(delta.x() * delta.x() + delta.y() * delta.y());
            if (!isfinite(length))
                return {};
            if (length <= 0.001f)
                continue;
            if (remaining <= length) {
                auto inverse_length = 1.0f / length;
                auto t = remaining * inverse_length;
                auto point = start + delta * t;
                auto tangent = delta * inverse_length;
                if (!valid_path_point(point) || !valid_path_point(tangent))
                    return {};
                return TextPathPosition { point, tangent };
            }
            remaining -= length;
        }
        return {};
    }
    return {};
}

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
    m_point_count = 0;
    m_has_current_point = false;
    m_valid = true;
}

PathImplAquamarine::Contour& PathImplAquamarine::ensure_current_contour()
{
    if (m_contours.is_empty() || m_contours.last().closed)
        m_contours.append({});
    return m_contours.last();
}

void PathImplAquamarine::reject_path()
{
    m_contours.clear();
    m_last_point = {};
    m_last_move_to = {};
    m_point_count = 0;
    m_has_current_point = false;
    m_valid = false;
}

bool PathImplAquamarine::append_decomposed_point(RinOSPathFlatten::Point point)
{
    auto needs_new_contour = m_contours.is_empty() || m_contours.last().closed;
    auto required_points = needs_new_contour ? 2u : 1u;
    if (!m_valid || !m_has_current_point
        || !RinOSPathFlatten::valid_point(point)
        || (needs_new_contour && m_contours.size() >= s_max_contours)
        || required_points > s_max_points - min(m_point_count, s_max_points)) {
        reject_path();
        return false;
    }
    auto& contour = ensure_current_contour();
    if (contour.points.is_empty()) {
        contour.points.append(m_last_point);
        ++m_point_count;
    }
    FloatPoint converted { point.x, point.y };
    if (contour.points.last() != converted) {
        contour.points.append(converted);
        ++m_point_count;
    }
    m_last_point = converted;
    m_has_current_point = true;
    return true;
}

bool PathImplAquamarine::append_decomposed_point_callback(
    void* opaque, RinOSPathFlatten::Point point)
{
    if (!opaque)
        return false;
    return static_cast<PathImplAquamarine*>(opaque)->append_decomposed_point(point);
}

void PathImplAquamarine::move_to(Gfx::FloatPoint const& point)
{
    if (!m_valid || !valid_path_point(point) || m_point_count >= s_max_points) {
        reject_path();
        return;
    }
    if (m_contours.is_empty()) {
        m_contours.append({});
    } else if (m_contours.last().closed || !m_contours.last().points.is_empty()) {
        if (m_contours.size() >= s_max_contours) {
            reject_path();
            return;
        }
        m_contours.append({});
    }
    auto& current = m_contours.last();
    current.points.append(point);
    ++m_point_count;
    m_last_point = point;
    m_last_move_to = point;
    m_has_current_point = true;
}

void PathImplAquamarine::line_to(Gfx::FloatPoint const& point)
{
    if (!m_valid || !valid_path_point(point)) {
        reject_path();
        return;
    }
    if (!m_has_current_point) {
        move_to(point);
        return;
    }
    (void)append_decomposed_point({ point.x(), point.y() });
}

void PathImplAquamarine::close()
{
    if (!m_valid || m_contours.is_empty())
        return;
    auto& contour = m_contours.last();
    if (contour.points.size() >= 2)
        contour.closed = true;
    m_last_point = m_last_move_to;
}

void PathImplAquamarine::elliptical_arc_to(FloatPoint point, FloatSize radii,
    float x_axis_rotation, bool large_arc, bool sweep)
{
    if (!m_valid || !valid_path_point(point)) {
        reject_path();
        return;
    }
    if (!m_has_current_point) {
        move_to(point);
        return;
    }
    auto result = RinOSPathFlatten::elliptical_arc(
        s_flatten_config, { m_last_point.x(), m_last_point.y() },
        { point.x(), point.y() }, radii.width(), radii.height(),
        x_axis_rotation, large_arc, sweep,
        append_decomposed_point_callback, this);
    if (result != RinOSPathFlatten::Result::Ok)
        reject_path();
}

void PathImplAquamarine::arc_to(FloatPoint point, float radius, bool large_arc, bool sweep)
{
    elliptical_arc_to(point, { radius, radius }, 0.0f, large_arc, sweep);
}

void PathImplAquamarine::quadratic_bezier_curve_to(FloatPoint through, FloatPoint point)
{
    if (!m_valid || !valid_path_point(through) || !valid_path_point(point)) {
        reject_path();
        return;
    }
    if (!m_has_current_point) {
        move_to(point);
        return;
    }
    auto result = RinOSPathFlatten::quadratic(
        s_flatten_config, { m_last_point.x(), m_last_point.y() },
        { through.x(), through.y() }, { point.x(), point.y() },
        append_decomposed_point_callback, this);
    if (result != RinOSPathFlatten::Result::Ok)
        reject_path();
}

void PathImplAquamarine::cubic_bezier_curve_to(FloatPoint c1, FloatPoint c2, FloatPoint p2)
{
    if (!m_valid || !valid_path_point(c1) || !valid_path_point(c2)
        || !valid_path_point(p2)) {
        reject_path();
        return;
    }
    if (!m_has_current_point) {
        move_to(p2);
        return;
    }
    auto result = RinOSPathFlatten::cubic(
        s_flatten_config, { m_last_point.x(), m_last_point.y() },
        { c1.x(), c1.y() }, { c2.x(), c2.y() }, { p2.x(), p2.y() },
        append_decomposed_point_callback, this);
    if (result != RinOSPathFlatten::Result::Ok)
        reject_path();
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
    auto baseline = m_has_current_point ? m_last_point : FloatPoint {};
    auto glyphs = shape_text(baseline, 0.0f, text, font,
        GlyphRun::TextType::Common);
    glyph_run(*glyphs);
}

void PathImplAquamarine::glyph_run(GlyphRun const& glyph_run)
{
    if (!m_valid)
        return;
    auto const& typeface = glyph_run.font().typeface();
    if (!typeface.has_glyph_outlines()) {
        // A GlyphRun holds glyph IDs, not Unicode scalar values. Mapping one
        // to a PSF font would draw an unrelated glyph after shaping, so no
        // outline-less fallback is correct here.
        reject_path();
        return;
    }
    auto units_per_em = typeface.units_per_em();
    auto scale = glyph_run.font().pixel_size() / static_cast<float>(units_per_em);
    if (!isfinite(scale) || scale <= 0) {
        reject_path();
        return;
    }
    auto baseline_offset = glyph_run.font().pixel_metrics().ascent;
    for (auto const& glyph : glyph_run.glyphs()) {
        auto outline = typeface.glyph_outline(glyph.glyph_id);
        if (!outline.has_value()) {
            reject_path();
            return;
        }
        for (auto const& command : outline.value()) {
            auto point = FloatPoint {
                glyph.position.x() + command.x * scale,
                glyph.position.y() + baseline_offset - command.y * scale,
            };
            if (!valid_path_point(point)) {
                reject_path();
                return;
            }
            switch (command.type) {
            case GlyphOutlineCommand::Type::MoveTo:
                move_to(point);
                break;
            case GlyphOutlineCommand::Type::LineTo:
                line_to(point);
                break;
            case GlyphOutlineCommand::Type::QuadraticCurveTo: {
                auto control = FloatPoint {
                    glyph.position.x() + command.control_x * scale,
                    glyph.position.y() + baseline_offset - command.control_y * scale,
                };
                if (!valid_path_point(control)) {
                    reject_path();
                    return;
                }
                quadratic_bezier_curve_to(control, point);
                break;
            }
            case GlyphOutlineCommand::Type::Close:
                close();
                break;
            }
            if (!m_valid)
                return;
        }
    }
}

void PathImplAquamarine::offset(Gfx::FloatPoint const& delta)
{
    if (!m_valid || !valid_path_point(delta)) {
        reject_path();
        return;
    }
    for (auto const& contour : m_contours) {
        for (auto const& point : contour.points) {
            FloatPoint translated = point;
            translated.translate_by(delta);
            if (!valid_path_point(translated)) {
                reject_path();
                return;
            }
        }
    }
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
    if (!m_valid || !other_impl.m_valid
        || other_impl.m_contours.size() > s_max_contours - min(m_contours.size(), s_max_contours)
        || other_impl.m_point_count > s_max_points - min(m_point_count, s_max_points)) {
        reject_path();
        return;
    }
    for (auto const& contour : other_impl.contours())
        m_contours.append(contour);
    m_point_count += other_impl.m_point_count;
    if (!other_impl.is_empty()) {
        m_last_point = other_impl.last_point();
        for (auto const& contour : other_impl.contours()) {
            if (!contour.points.is_empty())
                m_last_move_to = contour.points.first();
        }
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
    if (!m_valid)
        return true;
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
    if (!m_valid)
        return {};
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
    if (!m_valid || !valid_path_point(point))
        return false;
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
    if (!transformed->m_valid)
        return transformed;
    for (auto& contour : transformed->m_contours) {
        for (auto& point : contour.points) {
            point.transform_by(transform);
            if (!valid_path_point(point)) {
                transformed->reject_path();
                return transformed;
            }
        }
    }
    transformed->m_last_point.transform_by(transform);
    transformed->m_last_move_to.transform_by(transform);
    if (!valid_path_point(transformed->m_last_point)
        || !valid_path_point(transformed->m_last_move_to))
        transformed->reject_path();
    return transformed;
}

NonnullOwnPtr<PathImpl> PathImplAquamarine::place_text_along(Utf8View const& text, Font const& font) const
{
    auto utf16 = Utf16String::from_utf8_without_validation(text.as_string());
    return place_text_along(utf16.utf16_view(), font);
}

NonnullOwnPtr<PathImpl> PathImplAquamarine::place_text_along(Utf16View const& text, Font const& font) const
{
    auto result = PathImplAquamarine::create();
    auto path_length = first_contour_length(m_contours);
    if (!path_length.has_value())
        return result;

    auto const& typeface = font.typeface();
    if (!typeface.has_glyph_outlines()) {
        result->reject_path();
        return result;
    }
    auto scale = font.pixel_size() / static_cast<float>(typeface.units_per_em());
    if (!isfinite(scale) || scale <= 0) {
        result->reject_path();
        return result;
    }

    auto glyphs = shape_text({ 0.0f, font.pixel_metrics().ascent }, 0.0f, text, font, GlyphRun::TextType::Common);

    float cursor = 0.0f;
    for (auto const& glyph : glyphs->glyphs()) {
        auto advance = glyph.glyph_width;
        if (!isfinite(advance) || advance < 0) {
            result->reject_path();
            return result;
        }
        if (cursor + advance * 0.5f > path_length.value())
            break;

        auto placement = first_contour_position_and_tangent(m_contours, cursor);
        if (!placement.has_value()) {
            result->reject_path();
            return result;
        }
        auto outline = typeface.glyph_outline(glyph.glyph_id);
        if (!outline.has_value()) {
            result->reject_path();
            return result;
        }
        auto normal = FloatPoint { placement->tangent.y(), -placement->tangent.x() };
        auto transform_point = [&](float x, float y) {
            return placement->point + placement->tangent * (x * scale) + normal * (y * scale);
        };

        for (auto const& command : outline.value()) {
            auto point = transform_point(command.x, command.y);
            if (!valid_path_point(point)) {
                result->reject_path();
                return result;
            }
            switch (command.type) {
            case GlyphOutlineCommand::Type::MoveTo:
                result->move_to(point);
                break;
            case GlyphOutlineCommand::Type::LineTo:
                result->line_to(point);
                break;
            case GlyphOutlineCommand::Type::QuadraticCurveTo: {
                auto control = transform_point(command.control_x, command.control_y);
                if (!valid_path_point(control)) {
                    result->reject_path();
                    return result;
                }
                result->quadratic_bezier_curve_to(control, point);
                break;
            }
            case GlyphOutlineCommand::Type::Close:
                result->close();
                break;
            }
            if (!result->m_valid)
                return result;
        }
        cursor += advance;
    }

    return result;
}

String PathImplAquamarine::to_svg_string() const
{
    StringBuilder builder;
    if (!m_valid)
        return MUST(builder.to_string());
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
