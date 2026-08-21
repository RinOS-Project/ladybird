/*
 * Copyright (c) 2025, Altomani Gianluca <altomanigianluca@gmail.com>
 *
 * SPDX-License-Identifier: BSD-2-Clause
 */

#include <LibWeb/CredentialManagement/CredentialsContainer.h>
#include <LibWeb/WebIDL/DOMException.h>

namespace Web::CredentialManagement {

GC_DEFINE_ALLOCATOR(CredentialsContainer);

GC::Ref<CredentialsContainer> CredentialsContainer::create(JS::Realm& realm)
{
    return realm.create<CredentialsContainer>(realm);
}

CredentialsContainer::~CredentialsContainer() { }

// https://www.w3.org/TR/credential-management-1/#dom-credentialscontainer-get
GC::Ref<WebIDL::Promise> CredentialsContainer::get(CredentialRequestOptions const& options)
{
    auto* realm = vm().current_realm();
    if (options.public_key.has_value()) {
        return WebIDL::create_rejected_promise_from_exception(*realm,
            WebIDL::NotSupportedError::create(*realm, "Rin Pass WebAuthn broker is unavailable"_utf16));
    }
    return WebIDL::create_rejected_promise_from_exception(*realm, vm().throw_completion<JS::InternalError>(JS::ErrorType::NotImplemented, "get"sv));
}

// https://www.w3.org/TR/credential-management-1/#dom-credentialscontainer-store
GC::Ref<WebIDL::Promise> CredentialsContainer::store(Credential const&)
{
    auto* realm = vm().current_realm();
    return WebIDL::create_rejected_promise_from_exception(*realm, vm().throw_completion<JS::InternalError>(JS::ErrorType::NotImplemented, "store"sv));
}

// https://www.w3.org/TR/credential-management-1/#dom-credentialscontainer-create
GC::Ref<WebIDL::Promise> CredentialsContainer::create(CredentialCreationOptions const& options)
{
    auto* realm = vm().current_realm();
    if (options.public_key.has_value()) {
        return WebIDL::create_rejected_promise_from_exception(*realm,
            WebIDL::NotSupportedError::create(*realm, "Rin Pass WebAuthn broker is unavailable"_utf16));
    }
    return WebIDL::create_rejected_promise_from_exception(*realm, vm().throw_completion<JS::InternalError>(JS::ErrorType::NotImplemented, "create"sv));
}

// https://www.w3.org/TR/credential-management-1/#dom-credentialscontainer-preventsilentaccess
GC::Ref<WebIDL::Promise> CredentialsContainer::prevent_silent_access()
{
    auto* realm = vm().current_realm();
    return WebIDL::create_rejected_promise_from_exception(*realm, vm().throw_completion<JS::InternalError>(JS::ErrorType::NotImplemented, "prevent silent access"sv));
}

CredentialsContainer::CredentialsContainer(JS::Realm& realm)
    : PlatformObject(realm)
{
}

void CredentialsContainer::initialize(JS::Realm& realm)
{
    WEB_SET_PROTOTYPE_FOR_INTERFACE(CredentialsContainer);
    Base::initialize(realm);
}

}
