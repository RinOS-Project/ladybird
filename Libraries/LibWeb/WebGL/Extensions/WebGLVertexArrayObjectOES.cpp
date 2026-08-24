/*
 * Copyright (c) 2024, Andrew Kaster <andrew@ladybird.org>
 * Copyright (c) 2025, Luke Wilde <luke@ladybird.org>
 *
 * SPDX-License-Identifier: BSD-2-Clause
 */

#include <LibJS/Runtime/Realm.h>
#include <LibWeb/Bindings/Intrinsics.h>
#include <LibWeb/Bindings/WebGLVertexArrayObjectOESPrototype.h>
#include <LibWeb/WebGL/Extensions/WebGLVertexArrayObjectOES.h>
#ifdef AK_OS_RINOS
#include <LibWeb/WebGL/WebGLBuffer.h>
#endif

namespace Web::WebGL::Extensions {

GC_DEFINE_ALLOCATOR(WebGLVertexArrayObjectOES);

GC::Ref<WebGLVertexArrayObjectOES> WebGLVertexArrayObjectOES::create(JS::Realm& realm, WebGLRenderingContextBase& context, GLuint handle)
{
    return realm.create<WebGLVertexArrayObjectOES>(realm, context, handle);
}

WebGLVertexArrayObjectOES::WebGLVertexArrayObjectOES(JS::Realm& realm, WebGLRenderingContextBase& context, GLuint handle)
    : WebGLObject(realm, context, handle)
{
}

WebGLVertexArrayObjectOES::~WebGLVertexArrayObjectOES() = default;

void WebGLVertexArrayObjectOES::initialize(JS::Realm& realm)
{
    WEB_SET_PROTOTYPE_FOR_INTERFACE(WebGLVertexArrayObjectOES);
    Base::initialize(realm);
}

#ifdef AK_OS_RINOS
void WebGLVertexArrayObjectOES::visit_edges(Visitor& visitor)
{
    Base::visit_edges(visitor);
    for (auto& buffer : m_rin_gl_vertex_attrib_buffers)
        visitor.visit(buffer);
    visitor.visit(m_rin_gl_element_array_buffer_binding);
}
#endif

}
