/*
 * Copyright (c) 2026, RinOS contributors.
 *
 * SPDX-License-Identifier: BSD-2-Clause
 */

#include <AK/NumericLimits.h>
#include <LibJS/Runtime/TypedArray.h>
#include <LibWeb/WebGL/OpenGLContext.h>
#include <LibWeb/WebGL/WebGLRenderingContextOverloads.h>
#include <LibWeb/WebGL/WebGLUniformLocation.h>

extern "C" {
#include <ringl/ringl.h>
#include <ringl/ringl_sync.h>
}

namespace Web::WebGL {

static bool uses_webgl_depth_texture_format(WebIDL::UnsignedLong internalformat, WebIDL::UnsignedLong format)
{
    return internalformat == RINGL_DEPTH_COMPONENT
        || internalformat == RINGL_DEPTH_COMPONENT32F
        || internalformat == RINGL_DEPTH_STENCIL
        || internalformat == RINGL_DEPTH24_STENCIL8
        || format == RINGL_DEPTH_COMPONENT
        || format == RINGL_DEPTH_STENCIL;
}

static bool uses_webgl_depth_texture_format(WebIDL::UnsignedLong format)
{
    return format == RINGL_DEPTH_COMPONENT || format == RINGL_DEPTH_STENCIL;
}

static bool is_webgl1_color_texture_format(WebIDL::UnsignedLong format)
{
    return format == RINGL_RGBA || format == RINGL_RGB || format == RINGL_ALPHA
        || format == RINGL_LUMINANCE || format == RINGL_LUMINANCE_ALPHA;
}

static bool uses_oes_texture_float_format(WebIDL::UnsignedLong internalformat, WebIDL::UnsignedLong format, WebIDL::UnsignedLong type)
{
    return type == RINGL_FLOAT && internalformat == format
        && is_webgl1_color_texture_format(format);
}

static bool uses_oes_texture_float_format(WebIDL::UnsignedLong format, WebIDL::UnsignedLong type)
{
    return type == RINGL_FLOAT && is_webgl1_color_texture_format(format);
}

static bool uses_oes_texture_half_float_format(WebIDL::UnsignedLong internalformat, WebIDL::UnsignedLong format, WebIDL::UnsignedLong type)
{
    return type == RINGL_HALF_FLOAT_OES && internalformat == format
        && is_webgl1_color_texture_format(format);
}

static bool uses_oes_texture_half_float_format(WebIDL::UnsignedLong format, WebIDL::UnsignedLong type)
{
    return type == RINGL_HALF_FLOAT_OES && is_webgl1_color_texture_format(format);
}

WebGLRenderingContextOverloads::WebGLRenderingContextOverloads(JS::Realm& realm, NonnullOwnPtr<OpenGLContext> context)
    : WebGLRenderingContextImpl(realm, move(context))
{
}

void WebGLRenderingContextOverloads::buffer_data(WebIDL::UnsignedLong target, WebIDL::LongLong size, WebIDL::UnsignedLong usage)
{
    if (!make_rin_gl_current())
        return;

    ringl_buffer_data_from_bytes(target, size, nullptr, 0u, usage);
}

void WebGLRenderingContextOverloads::buffer_data(WebIDL::UnsignedLong target, Optional<GC::Root<WebIDL::BufferSource>> data, WebIDL::UnsignedLong usage)
{
    if (!make_rin_gl_current())
        return;

    // https://registry.khronos.org/webgl/specs/latest/1.0/#5.14.5
    // A null BufferSource is an INVALID_VALUE error. Do not turn an invalid
    // view into a process-aborting MUST(): it must leave the RinGL buffer
    // untouched and surface as a WebGL error instead.
    if (!data.has_value()) {
        set_error(RINGL_INVALID_VALUE);
        return;
    }

    auto span_or_error = get_offset_span<u8 const>(*data.value(), /* src_offset= */ 0);
    if (span_or_error.is_error()) {
        set_error(RINGL_INVALID_VALUE);
        return;
    }

    auto span = span_or_error.release_value();
    if (span.size() > static_cast<size_t>(NumericLimits<WebIDL::LongLong>::max())) {
        set_error(RINGL_OUT_OF_MEMORY);
        return;
    }
    ringl_buffer_data_from_bytes(target, static_cast<WebIDL::LongLong>(span.size()),
        span.data(), span.size(), usage);
}

void WebGLRenderingContextOverloads::buffer_sub_data(WebIDL::UnsignedLong target, WebIDL::LongLong offset, GC::Root<WebIDL::BufferSource> data)
{
    if (!make_rin_gl_current())
        return;

    if (!data) {
        set_error(RINGL_INVALID_VALUE);
        return;
    }

    auto span_or_error = get_offset_span<u8 const>(*data, /* src_offset= */ 0);
    if (span_or_error.is_error()) {
        set_error(RINGL_INVALID_VALUE);
        return;
    }

    auto span = span_or_error.release_value();
    if (span.size() > static_cast<size_t>(NumericLimits<WebIDL::LongLong>::max())) {
        set_error(RINGL_OUT_OF_MEMORY);
        return;
    }
    ringl_buffer_sub_data_from_bytes(target, offset,
        static_cast<WebIDL::LongLong>(span.size()), span.data(), span.size());
}

void WebGLRenderingContextOverloads::read_pixels(WebIDL::Long x, WebIDL::Long y, WebIDL::Long width, WebIDL::Long height, WebIDL::UnsignedLong format, WebIDL::UnsignedLong type, GC::Root<WebIDL::ArrayBufferView> pixels)
{
    if (!make_rin_gl_current())
        return;
    if (!rin_gl_bound_framebuffer_is_webgl1_compatible())
        return;
    if (!pixels) {
        set_error(RINGL_INVALID_VALUE);
        return;
    }
    if (type == RINGL_FLOAT) {
        if (!extension_enabled("WEBGL_color_buffer_float"sv)
            && !extension_enabled("EXT_color_buffer_half_float"sv)) {
            set_error(RINGL_INVALID_ENUM);
            return;
        }
        // `readPixels(..., FLOAT, ...)` writes native Float32 elements. A
        // byte view of the same size would expose a type-confused result.
        if (!is<JS::Float32Array>(*pixels->raw_object())) {
            set_error(RINGL_INVALID_OPERATION);
            return;
        }
    }
    // The browser-owned view must remain bounded all the way through the
    // readback API. In particular, do not reintroduce the raw-pointer
    // ringl_read_pixels() path here: it cannot reject a short destination.
    auto span_or_error = get_offset_span<u8>(*pixels, /* src_offset= */ 0);
    if (span_or_error.is_error()) {
        set_error(RINGL_INVALID_VALUE);
        return;
    }
    auto span = span_or_error.release_value();
    ringl_read_pixels_to_bytes(x, y, width, height, format, type,
                               span.data(), span.size());
}

void WebGLRenderingContextOverloads::tex_image2d(WebIDL::UnsignedLong target, WebIDL::Long level, WebIDL::Long internalformat, WebIDL::Long width, WebIDL::Long height, WebIDL::Long border, WebIDL::UnsignedLong format, WebIDL::UnsignedLong type, GC::Root<WebIDL::ArrayBufferView> pixels)
{
    if (!make_rin_gl_current())
        return;
    // Depth tokens are WebGL 1 extension-only even though RinGL's native
    // texture profile can store them. Do not let a caller observe that native
    // capability before WEBGL_depth_texture has been enabled.
    if (uses_webgl_depth_texture_format(internalformat, format)
        && !extension_enabled("WEBGL_depth_texture"sv)) {
        set_error(RINGL_INVALID_ENUM);
        return;
    }
    if (uses_oes_texture_float_format(internalformat, format, type)
        && !extension_enabled("OES_texture_float"sv)) {
        set_error(RINGL_INVALID_ENUM);
        return;
    }
    if (uses_oes_texture_half_float_format(internalformat, format, type)
        && !extension_enabled("OES_texture_half_float"sv)) {
        set_error(RINGL_INVALID_ENUM);
        return;
    }

    if (!pixels) {
        ringl_tex_image_2d_from_bytes(target, level, internalformat, width, height, border, format, type, nullptr, 0);
        return;
    }
    if (uses_oes_texture_float_format(internalformat, format, type)
        && !is<JS::Float32Array>(*pixels->raw_object())) {
        set_error(RINGL_INVALID_OPERATION);
        return;
    }
    if (uses_oes_texture_half_float_format(internalformat, format, type)
        && !is<JS::Uint16Array>(*pixels->raw_object())) {
        set_error(RINGL_INVALID_OPERATION);
        return;
    }

    auto span_or_error = get_offset_span<u8 const>(*pixels, /* src_offset= */ 0);
    if (span_or_error.is_error()) {
        set_error(RINGL_INVALID_VALUE);
        return;
    }
    auto span = span_or_error.release_value();
    ringl_tex_image_2d_from_bytes(target, level, internalformat, width, height, border, format, type, span.data(), span.size());
}

void WebGLRenderingContextOverloads::tex_sub_image2d(WebIDL::UnsignedLong target, WebIDL::Long level, WebIDL::Long xoffset, WebIDL::Long yoffset, WebIDL::Long width, WebIDL::Long height, WebIDL::UnsignedLong format, WebIDL::UnsignedLong type, GC::Root<WebIDL::ArrayBufferView> pixels)
{
    if (!make_rin_gl_current())
        return;
    if (uses_webgl_depth_texture_format(format)
        && !extension_enabled("WEBGL_depth_texture"sv)) {
        set_error(RINGL_INVALID_ENUM);
        return;
    }
    if (uses_oes_texture_float_format(format, type)
        && !extension_enabled("OES_texture_float"sv)) {
        set_error(RINGL_INVALID_ENUM);
        return;
    }
    if (uses_oes_texture_half_float_format(format, type)
        && !extension_enabled("OES_texture_half_float"sv)) {
        set_error(RINGL_INVALID_ENUM);
        return;
    }
    if (!pixels) {
        set_error(RINGL_INVALID_VALUE);
        return;
    }
    if (uses_oes_texture_float_format(format, type)
        && !is<JS::Float32Array>(*pixels->raw_object())) {
        set_error(RINGL_INVALID_OPERATION);
        return;
    }
    if (uses_oes_texture_half_float_format(format, type)
        && !is<JS::Uint16Array>(*pixels->raw_object())) {
        set_error(RINGL_INVALID_OPERATION);
        return;
    }

    auto span_or_error = get_offset_span<u8 const>(*pixels, /* src_offset= */ 0);
    if (span_or_error.is_error()) {
        set_error(RINGL_INVALID_VALUE);
        return;
    }
    auto span = span_or_error.release_value();
    ringl_tex_sub_image_2d_from_bytes(target, level, xoffset, yoffset, width, height, format, type, span.data(), span.size());
}

void WebGLRenderingContextOverloads::compressed_tex_image2d(WebIDL::UnsignedLong, WebIDL::Long, WebIDL::UnsignedLong internalformat, WebIDL::Long, WebIDL::Long, WebIDL::Long, GC::Root<WebIDL::ArrayBufferView>)
{
    if (!make_rin_gl_current())
        return;

    // RinGL advertises no compressed-texture extension or native compressed
    // storage. Keep the standard WebGL extension gate instead of accepting a
    // byte stream that the backend cannot decode.
    if (!enabled_compressed_texture_formats().contains_slow(internalformat)) {
        set_error(RINGL_INVALID_ENUM);
        return;
    }
    set_error(RINGL_INVALID_OPERATION);
}

void WebGLRenderingContextOverloads::compressed_tex_sub_image2d(WebIDL::UnsignedLong, WebIDL::Long, WebIDL::Long, WebIDL::Long, WebIDL::Long, WebIDL::Long, WebIDL::UnsignedLong format, GC::Root<WebIDL::ArrayBufferView>)
{
    if (!make_rin_gl_current())
        return;
    if (!enabled_compressed_texture_formats().contains_slow(format)) {
        set_error(RINGL_INVALID_ENUM);
        return;
    }
    set_error(RINGL_INVALID_OPERATION);
}

void WebGLRenderingContextOverloads::tex_image2d(WebIDL::UnsignedLong target, WebIDL::Long level, WebIDL::Long internalformat, WebIDL::UnsignedLong format, WebIDL::UnsignedLong type, TexImageSource source)
{
    if (!make_rin_gl_current())
        return;
    if (uses_webgl_depth_texture_format(internalformat, format)
        && !extension_enabled("WEBGL_depth_texture"sv)) {
        set_error(RINGL_INVALID_ENUM);
        return;
    }
    if (uses_oes_texture_float_format(internalformat, format, type)
        && !extension_enabled("OES_texture_float"sv)) {
        set_error(RINGL_INVALID_ENUM);
        return;
    }
    if (uses_oes_texture_half_float_format(internalformat, format, type)
        && !extension_enabled("OES_texture_half_float"sv)) {
        set_error(RINGL_INVALID_ENUM);
        return;
    }

    auto maybe_converted_texture = read_and_pixel_convert_texture_image_source(source, format, type);
    if (!maybe_converted_texture.has_value())
        return;
    auto converted_texture = maybe_converted_texture.release_value();
    ringl_tex_image_2d_from_bytes(target, level, internalformat,
                                  converted_texture.width, converted_texture.height, 0,
                                  format, type, converted_texture.buffer.data(), converted_texture.buffer.size());
}

void WebGLRenderingContextOverloads::tex_sub_image2d(WebIDL::UnsignedLong target, WebIDL::Long level, WebIDL::Long xoffset, WebIDL::Long yoffset, WebIDL::UnsignedLong format, WebIDL::UnsignedLong type, TexImageSource source)
{
    if (!make_rin_gl_current())
        return;
    if (uses_webgl_depth_texture_format(format)
        && !extension_enabled("WEBGL_depth_texture"sv)) {
        set_error(RINGL_INVALID_ENUM);
        return;
    }
    if (uses_oes_texture_float_format(format, type)
        && !extension_enabled("OES_texture_float"sv)) {
        set_error(RINGL_INVALID_ENUM);
        return;
    }
    if (uses_oes_texture_half_float_format(format, type)
        && !extension_enabled("OES_texture_half_float"sv)) {
        set_error(RINGL_INVALID_ENUM);
        return;
    }

    auto maybe_converted_texture = read_and_pixel_convert_texture_image_source(source, format, type);
    if (!maybe_converted_texture.has_value())
        return;
    auto converted_texture = maybe_converted_texture.release_value();
    ringl_tex_sub_image_2d_from_bytes(target, level, xoffset, yoffset,
                                      converted_texture.width, converted_texture.height,
                                      format, type, converted_texture.buffer.data(), converted_texture.buffer.size());
}

void WebGLRenderingContextOverloads::uniform1fv(GC::Root<WebGLUniformLocation> location, Float32List values)
{
    if (!make_rin_gl_current() || !location)
        return;
    WebIDL::Long location_handle;
    if (!validate_rin_gl_uniform_location(location, RINGL_FLOAT, location_handle))
        return;

    auto values_or_error = span_from_float32_list(values, /* src_offset= */ 0);
    if (values_or_error.is_error()) {
        set_error(RINGL_INVALID_VALUE);
        return;
    }
    auto view = values_or_error.release_value();
    if (view.is_empty())
        return;
    // RSH1 has one scalar declaration per location and no uniform arrays.
    if (view.size() != 1) {
        set_error(RINGL_INVALID_OPERATION);
        return;
    }
    ringl_uniform_1f(location_handle, view[0]);
}

void WebGLRenderingContextOverloads::uniform2fv(GC::Root<WebGLUniformLocation> location, Float32List values)
{
    if (!make_rin_gl_current() || !location)
        return;
    WebIDL::Long location_handle;
    if (!validate_rin_gl_uniform_location(location, RINGL_FLOAT_VEC2, location_handle))
        return;

    auto values_or_error = span_from_float32_list(values, /* src_offset= */ 0);
    if (values_or_error.is_error()) {
        set_error(RINGL_INVALID_VALUE);
        return;
    }
    auto view = values_or_error.release_value();
    if (view.size() % 2 != 0) {
        set_error(RINGL_INVALID_VALUE);
        return;
    }
    if (view.is_empty())
        return;
    // RSH1 has one vec2 declaration per location and no uniform arrays.
    if (view.size() != 2) {
        set_error(RINGL_INVALID_OPERATION);
        return;
    }
    ringl_uniform_2f(location_handle, view[0], view[1]);
}

void WebGLRenderingContextOverloads::uniform3fv(GC::Root<WebGLUniformLocation> location, Float32List values)
{
    if (!make_rin_gl_current() || !location)
        return;
    WebIDL::Long location_handle;
    if (!validate_rin_gl_uniform_location(location, RINGL_FLOAT_VEC3, location_handle))
        return;

    auto values_or_error = span_from_float32_list(values, /* src_offset= */ 0);
    if (values_or_error.is_error()) {
        set_error(RINGL_INVALID_VALUE);
        return;
    }
    auto view = values_or_error.release_value();
    if (view.size() % 3 != 0) {
        set_error(RINGL_INVALID_VALUE);
        return;
    }
    if (view.is_empty())
        return;
    // RSH1 has one vec3 declaration per location and no uniform arrays.
    if (view.size() != 3) {
        set_error(RINGL_INVALID_OPERATION);
        return;
    }
    ringl_uniform_3f(location_handle, view[0], view[1], view[2]);
}

void WebGLRenderingContextOverloads::uniform4fv(GC::Root<WebGLUniformLocation> location, Float32List values)
{
    if (!make_rin_gl_current() || !location)
        return;
    WebIDL::Long location_handle;
    if (!validate_rin_gl_uniform_location(location, RINGL_FLOAT_VEC4, location_handle))
        return;

    auto values_or_error = span_from_float32_list(values, /* src_offset= */ 0);
    if (values_or_error.is_error()) {
        set_error(RINGL_INVALID_VALUE);
        return;
    }
    auto view = values_or_error.release_value();
    if (view.size() % 4 != 0) {
        set_error(RINGL_INVALID_VALUE);
        return;
    }
    if (view.is_empty())
        return;
    // RSH1 exposes a single vec4 declaration, never a uniform array. Reject
    // surplus values before the backend mutates the linked executable.
    if (view.size() != 4) {
        set_error(RINGL_INVALID_OPERATION);
        return;
    }
    ringl_uniform_4f(location_handle, view[0], view[1], view[2], view[3]);
}

void WebGLRenderingContextOverloads::uniform1iv(GC::Root<WebGLUniformLocation> location, Int32List v)
{
    if (!make_rin_gl_current() || !location)
        return;

    WebIDL::Long location_handle;
    // WebGL permits uniform1iv for either a scalar int or sampler uniform.
    // RinGL keeps the linked program type check adjacent to the state update.
    if (!validate_rin_gl_uniform_location(location, 0, location_handle))
        return;

    auto span_or_error = span_from_int32_list(v, /* src_offset= */ 0);
    if (span_or_error.is_error()) {
        set_error(RINGL_INVALID_VALUE);
        return;
    }
    auto span = span_or_error.release_value();
    if (span.is_empty())
        return;
    if (span.size() != 1) {
        // The bounded profile supports scalar int and sampler declarations,
        // but not uniform arrays.
        set_error(RINGL_INVALID_OPERATION);
        return;
    }
    ringl_uniform_1i(location_handle, span[0]);
}

void WebGLRenderingContextOverloads::uniform2iv(GC::Root<WebGLUniformLocation> location, Int32List v)
{
    if (!make_rin_gl_current() || !location)
        return;
    WebIDL::Long location_handle;
    if (!validate_rin_gl_uniform_location(location, RINGL_INT_VEC2, location_handle))
        return;
    auto span_or_error = span_from_int32_list(v, /* src_offset= */ 0);
    if (span_or_error.is_error()) {
        set_error(RINGL_INVALID_VALUE);
        return;
    }
    auto span = span_or_error.release_value();
    if (span.is_empty())
        return;
    if (span.size() != 2) {
        set_error(RINGL_INVALID_OPERATION);
        return;
    }
    ringl_uniform_2i(location_handle, span[0], span[1]);
}

void WebGLRenderingContextOverloads::uniform3iv(GC::Root<WebGLUniformLocation> location, Int32List v)
{
    if (!make_rin_gl_current() || !location)
        return;
    WebIDL::Long location_handle;
    if (!validate_rin_gl_uniform_location(location, RINGL_INT_VEC3, location_handle))
        return;
    auto span_or_error = span_from_int32_list(v, /* src_offset= */ 0);
    if (span_or_error.is_error()) {
        set_error(RINGL_INVALID_VALUE);
        return;
    }
    auto span = span_or_error.release_value();
    if (span.is_empty())
        return;
    if (span.size() != 3) {
        set_error(RINGL_INVALID_OPERATION);
        return;
    }
    ringl_uniform_3i(location_handle, span[0], span[1], span[2]);
}

void WebGLRenderingContextOverloads::uniform4iv(GC::Root<WebGLUniformLocation> location, Int32List v)
{
    if (!make_rin_gl_current() || !location)
        return;
    WebIDL::Long location_handle;
    if (!validate_rin_gl_uniform_location(location, RINGL_INT_VEC4, location_handle))
        return;
    auto span_or_error = span_from_int32_list(v, /* src_offset= */ 0);
    if (span_or_error.is_error()) {
        set_error(RINGL_INVALID_VALUE);
        return;
    }
    auto span = span_or_error.release_value();
    if (span.is_empty())
        return;
    if (span.size() != 4) {
        set_error(RINGL_INVALID_OPERATION);
        return;
    }
    ringl_uniform_4i(location_handle, span[0], span[1], span[2], span[3]);
}

void WebGLRenderingContextOverloads::uniform_matrix2fv(GC::Root<WebGLUniformLocation> location, bool transpose, Float32List)
{
    if (!make_rin_gl_current() || !location)
        return;
    WebIDL::Long location_handle;
    if (!validate_rin_gl_sampler_uniform_location(location, location_handle))
        return;
    if (transpose) {
        set_error(RINGL_INVALID_VALUE);
        return;
    }
    set_error(RINGL_INVALID_OPERATION);
}

void WebGLRenderingContextOverloads::uniform_matrix3fv(GC::Root<WebGLUniformLocation> location, bool transpose, Float32List)
{
    if (!make_rin_gl_current() || !location)
        return;
    WebIDL::Long location_handle;
    if (!validate_rin_gl_sampler_uniform_location(location, location_handle))
        return;
    if (transpose) {
        set_error(RINGL_INVALID_VALUE);
        return;
    }
    set_error(RINGL_INVALID_OPERATION);
}

void WebGLRenderingContextOverloads::uniform_matrix4fv(GC::Root<WebGLUniformLocation> location, bool transpose, Float32List values)
{
    if (!make_rin_gl_current() || !location)
        return;
    WebIDL::Long location_handle;
    if (transpose) {
        set_error(RINGL_INVALID_VALUE);
        return;
    }
    if (!validate_rin_gl_uniform_location(location, RINGL_FLOAT_MAT4, location_handle))
        return;

    auto values_or_error = span_from_float32_list(values, /* src_offset= */ 0);
    if (values_or_error.is_error()) {
        set_error(RINGL_INVALID_VALUE);
        return;
    }
    auto view = values_or_error.release_value();
    if (view.size() % 16 != 0) {
        set_error(RINGL_INVALID_VALUE);
        return;
    }
    if (view.is_empty())
        return;
    // RinGL exposes one mat4 declaration per location and no uniform arrays.
    if (view.size() != 16) {
        set_error(RINGL_INVALID_OPERATION);
        return;
    }
    ringl_uniform_matrix4fv(location_handle, 0u, view.data());
}

}
