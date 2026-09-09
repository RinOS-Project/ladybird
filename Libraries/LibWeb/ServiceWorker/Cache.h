/*
 * Copyright (c) 2026, RinOS contributors
 *
 * SPDX-License-Identifier: BSD-2-Clause
 */

#pragma once

#include <AK/Vector.h>
#include <LibWeb/Bindings/PlatformObject.h>
#include <LibWeb/Fetch/Request.h>
#include <LibWeb/Fetch/Response.h>
#include <LibWeb/WebIDL/Promise.h>

namespace Web::ServiceWorker {

// https://w3c.github.io/ServiceWorker/#cache-interface
//
struct CacheQueryOptions {
    bool ignore_search { false };
    bool ignore_method { false };
    bool ignore_vary { false };
};

// CacheStorage owns these objects and provides the name-to-cache lifetime.
// Entries are kept as cloned Fetch objects, so callers cannot mutate a cached
// request or response through a previously retained JS reference.
class Cache final : public Bindings::PlatformObject {
    WEB_PLATFORM_OBJECT(Cache, Bindings::PlatformObject);
    GC_DECLARE_ALLOCATOR(Cache);

public:
    static GC::Ref<Cache> create(JS::Realm&, String name);

    String const& name() const { return m_name; }

    GC::Ref<WebIDL::Promise> match(Fetch::RequestInfo const&, CacheQueryOptions const& = {});
    GC::Ref<WebIDL::Promise> match_all(Optional<Fetch::RequestInfo> const&, CacheQueryOptions const& = {});
    GC::Ref<WebIDL::Promise> add(Fetch::RequestInfo const&);
    GC::Ref<WebIDL::Promise> add_all(Vector<Fetch::RequestInfo> const&);
    GC::Ref<WebIDL::Promise> put(Fetch::RequestInfo const&, GC::Root<Fetch::Response> const&);
    GC::Ref<WebIDL::Promise> delete_(Fetch::RequestInfo const&, CacheQueryOptions const& = {});
    GC::Ref<WebIDL::Promise> keys(Optional<Fetch::RequestInfo> const&, CacheQueryOptions const& = {});

private:
    struct Entry {
        GC::Ref<Fetch::Request> request;
        GC::Ref<Fetch::Response> response;
    };

    Cache(JS::Realm&, String name);

    virtual void initialize(JS::Realm&) override;
    virtual void visit_edges(Cell::Visitor&) override;

    WebIDL::ExceptionOr<GC::Ref<Fetch::Request>> normalize_request(Fetch::RequestInfo const&) const;
    GC::Ref<WebIDL::Promise> put_normalized(GC::Ref<Fetch::Request>, GC::Ref<Fetch::Response>);
    bool matches(Entry const&, Fetch::Request const&, CacheQueryOptions const&) const;
    static String match_url(String const&, bool ignore_search);

    String m_name;
    Vector<Entry> m_entries;
};

}
