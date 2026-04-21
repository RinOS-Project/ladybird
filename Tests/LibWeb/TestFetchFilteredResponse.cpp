/*
 * Copyright (c) 2026, OpenAI
 *
 * SPDX-License-Identifier: BSD-2-Clause
 */

#include <LibTest/TestCase.h>

#include <LibGC/Root.h>
#include <LibJS/Runtime/VM.h>
#include <LibURL/Parser.h>
#include <LibWeb/Bindings/MainThreadVM.h>
#include <LibWeb/Fetch/Infrastructure/HTTP/Requests.h>
#include <LibWeb/Fetch/Infrastructure/HTTP/Responses.h>

namespace {

JS::VM& fetch_test_vm()
{
    static JS::VM* vm = [] {
        Web::Bindings::initialize_main_thread_vm(Web::Bindings::AgentType::SimilarOriginWindow);
        return &Web::Bindings::main_thread_vm();
    }();
    return *vm;
}

URL::URL must_parse_url(StringView input)
{
    auto url = URL::Parser::basic_parse(input);
    VERIFY(url.has_value());
    return url.release_value();
}

}

TEST_CASE(filtered_responses_retain_internal_response)
{
    auto& vm = fetch_test_vm();
    auto response = Web::Fetch::Infrastructure::Response::create(vm);
    response->set_status(201);

    auto initial_url = must_parse_url("https://example.com/original"sv);
    response->url_list().append(initial_url);

    auto basic = Web::Fetch::Infrastructure::BasicFilteredResponse::create(vm, response);
    auto cors = Web::Fetch::Infrastructure::CORSFilteredResponse::create(vm, response);
    auto opaque = Web::Fetch::Infrastructure::OpaqueFilteredResponse::create(vm, response);

    EXPECT_EQ(basic->internal_response().ptr(), response.ptr());
    EXPECT_EQ(basic->unsafe_response().ptr(), response.ptr());
    EXPECT_EQ(cors->internal_response().ptr(), response.ptr());
    EXPECT_EQ(cors->unsafe_response().ptr(), response.ptr());
    EXPECT_EQ(opaque->internal_response().ptr(), response.ptr());
    EXPECT_EQ(opaque->unsafe_response().ptr(), response.ptr());

    auto updated_url = must_parse_url("https://example.com/updated"sv);
    basic->internal_response()->url_list().append(updated_url);
    EXPECT_EQ(basic->url_list().last().serialize(), updated_url.serialize());
}

TEST_CASE(main_fetch_callback_shape_keeps_internal_response_alive_through_filtering)
{
    auto& vm = fetch_test_vm();
    auto request = Web::Fetch::Infrastructure::Request::create(vm);
    request->set_response_tainting(Web::Fetch::Infrastructure::Request::ResponseTainting::Opaque);
    request->url_list().append(must_parse_url("https://example.com/about"sv));

    auto response = Web::Fetch::Infrastructure::Response::create(vm);
    auto internal_response = GC::make_root(response);

    response = Web::Fetch::Infrastructure::OpaqueFilteredResponse::create(vm, *internal_response);

    if (internal_response->url_list().is_empty())
        internal_response->set_url_list(request->url_list());

    internal_response->set_redirect_taint(request->redirect_taint());

    if (!request->timing_allow_failed())
        internal_response->set_timing_allow_passed(true);

    EXPECT_EQ(response->type(), Web::Fetch::Infrastructure::Response::Type::Opaque);
    EXPECT_EQ(response->unsafe_response().ptr(), internal_response.ptr());
    EXPECT_EQ(internal_response->url_list().size(), request->url_list().size());
    EXPECT_EQ(internal_response->url_list().first().serialize(), request->url_list().first().serialize());
    EXPECT_EQ(internal_response->redirect_taint(), request->redirect_taint());
    EXPECT(internal_response->timing_allow_passed());
}
