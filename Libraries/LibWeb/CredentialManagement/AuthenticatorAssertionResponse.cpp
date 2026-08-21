/*
 * Copyright (c) 2026, the RinOS developers
 *
 * SPDX-License-Identifier: BSD-2-Clause
 */

#include <LibJS/Runtime/ArrayBuffer.h>
#include <LibWeb/Bindings/Intrinsics.h>
#include <LibWeb/CredentialManagement/AuthenticatorAssertionResponse.h>

namespace Web::CredentialManagement {

GC_DEFINE_ALLOCATOR(AuthenticatorAssertionResponse);

GC::Ref<AuthenticatorAssertionResponse> AuthenticatorAssertionResponse::create(JS::Realm& realm,
    GC::Ref<JS::ArrayBuffer> client_data_json, GC::Ref<JS::ArrayBuffer> authenticator_data,
    GC::Ref<JS::ArrayBuffer> signature, GC::Ptr<JS::ArrayBuffer> user_handle)
{
    return realm.create<AuthenticatorAssertionResponse>(realm, client_data_json, authenticator_data, signature, user_handle);
}

AuthenticatorAssertionResponse::AuthenticatorAssertionResponse(JS::Realm& realm,
    GC::Ref<JS::ArrayBuffer> client_data_json, GC::Ref<JS::ArrayBuffer> authenticator_data,
    GC::Ref<JS::ArrayBuffer> signature, GC::Ptr<JS::ArrayBuffer> user_handle)
    : AuthenticatorResponse(realm, client_data_json)
    , m_authenticator_data(authenticator_data)
    , m_signature(signature)
    , m_user_handle(user_handle)
{
}

AuthenticatorAssertionResponse::~AuthenticatorAssertionResponse() = default;

void AuthenticatorAssertionResponse::initialize(JS::Realm& realm)
{
    WEB_SET_PROTOTYPE_FOR_INTERFACE(AuthenticatorAssertionResponse);
    Base::initialize(realm);
}

void AuthenticatorAssertionResponse::visit_edges(Visitor& visitor)
{
    Base::visit_edges(visitor);
    visitor.visit(m_authenticator_data);
    visitor.visit(m_signature);
    visitor.visit(m_user_handle);
}

}
