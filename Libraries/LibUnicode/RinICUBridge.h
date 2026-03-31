/*
 * Copyright (c) 2026, RinOS Contributors
 * SPDX-License-Identifier: BSD-2-Clause
 *
 * Bridge header for replacing ICU4C with rinicu (IPC) + libunicode (local).
 * Only included when AK_OS_RINOS is defined.
 */

#pragma once

#ifdef AK_OS_RINOS

#include <AK/String.h>
#include <AK/StringView.h>

extern "C" {
#include <rin_icu.h>
#include <rin_unicode.h>
}

namespace Unicode {

// Thread-local rinicu client connection.
// Multiple LibUnicode calls share one IPC connection per thread.
rin_icu_client_t& rin_icu_client();

// Convenience: call rinicu with a locale string, filling a char buffer.
// Returns a String from the buffer, or empty on failure.
String rin_icu_locale_string_op(
    int (*fn)(rin_icu_client_t*, char const*, char*, uint32_t, uint32_t*),
    StringView locale);

// Convenience: call rinicu timezone string op.
String rin_icu_tz_string_op(
    int (*fn)(rin_icu_client_t*, char const*, char*, uint32_t, uint32_t*),
    StringView tz);

} // namespace Unicode

#endif // AK_OS_RINOS
