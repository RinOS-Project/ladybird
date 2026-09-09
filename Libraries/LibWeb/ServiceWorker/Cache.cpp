/*
 * Copyright (c) 2026, RinOS contributors
 *
 * SPDX-License-Identifier: BSD-2-Clause
 */

#include <AK/TypeCasts.h>
#include <AK/ByteBuffer.h>
#include <AK/QuickSort.h>
#include <LibJS/Runtime/Array.h>
#include <LibWeb/Bindings/ExceptionOrUtils.h>
#include <LibWeb/Bindings/CachePrototype.h>
#include <LibWeb/Bindings/Intrinsics.h>
#include <LibWeb/Fetch/BodyInit.h>
#include <LibWeb/Fetch/FetchMethod.h>
#include <LibWeb/Fetch/Infrastructure/URL.h>
#include <LibWeb/Fetch/Request.h>
#include <LibWeb/Fetch/Response.h>
#include <LibWeb/Page/Page.h>
#include <LibWeb/ServiceWorker/Cache.h>
#include <LibWeb/WebIDL/DOMException.h>
#include <LibWeb/WebIDL/AbstractOperations.h>
#include <LibWeb/WebIDL/ExceptionOr.h>
#include <LibWeb/WebIDL/QuotaExceededError.h>
#include <LibWebView/StorageSetResult.h>

namespace Web::ServiceWorker {

GC_DEFINE_ALLOCATOR(Cache);

static constexpr StringView cache_entry_prefix = "RIN-CACHE-ENTRY-V2:"sv;

static char hex_digit(u8 value)
{
    return value < 10 ? static_cast<char>('0' + value) : static_cast<char>('a' + (value - 10));
}

static String hex_encode(ReadonlyBytes bytes)
{
    StringBuilder builder;
    builder.ensure_capacity(bytes.size() * 2);
    for (auto byte : bytes) {
        builder.append(hex_digit(byte >> 4));
        builder.append(hex_digit(byte & 0xf));
    }
    return builder.to_string_without_validation();
}

static Optional<ByteBuffer> hex_decode(StringView encoded)
{
    if (encoded.length() % 2 != 0)
        return {};
    auto bytes = ByteBuffer::create_zeroed(encoded.length() / 2);
    if (bytes.is_error())
        return {};
    auto buffer = bytes.release_value();
    auto nibble = [](char c) -> Optional<u8> {
        if (c >= '0' && c <= '9')
            return static_cast<u8>(c - '0');
        if (c >= 'a' && c <= 'f')
            return static_cast<u8>(c - 'a' + 10);
        if (c >= 'A' && c <= 'F')
            return static_cast<u8>(c - 'A' + 10);
        return {};
    };
    for (size_t i = 0; i < buffer.size(); ++i) {
        auto high = nibble(encoded[i * 2]);
        auto low = nibble(encoded[i * 2 + 1]);
        if (!high.has_value() || !low.has_value())
            return {};
        buffer[i] = static_cast<u8>((*high << 4) | *low);
    }
    return buffer;
}

static String encode_headers(HTTP::HeaderList const& headers)
{
    StringBuilder encoded;
    bool first = true;
    for (auto const& header : headers.headers()) {
        if (!first)
            encoded.append(';');
        first = false;
        auto encoded_name = hex_encode(header.name.bytes());
        auto encoded_value = hex_encode(header.value.bytes());
        encoded.append(encoded_name);
        encoded.append('=');
        encoded.append(encoded_value);
    }
    return encoded.to_string_without_validation();
}

static bool decode_headers(StringView encoded, HTTP::HeaderList& headers)
{
    for (auto item : encoded.split_view(';', SplitBehavior::KeepEmpty)) {
        if (item.is_empty())
            continue;
        auto separator = item.find_byte_offset('=');
        if (!separator.has_value())
            return false;
        auto name = hex_decode(item.substring_view(0, separator.value()));
        auto value = hex_decode(item.substring_view(separator.value() + 1));
        if (!name.has_value() || !value.has_value())
            return false;
        headers.append({ ByteString(name->bytes()), ByteString(value->bytes()) });
    }
    return true;
}

static Optional<String> decode_string(StringView encoded)
{
    auto bytes = hex_decode(encoded);
    if (!bytes.has_value())
        return {};
    auto string = String::from_utf8(StringView { bytes->bytes() });
    if (string.is_error())
        return {};
    return string.release_value();
}

Cache::Cache(JS::Realm& realm, String name, GC::Ptr<StorageAPI::StorageBottle> storage_bottle,
             GC::Ptr<Page> page, ByteString owner_origin, u64 owner_generation)
    : Bindings::PlatformObject(realm)
    , m_name(move(name))
    , m_storage_bottle(storage_bottle)
    , m_page(page)
    , m_owner_origin(move(owner_origin))
    , m_owner_generation(owner_generation)
{
    restore_entries();
}

void Cache::initialize(JS::Realm& realm)
{
    Base::initialize(realm);
    WEB_SET_PROTOTYPE_FOR_INTERFACE(Cache);
}

void Cache::visit_edges(Cell::Visitor& visitor)
{
    Base::visit_edges(visitor);
    visitor.visit(m_storage_bottle);
    visitor.visit(m_page);
    for (auto& entry : m_entries) {
        visitor.visit(entry.request);
        visitor.visit(entry.response);
    }
}

bool Cache::owner_is_current() const
{
    // Standalone in-memory Cache objects used by embedding/unit callers have
    // no durable owner to authenticate. Durable Caches always carry both a
    // storage bottle and a non-zero generation, so they cannot take this path.
    if (!m_storage_bottle && m_owner_generation == 0)
        return true;
    if (!m_page || m_owner_generation == 0 || m_owner_origin.is_empty())
        return false;
    auto response = m_page->client().request_service_worker_owner(
        4u, {}, m_owner_origin, {}, {}, 0u);
    return response.accepted && response.found &&
        response.generation == m_owner_generation &&
        response.origin == m_owner_origin;
}

bool Cache::owner_fetch_is_current(Fetch::Request const& request) const
{
    if (!owner_is_current())
        return false;
    if (!m_page || m_owner_generation == 0 || m_owner_origin.is_empty())
        return false;
    auto request_url = request.url().to_byte_string();
    auto response = m_page->client().request_service_worker_owner(
        5u, request_url, m_owner_origin, {}, {}, 0u);
    return response.accepted && response.found &&
        response.generation == m_owner_generation &&
        response.origin == m_owner_origin &&
        response.script_url == request_url;
}

GC::Ref<WebIDL::Promise> Cache::owner_rejected_promise() const
{
    return WebIDL::create_rejected_promise_from_exception(
        realm(), WebIDL::InvalidStateError::create(
            realm(), "Cache profile owner rejected the page"_utf16));
}

WebIDL::ExceptionOr<GC::Ref<Fetch::Request>> Cache::normalize_request(Fetch::RequestInfo const& input) const
{
    auto result = Fetch::Request::construct_impl(realm(), input);
    if (result.is_exception())
        return result.release_error();
    return result.release_value();
}

String Cache::match_url(String const& url, bool ignore_search)
{
    if (!ignore_search)
        return url;
    auto question_mark = url.find_byte_offset('?');
    if (!question_mark.has_value())
        return url;
    return MUST(url.substring_from_byte_offset(0, question_mark.value()));
}

String Cache::storage_key_for(Fetch::Request const& request) const
{
    StringBuilder key;
    key.append(cache_entry_prefix);
    auto encoded_name = hex_encode(m_name.bytes());
    auto encoded_url = hex_encode(request.url().bytes());
    key.append(encoded_name);
    key.append(':');
    key.append(encoded_url);
    return key.to_string_without_validation();
}

Optional<String> Cache::serialize_entry(Entry const& entry, ReadonlyBytes body) const
{
    StringBuilder value;
    value.append("RIN-CACHE-ENTRY-V2"sv);
    value.append('|');
    auto encoded_method = hex_encode(entry.request->method().bytes());
    auto encoded_url = hex_encode(entry.request->url().bytes());
    auto encoded_status_text = hex_encode(entry.response->status_text().bytes());
    auto encoded_request_headers = encode_headers(*entry.request->request()->header_list());
    auto encoded_response_headers = encode_headers(*entry.response->response()->header_list());
    auto encoded_body = hex_encode(body);
    auto type = String::number(static_cast<u8>(entry.response->response()->type()));
    auto status = String::number(entry.response->status());
    value.append(encoded_method);
    value.append('|');
    value.append(encoded_url);
    value.append('|');
    value.append(type);
    value.append('|');
    value.append(status);
    value.append('|');
    value.append(encoded_status_text);
    value.append('|');
    value.append(entry.response->body_impl() ? '1' : '0');
    value.append('|');
    value.append(encoded_request_headers);
    value.append('|');
    value.append(encoded_response_headers);
    value.append('|');
    value.append(encoded_body);
    value.append('|');
    auto sequence = String::number(entry.sequence);
    value.append(sequence);
    return value.to_string_without_validation();
}

Optional<Cache::Entry> Cache::deserialize_entry(String const& key, String const& value)
{
    if (!key.bytes_as_string_view().starts_with(cache_entry_prefix))
        return {};
    auto fields = value.bytes_as_string_view().split_view('|', SplitBehavior::KeepEmpty);
    if ((fields.size() != 10 && fields.size() != 11) || fields[0] != "RIN-CACHE-ENTRY-V2"sv)
        return {};

    auto method = decode_string(fields[1]);
    auto url = decode_string(fields[2]);
    auto status = fields[4].to_number<u16>();
    auto status_text = hex_decode(fields[5]);
    auto body = hex_decode(fields[9]);
    auto body_present = fields[6] == "1"sv;
    auto response_type = fields[3].to_number<u8>();
    auto sequence = fields.size() == 11 ? fields[10].to_number<u64>() : Optional<u64> {};
    if (fields.size() == 11 && !sequence.has_value())
        return {};
    if (!method.has_value() || !url.has_value() || !status.has_value() || !status_text.has_value() || !body.has_value() || !response_type.has_value() || *response_type > static_cast<u8>(Fetch::Infrastructure::Response::Type::OpaqueRedirect) || (*response_type == static_cast<u8>(Fetch::Infrastructure::Response::Type::Error)) || (fields[6] != "0"sv && fields[6] != "1"sv))
        return {};
    if (*status > 599)
        return {};

    Fetch::RequestInit request_init;
    request_init.method = method.release_value();
    auto request = Fetch::Request::construct_impl(realm(), url.release_value(), request_init);
    if (request.is_exception())
        return {};
    if (!decode_headers(fields[7], *request.value()->request()->header_list()))
        return {};

    auto response = Fetch::Response::create(realm(), Fetch::Infrastructure::Response::create(realm().vm()), Fetch::Headers::Guard::Response);
    response->response()->set_type(static_cast<Fetch::Infrastructure::Response::Type>(*response_type));
    response->response()->set_status(*status);
    response->response()->set_status_message(ByteString(status_text->bytes()));
    if (!decode_headers(fields[8], *response->response()->header_list()))
        return {};
    if (body_present) {
        auto extracted = Fetch::extract_body(realm(), Fetch::BodyInitOrReadableBytes { body->bytes() });
        if (extracted.is_exception())
            return {};
        response->response()->set_body(extracted.value().body);
    }
    return Entry { sequence.value_or(0), request.release_value(), response };
}

bool Cache::store_serialized_entry(String const& key, String const& value)
{
    if (!m_storage_bottle)
        return true;

    auto result = m_storage_bottle->set(key, value);
    if (!result.has<WebView::StorageOperationError>())
        return true;

    // Cache endpoint storage is quota-bound in the browser owner. Stage the
    // oldest records first and only publish their in-memory removal after the
    // replacement fits. If no candidate fits, restore every removed record so
    // a quota failure cannot silently destroy otherwise valid cache entries.
    Vector<String> evicted_keys;
    Vector<Optional<String>> evicted_values;
    size_t candidate_count = 0;
    while (candidate_count < m_entries.size()) {
        auto victim_key = storage_key_for(*m_entries[candidate_count].request);
        auto victim_value = m_storage_bottle->get(victim_key);
        evicted_keys.append(victim_key);
        evicted_values.append(victim_value);
        m_storage_bottle->remove(victim_key);
        ++candidate_count;

        result = m_storage_bottle->set(key, value);
        if (!result.has<WebView::StorageOperationError>()) {
            for (size_t i = 0; i < candidate_count; ++i)
                m_entries.remove(0);
            return true;
        }
    }

    // The attempted replacement was never published on quota failure. Put
    // the old values back before reporting failure; the owner storage was
    // already within quota before this transaction started.
    for (size_t i = 0; i < evicted_values.size(); ++i) {
        if (evicted_values[i].has_value())
            (void)m_storage_bottle->set(evicted_keys[i], evicted_values[i].value());
    }
    return false;
}

void Cache::restore_entries()
{
    if (!m_storage_bottle)
        return;
    for (auto const& key : m_storage_bottle->keys()) {
        if (!key.bytes_as_string_view().starts_with(cache_entry_prefix))
            continue;
        auto value = m_storage_bottle->get(key);
        if (!value.has_value())
            continue;
        auto entry = deserialize_entry(key, value.value());
        if (entry.has_value()) {
            if (entry->sequence == 0)
                entry->sequence = m_next_sequence++;
            else if (entry->sequence >= m_next_sequence)
                m_next_sequence = entry->sequence + 1;
            m_entries.append(entry.release_value());
        } else
            m_storage_bottle->remove(key);
    }
    quick_sort(m_entries, [](auto const& left, auto const& right) { return left.sequence < right.sequence; });
}

void Cache::remove_persisted_entries()
{
    if (!m_storage_bottle)
        return;
    StringBuilder prefix;
    prefix.append(cache_entry_prefix);
    auto encoded_name = hex_encode(m_name.bytes());
    prefix.append(encoded_name);
    prefix.append(':');
    auto prefix_string = prefix.to_string_without_validation();
    for (auto const& key : m_storage_bottle->keys()) {
        if (key.bytes_as_string_view().starts_with(prefix_string.bytes_as_string_view()))
            m_storage_bottle->remove(key);
    }
}

GC::Ref<WebIDL::Promise> Cache::persist_entry(Entry entry, GC::Ref<Fetch::Response> response)
{
    auto promise = WebIDL::create_promise(realm());
    auto bytes_promise = response->bytes();
    if (bytes_promise.is_exception())
        return WebIDL::create_rejected_promise_from_exception(realm(), bytes_promise.release_error());
    WebIDL::react_to_promise(
        *bytes_promise.value(),
        GC::create_function(realm().heap(), [this, promise, entry = move(entry)](JS::Value value) mutable -> WebIDL::ExceptionOr<JS::Value> {
            if (!owner_is_current()) {
                WebIDL::reject_promise(realm(), promise, WebIDL::InvalidStateError::create(
                    realm(), "Cache profile owner was revoked during put"_utf16));
                return JS::js_undefined();
            }
            if (!value.is_object()) {
                WebIDL::reject_promise(realm(), promise, JS::TypeError::create(realm(), "Cache response body is not a byte sequence"sv));
                return JS::js_undefined();
            }
            auto bytes = WebIDL::get_buffer_source_copy(value.as_object());
            if (bytes.is_error()) {
                WebIDL::reject_promise(realm(), promise, JS::TypeError::create(realm(), "Cache response body could not be read"sv));
                return JS::js_undefined();
            }
            if (entry.sequence == 0)
                entry.sequence = m_next_sequence++;
            auto key = storage_key_for(*entry.request);
            auto serialized = serialize_entry(entry, bytes.value().bytes());
            if (!serialized.has_value() || !store_serialized_entry(key, serialized.value())) {
                WebIDL::reject_promise(realm(), promise, WebIDL::QuotaExceededError::create(realm(), "Cache storage quota exceeded"_utf16));
                return JS::js_undefined();
            }
            commit_entry(move(entry));
            WebIDL::resolve_promise(realm(), promise);
            return JS::js_undefined();
        }),
        GC::create_function(realm().heap(), [this, promise](JS::Value reason) -> WebIDL::ExceptionOr<JS::Value> {
            WebIDL::reject_promise(realm(), promise, reason);
            return JS::js_undefined();
        }));
    return promise;
}

static bool is_cacheable_request(Fetch::Request const& request)
{
    return Fetch::Infrastructure::is_http_or_https_scheme(request.request()->url().scheme());
}

bool Cache::matches(Entry const& entry, Fetch::Request const& request, CacheQueryOptions const& options) const
{
    if (!options.ignore_method && entry.request->method() != request.method())
        return false;
    if (match_url(entry.request->url(), options.ignore_search) != match_url(request.url(), options.ignore_search))
        return false;

    if (options.ignore_vary)
        return true;

    bool matches_vary = true;
    entry.response->response()->header_list()->for_each_vary_header([&](StringView header_name) {
        if (header_name == "*"sv) {
            matches_vary = false;
            return IterationDecision::Break;
        }
        auto cached_value = entry.request->request()->header_list()->get(header_name).value_or({});
        auto candidate_value = request.request()->header_list()->get(header_name).value_or({});
        if (cached_value != candidate_value) {
            matches_vary = false;
            return IterationDecision::Break;
        }
        return IterationDecision::Continue;
    });
    return matches_vary;
}

GC::Ref<WebIDL::Promise> Cache::match(Fetch::RequestInfo const& input, CacheQueryOptions const& options)
{
    if (!owner_is_current())
        return owner_rejected_promise();
    auto request = normalize_request(input);
    if (request.is_exception())
        return WebIDL::create_rejected_promise_from_exception(realm(), request.release_error());

    for (auto const& entry : m_entries) {
        if (!matches(entry, *request.value(), options))
            continue;
        auto response = entry.response->clone();
        if (response.is_exception())
            return WebIDL::create_rejected_promise_from_exception(realm(), response.release_error());
        return WebIDL::create_resolved_promise(realm(), response.release_value());
    }
    return WebIDL::create_resolved_promise(realm(), JS::js_undefined());
}

GC::Ref<WebIDL::Promise> Cache::match_all(Optional<Fetch::RequestInfo> const& input, CacheQueryOptions const& options)
{
    if (!owner_is_current())
        return owner_rejected_promise();
    Optional<GC::Ref<Fetch::Request>> request;
    if (input.has_value()) {
        auto normalized = normalize_request(input.value());
        if (normalized.is_exception())
            return WebIDL::create_rejected_promise_from_exception(realm(), normalized.release_error());
        request = normalized.release_value();
    }

    GC::RootVector<JS::Value> responses(realm().heap());
    for (auto const& entry : m_entries) {
        if (request.has_value() && !matches(entry, *request, options))
            continue;
        auto response = entry.response->clone();
        if (response.is_exception())
            return WebIDL::create_rejected_promise_from_exception(realm(), response.release_error());
        responses.append(response.release_value());
    }
    return WebIDL::create_resolved_promise(realm(), JS::Array::create_from(realm(), responses));
}

GC::Ref<WebIDL::Promise> Cache::add(Fetch::RequestInfo const& input)
{
    if (!owner_is_current())
        return owner_rejected_promise();
    auto request = normalize_request(input);
    if (request.is_exception())
        return WebIDL::create_rejected_promise_from_exception(realm(), request.release_error());

    // Cache.add() is defined only for GET requests.  Reject before starting
    // the fetch so a caller cannot trigger an external request that can never
    // be committed to this cache.
    if (request.value()->method() != "GET"_string)
        return WebIDL::create_rejected_promise_from_exception(realm(), JS::throw_completion(JS::TypeError::create(realm(), "Only GET requests can be added to a Cache"sv)));
    if (!is_cacheable_request(*request.value()))
        return WebIDL::create_rejected_promise_from_exception(realm(), JS::throw_completion(JS::TypeError::create(realm(), "Only HTTP(S) requests can be added to a Cache"sv)));

    auto promise = WebIDL::create_promise(realm());
    // The fetch must be admitted by the same live page/profile owner as the
    // cache. The network IPC carries the page id; this generation check closes
    // the gap where a revoked profile could otherwise start a new fetch.
    if (!owner_is_current())
        return owner_rejected_promise();
    auto fetch_promise = Fetch::fetch(realm().vm(), input);
    WebIDL::react_to_promise(
        *fetch_promise,
        GC::create_function(realm().heap(), [this, promise, request = request.release_value()](JS::Value value) -> WebIDL::ExceptionOr<JS::Value> {
            if (!owner_fetch_is_current(*request)) {
                WebIDL::reject_promise(realm(), promise, WebIDL::InvalidStateError::create(
                    realm(), "Cache profile owner was revoked during fetch"_utf16));
                return JS::js_undefined();
            }
            if (!value.is<Fetch::Response>()) {
                WebIDL::reject_promise(realm(), promise, JS::TypeError::create(realm(), "Fetch did not produce a Response"sv));
                return JS::js_undefined();
            }

            GC::Ref<Fetch::Response> response = as<Fetch::Response>(value.as_object());
            if (!response->ok() || response->status() == 206) {
                WebIDL::reject_promise(realm(), promise, JS::TypeError::create(realm(), "Cache.add() received a non-cacheable response"sv));
                return JS::js_undefined();
            }
            auto put_promise = put_normalized(request, response);
            WebIDL::react_to_promise(
                *put_promise,
                GC::create_function(realm().heap(), [this, promise](JS::Value) -> WebIDL::ExceptionOr<JS::Value> {
                    WebIDL::resolve_promise(realm(), promise);
                    return JS::js_undefined();
                }),
                GC::create_function(realm().heap(), [this, promise](JS::Value reason) -> WebIDL::ExceptionOr<JS::Value> {
                    WebIDL::reject_promise(realm(), promise, reason);
                    return JS::js_undefined();
                }));
            return JS::js_undefined();
        }),
        GC::create_function(realm().heap(), [this, promise](JS::Value reason) -> WebIDL::ExceptionOr<JS::Value> {
            WebIDL::reject_promise(realm(), promise, reason);
            return JS::js_undefined();
        }));
    return promise;
}

GC::Ref<WebIDL::Promise> Cache::add_all(Vector<Fetch::RequestInfo> const& inputs)
{
    if (!owner_is_current())
        return owner_rejected_promise();
    auto promise = WebIDL::create_promise(realm());
    Vector<GC::Ref<Fetch::Request>> requests;
    Vector<GC::Ref<WebIDL::Promise>> fetch_promises;
    requests.ensure_capacity(inputs.size());
    fetch_promises.ensure_capacity(inputs.size());
    for (auto const& input : inputs) {
        auto request = normalize_request(input);
        if (request.is_exception())
            return WebIDL::create_rejected_promise_from_exception(realm(), request.release_error());
        // Validate the complete request list before starting any fetch.  This
        // keeps addAll() failure-atomic with respect to network side effects:
        // one unsupported method must not start earlier requests.
        if (request.value()->method() != "GET"_string)
            return WebIDL::create_rejected_promise_from_exception(realm(), JS::throw_completion(JS::TypeError::create(realm(), "Only GET requests can be added to a Cache"sv)));
        if (!is_cacheable_request(*request.value()))
            return WebIDL::create_rejected_promise_from_exception(realm(), JS::throw_completion(JS::TypeError::create(realm(), "Only HTTP(S) requests can be added to a Cache"sv)));
        requests.append(request.release_value());
    }

    // Start network work only after the complete input list has been
    // normalized and admitted.  A malformed or non-GET later entry therefore
    // cannot leave earlier entries with an in-flight fetch.
    for (auto const& request : requests) {
        if (!owner_fetch_is_current(*request))
            return owner_rejected_promise();
    }
    for (auto const& input : inputs)
        fetch_promises.append(Fetch::fetch(realm().vm(), input));

    WebIDL::wait_for_all(
        realm(),
        fetch_promises,
        [this, promise, requests = move(requests)](Vector<JS::Value> const& values) mutable {
            if (!owner_is_current()) {
                WebIDL::reject_promise(realm(), promise, WebIDL::InvalidStateError::create(
                    realm(), "Cache profile owner was revoked during fetch"_utf16));
                return;
            }
            Vector<Entry> staged_entries;
            staged_entries.ensure_capacity(values.size());
            for (size_t i = 0; i < values.size(); ++i) {
                if (!values[i].is<Fetch::Response>()) {
                    WebIDL::reject_promise(realm(), promise, JS::TypeError::create(realm(), "Cache.addAll() fetch did not produce a Response"sv));
                    return;
                }
                auto const& response = as<Fetch::Response>(values[i].as_object());
                if (!response.ok() || response.status() == 206) {
                    WebIDL::reject_promise(realm(), promise, JS::TypeError::create(realm(), "Cache.addAll() received a non-cacheable response"sv));
                    return;
                }
                auto staged = clone_entry(requests[i], response);
                if (staged.is_exception()) {
                    auto completion = Bindings::exception_to_throw_completion(realm().vm(), staged.release_error());
                    WebIDL::reject_promise(realm(), promise, completion.release_value());
                    return;
                }
                staged_entries.append(staged.release_value());
            }
            for (auto const& entry : staged_entries) {
                if (!owner_fetch_is_current(*entry.request)) {
                    WebIDL::reject_promise(realm(), promise, WebIDL::InvalidStateError::create(
                        realm(), "Cache profile owner was revoked during fetch"_utf16));
                    return;
                }
            }
            // Persist every cloned entry before resolving addAll(). Clones
            // are still staged until all fetches and response cloning have
            // succeeded, so malformed input and non-cacheable responses do
            // not publish a partial batch.
            Vector<GC::Ref<WebIDL::Promise>> persist_promises;
            persist_promises.ensure_capacity(staged_entries.size());
            for (auto& entry : staged_entries) {
                auto persistence_response = entry.response->clone();
                if (persistence_response.is_exception()) {
                    auto completion = Bindings::exception_to_throw_completion(realm().vm(), persistence_response.release_error());
                    WebIDL::reject_promise(realm(), promise, completion.release_value());
                    return;
                }
                persist_promises.append(persist_entry(move(entry), persistence_response.release_value()));
            }
            WebIDL::wait_for_all(
                realm(),
                persist_promises,
                [this, promise](Vector<JS::Value> const&) {
                    WebIDL::resolve_promise(realm(), promise);
                },
                [this, promise](JS::Value reason) {
                    WebIDL::reject_promise(realm(), promise, reason);
                });
        },
        [this, promise](JS::Value reason) {
            WebIDL::reject_promise(realm(), promise, reason);
        });
    return promise;
}

GC::Ref<WebIDL::Promise> Cache::put(Fetch::RequestInfo const& input, GC::Root<Fetch::Response> const& response)
{
    if (!owner_is_current())
        return owner_rejected_promise();
    auto request = normalize_request(input);
    if (request.is_exception())
        return WebIDL::create_rejected_promise_from_exception(realm(), request.release_error());
    return put_normalized(request.release_value(), *response);
}

GC::Ref<WebIDL::Promise> Cache::put_normalized(GC::Ref<Fetch::Request> request, GC::Ref<Fetch::Response> response)
{
    if (request->method() != "GET"_string)
        return WebIDL::create_rejected_promise_from_exception(realm(), JS::throw_completion(JS::TypeError::create(realm(), "Only GET requests can be stored in a Cache"sv)));
    if (!is_cacheable_request(*request))
        return WebIDL::create_rejected_promise_from_exception(realm(), JS::throw_completion(JS::TypeError::create(realm(), "Only HTTP(S) requests can be stored in a Cache"sv)));
    if (response->response()->type() == Fetch::Infrastructure::Response::Type::Error || response->status() == 206)
        return WebIDL::create_rejected_promise_from_exception(realm(), JS::throw_completion(JS::TypeError::create(realm(), "This response cannot be stored in a Cache"sv)));

    auto entry = clone_entry(request, *response);
    if (entry.is_exception())
        return WebIDL::create_rejected_promise_from_exception(realm(), entry.release_error());
    auto persistence_response = response->clone();
    if (persistence_response.is_exception())
        return WebIDL::create_rejected_promise_from_exception(realm(), persistence_response.release_error());
    return persist_entry(entry.release_value(), persistence_response.release_value());
}

WebIDL::ExceptionOr<Cache::Entry> Cache::clone_entry(GC::Ref<Fetch::Request> request, Fetch::Response const& response) const
{
    auto request_clone = request->clone();
    if (request_clone.is_exception())
        return request_clone.release_error();
    auto response_clone = response.clone();
    if (response_clone.is_exception())
        return response_clone.release_error();
    return Entry { 0, request_clone.release_value(), response_clone.release_value() };
}

void Cache::commit_entry(Entry entry)
{
    auto existing = m_entries.find_if([&](auto const& candidate) {
        return candidate.request->method() == entry.request->method() && candidate.request->url() == entry.request->url();
    });
    if (existing != m_entries.end())
        *existing = move(entry);
    else
        m_entries.append(move(entry));
}

GC::Ref<WebIDL::Promise> Cache::delete_(Fetch::RequestInfo const& input, CacheQueryOptions const& options)
{
    if (!owner_is_current())
        return owner_rejected_promise();
    auto request = normalize_request(input);
    if (request.is_exception())
        return WebIDL::create_rejected_promise_from_exception(realm(), request.release_error());
    Vector<String> removed_keys;
    auto removed = m_entries.remove_all_matching([&](auto const& entry) {
        if (!matches(entry, *request.value(), options))
            return false;
        removed_keys.append(storage_key_for(*entry.request));
        return true;
    });
    if (m_storage_bottle) {
        for (auto const& key : removed_keys)
            m_storage_bottle->remove(key);
    }
    return WebIDL::create_resolved_promise(realm(), JS::Value(removed));
}

GC::Ref<WebIDL::Promise> Cache::keys(Optional<Fetch::RequestInfo> const& input, CacheQueryOptions const& options)
{
    if (!owner_is_current())
        return owner_rejected_promise();
    Optional<GC::Ref<Fetch::Request>> request;
    if (input.has_value()) {
        auto normalized = normalize_request(input.value());
        if (normalized.is_exception())
            return WebIDL::create_rejected_promise_from_exception(realm(), normalized.release_error());
        request = normalized.release_value();
    }

    GC::RootVector<JS::Value> requests(realm().heap());
    for (auto const& entry : m_entries) {
        if (request.has_value() && !matches(entry, *request, options))
            continue;
        auto clone = entry.request->clone();
        if (clone.is_exception())
            return WebIDL::create_rejected_promise_from_exception(realm(), clone.release_error());
        requests.append(clone.release_value());
    }
    return WebIDL::create_resolved_promise(realm(), JS::Array::create_from(realm(), requests));
}

GC::Ref<Cache> Cache::create(JS::Realm& realm, String name, GC::Ptr<StorageAPI::StorageBottle> storage_bottle,
                             GC::Ptr<Page> page, ByteString owner_origin, u64 owner_generation)
{
    return realm.create<Cache>(realm, move(name), storage_bottle, page, move(owner_origin), owner_generation);
}

}
