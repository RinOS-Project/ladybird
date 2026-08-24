/*
 * Copyright (c) 2026, RinOS contributors.
 *
 * SPDX-License-Identifier: BSD-2-Clause
 */

#include <LibJS/Runtime/Realm.h>
#include <LibWeb/Bindings/Intrinsics.h>
#include <LibWeb/Bindings/OESTextureFloatPrototype.h>
#include <LibWeb/WebGL/Extensions/OESTextureFloat.h>
#include <LibWeb/WebGL/WebGLRenderingContextBase.h>

namespace Web::WebGL::Extensions {

GC_DEFINE_ALLOCATOR(OESTextureFloat);

JS::ThrowCompletionOr<GC::Ref<JS::Object>> OESTextureFloat::create(JS::Realm& realm, GC::Ref<WebGLRenderingContextBase> context)
{
    return realm.create<OESTextureFloat>(realm, context);
}

OESTextureFloat::OESTextureFloat(JS::Realm& realm, GC::Ref<WebGLRenderingContextBase> context)
    : PlatformObject(realm)
    , m_context(context)
{
}

void OESTextureFloat::initialize(JS::Realm& realm)
{
    WEB_SET_PROTOTYPE_FOR_INTERFACE(OESTextureFloat);
    Base::initialize(realm);
}

void OESTextureFloat::visit_edges(Visitor& visitor)
{
    Base::visit_edges(visitor);
    visitor.visit(m_context);
}

}
