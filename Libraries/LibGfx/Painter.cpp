/*
 * Copyright (c) 2024, Andreas Kling <andreas@ladybird.org>
 *
 * SPDX-License-Identifier: BSD-2-Clause
 */

#include <LibGfx/Bitmap.h>
#include <LibGfx/Painter.h>
#ifndef AK_OS_RINOS
#include <LibGfx/PainterSkia.h>
#endif
#include <LibGfx/PaintingSurface.h>

namespace Gfx {

Painter::~Painter() = default;

#ifdef AK_OS_RINOS
NonnullOwnPtr<Painter> Painter::create(NonnullRefPtr<Gfx::Bitmap> target_bitmap)
{
    (void)target_bitmap;
    VERIFY_NOT_REACHED(); // TODO: Implement PainterAquamarine
}
#else
NonnullOwnPtr<Painter> Painter::create(NonnullRefPtr<Gfx::Bitmap> target_bitmap)
{
    auto painting_surface = PaintingSurface::wrap_bitmap(target_bitmap);
    return make<PainterSkia>(painting_surface);
}
#endif

}
