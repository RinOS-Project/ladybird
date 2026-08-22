/*
 * Copyright (c) 2026, RinOS contributors
 *
 * SPDX-License-Identifier: BSD-2-Clause
 */

#include <AK/ByteBuffer.h>
#include <AK/NumericLimits.h>
#include <LibWeb/WebGL/OpenGLContext.h>
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
    if (target == RINGL_ARRAY_BUFFER) {
        m_array_buffer_binding = buffer;
        return;
    }

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

    ringl_delete_program(handle);
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
    ringl_delete_shader(handle);
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
