/*
 * Copyright (c) 2026, RinOS contributors
 *
 * SPDX-License-Identifier: BSD-2-Clause
 */

#include <LibWeb/Bindings/InternalGamepadPrototype.h>
#include <LibWeb/Bindings/Intrinsics.h>
#include <LibWeb/Internals/InternalGamepad.h>
#include <LibWeb/Internals/Internals.h>

namespace Web::Internals {

GC_DEFINE_ALLOCATOR(InternalGamepad);

static constexpr Array<i32, 15> BUTTONS {};
static constexpr Array<i32, 4> AXES {};
static constexpr Array<i32, 2> TRIGGERS {};

InternalGamepad::InternalGamepad(JS::Realm& realm, GC::Ref<Internals> internals)
    : Bindings::PlatformObject(realm)
    , m_sdl_joystick_id(0)
    , m_sdl_joystick(nullptr)
    , m_internals(internals)
{
}

InternalGamepad::~InternalGamepad() = default;

void InternalGamepad::initialize(JS::Realm& realm)
{
    WEB_SET_PROTOTYPE_FOR_INTERFACE(InternalGamepad);
    Base::initialize(realm);
}

void InternalGamepad::visit_edges(Cell::Visitor& visitor)
{
    Base::visit_edges(visitor);
    visitor.visit(m_received_rumble_effects);
    visitor.visit(m_received_rumble_trigger_effects);
    visitor.visit(m_internals);
}

void InternalGamepad::finalize()
{
    Base::finalize();
    disconnect();
}

Array<i32, 15> const& InternalGamepad::buttons()
{
    return BUTTONS;
}

Array<i32, 4> const& InternalGamepad::axes()
{
    return AXES;
}

Array<i32, 2> const& InternalGamepad::triggers()
{
    return TRIGGERS;
}

void InternalGamepad::set_button(int, bool)
{
}

void InternalGamepad::set_axis(int, short)
{
}

GC::RootVector<JS::Object*> InternalGamepad::get_received_rumble_effects() const
{
    GC::RootVector<JS::Object*> received_rumble_effects { realm().heap() };
    for (auto const received_rumble_effect : m_received_rumble_effects)
        received_rumble_effects.append(received_rumble_effect);
    return received_rumble_effects;
}

GC::RootVector<JS::Object*> InternalGamepad::get_received_rumble_trigger_effects() const
{
    GC::RootVector<JS::Object*> received_rumble_trigger_effects { realm().heap() };
    for (auto const received_rumble_trigger_effect : m_received_rumble_trigger_effects)
        received_rumble_trigger_effects.append(received_rumble_trigger_effect);
    return received_rumble_trigger_effects;
}

void InternalGamepad::received_rumble(u16 low_frequency_rumble, u16 high_frequency_rumble)
{
    auto object = JS::Object::create(realm(), nullptr);
    object->define_direct_property("lowFrequencyRumble"_utf16, JS::Value(low_frequency_rumble), JS::default_attributes);
    object->define_direct_property("highFrequencyRumble"_utf16, JS::Value(high_frequency_rumble), JS::default_attributes);
    m_received_rumble_effects.append(object);
}

void InternalGamepad::received_rumble_triggers(u16 left_rumble, u16 right_rumble)
{
    auto object = JS::Object::create(realm(), nullptr);
    object->define_direct_property("leftRumble"_utf16, JS::Value(left_rumble), JS::default_attributes);
    object->define_direct_property("rightRumble"_utf16, JS::Value(right_rumble), JS::default_attributes);
    m_received_rumble_trigger_effects.append(object);
}

void InternalGamepad::disconnect()
{
    m_internals->disconnect_virtual_gamepad(*this);
}

}
