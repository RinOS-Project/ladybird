/*
 * Copyright (c) 2026, RinOS Contributors
 * SPDX-License-Identifier: BSD-2-Clause
 *
 * Bridge header for replacing ICU4C with rinicu (IPC) + libunicode (local).
 * Only included when AK_OS_RINOS is defined.
 */

#pragma once

#ifdef AK_OS_RINOS

#include <AK/Optional.h>
#include <AK/String.h>
#include <AK/StringView.h>
#include <LibUnicode/DateTimeFormat.h>
#include <LibUnicode/DisplayNames.h>
#include <LibUnicode/ListFormat.h>
#include <LibUnicode/Normalize.h>
#include <LibUnicode/NumberFormat.h>
#include <LibUnicode/PluralRules.h>
#include <LibUnicode/RelativeTimeFormat.h>
#include <LibUnicode/Segmenter.h>

extern "C" {
#include "../../../rinicu/rin_icu.h"
#include <rin_unicode.h>
}

namespace Unicode {

static_assert(RIN_ICU_VERSION == 2u, "LibUnicode RinOS bridge requires RinICU ABI v2");
static_assert(sizeof(rin_icu_segmenter_options_t) == sizeof(RinIcuSegmenterOptions), "Unexpected RinICU segmenter options layout");
static_assert(sizeof(rin_icu_number_formatter_options_t) == sizeof(RinIcuNumberFormatterOptions), "Unexpected RinICU number formatter options layout");
static_assert(sizeof(rin_icu_datetime_formatter_options_t) == sizeof(RinIcuDateTimeFormatterOptions), "Unexpected RinICU datetime formatter options layout");
static_assert(sizeof(rin_icu_plural_rules_options_t) == sizeof(RinIcuPluralRulesOptions), "Unexpected RinICU plural rules options layout");
static_assert(sizeof(rin_icu_segment_t) == sizeof(RinIcuSegmentNextResponse), "Unexpected RinICU segment response layout");

// Thread-local rinicu client connection.
// Multiple LibUnicode calls share one IPC connection per thread.
rin_icu_client_t& rin_icu_client();

// Convenience: call rinicu with a locale string, filling a char buffer.
// Returns a String from the buffer, or empty on failure.
String rin_icu_locale_string_op(
    int (*fn)(rin_icu_client_t*, char const*, char*, size_t, size_t*),
    StringView locale);

// Convenience: call rinicu timezone string op.
String rin_icu_tz_string_op(
    int (*fn)(rin_icu_client_t*, char const*, char*, size_t, size_t*),
    StringView tz);

int rin_icu_normalization_form(NormalizationForm form);
u32 rin_icu_segmenter_granularity(SegmenterGranularity granularity);
u32 rin_icu_number_style(NumberFormatStyle style);
u32 rin_icu_plural_kind(PluralForm plural_form);
u32 rin_icu_style(Style style);
u32 rin_icu_language_display(LanguageDisplay display);
u32 rin_icu_list_format_type(ListFormatType type);
u32 rin_icu_numeric_display(NumericDisplay display);
u32 rin_icu_time_unit(TimeUnit unit);
u32 rin_icu_hour_cycle(Optional<HourCycle> const& hour_cycle, Optional<bool> const& hour12);

} // namespace Unicode

#endif // AK_OS_RINOS
