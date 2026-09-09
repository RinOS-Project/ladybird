/*
 * Copyright (c) 2026, RinOS contributors
 *
 * SPDX-License-Identifier: BSD-2-Clause
 */

#include <AK/TypeCasts.h>
#include <LibJS/Runtime/Array.h>
#include <LibWeb/Bindings/CachePrototype.h>
#include <LibWeb/Bindings/Intrinsics.h>
#include <LibWeb/Fetch/FetchMethod.h>
#include <LibWeb/Fetch/Request.h>
#include <LibWeb/Fetch/Response.h>
#include <LibWeb/ServiceWorker/Cache.h>
#include <LibWeb/WebIDL/ExceptionOr.h>

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

void Cache::visit_edges(Cell::Visitor& visitor)
{
    Base::visit_edges(visitor);
    for (auto& entry : m_entries) {
        visitor.visit(entry.request);
        visitor.visit(entry.response);
    }
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
    auto question_mark = url.find('?');
    if (!question_mark.has_value())
        return url;
    return url.substring(0, question_mark.value());
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
    Optional<GC::Ref<Fetch::Request>> request;
    if (input.has_value()) {
        auto normalized = normalize_request(input.value());
        if (normalized.is_exception())
            return WebIDL::create_rejected_promise_from_exception(realm(), normalized.release_error());
        request = normalized.release_value();
    }

    Vector<GC::Ref<Fetch::Response>> responses;
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
    auto request = normalize_request(input);
    if (request.is_exception())
        return WebIDL::create_rejected_promise_from_exception(realm(), request.release_error());

    // Cache.add() is defined only for GET requests.  Reject before starting
    // the fetch so a caller cannot trigger an external request that can never
    // be committed to this cache.
    if (request.value()->method() != "GET"_string)
        return WebIDL::create_rejected_promise_from_exception(realm(), JS::TypeError::create(realm(), "Only GET requests can be added to a Cache"sv));

    auto promise = WebIDL::create_promise(realm());
    auto fetch_promise = Fetch::fetch(realm().vm(), input);
    WebIDL::react_to_promise(
        *fetch_promise,
        GC::create_function(realm().heap(), [this, promise, request = request.release_value()](JS::Value value) -> WebIDL::ExceptionOr<JS::Value> {
            if (!value.is<Fetch::Response>()) {
                WebIDL::reject_promise(realm(), promise, JS::TypeError::create(realm(), "Fetch did not produce a Response"sv));
                return JS::js_undefined();
            }

            GC::Ref<Fetch::Response> response = as<Fetch::Response>(value.as_object());
            if (!response->ok()) {
                WebIDL::reject_promise(realm(), promise, JS::TypeError::create(realm(), "Cache.add() received a non-success response"sv));
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
            return WebIDL::create_rejected_promise_from_exception(realm(), JS::TypeError::create(realm(), "Only GET requests can be added to a Cache"sv));
        requests.append(request.release_value());
    }

    // Start network work only after the complete input list has been
    // normalized and admitted.  A malformed or non-GET later entry therefore
    // cannot leave earlier entries with an in-flight fetch.
    for (auto const& input : inputs)
        fetch_promises.append(Fetch::fetch(realm().vm(), input));

    WebIDL::wait_for_all(
        realm(),
        fetch_promises,
        [this, promise, requests = move(requests)](Vector<JS::Value> const& values) mutable {
            Vector<Entry> staged_entries;
            staged_entries.ensure_capacity(values.size());
            for (size_t i = 0; i < values.size(); ++i) {
                if (!values[i].is<Fetch::Response>()) {
                    WebIDL::reject_promise(realm(), promise, JS::TypeError::create(realm(), "Cache.addAll() fetch did not produce a Response"sv));
                    return;
                }
                GC::Ref<Fetch::Response> response = as<Fetch::Response>(values[i].as_object());
                if (!response->ok()) {
                    WebIDL::reject_promise(realm(), promise, JS::TypeError::create(realm(), "Cache.addAll() received a non-success response"sv));
                    return;
                }
                auto staged = clone_entry(requests[i], response);
                if (staged.is_exception()) {
                    WebIDL::reject_promise(realm(), promise, staged.release_error());
                    return;
                }
                staged_entries.append(staged.release_value());
            }
            // Publish the complete batch only after every request/response
            // clone succeeded. A failed clone therefore cannot leave a
            // partially committed addAll() result in the cache.
            m_entries.ensure_capacity(m_entries.size() + staged_entries.size());
            for (auto& entry : staged_entries)
                commit_entry(move(entry));
            WebIDL::resolve_promise(realm(), promise);
        },
        [this, promise](JS::Value reason) {
            WebIDL::reject_promise(realm(), promise, reason);
        });
    return promise;
}

GC::Ref<WebIDL::Promise> Cache::put(Fetch::RequestInfo const& input, GC::Root<Fetch::Response> const& response)
{
    auto request = normalize_request(input);
    if (request.is_exception())
        return WebIDL::create_rejected_promise_from_exception(realm(), request.release_error());
    return put_normalized(request.release_value(), *response);
}

GC::Ref<WebIDL::Promise> Cache::put_normalized(GC::Ref<Fetch::Request> request, GC::Ref<Fetch::Response> response)
{
    if (request->method() != "GET"_string)
        return WebIDL::create_rejected_promise_from_exception(realm(), JS::TypeError::create(realm(), "Only GET requests can be stored in a Cache"sv));
    if (response->type() == Bindings::ResponseType::Error)
        return WebIDL::create_rejected_promise_from_exception(realm(), JS::TypeError::create(realm(), "A network error response cannot be stored in a Cache"sv));

    auto entry = clone_entry(request, response);
    if (entry.is_exception())
        return WebIDL::create_rejected_promise_from_exception(realm(), entry.release_error());
    commit_entry(entry.release_value());
    return WebIDL::create_resolved_promise(realm(), JS::js_undefined());
}

WebIDL::ExceptionOr<Cache::Entry> Cache::clone_entry(GC::Ref<Fetch::Request> request, GC::Ref<Fetch::Response> response) const
{
    auto request_clone = request->clone();
    if (request_clone.is_exception())
        return request_clone.release_error();
    auto response_clone = response->clone();
    if (response_clone.is_exception())
        return response_clone.release_error();
    return Entry { request_clone.release_value(), response_clone.release_value() };
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
    auto request = normalize_request(input);
    if (request.is_exception())
        return WebIDL::create_rejected_promise_from_exception(realm(), request.release_error());
    auto it = m_entries.find_if([&](auto const& entry) { return matches(entry, *request.value(), options); });
    if (it == m_entries.end())
        return WebIDL::create_resolved_promise(realm(), JS::Value(false));
    m_entries.remove(it);
    return WebIDL::create_resolved_promise(realm(), JS::Value(true));
}

GC::Ref<WebIDL::Promise> Cache::keys(Optional<Fetch::RequestInfo> const& input, CacheQueryOptions const& options)
{
    Optional<GC::Ref<Fetch::Request>> request;
    if (input.has_value()) {
        auto normalized = normalize_request(input.value());
        if (normalized.is_exception())
            return WebIDL::create_rejected_promise_from_exception(realm(), normalized.release_error());
        request = normalized.release_value();
    }

    Vector<GC::Ref<Fetch::Request>> requests;
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

GC::Ref<Cache> Cache::create(JS::Realm& realm, String name)
{
    return realm.create<Cache>(realm, move(name));
}

}
