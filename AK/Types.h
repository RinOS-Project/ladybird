/*
 * Copyright (c) 2018-2020, Andreas Kling <andreas@ladybird.org>
 *
 * SPDX-License-Identifier: BSD-2-Clause
 */

#pragma once

#include <AK/Platform.h>

using u64 = __UINT64_TYPE__;
using u32 = __UINT32_TYPE__;
using u16 = __UINT16_TYPE__;
using u8 = __UINT8_TYPE__;
using i64 = __INT64_TYPE__;
using i32 = __INT32_TYPE__;
using i16 = __INT16_TYPE__;
using i8 = __INT8_TYPE__;

#if defined(__FLT16_MANT_DIG__)
using f16 = _Float16;
static_assert(__FLT16_MANT_DIG__ == 11 && __FLT16_MAX_EXP__ == 16);
#else
// Keep IEEE binary16 available on targets where the compiler does not expose
// a native half type (notably baseline IA-32). Integer-only conversions avoid
// imposing an ISA requirement such as F16C or AVX-512FP16.
struct f16 {
    constexpr f16() = default;
    template<typename T>
    requires(__is_integral(T))
    constexpr f16(T value)
        : m_bits(from_double(static_cast<double>(value)))
    {
    }
    constexpr f16(float value)
        : m_bits(from_double(static_cast<double>(value)))
    {
    }
    constexpr f16(double value)
        : m_bits(from_double(value))
    {
    }

    constexpr operator float() const { return to_float(m_bits); }
    constexpr u16 bits() const { return m_bits; }

    static constexpr f16 from_bits(u16 bits)
    {
        f16 value;
        value.m_bits = bits;
        return value;
    }

private:
    static constexpr u64 round_to_even(u64 value, unsigned shift)
    {
        auto truncated = value >> shift;
        auto remainder = value & ((u64 { 1 } << shift) - 1);
        auto halfway = u64 { 1 } << (shift - 1);
        if (remainder > halfway || (remainder == halfway && (truncated & 1u)))
            ++truncated;
        return truncated;
    }

    static constexpr u16 from_double(double value)
    {
        auto raw = __builtin_bit_cast(u64, value);
        auto sign = static_cast<u16>((raw >> 48) & 0x8000u);
        auto exponent = static_cast<unsigned>((raw >> 52) & 0x7ffu);
        auto fraction = raw & 0x000fffffffffffffull;

        if (exponent == 0x7ffu) {
            if (fraction == 0)
                return static_cast<u16>(sign | 0x7c00u);
            auto payload = static_cast<u16>(fraction >> 42) | 0x0200u;
            return static_cast<u16>(sign | 0x7c00u | (payload & 0x03ffu));
        }

        // Every binary64 subnormal is far below half of the least binary16
        // subnormal, so it rounds to signed zero.
        if (exponent == 0)
            return sign;

        auto unbiased_exponent = static_cast<int>(exponent) - 1023;
        if (unbiased_exponent > 15)
            return static_cast<u16>(sign | 0x7c00u);
        if (unbiased_exponent < -25)
            return sign;

        auto significand = (u64 { 1 } << 52) | fraction;
        if (unbiased_exponent < -14) {
            auto shift = static_cast<unsigned>(28 - unbiased_exponent);
            return static_cast<u16>(sign | round_to_even(significand, shift));
        }

        auto rounded_significand = round_to_even(significand, 42);
        auto half_exponent = static_cast<unsigned>(unbiased_exponent + 15);
        if (rounded_significand == 0x800u) {
            rounded_significand = 0x400u;
            ++half_exponent;
        }
        if (half_exponent >= 31u)
            return static_cast<u16>(sign | 0x7c00u);
        return static_cast<u16>(sign | (half_exponent << 10) | (rounded_significand & 0x03ffu));
    }

    static constexpr float to_float(u16 bits)
    {
        auto sign = static_cast<u32>(bits & 0x8000u) << 16;
        auto exponent = static_cast<unsigned>((bits >> 10) & 0x1fu);
        auto fraction = static_cast<u32>(bits & 0x03ffu);
        u32 raw;

        if (exponent == 0) {
            if (fraction == 0)
                raw = sign;
            else {
                int unbiased_exponent = -14;
                while ((fraction & 0x0400u) == 0) {
                    fraction <<= 1;
                    --unbiased_exponent;
                }
                fraction &= 0x03ffu;
                raw = sign | (static_cast<u32>(unbiased_exponent + 127) << 23) | (fraction << 13);
            }
        } else if (exponent == 0x1fu) {
            raw = sign | 0x7f800000u | (fraction << 13);
        } else {
            raw = sign | (static_cast<u32>(static_cast<int>(exponent) - 15 + 127) << 23) | (fraction << 13);
        }
        return __builtin_bit_cast(float, raw);
    }

    u16 m_bits { 0 };
};

static_assert(sizeof(f16) == sizeof(u16));
static_assert(f16(1.0).bits() == 0x3c00u);
static_assert(f16(65504.0).bits() == 0x7bffu);
static_assert(f16(0x1p-14).bits() == 0x0400u);
static_assert(f16(0x1p-24).bits() == 0x0001u);
static_assert(f16(1.00048828125).bits() == 0x3c00u);
static_assert(f16(1.00146484375).bits() == 0x3c02u);
static_assert(f16(0x1p-25).bits() == 0x0000u);
static_assert(f16(65520.0).bits() == 0x7c00u);
static_assert(static_cast<double>(f16::from_bits(0x0001u)) == 0x1p-24);
static_assert(static_cast<double>(f16::from_bits(0x0400u)) == 0x1p-14);
#endif

using f32 = float;
static_assert(__FLT_MANT_DIG__ == 24 && __FLT_MAX_EXP__ == 128);

using f64 = double;
static_assert(__DBL_MANT_DIG__ == 53 && __DBL_MAX_EXP__ == 1024);

#if __LDBL_MANT_DIG__ == 64 && __LDBL_MAX_EXP__ == 16384
#    define AK_HAS_FLOAT_80 1
using f80 = long double;
#elif __LDBL_MANT_DIG__ == 113 && __LDBL_MAX_EXP__ == 16384
#    define AK_HAS_FLOAT_128 1
using f128 = long double;
#endif

namespace AK::Detail {

// MakeSigned<> is here instead of in StdLibExtras.h because it's used in the definition of size_t and ssize_t
// and Types.h must not include StdLibExtras.h to avoid circular dependencies.
template<typename T>
struct __MakeSigned {
    using Type = void;
};
template<>
struct __MakeSigned<signed char> {
    using Type = signed char;
};
template<>
struct __MakeSigned<short> {
    using Type = short;
};
template<>
struct __MakeSigned<int> {
    using Type = int;
};
template<>
struct __MakeSigned<long> {
    using Type = long;
};
template<>
struct __MakeSigned<long long> {
    using Type = long long;
};
template<>
struct __MakeSigned<unsigned char> {
    using Type = char;
};
template<>
struct __MakeSigned<unsigned short> {
    using Type = short;
};
template<>
struct __MakeSigned<unsigned int> {
    using Type = int;
};
template<>
struct __MakeSigned<unsigned long> {
    using Type = long;
};
template<>
struct __MakeSigned<unsigned long long> {
    using Type = long long;
};
template<>
struct __MakeSigned<char> {
    using Type = char;
};

template<typename T>
using MakeSigned = typename __MakeSigned<T>::Type;

// Conditional<> is here instead of in StdLibExtras.h because it's used in the definition of FlatPtr
// and Types.h must not include StdLibExtras.h to avoid circular dependencies.
template<bool condition, class TrueType, class FalseType>
struct __Conditional {
    using Type = TrueType;
};

template<class TrueType, class FalseType>
struct __Conditional<false, TrueType, FalseType> {
    using Type = FalseType;
};

template<bool condition, class TrueType, class FalseType>
using Conditional = typename __Conditional<condition, TrueType, FalseType>::Type;

}

#ifdef AK_OS_SERENITY

using size_t = __SIZE_TYPE__;
using ssize_t = AK::Detail::MakeSigned<size_t>;

using ptrdiff_t = __PTRDIFF_TYPE__;

using intptr_t = __INTPTR_TYPE__;
using uintptr_t = __UINTPTR_TYPE__;

using uint8_t = u8;
using uint16_t = u16;
using uint32_t = u32;
using uint64_t = u64;

using int8_t = i8;
using int16_t = i16;
using int32_t = i32;
using int64_t = i64;

using pid_t = int;

#else
#    include <stddef.h>
#    include <stdint.h>
#    include <sys/types.h>

#    if defined(AK_OS_WINDOWS)
using ssize_t = AK::Detail::MakeSigned<size_t>;
using mode_t = unsigned short;
using pid_t = int;
#    endif
#endif

using FlatPtr = AK::Detail::Conditional<sizeof(void*) == 8, u64, u32>;

constexpr u64 KiB = 1024;
constexpr u64 MiB = KiB * KiB;
constexpr u64 GiB = KiB * KiB * KiB;
constexpr u64 TiB = KiB * KiB * KiB * KiB;
constexpr u64 PiB = KiB * KiB * KiB * KiB * KiB;
constexpr u64 EiB = KiB * KiB * KiB * KiB * KiB * KiB;

namespace AK_REPLACED_STD_NAMESPACE { // NOLINT(cert-dcl58-cpp) nullptr_t must be in ::std:: for some analysis tools

using nullptr_t = decltype(nullptr);

}

namespace AK {

using nullptr_t = AK_REPLACED_STD_NAMESPACE::nullptr_t;

static constexpr FlatPtr explode_byte(u8 b)
{
    FlatPtr value = b;
    if constexpr (sizeof(FlatPtr) == 4)
        return value << 24 | value << 16 | value << 8 | value;
    else if constexpr (sizeof(FlatPtr) == 8)
        return value << 56 | value << 48 | value << 40 | value << 32 | value << 24 | value << 16 | value << 8 | value;
}

static_assert(explode_byte(0xff) == static_cast<FlatPtr>(0xffffffffffffffffull));
static_assert(explode_byte(0x80) == static_cast<FlatPtr>(0x8080808080808080ull));
static_assert(explode_byte(0x7f) == static_cast<FlatPtr>(0x7f7f7f7f7f7f7f7full));
static_assert(explode_byte(0) == 0);

constexpr size_t align_up_to(size_t const value, size_t const alignment)
{
    return (value + (alignment - 1)) & ~(alignment - 1);
}

constexpr size_t align_down_to(size_t const value, size_t const alignment)
{
    return value & ~(alignment - 1);
}

enum class [[nodiscard]] TriState : u8 {
    False,
    True,
    Unknown
};

enum MemoryOrder {
    memory_order_relaxed = __ATOMIC_RELAXED,
    memory_order_consume = __ATOMIC_CONSUME,
    memory_order_acquire = __ATOMIC_ACQUIRE,
    memory_order_release = __ATOMIC_RELEASE,
    memory_order_acq_rel = __ATOMIC_ACQ_REL,
    memory_order_seq_cst = __ATOMIC_SEQ_CST
};

#ifdef AK_OS_WINDOWS
inline void* to_handle(int fd)
{
    return reinterpret_cast<void*>(static_cast<intptr_t>(fd));
}
inline int to_fd(void* handle)
{
    return reinterpret_cast<intptr_t>(handle);
}
#endif

}

#if USING_AK_GLOBALLY
using AK::align_down_to;
using AK::align_up_to;
using AK::explode_byte;
using AK::MemoryOrder;
using AK::nullptr_t;
using AK::TriState;
#    ifdef AK_OS_WINDOWS
using AK::to_fd;
using AK::to_handle;
#    endif
#endif
