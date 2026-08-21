/*
 * Copyright (c) 2026, the RinOS developers
 *
 * SPDX-License-Identifier: BSD-2-Clause
 */

#pragma once

#include <LibJS/Forward.h>
#include <LibWeb/Bindings/AuthenticatorResponsePrototype.h>
#include <LibWeb/Bindings/PlatformObject.h>

namespace Web::CredentialManagement {

// https://www.w3.org/TR/webauthn-3/#authenticatorresponse
class AuthenticatorResponse : public Bindings::PlatformObject {
    WEB_PLATFORM_OBJECT(AuthenticatorResponse, Bindings::PlatformObject);
    GC_DECLARE_ALLOCATOR(AuthenticatorResponse);

public:
    virtual ~AuthenticatorResponse() override;

    GC::Ref<JS::ArrayBuffer> client_data_json() const { return m_client_data_json; }

protected:
    AuthenticatorResponse(JS::Realm&, GC::Ref<JS::ArrayBuffer> client_data_json);
    virtual void initialize(JS::Realm&) override;
    virtual void visit_edges(Visitor&) override;

private:
    GC::Ref<JS::ArrayBuffer> m_client_data_json;
};

}
