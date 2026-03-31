/*
 * Copyright (c) 2025, Tim Flynn <trflynn89@ladybird.org>
 *
 * SPDX-License-Identifier: BSD-2-Clause
 */

#include <AK/Utf16String.h>
#include <LibUnicode/ICU.h>

#ifndef AK_OS_RINOS
#include <unicode/stringoptions.h>
#include <unicode/translit.h>
#include <unicode/unistr.h>
#else
extern "C" {
#include <rin_unicode.h>
}
#endif

// This file contains definitions of AK::Utf16String methods which require UCD data.

namespace AK {

#ifdef AK_OS_RINOS

// RinOS: case operations via libunicode codepoint-level conversion, then back to Utf16
Utf16String Utf16String::to_lowercase(Optional<StringView> const&) const
{
    if (has_ascii_storage())
        return to_ascii_lowercase();
    auto utf8 = MUST(to_utf8());
    auto lowered = MUST(utf8.to_lowercase());
    return Utf16String::from_utf8(lowered);
}

Utf16String Utf16String::to_uppercase(Optional<StringView> const&) const
{
    if (has_ascii_storage())
        return to_ascii_uppercase();
    auto utf8 = MUST(to_utf8());
    auto uppered = MUST(utf8.to_uppercase());
    return Utf16String::from_utf8(uppered);
}

Utf16String Utf16String::to_titlecase(Optional<StringView> const& locale, TrailingCodePointTransformation trailing_code_point_transformation) const
{
    auto utf8 = MUST(to_utf8());
    auto titled = MUST(utf8.to_titlecase(locale, trailing_code_point_transformation));
    return Utf16String::from_utf8(titled);
}

Utf16String Utf16String::to_casefold() const
{
    auto utf8 = MUST(to_utf8());
    auto folded = MUST(utf8.to_casefold());
    return Utf16String::from_utf8(folded);
}

Utf16String Utf16String::to_fullwidth() const
{
    auto utf8 = MUST(to_utf8());
    auto wide = MUST(utf8.to_fullwidth());
    return Utf16String::from_utf8(wide);
}

#else // !AK_OS_RINOS

Utf16String Utf16String::to_lowercase(Optional<StringView> const& locale) const
{
    if (has_ascii_storage() && !locale.has_value())
        return to_ascii_lowercase();

    Optional<Unicode::LocaleData&> locale_data;
    if (locale.has_value())
        locale_data = Unicode::LocaleData::for_locale(*locale);

    auto icu_string = Unicode::icu_string(*this);
    locale_data.has_value() ? icu_string.toLower(locale_data->locale()) : icu_string.toLower();

    return Unicode::icu_string_to_utf16_string(icu_string);
}

Utf16String Utf16String::to_uppercase(Optional<StringView> const& locale) const
{
    if (has_ascii_storage() && !locale.has_value())
        return to_ascii_uppercase();

    Optional<Unicode::LocaleData&> locale_data;
    if (locale.has_value())
        locale_data = Unicode::LocaleData::for_locale(*locale);

    auto icu_string = Unicode::icu_string(*this);
    locale_data.has_value() ? icu_string.toUpper(locale_data->locale()) : icu_string.toUpper();

    return Unicode::icu_string_to_utf16_string(icu_string);
}

Utf16String Utf16String::to_titlecase(Optional<StringView> const& locale, TrailingCodePointTransformation trailing_code_point_transformation) const
{
    Optional<Unicode::LocaleData&> locale_data;
    if (locale.has_value())
        locale_data = Unicode::LocaleData::for_locale(*locale);

    u32 options = 0;
    if (trailing_code_point_transformation == TrailingCodePointTransformation::PreserveExisting)
        options |= U_TITLECASE_NO_LOWERCASE;

    auto icu_string = Unicode::icu_string(*this);
    locale_data.has_value()
        ? icu_string.toTitle(nullptr, locale_data->locale(), options)
        : icu_string.toTitle(nullptr, icu::Locale::getDefault(), options);

    return Unicode::icu_string_to_utf16_string(icu_string);
}

Utf16String Utf16String::to_casefold() const
{
    auto icu_string = Unicode::icu_string(*this);
    icu_string.foldCase();

    return Unicode::icu_string_to_utf16_string(icu_string);
}

Utf16String Utf16String::to_fullwidth() const
{
    UErrorCode status = U_ZERO_ERROR;

    auto const transliterator = adopt_own_if_nonnull(icu::Transliterator::createInstance("Halfwidth-Fullwidth", UTRANS_FORWARD, status));
    VERIFY(Unicode::icu_success(status));

    auto icu_string = Unicode::icu_string(*this);
    transliterator->transliterate(icu_string);

    return Unicode::icu_string_to_utf16_string(icu_string);
}

#endif // !AK_OS_RINOS

}
