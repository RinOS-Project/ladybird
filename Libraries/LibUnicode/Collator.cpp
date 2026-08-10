/*
 * Copyright (c) 2024-2025, Tim Flynn <trflynn89@ladybird.org>
 *
 * SPDX-License-Identifier: BSD-2-Clause
 */

#include <LibUnicode/Collator.h>
#include <LibUnicode/ICU.h>

#ifndef AK_OS_RINOS
#include <unicode/coll.h>
#else
#include <LibUnicode/RinICUBridge.h>
#endif

namespace Unicode {

Usage usage_from_string(StringView usage)
{
    if (usage == "sort"sv)
        return Usage::Sort;
    if (usage == "search"sv)
        return Usage::Search;
    VERIFY_NOT_REACHED();
}

StringView usage_to_string(Usage usage)
{
    switch (usage) {
    case Usage::Sort:
        return "sort"sv;
    case Usage::Search:
        return "search"sv;
    }
    VERIFY_NOT_REACHED();
}

#ifndef AK_OS_RINOS
static NonnullOwnPtr<icu::Locale> apply_usage_to_locale(icu::Locale const& locale, Usage usage, StringView collation)
{
    auto result = adopt_own(*locale.clone());
    UErrorCode status = U_ZERO_ERROR;

    switch (usage) {
    case Usage::Sort:
        result->setUnicodeKeywordValue("co", icu_string_piece(collation), status);
        break;
    case Usage::Search:
        result->setUnicodeKeywordValue("co", "search", status);
        break;
    }

    verify_icu_success(status);
    return result;
}
#endif

Sensitivity sensitivity_from_string(StringView sensitivity)
{
    if (sensitivity == "base"sv)
        return Sensitivity::Base;
    if (sensitivity == "accent"sv)
        return Sensitivity::Accent;
    if (sensitivity == "case"sv)
        return Sensitivity::Case;
    if (sensitivity == "variant"sv)
        return Sensitivity::Variant;
    VERIFY_NOT_REACHED();
}

StringView sensitivity_to_string(Sensitivity sensitivity)
{
    switch (sensitivity) {
    case Sensitivity::Base:
        return "base"sv;
    case Sensitivity::Accent:
        return "accent"sv;
    case Sensitivity::Case:
        return "case"sv;
    case Sensitivity::Variant:
        return "variant"sv;
    }
    VERIFY_NOT_REACHED();
}

#ifndef AK_OS_RINOS
static constexpr UColAttributeValue icu_sensitivity(Sensitivity sensitivity)
{
    switch (sensitivity) {
    case Sensitivity::Base:
        return UCOL_PRIMARY;
    case Sensitivity::Accent:
        return UCOL_SECONDARY;
    case Sensitivity::Case:
        return UCOL_PRIMARY;
    case Sensitivity::Variant:
        return UCOL_TERTIARY;
    }
    VERIFY_NOT_REACHED();
}

static Sensitivity sensitivity_for_collator(icu::Collator const& collator)
{
    UErrorCode status = U_ZERO_ERROR;

    auto attribute = collator.getAttribute(UCOL_STRENGTH, status);
    verify_icu_success(status);

    switch (attribute) {
    case UCOL_PRIMARY:
        attribute = collator.getAttribute(UCOL_CASE_LEVEL, status);
        verify_icu_success(status);

        return attribute == UCOL_ON ? Sensitivity::Case : Sensitivity::Base;

    case UCOL_SECONDARY:
        return Sensitivity::Accent;

    default:
        return Sensitivity::Variant;
    }
}
#endif // !AK_OS_RINOS (icu_sensitivity / sensitivity_for_collator)

CaseFirst case_first_from_string(StringView case_first)
{
    if (case_first == "upper"sv)
        return CaseFirst::Upper;
    if (case_first == "lower"sv)
        return CaseFirst::Lower;
    if (case_first == "false"sv)
        return CaseFirst::False;
    VERIFY_NOT_REACHED();
}

StringView case_first_to_string(CaseFirst case_first)
{
    switch (case_first) {
    case CaseFirst::Upper:
        return "upper"sv;
    case CaseFirst::Lower:
        return "lower"sv;
    case CaseFirst::False:
        return "false"sv;
    }
    VERIFY_NOT_REACHED();
}

#ifdef AK_OS_RINOS

// RinOS: collation via rinicu IPC
class RinCollatorImpl : public Collator {
public:
    RinCollatorImpl(rin_icu_handle_t handle, Sensitivity sensitivity, bool ignore_punct)
        : m_handle(handle)
        , m_sensitivity(sensitivity)
        , m_ignore_punctuation(ignore_punct)
    {
    }

    virtual ~RinCollatorImpl() override
    {
        rin_icu_collator_destroy(&Unicode::rin_icu_client(), m_handle);
    }

    virtual Collator::Order compare(Utf16View const& lhs, Utf16View const& rhs) const override
    {
        auto lhs_utf8 = MUST(lhs.to_utf8());
        auto rhs_utf8 = MUST(rhs.to_utf8());
        int32_t result = 0;

        if (rin_icu_collator_compare(&Unicode::rin_icu_client(), m_handle,
                lhs_utf8.bytes_as_string_view().characters_without_null_termination(),
                rhs_utf8.bytes_as_string_view().characters_without_null_termination(),
                &result)
            != 0)
            return Order::Equal;

        if (result < 0)
            return Order::Before;
        if (result > 0)
            return Order::After;
        return Order::Equal;
    }

    virtual Sensitivity sensitivity() const override { return m_sensitivity; }
    virtual bool ignore_punctuation() const override { return m_ignore_punctuation; }

private:
    rin_icu_handle_t m_handle;
    Sensitivity m_sensitivity;
    bool m_ignore_punctuation;
};

static uint32_t rin_icu_strength(Sensitivity s)
{
    switch (s) {
    case Sensitivity::Base:
        return 1; // RIN_ICU_COLLATION_PRIMARY
    case Sensitivity::Accent:
        return 2; // RIN_ICU_COLLATION_SECONDARY
    case Sensitivity::Case:
        return 1; // primary + case_first
    case Sensitivity::Variant:
        return 3; // RIN_ICU_COLLATION_TERTIARY
    }
    return 3;
}

NonnullOwnPtr<Collator> Collator::create(
    StringView locale,
    Usage,
    StringView,
    Optional<Sensitivity> sensitivity,
    CaseFirst case_first,
    bool numeric,
    Optional<bool> ignore_punctuation)
{
    if (!sensitivity.has_value())
        sensitivity = Sensitivity::Variant;
    if (!ignore_punctuation.has_value())
        ignore_punctuation = false;

    char locale_buf[128];
    auto n = locale.length() < sizeof(locale_buf) - 1 ? locale.length() : sizeof(locale_buf) - 1;
    __builtin_memcpy(locale_buf, locale.characters_without_null_termination(), n);
    locale_buf[n] = '\0';

    rin_icu_collator_options_t opts = {};
    opts.strength = rin_icu_strength(*sensitivity);
    opts.case_first = (case_first == CaseFirst::Upper) ? 1 : (case_first == CaseFirst::Lower) ? 2
                                                                                               : 0;
    opts.numeric = numeric ? 1 : 0;
    opts.ignore_punctuation = *ignore_punctuation ? 1 : 0;

    rin_icu_handle_t handle = 0;
    rin_icu_collator_create(&rin_icu_client(), locale_buf, &opts, &handle);

    return adopt_own(*new RinCollatorImpl(handle, *sensitivity, *ignore_punctuation));
}

#else // !AK_OS_RINOS

static constexpr UColAttributeValue icu_case_first(CaseFirst case_first)
{
    switch (case_first) {
    case CaseFirst::Upper:
        return UCOL_UPPER_FIRST;
    case CaseFirst::Lower:
        return UCOL_LOWER_FIRST;
    case CaseFirst::False:
        return UCOL_OFF;
    }
    VERIFY_NOT_REACHED();
}

static bool ignore_punctuation_for_collator(icu::Collator const& collator)
{
    UErrorCode status = U_ZERO_ERROR;

    auto attribute = collator.getAttribute(UCOL_ALTERNATE_HANDLING, status);
    verify_icu_success(status);

    return attribute == UCOL_SHIFTED;
}

class CollatorImpl : public Collator {
public:
    explicit CollatorImpl(NonnullOwnPtr<icu::Collator> collator)
        : m_collator(move(collator))
    {
    }

    virtual Collator::Order compare(Utf16View const& lhs, Utf16View const& rhs) const override
    {
        UErrorCode status = U_ZERO_ERROR;

        auto lhs_it = icu_string_iterator(lhs);
        auto rhs_it = icu_string_iterator(rhs);

        auto result = m_collator->compare(lhs_it, rhs_it, status);
        verify_icu_success(status);

        switch (result) {
        case UCOL_LESS:
            return Order::Before;
        case UCOL_EQUAL:
            return Order::Equal;
        case UCOL_GREATER:
            return Order::After;
        }

        VERIFY_NOT_REACHED();
    }

    virtual Sensitivity sensitivity() const override
    {
        return sensitivity_for_collator(*m_collator);
    }

    virtual bool ignore_punctuation() const override
    {
        return ignore_punctuation_for_collator(*m_collator);
    }

private:
    NonnullOwnPtr<icu::Collator> m_collator;
};

NonnullOwnPtr<Collator> Collator::create(
    StringView locale,
    Usage usage,
    StringView collation,
    Optional<Sensitivity> sensitivity,
    CaseFirst case_first,
    bool numeric,
    Optional<bool> ignore_punctuation)
{
    UErrorCode status = U_ZERO_ERROR;

    auto locale_data = LocaleData::for_locale(locale);
    VERIFY(locale_data.has_value());

    auto locale_with_usage = apply_usage_to_locale(locale_data->locale(), usage, collation);

    auto collator = adopt_own(*icu::Collator::createInstance(*locale_with_usage, status));
    verify_icu_success(status);

    auto set_attribute = [&](UColAttribute attribute, UColAttributeValue value) {
        collator->setAttribute(attribute, value, status);
        verify_icu_success(status);
    };

    if (!sensitivity.has_value())
        sensitivity = sensitivity_for_collator(*collator);

    if (!ignore_punctuation.has_value())
        ignore_punctuation = ignore_punctuation_for_collator(*collator);

    set_attribute(UCOL_STRENGTH, icu_sensitivity(*sensitivity));
    set_attribute(UCOL_CASE_LEVEL, sensitivity == Sensitivity::Case ? UCOL_ON : UCOL_OFF);
    set_attribute(UCOL_CASE_FIRST, icu_case_first(case_first));
    set_attribute(UCOL_NUMERIC_COLLATION, numeric ? UCOL_ON : UCOL_OFF);
    set_attribute(UCOL_ALTERNATE_HANDLING, *ignore_punctuation ? UCOL_SHIFTED : UCOL_NON_IGNORABLE);
    set_attribute(UCOL_NORMALIZATION_MODE, UCOL_ON);

    return adopt_own(*new CollatorImpl(move(collator)));
}

#endif // !AK_OS_RINOS

}
