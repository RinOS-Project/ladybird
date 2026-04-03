/*
 * Copyright (c) 2026, RinOS contributors
 *
 * SPDX-License-Identifier: BSD-2-Clause
 */

#include <AK/TypeCasts.h>
#include <LibWeb/Gamepad/Gamepad.h>
#include <LibWeb/Gamepad/NavigatorGamepad.h>
#include <LibWeb/HTML/Navigator.h>

namespace Web::Gamepad {

WebIDL::ExceptionOr<GC::RootVector<GC::Ptr<Gamepad>>> NavigatorGamepadPartial::get_gamepads()
{
    auto& navigator = as<HTML::Navigator>(*this);
    GC::RootVector<GC::Ptr<Gamepad>> gamepads { navigator.realm().heap() };
    for (auto gamepad : m_gamepads)
        gamepads.append(gamepad);
    return gamepads;
}

void NavigatorGamepadPartial::visit_edges(GC::Cell::Visitor& visitor)
{
    visitor.visit(m_gamepads);
}

size_t NavigatorGamepadPartial::select_an_unused_gamepad_index(Badge<Gamepad>)
{
    for (size_t gamepad_index = 0; gamepad_index < m_gamepads.size(); ++gamepad_index) {
        if (!m_gamepads[gamepad_index])
            return gamepad_index;
    }

    m_gamepads.append(nullptr);
    return m_gamepads.size() - 1;
}

void NavigatorGamepadPartial::handle_gamepad_connected(SDL_JoystickID)
{
}

void NavigatorGamepadPartial::handle_gamepad_updated(Badge<EventHandler>, SDL_JoystickID)
{
}

void NavigatorGamepadPartial::handle_gamepad_disconnected(Badge<EventHandler>, SDL_JoystickID)
{
}

void NavigatorGamepadPartial::check_for_connected_gamepads()
{
}

void NavigatorGamepadPartial::set_has_gamepad_gesture(Badge<Gamepad>, bool value)
{
    m_has_gamepad_gesture = value;
}

GC::RootVector<GC::Ptr<Gamepad>> NavigatorGamepadPartial::gamepads(Badge<Gamepad>) const
{
    auto& navigator = as<HTML::Navigator>(*this);
    GC::RootVector<GC::Ptr<Gamepad>> gamepads { navigator.realm().heap() };
    for (auto gamepad : m_gamepads)
        gamepads.append(gamepad);
    return gamepads;
}

}
