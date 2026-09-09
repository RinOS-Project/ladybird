/*
 * Copyright (c) 2026, RinOS contributors
 *
 * SPDX-License-Identifier: BSD-2-Clause
 */

#include <LibWeb/Bindings/CachePrototype.h>
#include <LibWeb/Bindings/Intrinsics.h>
#include <LibJS/Runtime/Array.h>
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

GC::Ref<WebIDL::Promise> Cache::put(Fetch::RequestInfo const& input, GC::Root<Fetch::Response> const& response)
{
    auto request = normalize_request(input);
    if (request.is_exception())
        return WebIDL::create_rejected_promise_from_exception(realm(), request.release_error());
    if (request.value()->method() != "GET"_string)
        return WebIDL::create_rejected_promise_from_exception(realm(), JS::TypeError::create(realm(), "Only GET requests can be stored in a Cache"sv));
    if (response->type() == Bindings::ResponseType::Error)
        return WebIDL::create_rejected_promise_from_exception(realm(), JS::TypeError::create(realm(), "A network error response cannot be stored in a Cache"sv));

    auto request_clone = request.value()->clone();
    if (request_clone.is_exception())
        return WebIDL::create_rejected_promise_from_exception(realm(), request_clone.release_error());
    auto response_clone = response->clone();
    if (response_clone.is_exception())
        return WebIDL::create_rejected_promise_from_exception(realm(), response_clone.release_error());

    auto existing = m_entries.find_if([&](auto const& entry) {
        return entry.request->method() == request_clone.value()->method() && entry.request->url() == request_clone.value()->url();
    });
    Entry entry { request_clone.release_value(), response_clone.release_value() };
    if (existing != m_entries.end())
        *existing = move(entry);
    else
        m_entries.append(move(entry));
    return WebIDL::create_resolved_promise(realm(), JS::js_undefined());
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
