/*
 * PainterAquamarine - aquamarine-backed Painter for RinOS
 * SPDX-License-Identifier: BSD-2-Clause
 */

#pragma once

#include <AK/NonnullOwnPtr.h>
#include <AK/Vector.h>
#include <LibGfx/CompositingAndBlendingOperator.h>
#include <LibGfx/Painter.h>
#include <LibGfx/PaintingSurface.h>

namespace Gfx {

class PainterAquamarine final : public Painter {
public:
    explicit PainterAquamarine(NonnullRefPtr<Gfx::PaintingSurface>);
    explicit PainterAquamarine(NonnullRefPtr<Gfx::Bitmap>);
    virtual ~PainterAquamarine() override;

    virtual void clear_rect(Gfx::FloatRect const&, Color) override;
    virtual void fill_rect(Gfx::FloatRect const&, Color) override;

    virtual void draw_bitmap(Gfx::FloatRect const& dst_rect, Gfx::ImmutableBitmap const& src_bitmap, Gfx::IntRect const& src_rect, Gfx::ScalingMode, Optional<Gfx::Filter>, float global_alpha, Gfx::CompositingAndBlendingOperator) override;

    virtual void stroke_path(Gfx::Path const&, Gfx::Color, float thickness) override;
    virtual void stroke_path(Gfx::Path const&, Gfx::Color, float thickness, float blur_radius, Gfx::CompositingAndBlendingOperator, Gfx::Path::CapStyle, Gfx::Path::JoinStyle, float miter_limit, Vector<float> const& dash_array, float dash_offset) override;
    virtual void stroke_path(Gfx::Path const&, Gfx::PaintStyle const&, Optional<Gfx::Filter>, float thickness, float global_alpha, Gfx::CompositingAndBlendingOperator) override;
    virtual void stroke_path(Gfx::Path const&, Gfx::PaintStyle const&, Optional<Gfx::Filter>, float thickness, float global_alpha, Gfx::CompositingAndBlendingOperator, Gfx::Path::CapStyle const&, Gfx::Path::JoinStyle const&, float miter_limit, Vector<float> const&, float dash_offset) override;

    virtual void fill_path(Gfx::Path const&, Gfx::Color, Gfx::WindingRule) override;
    virtual void fill_path(Gfx::Path const&, Gfx::Color, Gfx::WindingRule, float blur_radius, Gfx::CompositingAndBlendingOperator) override;
    virtual void fill_path(Gfx::Path const&, Gfx::PaintStyle const&, Optional<Gfx::Filter>, float global_alpha, Gfx::CompositingAndBlendingOperator, Gfx::WindingRule) override;

    virtual void set_transform(Gfx::AffineTransform const&) override;

    virtual void save() override;
    virtual void restore() override;

    virtual void clip(Gfx::Path const&, Gfx::WindingRule) override;

    virtual void reset() override;

private:
    struct Impl;
    NonnullOwnPtr<Impl> m_impl;
};

}
