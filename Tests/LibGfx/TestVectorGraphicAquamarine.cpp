/*
 * Copyright (c) 2026, RinOS contributors
 *
 * SPDX-License-Identifier: BSD-2-Clause
 */

#include <LibGfx/Bitmap.h>
#include <LibGfx/Color.h>
#include <LibGfx/Path.h>
#include <LibGfx/VectorGraphic.h>
#include <LibTest/TestCase.h>

namespace {

class RectangleGraphic final : public Gfx::VectorGraphic {
public:
    virtual Gfx::IntSize intrinsic_size() const override { return { 4, 4 }; }

    virtual void draw(Gfx::Painter& painter) const override
    {
        Gfx::Path path;
        path.move_to({ 0, 0 });
        path.line_to({ 4, 0 });
        path.line_to({ 4, 4 });
        path.line_to({ 0, 4 });
        path.close();
        painter.fill_path(path, Gfx::Color::Red, Gfx::WindingRule::Nonzero);
    }
};

}

TEST_CASE(vector_graphic_bitmap_uses_aquamarine_painter)
{
    auto graphic = adopt_ref(*new RectangleGraphic);
    auto bitmap = MUST(graphic->bitmap({ 12, 8 }));

    EXPECT_EQ(bitmap->get_pixel(4, 3), Gfx::Color::Red);
    EXPECT_EQ(bitmap->get_pixel(0, 0), Gfx::Color::Transparent);
}
