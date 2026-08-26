/*
 * Copyright (c) 2024, Jelle Raaijmakers <jelle@ladybird.org>
 * Copyright (c) 2024, Luke Wilde <luke@ladybird.org>
 *
 * SPDX-License-Identifier: BSD-2-Clause
 */

#pragma once

#include <AK/Array.h>
#include <LibWeb/WebGL/WebGLObject.h>

namespace Web::WebGL {

class WebGLFramebuffer final : public WebGLObject {
    WEB_PLATFORM_OBJECT(WebGLFramebuffer, WebGLObject);
    GC_DECLARE_ALLOCATOR(WebGLFramebuffer);

public:
    static GC::Ref<WebGLFramebuffer> create(JS::Realm& realm, GC::Ref<WebGLRenderingContextBase>, GLuint handle);

    virtual ~WebGLFramebuffer();

    // The native API reports attachment handles, while WebGL must return the
    // original script-visible object for OBJECT_NAME queries. Keep that
    // association at the WebGL object boundary instead of reconstructing a
    // wrapper from an untrusted native handle.
    void set_attachment(GLenum attachment, GC::Ptr<WebGLObject> object, GLint level);
    GC::Ptr<WebGLObject> attachment_object(GLenum attachment) const;
    GLint attachment_level(GLenum attachment) const;
    bool uses_separate_depth_stencil_attachments() const;

protected:
    explicit WebGLFramebuffer(JS::Realm&, GC::Ref<WebGLRenderingContextBase>, GLuint handle);

    virtual void initialize(JS::Realm&) override;
    virtual void visit_edges(Visitor&) override;

private:
    struct Attachment {
        GC::Ptr<WebGLObject> object;
        GLint level { 0 };
    };

    // WebGL 2 defines sixteen color attachment tokens. RinGL's WebGL 1 MRT
    // profile currently uses the first four, while the generic backend can
    // retain wrapper identity for all standard tokens.
    Array<Attachment, 16> m_color_attachments;
    Attachment m_depth_attachment;
    Attachment m_stencil_attachment;
    bool m_uses_separate_depth_stencil_attachments { false };
};

}
