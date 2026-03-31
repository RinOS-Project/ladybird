/*
 * Copyright (c) 2024-2025, Tim Flynn <trflynn89@ladybird.org>
 *
 * SPDX-License-Identifier: BSD-2-Clause
 */

#include <AK/NonnullOwnPtr.h>
#include <LibUnicode/ICU.h>
#include <LibUnicode/ListFormat.h>

#ifndef AK_OS_RINOS
#include <unicode/listformatter.h>
#else
#include <LibUnicode/RinICUBridge.h>
#endif

namespace Unicode {

ListFormatType list_format_type_from_string(StringView list_format_type)
{
    if (list_format_type == "conjunction"sv)
        return ListFormatType::Conjunction;
    if (list_format_type == "disjunction"sv)
        return ListFormatType::Disjunction;
    if (list_format_type == "unit"sv)
        return ListFormatType::Unit;
    VERIFY_NOT_REACHED();
}

StringView list_format_type_to_string(ListFormatType list_format_type)
{
    switch (list_format_type) {
    case ListFormatType::Conjunction:
        return "conjunction"sv;
    case ListFormatType::Disjunction:
        return "disjunction"sv;
    case ListFormatType::Unit:
        return "unit"sv;
    }
    VERIFY_NOT_REACHED();
}

#ifndef AK_OS_RINOS
static constexpr UListFormatterType icu_list_format_type(ListFormatType type)
{
    switch (type) {
    case ListFormatType::Conjunction:
        return ULISTFMT_TYPE_AND;
    case ListFormatType::Disjunction:
        return ULISTFMT_TYPE_OR;
    case ListFormatType::Unit:
        return ULISTFMT_TYPE_UNITS;
    }
    VERIFY_NOT_REACHED();
}

static constexpr UListFormatterWidth icu_list_format_width(Style style)
{
    switch (style) {
    case Style::Long:
        return ULISTFMT_WIDTH_WIDE;
    case Style::Short:
        return ULISTFMT_WIDTH_SHORT;
    case Style::Narrow:
        return ULISTFMT_WIDTH_NARROW;
    }
    VERIFY_NOT_REACHED();
}

static constexpr StringView icu_list_format_field_to_string(i32 field)
{
    switch (field) {
    case ULISTFMT_LITERAL_FIELD:
        return "literal"sv;
    case ULISTFMT_ELEMENT_FIELD:
        return "element"sv;
    }
    VERIFY_NOT_REACHED();
}

class ListFormatImpl : public ListFormat {
public:
    ListFormatImpl(NonnullOwnPtr<icu::ListFormatter> formatter)
        : m_formatter(move(formatter))
    {
    }

    virtual ~ListFormatImpl() override = default;

    virtual Utf16String format(ReadonlySpan<Utf16String> list) const override
    {
        UErrorCode status = U_ZERO_ERROR;

        auto formatted = format_list_impl(list);
        if (!formatted.has_value())
            return {};

        auto formatted_string = formatted->toTempString(status);
        if (icu_failure(status))
            return {};

        return icu_string_to_utf16_string(formatted_string);
    }

    virtual Vector<Partition> format_to_parts(ReadonlySpan<Utf16String> list) const override
    {
        UErrorCode status = U_ZERO_ERROR;

        auto formatted = format_list_impl(list);
        if (!formatted.has_value())
            return {};

        auto formatted_string = formatted->toTempString(status);
        if (icu_failure(status))
            return {};

        Vector<Partition> result;

        icu::ConstrainedFieldPosition position;
        position.constrainCategory(UFIELD_CATEGORY_LIST);

        while (static_cast<bool>(formatted->nextPosition(position, status)) && icu_success(status)) {
            auto type = icu_list_format_field_to_string(position.getField());
            auto part = formatted_string.tempSubStringBetween(position.getStart(), position.getLimit());

            result.empend(type, icu_string_to_utf16_string(part));
        }

        return result;
    }

private:
    Optional<icu::FormattedList> format_list_impl(ReadonlySpan<Utf16String> list) const
    {
        UErrorCode status = U_ZERO_ERROR;

        auto icu_list = icu_string_list(list);

        auto formatted_list = m_formatter->formatStringsToValue(icu_list.data(), static_cast<i32>(icu_list.size()), status);
        if (icu_failure(status))
            return {};

        return formatted_list;
    }

    NonnullOwnPtr<icu::ListFormatter> m_formatter;
};

NonnullOwnPtr<ListFormat> ListFormat::create(StringView locale, ListFormatType type, Style style)
{
    UErrorCode status = U_ZERO_ERROR;

    auto locale_data = LocaleData::for_locale(locale);
    VERIFY(locale_data.has_value());

    auto formatter = adopt_own(*icu::ListFormatter::createInstance(locale_data->locale(), icu_list_format_type(type), icu_list_format_width(style), status));
    verify_icu_success(status);

    return adopt_own(*new ListFormatImpl(move(formatter)));
}

#else // AK_OS_RINOS

// RinOS: list formatting via rinicu IPC
class RinListFormatImpl : public ListFormat {
public:
    RinListFormatImpl(String locale, ListFormatType type, Style style)
        : m_locale(move(locale))
        , m_type(type)
        , m_style(style)
    {
    }

    virtual ~RinListFormatImpl() override = default;

    virtual Utf16String format(ReadonlySpan<Utf16String> list) const override
    {
        auto items = build_items(list);
        uint32_t rin_type = (m_type == ListFormatType::Conjunction) ? 1 : (m_type == ListFormatType::Disjunction) ? 2 : 3;
        ByteString locale_z(m_locale);
        char buf[4096];
        size_t len = 0;

        // Build a null-separated list of items for rinicu
        StringBuilder items_buf;
        for (size_t i = 0; i < items.size(); ++i) {
            if (i > 0)
                items_buf.append('\0');
            items_buf.append(items[i]);
        }
        auto items_str = MUST(items_buf.to_string());

        if (rin_icu_list_format(&rin_icu_client(), locale_z.characters(), rin_type, items_str.bytes_as_string_view().characters_without_null_termination(), items_str.byte_count(), static_cast<uint32_t>(items.size()), buf, sizeof(buf), &len) == 0 && len > 0)
            return Utf16String::from_utf8(StringView { buf, len });

        // Fallback: comma-separated
        StringBuilder fallback;
        for (size_t i = 0; i < items.size(); ++i) {
            if (i > 0)
                fallback.append(", "sv);
            fallback.append(items[i]);
        }
        return Utf16String::from_utf8(MUST(fallback.to_string()));
    }

    virtual Vector<Partition> format_to_parts(ReadonlySpan<Utf16String> list) const override
    {
        auto formatted = format(list);
        Vector<Partition> result;
        Partition part;
        part.type = "element"sv;
        part.value = move(formatted);
        result.append(move(part));
        return result;
    }

private:
    static Vector<ByteString> build_items(ReadonlySpan<Utf16String> list)
    {
        Vector<ByteString> items;
        for (auto const& item : list)
            items.append(ByteString(MUST(item.to_utf8())));
        return items;
    }

    String m_locale;
    ListFormatType m_type;
    Style m_style;
};

NonnullOwnPtr<ListFormat> ListFormat::create(StringView locale, ListFormatType type, Style style)
{
    return adopt_own(*new RinListFormatImpl(MUST(String::from_utf8(locale)), type, style));
}

#endif // AK_OS_RINOS

}
