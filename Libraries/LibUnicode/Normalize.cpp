/*
 * Copyright (c) 2022, mat
 * Copyright (c) 2024, Tim Flynn <trflynn89@serenityos.org>
 *
 * SPDX-License-Identifier: BSD-2-Clause
 */

#include <AK/StringBuilder.h>
#include <LibUnicode/ICU.h>
#include <LibUnicode/Normalize.h>

#ifndef AK_OS_RINOS
#include <unicode/normalizer2.h>
#else
#include <LibUnicode/RinICUBridge.h>
#endif

namespace Unicode {

NormalizationForm normalization_form_from_string(StringView form)
{
    if (form == "NFD"sv)
        return NormalizationForm::NFD;
    if (form == "NFC"sv)
        return NormalizationForm::NFC;
    if (form == "NFKD"sv)
        return NormalizationForm::NFKD;
    if (form == "NFKC"sv)
        return NormalizationForm::NFKC;
    VERIFY_NOT_REACHED();
}

StringView normalization_form_to_string(NormalizationForm form)
{
    switch (form) {
    case NormalizationForm::NFD:
        return "NFD"sv;
    case NormalizationForm::NFC:
        return "NFC"sv;
    case NormalizationForm::NFKD:
        return "NFKD"sv;
    case NormalizationForm::NFKC:
        return "NFKC"sv;
    }
    VERIFY_NOT_REACHED();
}

String normalize(StringView string, NormalizationForm form)
{
#ifdef AK_OS_RINOS
    // RinOS: use rinicu normalize IPC
    char result_buf[4096];
    size_t result_len = 0;

    // Null-terminate input for rinicu
    ByteString input_z(string);

    if (rin_icu_normalize(&rin_icu_client(), rin_icu_normalization_form(form), input_z.characters(), result_buf, sizeof(result_buf), &result_len) == 0 && result_len > 0)
        return MUST(String::from_utf8(StringView { result_buf, result_len }));

    return MUST(String::from_utf8(string));
#else
    UErrorCode status = U_ZERO_ERROR;
    icu::Normalizer2 const* normalizer = nullptr;

    switch (form) {
    case NormalizationForm::NFD:
        normalizer = icu::Normalizer2::getNFDInstance(status);
        break;
    case NormalizationForm::NFC:
        normalizer = icu::Normalizer2::getNFCInstance(status);
        break;
    case NormalizationForm::NFKD:
        normalizer = icu::Normalizer2::getNFKDInstance(status);
        break;
    case NormalizationForm::NFKC:
        normalizer = icu::Normalizer2::getNFKCInstance(status);
        break;
    }

    if (icu_failure(status))
        return MUST(String::from_utf8(string));

    VERIFY(normalizer);

    StringBuilder builder { string.length() };
    icu::StringByteSink sink { &builder };

    normalizer->normalizeUTF8(0, icu_string_piece(string), sink, nullptr, status);
    if (icu_failure(status))
        return MUST(String::from_utf8(string));

    return MUST(builder.to_string());
#endif // !AK_OS_RINOS
}

}
