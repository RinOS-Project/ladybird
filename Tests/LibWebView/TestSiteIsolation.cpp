/*
 * Copyright (c) 2026, the RinOS developers.
 *
 * SPDX-License-Identifier: BSD-2-Clause
 */

#include <LibTest/TestCase.h>
#include <LibURL/Parser.h>
#include <LibURL/URL.h>
#include <LibWebView/SiteIsolation.h>

TEST_CASE(cross_site_http_navigation_requires_a_fresh_webcontent_process)
{
    auto current = URL::Parser::basic_parse("https://first.example.com/document"sv);
    auto same_site = URL::Parser::basic_parse("https://subdomain.example.com/next"sv);
    auto cross_site = URL::Parser::basic_parse("https://second.example.net/document"sv);

    ASSERT(current.has_value());
    ASSERT(same_site.has_value());
    ASSERT(cross_site.has_value());
    EXPECT(WebView::is_url_suitable_for_same_process_navigation(*current, *same_site));
    EXPECT(!WebView::is_url_suitable_for_same_process_navigation(*current, *cross_site));
}

TEST_CASE(http_and_non_http_navigation_requires_a_fresh_webcontent_process)
{
    auto current = URL::Parser::basic_parse("https://first.example.com/document"sv);
    auto local = URL::Parser::basic_parse("file:///tmp/document.html"sv);

    ASSERT(current.has_value());
    ASSERT(local.has_value());
    EXPECT(!WebView::is_url_suitable_for_same_process_navigation(*current, *local));
}
