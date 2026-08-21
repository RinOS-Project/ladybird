/*
 * Copyright (c) 2026, RinOS contributors
 *
 * SPDX-License-Identifier: BSD-2-Clause
 */

#pragma once

#include <math.h>
#include <stddef.h>

namespace Gfx::RinOSPathFlatten {

static constexpr unsigned max_supported_depth = 16;
static constexpr float max_coordinate = 1048576.0f;

struct Point {
    float x { 0.0f };
    float y { 0.0f };
};

struct Config {
    float tolerance { 0.25f };
    unsigned max_depth { 12 };
};

enum class Result {
    Ok,
    Invalid,
    LimitExceeded,
    CallbackRejected,
};

using EmitPoint = bool (*)(void*, Point);

static inline bool finite_scalar(float value)
{
    return __builtin_isfinite(value) && value >= -max_coordinate && value <= max_coordinate;
}

static inline bool valid_point(Point point)
{
    return finite_scalar(point.x) && finite_scalar(point.y);
}

static inline bool valid_config(Config const& config)
{
    return __builtin_isfinite(config.tolerance) && config.tolerance > 0.0f
        && config.tolerance <= 16.0f && config.max_depth > 0
        && config.max_depth <= max_supported_depth;
}

static inline Point midpoint(Point first, Point second)
{
    return { (first.x + second.x) * 0.5f, (first.y + second.y) * 0.5f };
}

static inline float distance_to_chord_squared(Point point, Point start, Point end)
{
    auto dx = end.x - start.x;
    auto dy = end.y - start.y;
    auto length_squared = dx * dx + dy * dy;
    if (length_squared <= 1.0e-12f) {
        auto point_dx = point.x - start.x;
        auto point_dy = point.y - start.y;
        return point_dx * point_dx + point_dy * point_dy;
    }
    auto cross = (point.x - start.x) * dy - (point.y - start.y) * dx;
    return (cross * cross) / length_squared;
}

static inline Result emit(EmitPoint callback, void* context, Point point)
{
    if (!valid_point(point))
        return Result::Invalid;
    return callback(context, point) ? Result::Ok : Result::CallbackRejected;
}

static inline Result quadratic(Config const& config, Point start, Point control, Point end, EmitPoint callback, void* context)
{
    struct Segment {
        Point start;
        Point control;
        Point end;
        unsigned depth;
    };
    Segment stack[max_supported_depth + 2];
    size_t stack_size = 0;
    auto tolerance_squared = config.tolerance * config.tolerance;

    if (!valid_config(config) || !valid_point(start) || !valid_point(control)
        || !valid_point(end) || !callback)
        return Result::Invalid;

    stack[stack_size++] = { start, control, end, 0 };
    while (stack_size > 0) {
        auto segment = stack[--stack_size];
        if (distance_to_chord_squared(segment.control, segment.start, segment.end) <= tolerance_squared) {
            auto result = emit(callback, context, segment.end);
            if (result != Result::Ok)
                return result;
            continue;
        }
        if (segment.depth >= config.max_depth || stack_size + 2 > max_supported_depth + 2)
            return Result::LimitExceeded;

        auto first_control = midpoint(segment.start, segment.control);
        auto second_control = midpoint(segment.control, segment.end);
        auto split = midpoint(first_control, second_control);
        if (!valid_point(first_control) || !valid_point(second_control) || !valid_point(split))
            return Result::Invalid;
        stack[stack_size++] = { split, second_control, segment.end, segment.depth + 1 };
        stack[stack_size++] = { segment.start, first_control, split, segment.depth + 1 };
    }
    return Result::Ok;
}

static inline Result cubic(Config const& config, Point start, Point first_control, Point second_control, Point end, EmitPoint callback, void* context)
{
    struct Segment {
        Point start;
        Point first_control;
        Point second_control;
        Point end;
        unsigned depth;
    };
    Segment stack[max_supported_depth + 2];
    size_t stack_size = 0;
    auto tolerance_squared = config.tolerance * config.tolerance;

    if (!valid_config(config) || !valid_point(start) || !valid_point(first_control)
        || !valid_point(second_control) || !valid_point(end) || !callback)
        return Result::Invalid;

    stack[stack_size++] = { start, first_control, second_control, end, 0 };
    while (stack_size > 0) {
        auto segment = stack[--stack_size];
        auto first_distance = distance_to_chord_squared(segment.first_control, segment.start, segment.end);
        auto second_distance = distance_to_chord_squared(segment.second_control, segment.start, segment.end);
        if (first_distance <= tolerance_squared && second_distance <= tolerance_squared) {
            auto result = emit(callback, context, segment.end);
            if (result != Result::Ok)
                return result;
            continue;
        }
        if (segment.depth >= config.max_depth || stack_size + 2 > max_supported_depth + 2)
            return Result::LimitExceeded;

        auto p01 = midpoint(segment.start, segment.first_control);
        auto p12 = midpoint(segment.first_control, segment.second_control);
        auto p23 = midpoint(segment.second_control, segment.end);
        auto p012 = midpoint(p01, p12);
        auto p123 = midpoint(p12, p23);
        auto split = midpoint(p012, p123);
        if (!valid_point(p01) || !valid_point(p12) || !valid_point(p23)
            || !valid_point(p012) || !valid_point(p123) || !valid_point(split))
            return Result::Invalid;
        stack[stack_size++] = { split, p123, p23, segment.end, segment.depth + 1 };
        stack[stack_size++] = { segment.start, p01, p012, split, segment.depth + 1 };
    }
    return Result::Ok;
}

static inline Point ellipse_point(Point center, float radius_x, float radius_y, float cosine, float sine, float angle)
{
    auto x = radius_x * cosf(angle);
    auto y = radius_y * sinf(angle);
    return {
        center.x + cosine * x - sine * y,
        center.y + sine * x + cosine * y,
    };
}

static inline Result elliptical_arc(Config const& config, Point start, Point end,
    float radius_x, float radius_y, float x_axis_rotation, bool large_arc,
    bool sweep, EmitPoint callback, void* context)
{
    static constexpr float tau = 6.28318530717958647692f;
    struct Segment {
        float start_angle;
        float end_angle;
        Point start;
        Point end;
        unsigned depth;
    };
    Segment stack[max_supported_depth + 2];
    size_t stack_size = 0;

    if (!valid_config(config) || !valid_point(start) || !valid_point(end)
        || !finite_scalar(radius_x) || !finite_scalar(radius_y)
        || !__builtin_isfinite(x_axis_rotation) || !callback)
        return Result::Invalid;
    radius_x = fabsf(radius_x);
    radius_y = fabsf(radius_y);
    if (radius_x == 0.0f || radius_y == 0.0f)
        return emit(callback, context, end);
    if (start.x == end.x && start.y == end.y)
        return Result::Ok;

    auto cosine = cosf(x_axis_rotation);
    auto sine = sinf(x_axis_rotation);
    auto half_dx = (start.x - end.x) * 0.5f;
    auto half_dy = (start.y - end.y) * 0.5f;
    auto transformed_x = cosine * half_dx + sine * half_dy;
    auto transformed_y = -sine * half_dx + cosine * half_dy;
    auto radius_x_squared = radius_x * radius_x;
    auto radius_y_squared = radius_y * radius_y;
    auto transformed_x_squared = transformed_x * transformed_x;
    auto transformed_y_squared = transformed_y * transformed_y;
    auto scale = transformed_x_squared / radius_x_squared + transformed_y_squared / radius_y_squared;
    if (!__builtin_isfinite(scale))
        return Result::Invalid;
    if (scale > 1.0f) {
        auto correction = sqrtf(scale);
        radius_x *= correction;
        radius_y *= correction;
        radius_x_squared = radius_x * radius_x;
        radius_y_squared = radius_y * radius_y;
        if (!finite_scalar(radius_x) || !finite_scalar(radius_y))
            return Result::Invalid;
    }

    auto numerator = radius_x_squared * radius_y_squared
        - radius_x_squared * transformed_y_squared
        - radius_y_squared * transformed_x_squared;
    auto denominator = radius_x_squared * transformed_y_squared
        + radius_y_squared * transformed_x_squared;
    if (denominator <= 0.0f || !__builtin_isfinite(numerator) || !__builtin_isfinite(denominator))
        return Result::Invalid;
    auto coefficient = sqrtf(fmaxf(0.0f, numerator / denominator));
    if (large_arc == sweep)
        coefficient = -coefficient;
    auto center_x_transformed = coefficient * (radius_x * transformed_y / radius_y);
    auto center_y_transformed = coefficient * (-radius_y * transformed_x / radius_x);
    Point center {
        cosine * center_x_transformed - sine * center_y_transformed + (start.x + end.x) * 0.5f,
        sine * center_x_transformed + cosine * center_y_transformed + (start.y + end.y) * 0.5f,
    };
    if (!valid_point(center))
        return Result::Invalid;

    auto start_unit_x = (transformed_x - center_x_transformed) / radius_x;
    auto start_unit_y = (transformed_y - center_y_transformed) / radius_y;
    auto end_unit_x = (-transformed_x - center_x_transformed) / radius_x;
    auto end_unit_y = (-transformed_y - center_y_transformed) / radius_y;
    auto start_angle = atan2f(start_unit_y, start_unit_x);
    auto delta_angle = atan2f(start_unit_x * end_unit_y - start_unit_y * end_unit_x,
        start_unit_x * end_unit_x + start_unit_y * end_unit_y);
    if (!sweep && delta_angle > 0.0f)
        delta_angle -= tau;
    else if (sweep && delta_angle < 0.0f)
        delta_angle += tau;
    if (!__builtin_isfinite(start_angle) || !__builtin_isfinite(delta_angle))
        return Result::Invalid;

    stack[stack_size++] = { start_angle, start_angle + delta_angle, start, end, 0 };
    auto tolerance_squared = config.tolerance * config.tolerance;
    while (stack_size > 0) {
        auto segment = stack[--stack_size];
        auto middle_angle = (segment.start_angle + segment.end_angle) * 0.5f;
        auto curve_middle = ellipse_point(center, radius_x, radius_y, cosine, sine, middle_angle);
        auto chord_middle = midpoint(segment.start, segment.end);
        auto error_x = curve_middle.x - chord_middle.x;
        auto error_y = curve_middle.y - chord_middle.y;
        if (!valid_point(curve_middle))
            return Result::Invalid;
        if (error_x * error_x + error_y * error_y <= tolerance_squared) {
            auto result = emit(callback, context, segment.end);
            if (result != Result::Ok)
                return result;
            continue;
        }
        if (segment.depth >= config.max_depth || stack_size + 2 > max_supported_depth + 2)
            return Result::LimitExceeded;
        stack[stack_size++] = { middle_angle, segment.end_angle, curve_middle, segment.end, segment.depth + 1 };
        stack[stack_size++] = { segment.start_angle, middle_angle, segment.start, curve_middle, segment.depth + 1 };
    }
    return Result::Ok;
}

}
