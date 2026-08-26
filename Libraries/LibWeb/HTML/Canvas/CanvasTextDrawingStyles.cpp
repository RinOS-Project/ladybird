/*
 * Copyright (c) 2023, Bastiaan van der Plaat <bastiaan.v.d.plaat@gmail.com>
 * Copyright (c) 2023, Callum Law <callumlaw1709@outlook.com>
 *
 * SPDX-License-Identifier: BSD-2-Clause
 */

#include "CanvasTextDrawingStyles.h"
#include <AK/Math.h>
#include <LibWeb/CSS/ComputedProperties.h>
#include <LibWeb/CSS/FontComputer.h>
#include <LibWeb/CSS/FontFace.h>
#include <LibWeb/CSS/FontFaceSet.h>
#include <LibWeb/CSS/Parser/Parser.h>
#include <LibWeb/CSS/StyleComputer.h>
#include <LibWeb/CSS/StyleValues/FontStyleStyleValue.h>
#include <LibWeb/CSS/StyleValues/LengthStyleValue.h>
#include <LibWeb/CSS/StyleValues/NumberStyleValue.h>
#include <LibWeb/CSS/StyleValues/PercentageStyleValue.h>
#include <LibWeb/CSS/StyleValues/ShorthandStyleValue.h>
#include <LibWeb/CSS/StyleValues/StyleValueList.h>
#include <LibWeb/DOM/Document.h>
#include <LibWeb/HTML/Canvas/CanvasState.h>
#include <LibWeb/HTML/CanvasRenderingContext2D.h>
#include <LibWeb/HTML/OffscreenCanvas.h>
#include <LibWeb/HTML/OffscreenCanvasRenderingContext2D.h>
#include <LibWeb/HTML/Window.h>
#include <LibWeb/HTML/WorkerGlobalScope.h>
#include <LibWeb/Platform/FontPlugin.h>

#include <LibGfx/Font/FontDatabase.h>

namespace Web::HTML {

static Optional<Platform::GenericFont> generic_font_for_keyword(CSS::Keyword keyword)
{
    switch (keyword) {
    case CSS::Keyword::Monospace:
    case CSS::Keyword::UiMonospace:
        return Platform::GenericFont::Monospace;
    case CSS::Keyword::Serif:
        return Platform::GenericFont::Serif;
    case CSS::Keyword::Fantasy:
        return Platform::GenericFont::Fantasy;
    case CSS::Keyword::SansSerif:
        return Platform::GenericFont::SansSerif;
    case CSS::Keyword::Cursive:
        return Platform::GenericFont::Cursive;
    case CSS::Keyword::UiSerif:
        return Platform::GenericFont::UiSerif;
    case CSS::Keyword::UiSansSerif:
        return Platform::GenericFont::UiSansSerif;
    case CSS::Keyword::UiRounded:
        return Platform::GenericFont::UiRounded;
    default:
        return {};
    }
}

static RefPtr<Gfx::FontCascadeList const> compute_font_for_worker_global_scope(HTML::WorkerGlobalScope& worker, HTML::OffscreenCanvas& canvas, CSS::StyleValue const& font_style, CSS::StyleValue const& font_weight, CSS::StyleValue const& font_width, CSS::StyleValue const& font_size, CSS::StyleValue const& font_family)
{
    // A worker does not have a Document FontComputer, but canvas text still
    // has a well-defined font source: its WorkerGlobalScope. Resolve relative
    // lengths against the canvas' initial 10px font and use the canvas bitmap
    // as the viewport for viewport-relative values.
    auto initial_font = Platform::FontPlugin::the().default_font(7.5f);
    if (!initial_font)
        return {};

    auto const initial_font_size = CSSPixels::nearest_value_for(initial_font->pixel_size());
    auto const initial_font_metrics = CSS::Length::FontMetrics {
        initial_font_size,
        initial_font->pixel_metrics(),
        CSSPixels::nearest_value_for(initial_font->pixel_metrics().line_spacing()),
    };
    CSS::ComputationContext computation_context {
        .length_resolution_context = CSS::Length::ResolutionContext {
            .viewport_rect = CSSPixelRect { 0, 0, CSSPixels { canvas.width() }, CSSPixels { canvas.height() } },
            .font_metrics = initial_font_metrics,
            .root_font_metrics = initial_font_metrics,
        },
    };

    auto const computed_font_size = CSS::StyleComputer::compute_font_size(font_size.absolutized(computation_context), 0, {});
    auto const computed_font_weight = CSS::StyleComputer::compute_font_weight(font_weight.absolutized(computation_context), {});
    auto const computed_font_width = CSS::StyleComputer::compute_font_width(font_width.absolutized(computation_context));
    auto const computed_font_style = CSS::StyleComputer::compute_font_style(font_style.absolutized(computation_context));

    auto const font_size_in_px = computed_font_size->as_length().length().absolute_length_to_px();
    auto const font_size_in_pt = static_cast<float>(font_size_in_px.to_double() * 0.75);
    auto const weight = round_to<int>(computed_font_weight->as_number().number());
    auto const width = static_cast<unsigned>(round_to<int>(computed_font_width->as_percentage().percentage().value()));
    auto const slope = computed_font_style->as_font_style().to_font_slope();

    Gfx::FontVariationSettings variations;
    variations.set_weight(weight);
    variations.set_width(computed_font_width->as_percentage().percentage().value());
    variations.set_optical_sizing(font_size_in_px.to_double());

    auto font_list = Gfx::FontCascadeList::create();
    auto append_system_font = [&](FlyString const& family) {
        if (auto font = Gfx::FontDatabase::the().get(family, font_size_in_pt, weight, width, slope, variations))
            font_list->add(font.release_nonnull());
    };
    auto append_worker_font_faces = [&](FlyString const& family) {
        // Loaded FontFace objects are owned by the WorkerGlobalScope's
        // FontFaceSet. Put them ahead of same-named system faces, matching the
        // normal canvas font source ordering without borrowing a Document.
        for (auto const& font_face : worker.fonts()->loaded_fonts()) {
            if (!font_face->family().equals_ignoring_ascii_case(family))
                continue;
            auto typeface = font_face->typeface();
            if (typeface)
                font_list->add(typeface->font(font_size_in_pt, variations, {}));
        }
    };

    for (auto const& family : font_family.as_value_list().values()) {
        if (family->is_keyword()) {
            auto generic_font = generic_font_for_keyword(family->to_keyword());
            if (generic_font.has_value())
                append_system_font(Platform::FontPlugin::the().generic_font_name(generic_font.value(), weight, slope));
            continue;
        }

        auto family_name = CSS::string_from_style_value(family);
        append_worker_font_faces(family_name);
        append_system_font(family_name);
    }

    auto default_family = Platform::FontPlugin::the().generic_font_name(Platform::GenericFont::UiSansSerif, weight, slope);
    if (font_list->is_empty())
        append_system_font(default_family);
    for (auto const& symbol_font : Platform::FontPlugin::the().symbol_font_names())
        append_system_font(symbol_font);

    if (auto last_resort = Gfx::FontDatabase::the().get(default_family, font_size_in_pt, weight, width, slope, variations))
        font_list->set_last_resort_font(last_resort.release_nonnull());
    else if (auto fallback_font = Platform::FontPlugin::the().default_font(font_size_in_pt, variations))
        font_list->set_last_resort_font(fallback_font.release_nonnull());

    if (font_list->is_empty())
        return {};

    font_list->set_system_font_fallback_callback([](u32 code_point, Gfx::Font const& reference_font) -> RefPtr<Gfx::Font const> {
        return Gfx::FontDatabase::the().get_font_for_code_point(
            code_point,
            reference_font.point_size(),
            reference_font.weight(),
            reference_font.typeface().width(),
            reference_font.slope());
    });
    return font_list;
}

template<typename IncludingClass, typename CanvasType>
ByteString CanvasTextDrawingStyles<IncludingClass, CanvasType>::font() const
{
    // When font style value is empty return default string
    if (!my_drawing_state().font_style_value) {
        return "10px sans-serif";
    }

    // On getting, the font attribute must return the serialized form of the current font of the context (with no 'line-height' component).
    auto const& font_style_value = my_drawing_state().font_style_value->as_shorthand();
    auto font_style = font_style_value.longhand(CSS::PropertyID::FontStyle);
    auto font_weight = font_style_value.longhand(CSS::PropertyID::FontWeight);
    auto font_size = font_style_value.longhand(CSS::PropertyID::FontSize);
    auto font_family = font_style_value.longhand(CSS::PropertyID::FontFamily);
    return ByteString::formatted("{} {} {} {}",
        font_style->to_string(CSS::SerializationMode::Normal),
        font_weight->to_string(CSS::SerializationMode::Normal),
        font_size->to_string(CSS::SerializationMode::Normal),
        font_family->to_string(CSS::SerializationMode::Normal));
}

// https://html.spec.whatwg.org/multipage/canvas.html#font-style-source-object
template<typename IncludingClass, typename CanvasType>
Variant<DOM::Document*, HTML::WorkerGlobalScope*> CanvasTextDrawingStyles<IncludingClass, CanvasType>::get_font_source_for_font_style_source_object(CanvasType& font_style_source_object)
{
    // Font resolution for the font style source object requires a font source. This is determined for a given object implementing CanvasTextDrawingStyles by the following steps: [CSSFONTLOAD]

    if constexpr (SameAs<CanvasType, HTML::HTMLCanvasElement>) {
        // 1. If object's font style source object is a canvas element, return the element's node document.
        return &font_style_source_object.document();
    } else {
        // 2. Otherwise, object's font style source object is an OffscreenCanvas object:

        // 1. Let global be object's relevant global object.
        auto& global_object = HTML::relevant_global_object(font_style_source_object);

        // 2. If global is a Window object, then return global's associated Document.
        if (is<HTML::Window>(global_object)) {
            auto& window = as<HTML::Window>(global_object);
            return &(window.associated_document());
        }

        // 3. Assert: global implements WorkerGlobalScope.
        VERIFY(is<HTML::WorkerGlobalScope>(global_object));

        // 4. Return global.
        return &(as<HTML::WorkerGlobalScope>(global_object));
    };
}
template<typename IncludingClass, typename CanvasType>
void CanvasTextDrawingStyles<IncludingClass, CanvasType>::set_font(StringView font)
{
    // The font IDL attribute, on setting, must be parsed as a CSS <'font'> value (but without supporting property-independent style sheet syntax like 'inherit'),
    // and the resulting font must be assigned to the context, with the 'line-height' component forced to 'normal', with the 'font-size' component converted to CSS pixels,
    // and with system fonts being computed to explicit values.
    // FIXME: with the 'line-height' component forced to 'normal'
    // FIXME: with the 'font-size' component converted to CSS pixels
    // FIXME: Disallow tree counting functions if this is an offscreen canvas
    auto font_style_value_result = parse_css_value(CSS::Parser::ParsingParams {}, font, CSS::PropertyID::Font);

    // If the new value is syntactically incorrect (including using property-independent style sheet syntax like 'inherit' or 'initial'), then it must be ignored, without assigning a new font value.
    // NOTE: ShorthandStyleValue should be the only valid option here. We implicitly VERIFY this below.
    if (!font_style_value_result || !font_style_value_result->is_shorthand()) {
        return;
    }
    my_drawing_state().font_style_value = font_style_value_result.release_nonnull();

    // Load font with font style value properties
    auto const& font_style_value = my_drawing_state().font_style_value->as_shorthand();
    auto& canvas_element = static_cast<IncludingClass&>(*this).canvas_element();

    auto& font_style = *font_style_value.longhand(CSS::PropertyID::FontStyle);
    auto& font_weight = *font_style_value.longhand(CSS::PropertyID::FontWeight);
    auto& font_width = *font_style_value.longhand(CSS::PropertyID::FontWidth);
    auto& font_size = *font_style_value.longhand(CSS::PropertyID::FontSize);
    auto& font_family = *font_style_value.longhand(CSS::PropertyID::FontFamily);

    // https://drafts.csswg.org/css-font-loading/#font-source
    auto font_source = get_font_source_for_font_style_source_object(canvas_element);

    auto font_list = font_source.visit(
        [&](DOM::Document* document) -> RefPtr<Gfx::FontCascadeList const> {
            auto computed_math_depth = CSS::InitialValues::math_depth();

            // NOTE: The initial value here is non-standard as the default font is "10px sans-serif"
            // FIXME: Investigate whether this is the correct resolution context (i.e. whether we should instead use
            //        a font-size of 10px) for OffscreenCanvas
            auto length_resolution_context = CSS::Length::ResolutionContext::for_window(*document->window());
            Optional<DOM::AbstractElement> abstract_element;

            if constexpr (SameAs<CanvasType, HTML::HTMLCanvasElement>) {
                // NOTE: The canvas itself is considered the inheritance parent
                if (canvas_element.computed_properties()) {
                    // NOTE: Since we can't set a math depth directly here we always use the inherited value for the computed value
                    computed_math_depth = canvas_element.computed_properties()->math_depth();
                    abstract_element = DOM::AbstractElement { canvas_element };
                    length_resolution_context = CSS::Length::ResolutionContext::for_element(abstract_element.value());
                }
            }

            CSS::ComputationContext computation_context {
                .length_resolution_context = length_resolution_context,
                .abstract_element = abstract_element
            };

            // FIXME: Should font be recomputed on canvas element style change?
            // FIXME: Respect the <font-variant-css2> portion of <'font'>
            auto const& computed_font_size = CSS::StyleComputer::compute_font_size(font_size.absolutized(computation_context), computed_math_depth, abstract_element);
            auto const& computed_font_weight = CSS::StyleComputer::compute_font_weight(font_weight.absolutized(computation_context), abstract_element);
            auto const& computed_font_width = CSS::StyleComputer::compute_font_width(font_width.absolutized(computation_context));
            auto const& computed_font_style = CSS::StyleComputer::compute_font_style(font_style.absolutized(computation_context));

            return document->font_computer().compute_font_for_style_values(
                font_family,
                computed_font_size->as_length().length().absolute_length_to_px(),
                computed_font_style->as_font_style().to_font_slope(),
                computed_font_weight->as_number().number(),
                computed_font_width->as_percentage().percentage(),
                CSS::FontOpticalSizing::Auto,
                {},
                {});
        },
        [&](HTML::WorkerGlobalScope* worker) -> RefPtr<Gfx::FontCascadeList const> {
            if constexpr (SameAs<CanvasType, HTML::OffscreenCanvas>)
                return compute_font_for_worker_global_scope(*worker, canvas_element, font_style, font_weight, font_width, font_size, font_family);
            VERIFY_NOT_REACHED();
        });

    if (!font_list)
        return;

    my_drawing_state().current_font_cascade_list = font_list;
}

// https://html.spec.whatwg.org/multipage/canvas.html#dom-context-2d-letterspacing
template<typename IncludingClass, typename CanvasType>
String CanvasTextDrawingStyles<IncludingClass, CanvasType>::letter_spacing() const
{
    // The letterSpacing getter steps are to return the serialized form of this's letter spacing.
    StringBuilder builder;
    my_drawing_state().letter_spacing.serialize(builder);
    return MUST(builder.to_string());
}

// https://html.spec.whatwg.org/multipage/canvas.html#dom-context-2d-letterspacing
template<typename IncludingClass, typename CanvasType>
void CanvasTextDrawingStyles<IncludingClass, CanvasType>::set_letter_spacing(StringView letter_spacing)
{
    // 1. Let parsed be the result of parsing the given value as a CSS <length>.
    auto parsed = parse_css_type(CSS::Parser::ParsingParams {}, letter_spacing, CSS::ValueType::Length);

    // 2. If parsed is failure, then return.
    if (!parsed || !parsed->is_length())
        return;

    // 3. Set this's letter spacing to parsed.
    my_drawing_state().letter_spacing = parsed->as_length().length();
}

template class CanvasTextDrawingStyles<CanvasRenderingContext2D, HTMLCanvasElement>;
template class CanvasTextDrawingStyles<OffscreenCanvasRenderingContext2D, HTML::OffscreenCanvas>;

}
