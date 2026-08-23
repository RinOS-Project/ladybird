/*
 * Copyright (c) 2024, Jelle Raaijmakers <jelle@ladybird.org>
 * Copyright (c) 2024, Luke Wilde <luke@ladybird.org>
 *
 * SPDX-License-Identifier: BSD-2-Clause
 */

#pragma once

#include <LibWeb/WebGL/WebGLObject.h>

namespace Web::WebGL {

class WebGLFramebuffer final : public WebGLObject {
    WEB_PLATFORM_OBJECT(WebGLFramebuffer, WebGLObject);
    GC_DECLARE_ALLOCATOR(WebGLFramebuffer);

public:
    static GC::Ref<WebGLFramebuffer> create(JS::Realm& realm, GC::Ref<WebGLRenderingContextBase>, GLuint handle);

    virtual ~WebGLFramebuffer();

#ifdef AK_OS_RINOS
    // RinGL is the source of truth for attachment state. These retained WebGL
    // object edges let its direct embedding return the existing JS wrapper
    // after the backend confirms an attachment query, rather than fabricating
    // another wrapper from an opaque native handle.
    void set_rin_gl_attachment(GLenum attachment, GC::Ptr<WebGLObject> object, GLint level);
    GC::Ptr<WebGLObject> rin_gl_attachment_object(GLenum attachment) const;
    GLint rin_gl_attachment_level(GLenum attachment) const;
#endif

protected:
    explicit WebGLFramebuffer(JS::Realm&, GC::Ref<WebGLRenderingContextBase>, GLuint handle);

    virtual void initialize(JS::Realm&) override;
    virtual void visit_edges(Visitor&) override;

#ifdef AK_OS_RINOS
private:
    struct RinGLAttachment {
        GC::Ptr<WebGLObject> object;
        GLint level { 0 };
    };

    RinGLAttachment m_rin_gl_color_attachment;
    RinGLAttachment m_rin_gl_depth_attachment;
    RinGLAttachment m_rin_gl_stencil_attachment;
#endif
};

}
