/* SPDX-License-Identifier: BSD-2-Clause */

#include <LibJS/Runtime/Realm.h>
#include <LibWeb/Bindings/Intrinsics.h>
#include <LibWeb/Bindings/WebGLCompressedTextureEtc1Prototype.h>
#include <LibWeb/WebGL/Extensions/WebGLCompressedTextureEtc1.h>
#include <LibWeb/WebGL/WebGLRenderingContextBase.h>

namespace Web::WebGL::Extensions {

GC_DEFINE_ALLOCATOR(WebGLCompressedTextureEtc1);

static constexpr WebIDL::UnsignedLong compressed_rgb_etc1_webgl = 0x8d64;

JS::ThrowCompletionOr<GC::Ref<JS::Object>> WebGLCompressedTextureEtc1::create(JS::Realm& realm, GC::Ref<WebGLRenderingContextBase> context)
{
    return realm.create<WebGLCompressedTextureEtc1>(realm, context);
}

WebGLCompressedTextureEtc1::WebGLCompressedTextureEtc1(JS::Realm& realm, GC::Ref<WebGLRenderingContextBase> context)
    : PlatformObject(realm)
    , m_context(context)
{
    // RinGL validates and expands ETC1 into ordinary RGB storage before the
    // adapter reaches RinGPU. Publish precisely that native format here.
    m_context->enable_compressed_texture_format(compressed_rgb_etc1_webgl);
}

void WebGLCompressedTextureEtc1::initialize(JS::Realm& realm)
{
    WEB_SET_PROTOTYPE_FOR_INTERFACE(WebGLCompressedTextureEtc1);
    Base::initialize(realm);
}

void WebGLCompressedTextureEtc1::visit_edges(Visitor& visitor)
{
    Base::visit_edges(visitor);
    visitor.visit(m_context);
}

}
