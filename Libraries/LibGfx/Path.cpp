/*
 * Copyright (c) 2024, Andreas Kling <andreas@ladybird.org>
 *
 * SPDX-License-Identifier: BSD-2-Clause
 */

#include <LibGfx/Path.h>
#ifndef AK_OS_RINOS
#include <LibGfx/PathSkia.h>
#endif

namespace Gfx {

#ifdef AK_OS_RINOS
NonnullOwnPtr<Gfx::PathImpl> PathImpl::create()
{
    VERIFY_NOT_REACHED(); // TODO: Implement PathImplAquamarine
}
#else
NonnullOwnPtr<Gfx::PathImpl> PathImpl::create()
{
    return PathImplSkia::create();
}
#endif

PathImpl::~PathImpl() = default;

}
