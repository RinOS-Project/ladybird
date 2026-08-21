/*
 * Copyright (c) 2026, the RinOS developers
 *
 * SPDX-License-Identifier: BSD-2-Clause
 */

#include <LibJS/Runtime/ArrayBuffer.h>
#include <LibWeb/Bindings/Intrinsics.h>
#include <LibWeb/CredentialManagement/PublicKeyCredential.h>

namespace Web::CredentialManagement {

GC_DEFINE_ALLOCATOR(PublicKeyCredential);

GC::Ref<PublicKeyCredential> PublicKeyCredential::create(JS::Realm& realm, String id,
    GC::Ref<JS::ArrayBuffer> raw_id, GC::Ref<AuthenticatorResponse> response,
    Optional<String> authenticator_attachment)
{
    return realm.create<PublicKeyCredential>(realm, move(id), raw_id, response, move(authenticator_attachment));
}

GC::Ref<WebIDL::Promise> PublicKeyCredential::is_user_verifying_platform_authenticator_available(JS::VM& vm)
{
    // The RinOS browser process must not claim platform UV until the signed
    // browser-to-kernel Rin Pass channel has completed capability discovery.
    return WebIDL::create_resolved_promise(*vm.current_realm(), JS::Value(false));
}

GC::Ref<WebIDL::Promise> PublicKeyCredential::is_conditional_mediation_available(JS::VM& vm)
{
    // Conditional mediation is intentionally outside the initial Rin Pass
    // transport set. Expose the standards API while accurately reporting that
    // the capability is unavailable.
    return WebIDL::create_resolved_promise(*vm.current_realm(), JS::Value(false));
}

PublicKeyCredential::PublicKeyCredential(JS::Realm& realm, String id, GC::Ref<JS::ArrayBuffer> raw_id,
    GC::Ref<AuthenticatorResponse> response, Optional<String> authenticator_attachment)
    : Credential(realm, move(id))
    , m_raw_id(raw_id)
    , m_response(response)
    , m_authenticator_attachment(move(authenticator_attachment))
{
}

PublicKeyCredential::~PublicKeyCredential() = default;

void PublicKeyCredential::initialize(JS::Realm& realm)
{
    WEB_SET_PROTOTYPE_FOR_INTERFACE(PublicKeyCredential);
    Base::initialize(realm);
}

void PublicKeyCredential::visit_edges(Visitor& visitor)
{
    Base::visit_edges(visitor);
    visitor.visit(m_raw_id);
    visitor.visit(m_response);
}

}
