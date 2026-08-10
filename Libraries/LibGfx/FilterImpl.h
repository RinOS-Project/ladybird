/*
 * Copyright (c) 2024-2025, Lucien Fiorini <lucienfiorini@gmail.com>
 *
 * SPDX-License-Identifier: BSD-2-Clause
 */

#pragma once

#include <AK/NonnullOwnPtr.h>
#ifndef AK_OS_RINOS
#    include <core/SkColorFilter.h>
#    include <effects/SkImageFilters.h>
#endif

namespace Gfx {

#ifdef AK_OS_RINOS
// RinOS: stub filter implementation (no Skia)
struct FilterImpl {
    static NonnullOwnPtr<FilterImpl> create()
    {
        return adopt_own(*new FilterImpl());
    }
    NonnullOwnPtr<FilterImpl> clone() const
    {
        return adopt_own(*new FilterImpl());
    }
};
#else
struct FilterImpl {
    sk_sp<SkImageFilter> filter;

    static NonnullOwnPtr<FilterImpl> create(sk_sp<SkImageFilter> filter)
    {
        return adopt_own(*new FilterImpl(move(filter)));
    }

    NonnullOwnPtr<FilterImpl> clone() const
    {
        return adopt_own(*new FilterImpl(filter));
    }
};
#endif

}
