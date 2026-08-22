/*
 * Copyright (c) 2026, RinOS contributors
 *
 * SPDX-License-Identifier: BSD-2-Clause
 */

#include <LibWeb/WebGL/OpenGLContext.h>
#include <LibWeb/WebGL/WebGLRenderingContextImpl.h>

extern "C" {
#include <ringl/ringl.h>
#include <ringl/ringl_sync.h>
}

namespace Web::WebGL {

WebGLRenderingContextImpl::WebGLRenderingContextImpl(JS::Realm& realm, NonnullOwnPtr<OpenGLContext> context)
    : WebGLRenderingContextBase(realm)
    , m_context(move(context))
{
}

bool WebGLRenderingContextImpl::make_rin_gl_current()
{
    m_context->make_current();
    if (m_context->rin_gl_is_ready())
        return true;

    // `rin_gl_get_error()` retains the exact allocation/loss reason while the
    // backend surface is absent. `set_error()` consumes that reason before
    // considering the fallback supplied here.
    set_error(m_context->is_context_lost() ? RINGL_CONTEXT_LOST_WEBGL : RINGL_OUT_OF_MEMORY);
    return false;
}

void WebGLRenderingContextImpl::active_texture(WebIDL::UnsignedLong texture)
{
    if (!make_rin_gl_current())
        return;
    ringl_active_texture(texture);
}

void WebGLRenderingContextImpl::blend_color(float red, float green, float blue, float alpha)
{
    if (!make_rin_gl_current())
        return;
    ringl_blend_color(red, green, blue, alpha);
}

void WebGLRenderingContextImpl::blend_equation(WebIDL::UnsignedLong mode)
{
    if (!make_rin_gl_current())
        return;
    ringl_blend_equation(mode);
}

void WebGLRenderingContextImpl::blend_equation_separate(WebIDL::UnsignedLong mode_rgb, WebIDL::UnsignedLong mode_alpha)
{
    if (!make_rin_gl_current())
        return;
    ringl_blend_equation_separate(mode_rgb, mode_alpha);
}

void WebGLRenderingContextImpl::blend_func(WebIDL::UnsignedLong sfactor, WebIDL::UnsignedLong dfactor)
{
    if (!make_rin_gl_current())
        return;
    ringl_blend_func(sfactor, dfactor);
}

void WebGLRenderingContextImpl::blend_func_separate(WebIDL::UnsignedLong src_rgb, WebIDL::UnsignedLong dst_rgb, WebIDL::UnsignedLong src_alpha, WebIDL::UnsignedLong dst_alpha)
{
    if (!make_rin_gl_current())
        return;
    ringl_blend_func_separate(src_rgb, dst_rgb, src_alpha, dst_alpha);
}

void WebGLRenderingContextImpl::clear(WebIDL::UnsignedLong mask)
{
    if (!make_rin_gl_current())
        return;
    m_context->notify_content_will_change();
    needs_to_present();
    ringl_clear(mask);
}

void WebGLRenderingContextImpl::clear_color(float red, float green, float blue, float alpha)
{
    if (!make_rin_gl_current())
        return;
    ringl_clear_color(red, green, blue, alpha);
}

void WebGLRenderingContextImpl::clear_depth(float depth)
{
    if (!make_rin_gl_current())
        return;
    ringl_clear_depth(depth);
}

void WebGLRenderingContextImpl::clear_stencil(WebIDL::Long stencil)
{
    if (!make_rin_gl_current())
        return;
    ringl_clear_stencil(stencil);
}

void WebGLRenderingContextImpl::color_mask(bool red, bool green, bool blue, bool alpha)
{
    if (!make_rin_gl_current())
        return;
    ringl_color_mask(red, green, blue, alpha);
}

void WebGLRenderingContextImpl::cull_face(WebIDL::UnsignedLong mode)
{
    if (!make_rin_gl_current())
        return;
    ringl_cull_face(mode);
}

void WebGLRenderingContextImpl::depth_func(WebIDL::UnsignedLong func)
{
    if (!make_rin_gl_current())
        return;
    ringl_depth_func(func);
}

void WebGLRenderingContextImpl::depth_mask(bool flag)
{
    if (!make_rin_gl_current())
        return;
    ringl_depth_mask(flag ? RINGL_TRUE : RINGL_FALSE);
}

void WebGLRenderingContextImpl::disable(WebIDL::UnsignedLong cap)
{
    if (!make_rin_gl_current())
        return;
    ringl_disable(cap);
}

void WebGLRenderingContextImpl::disable_vertex_attrib_array(WebIDL::UnsignedLong index)
{
    if (!make_rin_gl_current())
        return;
    ringl_disable_vertex_attrib_array(index);
}

void WebGLRenderingContextImpl::draw_arrays(WebIDL::UnsignedLong mode, WebIDL::Long first, WebIDL::Long count)
{
    if (!make_rin_gl_current())
        return;
    m_context->notify_content_will_change();
    ringl_draw_arrays(mode, first, count);
    needs_to_present();
}

void WebGLRenderingContextImpl::draw_elements(WebIDL::UnsignedLong mode, WebIDL::Long count, WebIDL::UnsignedLong type, WebIDL::LongLong offset)
{
    if (!make_rin_gl_current())
        return;
    if (offset < 0) {
        // RinGL takes a byte offset as u64. Do not let a negative WebGL offset
        // wrap into a huge range and turn a validation error into a different
        // command before the backend sees it.
        set_error(RINGL_INVALID_VALUE);
        return;
    }
    m_context->notify_content_will_change();
    ringl_draw_elements(mode, count, type, offset);
    needs_to_present();
}

void WebGLRenderingContextImpl::enable(WebIDL::UnsignedLong cap)
{
    if (!make_rin_gl_current())
        return;
    ringl_enable(cap);
}

void WebGLRenderingContextImpl::enable_vertex_attrib_array(WebIDL::UnsignedLong index)
{
    if (!make_rin_gl_current())
        return;
    ringl_enable_vertex_attrib_array(index);
}

void WebGLRenderingContextImpl::finish()
{
    if (!make_rin_gl_current())
        return;
    ringl_finish();
}

void WebGLRenderingContextImpl::flush()
{
    if (!make_rin_gl_current())
        return;
    ringl_flush();
}

void WebGLRenderingContextImpl::front_face(WebIDL::UnsignedLong mode)
{
    if (!make_rin_gl_current())
        return;
    ringl_front_face(mode);
}

void WebGLRenderingContextImpl::pixel_storei(WebIDL::UnsignedLong pname, WebIDL::Long param)
{
    switch (pname) {
    case UNPACK_FLIP_Y_WEBGL:
        m_unpack_flip_y = param != 0;
        return;
    case UNPACK_PREMULTIPLY_ALPHA_WEBGL:
        m_unpack_premultiply_alpha = param != 0;
        return;
    case UNPACK_COLORSPACE_CONVERSION_WEBGL:
        m_unpack_colorspace_conversion = param;
        return;
    default:
        break;
    }
    if (!make_rin_gl_current())
        return;
    ringl_pixel_storei(pname, param);
}

void WebGLRenderingContextImpl::scissor(WebIDL::Long x, WebIDL::Long y, WebIDL::Long width, WebIDL::Long height)
{
    if (!make_rin_gl_current())
        return;
    ringl_scissor(x, y, width, height);
}

void WebGLRenderingContextImpl::stencil_func(WebIDL::UnsignedLong func, WebIDL::Long ref, WebIDL::UnsignedLong mask)
{
    if (!make_rin_gl_current())
        return;
    ringl_stencil_func(func, ref, mask);
}

void WebGLRenderingContextImpl::stencil_func_separate(WebIDL::UnsignedLong face, WebIDL::UnsignedLong func, WebIDL::Long ref, WebIDL::UnsignedLong mask)
{
    if (!make_rin_gl_current())
        return;
    ringl_stencil_func_separate(face, func, ref, mask);
}

void WebGLRenderingContextImpl::stencil_mask(WebIDL::UnsignedLong mask)
{
    if (!make_rin_gl_current())
        return;
    ringl_stencil_mask(mask);
}

void WebGLRenderingContextImpl::stencil_mask_separate(WebIDL::UnsignedLong face, WebIDL::UnsignedLong mask)
{
    if (!make_rin_gl_current())
        return;
    ringl_stencil_mask_separate(face, mask);
}

void WebGLRenderingContextImpl::stencil_op(WebIDL::UnsignedLong fail, WebIDL::UnsignedLong zfail, WebIDL::UnsignedLong zpass)
{
    if (!make_rin_gl_current())
        return;
    ringl_stencil_op(fail, zfail, zpass);
}

void WebGLRenderingContextImpl::stencil_op_separate(WebIDL::UnsignedLong face, WebIDL::UnsignedLong fail, WebIDL::UnsignedLong zfail, WebIDL::UnsignedLong zpass)
{
    if (!make_rin_gl_current())
        return;
    ringl_stencil_op_separate(face, fail, zfail, zpass);
}

void WebGLRenderingContextImpl::vertex_attrib1f(WebIDL::UnsignedLong index, float x)
{
    if (!make_rin_gl_current())
        return;
    ringl_vertex_attrib1f(index, x);
}

void WebGLRenderingContextImpl::vertex_attrib2f(WebIDL::UnsignedLong index, float x, float y)
{
    if (!make_rin_gl_current())
        return;
    ringl_vertex_attrib2f(index, x, y);
}

void WebGLRenderingContextImpl::vertex_attrib3f(WebIDL::UnsignedLong index, float x, float y, float z)
{
    if (!make_rin_gl_current())
        return;
    ringl_vertex_attrib3f(index, x, y, z);
}

void WebGLRenderingContextImpl::vertex_attrib4f(WebIDL::UnsignedLong index, float x, float y, float z, float w)
{
    if (!make_rin_gl_current())
        return;
    ringl_vertex_attrib4f(index, x, y, z, w);
}

void WebGLRenderingContextImpl::viewport(WebIDL::Long x, WebIDL::Long y, WebIDL::Long width, WebIDL::Long height)
{
    if (!make_rin_gl_current())
        return;
    ringl_viewport(x, y, width, height);
}

}
