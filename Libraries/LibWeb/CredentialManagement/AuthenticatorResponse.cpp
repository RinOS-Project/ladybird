/*
 * Copyright (c) 2026, the RinOS developers
 *
 * SPDX-License-Identifier: BSD-2-Clause
 */

#include <LibJS/Runtime/ArrayBuffer.h>
#include <LibWeb/Bindings/Intrinsics.h>
#include <LibWeb/CredentialManagement/AuthenticatorResponse.h>

namespace Web::CredentialManagement {

AuthenticatorResponse::AuthenticatorResponse(JS::Realm& realm, GC::Ref<JS::ArrayBuffer> client_data_json)
    : PlatformObject(realm)
    , m_client_data_json(client_data_json)
{
}

AuthenticatorResponse::~AuthenticatorResponse() = default;

void AuthenticatorResponse::initialize(JS::Realm& realm)
{
    WEB_SET_PROTOTYPE_FOR_INTERFACE(AuthenticatorResponse);
    Base::initialize(realm);
}

void AuthenticatorResponse::visit_edges(Visitor& visitor)
{
    Base::visit_edges(visitor);
    visitor.visit(m_client_data_json);
}

}
