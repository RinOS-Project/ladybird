/*
 * Copyright (c) 2026, RinOS Contributors
 * SPDX-License-Identifier: BSD-2-Clause
 */

#ifdef AK_OS_RINOS

#include <AK/Time.h>
#include <AK/String.h>
#include <LibUnicode/RinICUBridge.h>

namespace Unicode {

static constexpr i64 kRinIcuReconnectRetryCooldownMs = 250;
static thread_local rin_icu_client_t s_client = { -1, 1u, 0u };
static thread_local bool s_client_ready = false;
static thread_local i64 s_next_open_retry_ms = 0;

static void reset_rin_icu_client()
{
    s_client.fd = -1;
    s_client.next_request_id = 1u;
    s_client.reserved0 = 0u;
}

rin_icu_client_t& rin_icu_client()
{
    if (s_client_ready && s_client.fd >= 0)
        return s_client;

    if (s_client.fd >= 0) {
        s_client_ready = true;
        s_next_open_retry_ms = 0;
        return s_client;
    }

    auto now_ms = MonotonicTime::now_coarse().milliseconds();
    if (s_next_open_retry_ms != 0 && now_ms < s_next_open_retry_ms)
        return s_client;

    reset_rin_icu_client();
    if (rin_icu_client_open(&s_client) == 0 && s_client.fd >= 0) {
        s_client_ready = true;
        s_next_open_retry_ms = 0;
        return s_client;
    }

    reset_rin_icu_client();
    s_client_ready = false;
    s_next_open_retry_ms = now_ms + kRinIcuReconnectRetryCooldownMs;
    return s_client;
}

String rin_icu_locale_string_op(
    int (*fn)(rin_icu_client_t*, char const*, char*, size_t, size_t*),
    StringView locale)
{
    char buf[256];
    size_t len = 0;

    // Null-terminate the locale for C API
    char locale_buf[128];
    auto n = locale.length() < sizeof(locale_buf) - 1 ? locale.length() : sizeof(locale_buf) - 1;
    __builtin_memcpy(locale_buf, locale.characters_without_null_termination(), n);
    locale_buf[n] = '\0';

    if (fn(&rin_icu_client(), locale_buf, buf, sizeof(buf), &len) == 0 && len > 0)
        return MUST(String::from_utf8({ buf, len }));
    return MUST(String::from_utf8(locale));
}

String rin_icu_tz_string_op(
    int (*fn)(rin_icu_client_t*, char const*, char*, size_t, size_t*),
    StringView tz)
{
    char buf[256];
    size_t len = 0;

    char tz_buf[128];
    auto n = tz.length() < sizeof(tz_buf) - 1 ? tz.length() : sizeof(tz_buf) - 1;
    __builtin_memcpy(tz_buf, tz.characters_without_null_termination(), n);
    tz_buf[n] = '\0';

    if (fn(&rin_icu_client(), tz_buf, buf, sizeof(buf), &len) == 0 && len > 0)
        return MUST(String::from_utf8({ buf, len }));
    return MUST(String::from_utf8(tz));
}

int rin_icu_normalization_form(NormalizationForm form)
{
    switch (form) {
    case NormalizationForm::NFD:
        return RIN_ICU_NORMALIZE_NFD;
    case NormalizationForm::NFC:
        return RIN_ICU_NORMALIZE_NFC;
    case NormalizationForm::NFKD:
        return RIN_ICU_NORMALIZE_NFKD;
    case NormalizationForm::NFKC:
        return RIN_ICU_NORMALIZE_NFKC;
    }
    VERIFY_NOT_REACHED();
}

u32 rin_icu_segmenter_granularity(SegmenterGranularity granularity)
{
    switch (granularity) {
    case SegmenterGranularity::Grapheme:
        return RIN_ICU_SEGMENTATION_GRAPHEME;
    case SegmenterGranularity::Line:
        return RIN_ICU_SEGMENTATION_LINE;
    case SegmenterGranularity::Sentence:
        return RIN_ICU_SEGMENTATION_SENTENCE;
    case SegmenterGranularity::Word:
        return RIN_ICU_SEGMENTATION_WORD;
    }
    VERIFY_NOT_REACHED();
}

u32 rin_icu_number_style(NumberFormatStyle style)
{
    switch (style) {
    case NumberFormatStyle::Decimal:
    case NumberFormatStyle::Unit:
        return RIN_ICU_NUMBER_STYLE_DECIMAL;
    case NumberFormatStyle::Percent:
        return RIN_ICU_NUMBER_STYLE_PERCENT;
    case NumberFormatStyle::Currency:
        return RIN_ICU_NUMBER_STYLE_CURRENCY;
    }
    VERIFY_NOT_REACHED();
}

u32 rin_icu_plural_kind(PluralForm plural_form)
{
    switch (plural_form) {
    case PluralForm::Cardinal:
        return RIN_ICU_PLURAL_KIND_CARDINAL;
    case PluralForm::Ordinal:
        return RIN_ICU_PLURAL_KIND_ORDINAL;
    }
    VERIFY_NOT_REACHED();
}

u32 rin_icu_style(Style style)
{
    switch (style) {
    case Style::Long:
        return RIN_ICU_STYLE_LONG;
    case Style::Short:
        return RIN_ICU_STYLE_SHORT;
    case Style::Narrow:
        return RIN_ICU_STYLE_NARROW;
    }
    VERIFY_NOT_REACHED();
}

u32 rin_icu_language_display(LanguageDisplay display)
{
    switch (display) {
    case LanguageDisplay::Standard:
        return RIN_ICU_LANGUAGE_DISPLAY_STANDARD;
    case LanguageDisplay::Dialect:
        return RIN_ICU_LANGUAGE_DISPLAY_DIALECT;
    }
    VERIFY_NOT_REACHED();
}

u32 rin_icu_list_format_type(ListFormatType type)
{
    switch (type) {
    case ListFormatType::Conjunction:
        return RIN_ICU_LIST_FORMAT_CONJUNCTION;
    case ListFormatType::Disjunction:
        return RIN_ICU_LIST_FORMAT_DISJUNCTION;
    case ListFormatType::Unit:
        return RIN_ICU_LIST_FORMAT_UNIT;
    }
    VERIFY_NOT_REACHED();
}

u32 rin_icu_numeric_display(NumericDisplay display)
{
    switch (display) {
    case NumericDisplay::Always:
        return RIN_ICU_NUMERIC_DISPLAY_ALWAYS;
    case NumericDisplay::Auto:
        return RIN_ICU_NUMERIC_DISPLAY_AUTO;
    }
    VERIFY_NOT_REACHED();
}

u32 rin_icu_time_unit(TimeUnit unit)
{
    switch (unit) {
    case TimeUnit::Second:
        return RIN_ICU_TIME_UNIT_SECOND;
    case TimeUnit::Minute:
        return RIN_ICU_TIME_UNIT_MINUTE;
    case TimeUnit::Hour:
        return RIN_ICU_TIME_UNIT_HOUR;
    case TimeUnit::Day:
        return RIN_ICU_TIME_UNIT_DAY;
    case TimeUnit::Week:
        return RIN_ICU_TIME_UNIT_WEEK;
    case TimeUnit::Month:
        return RIN_ICU_TIME_UNIT_MONTH;
    case TimeUnit::Quarter:
        return RIN_ICU_TIME_UNIT_QUARTER;
    case TimeUnit::Year:
        return RIN_ICU_TIME_UNIT_YEAR;
    }
    VERIFY_NOT_REACHED();
}

u32 rin_icu_hour_cycle(Optional<HourCycle> const& hour_cycle, Optional<bool> const& hour12)
{
    if (hour12.has_value())
        return *hour12 ? RIN_ICU_HOUR_CYCLE_H12 : RIN_ICU_HOUR_CYCLE_H24;

    if (!hour_cycle.has_value())
        return RIN_ICU_HOUR_CYCLE_DEFAULT;

    switch (*hour_cycle) {
    case HourCycle::H11:
    case HourCycle::H12:
        return RIN_ICU_HOUR_CYCLE_H12;
    case HourCycle::H23:
    case HourCycle::H24:
        return RIN_ICU_HOUR_CYCLE_H24;
    }
    VERIFY_NOT_REACHED();
}

} // namespace Unicode

#endif // AK_OS_RINOS
