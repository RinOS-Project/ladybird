/*
 * Copyright (c) 2026-present, the Ladybird developers.
 *
 * SPDX-License-Identifier: BSD-2-Clause
 */

#include <AK/OwnPtr.h>
#include <AK/Utf16View.h>
#include <LibRegex/ECMAScriptRegex.h>
#include <LibRegex/Regex.h>

namespace regex {

struct ECMAScriptRegex::Impl {
    Regex<ECMA262Parser> regex;
    ECMAScriptCompileFlags flags;
    Vector<int> capture_slots;
    Vector<ECMAScriptRegex::MatchPair> find_all_matches;
    Vector<ECMAScriptNamedCaptureGroup> named_groups;
};

static ECMAScriptOptions to_regex_options(ECMAScriptCompileFlags flags)
{
    ECMAScriptOptions options {};
    if (flags.ignore_case)
        options |= ECMAScriptFlags::Insensitive;
    if (flags.multiline)
        options |= ECMAScriptFlags::Multiline;
    if (flags.dot_all)
        options |= ECMAScriptFlags::SingleLine;
    if (flags.unicode)
        options |= ECMAScriptFlags::Unicode;
    if (flags.unicode_sets)
        options |= ECMAScriptFlags::UnicodeSets;
    if (flags.sticky)
        options |= ECMAScriptFlags::Sticky;
    return options;
}

static void reset_capture_slots(Vector<int>& capture_slots, unsigned total_groups)
{
    capture_slots.resize(total_groups * 2);
    for (auto& slot : capture_slots)
        slot = -1;
}

static int to_code_unit_offset(Utf16View view, size_t offset, bool unicode_mode)
{
    if (!unicode_mode)
        return static_cast<int>(offset);
    return static_cast<int>(view.code_unit_offset_of(offset));
}

static void store_match_slot(Vector<int>& capture_slots, unsigned group_index, Match const& match, Utf16View input, size_t start_pos, bool unicode_mode)
{
    auto start_slot = group_index * 2;
    auto end_slot = start_slot + 1;
    if (match.view.is_null()) {
        capture_slots[start_slot] = -1;
        capture_slots[end_slot] = -1;
        return;
    }

    auto sub_input = input.substring_view(start_pos, input.length_in_code_units() - start_pos);
    auto start = to_code_unit_offset(sub_input, match.global_offset, unicode_mode) + static_cast<int>(start_pos);
    auto end = to_code_unit_offset(sub_input, match.global_offset + match.view.length(), unicode_mode) + static_cast<int>(start_pos);

    capture_slots[start_slot] = start;
    capture_slots[end_slot] = end;
}

ErrorOr<ECMAScriptRegex, String> ECMAScriptRegex::compile(StringView utf8_pattern, ECMAScriptCompileFlags flags)
{
    auto regex = Regex<ECMA262Parser> { utf8_pattern.to_byte_string(), to_regex_options(flags) };
    if (regex.parser_result.error != Error::NoError) {
        auto compile_error = regex.error_string();
        return String::from_utf8(compile_error.view()).release_value_but_fixme_should_propagate_errors();
    }

    Vector<ECMAScriptNamedCaptureGroup> named_groups;
    regex.parser_result.bytecode.visit([&](auto const& bytecode) {
        for (unsigned group_index = 0; group_index < regex.parser_result.capture_groups_count; ++group_index) {
            auto maybe_name_index = bytecode.get_group_name_index(group_index);
            if (!maybe_name_index.has_value())
                continue;

            auto group_name = MUST(String::from_utf8(bytecode.get_string(maybe_name_index.value()).bytes_as_string_view()));
            named_groups.unchecked_append(ECMAScriptNamedCaptureGroup {
                .name = move(group_name),
                .index = group_index + 1,
            });
        }
    });

    auto impl = adopt_own(*new Impl {
        .regex = move(regex),
        .flags = flags,
        .capture_slots = {},
        .find_all_matches = {},
        .named_groups = move(named_groups),
    });
    return ECMAScriptRegex(move(impl));
}

ECMAScriptRegex::~ECMAScriptRegex() = default;

ECMAScriptRegex::ECMAScriptRegex(ECMAScriptRegex&& other) = default;
ECMAScriptRegex& ECMAScriptRegex::operator=(ECMAScriptRegex&& other) = default;

ECMAScriptRegex::ECMAScriptRegex(OwnPtr<Impl> impl)
    : m_impl(move(impl))
{
}

MatchResult ECMAScriptRegex::exec(Utf16View input, size_t start_pos) const
{
    auto unicode_mode = m_impl->flags.unicode || m_impl->flags.unicode_sets;
    auto result = m_impl->regex.search(input.substring_view(start_pos, input.length_in_code_units() - start_pos));
    reset_capture_slots(m_impl->capture_slots, total_groups());

    if (!result.success || result.matches.is_empty())
        return MatchResult::NoMatch;

    if (m_impl->flags.sticky && result.matches[0].global_offset != 0)
        return MatchResult::NoMatch;

    store_match_slot(m_impl->capture_slots, 0, result.matches[0], input, start_pos, unicode_mode);
    if (!result.capture_group_matches.is_empty()) {
        auto captures = result.capture_group_matches[0];
        for (unsigned capture_group = 0; capture_group < captures.size(); ++capture_group)
            store_match_slot(m_impl->capture_slots, capture_group + 1, captures[capture_group], input, start_pos, unicode_mode);
    }

    return MatchResult::Match;
}

int ECMAScriptRegex::capture_slot(unsigned int slot) const
{
    if (slot >= m_impl->capture_slots.size())
        return -1;
    return m_impl->capture_slots[slot];
}

MatchResult ECMAScriptRegex::test(Utf16View input, size_t start_pos) const
{
    auto result = m_impl->regex.search(input.substring_view(start_pos, input.length_in_code_units() - start_pos));
    if (!result.success || result.matches.is_empty())
        return MatchResult::NoMatch;
    if (m_impl->flags.sticky && result.matches[0].global_offset != 0)
        return MatchResult::NoMatch;
    return MatchResult::Match;
}

unsigned int ECMAScriptRegex::capture_count() const
{
    return m_impl->regex.parser_result.capture_groups_count;
}

unsigned int ECMAScriptRegex::total_groups() const
{
    return capture_count() + 1;
}

bool ECMAScriptRegex::is_single_non_bmp_literal() const
{
    if (!(m_impl->flags.unicode || m_impl->flags.unicode_sets))
        return false;
    auto const& literal = m_impl->regex.parser_result.optimization_data.pure_substring_search;
    return literal.has_value()
        && literal->size() == 2
        && literal->at(0) >= 0xD800 && literal->at(0) <= 0xDBFF
        && literal->at(1) >= 0xDC00 && literal->at(1) <= 0xDFFF;
}

Vector<ECMAScriptNamedCaptureGroup> const& ECMAScriptRegex::named_groups() const
{
    return m_impl->named_groups;
}

int ECMAScriptRegex::find_all(Utf16View input, size_t start_pos) const
{
    auto unicode_mode = m_impl->flags.unicode || m_impl->flags.unicode_sets;
    auto result = m_impl->regex.search(input.substring_view(start_pos, input.length_in_code_units() - start_pos));
    m_impl->find_all_matches.clear();

    if (!result.success)
        return 0;

    m_impl->find_all_matches.ensure_capacity(result.matches.size());
    for (auto const& match : result.matches) {
        if (m_impl->flags.sticky && match.global_offset != 0) {
            m_impl->find_all_matches.clear();
            return 0;
        }

        auto sub_input = input.substring_view(start_pos, input.length_in_code_units() - start_pos);
        auto start = to_code_unit_offset(sub_input, match.global_offset, unicode_mode) + static_cast<int>(start_pos);
        auto end = to_code_unit_offset(sub_input, match.global_offset + match.view.length(), unicode_mode) + static_cast<int>(start_pos);
        m_impl->find_all_matches.unchecked_append({ start, end });
    }

    return static_cast<int>(m_impl->find_all_matches.size());
}

ECMAScriptRegex::MatchPair ECMAScriptRegex::find_all_match(int index) const
{
    if (index < 0 || static_cast<size_t>(index) >= m_impl->find_all_matches.size())
        return { -1, -1 };
    return m_impl->find_all_matches[static_cast<size_t>(index)];
}

}
