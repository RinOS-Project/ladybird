/*
 * Copyright (c) 2026, the RinOS developers
 *
 * SPDX-License-Identifier: BSD-2-Clause
 */

#include <LibJS/Runtime/ArrayBuffer.h>
#include <LibWeb/Bindings/Intrinsics.h>
#include <LibWeb/CredentialManagement/AuthenticatorAttestationResponse.h>

namespace Web::CredentialManagement {

GC_DEFINE_ALLOCATOR(AuthenticatorAttestationResponse);

GC::Ref<AuthenticatorAttestationResponse> AuthenticatorAttestationResponse::create(JS::Realm& realm,
    GC::Ref<JS::ArrayBuffer> client_data_json, GC::Ref<JS::ArrayBuffer> attestation_object,
    GC::Ref<JS::ArrayBuffer> authenticator_data, GC::Ptr<JS::ArrayBuffer> public_key,
    WebIDL::Long public_key_algorithm, Vector<Bindings::AuthenticatorTransport> transports)
{
    return realm.create<AuthenticatorAttestationResponse>(realm, client_data_json, attestation_object,
        authenticator_data, public_key, public_key_algorithm, move(transports));
}

AuthenticatorAttestationResponse::AuthenticatorAttestationResponse(JS::Realm& realm,
    GC::Ref<JS::ArrayBuffer> client_data_json, GC::Ref<JS::ArrayBuffer> attestation_object,
    GC::Ref<JS::ArrayBuffer> authenticator_data, GC::Ptr<JS::ArrayBuffer> public_key,
    WebIDL::Long public_key_algorithm, Vector<Bindings::AuthenticatorTransport> transports)
    : AuthenticatorResponse(realm, client_data_json)
    , m_attestation_object(attestation_object)
    , m_authenticator_data(authenticator_data)
    , m_public_key(public_key)
    , m_public_key_algorithm(public_key_algorithm)
    , m_transports(move(transports))
{
}

AuthenticatorAttestationResponse::~AuthenticatorAttestationResponse() = default;

void AuthenticatorAttestationResponse::initialize(JS::Realm& realm)
{
    WEB_SET_PROTOTYPE_FOR_INTERFACE(AuthenticatorAttestationResponse);
    Base::initialize(realm);
}

void AuthenticatorAttestationResponse::visit_edges(Visitor& visitor)
{
    Base::visit_edges(visitor);
    visitor.visit(m_attestation_object);
    visitor.visit(m_authenticator_data);
    visitor.visit(m_public_key);
}

}
