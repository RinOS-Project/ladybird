/*
 * Copyright (c) 2024, Shannon Booth <shannon@serenityos.org>
 *
 * SPDX-License-Identifier: BSD-2-Clause
 */

#include <LibWeb/Bindings/Intrinsics.h>
#include <LibWeb/Bindings/ScreenOrientationPrototype.h>
#include <LibWeb/CSS/ScreenOrientation.h>
#include <LibWeb/CSS/Screen.h>
#include <LibWeb/HTML/EventNames.h>

namespace Web::CSS {

GC_DEFINE_ALLOCATOR(ScreenOrientation);

ScreenOrientation::ScreenOrientation(Screen& screen)
    : DOM::EventTarget(screen.realm())
    , m_screen(screen)
{
}

void ScreenOrientation::initialize(JS::Realm& realm)
{
    WEB_SET_PROTOTYPE_FOR_INTERFACE(ScreenOrientation);
    Base::initialize(realm);
}

GC::Ref<ScreenOrientation> ScreenOrientation::create(Screen& screen)
{
    return screen.realm().create<ScreenOrientation>(screen);
}

void ScreenOrientation::visit_edges(Cell::Visitor& visitor)
{
    Base::visit_edges(visitor);
    visitor.visit(m_screen);
}

// https://w3c.github.io/screen-orientation/#lock-method
WebIDL::ExceptionOr<GC::Ref<WebIDL::Promise>> ScreenOrientation::lock(Bindings::OrientationLockType)
{
    return WebIDL::NotSupportedError::create(realm(), "FIXME: ScreenOrientation::lock() is not implemented"_utf16);
}

// https://w3c.github.io/screen-orientation/#unlock-method
void ScreenOrientation::unlock()
{
    // The lock() operation remains unavailable until a platform orientation
    // owner is connected. There is therefore no pending lock to cancel here;
    // unlock() is intentionally an idempotent no-op.
}

// https://w3c.github.io/screen-orientation/#type-attribute
Bindings::OrientationType ScreenOrientation::type() const
{
    // The Web-exposed screen area is the only orientation source currently
    // available to LibWeb. Without a rotation sensor, the primary orientation
    // is derived from its bounded CSS dimensions.
    auto width = m_screen->width();
    auto height = m_screen->height();
    if (height > width)
        return Bindings::OrientationType::PortraitPrimary;
    return Bindings::OrientationType::LandscapePrimary;
}

// https://w3c.github.io/screen-orientation/#angle-attribute
WebIDL::UnsignedShort ScreenOrientation::angle() const
{
    // No platform rotation sensor is connected yet. A primary orientation is
    // therefore always exposed with the spec-defined zero-degree angle.
    return 0;
}

// https://w3c.github.io/screen-orientation/#onchange-event-handler-attribute
void ScreenOrientation::set_onchange(GC::Ptr<WebIDL::CallbackType> event_handler)
{
    set_event_handler_attribute(HTML::EventNames::change, event_handler);
}

// https://w3c.github.io/screen-orientation/#onchange-event-handler-attribute
GC::Ptr<WebIDL::CallbackType> ScreenOrientation::onchange()
{
    return event_handler_attribute(HTML::EventNames::change);
}

}
