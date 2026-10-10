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
#include <LibWeb/ServiceWorker/Registration.h>
#include <LibWeb/ServiceWorker/ServiceWorker.h>
#include <LibWeb/ServiceWorker/ServiceWorkerRegistration.h>
#include <LibWeb/WebIDL/DOMException.h>
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

GC::Ref<WebIDL::Promise> ServiceWorkerRegistration::update()
{
    auto& realm = this->realm();
    auto& vm = realm.vm();
    auto promise = WebIDL::create_promise(realm);

    auto registration = Registration::get(m_storage_key, m_scope_url);
    auto* newest_worker = registration.has_value() ? registration->newest_worker() : nullptr;
    if (!newest_worker) {
        WebIDL::reject_promise(realm, promise,
            WebIDL::InvalidStateError::create(realm, "This ServiceWorkerRegistration has no associated worker"_utf16));
        return promise;
    }

    auto& client = HTML::relevant_settings_object(*this);
    auto job = Job::create(vm, Job::Type::Update,
        m_storage_key, m_scope_url, newest_worker->script_url, promise, client);
    job->worker_type = newest_worker->worker_type;
    job->update_via_cache = registration->update_via_cache();
    schedule_job(vm, job);
    return promise;
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
