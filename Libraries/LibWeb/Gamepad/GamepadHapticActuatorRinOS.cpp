/*
 * Copyright (c) 2026, RinOS contributors
 *
 * SPDX-License-Identifier: BSD-2-Clause
 */

#include <LibJS/Runtime/Value.h>
#include <LibWeb/Bindings/Intrinsics.h>
#include <LibWeb/DOM/DocumentObserver.h>
#include <LibWeb/Gamepad/Gamepad.h>
#include <LibWeb/Gamepad/GamepadHapticActuator.h>
#include <LibWeb/HTML/Window.h>
#include <LibWeb/Platform/Timer.h>
#include <LibWeb/WebIDL/Promise.h>

namespace Web::Gamepad {

GC_DEFINE_ALLOCATOR(GamepadHapticActuator);

GC::Ref<GamepadHapticActuator> GamepadHapticActuator::create(JS::Realm& realm, GC::Ref<Gamepad> gamepad)
{
    auto& window = as<HTML::Window>(realm.global_object());
    auto document_became_hidden_observer = realm.create<DOM::DocumentObserver>(realm, window.associated_document());
    return realm.create<GamepadHapticActuator>(realm, gamepad, document_became_hidden_observer);
}

GamepadHapticActuator::GamepadHapticActuator(JS::Realm& realm, GC::Ref<Gamepad> gamepad, GC::Ref<DOM::DocumentObserver> document_became_hidden_observer)
    : Bindings::PlatformObject(realm)
    , m_gamepad(gamepad)
    , m_document_became_hidden_observer(document_became_hidden_observer)
{
    m_document_became_hidden_observer->set_document_visibility_state_observer([this](HTML::VisibilityState visibility_state) {
        if (visibility_state == HTML::VisibilityState::Hidden)
            document_became_hidden();
    });
}

GamepadHapticActuator::~GamepadHapticActuator() = default;

void GamepadHapticActuator::initialize(JS::Realm& realm)
{
    WEB_SET_PROTOTYPE_FOR_INTERFACE(GamepadHapticActuator);
    Base::initialize(realm);
}

void GamepadHapticActuator::visit_edges(Cell::Visitor& visitor)
{
    Base::visit_edges(visitor);
    visitor.visit(m_gamepad);
    visitor.visit(m_document_became_hidden_observer);
    visitor.visit(m_playing_effect_promise);
    visitor.visit(m_playing_effect_timer);
}

GC::Ref<WebIDL::Promise> GamepadHapticActuator::play_effect(Bindings::GamepadHapticEffectType, GamepadEffectParameters const&)
{
    auto& realm = this->realm();
    return WebIDL::create_rejected_promise_from_exception(realm, WebIDL::NotSupportedError::create(realm, "Gamepad haptics are disabled on RinOS"_utf16));
}

GC::Ref<WebIDL::Promise> GamepadHapticActuator::reset()
{
    clear_playing_effect_timers();
    m_playing_effect_promise = nullptr;
    return WebIDL::create_resolved_promise(realm(), JS::js_undefined());
}

void GamepadHapticActuator::document_became_hidden()
{
    clear_playing_effect_timers();
    m_playing_effect_promise = nullptr;
}

void GamepadHapticActuator::issue_haptic_effect(Bindings::GamepadHapticEffectType, GamepadEffectParameters const&, GC::Ref<GC::Function<void()>> on_complete)
{
    on_complete->function()();
}

bool GamepadHapticActuator::stop_haptic_effects()
{
    clear_playing_effect_timers();
    return true;
}

void GamepadHapticActuator::clear_playing_effect_timers()
{
    if (m_playing_effect_timer) {
        m_playing_effect_timer->stop();
        m_playing_effect_timer = nullptr;
    }
}

}
