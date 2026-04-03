/*
 * Copyright (c) 2024, Andreas Kling <andreas@ladybird.org>
 *
 * SPDX-License-Identifier: BSD-2-Clause
 */

#include <LibGfx/Path.h>
#ifdef AK_OS_RINOS
#include <LibGfx/PathAquamarine.h>
#else
#include <LibGfx/PathSkia.h>
#endif

namespace Gfx {

#ifdef AK_OS_RINOS
NonnullOwnPtr<Gfx::PathImpl> PathImpl::create()
{
    return PathImplAquamarine::create();
}
#else
NonnullOwnPtr<Gfx::PathImpl> PathImpl::create()
{
    return PathImplSkia::create();
}
#endif

PathImpl::~PathImpl() = default;

}
