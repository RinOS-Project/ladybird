/*
 * Copyright (c) 2026, the RinOS developers.
 *
 * SPDX-License-Identifier: BSD-2-Clause
 */

// GenerateLayout.cpp must be compiled with the RinOS target ABI, but its
// resulting build-time helper has to run on the build host. That object only
// needs AK::vout(), so provide the narrow formatter used by its integer-only
// output instead of linking the target AK/libc into a host executable.

#include <cstddef>
#include <cstdint>
#include <cstring>
#include <unistd.h>

struct _FILE;
extern "C" int snprintf(char*, size_t, char const*, ...);
extern "C" int rinos_layout_generator_main() asm("_Z4mainv");

int main()
{
    return rinos_layout_generator_main();
}

namespace AK {

class StringView {
public:
    char const* characters_without_null_termination() const { return m_characters; }
    size_t length() const { return m_length; }

private:
    char const* m_characters;
    size_t m_length;
};

struct TypeErasedParameter {
    enum class Type {
        UnsignedInteger,
        SignedInteger,
        Boolean,
        Character,
        Float,
        Double,
        StringView,
        CString,
        CustomType,
    };

    struct CustomType {
        void const* value;
        void* formatter;
    };

    union Value {
        uint64_t as_unsigned;
        int64_t as_signed;
        bool as_bool;
        char as_char;
        float as_float;
        double as_double;
        StringView as_string_view;
        char const* as_c_string;
        CustomType as_custom_type;
    } value;
    Type type;
};

class TypeErasedFormatParams {
public:
    uint32_t size() const { return m_size; }
    TypeErasedParameter const* parameters() const
    {
        return reinterpret_cast<TypeErasedParameter const*>(reinterpret_cast<unsigned char const*>(this) + 8);
    }

private:
    uint32_t m_size;
    uint32_t m_next_index;
};

static void write_all(char const* data, size_t size)
{
    while (size > 0) {
        auto written = ::write(STDOUT_FILENO, data, size);
        if (written <= 0)
            return;
        data += static_cast<size_t>(written);
        size -= static_cast<size_t>(written);
    }
}

void vout(_FILE*, StringView format, TypeErasedFormatParams& params, bool newline)
{
    auto const* characters = format.characters_without_null_termination();
    auto length = format.length();
    size_t cursor = 0;
    uint32_t parameter_index = 0;

    while (cursor < length) {
        auto const* opening = static_cast<char const*>(memchr(characters + cursor, '{', length - cursor));
        if (!opening) {
            write_all(characters + cursor, length - cursor);
            break;
        }

        auto opening_offset = static_cast<size_t>(opening - characters);
        write_all(characters + cursor, opening_offset - cursor);
        auto const* closing = static_cast<char const*>(memchr(opening, '}', length - opening_offset));
        if (!closing || parameter_index >= params.size()) {
            write_all(opening, length - opening_offset);
            break;
        }

        bool hexadecimal = static_cast<size_t>(closing - opening) >= 2 && closing[-1] == 'X';
        auto const& parameter = params.parameters()[parameter_index++];
        char buffer[64];
        int formatted_length = 0;
        switch (parameter.type) {
        case TypeErasedParameter::Type::UnsignedInteger:
            formatted_length = hexadecimal
                ? snprintf(buffer, sizeof(buffer), "%llX", static_cast<unsigned long long>(parameter.value.as_unsigned))
                : snprintf(buffer, sizeof(buffer), "%llu", static_cast<unsigned long long>(parameter.value.as_unsigned));
            break;
        case TypeErasedParameter::Type::SignedInteger:
            formatted_length = hexadecimal
                ? snprintf(buffer, sizeof(buffer), "%llX", static_cast<unsigned long long>(parameter.value.as_signed))
                : snprintf(buffer, sizeof(buffer), "%lld", static_cast<long long>(parameter.value.as_signed));
            break;
        case TypeErasedParameter::Type::Boolean:
            formatted_length = snprintf(buffer, sizeof(buffer), "%s", parameter.value.as_bool ? "true" : "false");
            break;
        case TypeErasedParameter::Type::Character:
            buffer[0] = parameter.value.as_char;
            formatted_length = 1;
            break;
        case TypeErasedParameter::Type::StringView:
            write_all(parameter.value.as_string_view.characters_without_null_termination(), parameter.value.as_string_view.length());
            break;
        case TypeErasedParameter::Type::CString:
            write_all(parameter.value.as_c_string, strlen(parameter.value.as_c_string));
            break;
        default:
            write_all("<unsupported>", 13);
            break;
        }

        if (formatted_length > 0)
            write_all(buffer, static_cast<size_t>(formatted_length));
        cursor = static_cast<size_t>(closing - characters) + 1;
    }

    if (newline)
        write_all("\n", 1);
}

}
