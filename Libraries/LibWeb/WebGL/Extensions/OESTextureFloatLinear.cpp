/*
 * Copyright (c) 2026, RinOS contributors.
 *
 * SPDX-License-Identifier: BSD-2-Clause
 */

#include <LibJS/Runtime/Realm.h>
#include <LibWeb/Bindings/Intrinsics.h>
#include <LibWeb/Bindings/OESTextureFloatLinearPrototype.h>
#include <LibWeb/WebGL/Extensions/OESTextureFloatLinear.h>
#include <LibWeb/WebGL/WebGLRenderingContextBase.h>

namespace Web::WebGL::Extensions {

GC_DEFINE_ALLOCATOR(OESTextureFloatLinear);

JS::ThrowCompletionOr<GC::Ref<JS::Object>> OESTextureFloatLinear::create(JS::Realm& realm, GC::Ref<WebGLRenderingContextBase> context)
{
    return realm.create<OESTextureFloatLinear>(realm, context);
}

OESTextureFloatLinear::OESTextureFloatLinear(JS::Realm& realm, GC::Ref<WebGLRenderingContextBase> context)
    : PlatformObject(realm)
    , m_context(context)
{
}

void OESTextureFloatLinear::initialize(JS::Realm& realm)
{
    WEB_SET_PROTOTYPE_FOR_INTERFACE(OESTextureFloatLinear);
    Base::initialize(realm);
}

void OESTextureFloatLinear::visit_edges(Visitor& visitor)
{
    Base::visit_edges(visitor);
    visitor.visit(m_context);
}

}
