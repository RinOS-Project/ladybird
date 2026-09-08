/*
 * Copyright (c) 2023, Ali Mohammad Pur <mpfard@serenityos.org>
 * Copyright (c) 2025, Sam Atkins <sam@ladybird.org>
 *
 * SPDX-License-Identifier: BSD-2-Clause
 */

#include "CSSKeyframeRule.h"
#include <LibWeb/Bindings/CSSKeyframeRulePrototype.h>
#include <LibWeb/Bindings/Intrinsics.h>
#include <LibWeb/CSS/Parser/Tokenizer.h>
#include <LibWeb/CSS/CSSRuleList.h>
#include <LibWeb/Dump.h>

namespace Web::CSS {

GC_DEFINE_ALLOCATOR(CSSKeyframeRule);

GC::Ref<CSSKeyframeRule> CSSKeyframeRule::create(JS::Realm& realm, Percentage key, CSSStyleProperties& declarations)
{
    return realm.create<CSSKeyframeRule>(realm, key, declarations);
}

CSSKeyframeRule::CSSKeyframeRule(JS::Realm& realm, Percentage key, CSSStyleProperties& declarations)
    : CSSRule(realm, Type::Keyframe)
    , m_key(key)
    , m_declarations(declarations)
{
    m_declarations->set_parent_rule(*this);
}

void CSSKeyframeRule::visit_edges(Visitor& visitor)
{
    Base::visit_edges(visitor);
    visitor.visit(m_declarations);
}

void CSSKeyframeRule::initialize(JS::Realm& realm)
{
    WEB_SET_PROTOTYPE_FOR_INTERFACE(CSSKeyframeRule);
    Base::initialize(realm);
}

String CSSKeyframeRule::serialized() const
{
    StringBuilder builder;
    builder.appendff("{}% {{ {} }}", key().value(), style()->serialized());
    return MUST(builder.to_string());
}

void CSSKeyframeRule::set_key_text(String const& key_text)
{
    auto tokens = Parser::Tokenizer::tokenize(key_text.bytes_as_string_view(), "utf-8"sv);
    size_t index = 0;
    while (index < tokens.size() && tokens[index].is(Parser::Token::Type::Whitespace))
        ++index;
    if (index >= tokens.size())
        return;

    double candidate = 0.0;
    auto const& token = tokens[index++];
    if (token.is(Parser::Token::Type::Ident)) {
        if (token.ident().equals_ignoring_ascii_case("from"sv))
            candidate = 0.0;
        else if (token.ident().equals_ignoring_ascii_case("to"sv))
            candidate = 100.0;
        else
            return;
    } else if (token.is(Parser::Token::Type::Percentage)) {
        candidate = token.percentage();
    } else {
        return;
    }

    while (index < tokens.size() && tokens[index].is(Parser::Token::Type::Whitespace))
        ++index;
    if (index >= tokens.size() ||
        !tokens[index].is(Parser::Token::Type::EndOfFile) ||
        !isfinite(candidate) || candidate < 0.0 || candidate > 100.0)
        return;

    // CSSKeyframeRule currently stores one offset. A comma-separated selector
    // list therefore remains invalid and cannot partially mutate the rule.
    m_key = CSS::Percentage(candidate);
}

void CSSKeyframeRule::dump(StringBuilder& builder, int indent_levels) const
{
    Base::dump(builder, indent_levels);

    dump_indent(builder, indent_levels + 1);
    builder.appendff("Key: {}\n"sv, key_text());
    dump_style_properties(builder, style(), indent_levels + 1);
}

}
