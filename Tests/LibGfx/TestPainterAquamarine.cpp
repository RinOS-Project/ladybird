/*
 * Copyright (c) 2026, RinOS contributors
 *
 * SPDX-License-Identifier: BSD-2-Clause
 */

#include <LibGfx/Bitmap.h>
#include <LibGfx/ImmutableBitmap.h>
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

TEST_CASE(aquamarine_bitmap_blit_honors_source_over_alpha)
{
    auto source = MUST(Gfx::Bitmap::create(Gfx::BitmapFormat::BGRA8888, { 1, 1 }));
    source->set_pixel(0, 0, Gfx::Color::Red);
    auto target = MUST(Gfx::Bitmap::create(Gfx::BitmapFormat::BGRA8888, { 2, 2 }));
    target->set_pixel(0, 0, Gfx::Color::Blue);
    auto painter = Gfx::Painter::create(target);

    painter->draw_bitmap({ 0, 0, 1, 1 }, Gfx::ImmutableBitmap::create(source), { 0, 0, 1, 1 },
        Gfx::ScalingMode::NearestNeighbor, {}, 0.5f, Gfx::CompositingAndBlendingOperator::SourceOver);

    EXPECT_EQ(target->get_pixel(0, 0), Gfx::Color(128, 0, 127, 255));
}

TEST_CASE(aquamarine_bitmap_copy_and_bilinear_are_explicit)
{
    auto source = MUST(Gfx::Bitmap::create(Gfx::BitmapFormat::BGRA8888, { 2, 2 }));
    source->set_pixel(0, 0, Gfx::Color::Black);
    source->set_pixel(1, 0, Gfx::Color::White);
    source->set_pixel(0, 1, Gfx::Color::White);
    source->set_pixel(1, 1, Gfx::Color::Black);
    auto target = MUST(Gfx::Bitmap::create(Gfx::BitmapFormat::BGRA8888, { 3, 3 }));
    auto painter = Gfx::Painter::create(target);
    painter->draw_bitmap({ 0, 0, 3, 3 }, Gfx::ImmutableBitmap::create(source), { 0, 0, 2, 2 },
        Gfx::ScalingMode::Bilinear, {}, 1.0f, Gfx::CompositingAndBlendingOperator::Copy);

    EXPECT_EQ(target->get_pixel(1, 1), Gfx::Color(128, 128, 128, 255));
}

TEST_CASE(aquamarine_clip_rejects_non_rectangular_geometry)
{
    auto bitmap = MUST(Gfx::Bitmap::create(Gfx::BitmapFormat::BGRA8888, { 8, 8 }));
    auto painter = Gfx::Painter::create(bitmap);
    Gfx::Path triangle;
    triangle.move_to({ 1, 1 });
    triangle.line_to({ 7, 1 });
    triangle.line_to({ 4, 7 });
    triangle.close();

    painter->clip(triangle, Gfx::WindingRule::Nonzero);
    painter->fill_rect({ 0, 0, 8, 8 }, Gfx::Color::Red);

    EXPECT_EQ(bitmap->get_pixel(4, 3), Gfx::Color::Transparent);
}

TEST_CASE(aquamarine_fill_rect_honors_transform)
{
    auto bitmap = MUST(Gfx::Bitmap::create(Gfx::BitmapFormat::BGRA8888, { 8, 8 }));
    auto painter = Gfx::Painter::create(bitmap);
    Gfx::AffineTransform transform;
    transform.translate(3, 2);
    painter->set_transform(transform);

    painter->fill_rect({ 1, 1, 2, 2 }, Gfx::Color::Red);

    EXPECT_EQ(bitmap->get_pixel(4, 3), Gfx::Color::Red);
    EXPECT_EQ(bitmap->get_pixel(1, 1), Gfx::Color::Transparent);
}

TEST_CASE(aquamarine_clear_rect_honors_transform)
{
    auto bitmap = MUST(Gfx::Bitmap::create(Gfx::BitmapFormat::BGRA8888, { 8, 8 }));
    auto painter = Gfx::Painter::create(bitmap);
    painter->fill_rect({ 0, 0, 8, 8 }, Gfx::Color::Red);

    Gfx::AffineTransform transform;
    transform.translate(3, 2);
    painter->set_transform(transform);
    painter->clear_rect({ 1, 1, 2, 2 }, Gfx::Color::Transparent);

    EXPECT_EQ(bitmap->get_pixel(4, 3), Gfx::Color::Transparent);
    EXPECT_EQ(bitmap->get_pixel(1, 1), Gfx::Color::Red);
}

TEST_CASE(aquamarine_stroke_rejects_unimplemented_cap_semantics)
{
    auto bitmap = MUST(Gfx::Bitmap::create(Gfx::BitmapFormat::BGRA8888, { 8, 8 }));
    auto painter = Gfx::Painter::create(bitmap);
    Gfx::Path path;
    path.move_to({ 1, 4 });
    path.line_to({ 7, 4 });

    painter->stroke_path(path, Gfx::Color::Red, 2.0f, 0.0f,
        Gfx::CompositingAndBlendingOperator::SourceOver,
        Gfx::Path::CapStyle::Butt, Gfx::Path::JoinStyle::Round, 4.0f, {}, 0.0f);

    EXPECT_EQ(bitmap->get_pixel(4, 4), Gfx::Color::Transparent);
}
