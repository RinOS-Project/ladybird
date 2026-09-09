/*
 * Copyright (c) 2026, RinOS contributors
 *
 * SPDX-License-Identifier: BSD-2-Clause
 */

#pragma once

#include <LibWeb/Bindings/PlatformObject.h>

namespace Web::ServiceWorker {

// https://w3c.github.io/ServiceWorker/#cache-interface
//
// Cache is intentionally small at this stage.  CacheStorage owns these
// objects and provides the name-to-cache lifetime required by the Service
// Worker API.  Request/Response entry operations are tracked separately in
// the implementation status until the Fetch integration is ready.
class Cache final : public Bindings::PlatformObject {
    WEB_PLATFORM_OBJECT(Cache, Bindings::PlatformObject);
    GC_DECLARE_ALLOCATOR(Cache);

public:
    static GC::Ref<Cache> create(JS::Realm&, String name);

    String const& name() const { return m_name; }

private:
    Cache(JS::Realm&, String name);

    virtual void initialize(JS::Realm&) override;

    String m_name;
};

}
