/*
 * Copyright (c) 2026, the RinOS developers
 *
 * SPDX-License-Identifier: BSD-2-Clause
 */

#pragma once

#include <LibJS/Forward.h>
#include <LibWeb/Bindings/AuthenticatorAssertionResponsePrototype.h>
#include <LibWeb/CredentialManagement/AuthenticatorResponse.h>

namespace Web::CredentialManagement {

// https://www.w3.org/TR/webauthn-3/#authenticatorassertionresponse
class AuthenticatorAssertionResponse final : public AuthenticatorResponse {
    WEB_PLATFORM_OBJECT(AuthenticatorAssertionResponse, AuthenticatorResponse);
    GC_DECLARE_ALLOCATOR(AuthenticatorAssertionResponse);

public:
    static GC::Ref<AuthenticatorAssertionResponse> create(JS::Realm&, GC::Ref<JS::ArrayBuffer> client_data_json,
        GC::Ref<JS::ArrayBuffer> authenticator_data, GC::Ref<JS::ArrayBuffer> signature,
        GC::Ptr<JS::ArrayBuffer> user_handle);

    virtual ~AuthenticatorAssertionResponse() override;

    GC::Ref<JS::ArrayBuffer> authenticator_data() const { return m_authenticator_data; }
    GC::Ref<JS::ArrayBuffer> signature() const { return m_signature; }
    GC::Ptr<JS::ArrayBuffer> user_handle() const { return m_user_handle; }

private:
    AuthenticatorAssertionResponse(JS::Realm&, GC::Ref<JS::ArrayBuffer> client_data_json,
        GC::Ref<JS::ArrayBuffer> authenticator_data, GC::Ref<JS::ArrayBuffer> signature,
        GC::Ptr<JS::ArrayBuffer> user_handle);

    virtual void initialize(JS::Realm&) override;
    virtual void visit_edges(Visitor&) override;

    GC::Ref<JS::ArrayBuffer> m_authenticator_data;
    GC::Ref<JS::ArrayBuffer> m_signature;
    GC::Ptr<JS::ArrayBuffer> m_user_handle;
};

}
