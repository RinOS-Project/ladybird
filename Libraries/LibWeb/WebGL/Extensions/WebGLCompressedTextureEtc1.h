/* SPDX-License-Identifier: BSD-2-Clause */

#pragma once

#include <LibWeb/Bindings/PlatformObject.h>
#include <LibWeb/Forward.h>

namespace Web::WebGL::Extensions {

class WebGLCompressedTextureEtc1 : public Bindings::PlatformObject {
    WEB_PLATFORM_OBJECT(WebGLCompressedTextureEtc1, Bindings::PlatformObject);
    GC_DECLARE_ALLOCATOR(WebGLCompressedTextureEtc1);

public:
    static JS::ThrowCompletionOr<GC::Ref<JS::Object>> create(JS::Realm&, GC::Ref<WebGLRenderingContextBase>);

protected:
    void initialize(JS::Realm&) override;
    void visit_edges(Visitor&) override;

private:
    WebGLCompressedTextureEtc1(JS::Realm&, GC::Ref<WebGLRenderingContextBase>);

    GC::Ref<WebGLRenderingContextBase> m_context;
};

}
