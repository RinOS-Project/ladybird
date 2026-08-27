/*
 * Copyright (c) 2026, RinOS contributors
 *
 * SPDX-License-Identifier: BSD-2-Clause
 */

#pragma once

#include <LibWeb/Bindings/PlatformObject.h>
#include <LibWeb/Forward.h>

namespace Web::WebGL::Extensions {

// The EXT_shader_texture_lod object is intentionally empty. Its lifetime is
// still the WebGL opt-in that permits RinGL to compile texture2DLodEXT.
class EXTShaderTextureLod : public Bindings::PlatformObject {
    WEB_PLATFORM_OBJECT(EXTShaderTextureLod, Bindings::PlatformObject);
    GC_DECLARE_ALLOCATOR(EXTShaderTextureLod);

public:
    static JS::ThrowCompletionOr<GC::Ref<JS::Object>> create(JS::Realm&, GC::Ref<WebGLRenderingContextBase>);

protected:
    void initialize(JS::Realm&) override;
    void visit_edges(Visitor&) override;

private:
    EXTShaderTextureLod(JS::Realm&, GC::Ref<WebGLRenderingContextBase>);

    GC::Ref<WebGLRenderingContextBase> m_context;
};

}
