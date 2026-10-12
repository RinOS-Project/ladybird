/*
 * Copyright (c) 2024, Andrew Kaster <andrew@ladybird.org>
 *
 * SPDX-License-Identifier: BSD-2-Clause
 */

#pragma once

#include <AK/ByteBuffer.h>
#include <AK/HashMap.h>
#include <LibURL/URL.h>
#include <LibWeb/Bindings/ServiceWorkerPrototype.h>
#include <LibWeb/Bindings/WorkerPrototype.h>

namespace Web::ServiceWorker {

// https://w3c.github.io/ServiceWorker/#dfn-service-worker
// This class corresponds to "service worker", not "ServiceWorker"
// FIXME: This should be owned and managed at the user agent level
// FIXME: A lot of the fields for this struct actually need to live in the Agent for the service worker in the WebWorker process
struct ServiceWorkerRecord {
    // https://w3c.github.io/ServiceWorker/#dfn-state
    // A service worker has an associated state, which is one of "parsed", "installing", "installed", "activating", "activated", and "redundant". It is initially "parsed".
    Bindings::ServiceWorkerState state = Bindings::ServiceWorkerState::Parsed;

    // https://w3c.github.io/ServiceWorker/#dfn-script-url
    // A service worker has an associated script url (a URL).
    URL::URL script_url;

    // https://w3c.github.io/ServiceWorker/#dfn-type
    // A service worker has an associated type which is either "classic" or "module". Unless stated otherwise, it is "classic".
    Bindings::WorkerType worker_type = Bindings::WorkerType::Classic;

    // https://w3c.github.io/ServiceWorker/#dfn-classic-scripts-imported-flag
    // A service worker has an associated classic scripts imported flag. It is initially unset.
    bool classic_scripts_imported { false };

    // Exact fetched bytes are retained for the ServiceWorker update algorithm's
    // byte-for-byte comparison. Response streams are consumed while scripts are
    // parsed, so retaining Response objects alone is not sufficient here.
    OrderedHashMap<URL::URL, ByteBuffer> script_resource_map;
    // Set only when the map contains the complete installed script set. Until
    // then, an update must not treat a missing resource as an unchanged script.
    bool script_resource_map_complete { false };

    // FIXME: A lot more fields after this...
};

}
