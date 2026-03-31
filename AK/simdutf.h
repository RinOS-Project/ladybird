/*
 * RinOS simdutf compatibility layer.
 *
 * This is a narrow, header-only subset that provides the API surface used by
 * Ladybird's AK string/base64 helpers while avoiding the external simdutf
 * dependency in RinOS builds.
 */

#pragma once

#include <cstddef>
#include <cstdint>
#include <cstring>

namespace simdutf {

enum error_code {
    SUCCESS = 0,
    SURROGATE,
    TOO_SHORT,
    TOO_LONG,
    OVERLONG,
    HEADER_BITS,
    INVALID_UTF8,
    INVALID_UTF16,
    INVALID_UTF32,
    INVALID_BASE64_CHARACTER,
    BASE64_INPUT_REMAINDER,
    BASE64_EXTRA_BITS,
    OUTPUT_BUFFER_TOO_SMALL,
};

struct result {
    error_code error { SUCCESS };
    size_t count { 0 };
};

enum last_chunk_handling_options {
    loose,
    strict,
    stop_before_partial,
};

enum base64_options {
    base64_default = 0,
    base64_url = 1,
    base64_default_no_padding = 2,
    base64_url_with_padding = 3,
};

namespace detail {

static inline bool host_is_little_endian()
{
    union {
        uint16_t value;
        uint8_t bytes[2];
    } probe { 0x0100 };
    return probe.bytes[1] == 0x01;
}

static inline uint16_t byte_swap16(uint16_t value)
{
    return static_cast<uint16_t>((value >> 8) | (value << 8));
}

static inline char16_t from_le(char16_t value)
{
    if (host_is_little_endian())
        return value;
    return static_cast<char16_t>(byte_swap16(static_cast<uint16_t>(value)));
}

static inline char16_t from_be(char16_t value)
{
    if (host_is_little_endian())
        return static_cast<char16_t>(byte_swap16(static_cast<uint16_t>(value)));
    return value;
}

static inline bool is_high_surrogate(uint32_t value)
{
    return value >= 0xD800u && value <= 0xDBFFu;
}

static inline bool is_low_surrogate(uint32_t value)
{
    return value >= 0xDC00u && value <= 0xDFFFu;
}

static inline bool is_scalar_value(uint32_t value)
{
    return value <= 0x10FFFFu && !is_high_surrogate(value) && !is_low_surrogate(value);
}

static inline size_t utf8_length_for_scalar(uint32_t code_point)
{
    if (code_point <= 0x7Fu)
        return 1;
    if (code_point <= 0x7FFu)
        return 2;
    if (code_point <= 0xFFFFu)
        return 3;
    return 4;
}

static inline size_t encode_scalar_to_utf8(uint32_t code_point, char* output)
{
    if (code_point <= 0x7Fu) {
        output[0] = static_cast<char>(code_point);
        return 1;
    }
    if (code_point <= 0x7FFu) {
        output[0] = static_cast<char>(0xC0u | (code_point >> 6));
        output[1] = static_cast<char>(0x80u | (code_point & 0x3Fu));
        return 2;
    }
    if (code_point <= 0xFFFFu) {
        output[0] = static_cast<char>(0xE0u | (code_point >> 12));
        output[1] = static_cast<char>(0x80u | ((code_point >> 6) & 0x3Fu));
        output[2] = static_cast<char>(0x80u | (code_point & 0x3Fu));
        return 3;
    }
    output[0] = static_cast<char>(0xF0u | (code_point >> 18));
    output[1] = static_cast<char>(0x80u | ((code_point >> 12) & 0x3Fu));
    output[2] = static_cast<char>(0x80u | ((code_point >> 6) & 0x3Fu));
    output[3] = static_cast<char>(0x80u | (code_point & 0x3Fu));
    return 4;
}

static inline bool decode_utf8_one(char const* input, size_t length, uint32_t& code_point, size_t& consumed, error_code& error)
{
    if (length == 0) {
        error = TOO_SHORT;
        consumed = 0;
        return false;
    }

    auto b0 = static_cast<uint8_t>(input[0]);
    if (b0 <= 0x7F) {
        code_point = b0;
        consumed = 1;
        error = SUCCESS;
        return true;
    }

    uint32_t min_value = 0;
    size_t total = 0;
    uint32_t value = 0;

    if ((b0 & 0xE0u) == 0xC0u) {
        total = 2;
        value = b0 & 0x1Fu;
        min_value = 0x80u;
    } else if ((b0 & 0xF0u) == 0xE0u) {
        total = 3;
        value = b0 & 0x0Fu;
        min_value = 0x800u;
    } else if ((b0 & 0xF8u) == 0xF0u) {
        total = 4;
        value = b0 & 0x07u;
        min_value = 0x10000u;
    } else {
        error = HEADER_BITS;
        consumed = 0;
        return false;
    }

    if (length < total) {
        error = TOO_SHORT;
        consumed = 0;
        return false;
    }

    for (size_t i = 1; i < total; ++i) {
        auto byte = static_cast<uint8_t>(input[i]);
        if ((byte & 0xC0u) != 0x80u) {
            error = INVALID_UTF8;
            consumed = 0;
            return false;
        }
        value = (value << 6) | (byte & 0x3Fu);
    }

    if (value < min_value) {
        error = OVERLONG;
        consumed = 0;
        return false;
    }
    if (value > 0x10FFFFu) {
        error = TOO_LONG;
        consumed = 0;
        return false;
    }
    if (is_high_surrogate(value) || is_low_surrogate(value)) {
        error = SURROGATE;
        consumed = total;
        code_point = value;
        return false;
    }

    code_point = value;
    consumed = total;
    error = SUCCESS;
    return true;
}

static inline bool decode_utf16_one(char16_t const* input, size_t length, uint32_t& code_point, size_t& consumed)
{
    if (length == 0) {
        consumed = 0;
        return false;
    }

    uint32_t first = input[0];
    if (!is_high_surrogate(first) && !is_low_surrogate(first)) {
        code_point = first;
        consumed = 1;
        return true;
    }

    if (is_low_surrogate(first)) {
        consumed = 1;
        return false;
    }

    if (length < 2 || !is_low_surrogate(input[1])) {
        consumed = 1;
        return false;
    }

    code_point = 0x10000u + (((first - 0xD800u) << 10) | (static_cast<uint32_t>(input[1]) - 0xDC00u));
    consumed = 2;
    return true;
}

static inline bool decode_utf16_one_endian(char16_t const* input, size_t length, bool big_endian, uint32_t& code_point, size_t& consumed)
{
    if (length == 0) {
        consumed = 0;
        return false;
    }

    auto first = big_endian ? from_be(input[0]) : from_le(input[0]);
    if (!is_high_surrogate(first) && !is_low_surrogate(first)) {
        code_point = first;
        consumed = 1;
        return true;
    }

    if (is_low_surrogate(first)) {
        consumed = 1;
        return false;
    }

    if (length < 2) {
        consumed = 1;
        return false;
    }

    auto second = big_endian ? from_be(input[1]) : from_le(input[1]);
    if (!is_low_surrogate(second)) {
        consumed = 1;
        return false;
    }

    code_point = 0x10000u + (((static_cast<uint32_t>(first) - 0xD800u) << 10) | (static_cast<uint32_t>(second) - 0xDC00u));
    consumed = 2;
    return true;
}

static inline size_t encode_scalar_to_utf16(uint32_t code_point, char16_t* output)
{
    if (code_point <= 0xFFFFu) {
        output[0] = static_cast<char16_t>(code_point);
        return 1;
    }

    code_point -= 0x10000u;
    output[0] = static_cast<char16_t>(0xD800u | (code_point >> 10));
    output[1] = static_cast<char16_t>(0xDC00u | (code_point & 0x3FFu));
    return 2;
}

static inline size_t utf16_length_for_scalar(uint32_t code_point)
{
    return code_point <= 0xFFFFu ? 1 : 2;
}

static inline bool is_url_base64(base64_options options)
{
    return options == base64_url || options == base64_url_with_padding;
}

static inline bool omit_padding(base64_options options)
{
    return options == base64_default_no_padding || options == base64_url;
}

static inline char const* base64_alphabet(base64_options options)
{
    return is_url_base64(options)
        ? "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789-_"
        : "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
}

static inline int decode_base64_character(char ch, base64_options options)
{
    if (ch >= 'A' && ch <= 'Z')
        return ch - 'A';
    if (ch >= 'a' && ch <= 'z')
        return ch - 'a' + 26;
    if (ch >= '0' && ch <= '9')
        return ch - '0' + 52;
    if (ch == (is_url_base64(options) ? '-' : '+'))
        return 62;
    if (ch == (is_url_base64(options) ? '_' : '/'))
        return 63;
    return -1;
}

} // namespace detail

inline bool validate_ascii(char const* input, size_t length)
{
    for (size_t i = 0; i < length; ++i) {
        if (static_cast<unsigned char>(input[i]) > 0x7Fu)
            return false;
    }
    return true;
}

template<typename T>
inline T const* find(T const* start, T const* end, T needle)
{
    for (auto const* ptr = start; ptr != end; ++ptr) {
        if (*ptr == needle)
            return ptr;
    }
    return end;
}

inline size_t count_utf8(char const* input, size_t length)
{
    size_t count = 0;
    size_t offset = 0;
    while (offset < length) {
        uint32_t code_point = 0;
        size_t consumed = 0;
        error_code error = SUCCESS;
        if (!detail::decode_utf8_one(input + offset, length - offset, code_point, consumed, error)) {
            if (error == SURROGATE && consumed != 0)
                offset += consumed;
            else
                ++offset;
            ++count;
            continue;
        }
        offset += consumed;
        ++count;
    }
    return count;
}

inline result validate_utf8_with_errors(char const* input, size_t length)
{
    size_t offset = 0;
    while (offset < length) {
        uint32_t code_point = 0;
        size_t consumed = 0;
        error_code error = SUCCESS;
        if (!detail::decode_utf8_one(input + offset, length - offset, code_point, consumed, error))
            return { error, offset };
        offset += consumed;
    }
    return { SUCCESS, length };
}

inline bool validate_utf32(char32_t const* input, size_t length)
{
    for (size_t i = 0; i < length; ++i) {
        if (!detail::is_scalar_value(static_cast<uint32_t>(input[i])))
            return false;
    }
    return true;
}

inline size_t utf16_length_from_utf32(char32_t const* input, size_t length)
{
    size_t result_length = 0;
    for (size_t i = 0; i < length; ++i) {
        auto code_point = static_cast<uint32_t>(input[i]);
        if (!detail::is_scalar_value(code_point))
            code_point = 0xFFFDu;
        result_length += detail::utf16_length_for_scalar(code_point);
    }
    return result_length;
}

inline size_t convert_utf32_to_utf16(char32_t const* input, size_t length, char16_t* output)
{
    size_t written = 0;
    for (size_t i = 0; i < length; ++i) {
        auto code_point = static_cast<uint32_t>(input[i]);
        if (!detail::is_scalar_value(code_point))
            code_point = 0xFFFDu;
        written += detail::encode_scalar_to_utf16(code_point, output + written);
    }
    return written;
}

inline size_t convert_utf32_to_utf8(char32_t const* input, size_t length, char* output)
{
    size_t written = 0;
    for (size_t i = 0; i < length; ++i) {
        auto code_point = static_cast<uint32_t>(input[i]);
        if (!detail::is_scalar_value(code_point))
            code_point = 0xFFFDu;
        written += detail::encode_scalar_to_utf8(code_point, output + written);
    }
    return written;
}

inline size_t utf16_length_from_utf8(char const* input, size_t length)
{
    size_t result_length = 0;
    size_t offset = 0;
    while (offset < length) {
        uint32_t code_point = 0;
        size_t consumed = 0;
        error_code error = SUCCESS;
        if (!detail::decode_utf8_one(input + offset, length - offset, code_point, consumed, error)) {
            result_length += 1;
            offset += (error == SURROGATE && consumed != 0) ? consumed : 1;
            continue;
        }
        result_length += detail::utf16_length_for_scalar(code_point);
        offset += consumed;
    }
    return result_length;
}

inline size_t convert_utf8_to_utf16(char const* input, size_t length, char16_t* output)
{
    size_t written = 0;
    size_t offset = 0;
    while (offset < length) {
        uint32_t code_point = 0;
        size_t consumed = 0;
        error_code error = SUCCESS;
        if (!detail::decode_utf8_one(input + offset, length - offset, code_point, consumed, error)) {
            output[written++] = 0xFFFDu;
            offset += (error == SURROGATE && consumed != 0) ? consumed : 1;
            continue;
        }
        written += detail::encode_scalar_to_utf16(code_point, output + written);
        offset += consumed;
    }
    return written;
}

inline bool validate_utf16(char16_t const* input, size_t length)
{
    size_t offset = 0;
    while (offset < length) {
        uint32_t code_point = 0;
        size_t consumed = 0;
        if (!detail::decode_utf16_one(input + offset, length - offset, code_point, consumed))
            return false;
        offset += consumed;
    }
    return true;
}

inline bool validate_utf16le(char16_t const* input, size_t length)
{
    size_t offset = 0;
    while (offset < length) {
        uint32_t code_point = 0;
        size_t consumed = 0;
        if (!detail::decode_utf16_one_endian(input + offset, length - offset, false, code_point, consumed))
            return false;
        offset += consumed;
    }
    return true;
}

inline bool validate_utf16be(char16_t const* input, size_t length)
{
    size_t offset = 0;
    while (offset < length) {
        uint32_t code_point = 0;
        size_t consumed = 0;
        if (!detail::decode_utf16_one_endian(input + offset, length - offset, true, code_point, consumed))
            return false;
        offset += consumed;
    }
    return true;
}

inline bool validate_utf16_as_ascii(char16_t const* input, size_t length)
{
    for (size_t i = 0; i < length; ++i) {
        if (static_cast<uint16_t>(input[i]) > 0x7Fu)
            return false;
    }
    return true;
}

inline result validate_utf16_with_errors(char16_t const* input, size_t length)
{
    size_t offset = 0;
    while (offset < length) {
        uint32_t code_point = 0;
        size_t consumed = 0;
        if (!detail::decode_utf16_one(input + offset, length - offset, code_point, consumed))
            return { INVALID_UTF16, offset };
        offset += consumed;
    }
    return { SUCCESS, length };
}

inline size_t count_utf16(char16_t const* input, size_t length)
{
    size_t count = 0;
    size_t offset = 0;
    while (offset < length) {
        uint32_t code_point = 0;
        size_t consumed = 0;
        if (!detail::decode_utf16_one(input + offset, length - offset, code_point, consumed))
            consumed = 1;
        offset += consumed;
        ++count;
    }
    return count;
}

inline size_t utf8_length_from_utf16(char16_t const* input, size_t length)
{
    size_t utf8_length = 0;
    size_t offset = 0;
    while (offset < length) {
        uint32_t code_point = 0;
        size_t consumed = 0;
        if (!detail::decode_utf16_one(input + offset, length - offset, code_point, consumed)) {
            code_point = static_cast<uint16_t>(input[offset]);
            consumed = 1;
        }
        utf8_length += detail::utf8_length_for_scalar(code_point);
        offset += consumed;
    }
    return utf8_length;
}

inline size_t utf8_length_from_utf16le(char16_t const* input, size_t length)
{
    size_t utf8_length = 0;
    size_t offset = 0;
    while (offset < length) {
        uint32_t code_point = 0;
        size_t consumed = 0;
        if (!detail::decode_utf16_one_endian(input + offset, length - offset, false, code_point, consumed)) {
            code_point = static_cast<uint16_t>(detail::from_le(input[offset]));
            consumed = 1;
        }
        utf8_length += detail::utf8_length_for_scalar(code_point);
        offset += consumed;
    }
    return utf8_length;
}

inline size_t utf8_length_from_utf16be(char16_t const* input, size_t length)
{
    size_t utf8_length = 0;
    size_t offset = 0;
    while (offset < length) {
        uint32_t code_point = 0;
        size_t consumed = 0;
        if (!detail::decode_utf16_one_endian(input + offset, length - offset, true, code_point, consumed)) {
            code_point = static_cast<uint16_t>(detail::from_be(input[offset]));
            consumed = 1;
        }
        utf8_length += detail::utf8_length_for_scalar(code_point);
        offset += consumed;
    }
    return utf8_length;
}

inline size_t convert_utf16_to_utf8(char16_t const* input, size_t length, char* output)
{
    size_t written = 0;
    size_t offset = 0;
    while (offset < length) {
        uint32_t code_point = 0;
        size_t consumed = 0;
        if (!detail::decode_utf16_one(input + offset, length - offset, code_point, consumed)) {
            code_point = static_cast<uint16_t>(input[offset]);
            consumed = 1;
        }
        written += detail::encode_scalar_to_utf8(code_point, output + written);
        offset += consumed;
    }
    return written;
}

inline size_t convert_utf16le_to_utf8(char16_t const* input, size_t length, char* output)
{
    size_t written = 0;
    size_t offset = 0;
    while (offset < length) {
        uint32_t code_point = 0;
        size_t consumed = 0;
        if (!detail::decode_utf16_one_endian(input + offset, length - offset, false, code_point, consumed)) {
            code_point = static_cast<uint16_t>(detail::from_le(input[offset]));
            consumed = 1;
        }
        written += detail::encode_scalar_to_utf8(code_point, output + written);
        offset += consumed;
    }
    return written;
}

inline size_t convert_utf16be_to_utf8(char16_t const* input, size_t length, char* output)
{
    size_t written = 0;
    size_t offset = 0;
    while (offset < length) {
        uint32_t code_point = 0;
        size_t consumed = 0;
        if (!detail::decode_utf16_one_endian(input + offset, length - offset, true, code_point, consumed)) {
            code_point = static_cast<uint16_t>(detail::from_be(input[offset]));
            consumed = 1;
        }
        written += detail::encode_scalar_to_utf8(code_point, output + written);
        offset += consumed;
    }
    return written;
}

inline result convert_utf16_to_utf8_with_errors(char16_t const* input, size_t length, char* output)
{
    size_t written = 0;
    size_t offset = 0;
    while (offset < length) {
        uint32_t code_point = 0;
        size_t consumed = 0;
        if (!detail::decode_utf16_one(input + offset, length - offset, code_point, consumed))
            return { INVALID_UTF16, offset };
        written += detail::encode_scalar_to_utf8(code_point, output + written);
        offset += consumed;
    }
    return { SUCCESS, written };
}

inline void to_well_formed_utf16(char16_t const* input, size_t length, char16_t* output)
{
    size_t out = 0;
    size_t offset = 0;
    while (offset < length) {
        uint32_t code_point = 0;
        size_t consumed = 0;
        if (!detail::decode_utf16_one(input + offset, length - offset, code_point, consumed)) {
            output[out++] = 0xFFFDu;
            ++offset;
            continue;
        }
        if (consumed == 2) {
            output[out++] = input[offset];
            output[out++] = input[offset + 1];
        } else {
            output[out++] = static_cast<char16_t>(code_point);
        }
        offset += consumed;
    }
}

inline void to_well_formed_utf16le(char16_t const* input, size_t length, char16_t* output)
{
    size_t out = 0;
    size_t offset = 0;
    while (offset < length) {
        uint32_t code_point = 0;
        size_t consumed = 0;
        if (!detail::decode_utf16_one_endian(input + offset, length - offset, false, code_point, consumed)) {
            output[out++] = 0xFFFDu;
            ++offset;
            continue;
        }
        out += detail::encode_scalar_to_utf16(code_point, output + out);
        offset += consumed;
    }
}

inline void to_well_formed_utf16be(char16_t const* input, size_t length, char16_t* output)
{
    size_t out = 0;
    size_t offset = 0;
    while (offset < length) {
        uint32_t code_point = 0;
        size_t consumed = 0;
        if (!detail::decode_utf16_one_endian(input + offset, length - offset, true, code_point, consumed)) {
            output[out++] = 0xFFFDu;
            ++offset;
            continue;
        }
        out += detail::encode_scalar_to_utf16(code_point, output + out);
        offset += consumed;
    }
}

inline size_t maximal_binary_length_from_base64(char const* input, size_t length)
{
    (void)input;
    return ((length + 3) / 4) * 3;
}

inline size_t base64_length_from_binary(size_t length, base64_options options)
{
    size_t groups = ((length + 2) / 3) * 4;
    if (!detail::omit_padding(options))
        return groups;

    size_t remainder = length % 3;
    if (remainder == 0)
        return groups;
    return groups - (3 - remainder);
}

inline void binary_to_base64(char const* input, size_t length, char* output, base64_options options)
{
    auto const* alphabet = detail::base64_alphabet(options);
    bool omit_padding = detail::omit_padding(options);
    size_t out = 0;
    size_t i = 0;
    while (i + 3 <= length) {
        uint32_t chunk = (static_cast<uint8_t>(input[i]) << 16)
            | (static_cast<uint8_t>(input[i + 1]) << 8)
            | static_cast<uint8_t>(input[i + 2]);
        output[out++] = alphabet[(chunk >> 18) & 0x3F];
        output[out++] = alphabet[(chunk >> 12) & 0x3F];
        output[out++] = alphabet[(chunk >> 6) & 0x3F];
        output[out++] = alphabet[chunk & 0x3F];
        i += 3;
    }

    size_t remainder = length - i;
    if (remainder == 1) {
        uint32_t chunk = static_cast<uint8_t>(input[i]) << 16;
        output[out++] = alphabet[(chunk >> 18) & 0x3F];
        output[out++] = alphabet[(chunk >> 12) & 0x3F];
        if (!omit_padding) {
            output[out++] = '=';
            output[out++] = '=';
        }
    } else if (remainder == 2) {
        uint32_t chunk = (static_cast<uint8_t>(input[i]) << 16)
            | (static_cast<uint8_t>(input[i + 1]) << 8);
        output[out++] = alphabet[(chunk >> 18) & 0x3F];
        output[out++] = alphabet[(chunk >> 12) & 0x3F];
        output[out++] = alphabet[(chunk >> 6) & 0x3F];
        if (!omit_padding)
            output[out++] = '=';
    }
}

inline result base64_to_binary_safe(
    char const* input,
    size_t length,
    char* output,
    size_t& output_length,
    base64_options options,
    last_chunk_handling_options last_chunk_handling,
    bool decode_up_to_bad_character)
{
    size_t capacity = output_length;
    size_t written = 0;
    size_t i = 0;
    auto flush_byte = [&](uint8_t byte) {
        if (written < capacity)
            output[written] = static_cast<char>(byte);
        ++written;
    };

    while (i < length) {
        int values[4] { -2, -2, -2, -2 };
        size_t chunk_start = i;
        size_t count = 0;
        size_t padding = 0;

        while (count < 4 && i < length) {
            char ch = input[i++];
            if (ch == '=') {
                values[count++] = -1;
                ++padding;
                continue;
            }

            int value = detail::decode_base64_character(ch, options);
            if (value < 0) {
                if (decode_up_to_bad_character) {
                    output_length = written < capacity ? written : capacity;
                    return { INVALID_BASE64_CHARACTER, i - 1 };
                }
                output_length = written < capacity ? written : capacity;
                return { INVALID_BASE64_CHARACTER, i - 1 };
            }

            if (padding != 0) {
                output_length = written < capacity ? written : capacity;
                return { BASE64_INPUT_REMAINDER, i - 1 };
            }
            values[count++] = value;
        }

        if (count == 0)
            break;

        if (count < 4) {
            if (last_chunk_handling == stop_before_partial) {
                output_length = written < capacity ? written : capacity;
                return { SUCCESS, chunk_start };
            }

            if (count == 1) {
                output_length = written < capacity ? written : capacity;
                return { BASE64_INPUT_REMAINDER, chunk_start };
            }

            if (last_chunk_handling == strict) {
                output_length = written < capacity ? written : capacity;
                return { BASE64_INPUT_REMAINDER, chunk_start };
            }

            while (count < 4)
                values[count++] = -1;
            padding = 4 - (i - chunk_start);
        }

        if (values[0] < 0 || values[1] < 0) {
            output_length = written < capacity ? written : capacity;
            return { BASE64_INPUT_REMAINDER, chunk_start };
        }

        uint32_t sextet0 = static_cast<uint32_t>(values[0]);
        uint32_t sextet1 = static_cast<uint32_t>(values[1]);
        uint32_t sextet2 = values[2] < 0 ? 0u : static_cast<uint32_t>(values[2]);
        uint32_t sextet3 = values[3] < 0 ? 0u : static_cast<uint32_t>(values[3]);
        uint32_t triple = (sextet0 << 18) | (sextet1 << 12) | (sextet2 << 6) | sextet3;

        flush_byte(static_cast<uint8_t>((triple >> 16) & 0xFFu));

        if (values[2] >= 0) {
            flush_byte(static_cast<uint8_t>((triple >> 8) & 0xFFu));
            if (values[3] >= 0) {
                flush_byte(static_cast<uint8_t>(triple & 0xFFu));
            } else if ((triple & 0xFFu) != 0) {
                output_length = written < capacity ? written : capacity;
                return { BASE64_EXTRA_BITS, i };
            }
        } else if (((triple >> 8) & 0xFFu) != 0 || (triple & 0xFFu) != 0) {
            output_length = written < capacity ? written : capacity;
            return { BASE64_EXTRA_BITS, i };
        }

        if (padding != 0)
            break;
    }

    output_length = written < capacity ? written : capacity;
    if (written > capacity)
        return { OUTPUT_BUFFER_TOO_SMALL, length };
    return { SUCCESS, length };
}

} // namespace simdutf
