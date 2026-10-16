/*
 * Copyright (c) 2024-2025, Tim Flynn <trflynn89@ladybird.org>
 *
 * SPDX-License-Identifier: BSD-2-Clause
 */

#include <AK/QuickSort.h>
#include <AK/ScopeGuard.h>
#include <LibUnicode/DateTimeFormat.h>
#include <LibUnicode/ICU.h>
#include <LibUnicode/Locale.h>
#include <LibUnicode/UnicodeKeywords.h>
#ifdef AK_OS_RINOS
#    include <LibUnicode/generated_rinos_currency_codes.h>
#    include <LibUnicode/generated_rinos_numbering_systems.h>
#    include <rinicu/data_policy.h>
#endif

#ifndef AK_OS_RINOS
#    include <unicode/calendar.h>
#    include <unicode/coll.h>
#    include <unicode/locid.h>
#    include <unicode/numsys.h>
#    include <unicode/ucurr.h>
#endif

namespace Unicode {

Vector<String> available_keyword_values(StringView locale, StringView key)
{
    if (key == "ca"sv)
        return available_calendars(locale);
    if (key == "co"sv)
        return available_collations(locale);
    if (key == "hc"sv)
        return available_hour_cycles(locale);
    if (key == "kf"sv)
        return available_collation_case_orderings();
    if (key == "kn"sv)
        return available_collation_numeric_orderings();
    if (key == "nu"sv)
        return available_number_systems(locale);
    // Unknown Unicode extension keys have no product-owned value list.  Keep
    // the LocaleData lookup fail-closed instead of aborting the process from
    // a user-controlled locale extension.
    return {};
}

Vector<String> const& available_calendars()
{
    static auto calendars = []() {
        auto calendars = available_calendars("und"sv);

        quick_sort(calendars);
        return calendars;
    }();

    return calendars;
}

#ifdef AK_OS_RINOS
Vector<String> available_calendars(StringView)
{
    return { "gregory"_string, "iso8601"_string, "japanese"_string, "buddhist"_string };
}
#else
Vector<String> available_calendars(StringView locale)
{
    UErrorCode status = U_ZERO_ERROR;

    auto locale_data = LocaleData::for_locale(locale);
    if (!locale_data.has_value())
        return {};

    auto keywords = adopt_own_if_nonnull(icu::Calendar::getKeywordValuesForLocale("calendar", locale_data->locale(), 0, status));
    if (icu_failure(status))
        return {};

    return icu_string_enumeration_to_list(move(keywords), "ca", [](char const* value, size_t value_length) {
        // "islamic" and "islamic-rgsa" are deprecated calendar types that DateTimeFormat resolves to other calendars,
        // so they should not be advertised as available.
        return !StringView { value, value_length }.is_one_of("islamic"sv, "islamic-rgsa"sv);
    });
}
#endif

#ifdef AK_OS_RINOS
Vector<String> const& available_currencies()
{
    // Keep the inventory synchronized with the pinned CLDR release instead
    // of advertising a hand-written five-currency subset.  Formatting
    // behavior is still bounded by RinICU's separate currency projection.
    static auto currencies = []() -> Vector<String> {
        Vector<String> result;
        result.ensure_capacity(RINOS_GENERATED_CURRENCY_CODE_COUNT);
        for (size_t i = 0; i < RINOS_GENERATED_CURRENCY_CODE_COUNT; ++i)
            result.append(MUST(String::from_utf8(StringView { g_rinos_generated_currency_codes[i], 3 })));
        return result;
    }();
    return currencies;
}
#else
Vector<String> const& available_currencies()
{
    static auto currencies = []() -> Vector<String> {
        UErrorCode status = U_ZERO_ERROR;

        auto* currencies = ucurr_openISOCurrencies(UCURR_ALL, &status);
        ScopeGuard guard { [&]() { uenum_close(currencies); } };

        if (icu_failure(status))
            return {};

        Vector<String> result;

        while (true) {
            i32 length = 0;
            char const* next = uenum_next(currencies, &length, &status);

            if (icu_failure(status))
                return {};
            if (next == nullptr)
                break;

            // https://unicode-org.atlassian.net/browse/ICU-21687
            if (StringView currency { next, static_cast<size_t>(length) }; currency != "LSM"sv)
                result.append(MUST(String::from_utf8(currency)));
        }

        quick_sort(result);
        return result;
    }();

    return currencies;
}
#endif

Vector<String> const& available_collation_case_orderings()
{
    static Vector<String> case_orderings { "false"_string, "lower"_string, "upper"_string };
    return case_orderings;
}

Vector<String> const& available_collation_numeric_orderings()
{
    static Vector<String> case_orderings { "false"_string, "true"_string };
    return case_orderings;
}

#ifdef AK_OS_RINOS
Vector<String> const& available_collations()
{
    static Vector<String> collations { "default"_string, "emoji"_string, "eor"_string };
    return collations;
}
#else
Vector<String> const& available_collations()
{
    static auto collations = []() -> Vector<String> {
        UErrorCode status = U_ZERO_ERROR;

        auto keywords = adopt_own_if_nonnull(icu::Collator::getKeywordValues("collation", status));
        if (icu_failure(status))
            return {};

        auto collations = icu_string_enumeration_to_list(move(keywords), "co", [](char const* value, size_t value_length) {
            // https://tc39.es/ecma402/#sec-properties-of-intl-collator-instances
            // the values "standard" and "search" are not allowed
            return !StringView { value, value_length }.is_one_of("standard"sv, "search"sv);
        });

        quick_sort(collations);
        return collations;
    }();

    return collations;
}
#endif

#ifdef AK_OS_RINOS
Vector<String> available_collations(StringView)
{
    return { "default"_string };
}
#else
Vector<String> available_collations(StringView locale)
{
    UErrorCode status = U_ZERO_ERROR;

    auto locale_data = LocaleData::for_locale(locale);
    if (!locale_data.has_value())
        return {};

    auto keywords = adopt_own_if_nonnull(icu::Collator::getKeywordValuesForLocale("collation", locale_data->locale(), true, status));
    if (icu_failure(status))
        return {};

    auto collations = icu_string_enumeration_to_list(move(keywords), "co", [](char const* value, size_t value_length) {
        // https://tc39.es/ecma402/#sec-properties-of-intl-collator-instances
        // the values "standard" and "search" are not allowed
        return !StringView { value, value_length }.is_one_of("standard"sv, "search"sv);
    });

    if (!collations.contains_slow("default"sv))
        collations.prepend("default"_string);

    return collations;
}
#endif

Vector<String> const& available_hour_cycles()
{
    static Vector<String> case_orderings { "h11"_string, "h12"_string, "h23"_string, "h24"_string };
    return case_orderings;
}

Vector<String> available_hour_cycles(StringView locale)
{
    auto preferred_hour_cycle = default_hour_cycle(locale);
    if (!preferred_hour_cycle.has_value())
        return available_hour_cycles();

    Vector<String> hour_cycles;
    hour_cycles.append(MUST(String::from_utf8(hour_cycle_to_string(*preferred_hour_cycle))));

    for (auto const& hour_cycle : available_hour_cycles()) {
        if (hour_cycle != hour_cycles[0])
            hour_cycles.append(hour_cycle);
    }

    return hour_cycles;
}

#ifdef AK_OS_RINOS
Vector<String> const& available_number_systems()
{
    // Keep the advertised keyword values synchronized with the product digit
    // tables.  The generated list is sourced from CLDR's BCP 47 registry, but
    // deliberately excludes algorithmic and otherwise unsupported systems.
    static auto systems = []() -> Vector<String> {
        Vector<String> result;
        result.ensure_capacity(RINOS_GENERATED_NUMBERING_SYSTEM_COUNT);
        for (size_t i = 0; i < RINOS_GENERATED_NUMBERING_SYSTEM_COUNT; ++i) {
            auto const* system = g_rinos_generated_numbering_systems[i];
            result.append(MUST(String::from_utf8(StringView { system, strlen(system) })));
        }
        return result;
    }();
    return systems;
}
#else
Vector<String> const& available_number_systems()
{
    static auto number_systems = []() -> Vector<String> {
        UErrorCode status = U_ZERO_ERROR;

        auto keywords = adopt_own_if_nonnull(icu::NumberingSystem::getAvailableNames(status));
        if (icu_failure(status))
            return {};

        auto number_systems = icu_string_enumeration_to_list(move(keywords), "nu", [&](char const* keyword, size_t) {
            auto system = adopt_own_if_nonnull(icu::NumberingSystem::createInstanceByName(keyword, status));
            if (icu_failure(status))
                return false;

            return !static_cast<bool>(system->isAlgorithmic());
        });

        quick_sort(number_systems);
        return number_systems;
    }();

    return number_systems;
}
#endif

#ifdef AK_OS_RINOS
static Optional<StringView> rinos_numbering_system_name(u32 id)
{
    // These IDs are the append-only projection defined by RinICU's RICUDB1
    // policy.  Keep the lookup indexed by the wire ID rather than deriving a
    // default from a language name; the record is generated from CLDR's
    // locale defaultNumberingSystem field.
    static constexpr char const* names[] = {
        "latn", "arab", "arabext", "deva", "thai", "beng", "gujr",
        "guru", "knda", "khmr", "laoo", "mlym", "mymr", "orya",
        "telu", "tamldec", "tibt", "fullwide", "hanidec", "adlm",
        "olck", "cakm", "hmnp", "mtei", "nkoo",
    };
    if (id >= sizeof(names) / sizeof(names[0]))
        return {};
    return StringView { names[id] };
}

Vector<String> available_number_systems(StringView locale)
{
    auto locale_id = parse_unicode_locale_id(locale);
    if (!locale_id.has_value())
        return {};

    // RICUDB1 is generated from the official CLDR locale record.  An absent
    // field is the legacy/product-root case and retains CLDR's latn default;
    // a malformed or unknown present ID must not be guessed from the locale
    // language or advertised as a supported numbering system.
    ByteString locale_z(locale);
    RinIcuDataLocaleRecord record {};
    auto preferred_number_system = "latn"sv;
    if (rin_icu_locale_info(&rin_icu_client(), locale_z.characters(), &record) != 0 ||
        !rin_icu_data_locale_valid(&record))
        return {};
    if (rin_icu_data_numbering_system_present(record.week_data)) {
        auto name = rinos_numbering_system_name(
            rin_icu_data_numbering_system(record.week_data));
        if (!name.has_value())
            return {};
        preferred_number_system = *name;
    }

    Vector<String> result;
    for (auto const& system : available_number_systems()) {
        if (system.bytes_as_string_view() == preferred_number_system) {
            result.append(system);
            break;
        }
    }
    for (auto const& system : available_number_systems()) {
        if (!result.contains_slow(system))
            result.append(system);
    }
    return result;
}
#else
Vector<String> available_number_systems(StringView locale)
{
    auto locale_data = LocaleData::for_locale(locale);
    if (!locale_data.has_value())
        return {};

    Vector<String> number_systems;

    auto const* preferred_number_system = locale_data->numbering_system().getName();
    number_systems.append(MUST(String::from_utf8({ preferred_number_system, strlen(preferred_number_system) })));

    for (auto const& number_system : available_number_systems()) {
        if (number_system != number_systems[0])
            number_systems.append(number_system);
    }

    return number_systems;
}
#endif

}
