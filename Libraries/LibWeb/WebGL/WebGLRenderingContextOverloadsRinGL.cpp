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

}
