/*
 * Copyright (c) 2026, RinOS contributors
 *
 * SPDX-License-Identifier: BSD-2-Clause
 */

#include <AK/ByteBuffer.h>
#include <AK/NumericLimits.h>
#include <LibJS/Runtime/Array.h>
#include <LibJS/Runtime/ArrayBuffer.h>
#include <LibJS/Runtime/TypedArray.h>
#include <LibWeb/WebGL/OpenGLContext.h>
#include <LibWeb/WebGL/WebGLActiveInfo.h>
#include <LibWeb/WebGL/WebGLBuffer.h>
#include <LibWeb/WebGL/WebGLFramebuffer.h>
#include <LibWeb/WebGL/WebGLProgram.h>
#include <LibWeb/WebGL/WebGLQuery.h>
#include <LibWeb/WebGL/WebGLRenderbuffer.h>
#include <LibWeb/WebGL/WebGLRenderingContextImpl.h>
#include <LibWeb/WebGL/WebGLShader.h>
#include <LibWeb/WebGL/WebGLTexture.h>
#include <LibWeb/WebGL/WebGLTransformFeedback.h>
#include <LibWeb/WebGL/WebGLUniformLocation.h>
#include <LibWeb/WebGL/WebGLVertexArrayObject.h>

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
    if (ringl_get_bound_buffer(target) != handle)
        return;
    if (target == RINGL_ARRAY_BUFFER)
        m_array_buffer_binding = buffer;
    else
        m_element_array_buffer_binding = buffer;
}

void WebGLRenderingContextImpl::bind_framebuffer(WebIDL::UnsignedLong target, GC::Root<WebGLFramebuffer> framebuffer)
{
    if (!make_rin_gl_current())
        return;
    if (target != RINGL_FRAMEBUFFER) {
        set_error(RINGL_INVALID_ENUM);
        return;
    }

    GLuint handle = 0;
    if (framebuffer) {
        auto handle_or_error = framebuffer->handle(this);
        if (handle_or_error.is_error()) {
            set_error(RINGL_INVALID_OPERATION);
            return;
        }
        handle = handle_or_error.release_value();
    }

    ringl_bind_framebuffer(target, handle);
    if (ringl_get_bound_framebuffer(target) == handle)
        m_framebuffer_binding = framebuffer;
}

void WebGLRenderingContextImpl::bind_renderbuffer(WebIDL::UnsignedLong target, GC::Root<WebGLRenderbuffer> renderbuffer)
{
    if (!make_rin_gl_current())
        return;
    if (target != RINGL_RENDERBUFFER) {
        set_error(RINGL_INVALID_ENUM);
        return;
    }

    GLuint handle = 0;
    if (renderbuffer) {
        auto handle_or_error = renderbuffer->handle(this);
        if (handle_or_error.is_error()) {
            set_error(RINGL_INVALID_OPERATION);
            return;
        }
        handle = handle_or_error.release_value();
    }

    ringl_bind_renderbuffer(target, handle);
    if (ringl_get_bound_renderbuffer(target) == handle)
        m_renderbuffer_binding = renderbuffer;
}

void WebGLRenderingContextImpl::bind_texture(WebIDL::UnsignedLong target, GC::Root<WebGLTexture> texture)
{
    if (!make_rin_gl_current())
        return;

    // RinGL currently exposes only the level-zero 2D texture profile. Check
    // the target before touching the WebGL binding cache.
    if (target != RINGL_TEXTURE_2D) {
        set_error(RINGL_INVALID_ENUM);
        return;
    }

    GLuint handle = 0;
    if (texture) {
        auto handle_or_error = texture->handle(this);
        if (handle_or_error.is_error()) {
            set_error(RINGL_INVALID_OPERATION);
            return;
        }
        handle = handle_or_error.release_value();
    }

    ringl_bind_texture(target, handle);
    auto active_texture = ringl_get_active_texture();
    if (ringl_get_bound_texture(target) == handle
        && active_texture >= RINGL_TEXTURE0
        && active_texture < RINGL_TEXTURE0 + 8u) {
        m_rin_texture_bindings_2d[active_texture - RINGL_TEXTURE0] = texture;
        m_texture_binding_2d = texture;
    }
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

GC::Root<WebGLFramebuffer> WebGLRenderingContextImpl::create_framebuffer()
{
    if (!make_rin_gl_current())
        return {};

    GLuint handle = 0;
    ringl_gen_framebuffers(1, &handle);
    if (handle == 0)
        return {};
    return WebGLFramebuffer::create(realm(), *this, handle);
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

GC::Root<WebGLRenderbuffer> WebGLRenderingContextImpl::create_renderbuffer()
{
    if (!make_rin_gl_current())
        return {};

    GLuint handle = 0;
    ringl_gen_renderbuffers(1, &handle);
    if (handle == 0)
        return {};
    return WebGLRenderbuffer::create(realm(), *this, handle);
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

GC::Root<WebGLTexture> WebGLRenderingContextImpl::create_texture()
{
    if (!make_rin_gl_current())
        return {};

    GLuint handle = 0;
    ringl_gen_textures(1, &handle);
    if (handle == 0)
        return {};
    return WebGLTexture::create(realm(), *this, handle);
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
    for (auto& vertex_attrib_buffer : m_rin_vertex_attrib_buffers) {
        if (vertex_attrib_buffer == buffer)
            vertex_attrib_buffer = nullptr;
    }
}

void WebGLRenderingContextImpl::delete_framebuffer(GC::Root<WebGLFramebuffer> framebuffer)
{
    if (!make_rin_gl_current())
        return;

    GLuint handle = 0;
    if (framebuffer) {
        auto handle_or_error = framebuffer->handle(this);
        if (handle_or_error.is_error()) {
            set_error(RINGL_INVALID_OPERATION);
            return;
        }
        handle = handle_or_error.release_value();
    }

    ringl_delete_framebuffers(1, &handle);
    if (m_framebuffer_binding == framebuffer)
        m_framebuffer_binding = nullptr;
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

    if (program && !program->is_deleted()) {
        ringl_delete_program(handle);
        program->mark_deleted();
    }
    if (m_current_program == program)
        m_current_program = nullptr;
}

void WebGLRenderingContextImpl::delete_renderbuffer(GC::Root<WebGLRenderbuffer> renderbuffer)
{
    if (!make_rin_gl_current())
        return;

    GLuint handle = 0;
    if (renderbuffer) {
        auto handle_or_error = renderbuffer->handle(this);
        if (handle_or_error.is_error()) {
            set_error(RINGL_INVALID_OPERATION);
            return;
        }
        handle = handle_or_error.release_value();
    }

    ringl_delete_renderbuffers(1, &handle);
    if (m_renderbuffer_binding == renderbuffer)
        m_renderbuffer_binding = nullptr;
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
    if (shader && !shader->is_deleted()) {
        ringl_delete_shader(handle);
        shader->mark_deleted();
    }
}

void WebGLRenderingContextImpl::delete_texture(GC::Root<WebGLTexture> texture)
{
    if (!make_rin_gl_current())
        return;

    GLuint handle = 0;
    if (texture) {
        auto handle_or_error = texture->handle(this);
        if (handle_or_error.is_error()) {
            set_error(RINGL_INVALID_OPERATION);
            return;
        }
        handle = handle_or_error.release_value();
    }

    ringl_delete_textures(1, &handle);
    for (auto& binding : m_rin_texture_bindings_2d) {
        if (binding == texture)
            binding = nullptr;
    }
    auto active_texture = ringl_get_active_texture();
    if (active_texture >= RINGL_TEXTURE0
        && active_texture < RINGL_TEXTURE0 + 8u)
        m_texture_binding_2d = m_rin_texture_bindings_2d[active_texture - RINGL_TEXTURE0];
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

void WebGLRenderingContextImpl::copy_tex_image2d(WebIDL::UnsignedLong target, WebIDL::Long level, WebIDL::UnsignedLong internalformat, WebIDL::Long x, WebIDL::Long y, WebIDL::Long width, WebIDL::Long height, WebIDL::Long border)
{
    if (!make_rin_gl_current())
        return;
    ringl_copy_tex_image_2d(target, level, internalformat, x, y, width,
                            height, border);
}

void WebGLRenderingContextImpl::copy_tex_sub_image2d(WebIDL::UnsignedLong target, WebIDL::Long level, WebIDL::Long xoffset, WebIDL::Long yoffset, WebIDL::Long x, WebIDL::Long y, WebIDL::Long width, WebIDL::Long height)
{
    if (!make_rin_gl_current())
        return;
    ringl_copy_tex_sub_image_2d(target, level, xoffset, yoffset, x, y,
                                width, height);
}

WebIDL::Long WebGLRenderingContextImpl::get_attrib_location(GC::Root<WebGLProgram> program, String name)
{
    if (!make_rin_gl_current())
        return -1;
    if (!program) {
        set_error(RINGL_INVALID_OPERATION);
        return -1;
    }

    auto handle_or_error = program->handle(this);
    if (handle_or_error.is_error()) {
        set_error(RINGL_INVALID_OPERATION);
        return -1;
    }
    auto handle = handle_or_error.release_value();
    if (ringl_is_program(handle) == 0) {
        set_error(RINGL_INVALID_OPERATION);
        return -1;
    }

    auto name_null_terminated = null_terminated_string(name);
    return ringl_get_attrib_location(handle, name_null_terminated.data());
}

Optional<Vector<GC::Root<WebGLShader>>> WebGLRenderingContextImpl::get_attached_shaders(GC::Root<WebGLProgram> program)
{
    if (!make_rin_gl_current())
        return OptionalNone {};
    if (!program) {
        set_error(RINGL_INVALID_OPERATION);
        return OptionalNone {};
    }

    auto handle_or_error = program->handle(this);
    if (handle_or_error.is_error()) {
        set_error(RINGL_INVALID_OPERATION);
        return OptionalNone {};
    }
    if (ringl_is_program(handle_or_error.release_value()) == 0) {
        set_error(RINGL_INVALID_OPERATION);
        return OptionalNone {};
    }

    Vector<GC::Root<WebGLShader>> result;
    if (program->attached_vertex_shader())
        result.append(GC::make_root(*program->attached_vertex_shader()));
    if (program->attached_fragment_shader())
        result.append(GC::make_root(*program->attached_fragment_shader()));
    return result;
}

JS::Value WebGLRenderingContextImpl::get_buffer_parameter(WebIDL::UnsignedLong target, WebIDL::UnsignedLong pname)
{
    if (!make_rin_gl_current())
        return JS::js_null();

    switch (pname) {
    case RINGL_BUFFER_SIZE:
        return JS::Value(static_cast<double>(ringl_get_buffer_size(target)));
    case RINGL_BUFFER_USAGE:
        return JS::Value(ringl_get_buffer_usage(target));
    default:
        set_error(RINGL_INVALID_ENUM);
        return JS::js_null();
    }
}

GC::Root<WebGLActiveInfo> WebGLRenderingContextImpl::get_active_attrib(
    GC::Root<WebGLProgram> program, WebIDL::UnsignedLong index)
{
    RinGLActiveInfoV1 info {};

    if (!make_rin_gl_current())
        return {};
    if (!program) {
        set_error(RINGL_INVALID_OPERATION);
        return {};
    }
    auto handle_or_error = program->handle(this);
    if (handle_or_error.is_error()) {
        set_error(RINGL_INVALID_OPERATION);
        return {};
    }
    info.struct_size = sizeof(info);
    info.api_version = RINGL_API_VERSION;
    if (ringl_get_active_attrib(handle_or_error.release_value(), index, &info) != 0)
        return {};
    if (info.name_length >= sizeof(info.name)) {
        set_error(RINGL_INVALID_OPERATION);
        return {};
    }
    auto name = String::from_utf8_without_validation(
        ReadonlyBytes { reinterpret_cast<u8 const*>(info.name), info.name_length });
    return WebGLActiveInfo::create(realm(), move(name), static_cast<GLenum>(info.type),
        static_cast<GLsizei>(info.size));
}

GC::Root<WebGLActiveInfo> WebGLRenderingContextImpl::get_active_uniform(
    GC::Root<WebGLProgram> program, WebIDL::UnsignedLong index)
{
    RinGLActiveInfoV1 info {};

    if (!make_rin_gl_current())
        return {};
    if (!program) {
        set_error(RINGL_INVALID_OPERATION);
        return {};
    }
    auto handle_or_error = program->handle(this);
    if (handle_or_error.is_error()) {
        set_error(RINGL_INVALID_OPERATION);
        return {};
    }
    info.struct_size = sizeof(info);
    info.api_version = RINGL_API_VERSION;
    if (ringl_get_active_uniform(handle_or_error.release_value(), index, &info) != 0)
        return {};
    if (info.name_length >= sizeof(info.name)) {
        set_error(RINGL_INVALID_OPERATION);
        return {};
    }
    auto name = String::from_utf8_without_validation(
        ReadonlyBytes { reinterpret_cast<u8 const*>(info.name), info.name_length });
    return WebGLActiveInfo::create(realm(), move(name), static_cast<GLenum>(info.type),
        static_cast<GLsizei>(info.size));
}

WebIDL::ExceptionOr<JS::Value> WebGLRenderingContextImpl::get_parameter(WebIDL::UnsignedLong pname)
{
    if (!make_rin_gl_current())
        return JS::js_null();

    Array<int32_t, 4> values {};
    auto get_integer = [&] {
        if (ringl_get_integerv_bounded(pname, values.data(), values.size()) == 0)
            return true;
        set_error(RINGL_INVALID_OPERATION);
        return false;
    };

    switch (pname) {
    case RINGL_ARRAY_BUFFER_BINDING: {
        if (!get_integer() || values[0] == 0)
            return JS::js_null();
        auto buffer = m_array_buffer_binding;
        if (!buffer) {
            set_error(RINGL_INVALID_OPERATION);
            return JS::js_null();
        }
        auto handle_or_error = buffer->handle(this);
        if (handle_or_error.is_error()
            || static_cast<int32_t>(handle_or_error.release_value()) != values[0]) {
            set_error(RINGL_INVALID_OPERATION);
            return JS::js_null();
        }
        return JS::Value(buffer);
    }
    case RINGL_ELEMENT_ARRAY_BUFFER_BINDING: {
        if (!get_integer() || values[0] == 0)
            return JS::js_null();
        auto buffer = m_element_array_buffer_binding;
        if (!buffer) {
            set_error(RINGL_INVALID_OPERATION);
            return JS::js_null();
        }
        auto handle_or_error = buffer->handle(this);
        if (handle_or_error.is_error()
            || static_cast<int32_t>(handle_or_error.release_value()) != values[0]) {
            set_error(RINGL_INVALID_OPERATION);
            return JS::js_null();
        }
        return JS::Value(buffer);
    }
    case RINGL_CURRENT_PROGRAM: {
        if (!get_integer() || values[0] == 0)
            return JS::js_null();
        auto program = m_current_program;
        if (!program) {
            set_error(RINGL_INVALID_OPERATION);
            return JS::js_null();
        }
        auto handle_or_error = program->handle(this);
        if (handle_or_error.is_error()
            || static_cast<int32_t>(handle_or_error.release_value()) != values[0]) {
            set_error(RINGL_INVALID_OPERATION);
            return JS::js_null();
        }
        return JS::Value(program);
    }
    case RINGL_FRAMEBUFFER_BINDING: {
        if (!get_integer() || values[0] == 0)
            return JS::js_null();
        auto framebuffer = m_framebuffer_binding;
        if (!framebuffer) {
            set_error(RINGL_INVALID_OPERATION);
            return JS::js_null();
        }
        auto handle_or_error = framebuffer->handle(this);
        if (handle_or_error.is_error()
            || static_cast<int32_t>(handle_or_error.release_value()) != values[0]) {
            set_error(RINGL_INVALID_OPERATION);
            return JS::js_null();
        }
        return JS::Value(framebuffer);
    }
    case RINGL_RENDERBUFFER_BINDING: {
        if (!get_integer() || values[0] == 0)
            return JS::js_null();
        auto renderbuffer = m_renderbuffer_binding;
        if (!renderbuffer) {
            set_error(RINGL_INVALID_OPERATION);
            return JS::js_null();
        }
        auto handle_or_error = renderbuffer->handle(this);
        if (handle_or_error.is_error()
            || static_cast<int32_t>(handle_or_error.release_value()) != values[0]) {
            set_error(RINGL_INVALID_OPERATION);
            return JS::js_null();
        }
        return JS::Value(renderbuffer);
    }
    case RINGL_TEXTURE_BINDING_2D: {
        if (!get_integer() || values[0] == 0)
            return JS::js_null();
        auto texture = m_texture_binding_2d;
        if (!texture) {
            set_error(RINGL_INVALID_OPERATION);
            return JS::js_null();
        }
        auto handle_or_error = texture->handle(this);
        if (handle_or_error.is_error()
            || static_cast<int32_t>(handle_or_error.release_value()) != values[0]) {
            set_error(RINGL_INVALID_OPERATION);
            return JS::js_null();
        }
        return JS::Value(texture);
    }
    case RINGL_COLOR_CLEAR_VALUE: {
        RinGLClearValuesV1 clear_values {};
        clear_values.struct_size = sizeof(clear_values);
        clear_values.api_version = RINGL_API_VERSION;
        if (ringl_get_clear_values(&clear_values) != 0) {
            set_error(RINGL_INVALID_OPERATION);
            return JS::js_null();
        }
        Array<float, 4> result {
            clear_values.red,
            clear_values.green,
            clear_values.blue,
            clear_values.alpha,
        };
        auto bytes_or_error = ByteBuffer::copy(result.span().reinterpret<u8>());
        if (bytes_or_error.is_error()) {
            set_error(RINGL_OUT_OF_MEMORY);
            return JS::js_null();
        }
        auto array_buffer = JS::ArrayBuffer::create(realm(), bytes_or_error.release_value());
        return JS::Float32Array::create(realm(), result.size(), array_buffer);
    }
    case RINGL_BLEND_COLOR: {
        RinGLBlendColorV1 blend_color {};
        blend_color.struct_size = sizeof(blend_color);
        blend_color.api_version = RINGL_API_VERSION;
        if (ringl_get_blend_color(&blend_color) != 0) {
            set_error(RINGL_INVALID_OPERATION);
            return JS::js_null();
        }
        Array<float, 4> result {
            blend_color.red,
            blend_color.green,
            blend_color.blue,
            blend_color.alpha,
        };
        auto bytes_or_error = ByteBuffer::copy(result.span().reinterpret<u8>());
        if (bytes_or_error.is_error()) {
            set_error(RINGL_OUT_OF_MEMORY);
            return JS::js_null();
        }
        auto array_buffer = JS::ArrayBuffer::create(realm(), bytes_or_error.release_value());
        return JS::Float32Array::create(realm(), result.size(), array_buffer);
    }
    case RINGL_DEPTH_RANGE: {
        RinGLDepthRangeV1 depth_range {};
        depth_range.struct_size = sizeof(depth_range);
        depth_range.api_version = RINGL_API_VERSION;
        if (ringl_get_depth_range(&depth_range) != 0) {
            set_error(RINGL_INVALID_OPERATION);
            return JS::js_null();
        }
        Array<float, 2> result {
            depth_range.z_near,
            depth_range.z_far,
        };
        auto bytes_or_error = ByteBuffer::copy(result.span().reinterpret<u8>());
        if (bytes_or_error.is_error()) {
            set_error(RINGL_OUT_OF_MEMORY);
            return JS::js_null();
        }
        auto array_buffer = JS::ArrayBuffer::create(realm(), bytes_or_error.release_value());
        return JS::Float32Array::create(realm(), result.size(), array_buffer);
    }
    case RINGL_LINE_WIDTH: {
        RinGLLineWidthV1 line_width {};
        line_width.struct_size = sizeof(line_width);
        line_width.api_version = RINGL_API_VERSION;
        if (ringl_get_line_width(&line_width) != 0) {
            set_error(RINGL_INVALID_OPERATION);
            return JS::js_null();
        }
        return JS::Value(line_width.width);
    }
    case RINGL_ALIASED_LINE_WIDTH_RANGE: {
        RinGLLineWidthV1 line_width {};
        line_width.struct_size = sizeof(line_width);
        line_width.api_version = RINGL_API_VERSION;
        if (ringl_get_line_width(&line_width) != 0) {
            set_error(RINGL_INVALID_OPERATION);
            return JS::js_null();
        }
        Array<float, 2> result {
            line_width.minimum,
            line_width.maximum,
        };
        auto bytes_or_error = ByteBuffer::copy(result.span().reinterpret<u8>());
        if (bytes_or_error.is_error()) {
            set_error(RINGL_OUT_OF_MEMORY);
            return JS::js_null();
        }
        auto array_buffer = JS::ArrayBuffer::create(realm(), bytes_or_error.release_value());
        return JS::Float32Array::create(realm(), result.size(), array_buffer);
    }
    case RINGL_SAMPLE_COVERAGE_VALUE: {
        RinGLSampleCoverageV1 sample_coverage {};
        sample_coverage.struct_size = sizeof(sample_coverage);
        sample_coverage.api_version = RINGL_API_VERSION;
        if (ringl_get_sample_coverage(&sample_coverage) != 0) {
            set_error(RINGL_INVALID_OPERATION);
            return JS::js_null();
        }
        return JS::Value(sample_coverage.value);
    }
    case RINGL_DEPTH_CLEAR_VALUE:
    case RINGL_STENCIL_CLEAR_VALUE: {
        RinGLClearValuesV1 clear_values {};
        clear_values.struct_size = sizeof(clear_values);
        clear_values.api_version = RINGL_API_VERSION;
        if (ringl_get_clear_values(&clear_values) != 0) {
            set_error(RINGL_INVALID_OPERATION);
            return JS::js_null();
        }
        if (pname == RINGL_DEPTH_CLEAR_VALUE)
            return JS::Value(clear_values.depth);
        return JS::Value(clear_values.stencil);
    }
    case RINGL_COLOR_WRITEMASK: {
        if (!get_integer())
            return JS::js_null();
        auto sequence = TRY(JS::Array::create(realm(), values.size()));
        for (size_t index = 0; index < values.size(); ++index)
            TRY(sequence->create_data_property(JS::PropertyKey(index), JS::Value(values[index] != 0)));
        return JS::Value(sequence);
    }
    case RINGL_VIEWPORT:
    case RINGL_SCISSOR_BOX: {
        if (!get_integer())
            return JS::js_null();
        auto bytes_or_error = ByteBuffer::copy(values.span().reinterpret<u8>());
        if (bytes_or_error.is_error()) {
            set_error(RINGL_OUT_OF_MEMORY);
            return JS::js_null();
        }
        auto array_buffer = JS::ArrayBuffer::create(realm(), bytes_or_error.release_value());
        return JS::Int32Array::create(realm(), values.size(), array_buffer);
    }
    case RINGL_BLEND:
    case RINGL_CULL_FACE:
    case RINGL_DEPTH_TEST:
    case RINGL_SCISSOR_TEST:
    case RINGL_STENCIL_TEST:
    case RINGL_SAMPLE_COVERAGE:
        return JS::Value(ringl_is_enabled(pname) != 0);
    case RINGL_DEPTH_WRITEMASK:
        if (!get_integer())
            return JS::js_null();
        return JS::Value(values[0] != 0);
    case RINGL_ACTIVE_TEXTURE:
    case RINGL_UNPACK_ALIGNMENT:
    case RINGL_MAX_TEXTURE_SIZE_QUERY:
    case RINGL_MAX_TEXTURE_IMAGE_UNITS:
    case RINGL_MAX_COMBINED_TEXTURE_IMAGE_UNITS:
    case RINGL_MAX_VERTEX_ATTRIBS_QUERY:
    case RINGL_CULL_FACE_MODE:
    case RINGL_FRONT_FACE:
    case RINGL_DEPTH_FUNC:
    case RINGL_STENCIL_FUNC:
    case RINGL_STENCIL_REF:
    case RINGL_STENCIL_VALUE_MASK:
    case RINGL_STENCIL_FAIL:
    case RINGL_STENCIL_PASS_DEPTH_FAIL:
    case RINGL_STENCIL_PASS_DEPTH_PASS:
    case RINGL_STENCIL_WRITEMASK:
    case RINGL_STENCIL_BACK_FUNC:
    case RINGL_STENCIL_BACK_FAIL:
    case RINGL_STENCIL_BACK_PASS_DEPTH_FAIL:
    case RINGL_STENCIL_BACK_PASS_DEPTH_PASS:
    case RINGL_STENCIL_BACK_REF:
    case RINGL_STENCIL_BACK_VALUE_MASK:
    case RINGL_STENCIL_BACK_WRITEMASK:
    case RINGL_BLEND_SRC_RGB:
    case RINGL_BLEND_DST_RGB:
    case RINGL_BLEND_SRC_ALPHA:
    case RINGL_BLEND_DST_ALPHA:
    case RINGL_BLEND_EQUATION_RGB:
    case RINGL_BLEND_EQUATION_ALPHA:
    case RINGL_SAMPLE_COVERAGE_INVERT:
        if (!get_integer())
            return JS::js_null();
        return JS::Value(values[0]);
    default:
        set_error(RINGL_INVALID_ENUM);
        return JS::js_null();
    }
}

WebIDL::UnsignedLong WebGLRenderingContextImpl::get_error()
{
    (void)make_rin_gl_current();
    return get_error_value();
}

JS::Value WebGLRenderingContextImpl::get_program_parameter(GC::Root<WebGLProgram> program, WebIDL::UnsignedLong pname)
{
    if (!make_rin_gl_current())
        return JS::js_null();

    switch (pname) {
    case RINGL_DELETE_STATUS:
    case RINGL_LINK_STATUS:
    case RINGL_VALIDATE_STATUS:
    case RINGL_ATTACHED_SHADERS:
    case RINGL_ACTIVE_ATTRIBUTES:
    case RINGL_ACTIVE_UNIFORMS:
        break;
    default:
        set_error(RINGL_INVALID_ENUM);
        return JS::js_null();
    }

    if (!program) {
        set_error(RINGL_INVALID_OPERATION);
        return JS::js_null();
    }
    auto handle_or_error = program->handle(this);
    if (handle_or_error.is_error()) {
        set_error(RINGL_INVALID_OPERATION);
        return JS::js_null();
    }
    if (pname == RINGL_DELETE_STATUS)
        return JS::Value(program->is_deleted());
    auto handle = handle_or_error.release_value();
    if (ringl_is_program(handle) == 0) {
        set_error(RINGL_INVALID_OPERATION);
        return JS::js_null();
    }

    RinGLProgramInfoV1 info {};
    info.struct_size = sizeof(info);
    info.api_version = RINGL_API_VERSION;
    if (ringl_get_program_info(handle, &info) != 0)
        return JS::js_null();

    switch (pname) {
    case RINGL_LINK_STATUS:
        return JS::Value(info.link_status == RINGL_TRUE);
    case RINGL_VALIDATE_STATUS:
        return JS::Value(info.validate_status == RINGL_TRUE);
    case RINGL_ATTACHED_SHADERS:
        return JS::Value(info.attached_shader_count);
    case RINGL_ACTIVE_ATTRIBUTES:
        return JS::Value(info.active_attribute_count);
    case RINGL_ACTIVE_UNIFORMS:
        return JS::Value(info.active_uniform_count);
    default:
        return JS::js_null();
    }
}

JS::Value WebGLRenderingContextImpl::get_shader_parameter(GC::Root<WebGLShader> shader, WebIDL::UnsignedLong pname)
{
    if (!make_rin_gl_current())
        return JS::js_null();

    if (pname != RINGL_DELETE_STATUS && pname != RINGL_SHADER_TYPE &&
        pname != RINGL_COMPILE_STATUS) {
        set_error(RINGL_INVALID_ENUM);
        return JS::js_null();
    }
    if (!shader) {
        set_error(RINGL_INVALID_OPERATION);
        return JS::js_null();
    }
    auto handle_or_error = shader->handle(this);
    if (handle_or_error.is_error()) {
        set_error(RINGL_INVALID_OPERATION);
        return JS::js_null();
    }
    if (pname == RINGL_DELETE_STATUS)
        return JS::Value(shader->is_deleted());
    auto handle = handle_or_error.release_value();
    if (ringl_is_shader(handle) == 0) {
        set_error(RINGL_INVALID_OPERATION);
        return JS::js_null();
    }

    if (pname == RINGL_SHADER_TYPE)
        return JS::Value(ringl_get_shader_type(handle));
    return JS::Value(ringl_get_shader_compile_status(handle) == RINGL_TRUE);
}

JS::Value WebGLRenderingContextImpl::get_renderbuffer_parameter(WebIDL::UnsignedLong target, WebIDL::UnsignedLong pname)
{
    if (!make_rin_gl_current())
        return JS::js_null();

    switch (pname) {
    case RINGL_RENDERBUFFER_WIDTH:
    case RINGL_RENDERBUFFER_HEIGHT:
    case RINGL_RENDERBUFFER_INTERNAL_FORMAT:
    case RINGL_RENDERBUFFER_RED_SIZE:
    case RINGL_RENDERBUFFER_GREEN_SIZE:
    case RINGL_RENDERBUFFER_BLUE_SIZE:
    case RINGL_RENDERBUFFER_ALPHA_SIZE:
    case RINGL_RENDERBUFFER_DEPTH_SIZE:
    case RINGL_RENDERBUFFER_STENCIL_SIZE:
        break;
    default:
        set_error(RINGL_INVALID_ENUM);
        return JS::js_null();
    }

    RinGLRenderbufferInfoV1 info {};
    info.struct_size = sizeof(info);
    info.api_version = RINGL_API_VERSION;
    if (ringl_get_renderbuffer_info(target, &info) != 0)
        return JS::js_null();

    switch (pname) {
    case RINGL_RENDERBUFFER_WIDTH:
        return JS::Value(info.width);
    case RINGL_RENDERBUFFER_HEIGHT:
        return JS::Value(info.height);
    case RINGL_RENDERBUFFER_INTERNAL_FORMAT:
        return JS::Value(info.internal_format);
    case RINGL_RENDERBUFFER_RED_SIZE:
        return JS::Value(info.red_size);
    case RINGL_RENDERBUFFER_GREEN_SIZE:
        return JS::Value(info.green_size);
    case RINGL_RENDERBUFFER_BLUE_SIZE:
        return JS::Value(info.blue_size);
    case RINGL_RENDERBUFFER_ALPHA_SIZE:
        return JS::Value(info.alpha_size);
    case RINGL_RENDERBUFFER_DEPTH_SIZE:
        return JS::Value(info.depth_size);
    case RINGL_RENDERBUFFER_STENCIL_SIZE:
        return JS::Value(info.stencil_size);
    default:
        return JS::js_null();
    }
}

Optional<String> WebGLRenderingContextImpl::get_program_info_log(GC::Root<WebGLProgram> program)
{
    if (!make_rin_gl_current())
        return {};
    if (!program) {
        set_error(RINGL_INVALID_OPERATION);
        return {};
    }

    auto handle_or_error = program->handle(this);
    if (handle_or_error.is_error()) {
        set_error(RINGL_INVALID_OPERATION);
        return {};
    }
    auto handle = handle_or_error.release_value();
    if (ringl_is_program(handle) == 0) {
        set_error(RINGL_INVALID_OPERATION);
        return {};
    }

    auto length = ringl_get_program_info_log(handle, nullptr, 0);
    if (length == 0)
        return String {};
    if (length >= NumericLimits<size_t>::max()) {
        set_error(RINGL_OUT_OF_MEMORY);
        return {};
    }
    auto storage_or_error = ByteBuffer::create_uninitialized(static_cast<size_t>(length + 1));
    if (storage_or_error.is_error()) {
        set_error(RINGL_OUT_OF_MEMORY);
        return {};
    }
    auto storage = storage_or_error.release_value();
    ringl_get_program_info_log(handle, reinterpret_cast<char*>(storage.data()), storage.size());
    return String::from_utf8_without_validation(ReadonlyBytes { storage.data(), static_cast<size_t>(length) });
}

Optional<String> WebGLRenderingContextImpl::get_shader_info_log(GC::Root<WebGLShader> shader)
{
    if (!make_rin_gl_current())
        return {};
    if (!shader) {
        set_error(RINGL_INVALID_OPERATION);
        return {};
    }

    auto handle_or_error = shader->handle(this);
    if (handle_or_error.is_error()) {
        set_error(RINGL_INVALID_OPERATION);
        return {};
    }
    auto handle = handle_or_error.release_value();
    if (ringl_is_shader(handle) == 0) {
        set_error(RINGL_INVALID_OPERATION);
        return {};
    }

    auto length = ringl_get_shader_info_log(handle, nullptr, 0);
    if (length == 0)
        return String {};
    if (length >= NumericLimits<size_t>::max()) {
        set_error(RINGL_OUT_OF_MEMORY);
        return {};
    }
    auto storage_or_error = ByteBuffer::create_uninitialized(static_cast<size_t>(length + 1));
    if (storage_or_error.is_error()) {
        set_error(RINGL_OUT_OF_MEMORY);
        return {};
    }
    auto storage = storage_or_error.release_value();
    ringl_get_shader_info_log(handle, reinterpret_cast<char*>(storage.data()), storage.size());
    return String::from_utf8_without_validation(ReadonlyBytes { storage.data(), static_cast<size_t>(length) });
}

Optional<String> WebGLRenderingContextImpl::get_shader_source(GC::Root<WebGLShader> shader)
{
    if (!make_rin_gl_current())
        return {};
    if (!shader) {
        set_error(RINGL_INVALID_OPERATION);
        return {};
    }

    auto handle_or_error = shader->handle(this);
    if (handle_or_error.is_error()) {
        set_error(RINGL_INVALID_OPERATION);
        return {};
    }
    auto handle = handle_or_error.release_value();
    if (ringl_is_shader(handle) == 0) {
        set_error(RINGL_INVALID_OPERATION);
        return {};
    }

    auto length = ringl_get_shader_source_length(handle);
    if (length == 0)
        return String {};
    if (length >= NumericLimits<size_t>::max()) {
        set_error(RINGL_OUT_OF_MEMORY);
        return {};
    }
    auto storage_or_error = ByteBuffer::create_uninitialized(static_cast<size_t>(length + 1));
    if (storage_or_error.is_error()) {
        set_error(RINGL_OUT_OF_MEMORY);
        return {};
    }
    auto storage = storage_or_error.release_value();
    ringl_copy_shader_source(handle, reinterpret_cast<char*>(storage.data()), storage.size());
    return String::from_utf8_without_validation(ReadonlyBytes { storage.data(), static_cast<size_t>(length) });
}

JS::Value WebGLRenderingContextImpl::get_tex_parameter(WebIDL::UnsignedLong target, WebIDL::UnsignedLong pname)
{
    if (!make_rin_gl_current())
        return JS::js_null();

    switch (pname) {
    case RINGL_TEXTURE_MAG_FILTER:
    case RINGL_TEXTURE_MIN_FILTER:
    case RINGL_TEXTURE_WRAP_S:
    case RINGL_TEXTURE_WRAP_T:
        return JS::Value(ringl_get_tex_parameteri(target, pname));
    default:
        set_error(RINGL_INVALID_ENUM);
        return JS::js_null();
    }
}

JS::Value WebGLRenderingContextImpl::get_vertex_attrib(WebIDL::UnsignedLong index, WebIDL::UnsignedLong pname)
{
    if (!make_rin_gl_current())
        return JS::js_null();

    if (pname == RINGL_CURRENT_VERTEX_ATTRIB) {
        Array<float, 4> result;
        if (ringl_get_vertex_attrib_current(index, result.data()) != 0)
            return JS::js_null();

        auto bytes_or_error = ByteBuffer::copy(result.span().reinterpret<u8>());
        if (bytes_or_error.is_error()) {
            set_error(RINGL_OUT_OF_MEMORY);
            return JS::js_null();
        }
        auto array_buffer = JS::ArrayBuffer::create(realm(), bytes_or_error.release_value());
        return JS::Float32Array::create(realm(), result.size(), array_buffer);
    }

    switch (pname) {
    case RINGL_VERTEX_ATTRIB_ARRAY_BUFFER_BINDING:
    case RINGL_VERTEX_ATTRIB_ARRAY_ENABLED:
    case RINGL_VERTEX_ATTRIB_ARRAY_NORMALIZED:
    case RINGL_VERTEX_ATTRIB_ARRAY_SIZE:
    case RINGL_VERTEX_ATTRIB_ARRAY_STRIDE:
    case RINGL_VERTEX_ATTRIB_ARRAY_TYPE:
        break;
    default:
        set_error(RINGL_INVALID_ENUM);
        return JS::js_null();
    }

    RinGLVertexAttribInfoV1 info {};
    info.struct_size = sizeof(info);
    info.api_version = RINGL_API_VERSION;
    if (ringl_get_vertex_attrib(index, &info) != 0)
        return JS::js_null();

    switch (pname) {
    case RINGL_VERTEX_ATTRIB_ARRAY_BUFFER_BINDING: {
        if (info.buffer == 0)
            return JS::js_null();

        auto buffer = m_rin_vertex_attrib_buffers[index];
        if (!buffer) {
            set_error(RINGL_INVALID_OPERATION);
            return JS::js_null();
        }
        auto handle_or_error = buffer->handle(this);
        if (handle_or_error.is_error() || handle_or_error.release_value() != info.buffer) {
            set_error(RINGL_INVALID_OPERATION);
            return JS::js_null();
        }
        return JS::Value(buffer);
    }
    case RINGL_VERTEX_ATTRIB_ARRAY_ENABLED:
        return JS::Value(info.enabled == RINGL_TRUE);
    case RINGL_VERTEX_ATTRIB_ARRAY_NORMALIZED:
        return JS::Value(info.normalized == RINGL_TRUE);
    case RINGL_VERTEX_ATTRIB_ARRAY_SIZE:
        return JS::Value(info.size);
    case RINGL_VERTEX_ATTRIB_ARRAY_STRIDE:
        return JS::Value(info.stride);
    case RINGL_VERTEX_ATTRIB_ARRAY_TYPE:
        return JS::Value(info.type);
    default:
        return JS::js_null();
    }
}

WebIDL::LongLong WebGLRenderingContextImpl::get_vertex_attrib_offset(WebIDL::UnsignedLong index, WebIDL::UnsignedLong pname)
{
    if (!make_rin_gl_current())
        return 0;
    if (pname != RINGL_VERTEX_ATTRIB_ARRAY_POINTER) {
        set_error(RINGL_INVALID_ENUM);
        return 0;
    }

    RinGLVertexAttribInfoV1 info {};
    info.struct_size = sizeof(info);
    info.api_version = RINGL_API_VERSION;
    if (ringl_get_vertex_attrib(index, &info) != 0)
        return 0;
    if (info.offset > static_cast<uint64_t>(NumericLimits<WebIDL::LongLong>::max())) {
        set_error(RINGL_INVALID_OPERATION);
        return 0;
    }
    return static_cast<WebIDL::LongLong>(info.offset);
}

JS::Value WebGLRenderingContextImpl::get_uniform(GC::Root<WebGLProgram> program, GC::Root<WebGLUniformLocation> location)
{
    if (!make_rin_gl_current())
        return JS::js_null();
    if (!program || !location) {
        set_error(RINGL_INVALID_OPERATION);
        return JS::js_null();
    }

    auto program_handle_or_error = program->handle(this);
    if (program_handle_or_error.is_error()) {
        set_error(RINGL_INVALID_OPERATION);
        return JS::js_null();
    }
    auto program_handle = program_handle_or_error.release_value();
    if (ringl_is_program(program_handle) == 0) {
        set_error(RINGL_INVALID_OPERATION);
        return JS::js_null();
    }

    auto location_handle_or_error = location->handle(program.ptr());
    if (location_handle_or_error.is_error()) {
        set_error(RINGL_INVALID_OPERATION);
        return JS::js_null();
    }

    int32_t value = 0;
    if (ringl_get_uniform_1i(program_handle,
                             static_cast<int32_t>(location_handle_or_error.release_value()),
                             &value) != 0)
        return JS::js_null();
    return JS::Value(value);
}

GC::Root<WebGLUniformLocation> WebGLRenderingContextImpl::get_uniform_location(GC::Root<WebGLProgram> program, String name)
{
    if (!make_rin_gl_current())
        return {};
    if (!program) {
        set_error(RINGL_INVALID_OPERATION);
        return {};
    }

    auto handle_or_error = program->handle(this);
    if (handle_or_error.is_error()) {
        set_error(RINGL_INVALID_OPERATION);
        return {};
    }
    auto handle = handle_or_error.release_value();
    if (ringl_is_program(handle) == 0) {
        set_error(RINGL_INVALID_OPERATION);
        return {};
    }

    auto name_null_terminated = null_terminated_string(name);
    auto location = ringl_get_uniform_location(handle, name_null_terminated.data());
    if (location < 0)
        return {};
    return WebGLUniformLocation::create(realm(), static_cast<GLuint>(location), program.ptr());
}

bool WebGLRenderingContextImpl::is_framebuffer(GC::Root<WebGLFramebuffer> framebuffer)
{
    if (!make_rin_gl_current() || !framebuffer)
        return false;

    auto handle_or_error = framebuffer->handle(this);
    if (handle_or_error.is_error()) {
        set_error(RINGL_INVALID_OPERATION);
        return false;
    }
    return ringl_is_framebuffer(handle_or_error.release_value()) != 0;
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

bool WebGLRenderingContextImpl::is_renderbuffer(GC::Root<WebGLRenderbuffer> renderbuffer)
{
    if (!make_rin_gl_current() || !renderbuffer)
        return false;

    auto handle_or_error = renderbuffer->handle(this);
    if (handle_or_error.is_error()) {
        set_error(RINGL_INVALID_OPERATION);
        return false;
    }
    return ringl_is_renderbuffer(handle_or_error.release_value()) != 0;
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

bool WebGLRenderingContextImpl::is_texture(GC::Root<WebGLTexture> texture)
{
    if (!make_rin_gl_current() || !texture)
        return false;

    auto handle_or_error = texture->handle(this);
    if (handle_or_error.is_error()) {
        set_error(RINGL_INVALID_OPERATION);
        return false;
    }
    return ringl_is_texture(handle_or_error.release_value()) != 0;
}

bool WebGLRenderingContextImpl::is_enabled(WebIDL::UnsignedLong cap)
{
    if (!make_rin_gl_current())
        return false;
    return ringl_is_enabled(cap) != 0;
}

void WebGLRenderingContextImpl::line_width(float width)
{
    if (!make_rin_gl_current())
        return;
    ringl_line_width(width);
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

void WebGLRenderingContextImpl::validate_program(GC::Root<WebGLProgram> program)
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
    auto handle = handle_or_error.release_value();
    if (ringl_is_program(handle) == 0) {
        set_error(RINGL_INVALID_OPERATION);
        return;
    }
    ringl_validate_program(handle);
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
    if (ringl_get_active_texture() == texture
        && texture >= RINGL_TEXTURE0
        && texture < RINGL_TEXTURE0 + 8u)
        m_texture_binding_2d = m_rin_texture_bindings_2d[texture - RINGL_TEXTURE0];
}

void WebGLRenderingContextImpl::tex_parameteri(WebIDL::UnsignedLong target, WebIDL::UnsignedLong pname, WebIDL::Long param)
{
    if (!make_rin_gl_current())
        return;
    ringl_tex_parameteri(target, pname, param);
}

void WebGLRenderingContextImpl::uniform1i(GC::Root<WebGLUniformLocation> location, WebIDL::Long x)
{
    if (!make_rin_gl_current())
        return;
    // WebGL permits a null uniform location as a no-op. A non-null location
    // must belong to the program currently bound through useProgram().
    if (!location)
        return;

    auto location_handle_or_error = location->handle(m_current_program);
    if (location_handle_or_error.is_error()) {
        set_error(RINGL_INVALID_OPERATION);
        return;
    }
    ringl_uniform_1i(static_cast<WebIDL::Long>(location_handle_or_error.release_value()), x);
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

void WebGLRenderingContextImpl::depth_range(float z_near, float z_far)
{
    if (!make_rin_gl_current())
        return;
    ringl_depth_range(z_near, z_far);
}

void WebGLRenderingContextImpl::polygon_offset(float factor, float units)
{
    if (!make_rin_gl_current())
        return;
    ringl_polygon_offset(factor, units);
}

void WebGLRenderingContextImpl::hint(WebIDL::UnsignedLong target, WebIDL::UnsignedLong mode)
{
    if (!make_rin_gl_current())
        return;
    ringl_hint(target, mode);
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

void WebGLRenderingContextImpl::framebuffer_renderbuffer(WebIDL::UnsignedLong target, WebIDL::UnsignedLong attachment, WebIDL::UnsignedLong renderbuffertarget, GC::Root<WebGLRenderbuffer> renderbuffer)
{
    if (!make_rin_gl_current())
        return;

    GLuint handle = 0;
    if (renderbuffer) {
        auto handle_or_error = renderbuffer->handle(this);
        if (handle_or_error.is_error()) {
            set_error(RINGL_INVALID_OPERATION);
            return;
        }
        handle = handle_or_error.release_value();
    }
    ringl_framebuffer_renderbuffer(target, attachment, renderbuffertarget, handle);
}

void WebGLRenderingContextImpl::framebuffer_texture2d(WebIDL::UnsignedLong target, WebIDL::UnsignedLong attachment, WebIDL::UnsignedLong textarget, GC::Root<WebGLTexture> texture, WebIDL::Long level)
{
    if (!make_rin_gl_current())
        return;

    GLuint handle = 0;
    if (texture) {
        auto handle_or_error = texture->handle(this);
        if (handle_or_error.is_error()) {
            set_error(RINGL_INVALID_OPERATION);
            return;
        }
        handle = handle_or_error.release_value();
    }
    ringl_framebuffer_texture_2d(target, attachment, textarget, handle, level);
}

WebIDL::UnsignedLong WebGLRenderingContextImpl::check_framebuffer_status(WebIDL::UnsignedLong target)
{
    if (!make_rin_gl_current())
        return 0;
    return ringl_check_framebuffer_status(target);
}

void WebGLRenderingContextImpl::renderbuffer_storage(WebIDL::UnsignedLong target, WebIDL::UnsignedLong internalformat, WebIDL::Long width, WebIDL::Long height)
{
    if (!make_rin_gl_current())
        return;
    if (internalformat == RINGL_DEPTH_STENCIL)
        internalformat = RINGL_DEPTH24_STENCIL8;
    ringl_renderbuffer_storage(target, internalformat, width, height);
}

void WebGLRenderingContextImpl::scissor(WebIDL::Long x, WebIDL::Long y, WebIDL::Long width, WebIDL::Long height)
{
    if (!make_rin_gl_current())
        return;
    ringl_scissor(x, y, width, height);
}

void WebGLRenderingContextImpl::sample_coverage(float value, bool invert)
{
    if (!make_rin_gl_current())
        return;
    ringl_sample_coverage(value, invert ? RINGL_TRUE : RINGL_FALSE);
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

void WebGLRenderingContextImpl::vertex_attrib1fv(WebIDL::UnsignedLong index, Float32List values)
{
    if (!make_rin_gl_current())
        return;
    auto values_or_error = span_from_float32_list(values, 0);
    if (values_or_error.is_error()) {
        set_error(RINGL_INVALID_VALUE);
        return;
    }
    auto view = values_or_error.release_value();
    if (view.size() < 1) {
        set_error(RINGL_INVALID_VALUE);
        return;
    }
    ringl_vertex_attrib1f(index, view[0]);
}

void WebGLRenderingContextImpl::vertex_attrib2fv(WebIDL::UnsignedLong index, Float32List values)
{
    if (!make_rin_gl_current())
        return;
    auto values_or_error = span_from_float32_list(values, 0);
    if (values_or_error.is_error()) {
        set_error(RINGL_INVALID_VALUE);
        return;
    }
    auto view = values_or_error.release_value();
    if (view.size() < 2) {
        set_error(RINGL_INVALID_VALUE);
        return;
    }
    ringl_vertex_attrib2f(index, view[0], view[1]);
}

void WebGLRenderingContextImpl::vertex_attrib3fv(WebIDL::UnsignedLong index, Float32List values)
{
    if (!make_rin_gl_current())
        return;
    auto values_or_error = span_from_float32_list(values, 0);
    if (values_or_error.is_error()) {
        set_error(RINGL_INVALID_VALUE);
        return;
    }
    auto view = values_or_error.release_value();
    if (view.size() < 3) {
        set_error(RINGL_INVALID_VALUE);
        return;
    }
    ringl_vertex_attrib3f(index, view[0], view[1], view[2]);
}

void WebGLRenderingContextImpl::vertex_attrib4fv(WebIDL::UnsignedLong index, Float32List values)
{
    if (!make_rin_gl_current())
        return;
    auto values_or_error = span_from_float32_list(values, 0);
    if (values_or_error.is_error()) {
        set_error(RINGL_INVALID_VALUE);
        return;
    }
    auto view = values_or_error.release_value();
    if (view.size() < 4) {
        set_error(RINGL_INVALID_VALUE);
        return;
    }
    ringl_vertex_attrib4f(index, view[0], view[1], view[2], view[3]);
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

    RinGLVertexAttribInfoV1 info {};
    info.struct_size = sizeof(info);
    info.api_version = RINGL_API_VERSION;
    if (ringl_get_vertex_attrib(index, &info) != 0 || info.buffer == 0)
        return;

    auto bound_buffer = ringl_get_bound_buffer(RINGL_ARRAY_BUFFER);
    if (info.buffer != bound_buffer || !m_array_buffer_binding)
        return;
    auto handle_or_error = m_array_buffer_binding->handle(this);
    if (!handle_or_error.is_error() && handle_or_error.release_value() == info.buffer)
        m_rin_vertex_attrib_buffers[index] = m_array_buffer_binding;
}

void WebGLRenderingContextImpl::viewport(WebIDL::Long x, WebIDL::Long y, WebIDL::Long width, WebIDL::Long height)
{
    if (!make_rin_gl_current())
        return;
    ringl_viewport(x, y, width, height);
}

void WebGLRenderingContextImpl::visit_edges(JS::Cell::Visitor& visitor)
{
    Base::visit_edges(visitor);

    visitor.visit(m_array_buffer_binding);
    visitor.visit(m_element_array_buffer_binding);
    visitor.visit(m_current_program);
    visitor.visit(m_framebuffer_binding);
    visitor.visit(m_renderbuffer_binding);
    visitor.visit(m_texture_binding_2d);
    visitor.visit(m_texture_binding_cube_map);
    for (auto& binding : m_rin_texture_bindings_2d)
        visitor.visit(binding);
    for (auto& buffer : m_rin_vertex_attrib_buffers)
        visitor.visit(buffer);

    visitor.visit(m_uniform_buffer_binding);
    visitor.visit(m_copy_read_buffer_binding);
    visitor.visit(m_copy_write_buffer_binding);
    visitor.visit(m_transform_feedback_buffer_binding);
    visitor.visit(m_texture_binding_2d_array);
    visitor.visit(m_texture_binding_3d);
    visitor.visit(m_transform_feedback_binding);
    visitor.visit(m_pixel_pack_buffer_binding);
    visitor.visit(m_pixel_unpack_buffer_binding);
    visitor.visit(m_current_vertex_array);
    visitor.visit(m_any_samples_passed);
    visitor.visit(m_any_samples_passed_conservative);
    visitor.visit(m_transform_feedback_primitives_written);
}

}
