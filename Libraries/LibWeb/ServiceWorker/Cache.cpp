/*
 * Copyright (c) 2026, RinOS contributors
 *
 * SPDX-License-Identifier: BSD-2-Clause
 */

#include <LibWeb/Bindings/CachePrototype.h>
#include <LibWeb/Bindings/Intrinsics.h>
#include <LibWeb/ServiceWorker/Cache.h>

namespace Web::ServiceWorker {

GC_DEFINE_ALLOCATOR(Cache);

Cache::Cache(JS::Realm& realm, String name)
    : Bindings::PlatformObject(realm)
    , m_name(move(name))
{
}

void Cache::initialize(JS::Realm& realm)
{
    Base::initialize(realm);
    WEB_SET_PROTOTYPE_FOR_INTERFACE(Cache);
}

GC::Ref<Cache> Cache::create(JS::Realm& realm, String name)
{
    return realm.create<Cache>(realm, move(name));
}

}
