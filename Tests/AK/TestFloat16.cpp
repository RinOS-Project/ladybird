/*
 * Copyright (c) 2026, RinOS contributors
 *
 * SPDX-License-Identifier: BSD-2-Clause
 */

#include <LibTest/TestCase.h>

#include <AK/NumericLimits.h>
#include <AK/Types.h>

static u16 raw_bits(f16 value)
{
    return __builtin_bit_cast(u16, value);
}

static f16 from_raw_bits(u16 bits)
{
    return __builtin_bit_cast(f16, bits);
}

TEST_CASE(binary16_rounding_and_boundaries)
{
    EXPECT_EQ(raw_bits(static_cast<f16>(1.0)), 0x3c00u);
    EXPECT_EQ(raw_bits(static_cast<f16>(65504.0)), 0x7bffu);
    EXPECT_EQ(raw_bits(static_cast<f16>(65520.0)), 0x7c00u);
    EXPECT_EQ(raw_bits(static_cast<f16>(0x1p-14)), 0x0400u);
    EXPECT_EQ(raw_bits(static_cast<f16>(0x1p-24)), 0x0001u);
    EXPECT_EQ(raw_bits(static_cast<f16>(0x1p-25)), 0x0000u);
    EXPECT_EQ(raw_bits(static_cast<f16>(1.00048828125)), 0x3c00u);
    EXPECT_EQ(raw_bits(static_cast<f16>(1.00146484375)), 0x3c02u);
}

TEST_CASE(binary16_decode_and_numeric_limits)
{
    EXPECT_EQ(static_cast<double>(from_raw_bits(0x0001u)), 0x1p-24);
    EXPECT_EQ(static_cast<double>(from_raw_bits(0x0400u)), 0x1p-14);
    EXPECT_EQ(static_cast<double>(from_raw_bits(0xbc00u)), -1.0);
    EXPECT_EQ(raw_bits(NumericLimits<f16>::lowest()), 0xfbffu);
    EXPECT_EQ(raw_bits(NumericLimits<f16>::min_normal()), 0x0400u);
    EXPECT_EQ(raw_bits(NumericLimits<f16>::min_denormal()), 0x0001u);
    EXPECT_EQ(raw_bits(NumericLimits<f16>::max()), 0x7bffu);
    EXPECT_EQ(raw_bits(NumericLimits<f16>::epsilon()), 0x1400u);
    EXPECT_EQ(NumericLimits<f16>::digits(), 11u);
}
