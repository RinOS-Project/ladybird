/*
 * Copyright (c) 2026, the Ladybird developers.
 *
 * SPDX-License-Identifier: BSD-2-Clause
 */

#include <AK/CharacterTypes.h>
#include <AK/StringBuilder.h>
#include <LibXML/Parser/Parser.h>

namespace XML {

static constexpr int MAX_XML_TREE_DEPTH = 5000;

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

    Vector<ParseError> take_parse_errors() { return move(m_parse_errors); }
    Version version() const { return m_version; }

private:
    static bool is_name_char(char ch)
    {
        return is_ascii_alphanumeric(static_cast<unsigned char>(ch)) || ch == '_' || ch == ':' || ch == '-' || ch == '.';
    }

    ParseError make_parse_error(StringView message, Optional<LineTrackingLexer::Position> position = {})
    {
        return ParseError { position.value_or(m_lexer.current_position()), ByteString(message) };
    }

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

    ErrorOr<ByteString, ParseError> decode_entities(StringView raw)
    {
        StringBuilder builder;
        size_t index = 0;
        while (index < raw.length()) {
            if (raw[index] != '&') {
                builder.append(raw.substring_view(index, 1));
                ++index;
                continue;
            }

            auto end = raw.find(';', index + 1);
            if (!end.has_value()) {
                builder.append('&');
                ++index;
                continue;
            }

            auto entity = raw.substring_view(index + 1, *end - index - 1);
            index = *end + 1;

            if (entity == "lt"sv) {
                builder.append('<');
                continue;
            }
            if (entity == "gt"sv) {
                builder.append('>');
                continue;
            }
            if (entity == "amp"sv) {
                builder.append('&');
                continue;
            }
            if (entity == "quot"sv) {
                builder.append('"');
                continue;
            }
            if (entity == "apos"sv) {
                builder.append('\'');
                continue;
            }

            if (entity.starts_with('#')) {
                u32 code_point = 0;
                bool valid = false;
                if (entity.length() > 2 && (entity[1] == 'x' || entity[1] == 'X')) {
                    auto value = entity.substring_view(2).to_number<u32>(TrimWhitespace::No, 16);
                    if (value.has_value()) {
                        code_point = value.value();
                        valid = true;
                    }
                } else {
                    auto value = entity.substring_view(1).to_number<u32>(TrimWhitespace::No, 10);
                    if (value.has_value()) {
                        code_point = value.value();
                        valid = true;
                    }
                }

                if (valid) {
                    builder.append_code_point(code_point);
                    continue;
                }
            }

            if (m_is_xhtml_document && m_options.resolve_named_html_entity) {
                auto resolved = m_options.resolve_named_html_entity(entity);
                if (resolved.has_value()) {
                    builder.append(resolved.value());
                    continue;
                }
            }

            builder.append('&');
            builder.append(entity);
            builder.append(';');
        }

        return builder.to_byte_string();
    }

    ErrorOr<ByteString, ParseError> parse_attribute_value()
    {
        if (m_lexer.is_eof())
            return make_parse_error("Unexpected EOF in attribute value"sv);

        char quote = m_lexer.peek();
        size_t start = 0;
        size_t end = 0;
        if (quote == '"' || quote == '\'') {
            m_lexer.ignore();
            start = m_lexer.tell();
            while (!m_lexer.is_eof() && m_lexer.peek() != quote)
                m_lexer.ignore();
            if (m_lexer.is_eof())
                return make_parse_error("Unterminated quoted attribute value"sv);
            end = m_lexer.tell();
            m_lexer.ignore();
        } else {
            start = m_lexer.tell();
            while (!m_lexer.is_eof()) {
                auto ch = m_lexer.peek();
                if (is_ascii_space(static_cast<unsigned char>(ch)) || ch == '>' || ch == '/')
                    break;
                m_lexer.ignore();
            }
            end = m_lexer.tell();
        }

        return decode_entities(m_lexer.input().substring_view(start, end - start));
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

        auto comment = m_lexer.input().substring_view(start, m_lexer.tell() - start);
        m_lexer.ignore(3);

        if (m_options.preserve_comments)
            m_listener.comment(comment);
        return {};
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

    ErrorOr<void, ParseError> parse_xml_declaration()
    {
        auto position = m_lexer.current_position();
        if (!m_lexer.consume_specific("<?xml"sv))
            return make_error("Expected XML declaration"sv, position);

        while (!m_lexer.is_eof()) {
            skip_whitespace();
            if (m_lexer.next_is("?>"sv)) {
                m_lexer.ignore(2);
                return {};
            }

            auto attribute_name = TRY(parse_name());
            skip_whitespace();
            if (!m_lexer.consume_specific('='))
                return make_error("Expected '=' in XML declaration"sv);
            skip_whitespace();
            auto attribute_value = TRY(parse_attribute_value());

            if (attribute_name == "version"sv) {
                if (attribute_value == "1.0"sv)
                    m_version = Version::Version10;
                else
                    m_version = Version::Version11;
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
