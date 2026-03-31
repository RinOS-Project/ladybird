/*
 * Copyright (c) 2022, David Tuin <davidot@serenityos.org>
 * Copyright (c) 2025-2026, Tim Flynn <trflynn89@ladybird.org>
 *
 * SPDX-License-Identifier: BSD-2-Clause
 */

#include <AK/FloatingPoint.h>
#include <AK/NumericLimits.h>
#include <AK/StringConversions.h>
#include <AK/StringView.h>
#include <AK/Utf16View.h>
#include <float.h>
#include <math.h>
#include <stdlib.h>

#if !defined(AK_OS_RINOS)
#    include <fast_float/fast_float.h>
#    include <fmt/format.h>
#endif

namespace AK {

#define ENUMERATE_INTEGRAL_TYPES    \
    __ENUMERATE_TYPE(i8)            \
    __ENUMERATE_TYPE(i16)           \
    __ENUMERATE_TYPE(i32)           \
    __ENUMERATE_TYPE(long)          \
    __ENUMERATE_TYPE(long long)     \
    __ENUMERATE_TYPE(u8)            \
    __ENUMERATE_TYPE(u16)           \
    __ENUMERATE_TYPE(u32)           \
    __ENUMERATE_TYPE(unsigned long) \
    __ENUMERATE_TYPE(unsigned long long)

#define ENUMERATE_ARITHMETIC_TYPES \
    ENUMERATE_INTEGRAL_TYPES       \
    __ENUMERATE_TYPE(float)        \
    __ENUMERATE_TYPE(double)

template<typename CharType, Arithmetic ValueType>
static constexpr Optional<ParseFirstNumberResult<ValueType>> from_chars(CharType const* string, size_t length, int base)
{
    ValueType value { 0 };

#if defined(AK_OS_RINOS)
    if constexpr (IsSame<CharType, char16_t>) {
        auto bytes = Utf16View { string, length }.to_byte_string();
        if (bytes.is_error())
            return {};
        return from_chars<char, ValueType>(bytes.value().characters(), bytes.value().length(), base);
    } else {
        ByteString buffer { StringView { string, length } };
        char* endptr = nullptr;

        if constexpr (IsFloatingPoint<ValueType>) {
            if (base != 10)
                return {};

            if constexpr (IsSame<ValueType, float>)
                value = strtof(buffer.characters(), &endptr);
            else
                value = strtod(buffer.characters(), &endptr);
        } else if constexpr (IsSigned<ValueType>) {
            auto parsed = strtoll(buffer.characters(), &endptr, base);
            if (parsed < NumericLimits<ValueType>::min() || parsed > NumericLimits<ValueType>::max())
                return {};
            value = static_cast<ValueType>(parsed);
        } else {
            auto parsed = strtoull(buffer.characters(), &endptr, base);
            if (parsed > NumericLimits<ValueType>::max())
                return {};
            value = static_cast<ValueType>(parsed);
        }

        if (!endptr || endptr == buffer.characters())
            return {};

        return ParseFirstNumberResult<ValueType> {
            value,
            static_cast<size_t>(endptr - buffer.characters())
        };
    }
#else
    fast_float::parse_options_t<CharType> options;
    options.base = base;
    options.format |= fast_float::chars_format::no_infnan;

    if constexpr (IsSigned<ValueType> || IsFloatingPoint<ValueType>) {
        options.format |= fast_float::chars_format::allow_leading_plus;
    }

    auto result = fast_float::from_chars_advanced(string, string + length, value, options);

    if constexpr (IsFloatingPoint<ValueType>) {
        if (result.ec == std::errc::result_out_of_range && (__builtin_isinf(value) || value == 0))
            result.ec = {};
    }

    if (result.ec != std::errc {})
        return {};

    return ParseFirstNumberResult { value, static_cast<size_t>(result.ptr - string) };
#endif
}

template<Arithmetic T>
Optional<ParseFirstNumberResult<T>> parse_first_number(StringView string, TrimWhitespace trim_whitespace, int base)
{
    if (trim_whitespace == TrimWhitespace::Yes)
        string = StringUtils::trim_whitespace(string, TrimMode::Both);

    return from_chars<char, T>(string.characters_without_null_termination(), string.length(), base);
}

template<Arithmetic T>
Optional<ParseFirstNumberResult<T>> parse_first_number(Utf16View const& string, TrimWhitespace trim_whitespace, int base)
{
    if (string.has_ascii_storage())
        return parse_first_number<T>(string.bytes(), trim_whitespace, base);

    auto trimmed_string = trim_whitespace == TrimWhitespace::Yes ? string.trim_ascii_whitespace() : string;
    return from_chars<char16_t, T>(trimmed_string.utf16_span().data(), trimmed_string.length_in_code_units(), base);
}

#define __ENUMERATE_TYPE(type) \
    template Optional<ParseFirstNumberResult<type>> parse_first_number(StringView, TrimWhitespace, int);
ENUMERATE_ARITHMETIC_TYPES
#undef __ENUMERATE_TYPE

#define __ENUMERATE_TYPE(type) \
    template Optional<ParseFirstNumberResult<type>> parse_first_number(Utf16View const&, TrimWhitespace, int);
ENUMERATE_ARITHMETIC_TYPES
#undef __ENUMERATE_TYPE

template<Arithmetic T>
Optional<T> parse_number(StringView string, TrimWhitespace trim_whitespace, int base)
{
    if (trim_whitespace == TrimWhitespace::Yes)
        string = StringUtils::trim_whitespace(string, TrimMode::Both);

    auto result = parse_first_number<T>(string, TrimWhitespace::No, base);
    if (!result.has_value())
        return {};

    if (result->characters_parsed != string.length())
        return {};

    return result->value;
}

template<Arithmetic T>
Optional<T> parse_number(Utf16View const& string, TrimWhitespace trim_whitespace, int base)
{
    if (string.has_ascii_storage())
        return parse_number<T>(string.bytes(), trim_whitespace, base);

    auto trimmed_string = trim_whitespace == TrimWhitespace::Yes ? string.trim_ascii_whitespace() : string;

    auto result = parse_first_number<T>(trimmed_string, TrimWhitespace::No, base);
    if (!result.has_value())
        return {};

    if (result->characters_parsed != trimmed_string.length_in_code_units())
        return {};

    return result->value;
}

#define __ENUMERATE_TYPE(type) \
    template Optional<type> parse_number(StringView, TrimWhitespace, int);
ENUMERATE_ARITHMETIC_TYPES
#undef __ENUMERATE_TYPE

#define __ENUMERATE_TYPE(type) \
    template Optional<type> parse_number(Utf16View const&, TrimWhitespace, int);
ENUMERATE_ARITHMETIC_TYPES
#undef __ENUMERATE_TYPE

template<Integral T>
Optional<T> parse_hexadecimal_number(StringView string, TrimWhitespace trim_whitespace)
{
    return parse_number<T>(string, trim_whitespace, 16);
}

template<Integral T>
Optional<T> parse_hexadecimal_number(Utf16View const& string, TrimWhitespace trim_whitespace)
{
    return parse_number<T>(string, trim_whitespace, 16);
}

#define __ENUMERATE_TYPE(type) \
    template Optional<type> parse_hexadecimal_number(StringView, TrimWhitespace);
ENUMERATE_INTEGRAL_TYPES
#undef __ENUMERATE_TYPE

#define __ENUMERATE_TYPE(type) \
    template Optional<type> parse_hexadecimal_number(Utf16View const&, TrimWhitespace);
ENUMERATE_INTEGRAL_TYPES
#undef __ENUMERATE_TYPE

template<FloatingPoint T>
DecimalExponentialForm convert_to_decimal_exponential_form(T value)
{
    ASSERT(!isinf(value));
    ASSERT(!isnan(value));

    FloatExtractor<T> extractor;
    extractor.d = value;

#if defined(AK_OS_RINOS)
    constexpr int precision = IsSame<T, float> ? (FLT_DECIMAL_DIG - 1) : (DBL_DECIMAL_DIG - 1);
    char buffer[64] {};
    auto length = snprintf(buffer, sizeof(buffer), "%.*e", precision, static_cast<double>(value));
    VERIFY(length > 0);

    bool sign = buffer[0] == '-';
    auto const* cursor = buffer + (sign ? 1 : 0);
    auto const* exponent_marker = __builtin_strchr(cursor, 'e');
    VERIFY(exponent_marker);

    u64 significand = 0;
    i32 decimal_digits = 0;
    bool after_decimal = false;
    for (auto const* p = cursor; p < exponent_marker; ++p) {
        if (*p == '.') {
            after_decimal = true;
            continue;
        }
        significand = significand * 10 + static_cast<u64>(*p - '0');
        if (after_decimal)
            ++decimal_digits;
    }

    auto exponent = static_cast<i32>(strtol(exponent_marker + 1, nullptr, 10)) - decimal_digits;
    while (significand != 0 && significand % 10 == 0) {
        significand /= 10;
        ++exponent;
    }

    return { sign, significand, exponent };
#else
    auto [significand, exponent] = fmt::detail::dragonbox::to_decimal(value);
    return { static_cast<bool>(extractor.sign), significand, exponent };
#endif
}

template DecimalExponentialForm convert_to_decimal_exponential_form(float);
template DecimalExponentialForm convert_to_decimal_exponential_form(double);

}
