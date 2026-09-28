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
#include <LibWeb/Page/Page.h>
#include <LibWeb/StorageAPI/StorageBottle.h>
#include <LibWeb/StorageAPI/StorageEndpoint.h>
#include <LibWeb/StorageAPI/StorageKey.h>
#include <AK/StringBuilder.h>
#include <AK/Vector.h>
#include <LibWeb/ServiceWorker/Cache.h>
#include <LibWeb/ServiceWorker/CacheStorage.h>
#include <LibWeb/WebIDL/DOMException.h>
#include <LibWeb/WebIDL/QuotaExceededError.h>
#include <LibWeb/WebIDL/Promise.h>

namespace Web::ServiceWorker {

GC_DEFINE_ALLOCATOR(CacheStorage);

static auto const cache_storage_marker = "RIN-CACHE-NAME-V1"_string;
static constexpr StringView cache_storage_name_key_prefix =
    "RIN-CACHE-NAME-KEY-V1:"sv;

static String cache_storage_marker_key(String const& cache_name)
{
    static constexpr char hex_digits[] = "0123456789abcdef";
    StringBuilder key;
    key.append(cache_storage_name_key_prefix);
    for (u8 byte : cache_name.bytes()) {
        key.append(hex_digits[byte >> 4u]);
        key.append(hex_digits[byte & 0x0fu]);
    }
    return key.to_string_without_validation();
}

static Optional<String> cache_storage_name_from_marker_key(String const& key)
{
    auto bytes = key.bytes_as_string_view();
    if (!bytes.starts_with(cache_storage_name_key_prefix))
        return key;

    auto encoded = bytes.substring_view(cache_storage_name_key_prefix.length());
    if (encoded.length() % 2u != 0u)
        return {};
    auto hex_value = [](char ch) -> Optional<u8> {
        if (ch >= '0' && ch <= '9')
            return static_cast<u8>(ch - '0');
        if (ch >= 'a' && ch <= 'f')
            return static_cast<u8>(ch - 'a' + 10);
        if (ch >= 'A' && ch <= 'F')
            return static_cast<u8>(ch - 'A' + 10);
        return {};
    };

    Vector<u8> decoded;
    decoded.ensure_capacity(encoded.length() / 2u);
    for (size_t index = 0; index < encoded.length(); index += 2u) {
        auto high = hex_value(encoded[index]);
        auto low = hex_value(encoded[index + 1u]);
        if (!high.has_value() || !low.has_value())
            return {};
        decoded.append(static_cast<u8>((high.value() << 4u) | low.value()));
    }
    if (decoded.is_empty())
        return {};
    auto name = String::from_utf8(StringView {
        reinterpret_cast<char const*>(decoded.data()), decoded.size() });
    if (name.is_error())
        return {};
    return name.release_value();
}

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
    m_page = page;
    m_owner_origin = storage_key.value().to_string().to_byte_string();

    /* The durable Caches bottle is owned by Browser.  Do not restore names or
     * create a writable bottle until the current WebContent page has been
     * admitted by that owner.  This is deliberately a separate owner RPC
     * from ServiceWorker registration: a renderer that can register a worker
     * must still prove its profile before touching CacheStorage. */
    auto owner_response = page->client().request_service_worker_owner(
        4u, {}, m_owner_origin, {}, {}, 0u);
    if (!owner_response.accepted || !owner_response.found ||
        owner_response.generation == 0u)
        return;
    m_owner_authorized = true;
    m_owner_generation = owner_response.generation;

    m_storage_bottle = StorageAPI::LocalStorageBottle::create(
        heap(), *page, storage_key.value(), {}, StorageAPI::StorageEndpointType::Caches, m_owner_generation);
    for (auto const& marker_key : m_storage_bottle->keys()) {
        auto marker = m_storage_bottle->get(marker_key);
        if (!marker.has_value() || marker.value() != cache_storage_marker)
            continue;
        auto cache_name = cache_storage_name_from_marker_key(marker_key);
        if (!cache_name.has_value())
            continue;
        m_caches.set(cache_name.value(), Cache::create(realm, cache_name.value(), m_storage_bottle, page, m_owner_origin, m_owner_generation));
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
    visitor.visit(m_page);
}

bool CacheStorage::owner_is_current() const
{
    if (!m_owner_authorized || !m_page || m_owner_generation == 0 || m_owner_origin.is_empty())
        return false;
    auto response = m_page->client().request_service_worker_owner(
        4u, {}, m_owner_origin, {}, {}, 0u);
    return response.accepted && response.found &&
        response.generation == m_owner_generation &&
        response.origin == m_owner_origin;
}

bool CacheStorage::begin_owner_mutation_batch() const
{
    if (!owner_is_current())
        return false;
    auto response = m_page->client().request_service_worker_owner(
        7u, {}, m_owner_origin, {}, {}, 0u);
    return response.accepted && response.found &&
        response.generation == m_owner_generation &&
        response.origin == m_owner_origin &&
        response.script_url == m_owner_origin && response.scope == "/";
}

bool CacheStorage::commit_owner_mutation_batch() const
{
    if (!m_page || m_owner_generation == 0 || m_owner_origin.is_empty())
        return false;
    auto response = m_page->client().request_service_worker_owner(
        8u, {}, m_owner_origin, {}, {}, 0u);
    return response.accepted && response.found &&
        response.generation == m_owner_generation &&
        response.origin == m_owner_origin &&
        response.script_url == m_owner_origin && response.scope == "/";
}

void CacheStorage::abort_owner_mutation_batch() const
{
    if (!m_page || m_owner_generation == 0 || m_owner_origin.is_empty())
        return;
    (void)m_page->client().request_service_worker_owner(
        9u, {}, m_owner_origin, {}, {}, 0u);
}

GC::Ref<WebIDL::Promise> CacheStorage::owner_rejected_promise() const
{
    return WebIDL::create_rejected_promise_from_exception(
        realm(), WebIDL::InvalidStateError::create(
            realm(), "CacheStorage profile owner rejected the page"_utf16));
}

// https://w3c.github.io/ServiceWorker/#cache-storage-open
GC::Ref<WebIDL::Promise> CacheStorage::open(String const& cache_name)
{
    if (!owner_is_current())
        return owner_rejected_promise();
    if (!m_caches.contains(cache_name) && m_storage_bottle) {
        auto result = m_storage_bottle->set(
            cache_storage_marker_key(cache_name), cache_storage_marker);
        if (result.has<WebView::StorageOperationError>()) {
            auto quota_error = WebIDL::QuotaExceededError::create(
                realm(), "Cache storage quota exceeded"_utf16);
            return WebIDL::create_rejected_promise_from_exception(
                realm(), GC::Ref<WebIDL::DOMException>(*quota_error));
        }
    }

    auto cache = m_caches.ensure(cache_name, [this, &cache_name] {
        return Cache::create(realm(), cache_name, m_storage_bottle, m_page, m_owner_origin, m_owner_generation);
    });
    return WebIDL::create_resolved_promise(realm(), cache);
}

// https://w3c.github.io/ServiceWorker/#cache-storage-has
GC::Ref<WebIDL::Promise> CacheStorage::has(String const& cache_name)
{
    if (!owner_is_current())
        return WebIDL::create_resolved_promise(realm(), JS::Value(false));
    return WebIDL::create_resolved_promise(realm(), JS::Value(m_caches.contains(cache_name)));
}

// https://w3c.github.io/ServiceWorker/#cache-storage-delete
GC::Ref<WebIDL::Promise> CacheStorage::delete_(String const& cache_name)
{
    if (!owner_is_current())
        return owner_rejected_promise();
    auto cache = m_caches.get(cache_name);
    if (!cache.has_value())
        return WebIDL::create_resolved_promise(realm(), JS::Value(false));

    struct StorageRecord {
        String key;
        String value;
    };
    Vector<StorageRecord> original_entries;
    if (m_storage_bottle) {
        auto prefix = cache.value()->storage_key_prefix();
        for (auto const& key : m_storage_bottle->keys()) {
            if (!key.bytes_as_string_view().starts_with(
                    prefix.bytes_as_string_view()))
                continue;
            auto value = m_storage_bottle->get(key);
            if (value.has_value())
                original_entries.append({ key, value.release_value() });
        }
    }
    auto marker_key = cache_storage_marker_key(cache_name);
    auto original_marker = m_storage_bottle
        ? m_storage_bottle->get(marker_key)
        : Optional<String> {};
    if (m_storage_bottle && !original_marker.has_value()) {
        marker_key = cache_name;
        original_marker = m_storage_bottle->get(marker_key);
    }

    if (!begin_owner_mutation_batch())
        return owner_rejected_promise();
    const bool entries_removed = cache.value()->remove_persisted_entries();
    if (entries_removed && m_storage_bottle)
        m_storage_bottle->remove(marker_key);
    if (!entries_removed || !commit_owner_mutation_batch()) {
        if (m_storage_bottle) {
            for (auto const& record : original_entries)
                (void)m_storage_bottle->set(record.key, record.value);
            if (original_marker.has_value())
                (void)m_storage_bottle->set(marker_key, original_marker.value());
        }
        abort_owner_mutation_batch();
        return owner_rejected_promise();
    }

    (void)m_caches.remove(cache_name);
    return WebIDL::create_resolved_promise(realm(), JS::Value(true));
}

// https://w3c.github.io/ServiceWorker/#cache-storage-keys
GC::Ref<WebIDL::Promise> CacheStorage::keys()
{
    if (!owner_is_current())
        return owner_rejected_promise();
    GC::RootVector<JS::Value> cache_names(realm().heap());
    cache_names.ensure_capacity(m_caches.size());
    for (auto const& entry : m_caches)
        cache_names.append(JS::PrimitiveString::create(realm().vm(), entry.key));
    return WebIDL::create_resolved_promise(realm(), JS::Array::create_from(realm(), cache_names));
}

}
