/*
 * Copyright (c) 2024, Jelle Raaijmakers <jelle@ladybird.org>
 * Copyright (c) 2024, Aliaksandr Kalenik <kalenik.aliaksandr@gmail.com>
 * Copyright (c) 2024, Luke Wilde <luke@ladybird.org>
 *
 * SPDX-License-Identifier: BSD-2-Clause
 */

#include <LibWeb/Bindings/Intrinsics.h>
#include <LibWeb/Bindings/WebGLObjectPrototype.h>
#include <LibWeb/WebGL/WebGLObject.h>

#ifdef AK_OS_RINOS
extern "C" {
#include <ringl/ringl.h>
}
#else
#include <GLES2/gl2.h>
#endif

namespace Web::WebGL {

WebGLObject::WebGLObject(JS::Realm& realm, GC::Ref<WebGLRenderingContextBase> context, GLuint handle)
    : Bindings::PlatformObject(realm)
    , m_context(context)
    , m_handle(handle)
{
}

WebGLObject::~WebGLObject() = default;

void WebGLObject::initialize(JS::Realm& realm)
{
    WEB_SET_PROTOTYPE_FOR_INTERFACE(WebGLObject);
    Base::initialize(realm);
}

void WebGLObject::visit_edges(Visitor& visitor)
{
    Base::visit_edges(visitor);
    visitor.visit(m_context);
}

ErrorOr<GLuint> WebGLObject::handle(WebGLRenderingContextBase const* context) const
{
    if (context == m_context)
        return m_handle;
#ifdef AK_OS_RINOS
    return Error::from_errno(RINGL_INVALID_OPERATION);
#else
    return Error::from_errno(GL_INVALID_OPERATION);
#endif
}

}
