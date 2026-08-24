/*
 * Copyright (c) 2026, RinOS contributors.
 *
 * SPDX-License-Identifier: BSD-2-Clause
 */

#include <LibJS/Runtime/Realm.h>
#include <LibWeb/Bindings/Intrinsics.h>
#include <LibWeb/Bindings/OESTextureHalfFloatPrototype.h>
#include <LibWeb/WebGL/Extensions/OESTextureHalfFloat.h>
#include <LibWeb/WebGL/WebGLRenderingContextBase.h>

namespace Web::WebGL::Extensions {

GC_DEFINE_ALLOCATOR(OESTextureHalfFloat);

JS::ThrowCompletionOr<GC::Ref<JS::Object>> OESTextureHalfFloat::create(JS::Realm& realm, GC::Ref<WebGLRenderingContextBase> context)
{
    return realm.create<OESTextureHalfFloat>(realm, context);
}

OESTextureHalfFloat::OESTextureHalfFloat(JS::Realm& realm, GC::Ref<WebGLRenderingContextBase> context)
    : PlatformObject(realm)
    , m_context(context)
{
}

void OESTextureHalfFloat::initialize(JS::Realm& realm)
{
    WEB_SET_PROTOTYPE_FOR_INTERFACE(OESTextureHalfFloat);
    Base::initialize(realm);
}

void OESTextureHalfFloat::visit_edges(Visitor& visitor)
{
    Base::visit_edges(visitor);
    visitor.visit(m_context);
}

}
