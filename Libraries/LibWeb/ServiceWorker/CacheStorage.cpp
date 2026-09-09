/*
 * Copyright (c) 2025, Aliaksandr Kalenik <kalenik.aliaksandr@gmail.com>
 *
 * SPDX-License-Identifier: BSD-2-Clause
 */

#include <LibJS/Runtime/Array.h>
#include <LibJS/Runtime/PrimitiveString.h>
#include <LibWeb/Bindings/CacheStoragePrototype.h>
#include <LibWeb/Bindings/Intrinsics.h>
#include <LibWeb/ServiceWorker/Cache.h>
#include <LibWeb/ServiceWorker/CacheStorage.h>
#include <LibWeb/WebIDL/Promise.h>

namespace Web::ServiceWorker {

GC_DEFINE_ALLOCATOR(CacheStorage);

CacheStorage::CacheStorage(JS::Realm& realm)
    : Bindings::PlatformObject(realm)
{
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
}

// https://w3c.github.io/ServiceWorker/#cache-storage-open
GC::Ref<WebIDL::Promise> CacheStorage::open(String const& cache_name)
{
    auto cache = m_caches.ensure(cache_name, [this, &cache_name] {
        return Cache::create(realm(), cache_name);
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
    return WebIDL::create_resolved_promise(realm(), JS::Value(m_caches.remove(cache_name)));
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
