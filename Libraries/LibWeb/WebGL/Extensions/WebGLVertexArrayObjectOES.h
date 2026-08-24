/*
 * Copyright (c) 2024, Andrew Kaster <andrew@ladybird.org>
 * Copyright (c) 2025, Luke Wilde <luke@ladybird.org>
 *
 * SPDX-License-Identifier: BSD-2-Clause
 */

#pragma once

#ifdef AK_OS_RINOS
#include <AK/Array.h>
#include <LibGC/Ptr.h>
#endif
#include <LibWeb/WebGL/Types.h>
#include <LibWeb/WebGL/WebGLObject.h>

#ifdef AK_OS_RINOS
namespace Web::WebGL {
class WebGLBuffer;
}
#endif

namespace Web::WebGL::Extensions {

class WebGLVertexArrayObjectOES : public WebGLObject {
    WEB_PLATFORM_OBJECT(WebGLVertexArrayObjectOES, WebGLObject);
    GC_DECLARE_ALLOCATOR(WebGLVertexArrayObjectOES);

public:
    static GC::Ref<WebGLVertexArrayObjectOES> create(JS::Realm& realm, WebGLRenderingContextBase&, GLuint handle);

    virtual ~WebGLVertexArrayObjectOES() override;

#ifdef AK_OS_RINOS
    Array<GC::Ptr<WebGLBuffer>, 16>& rin_gl_vertex_attrib_buffers() { return m_rin_gl_vertex_attrib_buffers; }
    Array<GC::Ptr<WebGLBuffer>, 16> const& rin_gl_vertex_attrib_buffers() const { return m_rin_gl_vertex_attrib_buffers; }
    GC::Ptr<WebGLBuffer> rin_gl_element_array_buffer_binding() const { return m_rin_gl_element_array_buffer_binding; }
    void set_rin_gl_element_array_buffer_binding(GC::Ptr<WebGLBuffer> buffer) { m_rin_gl_element_array_buffer_binding = buffer; }
#endif

protected:
    explicit WebGLVertexArrayObjectOES(JS::Realm&, WebGLRenderingContextBase&, GLuint handle);

    virtual void initialize(JS::Realm&) override;

#ifdef AK_OS_RINOS
    virtual void visit_edges(Visitor&) override;

private:
    Array<GC::Ptr<WebGLBuffer>, 16> m_rin_gl_vertex_attrib_buffers;
    GC::Ptr<WebGLBuffer> m_rin_gl_element_array_buffer_binding;
#endif
};

}
