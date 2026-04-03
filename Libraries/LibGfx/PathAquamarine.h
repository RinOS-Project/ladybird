/*
 * Copyright (c) 2026, RinOS contributors
 *
 * SPDX-License-Identifier: BSD-2-Clause
 */

#pragma once

#include <LibGfx/Path.h>

namespace Gfx {

class PathImplAquamarine final : public PathImpl {
public:
    struct Contour {
        Vector<FloatPoint> points;
        bool closed { false };
    };

    static NonnullOwnPtr<PathImplAquamarine> create();

    virtual ~PathImplAquamarine() override;

    virtual void clear() override;
    virtual void move_to(Gfx::FloatPoint const&) override;
    virtual void line_to(Gfx::FloatPoint const&) override;
    virtual void close() override;
    virtual void elliptical_arc_to(FloatPoint point, FloatSize radii, float x_axis_rotation, bool large_arc, bool sweep) override;
    virtual void arc_to(FloatPoint point, float radius, bool large_arc, bool sweep) override;
    virtual void quadratic_bezier_curve_to(FloatPoint through, FloatPoint point) override;
    virtual void cubic_bezier_curve_to(FloatPoint c1, FloatPoint c2, FloatPoint p2) override;
    virtual void text(Utf8View const&, Font const&) override;
    virtual void text(Utf16View const&, Font const&) override;
    virtual void glyph_run(GlyphRun const&) override;
    virtual void offset(Gfx::FloatPoint const&) override;

    virtual void append_path(Gfx::Path const&) override;
    virtual void intersect(Gfx::Path const&) override;

    [[nodiscard]] virtual bool is_empty() const override;
    virtual Gfx::FloatPoint last_point() const override;
    virtual Gfx::FloatRect bounding_box() const override;
    virtual void set_fill_type(Gfx::WindingRule winding_rule) override;
    virtual bool contains(FloatPoint point, Gfx::WindingRule) const override;

    virtual NonnullOwnPtr<PathImpl> clone() const override;
    virtual NonnullOwnPtr<PathImpl> copy_transformed(Gfx::AffineTransform const&) const override;
    virtual NonnullOwnPtr<PathImpl> place_text_along(Utf8View const& text, Font const&) const override;
    virtual NonnullOwnPtr<PathImpl> place_text_along(Utf16View const& text, Font const&) const override;

    virtual String to_svg_string() const override;

    Vector<Contour> const& contours() const { return m_contours; }
    WindingRule fill_type() const { return m_fill_type; }

private:
    PathImplAquamarine();
    PathImplAquamarine(PathImplAquamarine const&);

    Contour& ensure_current_contour();
    void append_rectangle(FloatRect const&);
    void append_sampled_curve(Function<FloatPoint(float)> const& sampler);
    Optional<FloatPoint> point_along_first_contour(float distance) const;

    Vector<Contour> m_contours;
    FloatPoint m_last_point {};
    FloatPoint m_last_move_to {};
    WindingRule m_fill_type { WindingRule::Nonzero };
    bool m_has_current_point { false };
};

}
