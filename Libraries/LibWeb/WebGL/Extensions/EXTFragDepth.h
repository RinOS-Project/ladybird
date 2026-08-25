/*
 * Copyright (c) 2026, RinOS contributors
 *
 * SPDX-License-Identifier: BSD-2-Clause
 */

#pragma once

#include <LibWeb/Bindings/PlatformObject.h>
#include <LibWeb/Forward.h>

namespace Web::WebGL::Extensions {

// EXT_frag_depth has an intentionally empty IDL surface. The object still
// carries the WebGL capability lifetime which gates gl_FragDepthEXT in RinGL.
class EXTFragDepth : public Bindings::PlatformObject {
    WEB_PLATFORM_OBJECT(EXTFragDepth, Bindings::PlatformObject);
    GC_DECLARE_ALLOCATOR(EXTFragDepth);

public:
    static JS::ThrowCompletionOr<GC::Ref<JS::Object>> create(JS::Realm&, GC::Ref<WebGLRenderingContextBase>);

protected:
    void initialize(JS::Realm&) override;
    void visit_edges(Visitor&) override;

private:
    EXTFragDepth(JS::Realm&, GC::Ref<WebGLRenderingContextBase>);

    GC::Ref<WebGLRenderingContextBase> m_context;
};

}
