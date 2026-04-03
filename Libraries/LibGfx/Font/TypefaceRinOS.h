/*
 * Copyright (c) 2026, RinOS contributors
 *
 * SPDX-License-Identifier: BSD-2-Clause
 */

#pragma once

#include <AK/FlyString.h>
#include <LibGfx/Font/Typeface.h>

namespace Gfx {

class TypefaceRinOS final : public Typeface {
public:
    static TypefaceRinOS& the();

    virtual ~TypefaceRinOS() override;

    virtual u32 glyph_count() const override;
    virtual u16 units_per_em() const override;
    virtual u32 glyph_id_for_code_point(u32 code_point) const override;
    virtual FlyString const& family() const override;
    virtual u16 weight() const override;
    virtual u16 width() const override;
    virtual u8 slope() const override;

private:
    TypefaceRinOS();

    virtual ReadonlyBytes buffer() const override;
    virtual u32 ttc_index() const override;

    FlyString m_family;
};

}
