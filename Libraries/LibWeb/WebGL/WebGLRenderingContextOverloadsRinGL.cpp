/*
 * Copyright (c) 2026, RinOS contributors.
 *
 * SPDX-License-Identifier: BSD-2-Clause
 */

#include <AK/NumericLimits.h>
#include <LibWeb/WebGL/OpenGLContext.h>
#include <LibWeb/WebGL/WebGLRenderingContextOverloads.h>

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

}
