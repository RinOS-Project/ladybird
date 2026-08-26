/*
 * Copyright (c) 2026, RinOS contributors
 *
 * SPDX-License-Identifier: BSD-2-Clause
 */

#include <LibGfx/Bitmap.h>
#include <LibGfx/Painter.h>
#include <LibGfx/PaintStyle.h>
#include <LibGfx/Path.h>
#include <LibTest/TestCase.h>

TEST_CASE(solid_paint_style_fills_an_aquamarine_path)
{
    auto bitmap = MUST(Gfx::Bitmap::create(Gfx::BitmapFormat::BGRA8888, { 8, 8 }));
    auto painter = Gfx::Painter::create(bitmap);
    auto style = MUST(Gfx::SolidColorPaintStyle::create(Gfx::Color::Red));

    Gfx::Path path;
    path.move_to({ 2, 2 });
    path.line_to({ 6, 2 });
    path.line_to({ 6, 6 });
    path.line_to({ 2, 6 });
    path.close();

    painter->fill_path(path, *style, {}, 1.0f, Gfx::CompositingAndBlendingOperator::SourceOver, Gfx::WindingRule::Nonzero);

    EXPECT_EQ(bitmap->get_pixel(3, 3), Gfx::Color::Red);
    EXPECT_EQ(bitmap->get_pixel(0, 0), Gfx::Color::Transparent);
}

TEST_CASE(global_alpha_zero_does_not_modify_an_aquamarine_path)
{
    auto bitmap = MUST(Gfx::Bitmap::create(Gfx::BitmapFormat::BGRA8888, { 8, 8 }));
    auto painter = Gfx::Painter::create(bitmap);
    auto style = MUST(Gfx::SolidColorPaintStyle::create(Gfx::Color::Red));

    Gfx::Path path;
    path.move_to({ 2, 2 });
    path.line_to({ 6, 2 });
    path.line_to({ 6, 6 });
    path.line_to({ 2, 6 });
    path.close();

    painter->fill_path(path, *style, {}, 0.0f, Gfx::CompositingAndBlendingOperator::SourceOver, Gfx::WindingRule::Nonzero);

    EXPECT_EQ(bitmap->get_pixel(3, 3), Gfx::Color::Transparent);
}
