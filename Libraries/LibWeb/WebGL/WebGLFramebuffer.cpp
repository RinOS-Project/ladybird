/*
 * Copyright (c) 2024, Jelle Raaijmakers <jelle@ladybird.org>
 * Copyright (c) 2024, Luke Wilde <luke@ladybird.org>
 *
 * SPDX-License-Identifier: BSD-2-Clause
 */

#include <LibJS/Runtime/Realm.h>
#include <LibWeb/Bindings/Intrinsics.h>
#include <LibWeb/Bindings/WebGLFramebufferPrototype.h>
#include <LibWeb/WebGL/WebGLFramebuffer.h>

#ifdef AK_OS_RINOS
extern "C" {
#include <ringl/ringl.h>
}
#endif

namespace Web::WebGL {

GC_DEFINE_ALLOCATOR(WebGLFramebuffer);

GC::Ref<WebGLFramebuffer> WebGLFramebuffer::create(JS::Realm& realm, GC::Ref<WebGLRenderingContextBase> context, GLuint handle)
{
    return realm.create<WebGLFramebuffer>(realm, context, handle);
}

WebGLFramebuffer::WebGLFramebuffer(JS::Realm& realm, GC::Ref<WebGLRenderingContextBase> context, GLuint handle)
    : WebGLObject(realm, context, handle)
{
}

WebGLFramebuffer::~WebGLFramebuffer() = default;

void WebGLFramebuffer::initialize(JS::Realm& realm)
{
    WEB_SET_PROTOTYPE_FOR_INTERFACE(WebGLFramebuffer);
    Base::initialize(realm);
}

void WebGLFramebuffer::visit_edges(Visitor& visitor)
{
    Base::visit_edges(visitor);
#ifdef AK_OS_RINOS
    visitor.visit(m_rin_gl_color_attachment.object);
    visitor.visit(m_rin_gl_depth_attachment.object);
    visitor.visit(m_rin_gl_stencil_attachment.object);
#endif
}

#ifdef AK_OS_RINOS
void WebGLFramebuffer::set_rin_gl_attachment(GLenum attachment, GC::Ptr<WebGLObject> object, GLint level)
{
    RinGLAttachment value { object, level };

    switch (attachment) {
    case RINGL_COLOR_ATTACHMENT0:
        m_rin_gl_color_attachment = value;
        return;
    case RINGL_DEPTH_ATTACHMENT:
        m_rin_gl_depth_attachment = value;
        return;
    case RINGL_STENCIL_ATTACHMENT:
        m_rin_gl_stencil_attachment = value;
        return;
    case RINGL_DEPTH_STENCIL_ATTACHMENT:
        m_rin_gl_depth_attachment = value;
        m_rin_gl_stencil_attachment = value;
        return;
    default:
        return;
    }
}

GC::Ptr<WebGLObject> WebGLFramebuffer::rin_gl_attachment_object(GLenum attachment) const
{
    switch (attachment) {
    case RINGL_COLOR_ATTACHMENT0:
        return m_rin_gl_color_attachment.object;
    case RINGL_DEPTH_ATTACHMENT:
        return m_rin_gl_depth_attachment.object;
    case RINGL_STENCIL_ATTACHMENT:
        return m_rin_gl_stencil_attachment.object;
    case RINGL_DEPTH_STENCIL_ATTACHMENT:
        if (m_rin_gl_depth_attachment.object != m_rin_gl_stencil_attachment.object
            || !m_rin_gl_depth_attachment.object)
            return nullptr;
        return m_rin_gl_depth_attachment.object;
    default:
        return nullptr;
    }
}

GLint WebGLFramebuffer::rin_gl_attachment_level(GLenum attachment) const
{
    switch (attachment) {
    case RINGL_COLOR_ATTACHMENT0:
        return m_rin_gl_color_attachment.level;
    case RINGL_DEPTH_ATTACHMENT:
        return m_rin_gl_depth_attachment.level;
    case RINGL_STENCIL_ATTACHMENT:
        return m_rin_gl_stencil_attachment.level;
    case RINGL_DEPTH_STENCIL_ATTACHMENT:
        if (m_rin_gl_depth_attachment.object != m_rin_gl_stencil_attachment.object
            || !m_rin_gl_depth_attachment.object)
            return 0;
        return m_rin_gl_depth_attachment.level;
    default:
        return 0;
    }
}

#endif

}
