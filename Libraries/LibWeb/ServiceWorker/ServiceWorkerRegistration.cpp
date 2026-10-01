/*
 * Copyright (c) 2024, Tim Ledbetter <tim.ledbetter@ladybird.org>
 *
 * SPDX-License-Identifier: BSD-2-Clause
 */

#include <LibJS/Runtime/Realm.h>
#include <LibWeb/Bindings/Intrinsics.h>
#include <LibWeb/Bindings/ServiceWorkerRegistrationPrototype.h>
#include <LibWeb/HTML/Scripting/Environments.h>
#include <LibWeb/ServiceWorker/Job.h>
#include <LibWeb/ServiceWorker/ServiceWorker.h>
#include <LibWeb/ServiceWorker/ServiceWorkerRegistration.h>
#include <LibWeb/WebIDL/Promise.h>

namespace Web::ServiceWorker {

GC_DEFINE_ALLOCATOR(ServiceWorkerRegistration);

ServiceWorkerRegistration::ServiceWorkerRegistration(JS::Realm& realm, Registration const& registration)
    : DOM::EventTarget(realm)
    , m_storage_key(registration.storage_key())
    , m_scope_url(registration.scope_url())
    , m_update_via_cache(registration.update_via_cache())
{
}

void ServiceWorkerRegistration::initialize(JS::Realm& realm)
{
    WEB_SET_PROTOTYPE_FOR_INTERFACE(ServiceWorkerRegistration);
    Base::initialize(realm);
}

void ServiceWorkerRegistration::visit_edges(Cell::Visitor& visitor)
{
    Base::visit_edges(visitor);
    visitor.visit(m_installing);
    visitor.visit(m_waiting);
    visitor.visit(m_active);
}

GC::Ref<ServiceWorkerRegistration> ServiceWorkerRegistration::create(JS::Realm& realm, Registration const& registration)
{
    return realm.create<ServiceWorkerRegistration>(realm, registration);
}

GC::Ref<WebIDL::Promise> ServiceWorkerRegistration::unregister()
{
    auto& realm = this->realm();
    auto& vm = realm.vm();
    auto promise = WebIDL::create_promise(realm);
    auto& client = HTML::relevant_settings_object(*this);
    auto job = Job::create(vm, Job::Type::Unregister,
        m_storage_key, m_scope_url, m_scope_url, promise, client);
    schedule_job(vm, job);
    return promise;
}

}
