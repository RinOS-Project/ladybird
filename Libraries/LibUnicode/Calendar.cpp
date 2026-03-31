/*
 * Copyright (c) 2026, Tim Flynn <trflynn89@ladybird.org>
 *
 * SPDX-License-Identifier: BSD-2-Clause
 */

#include <LibUnicode/Calendar.h>

#ifndef AK_OS_RINOS
#    include <LibUnicode/RustFFI.h>
#endif

namespace Unicode {

// https://tc39.es/proposal-temporal/#prod-MonthCode
static constexpr bool is_valid_month_code_string(StringView month_code)
{
    // MonthCode :::
    //     M00L
    //     M0 NonZeroDigit L[opt]
    //     M NonZeroDigit DecimalDigit L[opt]
    auto length = month_code.length();

    if (length != 3 && length != 4)
        return false;

    if (month_code[0] != 'M')
        return false;

    if (!is_ascii_digit(month_code[1]) || !is_ascii_digit(month_code[2]))
        return false;

    if (length == 3 && month_code[1] == '0' && month_code[2] == '0')
        return false;
    if (length == 4 && month_code[3] != 'L')
        return false;

    return true;
}

// 12.2.1 ParseMonthCode ( argument ), https://tc39.es/proposal-temporal/#sec-temporal-parsemonthcode
Optional<MonthCode> parse_month_code(StringView month_code)
{
    // 3. If ParseText(StringToCodePoints(monthCode), MonthCode) is a List of errors, throw a RangeError exception.
    if (!is_valid_month_code_string(month_code))
        return {};

    // 4. Let isLeapMonth be false.
    auto is_leap_month = false;

    // 5. If the length of monthCode = 4, then
    if (month_code.length() == 4) {
        // a. Assert: The fourth code unit of monthCode is 0x004C (LATIN CAPITAL LETTER L).
        VERIFY(month_code[3] == 'L');

        // b. Set isLeapMonth to true.
        is_leap_month = true;
    }

    // 6. Let monthCodeDigits be the substring of monthCode from 1 to 3.
    auto month_code_digits = month_code.substring_view(1, 2);

    // 7. Let monthNumber be ℝ(StringToNumber(monthCodeDigits)).
    auto month_number = month_code_digits.to_number<u8>().value();

    // 8. Return the Record { [[MonthNumber]]: monthNumber, [[IsLeapMonth]]: isLeapMonth }.
    return MonthCode { month_number, is_leap_month };
}

// 12.2.2 CreateMonthCode ( monthNumber, isLeapMonth ), https://tc39.es/proposal-temporal/#sec-temporal-createmonthcode
String create_month_code(u8 month_number, bool is_leap_month)
{
    // 1. Assert: If isLeapMonth is false, monthNumber > 0.
    if (!is_leap_month)
        VERIFY(month_number > 0);

    // 2. Let numberPart be ToZeroPaddedDecimalString(monthNumber, 2).

    // 3. If isLeapMonth is true, then
    if (is_leap_month) {
        // a. Return the string-concatenation of the code unit 0x004D (LATIN CAPITAL LETTER M), numberPart, and the
        //    code unit 0x004C (LATIN CAPITAL LETTER L).
        return MUST(String::formatted("M{:02}L", month_number));
    }

    // 4. Return the string-concatenation of the code unit 0x004D (LATIN CAPITAL LETTER M) and numberPart.
    return MUST(String::formatted("M{:02}", month_number));
}

CalendarDate iso_date_to_calendar_date(String const& calendar, ISODate iso_date)
{
#ifdef AK_OS_RINOS
    (void)calendar;
    // RinOS: Gregorian-only — ISO date is the calendar date
    // Day of week: Tomohiko Sakamoto's algorithm
    auto y = iso_date.year;
    auto m = iso_date.month;
    auto d = iso_date.day;
    static constexpr int t[] = { 0, 3, 2, 5, 0, 3, 5, 1, 4, 6, 2, 4 };
    if (m < 3) y -= 1;
    int dow = (y + y / 4 - y / 100 + y / 400 + t[m - 1] + d) % 7;
    if (dow <= 0) dow += 7; // 1=Mon..7=Sun adjusted to 1=Sun..7=Sat below
    // ISO: 1=Monday..7=Sunday
    int iso_dow = (dow == 0) ? 7 : dow; // Sun=0→7

    // Day of year
    static constexpr int days_before_month[] = { 0, 31, 59, 90, 120, 151, 181, 212, 243, 273, 304, 334 };
    bool leap = (iso_date.year % 4 == 0 && (iso_date.year % 100 != 0 || iso_date.year % 400 == 0));
    int doy = days_before_month[m - 1] + d + (leap && m > 2 ? 1 : 0);

    int days_in_m = 31;
    if (m == 4 || m == 6 || m == 9 || m == 11) days_in_m = 30;
    else if (m == 2) days_in_m = leap ? 29 : 28;

    int days_in_y = leap ? 366 : 365;

    return CalendarDate {
        .era = {},
        .era_year = {},
        .year = iso_date.year,
        .month = iso_date.month,
        .month_code = MUST(String::formatted("M{:02}", iso_date.month)),
        .day = iso_date.day,
        .day_of_week = static_cast<u8>(iso_dow),
        .day_of_year = static_cast<u16>(doy),
        .week_of_year = {},
        .days_in_week = 7,
        .days_in_month = static_cast<u8>(days_in_m),
        .days_in_year = static_cast<u16>(days_in_y),
        .months_in_year = 12,
        .in_leap_year = leap,
    };
#else
    auto result = FFI::icu_iso_date_to_calendar_date(calendar.bytes().data(), calendar.bytes().size(), iso_date.year, iso_date.month, iso_date.day);

    return CalendarDate {
        .era = {},
        .era_year = {},
        .year = result.year,
        .month = result.month,
        .month_code = String::from_utf8_without_validation({ result.month_code, result.month_code_length }),
        .day = result.day,
        .day_of_week = result.day_of_week,
        .day_of_year = result.day_of_year,
        .week_of_year = {},
        .days_in_week = result.days_in_week,
        .days_in_month = result.days_in_month,
        .days_in_year = result.days_in_year,
        .months_in_year = result.months_in_year,
        .in_leap_year = result.in_leap_year,
    };
#endif
}

Optional<ISODate> calendar_date_to_iso_date(String const& calendar, i32 year, u8 month, u8 day)
{
#ifdef AK_OS_RINOS
    (void)calendar;
    return ISODate { year, month, day };
#else
    auto result = FFI::icu_calendar_date_to_iso_date(calendar.bytes().data(), calendar.bytes().size(), year, month, day);
    if (!result.has_value)
        return {};

    return ISODate { result.iso_date.year, result.iso_date.month, result.iso_date.day };
#endif
}

Optional<ISODate> iso_year_and_month_code_to_iso_date(String const& calendar, i32 year, StringView month_code, u8 day)
{
#ifdef AK_OS_RINOS
    (void)calendar;
    auto mc = parse_month_code(month_code);
    if (!mc.has_value() || mc->is_leap_month)
        return {};
    return ISODate { year, mc->month_number, day };
#else
    auto result = FFI::icu_iso_year_and_month_code_to_iso_date(calendar.bytes().data(), calendar.bytes().size(), year, month_code.bytes().data(), month_code.length(), day);
    if (!result.has_value)
        return {};

    return ISODate { result.iso_date.year, result.iso_date.month, result.iso_date.day };
#endif
}

Optional<ISODate> calendar_year_and_month_code_to_iso_date(String const& calendar, i32 arithmetic_year, StringView month_code, u8 day)
{
#ifdef AK_OS_RINOS
    (void)calendar;
    auto mc = parse_month_code(month_code);
    if (!mc.has_value() || mc->is_leap_month)
        return {};
    return ISODate { arithmetic_year, mc->month_number, day };
#else
    auto result = FFI::icu_calendar_year_and_month_code_to_iso_date(calendar.bytes().data(), calendar.bytes().size(), arithmetic_year, month_code.bytes().data(), month_code.length(), day);
    if (!result.has_value)
        return {};

    return ISODate { result.iso_date.year, result.iso_date.month, result.iso_date.day };
#endif
}

u8 calendar_months_in_year(String const& calendar, i32 arithmetic_year)
{
#ifdef AK_OS_RINOS
    (void)calendar;
    (void)arithmetic_year;
    return 12;
#else
    return FFI::icu_calendar_months_in_year(calendar.bytes().data(), calendar.bytes().size(), arithmetic_year);
#endif
}

u8 calendar_days_in_month(String const& calendar, i32 arithmetic_year, u8 ordinal_month)
{
#ifdef AK_OS_RINOS
    (void)calendar;
    static constexpr u8 days[] = { 31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31 };
    if (ordinal_month < 1 || ordinal_month > 12) return 30;
    if (ordinal_month == 2) {
        bool leap = (arithmetic_year % 4 == 0 && (arithmetic_year % 100 != 0 || arithmetic_year % 400 == 0));
        return leap ? 29 : 28;
    }
    return days[ordinal_month - 1];
#else
    return FFI::icu_calendar_days_in_month(calendar.bytes().data(), calendar.bytes().size(), arithmetic_year, ordinal_month);
#endif
}

u8 calendar_max_days_in_month_code(String const& calendar, StringView month_code)
{
#ifdef AK_OS_RINOS
    (void)calendar;
    auto mc = parse_month_code(month_code);
    if (!mc.has_value()) return 31;
    static constexpr u8 max_days[] = { 31, 29, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31 };
    if (mc->month_number < 1 || mc->month_number > 12) return 31;
    return max_days[mc->month_number - 1];
#else
    return FFI::icu_calendar_max_days_in_month_code(calendar.bytes().data(), calendar.bytes().size(), month_code.bytes().data(), month_code.length());
#endif
}

bool calendar_year_contains_month_code(String const& calendar, i32 arithmetic_year, StringView month_code)
{
#ifdef AK_OS_RINOS
    (void)calendar;
    (void)arithmetic_year;
    auto mc = parse_month_code(month_code);
    if (!mc.has_value() || mc->is_leap_month) return false;
    return mc->month_number >= 1 && mc->month_number <= 12;
#else
    return FFI::icu_year_contains_month_code(calendar.bytes().data(), calendar.bytes().size(), arithmetic_year, month_code.bytes().data(), month_code.length());
#endif
}

}
