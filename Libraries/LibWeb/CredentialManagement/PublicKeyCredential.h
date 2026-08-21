/*
 * Copyright (c) 2026, the RinOS developers
 *
 * SPDX-License-Identifier: BSD-2-Clause
 */

#pragma once

#include <AK/Optional.h>
#include <AK/Vector.h>
#include <LibJS/Forward.h>
#include <LibWeb/Bindings/AuthenticatorAttestationResponsePrototype.h>
#include <LibWeb/Bindings/PublicKeyCredentialPrototype.h>
#include <LibWeb/CredentialManagement/AuthenticatorResponse.h>
#include <LibWeb/CredentialManagement/Credential.h>
#include <LibWeb/WebIDL/Buffers.h>
#include <LibWeb/WebIDL/Promise.h>
#include <LibWeb/WebIDL/Types.h>

namespace Web::CredentialManagement {

struct AuthenticationExtensionsClientOutputs { };

// https://www.w3.org/TR/webauthn-3/#iface-pkcredential
class PublicKeyCredential final : public Credential {
    WEB_PLATFORM_OBJECT(PublicKeyCredential, Credential);
    GC_DECLARE_ALLOCATOR(PublicKeyCredential);

public:
    static GC::Ref<PublicKeyCredential> create(JS::Realm&, String id, GC::Ref<JS::ArrayBuffer> raw_id,
        GC::Ref<AuthenticatorResponse> response, Optional<String> authenticator_attachment);
    static GC::Ref<WebIDL::Promise> is_user_verifying_platform_authenticator_available(JS::VM&);
    static GC::Ref<WebIDL::Promise> is_conditional_mediation_available(JS::VM&);

    virtual ~PublicKeyCredential() override;

    String type() const override { return "public-key"_string; }
    GC::Ref<JS::ArrayBuffer> raw_id() const { return m_raw_id; }
    GC::Ref<AuthenticatorResponse> response() const { return m_response; }
    Optional<String> const& authenticator_attachment() const { return m_authenticator_attachment; }
    AuthenticationExtensionsClientOutputs get_client_extension_results() const { return {}; }

private:
    PublicKeyCredential(JS::Realm&, String id, GC::Ref<JS::ArrayBuffer> raw_id,
        GC::Ref<AuthenticatorResponse> response, Optional<String> authenticator_attachment);

    virtual void initialize(JS::Realm&) override;
    virtual void visit_edges(Visitor&) override;

    GC::Ref<JS::ArrayBuffer> m_raw_id;
    GC::Ref<AuthenticatorResponse> m_response;
    Optional<String> m_authenticator_attachment;
};

struct PublicKeyCredentialRpEntity {
    String name;
    Optional<String> id;
};

struct PublicKeyCredentialUserEntity {
    GC::Root<WebIDL::BufferSource> id;
    String name;
    String display_name;
};

struct PublicKeyCredentialParameters {
    String type;
    WebIDL::Long alg;
};

struct PublicKeyCredentialDescriptor {
    String type;
    GC::Root<WebIDL::BufferSource> id;
    Optional<Vector<Bindings::AuthenticatorTransport>> transports;
};

struct AuthenticatorSelectionCriteria {
    Optional<Bindings::AuthenticatorAttachment> authenticator_attachment;
    Bindings::ResidentKeyRequirement resident_key { Bindings::ResidentKeyRequirement::Discouraged };
    bool require_resident_key { false };
    Bindings::UserVerificationRequirement user_verification { Bindings::UserVerificationRequirement::Preferred };
};

struct PublicKeyCredentialCreationOptions {
    PublicKeyCredentialRpEntity rp;
    PublicKeyCredentialUserEntity user;
    GC::Root<WebIDL::BufferSource> challenge;
    Vector<PublicKeyCredentialParameters> pub_key_cred_params;
    Optional<WebIDL::UnsignedLong> timeout;
    Vector<PublicKeyCredentialDescriptor> exclude_credentials;
    Optional<AuthenticatorSelectionCriteria> authenticator_selection;
    Bindings::AttestationConveyancePreference attestation { Bindings::AttestationConveyancePreference::None };
    Vector<String> hints;
};

struct PublicKeyCredentialRequestOptions {
    GC::Root<WebIDL::BufferSource> challenge;
    Optional<WebIDL::UnsignedLong> timeout;
    Optional<String> rp_id;
    Vector<PublicKeyCredentialDescriptor> allow_credentials;
    Bindings::UserVerificationRequirement user_verification { Bindings::UserVerificationRequirement::Preferred };
    Vector<String> hints;
};

}
