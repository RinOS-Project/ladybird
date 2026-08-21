/*
 * Copyright (c) 2026, the RinOS developers
 *
 * SPDX-License-Identifier: BSD-2-Clause
 */

#pragma once

#include <AK/Vector.h>
#include <LibJS/Forward.h>
#include <LibWeb/Bindings/AuthenticatorAttestationResponsePrototype.h>
#include <LibWeb/CredentialManagement/AuthenticatorResponse.h>
#include <LibWeb/WebIDL/Types.h>

namespace Web::CredentialManagement {

// https://www.w3.org/TR/webauthn-3/#authenticatorattestationresponse
class AuthenticatorAttestationResponse final : public AuthenticatorResponse {
    WEB_PLATFORM_OBJECT(AuthenticatorAttestationResponse, AuthenticatorResponse);
    GC_DECLARE_ALLOCATOR(AuthenticatorAttestationResponse);

public:
    static GC::Ref<AuthenticatorAttestationResponse> create(JS::Realm&, GC::Ref<JS::ArrayBuffer> client_data_json,
        GC::Ref<JS::ArrayBuffer> attestation_object, GC::Ref<JS::ArrayBuffer> authenticator_data,
        GC::Ptr<JS::ArrayBuffer> public_key, WebIDL::Long public_key_algorithm,
        Vector<Bindings::AuthenticatorTransport> transports);

    virtual ~AuthenticatorAttestationResponse() override;

    GC::Ref<JS::ArrayBuffer> attestation_object() const { return m_attestation_object; }
    Vector<Bindings::AuthenticatorTransport> get_transports() const { return m_transports; }
    GC::Ref<JS::ArrayBuffer> get_authenticator_data() const { return m_authenticator_data; }
    GC::Ptr<JS::ArrayBuffer> get_public_key() const { return m_public_key; }
    WebIDL::Long get_public_key_algorithm() const { return m_public_key_algorithm; }

private:
    AuthenticatorAttestationResponse(JS::Realm&, GC::Ref<JS::ArrayBuffer> client_data_json,
        GC::Ref<JS::ArrayBuffer> attestation_object, GC::Ref<JS::ArrayBuffer> authenticator_data,
        GC::Ptr<JS::ArrayBuffer> public_key, WebIDL::Long public_key_algorithm,
        Vector<Bindings::AuthenticatorTransport> transports);

    virtual void initialize(JS::Realm&) override;
    virtual void visit_edges(Visitor&) override;

    GC::Ref<JS::ArrayBuffer> m_attestation_object;
    GC::Ref<JS::ArrayBuffer> m_authenticator_data;
    GC::Ptr<JS::ArrayBuffer> m_public_key;
    WebIDL::Long m_public_key_algorithm;
    Vector<Bindings::AuthenticatorTransport> m_transports;
};

}
