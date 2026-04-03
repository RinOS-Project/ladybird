/*
 * Copyright (c) 2026, RinOS contributors
 *
 * SPDX-License-Identifier: BSD-2-Clause
 */

#include <AK/StringView.h>
#include <LibWeb/Bindings/Intrinsics.h>
#include <LibWeb/Gamepad/Gamepad.h>
#include <LibWeb/Gamepad/GamepadButton.h>
#include <LibWeb/Gamepad/GamepadHapticActuator.h>
#include <LibWeb/HTML/Navigator.h>
#include <LibWeb/HTML/Scripting/Environments.h>
#include <LibWeb/HTML/Window.h>
#include <LibWeb/HighResolutionTime/TimeOrigin.h>

namespace Web::Gamepad {

GC_DEFINE_ALLOCATOR(Gamepad);

GC::Ref<Gamepad> Gamepad::create(JS::Realm& realm, SDL_JoystickID sdl_joystick_id)
{
    auto gamepad = realm.create<Gamepad>(realm, sdl_joystick_id);
    gamepad->m_id = Utf16String::from_utf8("RinOS disabled gamepad backend"sv);

    auto& window = as<HTML::Window>(HTML::relevant_global_object(gamepad));
    gamepad->m_index = window.navigator()->select_an_unused_gamepad_index({});
    gamepad->m_connected = true;
    gamepad->m_timestamp = HighResolutionTime::current_high_resolution_time(HTML::relevant_global_object(gamepad));
    gamepad->m_mapping = Bindings::GamepadMappingType::Standard;
    gamepad->m_vibration_actuator = GamepadHapticActuator::create(realm, gamepad);

    return gamepad;
}

Gamepad::Gamepad(JS::Realm& realm, SDL_JoystickID sdl_joystick_id)
    : PlatformObject(realm)
    , m_sdl_joystick_id(sdl_joystick_id)
{
}

void Gamepad::initialize(JS::Realm& realm)
{
    WEB_SET_PROTOTYPE_FOR_INTERFACE(Gamepad);
    Base::initialize(realm);
}

void Gamepad::visit_edges(Cell::Visitor& visitor)
{
    Base::visit_edges(visitor);
    visitor.visit(m_buttons);
    visitor.visit(m_vibration_actuator);
}

void Gamepad::finalize()
{
    Base::finalize();
}

void Gamepad::select_a_mapping()
{
    m_mapping = Bindings::GamepadMappingType::Standard;
}

void Gamepad::initialize_axes()
{
}

void Gamepad::initialize_buttons()
{
}

void Gamepad::map_and_normalize_axes()
{
}

void Gamepad::map_and_normalize_buttons()
{
}

bool Gamepad::contains_gamepad_user_gesture()
{
    return false;
}

GC::Ref<GamepadHapticActuator> Gamepad::vibration_actuator() const
{
    VERIFY(m_vibration_actuator);
    return *m_vibration_actuator;
}

void Gamepad::set_connected(Badge<NavigatorGamepadPartial>, bool value)
{
    m_connected = value;
}

void Gamepad::set_exposed(Badge<NavigatorGamepadPartial>, bool value)
{
    m_exposed = value;
}

void Gamepad::set_timestamp(Badge<NavigatorGamepadPartial>, HighResolutionTime::DOMHighResTimeStamp value)
{
    m_timestamp = value;
}

void Gamepad::update_gamepad_state(Badge<NavigatorGamepadPartial>)
{
    m_timestamp = HighResolutionTime::current_high_resolution_time(HTML::relevant_global_object(*this));
}

}
