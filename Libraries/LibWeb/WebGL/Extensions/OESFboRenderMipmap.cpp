/*
 * Copyright (c) 2026, RinOS contributors.
 *
 * SPDX-License-Identifier: BSD-2-Clause
 */

#include <LibJS/Runtime/Realm.h>
#include <LibWeb/Bindings/Intrinsics.h>
#include <LibWeb/Bindings/OESFboRenderMipmapPrototype.h>
#include <LibWeb/WebGL/Extensions/OESFboRenderMipmap.h>
#include <LibWeb/WebGL/WebGLRenderingContextBase.h>

namespace Web::WebGL::Extensions {

GC_DEFINE_ALLOCATOR(OESFboRenderMipmap);

JS::ThrowCompletionOr<GC::Ref<JS::Object>> OESFboRenderMipmap::create(JS::Realm& realm, GC::Ref<WebGLRenderingContextBase> context)
{
    return realm.create<OESFboRenderMipmap>(realm, context);
}

OESFboRenderMipmap::OESFboRenderMipmap(JS::Realm& realm, GC::Ref<WebGLRenderingContextBase> context)
    : PlatformObject(realm)
    , m_context(context)
{
}

void OESFboRenderMipmap::initialize(JS::Realm& realm)
{
    WEB_SET_PROTOTYPE_FOR_INTERFACE(OESFboRenderMipmap);
    Base::initialize(realm);
}

void OESFboRenderMipmap::visit_edges(Visitor& visitor)
{
    Base::visit_edges(visitor);
    visitor.visit(m_context);
}

}
