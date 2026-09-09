/*
 * Copyright (c) 2024, MacDue <macdue@dueutil.tech>
 * Copyright (c) 2024, Jamie Mansfield <jmansfield@cadixdev.org>
 *
 * SPDX-License-Identifier: BSD-2-Clause
 */

#include <LibWeb/Bindings/Intrinsics.h>
#include <LibWeb/Bindings/SVGTransformPrototype.h>
#include <LibWeb/SVG/SVGTransform.h>

#include <AK/Math.h>

namespace Web::SVG {

GC_DEFINE_ALLOCATOR(SVGTransform);

GC::Ref<SVGTransform> SVGTransform::create(JS::Realm& realm)
{
    return realm.create<SVGTransform>(realm);
}

SVGTransform::SVGTransform(JS::Realm& realm)
    : PlatformObject(realm)
{
}

SVGTransform::~SVGTransform() = default;

void SVGTransform::initialize(JS::Realm& realm)
{
    WEB_SET_PROTOTYPE_FOR_INTERFACE(SVGTransform);
    Base::initialize(realm);
}

// https://svgwg.org/svg2-draft/single-page.html#coords-__svg__SVGTransform__type
SVGTransform::Type SVGTransform::type()
{
    return m_type;
}

// https://svgwg.org/svg2-draft/single-page.html#coords-__svg__SVGTransform__angle
float SVGTransform::angle()
{
    return m_angle;
}

// https://svgwg.org/svg2-draft/single-page.html#coords-__svg__SVGTransform__setTranslate
void SVGTransform::set_translate(float tx, float ty)
{
    if (!isfinite(tx) || !isfinite(ty))
        return;
    m_matrix = Gfx::AffineTransform {};
    m_matrix.translate(tx, ty);
    m_type = Type::Translate;
    m_angle = 0.0f;
}

// https://svgwg.org/svg2-draft/single-page.html#coords-__svg__SVGTransform__setScale
void SVGTransform::set_scale(float sx, float sy)
{
    if (!isfinite(sx) || !isfinite(sy))
        return;
    m_matrix = Gfx::AffineTransform {};
    m_matrix.scale(sx, sy);
    m_type = Type::Scale;
    m_angle = 0.0f;
}

// https://svgwg.org/svg2-draft/single-page.html#coords-__svg__SVGTransform__setRotate
void SVGTransform::set_rotate(float angle, float cx, float cy)
{
    if (!isfinite(angle) || !isfinite(cx) || !isfinite(cy))
        return;
    m_matrix = Gfx::AffineTransform {};
    m_matrix.translate(cx, cy)
        .rotate_radians(AK::to_radians(angle))
        .translate(-cx, -cy);
    m_type = Type::Rotate;
    m_angle = angle;
}

// https://svgwg.org/svg2-draft/single-page.html#coords-__svg__SVGTransform__setSkewX
void SVGTransform::set_skew_x(float angle)
{
    if (!isfinite(angle))
        return;
    m_matrix = Gfx::AffineTransform {};
    m_matrix.skew_radians(AK::to_radians(angle), 0.0f);
    m_type = Type::SkewX;
    m_angle = angle;
}

// https://svgwg.org/svg2-draft/single-page.html#coords-__svg__SVGTransform__setSkewY
void SVGTransform::set_skew_y(float angle)
{
    if (!isfinite(angle))
        return;
    m_matrix = Gfx::AffineTransform {};
    m_matrix.skew_radians(0.0f, AK::to_radians(angle));
    m_type = Type::SkewY;
    m_angle = angle;
}

void SVGTransform::set_matrix(float a, float b, float c, float d, float e, float f)
{
    if (!isfinite(a) || !isfinite(b) || !isfinite(c) || !isfinite(d) || !isfinite(e) || !isfinite(f))
        return;
    m_matrix = Gfx::AffineTransform { a, b, c, d, e, f };
    m_type = Type::Matrix;
    m_angle = 0.0f;
}

}
