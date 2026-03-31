/*
 * Copyright (c) 2021-2024, Tim Flynn <trflynn89@serenityos.org>
 *
 * SPDX-License-Identifier: BSD-2-Clause
 */

#include <AK/Array.h>
#include <AK/CharacterTypes.h>
#include <AK/Find.h>
#include <AK/HashMap.h>
#include <AK/NonnullOwnPtr.h>
#include <AK/Traits.h>
#include <LibUnicode/CharacterTypes.h>
#include <LibUnicode/ICU.h>

#ifndef AK_OS_RINOS
#    include <unicode/uchar.h>
#    include <unicode/uniset.h>
#    include <unicode/uscript.h>
#    include <unicode/uset.h>
#else
#    include <LibUnicode/RinICUBridge.h>
#    include <AK/CharacterTypes.h>
#endif

namespace Unicode {

#ifdef AK_OS_RINOS

// RinOS: Simplified Unicode character property implementation
// Uses libunicode for basic classification + hardcoded property name tables

// GeneralCategory constants (matching ICU U_CHAR_CATEGORY_COUNT layout)
static constexpr GeneralCategory GC_UNASSIGNED = 0;
static constexpr GeneralCategory GC_UPPERCASE_LETTER = 1;
static constexpr GeneralCategory GC_LOWERCASE_LETTER = 2;
static constexpr GeneralCategory GC_TITLECASE_LETTER = 3;
static constexpr GeneralCategory GC_MODIFIER_LETTER = 4;
static constexpr GeneralCategory GC_OTHER_LETTER = 5;
static constexpr GeneralCategory GC_NON_SPACING_MARK = 6;
static constexpr GeneralCategory GC_ENCLOSING_MARK = 7;
static constexpr GeneralCategory GC_COMBINING_SPACING_MARK = 8;
static constexpr GeneralCategory GC_DECIMAL_DIGIT_NUMBER = 9;
static constexpr GeneralCategory GC_LETTER_NUMBER = 10;
static constexpr GeneralCategory GC_OTHER_NUMBER = 11;
static constexpr GeneralCategory GC_SPACE_SEPARATOR = 12;
static constexpr GeneralCategory GC_LINE_SEPARATOR = 13;
static constexpr GeneralCategory GC_PARAGRAPH_SEPARATOR = 14;
static constexpr GeneralCategory GC_CONTROL_CHAR = 15;
static constexpr GeneralCategory GC_FORMAT_CHAR = 16;
static constexpr GeneralCategory GC_PRIVATE_USE_CHAR = 17;
static constexpr GeneralCategory GC_SURROGATE = 18;
static constexpr GeneralCategory GC_DASH_PUNCTUATION = 19;
static constexpr GeneralCategory GC_START_PUNCTUATION = 20;
static constexpr GeneralCategory GC_END_PUNCTUATION = 21;
static constexpr GeneralCategory GC_CONNECTOR_PUNCTUATION = 22;
static constexpr GeneralCategory GC_OTHER_PUNCTUATION = 23;
static constexpr GeneralCategory GC_MATH_SYMBOL = 24;
static constexpr GeneralCategory GC_CURRENCY_SYMBOL = 25;
static constexpr GeneralCategory GC_MODIFIER_SYMBOL = 26;
static constexpr GeneralCategory GC_OTHER_SYMBOL = 27;
static constexpr GeneralCategory GC_INITIAL_PUNCTUATION = 28;
static constexpr GeneralCategory GC_FINAL_PUNCTUATION = 29;
static constexpr GeneralCategory GC_CHAR_CATEGORY_COUNT = 30;

static constexpr GeneralCategory GENERAL_CATEGORY_CASED_LETTER = GC_CHAR_CATEGORY_COUNT + 1;
static constexpr GeneralCategory GENERAL_CATEGORY_LETTER = GC_CHAR_CATEGORY_COUNT + 2;
static constexpr GeneralCategory GENERAL_CATEGORY_MARK = GC_CHAR_CATEGORY_COUNT + 3;
static constexpr GeneralCategory GENERAL_CATEGORY_NUMBER = GC_CHAR_CATEGORY_COUNT + 4;
static constexpr GeneralCategory GENERAL_CATEGORY_PUNCTUATION = GC_CHAR_CATEGORY_COUNT + 5;
static constexpr GeneralCategory GENERAL_CATEGORY_SYMBOL = GC_CHAR_CATEGORY_COUNT + 6;
static constexpr GeneralCategory GENERAL_CATEGORY_SEPARATOR = GC_CHAR_CATEGORY_COUNT + 7;
static constexpr GeneralCategory GENERAL_CATEGORY_OTHER = GC_CHAR_CATEGORY_COUNT + 8;
static constexpr GeneralCategory GENERAL_CATEGORY_LIMIT = GC_CHAR_CATEGORY_COUNT + 9;

struct GCNameEntry {
    StringView long_name;
    StringView short_name;
};

// Simplified general category classification using libunicode
static GeneralCategory classify_code_point(u32 cp)
{
    if (cp <= 0x1F || (cp >= 0x7F && cp <= 0x9F))
        return GC_CONTROL_CHAR;
    if (rin_unicode_isdigit(cp))
        return GC_DECIMAL_DIGIT_NUMBER;
    if (rin_unicode_isupper(cp))
        return GC_UPPERCASE_LETTER;
    if (rin_unicode_islower(cp))
        return GC_LOWERCASE_LETTER;
    if (rin_unicode_isalpha(cp))
        return GC_OTHER_LETTER;
    if (rin_unicode_isspace(cp)) {
        if (cp == 0x2028) return GC_LINE_SEPARATOR;
        if (cp == 0x2029) return GC_PARAGRAPH_SEPARATOR;
        return GC_SPACE_SEPARATOR;
    }
    if (rin_unicode_ispunct(cp)) {
        if (cp == '-' || cp == 0x2010 || cp == 0x2011 || cp == 0x2012 || cp == 0x2013 || cp == 0x2014 || cp == 0x2015)
            return GC_DASH_PUNCTUATION;
        if (cp == '(' || cp == '[' || cp == '{')
            return GC_START_PUNCTUATION;
        if (cp == ')' || cp == ']' || cp == '}')
            return GC_END_PUNCTUATION;
        if (cp == '_')
            return GC_CONNECTOR_PUNCTUATION;
        return GC_OTHER_PUNCTUATION;
    }
    if (cp == '$' || cp == 0x00A2 || cp == 0x00A3 || cp == 0x00A4 || cp == 0x00A5 || cp == 0x20AC || cp == 0x20B9)
        return GC_CURRENCY_SYMBOL;
    if (cp == '+' || cp == '<' || cp == '=' || cp == '>' || cp == '|' || cp == '~' || cp == 0x00AC || cp == 0x00B1)
        return GC_MATH_SYMBOL;
    if (cp >= 0xE000 && cp <= 0xF8FF)
        return GC_PRIVATE_USE_CHAR;
    if (cp >= 0xD800 && cp <= 0xDFFF)
        return GC_SURROGATE;
    if (cp >= 0x200B && cp <= 0x200F)
        return GC_FORMAT_CHAR;
    return GC_UNASSIGNED;
}

static bool category_matches(GeneralCategory actual, GeneralCategory expected)
{
    if (actual == expected)
        return true;
    // Composite categories
    if (expected == GENERAL_CATEGORY_CASED_LETTER)
        return actual == GC_UPPERCASE_LETTER || actual == GC_LOWERCASE_LETTER || actual == GC_TITLECASE_LETTER;
    if (expected == GENERAL_CATEGORY_LETTER)
        return actual >= GC_UPPERCASE_LETTER && actual <= GC_OTHER_LETTER;
    if (expected == GENERAL_CATEGORY_MARK)
        return actual >= GC_NON_SPACING_MARK && actual <= GC_COMBINING_SPACING_MARK;
    if (expected == GENERAL_CATEGORY_NUMBER)
        return actual >= GC_DECIMAL_DIGIT_NUMBER && actual <= GC_OTHER_NUMBER;
    if (expected == GENERAL_CATEGORY_PUNCTUATION)
        return actual == GC_DASH_PUNCTUATION || actual == GC_START_PUNCTUATION || actual == GC_END_PUNCTUATION
            || actual == GC_CONNECTOR_PUNCTUATION || actual == GC_OTHER_PUNCTUATION
            || actual == GC_INITIAL_PUNCTUATION || actual == GC_FINAL_PUNCTUATION;
    if (expected == GENERAL_CATEGORY_SYMBOL)
        return actual >= GC_MATH_SYMBOL && actual <= GC_OTHER_SYMBOL;
    if (expected == GENERAL_CATEGORY_SEPARATOR)
        return actual >= GC_SPACE_SEPARATOR && actual <= GC_PARAGRAPH_SEPARATOR;
    if (expected == GENERAL_CATEGORY_OTHER)
        return actual == GC_CONTROL_CHAR || actual == GC_FORMAT_CHAR || actual == GC_PRIVATE_USE_CHAR
            || actual == GC_SURROGATE || actual == GC_UNASSIGNED;
    return false;
}

Optional<GeneralCategory> general_category_from_string(StringView general_category)
{
    // Hardcoded GC name table (long_name, short_name) indexed by GeneralCategory value
    static constexpr GCNameEntry gc_names[] = {
        { "Unassigned"sv, "Cn"sv },           // 0
        { "Uppercase_Letter"sv, "Lu"sv },      // 1
        { "Lowercase_Letter"sv, "Ll"sv },      // 2
        { "Titlecase_Letter"sv, "Lt"sv },      // 3
        { "Modifier_Letter"sv, "Lm"sv },       // 4
        { "Other_Letter"sv, "Lo"sv },          // 5
        { "Nonspacing_Mark"sv, "Mn"sv },       // 6
        { "Enclosing_Mark"sv, "Me"sv },        // 7
        { "Spacing_Mark"sv, "Mc"sv },          // 8
        { "Decimal_Number"sv, "Nd"sv },        // 9
        { "Letter_Number"sv, "Nl"sv },         // 10
        { "Other_Number"sv, "No"sv },          // 11
        { "Space_Separator"sv, "Zs"sv },       // 12
        { "Line_Separator"sv, "Zl"sv },        // 13
        { "Paragraph_Separator"sv, "Zp"sv },   // 14
        { "Control"sv, "Cc"sv },               // 15
        { "Format"sv, "Cf"sv },                // 16
        { "Private_Use"sv, "Co"sv },           // 17
        { "Surrogate"sv, "Cs"sv },             // 18
        { "Dash_Punctuation"sv, "Pd"sv },      // 19
        { "Open_Punctuation"sv, "Ps"sv },      // 20
        { "Close_Punctuation"sv, "Pe"sv },     // 21
        { "Connector_Punctuation"sv, "Pc"sv }, // 22
        { "Other_Punctuation"sv, "Po"sv },     // 23
        { "Math_Symbol"sv, "Sm"sv },           // 24
        { "Currency_Symbol"sv, "Sc"sv },       // 25
        { "Modifier_Symbol"sv, "Sk"sv },       // 26
        { "Other_Symbol"sv, "So"sv },          // 27
        { "Initial_Punctuation"sv, "Pi"sv },   // 28
        { "Final_Punctuation"sv, "Pf"sv },     // 29
    };

    // Composite categories
    struct CompositeEntry { StringView long_name; StringView short_name; GeneralCategory category; };
    static constexpr CompositeEntry composite_names[] = {
        { "Cased_Letter"sv, "LC"sv, GENERAL_CATEGORY_CASED_LETTER },
        { "Letter"sv, "L"sv, GENERAL_CATEGORY_LETTER },
        { "Mark"sv, "M"sv, GENERAL_CATEGORY_MARK },
        { "Number"sv, "N"sv, GENERAL_CATEGORY_NUMBER },
        { "Punctuation"sv, "P"sv, GENERAL_CATEGORY_PUNCTUATION },
        { "Symbol"sv, "S"sv, GENERAL_CATEGORY_SYMBOL },
        { "Separator"sv, "Z"sv, GENERAL_CATEGORY_SEPARATOR },
        { "Other"sv, "C"sv, GENERAL_CATEGORY_OTHER },
    };

    for (size_t i = 0; i < sizeof(gc_names) / sizeof(gc_names[0]); ++i) {
        if (general_category == gc_names[i].long_name || general_category == gc_names[i].short_name)
            return static_cast<GeneralCategory>(i);
    }
    for (auto const& entry : composite_names) {
        if (general_category == entry.long_name || general_category == entry.short_name)
            return entry.category;
    }
    return {};
}

bool code_point_has_general_category(u32 code_point, GeneralCategory general_category, CaseSensitivity case_sensitivity)
{
    auto actual = classify_code_point(code_point);
    if (category_matches(actual, general_category))
        return true;

    if (case_sensitivity == CaseSensitivity::CaseInsensitive) {
        // Check case variants
        u32 lower = rin_unicode_tolower(code_point);
        u32 upper = rin_unicode_toupper(code_point);
        if (lower != code_point && category_matches(classify_code_point(lower), general_category))
            return true;
        if (upper != code_point && category_matches(classify_code_point(upper), general_category))
            return true;
    }
    return false;
}

bool code_point_is_printable(u32 code_point)
{
    if (code_point < 0x20) return false;
    if (code_point == 0x7F) return false;
    if (code_point >= 0x80 && code_point <= 0x9F) return false;
    auto gc = classify_code_point(code_point);
    return gc != GC_CONTROL_CHAR && gc != GC_SURROGATE && gc != GC_UNASSIGNED;
}

bool code_point_has_control_general_category(u32 code_point)
{
    return code_point_has_general_category(code_point, GC_CONTROL_CHAR);
}

bool code_point_has_letter_general_category(u32 code_point)
{
    return code_point_has_general_category(code_point, GENERAL_CATEGORY_LETTER);
}

bool code_point_has_mark_general_category(u32 code_point)
{
    return code_point_has_general_category(code_point, GENERAL_CATEGORY_MARK);
}

bool code_point_has_number_general_category(u32 code_point)
{
    return code_point_has_general_category(code_point, GENERAL_CATEGORY_NUMBER);
}

bool code_point_has_punctuation_general_category(u32 code_point)
{
    return code_point_has_general_category(code_point, GENERAL_CATEGORY_PUNCTUATION);
}

bool code_point_has_separator_general_category(u32 code_point)
{
    return code_point_has_general_category(code_point, GENERAL_CATEGORY_SEPARATOR);
}

bool code_point_has_space_separator_general_category(u32 code_point)
{
    return code_point_has_general_category(code_point, GC_SPACE_SEPARATOR);
}

bool code_point_has_symbol_general_category(u32 code_point)
{
    return code_point_has_general_category(code_point, GENERAL_CATEGORY_SYMBOL);
}

// Property constants for RinOS (matching ICU UCHAR_BINARY_LIMIT layout)
static constexpr Property PROPERTY_ANY = 57 + 1;   // UCHAR_BINARY_LIMIT ~ 57 in ICU
static constexpr Property PROPERTY_ASCII = 57 + 2;
static constexpr Property PROPERTY_ASSIGNED = 57 + 3;
static constexpr Property PROPERTY_LIMIT = 57 + 4;

// Hardcoded property name mapping
struct PropNameEntry { StringView long_name; StringView short_name; Property value; };
static constexpr PropNameEntry s_property_names[] = {
    { "Alphabetic"sv, "Alpha"sv, Property(0) },    // placeholder index
    { "ASCII_Hex_Digit"sv, "AHex"sv, Property(1) },
    { "Bidi_Control"sv, "Bidi_C"sv, Property(2) },
    { "Bidi_Mirrored"sv, "Bidi_M"sv, Property(3) },
    { "Case_Ignorable"sv, "CI"sv, Property(4) },
    { "Cased"sv, "Cased"sv, Property(5) },
    { "Changes_When_Casefolded"sv, "CWCF"sv, Property(6) },
    { "Changes_When_Casemapped"sv, "CWCM"sv, Property(7) },
    { "Changes_When_Lowercased"sv, "CWL"sv, Property(8) },
    { "Changes_When_NFKC_Casefolded"sv, "CWKCF"sv, Property(9) },
    { "Changes_When_Titlecased"sv, "CWT"sv, Property(10) },
    { "Changes_When_Uppercased"sv, "CWU"sv, Property(11) },
    { "Dash"sv, "Dash"sv, Property(12) },
    { "Default_Ignorable_Code_Point"sv, "DI"sv, Property(13) },
    { "Deprecated"sv, "Dep"sv, Property(14) },
    { "Diacritic"sv, "Dia"sv, Property(15) },
    { "Emoji"sv, "Emoji"sv, Property(16) },
    { "Emoji_Component"sv, "EComp"sv, Property(17) },
    { "Emoji_Modifier"sv, "EMod"sv, Property(18) },
    { "Emoji_Modifier_Base"sv, "EBase"sv, Property(19) },
    { "Emoji_Presentation"sv, "EPres"sv, Property(20) },
    { "Extended_Pictographic"sv, "ExtPict"sv, Property(21) },
    { "Extender"sv, "Ext"sv, Property(22) },
    { "Grapheme_Base"sv, "Gr_Base"sv, Property(23) },
    { "Grapheme_Extend"sv, "Gr_Ext"sv, Property(24) },
    { "Hex_Digit"sv, "Hex"sv, Property(25) },
    { "IDS_Binary_Operator"sv, "IDSB"sv, Property(26) },
    { "IDS_Trinary_Operator"sv, "IDST"sv, Property(27) },
    { "ID_Continue"sv, "IDC"sv, Property(28) },
    { "ID_Start"sv, "IDS"sv, Property(29) },
    { "Ideographic"sv, "Ideo"sv, Property(30) },
    { "Join_Control"sv, "Join_C"sv, Property(31) },
    { "Logical_Order_Exception"sv, "LOE"sv, Property(32) },
    { "Lowercase"sv, "Lower"sv, Property(33) },
    { "Math"sv, "Math"sv, Property(34) },
    { "Noncharacter_Code_Point"sv, "NChar"sv, Property(35) },
    { "Pattern_Syntax"sv, "Pat_Syn"sv, Property(36) },
    { "Pattern_White_Space"sv, "Pat_WS"sv, Property(37) },
    { "Quotation_Mark"sv, "QMark"sv, Property(38) },
    { "Radical"sv, "Radical"sv, Property(39) },
    { "Regional_Indicator"sv, "RI"sv, Property(40) },
    { "Sentence_Terminal"sv, "STerm"sv, Property(41) },
    { "Soft_Dotted"sv, "SD"sv, Property(42) },
    { "Terminal_Punctuation"sv, "Term"sv, Property(43) },
    { "Unified_Ideograph"sv, "UIdeo"sv, Property(44) },
    { "Uppercase"sv, "Upper"sv, Property(45) },
    { "Variation_Selector"sv, "VS"sv, Property(46) },
    { "White_Space"sv, "WSpace"sv, Property(47) },
    { "XID_Continue"sv, "XIDC"sv, Property(48) },
    { "XID_Start"sv, "XIDS"sv, Property(49) },
    // String properties for ECMA-262
    { "Basic_Emoji"sv, "Basic_Emoji"sv, Property(50) },
    { "Emoji_Keycap_Sequence"sv, "EKS"sv, Property(51) },
    { "RGI_Emoji"sv, "RGI_Emoji"sv, Property(52) },
    { "RGI_Emoji_Flag_Sequence"sv, "RGIEFS"sv, Property(53) },
    { "RGI_Emoji_Tag_Sequence"sv, "RGIETS"sv, Property(54) },
    { "RGI_Emoji_Modifier_Sequence"sv, "RGIEMS"sv, Property(55) },
    { "RGI_Emoji_ZWJ_Sequence"sv, "RGIEZS"sv, Property(56) },
};

Optional<Property> property_from_string(StringView property)
{
    if (property == "Any"sv)
        return PROPERTY_ANY;
    if (property == "ASCII"sv)
        return PROPERTY_ASCII;
    if (property == "Assigned"sv)
        return PROPERTY_ASSIGNED;
    for (auto const& entry : s_property_names) {
        if (property == entry.long_name || property == entry.short_name)
            return entry.value;
    }
    return {};
}

// Simplified property check using libunicode
static bool check_binary_property_simple(u32 cp, Property property)
{
    // Alphabetic
    if (property == Property(0))
        return rin_unicode_isalpha(cp);
    // Lowercase
    if (property == Property(33))
        return rin_unicode_islower(cp);
    // Uppercase
    if (property == Property(45))
        return rin_unicode_isupper(cp);
    // White_Space
    if (property == Property(47))
        return rin_unicode_isspace(cp);
    // Dash
    if (property == Property(12))
        return cp == '-' || cp == 0x2010 || cp == 0x2011 || cp == 0x2012 || cp == 0x2013 || cp == 0x2014 || cp == 0x2015 || cp == 0x2212 || cp == 0xFE58 || cp == 0xFE63 || cp == 0xFF0D;
    // Hex_Digit
    if (property == Property(25))
        return is_ascii_hex_digit(cp) || (cp >= 0xFF10 && cp <= 0xFF19) || (cp >= 0xFF21 && cp <= 0xFF26) || (cp >= 0xFF41 && cp <= 0xFF46);
    // ASCII_Hex_Digit
    if (property == Property(1))
        return is_ascii_hex_digit(cp);
    // ID_Start
    if (property == Property(29))
        return rin_unicode_isalpha(cp) || cp == '_' || cp == '$';
    // ID_Continue
    if (property == Property(28))
        return rin_unicode_isalpha(cp) || rin_unicode_isdigit(cp) || cp == '_' || cp == '$' || cp == 0x200C || cp == 0x200D;
    // XID_Start
    if (property == Property(49))
        return rin_unicode_isalpha(cp) || cp == '_';
    // XID_Continue
    if (property == Property(48))
        return rin_unicode_isalpha(cp) || rin_unicode_isdigit(cp) || cp == '_' || cp == 0x200C || cp == 0x200D;
    // Emoji (basic range)
    if (property == Property(16))
        return (cp >= 0x1F600 && cp <= 0x1F64F) || (cp >= 0x1F300 && cp <= 0x1F5FF) || (cp >= 0x1F680 && cp <= 0x1F6FF)
            || (cp >= 0x1F900 && cp <= 0x1F9FF) || (cp >= 0x2600 && cp <= 0x26FF) || (cp >= 0x2700 && cp <= 0x27BF)
            || (cp >= 0x231A && cp <= 0x231B) || cp == 0x23F0 || cp == 0x23F3
            || (cp >= 0xFE00 && cp <= 0xFE0F) || (cp >= 0x1FA00 && cp <= 0x1FA6F) || (cp >= 0x1FA70 && cp <= 0x1FAFF);
    // Emoji_Presentation
    if (property == Property(20))
        return (cp >= 0x1F600 && cp <= 0x1F64F) || (cp >= 0x1F300 && cp <= 0x1F5FF) || (cp >= 0x1F680 && cp <= 0x1F6FF)
            || (cp >= 0x1F900 && cp <= 0x1F9FF) || (cp >= 0x1FA00 && cp <= 0x1FAFF);
    // Emoji_Modifier_Base
    if (property == Property(19))
        return (cp >= 0x1F466 && cp <= 0x1F469) || (cp >= 0x1F476 && cp <= 0x1F478) || cp == 0x1F44D || cp == 0x1F44E
            || cp == 0x270A || cp == 0x270B || cp == 0x270C || cp == 0x270D;
    // Regional_Indicator
    if (property == Property(40))
        return cp >= 0x1F1E6 && cp <= 0x1F1FF;
    // Variation_Selector
    if (property == Property(46))
        return (cp >= 0xFE00 && cp <= 0xFE0F) || (cp >= 0xE0100 && cp <= 0xE01EF);
    // Ideographic
    if (property == Property(30))
        return (cp >= 0x4E00 && cp <= 0x9FFF) || (cp >= 0x3400 && cp <= 0x4DBF) || (cp >= 0x20000 && cp <= 0x2A6DF)
            || (cp >= 0x2A700 && cp <= 0x2B739) || (cp >= 0x2B740 && cp <= 0x2B81D) || (cp >= 0x2B820 && cp <= 0x2CEA1)
            || (cp >= 0x2CEB0 && cp <= 0x2EBE0) || (cp >= 0x30000 && cp <= 0x3134A) || (cp >= 0x31350 && cp <= 0x323AF);
    // Math
    if (property == Property(34))
        return cp == '+' || cp == '<' || cp == '=' || cp == '>' || cp == '|' || cp == '~'
            || cp == 0x00AC || cp == 0x00B1 || cp == 0x00D7 || cp == 0x00F7
            || (cp >= 0x2200 && cp <= 0x22FF) || (cp >= 0x2A00 && cp <= 0x2AFF);
    // Pattern_Syntax
    if (property == Property(36))
        return (cp >= 0x21 && cp <= 0x2F) || (cp >= 0x3A && cp <= 0x40) || (cp >= 0x5B && cp <= 0x5E) || cp == 0x60
            || (cp >= 0x7B && cp <= 0x7E) || (cp >= 0x00A1 && cp <= 0x00A7) || cp == 0x00A9 || cp == 0x00AB
            || cp == 0x00AC || cp == 0x00AE || cp == 0x00B0 || cp == 0x00B1 || cp == 0x00B6
            || cp == 0x00BB || cp == 0x00BF || cp == 0x00D7 || cp == 0x00F7
            || (cp >= 0x2010 && cp <= 0x2027) || (cp >= 0x2030 && cp <= 0x203E) || (cp >= 0x2041 && cp <= 0x2053)
            || (cp >= 0x2055 && cp <= 0x205E) || (cp >= 0x2190 && cp <= 0x245F)
            || (cp >= 0x2500 && cp <= 0x2775) || (cp >= 0x2794 && cp <= 0x2BFF)
            || (cp >= 0x2E00 && cp <= 0x2E7F) || (cp >= 0x3001 && cp <= 0x3003) || (cp >= 0x3008 && cp <= 0x3020)
            || cp == 0x3030 || cp == 0xFD3E || cp == 0xFD3F || (cp >= 0xFE45 && cp <= 0xFE46);
    // Pattern_White_Space
    if (property == Property(37))
        return cp == 0x09 || cp == 0x0A || cp == 0x0B || cp == 0x0C || cp == 0x0D || cp == 0x20
            || cp == 0x85 || cp == 0x200E || cp == 0x200F || cp == 0x2028 || cp == 0x2029;
    // Noncharacter_Code_Point
    if (property == Property(35))
        return (cp >= 0xFDD0 && cp <= 0xFDEF) || (cp & 0xFFFE) == 0xFFFE;
    // Cased
    if (property == Property(5))
        return rin_unicode_isupper(cp) || rin_unicode_islower(cp);
    // Grapheme_Base
    if (property == Property(23)) {
        auto gc = classify_code_point(cp);
        return gc != GC_CONTROL_CHAR && gc != GC_FORMAT_CHAR && gc != GC_SURROGATE && gc != GC_UNASSIGNED
            && gc != GC_NON_SPACING_MARK && gc != GC_ENCLOSING_MARK && gc != GC_LINE_SEPARATOR && gc != GC_PARAGRAPH_SEPARATOR;
    }
    // Default: unknown property → false
    return false;
}

bool code_point_has_property(u32 code_point, Property property, CaseSensitivity case_sensitivity)
{
    if (property == PROPERTY_ANY)
        return is_unicode(code_point);
    if (property == PROPERTY_ASCII)
        return is_ascii(code_point);
    if (property == PROPERTY_ASSIGNED) {
        auto gc = classify_code_point(code_point);
        return gc != GC_UNASSIGNED;
    }
    if (check_binary_property_simple(code_point, property))
        return true;
    if (case_sensitivity == CaseSensitivity::CaseInsensitive) {
        u32 lower = rin_unicode_tolower(code_point);
        u32 upper = rin_unicode_toupper(code_point);
        if (lower != code_point && check_binary_property_simple(lower, property))
            return true;
        if (upper != code_point && check_binary_property_simple(upper, property))
            return true;
    }
    return false;
}

bool code_point_has_emoji_property(u32 code_point) { return check_binary_property_simple(code_point, Property(16)); }
bool code_point_has_emoji_modifier_base_property(u32 code_point) { return check_binary_property_simple(code_point, Property(19)); }
bool code_point_has_emoji_presentation_property(u32 code_point) { return check_binary_property_simple(code_point, Property(20)); }

bool code_point_has_identifier_start_property(u32 code_point)
{
    return rin_unicode_isalpha(code_point) || code_point == '_' || code_point == '$';
}

bool code_point_has_identifier_continue_property(u32 code_point)
{
    return rin_unicode_isalpha(code_point) || rin_unicode_isdigit(code_point) || code_point == '_' || code_point == '$'
        || code_point == 0x200C || code_point == 0x200D;
}

bool code_point_has_regional_indicator_property(u32 code_point) { return code_point >= 0x1F1E6 && code_point <= 0x1F1FF; }
bool code_point_has_variation_selector_property(u32 code_point) { return (code_point >= 0xFE00 && code_point <= 0xFE0F) || (code_point >= 0xE0100 && code_point <= 0xE01EF); }
bool code_point_has_white_space_property(u32 code_point) { return rin_unicode_isspace(code_point); }

bool is_ecma262_property(Property property)
{
    if (property == PROPERTY_ANY || property == PROPERTY_ASCII || property == PROPERTY_ASSIGNED)
        return true;
    // All properties in our table up to index 49 are ECMA-262 binary properties
    return property.value() <= 49;
}

bool is_ecma262_string_property(Property property)
{
    return property.value() >= 50 && property.value() <= 56;
}

Vector<String> get_property_strings(Property)
{
    // String property enumeration not available on RinOS
    return {};
}

// Script handling
static constexpr size_t SCRIPT_CODE_LIMIT = 200;

struct ScriptNameEntry { StringView long_name; StringView short_name; };
static constexpr ScriptNameEntry s_script_names[] = {
    { "Common"sv, "Zyyy"sv },         // 0
    { "Inherited"sv, "Zinh"sv },      // 1
    { "Arabic"sv, "Arab"sv },         // 2
    { "Armenian"sv, "Armn"sv },       // 3
    { "Bengali"sv, "Beng"sv },        // 4
    { "Bopomofo"sv, "Bopo"sv },      // 5
    { "Cherokee"sv, "Cher"sv },       // 6
    { "Coptic"sv, "Copt"sv },        // 7
    { "Cyrillic"sv, "Cyrl"sv },      // 8
    { "Devanagari"sv, "Deva"sv },    // 9
    { "Ethiopian"sv, "Ethi"sv },     // 10
    { "Georgian"sv, "Geor"sv },      // 11
    { "Greek"sv, "Grek"sv },         // 12
    { "Gujarati"sv, "Gujr"sv },     // 13
    { "Gurmukhi"sv, "Guru"sv },     // 14
    { "Han"sv, "Hani"sv },          // 15
    { "Hangul"sv, "Hang"sv },       // 16
    { "Hebrew"sv, "Hebr"sv },       // 17
    { "Hiragana"sv, "Hira"sv },     // 18
    { "Kannada"sv, "Knda"sv },      // 19
    { "Katakana"sv, "Kana"sv },     // 20
    { "Khmer"sv, "Khmr"sv },        // 21
    { "Lao"sv, "Laoo"sv },          // 22
    { "Latin"sv, "Latn"sv },        // 23
    { "Malayalam"sv, "Mlym"sv },     // 24
    { "Mongolian"sv, "Mong"sv },    // 25
    { "Myanmar"sv, "Mymr"sv },      // 26
    { "Oriya"sv, "Orya"sv },        // 27
    { "Sinhala"sv, "Sinh"sv },      // 28
    { "Tamil"sv, "Taml"sv },        // 29
    { "Telugu"sv, "Telu"sv },       // 30
    { "Thaana"sv, "Thaa"sv },       // 31
    { "Thai"sv, "Thai"sv },         // 32
    { "Tibetan"sv, "Tibt"sv },      // 33
    { "Canadian_Aboriginal"sv, "Cans"sv }, // 34
    { "Yi"sv, "Yiii"sv },           // 35
    { "Tagalog"sv, "Tglg"sv },      // 36
    { "Hanunoo"sv, "Hano"sv },      // 37
    { "Buhid"sv, "Buhd"sv },        // 38
    { "Tagbanwa"sv, "Tagb"sv },     // 39
};

Optional<Script> script_from_string(StringView script)
{
    for (size_t i = 0; i < sizeof(s_script_names) / sizeof(s_script_names[0]); ++i) {
        if (script == s_script_names[i].long_name || script == s_script_names[i].short_name)
            return static_cast<Script>(i);
    }
    return {};
}

// Simplified script detection based on code point ranges
bool code_point_has_script(u32 code_point, Script script)
{
    auto s = script.value();
    // Latin
    if (s == 23)
        return (code_point >= 0x41 && code_point <= 0x5A) || (code_point >= 0x61 && code_point <= 0x7A)
            || (code_point >= 0xC0 && code_point <= 0x024F) || (code_point >= 0x1E00 && code_point <= 0x1EFF);
    // Han (CJK)
    if (s == 15)
        return (code_point >= 0x4E00 && code_point <= 0x9FFF) || (code_point >= 0x3400 && code_point <= 0x4DBF)
            || (code_point >= 0x20000 && code_point <= 0x2A6DF) || (code_point >= 0xF900 && code_point <= 0xFAFF);
    // Hiragana
    if (s == 18)
        return code_point >= 0x3041 && code_point <= 0x309F;
    // Katakana
    if (s == 20)
        return (code_point >= 0x30A0 && code_point <= 0x30FF) || (code_point >= 0x31F0 && code_point <= 0x31FF);
    // Hangul
    if (s == 16)
        return (code_point >= 0xAC00 && code_point <= 0xD7AF) || (code_point >= 0x1100 && code_point <= 0x11FF)
            || (code_point >= 0x3130 && code_point <= 0x318F);
    // Cyrillic
    if (s == 8)
        return (code_point >= 0x0400 && code_point <= 0x04FF) || (code_point >= 0x0500 && code_point <= 0x052F);
    // Greek
    if (s == 12)
        return (code_point >= 0x0370 && code_point <= 0x03FF) || (code_point >= 0x1F00 && code_point <= 0x1FFF);
    // Arabic
    if (s == 2)
        return (code_point >= 0x0600 && code_point <= 0x06FF) || (code_point >= 0x0750 && code_point <= 0x077F)
            || (code_point >= 0x08A0 && code_point <= 0x08FF);
    // Hebrew
    if (s == 17)
        return (code_point >= 0x0590 && code_point <= 0x05FF);
    // Devanagari
    if (s == 9)
        return (code_point >= 0x0900 && code_point <= 0x097F) || (code_point >= 0xA8E0 && code_point <= 0xA8FF);
    // Thai
    if (s == 32)
        return code_point >= 0x0E00 && code_point <= 0x0E7F;
    // Common (ASCII digits, punctuation, symbols)
    if (s == 0)
        return is_ascii(code_point) && !rin_unicode_isalpha(code_point);

    return false;
}

bool code_point_has_script_extension(u32 code_point, Script script)
{
    // Simplified: same as script check
    return code_point_has_script(code_point, script);
}

BidiClass bidirectional_class(u32 code_point)
{
    // Simplified Bidi classification
    // RTL scripts: Arabic, Hebrew, Thaana, etc.
    if ((code_point >= 0x0590 && code_point <= 0x05FF) || (code_point >= 0xFB1D && code_point <= 0xFB4F))
        return BidiClass::RightToLeft;
    if ((code_point >= 0x0600 && code_point <= 0x06FF) || (code_point >= 0x0750 && code_point <= 0x077F)
        || (code_point >= 0x08A0 && code_point <= 0x08FF) || (code_point >= 0xFB50 && code_point <= 0xFDFF)
        || (code_point >= 0xFE70 && code_point <= 0xFEFF))
        return BidiClass::RightToLeftArabic;
    if ((code_point >= 0x0660 && code_point <= 0x0669) || (code_point >= 0x06F0 && code_point <= 0x06F9))
        return BidiClass::ArabicNumber;
    if (is_ascii_digit(code_point) || (code_point >= 0xFF10 && code_point <= 0xFF19))
        return BidiClass::EuropeanNumber;
    if (code_point == '+' || code_point == '-')
        return BidiClass::EuropeanNumberSeparator;
    if (code_point == '#' || code_point == '$' || code_point == 0x00A2 || code_point == 0x00A3 || code_point == 0x00A4 || code_point == 0x00A5)
        return BidiClass::EuropeanNumberTerminator;
    if (code_point == ',' || code_point == '.' || code_point == ':')
        return BidiClass::CommonNumberSeparator;
    if (code_point == 0x0A || code_point == 0x0D || code_point == 0x1C || code_point == 0x1D || code_point == 0x1E || code_point == 0x1F || code_point == 0x85 || code_point == 0x2029)
        return BidiClass::BlockSeparator;
    if (code_point == 0x09 || code_point == 0x0B || code_point == 0x1F || code_point == 0x2028)
        return BidiClass::SegmentSeparator;
    if (rin_unicode_isspace(code_point))
        return BidiClass::WhiteSpaceNeutral;
    if (code_point == 0x200C || code_point == 0x200D)
        return BidiClass::BoundaryNeutral;
    // LRE, LRO, RLE, RLO, PDF
    if (code_point == 0x202A) return BidiClass::LeftToRightEmbedding;
    if (code_point == 0x202D) return BidiClass::LeftToRightOverride;
    if (code_point == 0x202B) return BidiClass::RightToLeftEmbedding;
    if (code_point == 0x202E) return BidiClass::RightToLeftOverride;
    if (code_point == 0x202C) return BidiClass::PopDirectionalFormat;
    // Isolates
    if (code_point == 0x2066) return BidiClass::LeftToRightIsolate;
    if (code_point == 0x2067) return BidiClass::RightToLeftIsolate;
    if (code_point == 0x2068) return BidiClass::FirstStrongIsolate;
    if (code_point == 0x2069) return BidiClass::PopDirectionalIsolate;
    // NSM (combining marks)
    if ((code_point >= 0x0300 && code_point <= 0x036F) || (code_point >= 0x1AB0 && code_point <= 0x1AFF)
        || (code_point >= 0x1DC0 && code_point <= 0x1DFF) || (code_point >= 0x20D0 && code_point <= 0x20FF)
        || (code_point >= 0xFE20 && code_point <= 0xFE2F))
        return BidiClass::DirNonSpacingMark;
    // Default: Left-to-Right for most code points
    return BidiClass::LeftToRight;
}

LineBreakClass line_break_class(u32 code_point)
{
    if (rin_unicode_isalpha(code_point)) {
        // CJK ideographs
        if ((code_point >= 0x4E00 && code_point <= 0x9FFF) || (code_point >= 0x3400 && code_point <= 0x4DBF)
            || (code_point >= 0x20000 && code_point <= 0x2A6DF) || (code_point >= 0xAC00 && code_point <= 0xD7AF)
            || (code_point >= 0x3041 && code_point <= 0x309F) || (code_point >= 0x30A0 && code_point <= 0x30FF))
            return LineBreakClass::Ideographic;
        // Hebrew letter
        if (code_point >= 0x0590 && code_point <= 0x05FF)
            return LineBreakClass::Alphabetic;
        return LineBreakClass::Alphabetic;
    }
    if (rin_unicode_isdigit(code_point))
        return LineBreakClass::Numeric;
    if ((code_point >= 0x0300 && code_point <= 0x036F) || (code_point >= 0x1DC0 && code_point <= 0x1DFF)
        || (code_point >= 0x20D0 && code_point <= 0x20FF))
        return LineBreakClass::CombiningMark;
    if (code_point >= 0x0E00 && code_point <= 0x0E7F)
        return LineBreakClass::ComplexContext;
    return LineBreakClass::Other;
}

u32 canonicalize(u32 code_point, bool unicode_mode)
{
    if (unicode_mode) {
        // Case fold
        char buf[8];
        size_t len = 0;
        if (rin_unicode_casefold_full(code_point, buf, sizeof(buf), &len) == 0 && len > 0) {
            // Return first code point of the folded result
            Utf8View view { StringView { buf, len } };
            auto it = view.begin();
            if (it != view.end())
                return *it;
        }
        return code_point;
    }

    if (code_point < 128)
        return to_ascii_uppercase(code_point);

    auto code_point_string = String::from_code_point(code_point);
    auto uppercased = code_point_string.to_uppercase();
    if (uppercased.is_error())
        return code_point;

    auto code_points = uppercased.value().code_points();
    if (code_points.length() != 1)
        return code_point;

    auto it = code_points.begin();
    auto uppercased_code_point = *it;

    if (code_point >= 128 && uppercased_code_point < 128)
        return code_point;

    return uppercased_code_point;
}

Vector<CodePointRange> expand_range_case_insensitive(u32 from, u32 to)
{
    // Simplified: add original range + case variants
    Vector<CodePointRange> result;
    result.append({ from, to });
    for (u32 cp = from; cp <= to && cp - from < 1000; ++cp) {
        u32 lower = rin_unicode_tolower(cp);
        u32 upper = rin_unicode_toupper(cp);
        if (lower != cp)
            result.append({ lower, lower });
        if (upper != cp)
            result.append({ upper, upper });
    }
    return result;
}

void for_each_case_folded_code_point(u32 code_point, Function<IterationDecision(u32)> callback)
{
    // Fold the code point
    u32 folded = canonicalize(code_point, true);
    if (callback(folded) == IterationDecision::Break)
        return;
    // Also try case variants
    u32 lower = rin_unicode_tolower(code_point);
    u32 upper = rin_unicode_toupper(code_point);
    if (lower != code_point && lower != folded) {
        if (callback(lower) == IterationDecision::Break)
            return;
    }
    if (upper != code_point && upper != folded && upper != lower)
        callback(upper);
}

bool code_point_matches_range_ignoring_case(u32 code_point, u32 from, u32 to, bool unicode_mode)
{
    if (code_point >= from && code_point <= to)
        return true;

    auto canonical_ch = canonicalize(code_point, unicode_mode);
    if (canonical_ch >= from && canonical_ch <= to)
        return true;

    u32 lower = rin_unicode_tolower(code_point);
    u32 upper = rin_unicode_toupper(code_point);
    if (lower >= from && lower <= to)
        return true;
    if (upper >= from && upper <= to)
        return true;

    return false;
}

#else // AK_OS_RINOS

template<typename PropertyType>
struct PropertyName {
    Optional<StringView> long_name;
    Optional<StringView> short_name;
    Optional<StringView> additional_name;
};

// From uchar.h:
// Unicode allows for additional names, beyond the long and short name, which would be indicated by U_LONG_PROPERTY_NAME + i
static constexpr auto ADDITIONAL_NAME = static_cast<UPropertyNameChoice>(U_LONG_PROPERTY_NAME + 1);

}

template<typename PropertyType>
struct AK::Traits<Unicode::PropertyName<PropertyType>> {
    static constexpr bool equals(Unicode::PropertyName<PropertyType> const& candidate, StringView property)
    {
        return property == candidate.long_name || property == candidate.short_name || property == candidate.additional_name;
    }
};

namespace Unicode {

static constexpr GeneralCategory GENERAL_CATEGORY_CASED_LETTER = U_CHAR_CATEGORY_COUNT + 1;
static constexpr GeneralCategory GENERAL_CATEGORY_LETTER = U_CHAR_CATEGORY_COUNT + 2;
static constexpr GeneralCategory GENERAL_CATEGORY_MARK = U_CHAR_CATEGORY_COUNT + 3;
static constexpr GeneralCategory GENERAL_CATEGORY_NUMBER = U_CHAR_CATEGORY_COUNT + 4;
static constexpr GeneralCategory GENERAL_CATEGORY_PUNCTUATION = U_CHAR_CATEGORY_COUNT + 5;
static constexpr GeneralCategory GENERAL_CATEGORY_SYMBOL = U_CHAR_CATEGORY_COUNT + 6;
static constexpr GeneralCategory GENERAL_CATEGORY_SEPARATOR = U_CHAR_CATEGORY_COUNT + 7;
static constexpr GeneralCategory GENERAL_CATEGORY_OTHER = U_CHAR_CATEGORY_COUNT + 8;
static constexpr GeneralCategory GENERAL_CATEGORY_LIMIT = U_CHAR_CATEGORY_COUNT + 9;

static HashMap<GeneralCategory, NonnullOwnPtr<icu::UnicodeSet>> s_category_sets_with_case_closure;
static HashMap<Property, NonnullOwnPtr<icu::UnicodeSet>> s_property_sets_with_case_closure;

Optional<GeneralCategory> general_category_from_string(StringView general_category)
{
    static auto general_category_names = []() {
        Array<PropertyName<GeneralCategory>, GENERAL_CATEGORY_LIMIT.value()> names;

        auto set_names = [&](auto property, auto index, auto general_category) {
            if (char const* name = u_getPropertyValueName(property, general_category, U_LONG_PROPERTY_NAME))
                names[index.value()].long_name = StringView { name, strlen(name) };
            if (char const* name = u_getPropertyValueName(property, general_category, U_SHORT_PROPERTY_NAME))
                names[index.value()].short_name = StringView { name, strlen(name) };
            if (char const* name = u_getPropertyValueName(property, general_category, ADDITIONAL_NAME))
                names[index.value()].additional_name = StringView { name, strlen(name) };
        };

        for (GeneralCategory general_category = 0; general_category < U_CHAR_CATEGORY_COUNT; ++general_category)
            set_names(UCHAR_GENERAL_CATEGORY, general_category, static_cast<UCharCategory>(general_category.value()));

        set_names(UCHAR_GENERAL_CATEGORY_MASK, GENERAL_CATEGORY_CASED_LETTER, U_GC_LC_MASK);
        set_names(UCHAR_GENERAL_CATEGORY_MASK, GENERAL_CATEGORY_LETTER, U_GC_L_MASK);
        set_names(UCHAR_GENERAL_CATEGORY_MASK, GENERAL_CATEGORY_MARK, U_GC_M_MASK);
        set_names(UCHAR_GENERAL_CATEGORY_MASK, GENERAL_CATEGORY_NUMBER, U_GC_N_MASK);
        set_names(UCHAR_GENERAL_CATEGORY_MASK, GENERAL_CATEGORY_PUNCTUATION, U_GC_P_MASK);
        set_names(UCHAR_GENERAL_CATEGORY_MASK, GENERAL_CATEGORY_SYMBOL, U_GC_S_MASK);
        set_names(UCHAR_GENERAL_CATEGORY_MASK, GENERAL_CATEGORY_SEPARATOR, U_GC_Z_MASK);
        set_names(UCHAR_GENERAL_CATEGORY_MASK, GENERAL_CATEGORY_OTHER, U_GC_C_MASK);

        return names;
    }();

    if (auto index = find_index(general_category_names.begin(), general_category_names.end(), general_category); index != general_category_names.size())
        return static_cast<GeneralCategory>(index);
    return {};
}

static uint32_t get_icu_mask(GeneralCategory general_category)
{
    if (general_category == GENERAL_CATEGORY_CASED_LETTER)
        return U_GC_LC_MASK;
    if (general_category == GENERAL_CATEGORY_LETTER)
        return U_GC_L_MASK;
    if (general_category == GENERAL_CATEGORY_MARK)
        return U_GC_M_MASK;
    if (general_category == GENERAL_CATEGORY_NUMBER)
        return U_GC_N_MASK;
    if (general_category == GENERAL_CATEGORY_PUNCTUATION)
        return U_GC_P_MASK;
    if (general_category == GENERAL_CATEGORY_SYMBOL)
        return U_GC_S_MASK;
    if (general_category == GENERAL_CATEGORY_SEPARATOR)
        return U_GC_Z_MASK;
    if (general_category == GENERAL_CATEGORY_OTHER)
        return U_GC_C_MASK;

    return U_MASK(static_cast<UCharCategory>(general_category.value()));
}

bool code_point_has_general_category(u32 code_point, GeneralCategory general_category, CaseSensitivity case_sensitivity)
{
    auto icu_code_point = static_cast<UChar32>(code_point);
    auto category_mask = get_icu_mask(general_category);

    if ((U_GET_GC_MASK(icu_code_point) & category_mask) != 0)
        return true;

    if (case_sensitivity == CaseSensitivity::CaseSensitive)
        return false;

    auto& set = s_category_sets_with_case_closure.ensure(general_category, [&] {
        UErrorCode status = U_ZERO_ERROR;
        auto new_set = make<icu::UnicodeSet>();
        new_set->applyIntPropertyValue(UCHAR_GENERAL_CATEGORY_MASK, static_cast<int32_t>(category_mask), status);
        new_set->closeOver(USET_CASE_INSENSITIVE);
        new_set->freeze();
        return new_set;
    });

    return set->contains(icu_code_point);
}

bool code_point_is_printable(u32 code_point)
{
    return static_cast<bool>(u_isprint(static_cast<UChar32>(code_point)));
}

bool code_point_has_control_general_category(u32 code_point)
{
    return code_point_has_general_category(code_point, U_CONTROL_CHAR);
}

bool code_point_has_letter_general_category(u32 code_point)
{
    return code_point_has_general_category(code_point, GENERAL_CATEGORY_LETTER);
}

bool code_point_has_mark_general_category(u32 code_point)
{
    return code_point_has_general_category(code_point, GENERAL_CATEGORY_MARK);
}

bool code_point_has_number_general_category(u32 code_point)
{
    return code_point_has_general_category(code_point, GENERAL_CATEGORY_NUMBER);
}

bool code_point_has_punctuation_general_category(u32 code_point)
{
    return code_point_has_general_category(code_point, GENERAL_CATEGORY_PUNCTUATION);
}

bool code_point_has_separator_general_category(u32 code_point)
{
    return code_point_has_general_category(code_point, GENERAL_CATEGORY_SEPARATOR);
}

bool code_point_has_space_separator_general_category(u32 code_point)
{
    return code_point_has_general_category(code_point, U_SPACE_SEPARATOR);
}

bool code_point_has_symbol_general_category(u32 code_point)
{
    return code_point_has_general_category(code_point, GENERAL_CATEGORY_SYMBOL);
}

static constexpr Property PROPERTY_ANY = UCHAR_BINARY_LIMIT + 1;
static constexpr Property PROPERTY_ASCII = UCHAR_BINARY_LIMIT + 2;
static constexpr Property PROPERTY_ASSIGNED = UCHAR_BINARY_LIMIT + 3;
static constexpr Property PROPERTY_LIMIT = UCHAR_BINARY_LIMIT + 4;

Optional<Property> property_from_string(StringView property)
{
    static auto property_names = []() {
        Array<PropertyName<Property>, PROPERTY_LIMIT.value()> names;

        for (Property property = 0; property < UCHAR_BINARY_LIMIT; ++property) {
            auto icu_property = static_cast<UProperty>(property.value());

            if (char const* name = u_getPropertyName(icu_property, U_LONG_PROPERTY_NAME))
                names[property.value()].long_name = StringView { name, strlen(name) };
            if (char const* name = u_getPropertyName(icu_property, U_SHORT_PROPERTY_NAME))
                names[property.value()].short_name = StringView { name, strlen(name) };
            if (char const* name = u_getPropertyName(icu_property, ADDITIONAL_NAME))
                names[property.value()].additional_name = StringView { name, strlen(name) };
        }

        names[PROPERTY_ANY.value()] = { "Any"sv, {}, {} };
        names[PROPERTY_ASCII.value()] = { "ASCII"sv, {}, {} };
        names[PROPERTY_ASSIGNED.value()] = { "Assigned"sv, {}, {} };

        return names;
    }();

    if (auto index = find_index(property_names.begin(), property_names.end(), property); index != property_names.size())
        return static_cast<Property>(index);
    return {};
}

bool code_point_has_property(u32 code_point, Property property, CaseSensitivity case_sensitivity)
{
    auto icu_code_point = static_cast<UChar32>(code_point);

    if (property == PROPERTY_ANY)
        return is_unicode(code_point);
    if (property == PROPERTY_ASCII)
        return is_ascii(code_point);
    if (property == PROPERTY_ASSIGNED)
        return u_isdefined(icu_code_point) != 0;

    auto icu_property = static_cast<UProperty>(property.value());
    if (u_hasBinaryProperty(icu_code_point, icu_property))
        return true;

    if (case_sensitivity == CaseSensitivity::CaseSensitive)
        return false;

    auto& set = s_property_sets_with_case_closure.ensure(property, [&] {
        UErrorCode status = U_ZERO_ERROR;
        auto new_set = make<icu::UnicodeSet>();
        new_set->applyIntPropertyValue(icu_property, 1, status);
        new_set->closeOver(USET_CASE_INSENSITIVE);
        new_set->freeze();
        return new_set;
    });

    return set->contains(icu_code_point);
}

bool code_point_has_emoji_property(u32 code_point)
{
    return code_point_has_property(code_point, UCHAR_EMOJI);
}

bool code_point_has_emoji_modifier_base_property(u32 code_point)
{
    return code_point_has_property(code_point, UCHAR_EMOJI_MODIFIER_BASE);
}

bool code_point_has_emoji_presentation_property(u32 code_point)
{
    return code_point_has_property(code_point, UCHAR_EMOJI_PRESENTATION);
}

bool code_point_has_identifier_start_property(u32 code_point)
{
    return u_isIDStart(static_cast<UChar32>(code_point));
}

bool code_point_has_identifier_continue_property(u32 code_point)
{
    return u_isIDPart(static_cast<UChar32>(code_point));
}

bool code_point_has_regional_indicator_property(u32 code_point)
{
    return code_point_has_property(code_point, UCHAR_REGIONAL_INDICATOR);
}

bool code_point_has_variation_selector_property(u32 code_point)
{
    return code_point_has_property(code_point, UCHAR_VARIATION_SELECTOR);
}

bool code_point_has_white_space_property(u32 code_point)
{
    return code_point_has_property(code_point, UCHAR_WHITE_SPACE);
}

// https://tc39.es/ecma262/#table-binary-unicode-properties
bool is_ecma262_property(Property property)
{
    if (property == PROPERTY_ANY || property == PROPERTY_ASCII || property == PROPERTY_ASSIGNED)
        return true;

    switch (property.value()) {
    case UCHAR_ASCII_HEX_DIGIT:
    case UCHAR_ALPHABETIC:
    case UCHAR_BIDI_CONTROL:
    case UCHAR_BIDI_MIRRORED:
    case UCHAR_CASE_IGNORABLE:
    case UCHAR_CASED:
    case UCHAR_CHANGES_WHEN_CASEFOLDED:
    case UCHAR_CHANGES_WHEN_CASEMAPPED:
    case UCHAR_CHANGES_WHEN_LOWERCASED:
    case UCHAR_CHANGES_WHEN_NFKC_CASEFOLDED:
    case UCHAR_CHANGES_WHEN_TITLECASED:
    case UCHAR_CHANGES_WHEN_UPPERCASED:
    case UCHAR_DASH:
    case UCHAR_DEFAULT_IGNORABLE_CODE_POINT:
    case UCHAR_DEPRECATED:
    case UCHAR_DIACRITIC:
    case UCHAR_EMOJI:
    case UCHAR_EMOJI_COMPONENT:
    case UCHAR_EMOJI_MODIFIER:
    case UCHAR_EMOJI_MODIFIER_BASE:
    case UCHAR_EMOJI_PRESENTATION:
    case UCHAR_EXTENDED_PICTOGRAPHIC:
    case UCHAR_EXTENDER:
    case UCHAR_GRAPHEME_BASE:
    case UCHAR_GRAPHEME_EXTEND:
    case UCHAR_HEX_DIGIT:
    case UCHAR_IDS_BINARY_OPERATOR:
    case UCHAR_IDS_TRINARY_OPERATOR:
    case UCHAR_ID_CONTINUE:
    case UCHAR_ID_START:
    case UCHAR_IDEOGRAPHIC:
    case UCHAR_JOIN_CONTROL:
    case UCHAR_LOGICAL_ORDER_EXCEPTION:
    case UCHAR_LOWERCASE:
    case UCHAR_MATH:
    case UCHAR_NONCHARACTER_CODE_POINT:
    case UCHAR_PATTERN_SYNTAX:
    case UCHAR_PATTERN_WHITE_SPACE:
    case UCHAR_QUOTATION_MARK:
    case UCHAR_RADICAL:
    case UCHAR_REGIONAL_INDICATOR:
    case UCHAR_S_TERM:
    case UCHAR_SOFT_DOTTED:
    case UCHAR_TERMINAL_PUNCTUATION:
    case UCHAR_UNIFIED_IDEOGRAPH:
    case UCHAR_UPPERCASE:
    case UCHAR_VARIATION_SELECTOR:
    case UCHAR_WHITE_SPACE:
    case UCHAR_XID_CONTINUE:
    case UCHAR_XID_START:
        return true;
    default:
        return false;
    }
}

// https://tc39.es/ecma262/#table-binary-unicode-properties-of-strings
bool is_ecma262_string_property(Property property)
{
    switch (property.value()) {
    case UCHAR_BASIC_EMOJI:
    case UCHAR_EMOJI_KEYCAP_SEQUENCE:
    case UCHAR_RGI_EMOJI:
    case UCHAR_RGI_EMOJI_FLAG_SEQUENCE:
    case UCHAR_RGI_EMOJI_TAG_SEQUENCE:
    case UCHAR_RGI_EMOJI_MODIFIER_SEQUENCE:
    case UCHAR_RGI_EMOJI_ZWJ_SEQUENCE:
        return true;
    default:
        return false;
    }
}

Vector<String> get_property_strings(Property property)
{
    Vector<String> result;

    if (!is_ecma262_string_property(property))
        return result;

    UErrorCode status = U_ZERO_ERROR;
    auto const* icu_set = u_getBinaryPropertySet(static_cast<UProperty>(property.value()), &status);
    if (!icu_success(status) || !icu_set)
        return result;

    auto const* unicode_set = icu::UnicodeSet::fromUSet(icu_set);
    if (!unicode_set)
        return result;

    auto range_count = unicode_set->getRangeCount();
    for (int32_t i = 0; i < range_count; ++i) {
        auto start = unicode_set->getRangeStart(i);
        auto end = unicode_set->getRangeEnd(i);

        for (auto code_point = start; code_point <= end; ++code_point) {
            result.append(String::from_code_point(code_point));
        }
    }

    for (auto const& str : unicode_set->strings()) {
        result.append(icu_string_to_string(str));
    }

    return result;
}

Optional<Script> script_from_string(StringView script)
{
    static auto script_names = []() {
        Array<PropertyName<Script>, static_cast<size_t>(USCRIPT_CODE_LIMIT)> names;

        for (Script script = 0; script < USCRIPT_CODE_LIMIT; ++script) {
            auto icu_script = static_cast<UScriptCode>(script.value());

            if (char const* name = uscript_getName(icu_script))
                names[script.value()].long_name = StringView { name, strlen(name) };
            if (char const* name = uscript_getShortName(icu_script))
                names[script.value()].short_name = StringView { name, strlen(name) };
            if (char const* name = u_getPropertyValueName(UCHAR_SCRIPT, icu_script, ADDITIONAL_NAME))
                names[script.value()].additional_name = StringView { name, strlen(name) };
        }

        return names;
    }();

    if (auto index = find_index(script_names.begin(), script_names.end(), script); index != script_names.size())
        return static_cast<Script>(index);
    return {};
}

bool code_point_has_script(u32 code_point, Script script)
{
    UErrorCode status = U_ZERO_ERROR;

    auto icu_code_point = static_cast<UChar32>(code_point);
    auto icu_script = static_cast<UScriptCode>(script.value());

    if (auto result = uscript_getScript(icu_code_point, &status); icu_success(status))
        return result == icu_script;
    return false;
}

bool code_point_has_script_extension(u32 code_point, Script script)
{
    auto icu_code_point = static_cast<UChar32>(code_point);
    auto icu_script = static_cast<UScriptCode>(script.value());

    return static_cast<bool>(uscript_hasScript(icu_code_point, icu_script));
}

static constexpr BidiClass char_direction_to_bidi_class(UCharDirection direction)
{
    switch (direction) {
    case U_ARABIC_NUMBER:
        return BidiClass::ArabicNumber;
    case U_BLOCK_SEPARATOR:
        return BidiClass::BlockSeparator;
    case U_BOUNDARY_NEUTRAL:
        return BidiClass::BoundaryNeutral;
    case U_COMMON_NUMBER_SEPARATOR:
        return BidiClass::CommonNumberSeparator;
    case U_DIR_NON_SPACING_MARK:
        return BidiClass::DirNonSpacingMark;
    case U_EUROPEAN_NUMBER:
        return BidiClass::EuropeanNumber;
    case U_EUROPEAN_NUMBER_SEPARATOR:
        return BidiClass::EuropeanNumberSeparator;
    case U_EUROPEAN_NUMBER_TERMINATOR:
        return BidiClass::EuropeanNumberTerminator;
    case U_FIRST_STRONG_ISOLATE:
        return BidiClass::FirstStrongIsolate;
    case U_LEFT_TO_RIGHT:
        return BidiClass::LeftToRight;
    case U_LEFT_TO_RIGHT_EMBEDDING:
        return BidiClass::LeftToRightEmbedding;
    case U_LEFT_TO_RIGHT_ISOLATE:
        return BidiClass::LeftToRightIsolate;
    case U_LEFT_TO_RIGHT_OVERRIDE:
        return BidiClass::LeftToRightOverride;
    case U_OTHER_NEUTRAL:
        return BidiClass::OtherNeutral;
    case U_POP_DIRECTIONAL_FORMAT:
        return BidiClass::PopDirectionalFormat;
    case U_POP_DIRECTIONAL_ISOLATE:
        return BidiClass::PopDirectionalIsolate;
    case U_RIGHT_TO_LEFT:
        return BidiClass::RightToLeft;
    case U_RIGHT_TO_LEFT_ARABIC:
        return BidiClass::RightToLeftArabic;
    case U_RIGHT_TO_LEFT_EMBEDDING:
        return BidiClass::RightToLeftEmbedding;
    case U_RIGHT_TO_LEFT_ISOLATE:
        return BidiClass::RightToLeftIsolate;
    case U_RIGHT_TO_LEFT_OVERRIDE:
        return BidiClass::RightToLeftOverride;
    case U_SEGMENT_SEPARATOR:
        return BidiClass::SegmentSeparator;
    case U_WHITE_SPACE_NEUTRAL:
        return BidiClass::WhiteSpaceNeutral;
    case U_CHAR_DIRECTION_COUNT:
        break;
    }
    VERIFY_NOT_REACHED();
}

BidiClass bidirectional_class(u32 code_point)
{
    auto icu_code_point = static_cast<UChar32>(code_point);

    auto direction = u_charDirection(icu_code_point);
    return char_direction_to_bidi_class(direction);
}

LineBreakClass line_break_class(u32 code_point)
{
    auto icu_code_point = static_cast<UChar32>(code_point);
    auto icu_line_break = static_cast<ULineBreak>(u_getIntPropertyValue(icu_code_point, UCHAR_LINE_BREAK));

    switch (icu_line_break) {
    case U_LB_ALPHABETIC:
    case U_LB_HEBREW_LETTER:
        return LineBreakClass::Alphabetic;
    case U_LB_NUMERIC:
        return LineBreakClass::Numeric;
    case U_LB_IDEOGRAPHIC:
    case U_LB_H2:
    case U_LB_H3:
        return LineBreakClass::Ideographic;
    case U_LB_AMBIGUOUS:
        return LineBreakClass::Ambiguous;
    case U_LB_COMPLEX_CONTEXT:
        return LineBreakClass::ComplexContext;
    case U_LB_COMBINING_MARK:
        return LineBreakClass::CombiningMark;
    default:
        return LineBreakClass::Other;
    }
}

// 22.2.2.7.3 Canonicalize ( rer, ch ), https://tc39.es/ecma262/#sec-runtime-semantics-canonicalize-ch
u32 canonicalize(u32 code_point, bool unicode_mode)
{
    // 1. If HasEitherUnicodeFlag(rer) is true and rer.[[IgnoreCase]] is true, then
    //    a. If the file CaseFolding.txt of the Unicode Character Database provides a simple or common case folding mapping for ch, return the result of applying that mapping to ch.
    //    b. Return ch.
    if (unicode_mode)
        return u_foldCase(static_cast<UChar32>(code_point), U_FOLD_CASE_DEFAULT);

    // 2. If rer.[[IgnoreCase]] is false, return ch.
    // NOTE: This is handled by the caller.

    // 3. Assert: ch is a UTF-16 code unit.
    // 4. Let cp be the code point whose numeric value is the numeric value of ch.
    // NOTE: We already have a code point.

    // 5. Let u be toUppercase(« cp »), according to the Unicode Default Case Conversion algorithm.
    // 6. Let uStr be CodePointsToString(u).

    // OPTIMIZATION: For ASCII characters, toUppercase is just to_ascii_uppercase.
    //               Conditions in 7 & 9 are trivially satisfied (ASCII always maps to a single code point, and the result stays in the same range).
    if (code_point < 128)
        return to_ascii_uppercase(code_point);

    auto code_point_string = String::from_code_point(code_point);
    auto uppercased = code_point_string.to_uppercase();
    if (uppercased.is_error())
        return code_point;

    auto code_points = uppercased.value().code_points();

    // 7. If the length of uStr ≠ 1, return ch.
    if (code_points.length() != 1)
        return code_point;

    // 8. Let cu be uStr's single code unit element.
    auto it = code_points.begin();
    auto uppercased_code_point = *it;

    // 9. If the numeric value of ch ≥ 128 and the numeric value of cu < 128, return ch.
    if (code_point >= 128 && uppercased_code_point < 128)
        return code_point;

    // 10. Return cu.
    return uppercased_code_point;
}

Vector<CodePointRange> expand_range_case_insensitive(u32 from, u32 to)
{
    icu::UnicodeSet set(static_cast<UChar32>(from), static_cast<UChar32>(to));
    set.closeOver(USET_CASE_INSENSITIVE);

    Vector<CodePointRange> result;
    auto range_count = set.getRangeCount();
    result.ensure_capacity(range_count);

    for (int32_t i = 0; i < range_count; ++i)
        result.unchecked_append({ static_cast<u32>(set.getRangeStart(i)), static_cast<u32>(set.getRangeEnd(i)) });

    return result;
}

void for_each_case_folded_code_point(u32 code_point, Function<IterationDecision(u32)> callback)
{
    u32 canonical = canonicalize(code_point, true);

    icu::UnicodeSet closure(static_cast<UChar32>(canonical), static_cast<UChar32>(canonical));
    closure.closeOver(USET_CASE_INSENSITIVE);

    auto range_count = closure.getRangeCount();
    for (int32_t i = 0; i < range_count; ++i) {
        auto start = closure.getRangeStart(i);
        auto end = closure.getRangeEnd(i);
        for (auto cp = start; cp <= end; ++cp) {
            if (callback(static_cast<u32>(cp)) == IterationDecision::Break)
                return;
        }
    }
}

bool code_point_matches_range_ignoring_case(u32 code_point, u32 from, u32 to, bool unicode_mode)
{
    if (code_point >= from && code_point <= to)
        return true;

    icu::UnicodeSet candidates(static_cast<UChar32>(code_point), static_cast<UChar32>(code_point));
    candidates.closeOver(USET_CASE_INSENSITIVE);
    candidates.retain(static_cast<UChar32>(from), static_cast<UChar32>(to));

    if (candidates.isEmpty())
        return false;

    auto canonical_ch = canonicalize(code_point, unicode_mode);
    auto range_count = candidates.getRangeCount();
    for (auto i = 0; i < range_count; ++i) {
        auto start = candidates.getRangeStart(i);
        auto end = candidates.getRangeEnd(i);

        for (auto candidate_cp = start; candidate_cp <= end; ++candidate_cp) {
            if (canonicalize(candidate_cp, unicode_mode) == canonical_ch)
                return true;
        }
    }

    return false;
}

#endif // AK_OS_RINOS

}
