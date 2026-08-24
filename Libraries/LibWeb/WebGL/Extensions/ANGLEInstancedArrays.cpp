/*
 * Copyright (c) 2024, Aliaksandr Kalenik <kalenik.aliaksandr@gmail.com>
 *
 * SPDX-License-Identifier: BSD-2-Clause
 */

#include <LibJS/Runtime/Realm.h>
#include <LibWeb/Bindings/ANGLEInstancedArraysPrototype.h>
#include <LibWeb/Bindings/Intrinsics.h>
#include <LibWeb/WebGL/Extensions/ANGLEInstancedArrays.h>
#include <LibWeb/WebGL/OpenGLContext.h>
#include <LibWeb/WebGL/WebGLRenderingContextBase.h>

#ifdef AK_OS_RINOS
#include <LibWeb/WebGL/WebGLRenderingContextImpl.h>
#else
#define GL_GLEXT_PROTOTYPES 1
#include <GLES2/gl2.h>
#include <GLES2/gl2ext.h>
#endif

namespace Web::WebGL::Extensions {

GC_DEFINE_ALLOCATOR(ANGLEInstancedArrays);

JS::ThrowCompletionOr<GC::Ref<JS::Object>> ANGLEInstancedArrays::create(JS::Realm& realm, GC::Ref<WebGLRenderingContextBase> context)
{
    return realm.create<ANGLEInstancedArrays>(realm, context);
}

ANGLEInstancedArrays::ANGLEInstancedArrays(JS::Realm& realm, GC::Ref<WebGLRenderingContextBase> context)
    : PlatformObject(realm)
    , m_context(context)
{
}

void ANGLEInstancedArrays::vertex_attrib_divisor_angle(GLuint index, GLuint divisor)
{
#ifdef AK_OS_RINOS
    static_cast<WebGLRenderingContextImpl&>(*m_context)
        .vertex_attrib_divisor_angle(index, divisor);
#else
    m_context->context().make_current();
    glVertexAttribDivisorANGLE(index, divisor);
#endif
}

void ANGLEInstancedArrays::draw_arrays_instanced_angle(GLenum mode, GLint first, GLsizei count, GLsizei primcount)
{
#ifdef AK_OS_RINOS
    static_cast<WebGLRenderingContextImpl&>(*m_context)
        .draw_arrays_instanced_angle(mode, first, count, primcount);
#else
    m_context->context().make_current();
    glDrawArraysInstancedANGLE(mode, first, count, primcount);
#endif
}

void ANGLEInstancedArrays::draw_elements_instanced_angle(GLenum mode, GLsizei count, GLenum type, GLintptr offset, GLsizei primcount)
{
#ifdef AK_OS_RINOS
    static_cast<WebGLRenderingContextImpl&>(*m_context)
        .draw_elements_instanced_angle(mode, count, type, offset, primcount);
#else
    m_context->context().make_current();
    glDrawElementsInstancedANGLE(mode, count, type, reinterpret_cast<void*>(offset), primcount);
#endif
}

void ANGLEInstancedArrays::initialize(JS::Realm& realm)
{
    WEB_SET_PROTOTYPE_FOR_INTERFACE(ANGLEInstancedArrays);
    Base::initialize(realm);
}

void ANGLEInstancedArrays::visit_edges(Visitor& visitor)
{
    Base::visit_edges(visitor);
    visitor.visit(m_context);
}

}
