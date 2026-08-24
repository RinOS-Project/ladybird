/*
 * Copyright (c) 2025, Luke Wilde <luke@ladybird.org>
 *
 * SPDX-License-Identifier: BSD-2-Clause
 */

#include <LibJS/Runtime/Realm.h>
#include <LibWeb/Bindings/Intrinsics.h>
#include <LibWeb/Bindings/OESVertexArrayObjectPrototype.h>
#include <LibWeb/WebGL/Extensions/OESVertexArrayObject.h>
#include <LibWeb/WebGL/Extensions/WebGLVertexArrayObjectOES.h>
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

GC_DEFINE_ALLOCATOR(OESVertexArrayObject);

JS::ThrowCompletionOr<GC::Ref<JS::Object>> OESVertexArrayObject::create(JS::Realm& realm, GC::Ref<WebGLRenderingContextBase> context)
{
    return realm.create<OESVertexArrayObject>(realm, context);
}

OESVertexArrayObject::OESVertexArrayObject(JS::Realm& realm, GC::Ref<WebGLRenderingContextBase> context)
    : PlatformObject(realm)
    , m_context(context)
{
}

GC::Ref<WebGLVertexArrayObjectOES> OESVertexArrayObject::create_vertex_array_oes()
{
#ifdef AK_OS_RINOS
    auto& context = static_cast<WebGLRenderingContextImpl&>(*m_context);
    return WebGLVertexArrayObjectOES::create(realm(), m_context,
        context.create_vertex_array_oes());
#else
    m_context->context().make_current();

    GLuint handle = 0;
    glGenVertexArraysOES(1, &handle);
    return WebGLVertexArrayObjectOES::create(realm(), m_context, handle);
#endif
}

void OESVertexArrayObject::delete_vertex_array_oes(GC::Root<WebGLVertexArrayObjectOES> array_object)
{
#ifdef AK_OS_RINOS
    static_cast<WebGLRenderingContextImpl&>(*m_context)
        .delete_vertex_array_oes(array_object);
#else
    m_context->context().make_current();

    GLuint vertex_array_handle = 0;
    if (array_object) {
        auto handle_or_error = array_object->handle(m_context.ptr());
        if (handle_or_error.is_error()) {
            // FIXME: m_context->set_error(GL_INVALID_OPERATION);
            return;
        }
        vertex_array_handle = handle_or_error.release_value();
    }

    glDeleteVertexArraysOES(1, &vertex_array_handle);
#endif
}

bool OESVertexArrayObject::is_vertex_array_oes(GC::Root<WebGLVertexArrayObjectOES> array_object)
{
#ifdef AK_OS_RINOS
    return static_cast<WebGLRenderingContextImpl&>(*m_context)
        .is_vertex_array_oes(array_object);
#else
    m_context->context().make_current();

    GLuint vertex_array_handle = 0;
    if (array_object) {
        auto handle_or_error = array_object->handle(m_context.ptr());
        if (handle_or_error.is_error()) {
            return false;
        }
        vertex_array_handle = handle_or_error.release_value();
    }

    return glIsVertexArrayOES(vertex_array_handle) == GL_TRUE;
#endif
}

void OESVertexArrayObject::bind_vertex_array_oes(GC::Root<WebGLVertexArrayObjectOES> array_object)
{
#ifdef AK_OS_RINOS
    static_cast<WebGLRenderingContextImpl&>(*m_context)
        .bind_vertex_array_oes(array_object);
#else
    m_context->context().make_current();

    GLuint vertex_array_handle = 0;
    if (array_object) {
        auto handle_or_error = array_object->handle(m_context.ptr());
        if (handle_or_error.is_error()) {
            // FIXME: m_context->set_error(GL_INVALID_OPERATION);
            return;
        }
        vertex_array_handle = handle_or_error.release_value();
    }

    glBindVertexArrayOES(vertex_array_handle);
#endif
}

void OESVertexArrayObject::initialize(JS::Realm& realm)
{
    WEB_SET_PROTOTYPE_FOR_INTERFACE(OESVertexArrayObject);
    Base::initialize(realm);
}

void OESVertexArrayObject::visit_edges(Visitor& visitor)
{
    Base::visit_edges(visitor);
    visitor.visit(m_context);
}

}
