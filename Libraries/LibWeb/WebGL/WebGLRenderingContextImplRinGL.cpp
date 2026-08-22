/*
 * Copyright (c) 2026, RinOS contributors
 *
 * SPDX-License-Identifier: BSD-2-Clause
 */

#include <AK/NumericLimits.h>
#include <LibWeb/WebGL/OpenGLContext.h>
#include <LibWeb/WebGL/WebGLBuffer.h>
#include <LibWeb/WebGL/WebGLProgram.h>
#include <LibWeb/WebGL/WebGLRenderingContextImpl.h>
#include <LibWeb/WebGL/WebGLShader.h>

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

void WebGLRenderingContextImpl::attach_shader(GC::Root<WebGLProgram> program, GC::Root<WebGLShader> shader)
{
    if (!make_rin_gl_current())
        return;
    if (!program || !shader) {
        set_error(RINGL_INVALID_OPERATION);
        return;
    }

    auto program_handle_or_error = program->handle(this);
    auto shader_handle_or_error = shader->handle(this);
    if (program_handle_or_error.is_error() || shader_handle_or_error.is_error()) {
        set_error(RINGL_INVALID_OPERATION);
        return;
    }
    auto program_handle = program_handle_or_error.release_value();
    auto shader_handle = shader_handle_or_error.release_value();

    if (ringl_is_program(program_handle) == 0 || ringl_is_shader(shader_handle) == 0) {
        set_error(RINGL_INVALID_OPERATION);
        return;
    }

    if (program->attached_vertex_shader() == shader || program->attached_fragment_shader() == shader) {
        set_error(RINGL_INVALID_OPERATION);
        return;
    }
    if ((shader->type() == RINGL_VERTEX_SHADER && program->attached_vertex_shader())
        || (shader->type() == RINGL_FRAGMENT_SHADER && program->attached_fragment_shader())) {
        set_error(RINGL_INVALID_OPERATION);
        return;
    }
    if (shader->type() != RINGL_VERTEX_SHADER && shader->type() != RINGL_FRAGMENT_SHADER) {
        set_error(RINGL_INVALID_OPERATION);
        return;
    }

    ringl_attach_shader(program_handle, shader_handle);
    if (shader->type() == RINGL_VERTEX_SHADER)
        program->set_attached_vertex_shader(shader.ptr());
    else
        program->set_attached_fragment_shader(shader.ptr());
}

void WebGLRenderingContextImpl::bind_attrib_location(GC::Root<WebGLProgram> program, WebIDL::UnsignedLong index, String name)
{
    if (!make_rin_gl_current())
        return;
    if (!program) {
        set_error(RINGL_INVALID_OPERATION);
        return;
    }

    auto program_handle_or_error = program->handle(this);
    if (program_handle_or_error.is_error()) {
        set_error(RINGL_INVALID_OPERATION);
        return;
    }

    auto program_handle = program_handle_or_error.release_value();
    if (ringl_is_program(program_handle) == 0) {
        set_error(RINGL_INVALID_OPERATION);
        return;
    }

    auto name_null_terminated = null_terminated_string(name);
    ringl_bind_attrib_location(program_handle, index, name_null_terminated.data());
}

void WebGLRenderingContextImpl::bind_buffer(WebIDL::UnsignedLong target, GC::Root<WebGLBuffer> buffer)
{
    if (!make_rin_gl_current())
        return;

    if (target != RINGL_ARRAY_BUFFER && target != RINGL_ELEMENT_ARRAY_BUFFER) {
        set_error(RINGL_INVALID_ENUM);
        return;
    }

    GLuint handle = 0;
    if (buffer) {
        auto handle_or_error = buffer->handle(this);
        if (handle_or_error.is_error()) {
            set_error(RINGL_INVALID_OPERATION);
            return;
        }
        handle = handle_or_error.release_value();
        if (!buffer->is_compatible_with(target)) {
            set_error(RINGL_INVALID_OPERATION);
            return;
        }
    }

    ringl_bind_buffer(target, handle);
    if (target == RINGL_ARRAY_BUFFER) {
        m_array_buffer_binding = buffer;
        return;
    }

    m_element_array_buffer_binding = buffer;
}

GC::Root<WebGLBuffer> WebGLRenderingContextImpl::create_buffer()
{
    if (!make_rin_gl_current())
        return {};

    GLuint handle = 0;
    ringl_gen_buffers(1, &handle);
    if (handle == 0)
        return {};
    return WebGLBuffer::create(realm(), *this, handle);
}

GC::Root<WebGLProgram> WebGLRenderingContextImpl::create_program()
{
    if (!make_rin_gl_current())
        return {};

    auto handle = ringl_create_program();
    if (handle == 0)
        return {};
    return WebGLProgram::create(realm(), *this, handle);
}

GC::Root<WebGLShader> WebGLRenderingContextImpl::create_shader(WebIDL::UnsignedLong type)
{
    if (!make_rin_gl_current())
        return {};
    if (type != RINGL_VERTEX_SHADER && type != RINGL_FRAGMENT_SHADER) {
        set_error(RINGL_INVALID_ENUM);
        return {};
    }

    auto handle = ringl_create_shader(type);
    if (handle == 0)
        return {};
    return WebGLShader::create(realm(), *this, handle, type);
}

void WebGLRenderingContextImpl::delete_buffer(GC::Root<WebGLBuffer> buffer)
{
    if (!make_rin_gl_current())
        return;

    GLuint handle = 0;
    if (buffer) {
        auto handle_or_error = buffer->handle(this);
        if (handle_or_error.is_error()) {
            set_error(RINGL_INVALID_OPERATION);
            return;
        }
        handle = handle_or_error.release_value();
    }

    ringl_delete_buffers(1, &handle);
    if (m_array_buffer_binding == buffer)
        m_array_buffer_binding = nullptr;
    if (m_element_array_buffer_binding == buffer)
        m_element_array_buffer_binding = nullptr;
}

void WebGLRenderingContextImpl::delete_program(GC::Root<WebGLProgram> program)
{
    if (!make_rin_gl_current())
        return;

    GLuint handle = 0;
    if (program) {
        auto handle_or_error = program->handle(this);
        if (handle_or_error.is_error()) {
            set_error(RINGL_INVALID_OPERATION);
            return;
        }
        handle = handle_or_error.release_value();
    }

    ringl_delete_program(handle);
    if (m_current_program == program)
        m_current_program = nullptr;
}

void WebGLRenderingContextImpl::delete_shader(GC::Root<WebGLShader> shader)
{
    if (!make_rin_gl_current())
        return;

    GLuint handle = 0;
    if (shader) {
        auto handle_or_error = shader->handle(this);
        if (handle_or_error.is_error()) {
            set_error(RINGL_INVALID_OPERATION);
            return;
        }
        handle = handle_or_error.release_value();
    }
    ringl_delete_shader(handle);
}

void WebGLRenderingContextImpl::detach_shader(GC::Root<WebGLProgram> program, GC::Root<WebGLShader> shader)
{
    if (!make_rin_gl_current())
        return;
    if (!program || !shader) {
        set_error(RINGL_INVALID_OPERATION);
        return;
    }

    auto program_handle_or_error = program->handle(this);
    auto shader_handle_or_error = shader->handle(this);
    if (program_handle_or_error.is_error() || shader_handle_or_error.is_error()) {
        set_error(RINGL_INVALID_OPERATION);
        return;
    }

    auto program_handle = program_handle_or_error.release_value();
    auto shader_handle = shader_handle_or_error.release_value();
    if (ringl_is_program(program_handle) == 0) {
        set_error(RINGL_INVALID_OPERATION);
        return;
    }
    ringl_detach_shader(program_handle, shader_handle);
    if (shader->type() == RINGL_VERTEX_SHADER && program->attached_vertex_shader() == shader)
        program->set_attached_vertex_shader(nullptr);
    else if (shader->type() == RINGL_FRAGMENT_SHADER && program->attached_fragment_shader() == shader)
        program->set_attached_fragment_shader(nullptr);
}

void WebGLRenderingContextImpl::compile_shader(GC::Root<WebGLShader> shader)
{
    if (!make_rin_gl_current())
        return;
    if (!shader) {
        set_error(RINGL_INVALID_OPERATION);
        return;
    }

    auto handle_or_error = shader->handle(this);
    if (handle_or_error.is_error()) {
        set_error(RINGL_INVALID_OPERATION);
        return;
    }
    ringl_compile_shader(handle_or_error.release_value());
}

bool WebGLRenderingContextImpl::is_program(GC::Root<WebGLProgram> program)
{
    if (!make_rin_gl_current() || !program)
        return false;

    auto handle_or_error = program->handle(this);
    if (handle_or_error.is_error()) {
        set_error(RINGL_INVALID_OPERATION);
        return false;
    }
    return ringl_is_program(handle_or_error.release_value()) != 0;
}

bool WebGLRenderingContextImpl::is_shader(GC::Root<WebGLShader> shader)
{
    if (!make_rin_gl_current() || !shader)
        return false;

    auto handle_or_error = shader->handle(this);
    if (handle_or_error.is_error()) {
        set_error(RINGL_INVALID_OPERATION);
        return false;
    }
    return ringl_is_shader(handle_or_error.release_value()) != 0;
}

void WebGLRenderingContextImpl::link_program(GC::Root<WebGLProgram> program)
{
    if (!make_rin_gl_current())
        return;
    if (!program) {
        set_error(RINGL_INVALID_OPERATION);
        return;
    }

    auto handle_or_error = program->handle(this);
    if (handle_or_error.is_error()) {
        set_error(RINGL_INVALID_OPERATION);
        return;
    }
    ringl_link_program(handle_or_error.release_value());
}

void WebGLRenderingContextImpl::shader_source(GC::Root<WebGLShader> shader, String source)
{
    if (!make_rin_gl_current())
        return;
    if (!shader) {
        set_error(RINGL_INVALID_OPERATION);
        return;
    }

    auto handle_or_error = shader->handle(this);
    if (handle_or_error.is_error()) {
        set_error(RINGL_INVALID_OPERATION);
        return;
    }

    auto source_bytes = source.bytes();
    if (source_bytes.size() > static_cast<size_t>(NumericLimits<WebIDL::LongLong>::max())) {
        set_error(RINGL_OUT_OF_MEMORY);
        return;
    }
    static constexpr char empty_source[] = "";
    auto source_data = source_bytes.is_empty() ? empty_source : reinterpret_cast<char const*>(source_bytes.data());
    ringl_shader_source(handle_or_error.release_value(), source_data, static_cast<WebIDL::LongLong>(source_bytes.size()));
}

void WebGLRenderingContextImpl::use_program(GC::Root<WebGLProgram> program)
{
    if (!make_rin_gl_current())
        return;

    GLuint handle = 0;
    if (program) {
        auto handle_or_error = program->handle(this);
        if (handle_or_error.is_error()) {
            set_error(RINGL_INVALID_OPERATION);
            return;
        }
        handle = handle_or_error.release_value();
    }

    ringl_use_program(handle);
    if (handle == 0 || ringl_get_current_program() == handle)
        m_current_program = program;
}

bool WebGLRenderingContextImpl::is_buffer(GC::Root<WebGLBuffer> buffer)
{
    if (!make_rin_gl_current())
        return false;
    if (!buffer)
        return false;

    auto handle_or_error = buffer->handle(this);
    if (handle_or_error.is_error()) {
        set_error(RINGL_INVALID_OPERATION);
        return false;
    }
    return ringl_is_buffer(handle_or_error.release_value()) != 0;
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

void WebGLRenderingContextImpl::vertex_attrib_pointer(WebIDL::UnsignedLong index, WebIDL::Long size, WebIDL::UnsignedLong type, bool normalized, WebIDL::Long stride, WebIDL::LongLong offset)
{
    if (!make_rin_gl_current())
        return;

    // The RinGL ABI takes an unsigned byte offset. Reject before conversion so
    // a negative WebGL offset cannot become a large, aligned native offset.
    if (offset < 0) {
        set_error(RINGL_INVALID_VALUE);
        return;
    }

    ringl_vertex_attrib_pointer(index, size, type, normalized ? RINGL_TRUE : RINGL_FALSE, stride, static_cast<uint64_t>(offset));
}

void WebGLRenderingContextImpl::viewport(WebIDL::Long x, WebIDL::Long y, WebIDL::Long width, WebIDL::Long height)
{
    if (!make_rin_gl_current())
        return;
    ringl_viewport(x, y, width, height);
}

}
