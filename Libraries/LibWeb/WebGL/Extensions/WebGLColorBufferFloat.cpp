/*
 * Copyright (c) 2026, RinOS contributors.
 *
 * SPDX-License-Identifier: BSD-2-Clause
 */

#include <LibJS/Runtime/Realm.h>
#include <LibWeb/Bindings/Intrinsics.h>
#include <LibWeb/Bindings/WebGLColorBufferFloatPrototype.h>
#include <LibWeb/WebGL/Extensions/WebGLColorBufferFloat.h>
#include <LibWeb/WebGL/WebGLRenderingContextBase.h>

namespace Web::WebGL::Extensions {

GC_DEFINE_ALLOCATOR(WebGLColorBufferFloat);

JS::ThrowCompletionOr<GC::Ref<JS::Object>> WebGLColorBufferFloat::create(JS::Realm& realm, GC::Ref<WebGLRenderingContextBase> context)
{
    return realm.create<WebGLColorBufferFloat>(realm, context);
}

WebGLColorBufferFloat::WebGLColorBufferFloat(JS::Realm& realm, GC::Ref<WebGLRenderingContextBase> context)
    : PlatformObject(realm)
    , m_context(context)
{
}

void WebGLColorBufferFloat::initialize(JS::Realm& realm)
{
    WEB_SET_PROTOTYPE_FOR_INTERFACE(WebGLColorBufferFloat);
    Base::initialize(realm);
}

void WebGLColorBufferFloat::visit_edges(Visitor& visitor)
{
    Base::visit_edges(visitor);
    visitor.visit(m_context);
}

}
