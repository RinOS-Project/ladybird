/*
 * Copyright (c) 2026, RinOS contributors.
 *
 * SPDX-License-Identifier: BSD-2-Clause
 */

#include <LibJS/Runtime/Realm.h>
#include <LibWeb/Bindings/EXTColorBufferHalfFloatPrototype.h>
#include <LibWeb/Bindings/Intrinsics.h>
#include <LibWeb/WebGL/Extensions/EXTColorBufferHalfFloat.h>
#include <LibWeb/WebGL/WebGLRenderingContextBase.h>

namespace Web::WebGL::Extensions {

GC_DEFINE_ALLOCATOR(EXTColorBufferHalfFloat);

JS::ThrowCompletionOr<GC::Ref<JS::Object>> EXTColorBufferHalfFloat::create(JS::Realm& realm, GC::Ref<WebGLRenderingContextBase> context)
{
    return realm.create<EXTColorBufferHalfFloat>(realm, context);
}

EXTColorBufferHalfFloat::EXTColorBufferHalfFloat(JS::Realm& realm, GC::Ref<WebGLRenderingContextBase> context)
    : PlatformObject(realm)
    , m_context(context)
{
}

void EXTColorBufferHalfFloat::initialize(JS::Realm& realm)
{
    WEB_SET_PROTOTYPE_FOR_INTERFACE(EXTColorBufferHalfFloat);
    Base::initialize(realm);
}

void EXTColorBufferHalfFloat::visit_edges(Visitor& visitor)
{
    Base::visit_edges(visitor);
    visitor.visit(m_context);
}

}
