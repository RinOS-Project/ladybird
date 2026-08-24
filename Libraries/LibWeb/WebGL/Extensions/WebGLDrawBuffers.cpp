/*
 * Copyright (c) 2025, Luke Wilde <luke@ladybird.org>
 *
 * SPDX-License-Identifier: BSD-2-Clause
 */

#include <LibJS/Runtime/Realm.h>
#include <LibWeb/Bindings/Intrinsics.h>
#include <LibWeb/Bindings/WebGLDrawBuffersPrototype.h>
#include <LibWeb/WebGL/Extensions/WebGLDrawBuffers.h>
#include <LibWeb/WebGL/OpenGLContext.h>
#include <LibWeb/WebGL/WebGLRenderingContextBase.h>

#ifdef AK_OS_RINOS
extern "C" {
#    include <ringl/ringl.h>
}
#else
#    define GL_GLEXT_PROTOTYPES 1
#    include <GLES2/gl2.h>
#    include <GLES2/gl2ext.h>
#endif

namespace Web::WebGL::Extensions {

GC_DEFINE_ALLOCATOR(WebGLDrawBuffers);

JS::ThrowCompletionOr<GC::Ref<JS::Object>> WebGLDrawBuffers::create(JS::Realm& realm, GC::Ref<WebGLRenderingContextBase> context)
{
    return realm.create<WebGLDrawBuffers>(realm, context);
}

WebGLDrawBuffers::WebGLDrawBuffers(JS::Realm& realm, GC::Ref<WebGLRenderingContextBase> context)
    : PlatformObject(realm)
    , m_context(context)
{
}

void WebGLDrawBuffers::draw_buffers_webgl(Vector<GLenum> buffers)
{
#ifdef AK_OS_RINOS
    m_context->context().rin_gl_draw_buffers(static_cast<u32>(buffers.size()),
        reinterpret_cast<u32 const*>(buffers.data()));
#else
    m_context->context().make_current();
    glDrawBuffersEXT(buffers.size(), buffers.data());
#endif
}

void WebGLDrawBuffers::initialize(JS::Realm& realm)
{
    WEB_SET_PROTOTYPE_FOR_INTERFACE(WebGLDrawBuffers);
    Base::initialize(realm);
}

void WebGLDrawBuffers::visit_edges(Visitor& visitor)
{
    Base::visit_edges(visitor);
    visitor.visit(m_context);
}

}
