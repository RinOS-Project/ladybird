/*
 * Copyright (c) 2025, Ladybird contributors
 *
 * SPDX-License-Identifier: BSD-2-Clause
 */

#include <AK/OwnPtr.h>
#include <LibGfx/Bitmap.h>
#include <LibGfx/CompositingAndBlendingOperator.h>
#include <LibGfx/ImmutableBitmap.h>
#ifndef AK_OS_RINOS
#include <LibGfx/PainterSkia.h>
#endif
#include <LibGfx/Rect.h>
#include <LibJS/Runtime/TypedArray.h>
#include <LibUnicode/Segmenter.h>
#include <LibWeb/Bindings/Intrinsics.h>
#include <LibWeb/Bindings/OffscreenCanvasRenderingContext2DPrototype.h>
#include <LibWeb/CSS/Parser/Parser.h>
#include <LibWeb/CSS/PropertyID.h>
#include <LibWeb/HTML/CanvasRenderingContext2D.h>
#include <LibWeb/HTML/HTMLCanvasElement.h>
#include <LibWeb/HTML/HTMLImageElement.h>
#include <LibWeb/HTML/ImageBitmap.h>
#include <LibWeb/HTML/ImageData.h>
#include <LibWeb/HTML/OffscreenCanvas.h>
#include <LibWeb/HTML/OffscreenCanvasRenderingContext2D.h>
#include <LibWeb/HTML/Path2D.h>
#include <LibWeb/HTML/TextMetrics.h>
#include <LibWeb/HTML/TraversableNavigable.h>
#include <LibWeb/Infra/CharacterTypes.h>
#include <LibWeb/Layout/TextNode.h>
#include <LibWeb/Painting/Paintable.h>
#include <LibWeb/Platform/FontPlugin.h>
#include <LibWeb/SVG/SVGImageElement.h>
#include <LibWeb/WebIDL/ExceptionOr.h>

namespace Web::HTML {

GC_DEFINE_ALLOCATOR(OffscreenCanvasRenderingContext2D);

JS::ThrowCompletionOr<GC::Ref<OffscreenCanvasRenderingContext2D>> OffscreenCanvasRenderingContext2D::create(JS::Realm& realm, OffscreenCanvas& offscreen_canvas, JS::Value options)
{
    auto context_attributes = TRY(CanvasRenderingContext2DSettings::from_js_value(realm.vm(), options));
    return realm.create<OffscreenCanvasRenderingContext2D>(realm, offscreen_canvas, context_attributes);
}

OffscreenCanvasRenderingContext2D::OffscreenCanvasRenderingContext2D(JS::Realm& realm, OffscreenCanvas& offscreen_canvas, CanvasRenderingContext2DSettings context_attributes)
    : PlatformObject(realm)
    , CanvasPath(static_cast<Bindings::PlatformObject&>(*this), *this)
    , m_canvas(offscreen_canvas)
    , m_size(offscreen_canvas.bitmap_size_for_canvas())
    , m_context_attributes(context_attributes)
{
    initialize_new_bitmap_to_context_defaults();
}

OffscreenCanvasRenderingContext2D::~OffscreenCanvasRenderingContext2D() = default;

void OffscreenCanvasRenderingContext2D::initialize(JS::Realm& realm)
{
    Base::initialize(realm);
    set_prototype(&Bindings::ensure_web_prototype<Bindings::OffscreenCanvasRenderingContext2DPrototype>(realm, "OffscreenCanvasRenderingContext2D"_string));
}

void OffscreenCanvasRenderingContext2D::visit_edges(Cell::Visitor& visitor)
{
    Base::visit_edges(visitor);
    CanvasState::visit_edges(visitor);
    visitor.visit(m_canvas);
}

void OffscreenCanvasRenderingContext2D::set_size(Gfx::IntSize const& size)
{
    // The caller invokes this after every bitmap replacement, including a
    // same-size reset and transferToImageBitmap(). Never retain a painter for
    // the detached backing bitmap.
    m_size = size;
    m_painter = nullptr;
}

void OffscreenCanvasRenderingContext2D::initialize_new_bitmap_to_context_defaults()
{
    if (m_context_attributes.alpha)
        return;

    auto bitmap = m_canvas->bitmap();
    if (!bitmap)
        return;

    // A bitmap newly allocated by OffscreenCanvas starts transparent. The 2D
    // alpha:false contract instead exposes opaque black, independent of the
    // retained transform from a transferToImageBitmap() call.
    if (auto* canvas_painter = painter()) {
        canvas_painter->reset();
        canvas_painter->clear_rect(bitmap->rect().to_type<float>(), clear_color());
        canvas_painter->set_transform(drawing_state().transform);
    }
}

GC::Ref<OffscreenCanvas> OffscreenCanvasRenderingContext2D::canvas()
{
    return m_canvas;
}

OffscreenCanvas& OffscreenCanvasRenderingContext2D::canvas_element()
{
    return *m_canvas;
}

OffscreenCanvas const& OffscreenCanvasRenderingContext2D::canvas_element() const
{

    return *m_canvas;
}

void OffscreenCanvasRenderingContext2D::mark_as_origin_tainted()
{
    m_canvas->set_origin_clean(false);
}

Gfx::Path OffscreenCanvasRenderingContext2D::rect_path(float x, float y, float width, float height)
{
    auto top_left = Gfx::FloatPoint(x, y);
    auto top_right = Gfx::FloatPoint(x + width, y);
    auto bottom_left = Gfx::FloatPoint(x, y + height);
    auto bottom_right = Gfx::FloatPoint(x + width, y + height);

    Gfx::Path path;
    path.move_to(top_left);
    path.line_to(top_right);
    path.line_to(bottom_right);
    path.line_to(bottom_left);
    path.line_to(top_left);
    return path;
}

Gfx::Color OffscreenCanvasRenderingContext2D::clear_color() const
{
    return m_context_attributes.alpha ? Gfx::Color::Transparent : Gfx::Color::Black;
}

void OffscreenCanvasRenderingContext2D::fill_rect(float x, float y, float width, float height)
{
    fill_internal(rect_path(x, y, width, height), Gfx::WindingRule::EvenOdd);
}

// https://html.spec.whatwg.org/multipage/canvas.html#dom-context-2d-clearrect
void OffscreenCanvasRenderingContext2D::clear_rect(float x, float y, float width, float height)
{
    if (!isfinite(x) || !isfinite(y) || !isfinite(width) || !isfinite(height))
        return;

    if (auto* canvas_painter = painter())
        canvas_painter->clear_rect({ x, y, width, height }, clear_color());
}

void OffscreenCanvasRenderingContext2D::stroke_rect(float x, float y, float width, float height)
{
    stroke_internal(rect_path(x, y, width, height));
}

WebIDL::ExceptionOr<void> OffscreenCanvasRenderingContext2D::draw_image_internal(
    CanvasImageSource const& image, float source_x, float source_y,
    float source_width, float source_height, float destination_x,
    float destination_y, float destination_width, float destination_height)
{
    if (!isfinite(source_x) || !isfinite(source_y) || !isfinite(source_width) || !isfinite(source_height) || !isfinite(destination_x) || !isfinite(destination_y) || !isfinite(destination_width) || !isfinite(destination_height))
        return {};

    auto usability = TRY(check_usability_of_image(image));
    if (usability == CanvasImageSourceUsability::Bad)
        return {};

    auto source_bitmap = canvas_image_source_bitmap(image);
    if (!source_bitmap)
        return {};

    if (source_width < 0) {
        source_x += source_width;
        source_width = abs(source_width);
    }
    if (source_height < 0) {
        source_y += source_height;
        source_height = abs(source_height);
    }
    if (destination_width < 0) {
        destination_x += destination_width;
        destination_width = abs(destination_width);
    }
    if (destination_height < 0) {
        destination_y += destination_height;
        destination_height = abs(destination_height);
    }
    if (source_width == 0 || source_height == 0)
        return {};

    auto source_rect = Gfx::FloatRect { source_x, source_y, source_width, source_height };
    auto destination_rect = Gfx::FloatRect { destination_x, destination_y, destination_width, destination_height };
    auto clipped_source = source_rect.intersected(source_bitmap->rect().to_type<float>());
    auto clipped_destination = destination_rect;
    if (clipped_source != source_rect) {
        clipped_destination.set_width(clipped_destination.width() * (clipped_source.width() / source_rect.width()));
        clipped_destination.set_height(clipped_destination.height() * (clipped_source.height() / source_rect.height()));
    }

    auto scaling_mode = drawing_state().image_smoothing_enabled
        ? Gfx::ScalingMode::BilinearMipmap
        : Gfx::ScalingMode::NearestNeighbor;
    if (auto* canvas_painter = painter()) {
        canvas_painter->draw_bitmap(
            clipped_destination, *source_bitmap, clipped_source.to_rounded<int>(),
            scaling_mode, drawing_state().filter, drawing_state().global_alpha,
            drawing_state().current_compositing_and_blending_operator);
        if (image_is_not_origin_clean(image))
            mark_as_origin_tainted();
    }
    return {};
}

void OffscreenCanvasRenderingContext2D::begin_path()
{
    path().clear();
}

void OffscreenCanvasRenderingContext2D::stroke()
{
    stroke_internal(path());
}

void OffscreenCanvasRenderingContext2D::stroke(Path2D const& path)
{
    stroke_internal(path.path());
}

void OffscreenCanvasRenderingContext2D::fill_text(Utf16String const&, float, float, Optional<double>)
{
    dbgln("(STUBBED) OffscreenCanvasRenderingContext2D::fill_text()");
}

void OffscreenCanvasRenderingContext2D::stroke_text(Utf16String const&, float, float, Optional<double>)
{
    dbgln("(STUBBED) OffscreenCanvasRenderingContext2D::stroke_text()");
}

static Gfx::WindingRule parse_fill_rule(StringView fill_rule)
{
    if (fill_rule == "evenodd"sv)
        return Gfx::WindingRule::EvenOdd;
    return Gfx::WindingRule::Nonzero;
}

static Gfx::Path::CapStyle to_gfx_cap(Bindings::CanvasLineCap cap_style)
{
    switch (cap_style) {
    case Bindings::CanvasLineCap::Butt:
        return Gfx::Path::CapStyle::Butt;
    case Bindings::CanvasLineCap::Round:
        return Gfx::Path::CapStyle::Round;
    case Bindings::CanvasLineCap::Square:
        return Gfx::Path::CapStyle::Square;
    }
    VERIFY_NOT_REACHED();
}

static Gfx::Path::JoinStyle to_gfx_join(Bindings::CanvasLineJoin join_style)
{
    switch (join_style) {
    case Bindings::CanvasLineJoin::Round:
        return Gfx::Path::JoinStyle::Round;
    case Bindings::CanvasLineJoin::Bevel:
        return Gfx::Path::JoinStyle::Bevel;
    case Bindings::CanvasLineJoin::Miter:
        return Gfx::Path::JoinStyle::Miter;
    }
    VERIFY_NOT_REACHED();
}

void OffscreenCanvasRenderingContext2D::paint_shadow_for_fill_internal(Gfx::Path const& path, Gfx::WindingRule winding_rule)
{
    auto* canvas_painter = painter();
    if (!canvas_painter)
        return;

    auto& state = drawing_state();
    if ((state.shadow_blur == 0.0f && state.shadow_offset_x == 0.0f && state.shadow_offset_y == 0.0f)
        || state.current_compositing_and_blending_operator == Gfx::CompositingAndBlendingOperator::Copy)
        return;

    auto alpha = state.global_alpha * (state.shadow_color.alpha() / 255.0f);
    if (auto fill_style_color = state.fill_style.as_color(); fill_style_color.has_value() && fill_style_color->alpha() > 0)
        alpha = (fill_style_color->alpha() / 255.0f) * state.global_alpha;
    if (alpha == 0.0f)
        return;

    canvas_painter->save();
    Gfx::AffineTransform transform;
    transform.translate(state.shadow_offset_x, state.shadow_offset_y);
    transform.multiply(state.transform);
    canvas_painter->set_transform(transform);
    canvas_painter->fill_path(path, state.shadow_color.with_opacity(alpha), winding_rule, state.shadow_blur, state.current_compositing_and_blending_operator);
    canvas_painter->restore();
}

void OffscreenCanvasRenderingContext2D::paint_shadow_for_stroke_internal(Gfx::Path const& path, Gfx::Path::CapStyle line_cap, Gfx::Path::JoinStyle line_join, Vector<float> const& dash_array)
{
    auto* canvas_painter = painter();
    if (!canvas_painter)
        return;

    auto& state = drawing_state();
    if ((state.shadow_blur == 0.0f && state.shadow_offset_x == 0.0f && state.shadow_offset_y == 0.0f)
        || state.current_compositing_and_blending_operator == Gfx::CompositingAndBlendingOperator::Copy)
        return;

    auto alpha = state.global_alpha * (state.shadow_color.alpha() / 255.0f);
    if (auto fill_style_color = state.fill_style.as_color(); fill_style_color.has_value() && fill_style_color->alpha() > 0)
        alpha = (fill_style_color->alpha() / 255.0f) * state.global_alpha;
    if (alpha == 0.0f)
        return;

    canvas_painter->save();
    Gfx::AffineTransform transform;
    transform.translate(state.shadow_offset_x, state.shadow_offset_y);
    transform.multiply(state.transform);
    canvas_painter->set_transform(transform);
    canvas_painter->stroke_path(path, state.shadow_color.with_opacity(alpha), state.line_width, state.shadow_blur, state.current_compositing_and_blending_operator, line_cap, line_join, state.miter_limit, dash_array, state.line_dash_offset);
    canvas_painter->restore();
}

void OffscreenCanvasRenderingContext2D::fill_internal(Gfx::Path const& path, Gfx::WindingRule winding_rule)
{
    auto* canvas_painter = painter();
    if (!canvas_painter)
        return;

    auto& state = drawing_state();
    auto paint_style = state.fill_style.to_gfx_paint_style();
    if (!paint_style->is_visible())
        return;

    paint_shadow_for_fill_internal(path, winding_rule);
    canvas_painter->fill_path(path, paint_style, state.filter, state.global_alpha, state.current_compositing_and_blending_operator, winding_rule);
}

void OffscreenCanvasRenderingContext2D::stroke_internal(Gfx::Path const& path)
{
    auto* canvas_painter = painter();
    if (!canvas_painter)
        return;

    auto& state = drawing_state();
    auto paint_style = state.stroke_style.to_gfx_paint_style();
    if (!paint_style->is_visible())
        return;

    auto dash_array = Vector<float> {};
    dash_array.ensure_capacity(state.dash_list.size());
    for (auto dash : state.dash_list)
        dash_array.append(static_cast<float>(dash));

    auto line_cap = to_gfx_cap(state.line_cap);
    auto line_join = to_gfx_join(state.line_join);
    paint_shadow_for_stroke_internal(path, line_cap, line_join, dash_array);
    canvas_painter->stroke_path(path, paint_style, state.filter, state.line_width, state.global_alpha, state.current_compositing_and_blending_operator, line_cap, line_join, state.miter_limit, dash_array, state.line_dash_offset);
}

void OffscreenCanvasRenderingContext2D::fill(StringView fill_rule)
{
    fill_internal(path(), parse_fill_rule(fill_rule));
}

void OffscreenCanvasRenderingContext2D::fill(Path2D& path, StringView fill_rule)
{
    fill_internal(path.path(), parse_fill_rule(fill_rule));
}

// https://html.spec.whatwg.org/multipage/canvas.html#dom-context-2d-createimagedata
WebIDL::ExceptionOr<GC::Ref<ImageData>> OffscreenCanvasRenderingContext2D::create_image_data(int width, int height, Optional<ImageDataSettings> const& settings) const
{
    if (width == 0 || height == 0)
        return WebIDL::IndexSizeError::create(realm(), "Width and height must not be zero"_utf16);
    return ImageData::create(realm(), abs(width), abs(height), settings);
}

WebIDL::ExceptionOr<GC::Ref<ImageData>> OffscreenCanvasRenderingContext2D::create_image_data(ImageData const& image_data) const
{
    return ImageData::create(realm(), image_data.width(), image_data.height());
}

WebIDL::ExceptionOr<GC::Ptr<ImageData>> OffscreenCanvasRenderingContext2D::get_image_data(int x, int y, int width, int height, Optional<ImageDataSettings> const& settings) const
{
    if (width == 0 || height == 0)
        return WebIDL::IndexSizeError::create(realm(), "Width and height must not be zero"_utf16);
    if (!m_canvas->is_origin_clean())
        return WebIDL::SecurityError::create(realm(), "OffscreenCanvas is not origin-clean"_utf16);

    auto abs_width = abs(width);
    auto abs_height = abs(height);
    auto image_data = TRY(ImageData::create(realm(), abs_width, abs_height, settings));
    auto bitmap = m_canvas->bitmap();
    if (!bitmap)
        return image_data;

    auto source_rect = Gfx::IntRect { x, y, abs_width, abs_height };
    if (width < 0 || height < 0)
        source_rect = source_rect.translated(min(width, 0), min(height, 0));
    auto clipped_source = source_rect.intersected(bitmap->rect());
    if (clipped_source.is_empty())
        return image_data;

    VERIFY(bitmap->alpha_type() == Gfx::AlphaType::Premultiplied);
    VERIFY(image_data->bitmap().alpha_type() == Gfx::AlphaType::Unpremultiplied);
    auto destination_rect = clipped_source.translated(-source_rect.x(), -source_rect.y());
    auto immutable_bitmap = Gfx::ImmutableBitmap::create(bitmap.release_nonnull());
    auto image_data_painter = Gfx::Painter::create(image_data->bitmap());
    image_data_painter->draw_bitmap(destination_rect.to_type<float>(), *immutable_bitmap, clipped_source, Gfx::ScalingMode::NearestNeighbor, {}, 1, Gfx::CompositingAndBlendingOperator::SourceOver);
    return image_data;
}

WebIDL::ExceptionOr<void> OffscreenCanvasRenderingContext2D::put_image_data(ImageData& image_data, float dx, float dy)
{
    if (auto* canvas_painter = painter())
        TRY(put_pixels_from_an_image_data_onto_a_bitmap(image_data, *canvas_painter, dx, dy, 0, 0, image_data.width(), image_data.height()));
    return {};
}

WebIDL::ExceptionOr<void> OffscreenCanvasRenderingContext2D::put_image_data(ImageData& image_data, float x, float y, float dirty_x, float dirty_y, float dirty_width, float dirty_height)
{
    if (auto* canvas_painter = painter())
        TRY(put_pixels_from_an_image_data_onto_a_bitmap(image_data, *canvas_painter, x, y, dirty_x, dirty_y, dirty_width, dirty_height));
    return {};
}

WebIDL::ExceptionOr<void> OffscreenCanvasRenderingContext2D::put_pixels_from_an_image_data_onto_a_bitmap(ImageData& image_data, Gfx::Painter& canvas_painter, float dx, float dy, float dirty_x, float dirty_y, float dirty_width, float dirty_height)
{
    auto* buffer = image_data.data()->viewed_array_buffer();
    if (buffer->is_detached())
        return WebIDL::InvalidStateError::create(image_data.realm(), "ImageData's underlying buffer is detached"_utf16);

    if (dirty_width < 0) {
        dirty_x += dirty_width;
        dirty_width = abs(dirty_width);
    }
    if (dirty_height < 0) {
        dirty_y += dirty_height;
        dirty_height = abs(dirty_height);
    }
    if (dirty_x < 0) {
        dirty_width += dirty_x;
        dirty_x = 0;
    }
    if (dirty_y < 0) {
        dirty_height += dirty_y;
        dirty_y = 0;
    }
    if (dirty_x + dirty_width > image_data.width())
        dirty_width = image_data.width() - dirty_x;
    if (dirty_y + dirty_height > image_data.height())
        dirty_height = image_data.height() - dirty_y;
    if (dirty_width <= 0 || dirty_height <= 0)
        return {};

    auto destination_rect = Gfx::FloatRect { dx + dirty_x, dy + dirty_y, dirty_width, dirty_height };
    canvas_painter.save();
    canvas_painter.set_transform({});
    canvas_painter.draw_bitmap(destination_rect, Gfx::ImmutableBitmap::create(image_data.bitmap(), Gfx::AlphaType::Unpremultiplied), Gfx::IntRect { dirty_x, dirty_y, dirty_width, dirty_height }, Gfx::ScalingMode::NearestNeighbor, {}, 1, Gfx::CompositingAndBlendingOperator::SourceOver);
    canvas_painter.restore();
    return {};
}

void OffscreenCanvasRenderingContext2D::reset_to_default_state()
{
    m_canvas->set_origin_clean(true);
    if (auto* canvas_painter = painter()) {
        if (auto bitmap = m_canvas->bitmap()) {
            canvas_painter->reset();
            canvas_painter->clear_rect(bitmap->rect().to_type<float>(), clear_color());
        }
    }

    path().clear();
    clear_drawing_state_stack();
    reset_drawing_state();
    if (auto* canvas_painter = painter())
        canvas_painter->reset();
}

GC::Ref<TextMetrics> OffscreenCanvasRenderingContext2D::measure_text(Utf16String const&)
{
    dbgln("(STUBBED) OffscreenCanvasRenderingContext2D::measure_text()");

    auto metrics = TextMetrics::create(realm());
    return metrics;
}

void OffscreenCanvasRenderingContext2D::clip_internal(Gfx::Path& path, Gfx::WindingRule winding_rule)
{
    if (auto* canvas_painter = painter())
        canvas_painter->clip(path, winding_rule);
}

void OffscreenCanvasRenderingContext2D::clip(StringView fill_rule)
{
    clip_internal(path(), parse_fill_rule(fill_rule));
}

void OffscreenCanvasRenderingContext2D::clip(Path2D& path, StringView fill_rule)
{
    clip_internal(path.path(), parse_fill_rule(fill_rule));
}

static bool is_point_in_path_internal(Gfx::Path path, Gfx::AffineTransform const& transform, double x, double y, StringView fill_rule)
{
    auto point = Gfx::FloatPoint(x, y);
    if (auto inverse_transform = transform.inverse(); inverse_transform.has_value())
        point = inverse_transform->map(point);
    return path.contains(point, parse_fill_rule(fill_rule));
}

bool OffscreenCanvasRenderingContext2D::is_point_in_path(double x, double y, StringView fill_rule)
{
    return is_point_in_path_internal(path(), drawing_state().transform, x, y, fill_rule);
}

bool OffscreenCanvasRenderingContext2D::is_point_in_path(Path2D const& path, double x, double y, StringView fill_rule)
{
    return is_point_in_path_internal(path.path(), drawing_state().transform, x, y, fill_rule);
}

bool OffscreenCanvasRenderingContext2D::image_smoothing_enabled() const
{
    return drawing_state().image_smoothing_enabled;
}

void OffscreenCanvasRenderingContext2D::set_image_smoothing_enabled(bool enabled)
{
    drawing_state().image_smoothing_enabled = enabled;
}

Bindings::ImageSmoothingQuality OffscreenCanvasRenderingContext2D::image_smoothing_quality() const
{
    return drawing_state().image_smoothing_quality;
}

void OffscreenCanvasRenderingContext2D::set_image_smoothing_quality(Bindings::ImageSmoothingQuality quality)
{
    drawing_state().image_smoothing_quality = quality;
}

String OffscreenCanvasRenderingContext2D::filter() const
{
    dbgln("(STUBBED) OffscreenCanvasRenderingContext2D::filter()");
    return String::from_utf8_without_validation("none"sv.bytes());
}

void OffscreenCanvasRenderingContext2D::set_filter(String)
{
    dbgln("(STUBBED) OffscreenCanvasRenderingContext2D::set_filter()");
}

float OffscreenCanvasRenderingContext2D::shadow_offset_x() const
{
    return drawing_state().shadow_offset_x;
}

// https://html.spec.whatwg.org/multipage/canvas.html#dom-context-2d-shadowoffsetx
void OffscreenCanvasRenderingContext2D::set_shadow_offset_x(float offset_x)
{
    // On setting, the attribute being set must be set to the new value, except if the value is infinite or NaN,
    // in which case the new value must be ignored.
    if (isinf(offset_x) || isnan(offset_x))
        return;

    drawing_state().shadow_offset_x = offset_x;
}

float OffscreenCanvasRenderingContext2D::shadow_offset_y() const
{
    return drawing_state().shadow_offset_y;
}

// https://html.spec.whatwg.org/multipage/canvas.html#dom-context-2d-shadowoffsety
void OffscreenCanvasRenderingContext2D::set_shadow_offset_y(float offset_y)
{
    // On setting, the attribute being set must be set to the new value, except if the value is infinite or NaN,
    // in which case the new value must be ignored.
    if (isinf(offset_y) || isnan(offset_y))
        return;

    drawing_state().shadow_offset_y = offset_y;
}

float OffscreenCanvasRenderingContext2D::shadow_blur() const
{
    return drawing_state().shadow_blur;
}

// https://html.spec.whatwg.org/multipage/canvas.html#dom-context-2d-shadowblur
void OffscreenCanvasRenderingContext2D::set_shadow_blur(float blur_radius)
{
    // On setting, the attribute must be set to the new value,
    // except if the value is negative, infinite or NaN, in which case the new value must be ignored.
    if (blur_radius < 0 || isinf(blur_radius) || isnan(blur_radius))
        return;

    drawing_state().shadow_blur = blur_radius;
}

String OffscreenCanvasRenderingContext2D::shadow_color() const
{
    // https://html.spec.whatwg.org/multipage/canvas.html#dom-context-2d-shadowcolor
    return drawing_state().shadow_color.to_string(Gfx::Color::HTMLCompatibleSerialization::Yes);
}

void OffscreenCanvasRenderingContext2D::set_shadow_color(String color)
{
    // 1. Let context be this's canvas attribute's value, if that is an element; otherwise null.

    // 2. Let parsedValue be the result of parsing the given value with context if non-null.
    auto style_value = parse_css_value(CSS::Parser::ParsingParams(), color, CSS::PropertyID::Color);
    if (style_value && style_value->has_color()) {
        auto parsedValue = style_value->to_color({}).value_or(Color::Black);

        // 4. Set this's shadow color to parsedValue.
        drawing_state().shadow_color = parsedValue;
    } else {
        // 3. If parsedValue is failure, then return.
        return;
    }
}

float OffscreenCanvasRenderingContext2D::global_alpha() const
{
    return drawing_state().global_alpha;
}

// https://html.spec.whatwg.org/multipage/canvas.html#dom-context-2d-globalalpha
void OffscreenCanvasRenderingContext2D::set_global_alpha(float alpha)
{
    // 1. If the given value is either infinite, NaN, or not in the range 0.0 to 1.0, then return.
    if (!isfinite(alpha) || alpha < 0.0f || alpha > 1.0f) {
        return;
    }
    // 2. Otherwise, set this's global alpha to the given value.
    drawing_state().global_alpha = alpha;
}

String OffscreenCanvasRenderingContext2D::global_composite_operation() const
{
    switch (drawing_state().current_compositing_and_blending_operator) {
#define ENUMERATE_COMPOSITE_OPERATIONS(E)  \
    E("normal", Normal)                    \
    E("multiply", Multiply)                \
    E("screen", Screen)                    \
    E("overlay", Overlay)                  \
    E("darken", Darken)                    \
    E("lighten", Lighten)                  \
    E("color-dodge", ColorDodge)           \
    E("color-burn", ColorBurn)             \
    E("hard-light", HardLight)             \
    E("soft-light", SoftLight)             \
    E("difference", Difference)            \
    E("exclusion", Exclusion)              \
    E("hue", Hue)                          \
    E("saturation", Saturation)            \
    E("color", Color)                      \
    E("luminosity", Luminosity)            \
    E("clear", Clear)                      \
    E("copy", Copy)                        \
    E("source-over", SourceOver)           \
    E("destination-over", DestinationOver) \
    E("source-in", SourceIn)               \
    E("destination-in", DestinationIn)     \
    E("source-out", SourceOut)             \
    E("destination-out", DestinationOut)   \
    E("source-atop", SourceATop)           \
    E("destination-atop", DestinationATop) \
    E("xor", Xor)                          \
    E("lighter", Lighter)                  \
    E("plus-darker", PlusDarker)           \
    E("plus-lighter", PlusLighter)
#define ENUMERATE_COMPOSITE_OPERATION(name, operation) \
    case Gfx::CompositingAndBlendingOperator::operation: \
        return name##_string;
        ENUMERATE_COMPOSITE_OPERATIONS(ENUMERATE_COMPOSITE_OPERATION)
#undef ENUMERATE_COMPOSITE_OPERATION
    default:
        VERIFY_NOT_REACHED();
    }
}

// https://html.spec.whatwg.org/multipage/canvas.html#dom-context-2d-globalcompositeoperation
void OffscreenCanvasRenderingContext2D::set_global_composite_operation(String global_composite_operation)
{
#define SET_COMPOSITE_OPERATION(name, operation)                                                       \
    if (global_composite_operation == name##sv) {                                                     \
        drawing_state().current_compositing_and_blending_operator = Gfx::CompositingAndBlendingOperator::operation; \
        return;                                                                                        \
    }
    ENUMERATE_COMPOSITE_OPERATIONS(SET_COMPOSITE_OPERATION)
#undef SET_COMPOSITE_OPERATION
#undef ENUMERATE_COMPOSITE_OPERATIONS
}

[[nodiscard]] Gfx::Painter* OffscreenCanvasRenderingContext2D::painter()
{
    if (m_painter)
        return m_painter.ptr();

    auto bitmap = m_canvas->bitmap();
    if (!bitmap)
        return nullptr;

    m_painter = Gfx::Painter::create(bitmap.release_nonnull());
    m_painter->set_transform(drawing_state().transform);
    return m_painter.ptr();
}

}
