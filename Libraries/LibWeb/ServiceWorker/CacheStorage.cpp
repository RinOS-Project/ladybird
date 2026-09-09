/*
 * Copyright (c) 2025, Aliaksandr Kalenik <kalenik.aliaksandr@gmail.com>
 *
 * SPDX-License-Identifier: BSD-2-Clause
 */

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

}
