/*
 * Copyright (c) 2026, RinOS Contributors
 * SPDX-License-Identifier: BSD-2-Clause
 */

#ifdef AK_OS_RINOS

#include <AK/String.h>
#include <LibUnicode/RinICUBridge.h>

namespace Unicode {

static thread_local rin_icu_client_t s_client = { -1, 0, { 0 } };
static thread_local bool s_client_initialized = false;

rin_icu_client_t& rin_icu_client()
{
    if (!s_client_initialized) {
        rin_icu_client_open(&s_client);
        s_client_initialized = true;
    }
    return s_client;
}

String rin_icu_locale_string_op(
    int (*fn)(rin_icu_client_t*, char const*, char*, uint32_t, uint32_t*),
    StringView locale)
{
    char buf[256];
    uint32_t len = 0;

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
    int (*fn)(rin_icu_client_t*, char const*, char*, uint32_t, uint32_t*),
    StringView tz)
{
    char buf[256];
    uint32_t len = 0;

    char tz_buf[128];
    auto n = tz.length() < sizeof(tz_buf) - 1 ? tz.length() : sizeof(tz_buf) - 1;
    __builtin_memcpy(tz_buf, tz.characters_without_null_termination(), n);
    tz_buf[n] = '\0';

    if (fn(&rin_icu_client(), tz_buf, buf, sizeof(buf), &len) == 0 && len > 0)
        return MUST(String::from_utf8({ buf, len }));
    return MUST(String::from_utf8(tz));
}

} // namespace Unicode

#endif // AK_OS_RINOS
