/*
 * Copyright (c) 2026, RinOS contributors
 *
 * SPDX-License-Identifier: BSD-2-Clause
 */

#include <LibGfx/Font/TypefaceRinOS.h>
#include <LibGfx/Font/Font.h>

namespace Gfx {

TypefaceRinOS& TypefaceRinOS::the()
{
    static NonnullRefPtr<Typeface> s_typeface = adopt_ref(*new TypefaceRinOS());
    return static_cast<TypefaceRinOS&>(*s_typeface);
}

TypefaceRinOS::TypefaceRinOS()
    : m_family(FlyString::from_utf8_without_validation("RinOS UI"sv.bytes()))
{
}

TypefaceRinOS::~TypefaceRinOS() = default;

u32 TypefaceRinOS::glyph_count() const
{
    return 0x110000u;
}

u16 TypefaceRinOS::units_per_em() const
{
    return 16;
}

u32 TypefaceRinOS::glyph_id_for_code_point(u32 code_point) const
{
    if (code_point == 0)
        return static_cast<u32>('?');
    return code_point;
}

FlyString const& TypefaceRinOS::family() const
{
    return m_family;
}

u16 TypefaceRinOS::weight() const
{
    return 400;
}

u16 TypefaceRinOS::width() const
{
    return Gfx::FontWidth::Normal;
}

u8 TypefaceRinOS::slope() const
{
    return 0;
}

ReadonlyBytes TypefaceRinOS::buffer() const
{
    return {};
}

u32 TypefaceRinOS::ttc_index() const
{
    return 0;
}

}
