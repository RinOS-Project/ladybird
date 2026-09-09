/*
 * Copyright (c) 2025, Aliaksandr Kalenik <kalenik.aliaksandr@gmail.com>
 *
 * SPDX-License-Identifier: BSD-2-Clause
 */

#include <LibJS/Runtime/Array.h>
#include <LibJS/Runtime/PrimitiveString.h>
#include <LibWeb/Bindings/CacheStoragePrototype.h>
#include <LibWeb/Bindings/Intrinsics.h>
#include <LibWeb/HTML/Scripting/Environments.h>
#include <LibWeb/HTML/Window.h>
#include <LibWeb/HTML/WorkerGlobalScope.h>
#include <LibWeb/StorageAPI/StorageBottle.h>
#include <LibWeb/StorageAPI/StorageEndpoint.h>
#include <LibWeb/StorageAPI/StorageKey.h>
#include <LibWeb/ServiceWorker/Cache.h>
#include <LibWeb/ServiceWorker/CacheStorage.h>
#include <LibWeb/WebIDL/QuotaExceededError.h>
#include <LibWeb/WebIDL/Promise.h>

namespace Web::ServiceWorker {

GC_DEFINE_ALLOCATOR(CacheStorage);

static auto const cache_storage_marker = "RIN-CACHE-NAME-V1"_string;

static GC::Ptr<Page> page_for_cache_storage(JS::Realm& realm)
{
    auto& global_object = realm.global_object();
    if (is<HTML::Window>(global_object))
        return as<HTML::Window>(global_object).page();
    if (is<HTML::WorkerGlobalScope>(global_object))
        return as<HTML::WorkerGlobalScope>(global_object).page();
    return {};
}

CacheStorage::CacheStorage(JS::Realm& realm)
    : Bindings::PlatformObject(realm)
{
    auto storage_key = StorageAPI::obtain_a_storage_key(
        HTML::relevant_settings_object(realm.global_object()));
    auto page = page_for_cache_storage(realm);
    if (!storage_key.has_value() || !page)
        return;

    m_storage_bottle = StorageAPI::LocalStorageBottle::create(
        heap(), *page, storage_key.value(), {}, StorageAPI::StorageEndpointType::Caches);
    for (auto const& cache_name : m_storage_bottle->keys()) {
        auto marker = m_storage_bottle->get(cache_name);
        if (!marker.has_value() || marker.value() != cache_storage_marker)
            continue;
        m_caches.set(cache_name, Cache::create(realm, cache_name, m_storage_bottle));
    }
}

void CacheStorage::initialize(JS::Realm& realm)
{
    Base::initialize(realm);
    WEB_SET_PROTOTYPE_FOR_INTERFACE(CacheStorage);
}

void CacheStorage::visit_edges(JS::Cell::Visitor& visitor)
{
    Base::visit_edges(visitor);
    visitor.visit(m_caches);
    visitor.visit(m_storage_bottle);
}

// https://w3c.github.io/ServiceWorker/#cache-storage-open
GC::Ref<WebIDL::Promise> CacheStorage::open(String const& cache_name)
{
    if (!m_caches.contains(cache_name) && m_storage_bottle) {
        auto result = m_storage_bottle->set(cache_name, cache_storage_marker);
        if (result.has<WebView::StorageOperationError>())
            return WebIDL::create_rejected_promise_from_exception(
                realm(), WebIDL::QuotaExceededError::create(realm(), "Cache storage quota exceeded"_utf16));
    }

    auto cache = m_caches.ensure(cache_name, [this, &cache_name] {
        return Cache::create(realm(), cache_name, m_storage_bottle);
    });
    return WebIDL::create_resolved_promise(realm(), cache);
}

// https://w3c.github.io/ServiceWorker/#cache-storage-has
GC::Ref<WebIDL::Promise> CacheStorage::has(String const& cache_name)
{
    return WebIDL::create_resolved_promise(realm(), JS::Value(m_caches.contains(cache_name)));
}

// https://w3c.github.io/ServiceWorker/#cache-storage-delete
GC::Ref<WebIDL::Promise> CacheStorage::delete_(String const& cache_name)
{
    if (auto cache = m_caches.get(cache_name); cache.has_value())
        cache.value()->remove_persisted_entries();
    const bool removed = m_caches.remove(cache_name);
    if (removed && m_storage_bottle)
        m_storage_bottle->remove(cache_name);
    return WebIDL::create_resolved_promise(realm(), JS::Value(removed));
}

// https://w3c.github.io/ServiceWorker/#cache-storage-keys
GC::Ref<WebIDL::Promise> CacheStorage::keys()
{
    GC::RootVector<JS::Value> cache_names(realm().heap());
    cache_names.ensure_capacity(m_caches.size());
    for (auto const& entry : m_caches)
        cache_names.append(JS::PrimitiveString::create(realm().vm(), entry.key));
    return WebIDL::create_resolved_promise(realm(), JS::Array::create_from(realm(), cache_names));
}

}
