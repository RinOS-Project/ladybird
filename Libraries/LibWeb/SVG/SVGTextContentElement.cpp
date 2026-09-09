/*
 * Copyright (c) 2022, Andreas Kling <andreas@ladybird.org>
 * Copyright (c) 2023, Aliaksandr Kalenik <kalenik.aliaksandr@gmail.com>
 *
 * SPDX-License-Identifier: BSD-2-Clause
 */

#include <LibWeb/Bindings/SVGTextContentElementPrototype.h>
#include <LibWeb/DOM/Document.h>
#include <LibWeb/DOM/Text.h>
#include <LibWeb/Geometry/DOMMatrix.h>
#include <LibWeb/Layout/Node.h>
#include <LibWeb/Painting/PaintableWithLines.h>
#include <LibWeb/Painting/TextPaintable.h>
#include <LibWeb/SVG/AttributeParser.h>
#include <LibWeb/SVG/SVGTextContentElement.h>

namespace Web::SVG {

SVGTextContentElement::SVGTextContentElement(DOM::Document& document, DOM::QualifiedName qualified_name)
    : SVGGraphicsElement(document, move(qualified_name))
{
}

void SVGTextContentElement::initialize(JS::Realm& realm)
{
    WEB_SET_PROTOTYPE_FOR_INTERFACE(SVGTextContentElement);
    Base::initialize(realm);
}

// NB: Called during painting.
Optional<TextAnchor> SVGTextContentElement::text_anchor() const
{
    if (!unsafe_layout_node())
        return {};
    switch (unsafe_layout_node()->computed_values().text_anchor()) {
    case CSS::TextAnchor::Start:
        return TextAnchor::Start;
    case CSS::TextAnchor::Middle:
        return TextAnchor::Middle;
    case CSS::TextAnchor::End:
        return TextAnchor::End;
    default:
        VERIFY_NOT_REACHED();
    }
}

Utf16String SVGTextContentElement::text_contents() const
{
    return child_text_content().trim_ascii_whitespace();
}

// https://svgwg.org/svg2-draft/text.html#__svg__SVGTextContentElement__getNumberOfChars
WebIDL::ExceptionOr<WebIDL::Long> SVGTextContentElement::get_number_of_chars() const
{
    return static_cast<WebIDL::Long>(text_contents().length_in_code_units());
}

GC::Ref<Geometry::DOMPoint> SVGTextContentElement::get_start_position_of_char(WebIDL::UnsignedLong charnum)
{
    auto point = Geometry::DOMPoint::create(realm());

    // SVG character indices are UTF-16 code-unit offsets in the text content.
    // Keep the method bounded for detached/unrendered text and out-of-range
    // indices instead of fabricating a position from the element's box.
    auto text_length = text_contents().length_in_code_units();
    if (charnum >= text_length)
        return point;

    const_cast<DOM::Document&>(document()).update_layout_if_needed_for_node(*this, DOM::UpdateLayoutReason::SVGGraphicsElementGetBBox);

    size_t text_offset = 0;
    bool found = false;
    double screen_x = 0;
    double screen_y = 0;

    for_each_in_inclusive_subtree_of_type<DOM::Text>([&](DOM::Text const& text_node) {
        if (found)
            return TraversalDecision::Break;

        auto const* layout_node = as_if<Layout::TextNode>(text_node.unsafe_layout_node());
        auto const* text_paintable = text_node.unsafe_paintable();
        if (!layout_node || !text_paintable)
            return TraversalDecision::Continue;

        auto node_length = layout_node->text_for_rendering().length_in_code_units();
        if (charnum >= text_offset + node_length) {
            text_offset += node_length;
            return TraversalDecision::Continue;
        }

        auto const* lines = text_paintable->parent();
        for (; lines; lines = lines->parent()) {
            auto const* paintable_with_lines = as_if<Painting::PaintableWithLines>(*lines);
            if (!paintable_with_lines)
                continue;

            for (auto const& fragment : paintable_with_lines->fragments()) {
                if (&fragment.layout_node() != layout_node)
                    continue;
                auto glyph_run = fragment.glyph_run();
                if (!glyph_run)
                    continue;

                auto target_offset = static_cast<size_t>(charnum - text_offset);
                size_t offset = 0;
                float inline_position = 0;
                for (auto const& glyph : glyph_run->glyphs()) {
                    if (target_offset < offset + glyph.length_in_code_units)
                        break;
                    offset += glyph.length_in_code_units;
                    inline_position += glyph.glyph_width;
                }

                auto rect = fragment.absolute_rect();
                if (fragment.orientation() == Gfx::Orientation::Horizontal) {
                    screen_x = (rect.x() + CSSPixels { inline_position }).to_double();
                    screen_y = (rect.y() + fragment.baseline()).to_double();
                } else {
                    screen_x = (rect.x() + fragment.baseline()).to_double();
                    screen_y = (rect.y() + CSSPixels { inline_position }).to_double();
                }
                found = true;
                return TraversalDecision::Break;
            }
            if (found)
                break;
        }

        return found ? TraversalDecision::Break : TraversalDecision::Continue;
    });

    if (!found)
        return point;

    point->set_x(screen_x);
    point->set_y(screen_y);

    // Fragment coordinates are absolute CSS pixels. Convert them back to the
    // element's current SVG user space when a screen CTM is available.
    if (auto screen_ctm = get_screen_ctm()) {
        auto inverse = screen_ctm->inverse();
        auto local = inverse->transform_point(*point);
        point->set_x(local->x());
        point->set_y(local->y());
    }
    return point;
}

}
