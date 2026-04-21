/*
 * Copyright (c) 2026, OpenAI
 *
 * SPDX-License-Identifier: BSD-2-Clause
 */

#include <LibTest/TestCase.h>

#include <LibWebView/Menu.h>

TEST_CASE(group_menu_inline_checkable_actions_can_toggle)
{
    auto menu = WebView::Menu::create_group("Group"sv);
    menu->add_action(WebView::Action::create_checkable("First"sv, WebView::ActionID::PreferredMotion, [] {}));
    menu->add_action(WebView::Action::create_checkable("Second"sv, WebView::ActionID::PreferredMotion, [] {}));

    EXPECT_EQ(menu->size(), 2u);

    auto& first = *menu->items()[0].get<NonnullRefPtr<WebView::Action>>();
    auto& second = *menu->items()[1].get<NonnullRefPtr<WebView::Action>>();

    first.set_checked(true);
    EXPECT_EQ(first.checked(), true);
    EXPECT_EQ(second.checked(), false);

    second.set_checked(true);
    EXPECT_EQ(first.checked(), false);
    EXPECT_EQ(second.checked(), true);
}

TEST_CASE(group_menu_action_survives_group_destruction)
{
    auto action = WebView::Action::create_checkable("Persisted"sv, WebView::ActionID::PreferredMotion, [] {});

    {
        auto menu = WebView::Menu::create_group("Group"sv);
        menu->add_action(action);
        EXPECT_EQ(action->checked(), false);
    }

    action->set_checked(true);
    EXPECT_EQ(action->checked(), true);
}

TEST_CASE(normal_menu_inline_action_is_retained)
{
    bool activated = false;

    auto menu = WebView::Menu::create("Menu"sv);
    menu->add_action(WebView::Action::create("Run"sv, WebView::ActionID::DumpDOMTree, [&] {
        activated = true;
    }));

    EXPECT_EQ(menu->size(), 1u);

    auto& action = *menu->items().first().get<NonnullRefPtr<WebView::Action>>();
    EXPECT_EQ(action.text(), "Run"sv);
    action.activate();

    EXPECT_EQ(activated, true);
}
