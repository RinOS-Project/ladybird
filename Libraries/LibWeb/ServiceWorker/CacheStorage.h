/*
 * Copyright (c) 2025, Aliaksandr Kalenik <kalenik.aliaksandr@gmail.com>
 *
 * SPDX-License-Identifier: BSD-2-Clause
 */

#pragma once

#include <AK/HashMap.h>
#include <LibWeb/Bindings/PlatformObject.h>
#include <LibWeb/WebIDL/Promise.h>

namespace Web::StorageAPI {
class StorageBottle;
}

namespace Web::ServiceWorker {

class Cache;

// https://w3c.github.io/ServiceWorker/#cachestorage-interface
class CacheStorage : public Bindings::PlatformObject {
    WEB_PLATFORM_OBJECT(CacheStorage, Bindings::PlatformObject);
    GC_DECLARE_ALLOCATOR(CacheStorage);

public:
    GC::Ref<WebIDL::Promise> has(String const& cache_name);
    GC::Ref<WebIDL::Promise> open(String const& cache_name);
    GC::Ref<WebIDL::Promise> delete_(String const& cache_name);
    GC::Ref<WebIDL::Promise> keys();

private:
    explicit CacheStorage(JS::Realm&);

    virtual void initialize(JS::Realm&) override;
    virtual void visit_edges(JS::Cell::Visitor&) override;

    OrderedHashMap<String, GC::Ref<Cache>> m_caches;
    GC::Ptr<StorageAPI::StorageBottle> m_storage_bottle;
    bool m_owner_authorized { false };
    u64 m_owner_generation { 0 };
};

}
