/*
 * Copyright (c) 2024, Andrew Kaster <andrew@ladybird.org>
 *
 * SPDX-License-Identifier: BSD-2-Clause
 */

#pragma once

#include <AK/Optional.h>
#include <AK/StringBuilder.h>
#include <AK/Traits.h>
#include <LibURL/Origin.h>
#include <LibURL/Site.h>
#include <LibWeb/Forward.h>

namespace Web::StorageAPI {

// https://storage.spec.whatwg.org/#storage-keys
struct StorageKey {

    // ServiceWorker storage keys also retain the schemeful top-level site so
    // the registration map can distinguish the same origin across partitions.
    URL::Origin origin;
    URL::Site top_level_site;

    explicit StorageKey(URL::Origin origin)
        : origin(move(origin))
        , top_level_site(URL::Site::obtain(this->origin))
    {
    }

    StorageKey(URL::Origin origin, URL::Site top_level_site)
        : origin(move(origin))
        , top_level_site(move(top_level_site))
    {
    }

    String to_string() const
    {
        // StorageEndpoint owner IPC currently serializes the origin alone.
        // The Browser ServiceWorker owner derives its site from the
        // authenticated page owner instead of accepting it through this
        // renderer-facing serialization.
        return origin.serialize();
    }

    String to_cache_storage_string() const
    {
        StringBuilder key;
        key.append(origin.serialize());
        key.append('\n');
        key.append(top_level_site.serialize());
        return key.to_string_without_validation();
    }

    friend bool operator==(StorageKey const& a, StorageKey const& b)
    {
        // The top-level site is a partition key for ServiceWorker registrations.
        return a.origin.is_same_origin(b.origin)
            && a.top_level_site.is_same_site(b.top_level_site);
    }
};

Optional<StorageKey> obtain_a_storage_key(HTML::Environment const&);
StorageKey obtain_a_storage_key_for_non_storage_purposes(URL::Origin const&);
StorageKey obtain_a_storage_key_for_non_storage_purposes(HTML::Environment const&);

}

namespace AK {

template<>
struct Traits<Web::StorageAPI::StorageKey> : public DefaultTraits<Web::StorageAPI::StorageKey> {
    static unsigned hash(Web::StorageAPI::StorageKey const& key)
    {
        auto const origin_hash = Traits<URL::Origin>::hash(key.origin);
        auto const top_level_site_hash = key.top_level_site.serialize().hash();
        return pair_int_hash(origin_hash, top_level_site_hash);
    }
};

}
