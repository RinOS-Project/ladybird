/*
 * Copyright (c) 2026, RinOS contributors.
 *
 * SPDX-License-Identifier: BSD-2-Clause
 */

#include <LibJS/Runtime/Realm.h>
#include <LibWeb/Bindings/Intrinsics.h>
#include <LibWeb/Bindings/OESTextureHalfFloatLinearPrototype.h>
#include <LibWeb/WebGL/Extensions/OESTextureHalfFloatLinear.h>
#include <LibWeb/WebGL/WebGLRenderingContextBase.h>

namespace Web::WebGL::Extensions {

GC_DEFINE_ALLOCATOR(OESTextureHalfFloatLinear);

JS::ThrowCompletionOr<GC::Ref<JS::Object>> OESTextureHalfFloatLinear::create(JS::Realm& realm, GC::Ref<WebGLRenderingContextBase> context)
{
    return realm.create<OESTextureHalfFloatLinear>(realm, context);
}

OESTextureHalfFloatLinear::OESTextureHalfFloatLinear(JS::Realm& realm, GC::Ref<WebGLRenderingContextBase> context)
    : PlatformObject(realm)
    , m_context(context)
{
}

void OESTextureHalfFloatLinear::initialize(JS::Realm& realm)
{
    WEB_SET_PROTOTYPE_FOR_INTERFACE(OESTextureHalfFloatLinear);
    Base::initialize(realm);
}

void OESTextureHalfFloatLinear::visit_edges(Visitor& visitor)
{
    Base::visit_edges(visitor);
    visitor.visit(m_context);
}

}
