/*
 * Copyright (c) 2023, Simon Wanner <simon@skyrising.xyz>
 * Copyright (c) 2024, Tim Flynn <trflynn89@serenityos.org>
 *
 * SPDX-License-Identifier: BSD-2-Clause
 */

#include <LibUnicode/ICU.h>
#include <LibUnicode/IDNA.h>

#ifdef AK_OS_RINOS
#    include <AK/StringBuilder.h>
#    include <AK/Utf8View.h>
#    include <AK/Vector.h>
#    include <LibUnicode/IDNAData.h>
#    include <algorithm>
#    include <array>
#    include <limits>
#    include <utility>
#else
#    include <AK/StringBuilder.h>
#endif

#ifndef AK_OS_RINOS
#    include <unicode/idna.h>
#endif

namespace Unicode::IDNA {

#ifdef AK_OS_RINOS

namespace {

using namespace Data;

static ErrorOr<String> idna_error()
{
    return Error::from_string_literal("Unable to convert domain to ASCII");
}

template<typename Row, size_t row_count, typename KeyFunction>
static Row const* find_code_point(std::array<Row, row_count> const& rows, u32 code_point, KeyFunction key)
{
    auto const iterator = std::lower_bound(rows.begin(), rows.end(), code_point, [&](auto const& row, u32 value) {
        return key(row) < value;
    });
    if (iterator == rows.end() || key(*iterator) != code_point)
        return nullptr;
    return &*iterator;
}

template<size_t row_count>
static bool contains(std::array<Range, row_count> const& ranges, u32 code_point)
{
    auto iterator = std::upper_bound(ranges.begin(), ranges.end(), code_point, [](u32 value, auto const& range) {
        return value < range.first;
    });
    if (iterator == ranges.begin())
        return false;
    auto const& range = *--iterator;
    return code_point >= range.first && code_point < range.end;
}

static Uts46Row const* uts46_row_for(u32 code_point)
{
    auto iterator = std::upper_bound(uts46_rows.begin(), uts46_rows.end(), code_point, [](u32 value, auto const& row) {
        return value < row.start;
    });
    if (iterator == uts46_rows.begin())
        return nullptr;
    return &*--iterator;
}

static u8 combining_class(u32 code_point)
{
    auto const* row = find_code_point(combining_classes, code_point, [](auto const& value) { return value.code_point; });
    return row ? row->value : 0;
}

static u8 bidi_class(u32 code_point)
{
    auto iterator = std::upper_bound(bidi_ranges.begin(), bidi_ranges.end(), code_point, [](u32 value, auto const& row) {
        return value < row.first;
    });
    if (iterator == bidi_ranges.begin())
        return 0;
    auto const& range = *--iterator;
    return code_point < range.end ? range.value : 0;
}

static char joining_type(u32 code_point)
{
    auto const* row = find_code_point(joining_types, code_point, [](auto const& value) { return value.code_point; });
    return row ? row->value : '\0';
}

static u32 compose_pair(u32 first, u32 second)
{
    constexpr u32 hangul_syllable_base = 0xAC00;
    constexpr u32 hangul_leading_base = 0x1100;
    constexpr u32 hangul_vowel_base = 0x1161;
    constexpr u32 hangul_trailing_base = 0x11A7;
    constexpr u32 hangul_leading_count = 19;
    constexpr u32 hangul_vowel_count = 21;
    constexpr u32 hangul_trailing_count = 28;
    constexpr u32 hangul_n_count = hangul_vowel_count * hangul_trailing_count;
    constexpr u32 hangul_syllable_count = hangul_leading_count * hangul_n_count;

    if (first >= hangul_leading_base && first < hangul_leading_base + hangul_leading_count
        && second >= hangul_vowel_base && second < hangul_vowel_base + hangul_vowel_count) {
        return hangul_syllable_base + ((first - hangul_leading_base) * hangul_vowel_count + (second - hangul_vowel_base)) * hangul_trailing_count;
    }
    if (first >= hangul_syllable_base && first < hangul_syllable_base + hangul_syllable_count
        && (first - hangul_syllable_base) % hangul_trailing_count == 0
        && second > hangul_trailing_base && second < hangul_trailing_base + hangul_trailing_count) {
        return first + second - hangul_trailing_base;
    }

    auto const iterator = std::lower_bound(compositions.begin(), compositions.end(), std::pair { first, second }, [](auto const& row, auto const& pair) {
        return std::pair { row.first, row.second } < pair;
    });
    if (iterator == compositions.end() || iterator->first != first || iterator->second != second)
        return 0;
    return iterator->composed;
}

static ErrorOr<void> recursively_decompose(u32 code_point, Vector<u32>& output)
{
    constexpr u32 hangul_syllable_base = 0xAC00;
    constexpr u32 hangul_leading_base = 0x1100;
    constexpr u32 hangul_vowel_base = 0x1161;
    constexpr u32 hangul_trailing_base = 0x11A7;
    constexpr u32 hangul_vowel_count = 21;
    constexpr u32 hangul_trailing_count = 28;
    constexpr u32 hangul_n_count = hangul_vowel_count * hangul_trailing_count;
    constexpr u32 hangul_syllable_count = 19 * hangul_n_count;

    if (code_point >= hangul_syllable_base && code_point < hangul_syllable_base + hangul_syllable_count) {
        auto const syllable_index = code_point - hangul_syllable_base;
        TRY(output.try_append(hangul_leading_base + syllable_index / hangul_n_count));
        TRY(output.try_append(hangul_vowel_base + (syllable_index % hangul_n_count) / hangul_trailing_count));
        auto const trailing_index = syllable_index % hangul_trailing_count;
        if (trailing_index != 0)
            TRY(output.try_append(hangul_trailing_base + trailing_index));
        return { };
    }

    auto const* decomposition = find_code_point(decompositions, code_point, [](auto const& value) { return value.code_point; });
    if (!decomposition) {
        TRY(output.try_append(code_point));
        return { };
    }

    for (size_t index = 0; index < decomposition->mapping_length; ++index)
        TRY(recursively_decompose(decomposition_mappings[decomposition->mapping_offset + index], output));
    return { };
}

static ErrorOr<Vector<u32>> normalize_nfc(Vector<u32> const& input)
{
    Vector<u32> decomposed;
    decomposed.ensure_capacity(input.size());
    for (auto code_point : input)
        TRY(recursively_decompose(code_point, decomposed));

    // Canonical ordering is a stable insertion sort by combining class within
    // each starter sequence.
    for (size_t index = 1; index < decomposed.size(); ++index) {
        auto const current_class = combining_class(decomposed[index]);
        if (current_class == 0)
            continue;
        auto insertion_index = index;
        while (insertion_index > 0) {
            auto const previous_class = combining_class(decomposed[insertion_index - 1]);
            if (previous_class == 0 || previous_class <= current_class)
                break;
            std::swap(decomposed[insertion_index], decomposed[insertion_index - 1]);
            --insertion_index;
        }
    }

    Vector<u32> composed;
    composed.ensure_capacity(decomposed.size());
    if (decomposed.is_empty())
        return composed;

    TRY(composed.try_append(decomposed[0]));
    bool has_starter = combining_class(decomposed[0]) == 0;
    auto starter_index = 0u;
    auto starter = decomposed[0];
    u8 previous_combining_class = 0;
    for (size_t index = 1; index < decomposed.size(); ++index) {
        auto const code_point = decomposed[index];
        auto const current_class = combining_class(code_point);
        auto const composite = has_starter && (previous_combining_class == 0 || previous_combining_class < current_class)
            ? compose_pair(starter, code_point)
            : 0;
        if (composite != 0) {
            composed[starter_index] = composite;
            starter = composite;
            continue;
        }

        if (current_class == 0) {
            starter_index = composed.size();
            starter = code_point;
            has_starter = true;
        }
        previous_combining_class = current_class;
        TRY(composed.try_append(code_point));
    }
    return composed;
}

static bool equal_code_points(Vector<u32> const& left, Vector<u32> const& right)
{
    if (left.size() != right.size())
        return false;
    for (size_t index = 0; index < left.size(); ++index) {
        if (left[index] != right[index])
            return false;
    }
    return true;
}

static ErrorOr<bool> map_code_point(u32 code_point, ToAsciiOptions const& options, Vector<u32>& output)
{
    auto const* row = uts46_row_for(code_point);
    if (!row)
        return false;

    auto const has_mapping = row->mapping_length != 0;
    auto const transitional = options.transitional_processing == TransitionalProcessing::Yes;
    auto const std3_rules = options.use_std3_ascii_rules == UseStd3AsciiRules::Yes;

    if (row->status == 'V' || (row->status == 'D' && !transitional) || (row->status == '3' && !std3_rules && !has_mapping)) {
        TRY(output.try_append(code_point));
        return true;
    }
    if (row->status == 'M' || (row->status == '3' && !std3_rules) || (row->status == 'D' && transitional)) {
        for (size_t index = 0; index < row->mapping_length; ++index)
            TRY(output.try_append(uts46_mappings[row->mapping_offset + index]));
        return true;
    }
    if (row->status == 'I')
        return true;
    return false;
}

static bool valid_context_j(Vector<u32> const& label, size_t position)
{
    auto const code_point = label[position];
    if (code_point == 0x200D)
        return position > 0 && combining_class(label[position - 1]) == 9;
    if (code_point != 0x200C)
        return false;

    if (position > 0 && combining_class(label[position - 1]) == 9)
        return true;

    bool found_left_joining = false;
    for (size_t index = position; index > 0;) {
        auto const type = joining_type(label[--index]);
        if (type == 'T')
            continue;
        found_left_joining = type == 'L' || type == 'D';
        break;
    }
    if (!found_left_joining)
        return false;

    for (size_t index = position + 1; index < label.size(); ++index) {
        auto const type = joining_type(label[index]);
        if (type == 'T')
            continue;
        return type == 'R' || type == 'D';
    }
    return false;
}

static bool valid_context_o(Vector<u32> const& label, size_t position)
{
    auto const code_point = label[position];
    if (code_point == 0x00B7)
        return position > 0 && position + 1 < label.size() && label[position - 1] == 'l' && label[position + 1] == 'l';
    if (code_point == 0x0375)
        return position + 1 < label.size() && contains(script_greek, label[position + 1]);
    if (code_point == 0x05F3 || code_point == 0x05F4)
        return position > 0 && contains(script_hebrew, label[position - 1]);
    if (code_point == 0x30FB) {
        for (auto candidate : label) {
            if (candidate != 0x30FB && (contains(script_han, candidate) || contains(script_hiragana, candidate) || contains(script_katakana, candidate)))
                return true;
        }
        return false;
    }
    if (code_point >= 0x0660 && code_point <= 0x0669) {
        for (auto candidate : label) {
            if (candidate >= 0x06F0 && candidate <= 0x06F9)
                return false;
        }
        return true;
    }
    if (code_point >= 0x06F0 && code_point <= 0x06F9) {
        for (auto candidate : label) {
            if (candidate >= 0x0660 && candidate <= 0x0669)
                return false;
        }
        return true;
    }
    return false;
}

static bool valid_bidi_label(Vector<u32> const& label)
{
    bool contains_right_to_left_code_point = false;
    for (auto code_point : label) {
        auto const direction = bidi_class(code_point);
        if (direction == 2 || direction == 3 || direction == 5) {
            contains_right_to_left_code_point = true;
            break;
        }
    }
    if (!contains_right_to_left_code_point)
        return true;

    auto const first_direction = bidi_class(label[0]);
    bool const right_to_left = first_direction == 2 || first_direction == 3;
    if (!right_to_left && first_direction != 1)
        return false;

    bool valid_ending = false;
    u8 number_type = 0;
    for (auto code_point : label) {
        auto const direction = bidi_class(code_point);
        if (right_to_left) {
            if (direction != 2 && direction != 3 && direction != 5 && direction != 4 && direction != 6 && direction != 7
                && direction != 8 && direction != 9 && direction != 10 && direction != 11)
                return false;
            if (direction == 2 || direction == 3 || direction == 4 || direction == 5)
                valid_ending = true;
            else if (direction != 11)
                valid_ending = false;
            if (direction == 4 || direction == 5) {
                if (number_type != 0 && number_type != direction)
                    return false;
                number_type = direction;
            }
        } else {
            if (direction != 1 && direction != 4 && direction != 6 && direction != 7 && direction != 8 && direction != 9
                && direction != 10 && direction != 11)
                return false;
            if (direction == 1 || direction == 4)
                valid_ending = true;
            else if (direction != 11)
                valid_ending = false;
        }
    }
    return valid_ending;
}

static bool valid_label(Vector<u32> const& label, ToAsciiOptions const& options)
{
    if (label.is_empty() || contains(mark_ranges, label[0]))
        return false;

    if (options.check_hyphens == CheckHyphens::No && label.size() >= 4
        && label[0] == 'x' && label[1] == 'n' && label[2] == '-' && label[3] == '-')
        return false;

    if (options.check_hyphens == CheckHyphens::Yes) {
        if (label[0] == '-' || label.last() == '-')
            return false;
        if (label.size() >= 4 && label[2] == '-' && label[3] == '-')
            return false;
    }

    for (size_t position = 0; position < label.size(); ++position) {
        auto const code_point = label[position];
        if (contains(contextj, code_point)) {
            if (options.check_joiners == CheckJoiners::No || valid_context_j(label, position))
                continue;
            return false;
        }
        if (contains(contexto, code_point)) {
            if (valid_context_o(label, position))
                continue;
            return false;
        }

        auto const* row = uts46_row_for(code_point);
        if (!row)
            return false;
        if (row->status == 'V' || (row->status == 'D' && options.transitional_processing == TransitionalProcessing::No)
            || (row->status == '3' && options.use_std3_ascii_rules == UseStd3AsciiRules::No))
            continue;
        return false;
    }

    return options.check_bidi == CheckBidi::No || valid_bidi_label(label);
}

static bool decode_punycode_digit(u32 code_point, u32& digit)
{
    if (code_point >= 'a' && code_point <= 'z') {
        digit = code_point - 'a';
        return true;
    }
    if (code_point >= 'A' && code_point <= 'Z') {
        digit = code_point - 'A';
        return true;
    }
    if (code_point >= '0' && code_point <= '9') {
        digit = code_point - '0' + 26;
        return true;
    }
    return false;
}

static char punycode_digit(u32 digit)
{
    return digit < 26 ? static_cast<char>('a' + digit) : static_cast<char>('0' + digit - 26);
}

static u64 adapt_punycode_bias(u64 delta, u64 number_of_points, bool first_time)
{
    constexpr u64 base = 36;
    constexpr u64 tmin = 1;
    constexpr u64 tmax = 26;
    constexpr u64 skew = 38;
    constexpr u64 damp = 700;

    delta = first_time ? delta / damp : delta / 2;
    delta += delta / number_of_points;
    u64 k = 0;
    while (delta > ((base - tmin) * tmax) / 2) {
        delta /= base - tmin;
        k += base;
    }
    return k + (base - tmin + 1) * delta / (delta + skew);
}

static ErrorOr<bool> encode_punycode(Vector<u32> const& input, StringBuilder& output)
{
    constexpr u64 base = 36;
    constexpr u64 tmin = 1;
    constexpr u64 tmax = 26;
    constexpr u64 initial_n = 128;
    constexpr u64 initial_bias = 72;
    constexpr u64 maximum = std::numeric_limits<u32>::max();

    u64 basic_count = 0;
    for (auto code_point : input) {
        if (code_point >= 0x80)
            continue;
        TRY(output.try_append(static_cast<char>(code_point)));
        ++basic_count;
    }
    u64 handled_count = basic_count;
    if (basic_count != 0 && handled_count < input.size())
        TRY(output.try_append('-'));

    u64 n = initial_n;
    u64 delta = 0;
    u64 bias = initial_bias;
    while (handled_count < input.size()) {
        u64 next_minimum = maximum + 1;
        for (auto code_point : input) {
            if (code_point >= n && code_point < next_minimum)
                next_minimum = code_point;
        }
        if (next_minimum > maximum || (next_minimum - n) > (maximum - delta) / (handled_count + 1))
            return false;
        delta += (next_minimum - n) * (handled_count + 1);
        n = next_minimum;

        for (auto code_point : input) {
            if (code_point < n) {
                if (delta == maximum)
                    return false;
                ++delta;
            } else if (code_point == n) {
                u64 quotient = delta;
                for (u64 k = base;; k += base) {
                    auto const threshold = k <= bias ? tmin : (k >= bias + tmax ? tmax : k - bias);
                    if (quotient < threshold)
                        break;
                    auto const digit = threshold + (quotient - threshold) % (base - threshold);
                    TRY(output.try_append(punycode_digit(static_cast<u32>(digit))));
                    quotient = (quotient - threshold) / (base - threshold);
                }
                TRY(output.try_append(punycode_digit(static_cast<u32>(quotient))));
                bias = adapt_punycode_bias(delta, handled_count + 1, handled_count == basic_count);
                if (bias > maximum)
                    return false;
                delta = 0;
                ++handled_count;
            }
        }
        if (delta == maximum || n == maximum)
            return false;
        ++delta;
        ++n;
    }
    return true;
}

static ErrorOr<bool> decode_punycode(Vector<u32> const& label, size_t payload_start, Vector<u32>& output)
{
    constexpr u64 base = 36;
    constexpr u64 tmin = 1;
    constexpr u64 tmax = 26;
    constexpr u64 initial_n = 128;
    constexpr u64 initial_bias = 72;
    constexpr u64 maximum = std::numeric_limits<u32>::max();

    size_t delimiter = label.size();
    for (size_t index = payload_start; index < label.size(); ++index) {
        if (label[index] == '-')
            delimiter = index;
    }
    size_t index = payload_start;
    if (delimiter != label.size()) {
        for (; index < delimiter; ++index) {
            if (label[index] >= 0x80)
                return false;
            TRY(output.try_append(label[index]));
        }
        index = delimiter + 1;
    }

    u64 n = initial_n;
    u64 insertion_index = 0;
    u64 bias = initial_bias;
    while (index < label.size()) {
        auto const old_insertion_index = insertion_index;
        u64 weight = 1;
        for (u64 k = base;; k += base) {
            if (index >= label.size())
                return false;
            u32 digit = 0;
            if (!decode_punycode_digit(label[index++], digit) || digit > (maximum - insertion_index) / weight)
                return false;
            insertion_index += digit * weight;
            auto const threshold = k <= bias ? tmin : (k >= bias + tmax ? tmax : k - bias);
            if (digit < threshold)
                break;
            if (weight > maximum / (base - threshold))
                return false;
            weight *= base - threshold;
        }

        bias = adapt_punycode_bias(insertion_index - old_insertion_index, output.size() + 1, old_insertion_index == 0);
        n += insertion_index / (output.size() + 1);
        insertion_index %= output.size() + 1;
        if (n > 0x10FFFF || (n >= 0xD800 && n <= 0xDFFF) || bias > maximum)
            return false;
        TRY(output.try_insert(insertion_index, static_cast<u32>(n)));
        ++insertion_index;
    }
    return true;
}

static ErrorOr<String> label_to_ascii(Vector<u32> const& label, ToAsciiOptions const& options)
{
    StringBuilder output;
    if (label.size() >= 4 && label[0] == 'x' && label[1] == 'n' && label[2] == '-' && label[3] == '-') {
        auto invalid_punycode = [&]() -> ErrorOr<String> {
            if (options.ignore_invalid_punycode == IgnoreInvalidPunycode::No)
                return idna_error();
            for (auto code_point : label)
                TRY(output.try_append_code_point(code_point));
            return output.to_string();
        };

        Vector<u32> decoded;
        auto const did_decode = TRY(decode_punycode(label, 4, decoded));
        if (!did_decode || decoded.is_empty() || !std::any_of(decoded.begin(), decoded.end(), [](u32 code_point) { return code_point >= 0x80; })) {
            return invalid_punycode();
        }

        auto normalized = TRY(normalize_nfc(decoded));
        if (!equal_code_points(decoded, normalized) || !valid_label(decoded, options))
            return invalid_punycode();

        StringBuilder canonical_label;
        TRY(canonical_label.try_append("xn--"sv));
        if (!TRY(encode_punycode(decoded, canonical_label)))
            return invalid_punycode();
        auto canonical = TRY(canonical_label.to_string());
        if (canonical.bytes().size() != label.size())
            return invalid_punycode();
        for (size_t index = 0; index < label.size(); ++index) {
            if (label[index] != static_cast<u8>(canonical.bytes()[index]))
                return invalid_punycode();
        }

        for (auto code_point : label)
            TRY(output.try_append_code_point(code_point));
        return output.to_string();
    }

    auto normalized = TRY(normalize_nfc(label));
    if (!valid_label(normalized, options))
        return idna_error();

    bool has_non_ascii = false;
    for (auto code_point : normalized)
        has_non_ascii |= code_point >= 0x80;
    if (!has_non_ascii) {
        for (auto code_point : normalized)
            TRY(output.try_append_code_point(code_point));
        return output.to_string();
    }

    TRY(output.try_append("xn--"sv));
    if (!TRY(encode_punycode(normalized, output)))
        return idna_error();
    return output.to_string();
}

} // namespace

// https://www.unicode.org/reports/tr46/#ToASCII
ErrorOr<String> to_ascii(Utf8View domain_name, ToAsciiOptions const& options)
{
    if (!domain_name.validate())
        return idna_error();

    Vector<u32> mapped;
    mapped.ensure_capacity(domain_name.length());
    for (auto code_point : domain_name) {
        if (!TRY(map_code_point(code_point, options, mapped)))
            return idna_error();
    }

    if (mapped.is_empty()) {
        if (options.verify_dns_length == VerifyDnsLength::Yes)
            return idna_error();
        return String::from_utf8(""sv);
    }
    if (mapped.size() == 1 && mapped[0] == '.')
        return String::from_utf8("."sv);

    StringBuilder output;
    bool const trailing_dot = mapped.last() == '.';
    size_t label_start = 0;
    for (size_t index = 0; index <= mapped.size(); ++index) {
        if (index < mapped.size() && mapped[index] != '.')
            continue;

        Vector<u32> label;
        label.ensure_capacity(index - label_start);
        for (size_t label_index = label_start; label_index < index; ++label_index)
            label.append(mapped[label_index]);

        if (label.is_empty()) {
            auto const final_root_label = index == mapped.size() && trailing_dot;
            if (options.verify_dns_length == VerifyDnsLength::Yes && !final_root_label)
                return idna_error();
        } else {
            auto ascii_label = TRY(label_to_ascii(label, options));
            if (options.verify_dns_length == VerifyDnsLength::Yes && ascii_label.bytes().size() > 63)
                return idna_error();
            TRY(output.try_append(ascii_label.bytes_as_string_view()));
        }

        if (index < mapped.size())
            TRY(output.try_append('.'));
        label_start = index + 1;
    }

    if (options.verify_dns_length == VerifyDnsLength::Yes) {
        auto const maximum_name_length = trailing_dot ? 254u : 253u;
        if (output.length() > maximum_name_length)
            return idna_error();
    }
    return output.to_string();
}

#else

// https://www.unicode.org/reports/tr46/#ToASCII
ErrorOr<String> to_ascii(Utf8View domain_name, ToAsciiOptions const& options)
{
    u32 icu_options = 0;

    if (options.check_bidi == CheckBidi::Yes)
        icu_options |= UIDNA_CHECK_BIDI;
    if (options.check_joiners == CheckJoiners::Yes)
        icu_options |= UIDNA_CHECK_CONTEXTJ;
    if (options.use_std3_ascii_rules == UseStd3AsciiRules::Yes)
        icu_options |= UIDNA_USE_STD3_RULES;
    if (options.transitional_processing == TransitionalProcessing::No)
        icu_options |= UIDNA_NONTRANSITIONAL_TO_ASCII | UIDNA_NONTRANSITIONAL_TO_UNICODE;

    UErrorCode status = U_ZERO_ERROR;

    auto idna = adopt_own_if_nonnull(icu::IDNA::createUTS46Instance(icu_options, status));
    if (icu_failure(status))
        return Error::from_string_literal("Unable to create an IDNA instance");

    StringBuilder builder { domain_name.as_string().length() };
    icu::StringByteSink sink { &builder };

    icu::IDNAInfo info;
    idna->nameToASCII_UTF8(icu_string_piece(domain_name.as_string()), sink, info, status);

    auto errors = info.getErrors();

    if (options.check_hyphens == CheckHyphens::No) {
        errors &= ~UIDNA_ERROR_HYPHEN_3_4;
        errors &= ~UIDNA_ERROR_LEADING_HYPHEN;
        errors &= ~UIDNA_ERROR_TRAILING_HYPHEN;
    }

    if (options.verify_dns_length == VerifyDnsLength::No) {
        errors &= ~UIDNA_ERROR_EMPTY_LABEL;
        errors &= ~UIDNA_ERROR_LABEL_TOO_LONG;
        errors &= ~UIDNA_ERROR_DOMAIN_NAME_TOO_LONG;
    }

    if (options.ignore_invalid_punycode == IgnoreInvalidPunycode::Yes) {
        errors &= ~UIDNA_ERROR_PUNYCODE;
    }

    if (icu_failure(status) || errors != 0)
        return Error::from_string_literal("Unable to convert domain to ASCII");

    return builder.to_string();
}

#endif // AK_OS_RINOS

}
