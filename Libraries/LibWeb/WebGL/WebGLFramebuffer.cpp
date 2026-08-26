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

namespace Web::WebGL {

static constexpr GLenum color_attachment0 = 0x8ce0;
static constexpr size_t color_attachment_count = 16;
static constexpr GLenum depth_attachment = 0x8d00;
static constexpr GLenum stencil_attachment = 0x8d20;
static constexpr GLenum depth_stencil_attachment = 0x821a;

static bool is_color_attachment(GLenum attachment)
{
    return attachment >= color_attachment0
        && attachment - color_attachment0 < color_attachment_count;
}

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
    for (auto& attachment : m_color_attachments)
        visitor.visit(attachment.object);
    visitor.visit(m_depth_attachment.object);
    visitor.visit(m_stencil_attachment.object);
}

void WebGLFramebuffer::set_attachment(GLenum attachment, GC::Ptr<WebGLObject> object, GLint level)
{
    Attachment value { object, level };

    if (is_color_attachment(attachment)) {
        m_color_attachments[attachment - color_attachment0] = value;
        return;
    }

    switch (attachment) {
    case depth_attachment:
        m_depth_attachment = value;
        m_uses_separate_depth_stencil_attachments = true;
        return;
    case stencil_attachment:
        m_stencil_attachment = value;
        m_uses_separate_depth_stencil_attachments = true;
        return;
    case depth_stencil_attachment:
        m_depth_attachment = value;
        m_stencil_attachment = value;
        m_uses_separate_depth_stencil_attachments = false;
        return;
    default:
        return;
    }
}

GC::Ptr<WebGLObject> WebGLFramebuffer::attachment_object(GLenum attachment) const
{
    if (is_color_attachment(attachment))
        return m_color_attachments[attachment - color_attachment0].object;

    switch (attachment) {
    case depth_attachment:
        return m_depth_attachment.object;
    case stencil_attachment:
        return m_stencil_attachment.object;
    case depth_stencil_attachment:
        if (m_depth_attachment.object != m_stencil_attachment.object
            || !m_depth_attachment.object)
            return nullptr;
        return m_depth_attachment.object;
    default:
        return nullptr;
    }
}

GLint WebGLFramebuffer::attachment_level(GLenum attachment) const
{
    if (is_color_attachment(attachment))
        return m_color_attachments[attachment - color_attachment0].level;

    switch (attachment) {
    case depth_attachment:
        return m_depth_attachment.level;
    case stencil_attachment:
        return m_stencil_attachment.level;
    case depth_stencil_attachment:
        if (m_depth_attachment.object != m_stencil_attachment.object
            || !m_depth_attachment.object)
            return 0;
        return m_depth_attachment.level;
    default:
        return 0;
    }
}

bool WebGLFramebuffer::uses_separate_depth_stencil_attachments() const
{
    return m_uses_separate_depth_stencil_attachments
        && m_depth_attachment.object
        && m_stencil_attachment.object;
}

}
