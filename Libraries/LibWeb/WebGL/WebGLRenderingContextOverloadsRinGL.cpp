/*
 * Copyright (c) 2026, RinOS contributors.
 *
 * SPDX-License-Identifier: BSD-2-Clause
 */

#include <AK/NumericLimits.h>
#include <LibWeb/WebGL/OpenGLContext.h>
#include <LibWeb/WebGL/WebGLRenderingContextOverloads.h>
#include <LibWeb/WebGL/WebGLUniformLocation.h>

extern "C" {
#include <ringl/ringl.h>
#include <ringl/ringl_sync.h>
}

namespace Web::WebGL {

WebGLRenderingContextOverloads::WebGLRenderingContextOverloads(JS::Realm& realm, NonnullOwnPtr<OpenGLContext> context)
    : WebGLRenderingContextImpl(realm, move(context))
{
}

void WebGLRenderingContextOverloads::buffer_data(WebIDL::UnsignedLong target, WebIDL::LongLong size, WebIDL::UnsignedLong usage)
{
    if (!make_rin_gl_current())
        return;

    ringl_buffer_data(target, size, nullptr, usage);
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
    ringl_buffer_data(target, static_cast<WebIDL::LongLong>(span.size()), span.data(), usage);
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
    ringl_buffer_sub_data(target, offset, static_cast<WebIDL::LongLong>(span.size()), span.data());
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

    if (!pixels) {
        ringl_tex_image_2d_from_bytes(target, level, internalformat, width, height, border, format, type, nullptr, 0);
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
    if (!pixels) {
        set_error(RINGL_INVALID_VALUE);
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

    auto maybe_converted_texture = read_and_pixel_convert_texture_image_source(source, format, type);
    if (!maybe_converted_texture.has_value())
        return;
    auto converted_texture = maybe_converted_texture.release_value();
    ringl_tex_sub_image_2d_from_bytes(target, level, xoffset, yoffset,
                                      converted_texture.width, converted_texture.height,
                                      format, type, converted_texture.buffer.data(), converted_texture.buffer.size());
}

void WebGLRenderingContextOverloads::uniform1fv(GC::Root<WebGLUniformLocation> location, Float32List)
{
    if (!make_rin_gl_current() || !location)
        return;
    WebIDL::Long location_handle;
    if (!validate_rin_gl_sampler_uniform_location(location, location_handle))
        return;
    set_error(RINGL_INVALID_OPERATION);
}

void WebGLRenderingContextOverloads::uniform2fv(GC::Root<WebGLUniformLocation> location, Float32List)
{
    if (!make_rin_gl_current() || !location)
        return;
    WebIDL::Long location_handle;
    if (!validate_rin_gl_sampler_uniform_location(location, location_handle))
        return;
    set_error(RINGL_INVALID_OPERATION);
}

void WebGLRenderingContextOverloads::uniform3fv(GC::Root<WebGLUniformLocation> location, Float32List)
{
    if (!make_rin_gl_current() || !location)
        return;
    WebIDL::Long location_handle;
    if (!validate_rin_gl_sampler_uniform_location(location, location_handle))
        return;
    set_error(RINGL_INVALID_OPERATION);
}

void WebGLRenderingContextOverloads::uniform4fv(GC::Root<WebGLUniformLocation> location, Float32List)
{
    if (!make_rin_gl_current() || !location)
        return;
    WebIDL::Long location_handle;
    if (!validate_rin_gl_sampler_uniform_location(location, location_handle))
        return;
    set_error(RINGL_INVALID_OPERATION);
}

void WebGLRenderingContextOverloads::uniform1iv(GC::Root<WebGLUniformLocation> location, Int32List v)
{
    if (!make_rin_gl_current() || !location)
        return;

    WebIDL::Long location_handle;
    if (!validate_rin_gl_sampler_uniform_location(location, location_handle))
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
        // The current RSH1 profile exposes scalar sampler2D declarations,
        // never sampler arrays.
        set_error(RINGL_INVALID_OPERATION);
        return;
    }
    ringl_uniform_1i(location_handle, span[0]);
}

void WebGLRenderingContextOverloads::uniform2iv(GC::Root<WebGLUniformLocation> location, Int32List)
{
    if (!make_rin_gl_current() || !location)
        return;
    WebIDL::Long location_handle;
    if (!validate_rin_gl_sampler_uniform_location(location, location_handle))
        return;
    set_error(RINGL_INVALID_OPERATION);
}

void WebGLRenderingContextOverloads::uniform3iv(GC::Root<WebGLUniformLocation> location, Int32List)
{
    if (!make_rin_gl_current() || !location)
        return;
    WebIDL::Long location_handle;
    if (!validate_rin_gl_sampler_uniform_location(location, location_handle))
        return;
    set_error(RINGL_INVALID_OPERATION);
}

void WebGLRenderingContextOverloads::uniform4iv(GC::Root<WebGLUniformLocation> location, Int32List)
{
    if (!make_rin_gl_current() || !location)
        return;
    WebIDL::Long location_handle;
    if (!validate_rin_gl_sampler_uniform_location(location, location_handle))
        return;
    set_error(RINGL_INVALID_OPERATION);
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

void WebGLRenderingContextOverloads::uniform_matrix4fv(GC::Root<WebGLUniformLocation> location, bool transpose, Float32List)
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

}
