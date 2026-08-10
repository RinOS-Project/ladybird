/*
 * Copyright (c) 2026-present, the Ladybird developers.
 *
 * SPDX-License-Identifier: BSD-2-Clause
 */

#include <AK/CharacterTypes.h>
#include <AK/StringBuilder.h>
#include <LibXML/Parser/Parser.h>

namespace XML {

static constexpr int MAX_XML_TREE_DEPTH = 5000;

struct ParserContext {
    Listener* listener { nullptr };
    Optional<ParseError> error;
    bool document_ended { false };

    OwnPtr<Node> root_node;
    Node* current_node { nullptr };
    Optional<Doctype> doctype;
    HashMap<Name, ByteString> processing_instructions;
    Version version { Version::Version11 };

    Vector<ParseError> parse_errors;

    Parser::Options const* options { nullptr };
    bool is_xhtml_document { false };
    int depth { 0 };
};

static ByteString xml_char_to_byte_string(xmlChar const* str)
{
    if (!str)
        return {};
    return ByteString(reinterpret_cast<char const*>(str));
}

static ByteString xml_char_to_byte_string(xmlChar const* str, int len)
{
    if (!str || len <= 0)
        return {};
    return ByteString(StringView(reinterpret_cast<char const*>(str), static_cast<size_t>(len)));
}

static StringView xml_char_to_string_view(xmlChar const* str)
{
    if (!str)
        return {};
    return StringView(reinterpret_cast<char const*>(str), strlen(reinterpret_cast<char const*>(str)));
}

static StringView xml_char_to_string_view(xmlChar const* str, int len)
{
    if (!str || len <= 0)
        return {};
    return StringView(reinterpret_cast<char const*>(str), static_cast<size_t>(len));
}

static Utf16FlyString xml_name_to_utf16_fly_string(xmlChar const* localname, xmlChar const* prefix)
{
    StringBuilder builder;
    if (prefix) {
        builder.append(xml_char_to_string_view(prefix));
        builder.append(':');
    }
    builder.append(xml_char_to_string_view(localname));
    return Utf16FlyString::from_utf8(builder.string_view());
}

static ByteString xml_name_to_byte_string(xmlChar const* localname, xmlChar const* prefix)
{
    StringBuilder builder;
    if (prefix) {
        builder.append(xml_char_to_string_view(prefix));
        builder.append(':');
    }
    builder.append(xml_char_to_string_view(localname));
    return builder.to_byte_string();
}

static bool is_known_xhtml_public_id(StringView public_id)
{
    return public_id.is_one_of(
        "-//W3C//DTD XHTML 1.0 Transitional//EN"sv,
        "-//W3C//DTD XHTML 1.1//EN"sv,
        "-//W3C//DTD XHTML 1.0 Strict//EN"sv,
        "-//W3C//DTD XHTML 1.0 Frameset//EN"sv,
        "-//W3C//DTD XHTML Basic 1.0//EN"sv,
        "-//W3C//DTD XHTML 1.1 plus MathML 2.0//EN"sv,
        "-//W3C//DTD XHTML 1.1 plus MathML 2.0 plus SVG 1.1//EN"sv,
        "-//W3C//DTD MathML 2.0//EN"sv,
        "-//WAPFORUM//DTD XHTML Mobile 1.0//EN"sv,
        "-//WAPFORUM//DTD XHTML Mobile 1.1//EN"sv,
        "-//WAPFORUM//DTD XHTML Mobile 1.2//EN"sv);
}

class DOMBuilder final : public Listener {
public:
    virtual ErrorOr<void> set_source(ByteString) override { return {}; }

    virtual void set_doctype(XML::Doctype doctype) override
    {
        m_doctype = move(doctype);
    }

    virtual void element_start(Name const& name, OrderedHashMap<Name, ByteString> const& attributes) override
    {
        auto node = adopt_own(*new Node { {}, Node::Element { name, attributes, {} }, nullptr });

        auto* raw = node.ptr();
        if (!m_stack.is_empty()) {
            raw->parent = m_stack.last();
            m_stack.last()->content.get<Node::Element>().children.append(move(node));
        } else {
            m_root = move(node);
        }
        m_stack.append(raw);
    }

    virtual void element_end(Name const&) override
    {
        if (!m_stack.is_empty())
            m_stack.take_last();
    }

    virtual void text(StringView data) override
    {
        if (m_stack.is_empty() || data.is_empty())
            return;

        auto& children = m_stack.last()->content.get<Node::Element>().children;
        if (!children.is_empty() && children.last()->is_text()) {
            children.last()->content.get<Node::Text>().builder.append(data);
            return;
        }

        auto node = adopt_own(*new Node { {}, Node::Text {}, m_stack.last() });
        node->content.get<Node::Text>().builder.append(data);
        children.append(move(node));
    }

    virtual void cdata_section(StringView data) override
    {
        text(data);
    }

    virtual void comment(StringView data) override
    {
        if (m_stack.is_empty())
            return;

        auto node = adopt_own(*new Node { {}, Node::Comment { ByteString(data) }, m_stack.last() });
        m_stack.last()->content.get<Node::Element>().children.append(move(node));
    }

    virtual void processing_instruction(StringView target, StringView data) override
    {
        m_processing_instructions.set(ByteString(target), ByteString(data));
    }

    NonnullOwnPtr<Node> release_root() { return m_root.release_nonnull(); }
    bool has_root() const { return m_root; }
    Optional<Doctype> release_doctype() { return move(m_doctype); }
    HashMap<Name, ByteString> release_processing_instructions() { return move(m_processing_instructions); }

private:
    OwnPtr<Node> m_root;
    Vector<Node*> m_stack;
    Optional<Doctype> m_doctype;
    HashMap<Name, ByteString> m_processing_instructions;
};

class SimpleParser {
public:
    SimpleParser(StringView source, Parser::Options const& options, Listener& listener)
        : m_lexer(source)
        , m_options(options)
        , m_listener(listener)
    {
    }

    ErrorOr<void, ParseError> parse()
    {
        if (m_lexer.next_is("\xEF\xBB\xBF"sv))
            m_lexer.ignore(3);

        auto source_result = m_listener.set_source(ByteString(m_lexer.input()));
        if (source_result.is_error())
            return make_error("Failed to set source"sv);
        m_listener.document_start();
        m_document_started = true;

        skip_whitespace();

        if (m_lexer.next_is("<?xml"sv)) {
            auto declaration_start = m_lexer.current_position();
            TRY(parse_xml_declaration());
            m_seen_xml_declaration = true;
            skip_whitespace();
            if (m_lexer.next_is("<?xml"sv))
                return make_error("Duplicate XML declaration"sv, declaration_start);
        }

        while (!m_lexer.is_eof()) {
            skip_whitespace();
            if (m_lexer.next_is("<!--"sv)) {
                TRY(parse_comment());
                continue;
            }
            if (m_lexer.next_is("<?"sv)) {
                TRY(parse_processing_instruction());
                continue;
            }
            if (m_lexer.next_is("<!DOCTYPE"sv)) {
                TRY(parse_doctype());
                continue;
            }
            break;
        }

        skip_whitespace();
        if (m_lexer.is_eof())
            return make_error("No root element"sv);

        TRY(parse_element(0));

        while (!m_lexer.is_eof()) {
            skip_whitespace();
            if (m_lexer.is_eof())
                break;
            if (m_lexer.next_is("<!--"sv)) {
                TRY(parse_comment());
                continue;
            }
            if (m_lexer.next_is("<?"sv)) {
                TRY(parse_processing_instruction());
                continue;
            }
            return make_error("Unexpected content after root element"sv);
        }

        m_listener.document_end();
        m_document_ended = true;
        return {};
    }

    if (context->listener) {
        auto name = xml_name_to_utf16_fly_string(localname, prefix);
        Vector<ListenerAttribute> attrs;
        attrs.ensure_capacity(static_cast<size_t>(nb_namespaces + nb_attributes));

        for (int i = 0; i < nb_namespaces; i++) {
            auto* ns_prefix = namespaces[i * 2];
            auto* ns_uri = namespaces[i * 2 + 1];

            StringBuilder attr_name;
            if (ns_prefix) {
                attr_name.append("xmlns:"sv);
                attr_name.append(xml_char_to_string_view(ns_prefix));
            } else {
                attr_name.append("xmlns"sv);
            }

            attrs.unchecked_append({
                Utf16FlyString::from_utf8(attr_name.string_view()),
                Utf16String::from_utf8(xml_char_to_string_view(ns_uri)),
            });
        }

        for (int i = 0; i < nb_attributes; i++) {
            auto* attr_localname = attributes[i * 5 + 0];
            auto* attr_prefix = attributes[i * 5 + 1];
            auto* value_begin = attributes[i * 5 + 3];
            auto* value_end = attributes[i * 5 + 4];

            auto value_len = static_cast<int>(value_end - value_begin);
            attrs.unchecked_append({
                xml_name_to_utf16_fly_string(attr_localname, attr_prefix),
                Utf16String::from_utf8(xml_char_to_string_view(value_begin, value_len)),
            });
        }

        context->listener->element_start(name, attrs);
        return;
    }

    auto name = xml_name_to_byte_string(localname, prefix);
    OrderedHashMap<Name, ByteString> attrs;

    ErrorOr<void, ParseError> make_error(StringView message, Optional<LineTrackingLexer::Position> position = {})
    {
        auto error = make_parse_error(message, position);
        m_parse_errors.append(error);
        return error;
    }

    void skip_whitespace()
    {
        while (!m_lexer.is_eof() && is_ascii_space(static_cast<unsigned char>(m_lexer.peek())))
            m_lexer.ignore();
    }

    ErrorOr<ByteString, ParseError> parse_name()
    {
        if (m_lexer.is_eof() || !is_name_char(m_lexer.peek()))
            return make_parse_error("Expected XML name"sv);

        auto start = m_lexer.tell();
        while (!m_lexer.is_eof() && is_name_char(m_lexer.peek()))
            m_lexer.ignore();

        return ByteString(m_lexer.input().substring_view(start, m_lexer.tell() - start));
    }

    auto element = adopt_own(*new Node {
        .offset = {},
        .content = Node::Element { name, move(attrs), {} },
        .parent = context->current_node,
    });

    auto* element_ptr = element.ptr();

    if (context->current_node) {
        VERIFY(context->current_node->is_element());
        context->current_node->content.get<Node::Element>().children.append(move(element));
    } else {
        context->root_node = move(element);
    }

    context->current_node = element_ptr;
}

    ErrorOr<void, ParseError> parse_comment()
    {
        auto position = m_lexer.current_position();
        if (!m_lexer.consume_specific("<!--"sv))
            return make_error("Expected comment start"sv, position);

        auto start = m_lexer.tell();
        while (!m_lexer.is_eof() && !m_lexer.next_is("-->"sv))
            m_lexer.ignore();
        if (m_lexer.is_eof())
            return make_error("Unterminated comment"sv, position);

    if (context->listener) {
        auto name = xml_name_to_utf16_fly_string(localname, prefix);
        context->listener->element_end(name);
    } else if (context->current_node) {
        context->current_node = context->current_node->parent;
    }

    ErrorOr<void, ParseError> parse_cdata()
    {
        auto position = m_lexer.current_position();
        if (!m_lexer.consume_specific("<![CDATA["sv))
            return make_error("Expected CDATA section"sv, position);

        auto start = m_lexer.tell();
        while (!m_lexer.is_eof() && !m_lexer.next_is("]]>"sv))
            m_lexer.ignore();
        if (m_lexer.is_eof())
            return make_error("Unterminated CDATA section"sv, position);

        auto data = m_lexer.input().substring_view(start, m_lexer.tell() - start);
        m_lexer.ignore(3);

        if (m_options.preserve_cdata)
            m_listener.cdata_section(data);
        else
            m_listener.text(data);
        return {};
    }

    ErrorOr<void, ParseError> parse_processing_instruction()
    {
        auto position = m_lexer.current_position();
        if (!m_lexer.consume_specific("<?"sv))
            return make_error("Expected processing instruction"sv, position);

        auto target = TRY(parse_name());
        skip_whitespace();
        auto data_start = m_lexer.tell();
        while (!m_lexer.is_eof() && !m_lexer.next_is("?>"sv))
            m_lexer.ignore();
        if (m_lexer.is_eof())
            return make_error("Unterminated processing instruction"sv, position);

        auto data = m_lexer.input().substring_view(data_start, m_lexer.tell() - data_start).trim_whitespace();
        m_lexer.ignore(2);
        m_listener.processing_instruction(target, data);
        return {};
    }

static void processing_instruction_handler(void* ctx, xmlChar const* target, xmlChar const* data)
{
    auto* parser_ctx = static_cast<xmlParserCtxtPtr>(ctx);
    auto* context = static_cast<ParserContext*>(parser_ctx->_private);
    if (!context)
        return;

    // Processing instructions inside a DTD subset are not document children.
    if (parser_ctx->inSubset != 0)
        return;

    if (context->listener) {
        auto target_str = Utf16FlyString::from_utf8(xml_char_to_string_view(target));
        auto data_str = Utf16String::from_utf8(xml_char_to_string_view(data));
        context->listener->processing_instruction(target_str, data_str);
    } else {
        auto target_str = xml_char_to_byte_string(target);
        auto data_str = xml_char_to_byte_string(data);
        context->processing_instructions.set(target_str, data_str);
    }
}

        return make_error("Unterminated XML declaration"sv, position);
    }

    ErrorOr<void, ParseError> parse_doctype()
    {
        auto position = m_lexer.current_position();
        if (!m_lexer.consume_specific("<!DOCTYPE"sv))
            return make_error("Expected DOCTYPE"sv, position);

        skip_whitespace();
        Doctype doctype;
        doctype.type = TRY(parse_name());
        skip_whitespace();

        if (m_lexer.next_is("PUBLIC"sv)) {
            m_lexer.ignore(6);
            skip_whitespace();
            auto public_literal = TRY(parse_attribute_value());
            skip_whitespace();
            auto system_literal = TRY(parse_attribute_value());
            doctype.external_id = ExternalID {
                .public_id = PublicID { public_literal },
                .system_id = SystemID { system_literal },
            };
            if (is_known_xhtml_public_id(public_literal))
                m_is_xhtml_document = true;
        } else if (m_lexer.next_is("SYSTEM"sv)) {
            m_lexer.ignore(6);
            skip_whitespace();
            auto system_literal = TRY(parse_attribute_value());
            doctype.external_id = ExternalID {
                .public_id = {},
                .system_id = SystemID { system_literal },
            };
        }

        skip_whitespace();
        if (m_lexer.consume_specific('[')) {
            int bracket_depth = 1;
            char quote = '\0';
            while (!m_lexer.is_eof() && bracket_depth > 0) {
                auto ch = m_lexer.consume();
                if (quote) {
                    if (ch == quote)
                        quote = '\0';
                    continue;
                }
                if (ch == '"' || ch == '\'') {
                    quote = ch;
                    continue;
                }
                if (ch == '[')
                    ++bracket_depth;
                else if (ch == ']')
                    --bracket_depth;
            }
            if (bracket_depth != 0)
                return make_error("Unterminated DOCTYPE internal subset"sv, position);
            skip_whitespace();
        }

        if (!m_lexer.consume_specific('>'))
            return make_error("Expected '>' after DOCTYPE"sv, position);

        m_listener.set_doctype(doctype);
        return {};
    }

    ErrorOr<void, ParseError> parse_element(int depth)
    {
        if (depth >= MAX_XML_TREE_DEPTH)
            return make_error("Excessive node nesting."sv);

        auto position = m_lexer.current_position();
        if (!m_lexer.consume_specific('<'))
            return make_error("Expected '<'"sv, position);

        if (m_lexer.next_is('/') || m_lexer.next_is('!') || m_lexer.next_is('?'))
            return make_error("Unexpected XML token"sv, position);

        auto name = TRY(parse_name());
        OrderedHashMap<Name, ByteString> attributes;
        skip_whitespace();

        while (!m_lexer.is_eof() && !m_lexer.next_is('>') && !m_lexer.next_is('/')) {
            auto attribute_name = TRY(parse_name());
            skip_whitespace();
            ByteString attribute_value;
            if (m_lexer.consume_specific('=')) {
                skip_whitespace();
                attribute_value = TRY(parse_attribute_value());
            }
            attributes.set(attribute_name, move(attribute_value));
            skip_whitespace();
        }

        bool self_closing = false;
        if (m_lexer.consume_specific('/'))
            self_closing = true;
        if (!m_lexer.consume_specific('>'))
            return make_error("Expected '>' after start tag"sv, position);

        m_listener.element_start(name, attributes);
        if (self_closing) {
            m_listener.element_end(name);
            return {};
        }

        while (!m_lexer.is_eof()) {
            if (m_lexer.next_is("</"sv)) {
                m_lexer.ignore(2);
                auto close_name = TRY(parse_name());
                skip_whitespace();
                if (!m_lexer.consume_specific('>'))
                    return make_error("Expected '>' after end tag"sv);
                if (close_name != name)
                    return make_error(ByteString::formatted("Mismatched end tag, expected </{}>", name));
                m_listener.element_end(name);
                return {};
            }

            if (m_lexer.next_is("<!--"sv)) {
                TRY(parse_comment());
                continue;
            }

            if (m_lexer.next_is("<![CDATA["sv)) {
                TRY(parse_cdata());
                continue;
            }

            if (m_lexer.next_is("<?"sv)) {
                TRY(parse_processing_instruction());
                continue;
            }

            if (m_lexer.next_is('<')) {
                TRY(parse_element(depth + 1));
                continue;
            }

            auto start = m_lexer.tell();
            while (!m_lexer.is_eof() && !m_lexer.next_is('<'))
                m_lexer.ignore();
            auto decoded = TRY(decode_entities(m_lexer.input().substring_view(start, m_lexer.tell() - start)));
            if (!decoded.is_empty())
                m_listener.text(decoded);
        }

        return make_error(ByteString::formatted("Missing closing tag for <{}>", name), position);
    }

public:
    bool document_ended() const { return m_document_ended; }

private:
    LineTrackingLexer m_lexer;
    Parser::Options const& m_options;
    Listener& m_listener;
    Vector<ParseError> m_parse_errors;
    Version m_version { Version::Version11 };
    bool m_is_xhtml_document { false };
    bool m_seen_xml_declaration { false };
    bool m_document_started { false };
    bool m_document_ended { false };
};

ErrorOr<void, ParseError> Parser::parse_with_listener(Listener& listener)
{
    SimpleParser parser { m_source, m_options, listener };
    auto result = parser.parse();
    m_parse_errors = parser.take_parse_errors();

    if (!parser.document_ended())
        listener.document_end();

    if (result.is_error() && m_options.treat_errors_as_fatal)
        return result.release_error();
    return {};
}

ErrorOr<Document, ParseError> Parser::parse()
{
    DOMBuilder builder;
    SimpleParser parser { m_source, m_options, builder };
    auto result = parser.parse();
    m_parse_errors = parser.take_parse_errors();

    if (result.is_error() && m_options.treat_errors_as_fatal)
        return result.release_error();

    if (!builder.has_root()) {
        if (!m_parse_errors.is_empty())
            return m_parse_errors.first();
        return ParseError { {}, ByteString("No root element") };
    }

    return Document(builder.release_root(), builder.release_doctype(), builder.release_processing_instructions(), parser.version());
}

}
