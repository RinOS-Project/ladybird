/*
 * Copyright (c) 2024, Jelle Raaijmakers <jelle@ladybird.org>
 *
 * SPDX-License-Identifier: BSD-2-Clause
 */

#include <LibWeb/Bindings/Intrinsics.h>
#include <LibWeb/Bindings/MediaSourcePrototype.h>
#include <LibWeb/MediaSourceExtensions/EventNames.h>
#include <LibWeb/MediaSourceExtensions/MediaSource.h>
#include <LibWeb/MimeSniff/MimeType.h>

namespace Web::MediaSourceExtensions {

GC_DEFINE_ALLOCATOR(MediaSource);

WebIDL::ExceptionOr<GC::Ref<MediaSource>> MediaSource::construct_impl(JS::Realm& realm)
{
    return realm.create<MediaSource>(realm);
}

MediaSource::MediaSource(JS::Realm& realm)
    : DOM::EventTarget(realm)
{
}

MediaSource::~MediaSource() = default;

void MediaSource::initialize(JS::Realm& realm)
{
    WEB_SET_PROTOTYPE_FOR_INTERFACE(MediaSource);
    Base::initialize(realm);
}

// https://w3c.github.io/media-source/#dom-mediasource-onsourceopen
void MediaSource::set_onsourceopen(GC::Ptr<WebIDL::CallbackType> event_handler)
{
    set_event_handler_attribute(EventNames::sourceopen, event_handler);
}

// https://w3c.github.io/media-source/#dom-mediasource-onsourceopen
GC::Ptr<WebIDL::CallbackType> MediaSource::onsourceopen()
{
    return event_handler_attribute(EventNames::sourceopen);
}

// https://w3c.github.io/media-source/#dom-mediasource-onsourceended
void MediaSource::set_onsourceended(GC::Ptr<WebIDL::CallbackType> event_handler)
{
    set_event_handler_attribute(EventNames::sourceended, event_handler);
}

// https://w3c.github.io/media-source/#dom-mediasource-onsourceended
GC::Ptr<WebIDL::CallbackType> MediaSource::onsourceended()
{
    return event_handler_attribute(EventNames::sourceended);
}

// https://w3c.github.io/media-source/#dom-mediasource-onsourceclose
void MediaSource::set_onsourceclose(GC::Ptr<WebIDL::CallbackType> event_handler)
{
    set_event_handler_attribute(EventNames::sourceclose, event_handler);
}

// https://w3c.github.io/media-source/#dom-mediasource-onsourceclose
GC::Ptr<WebIDL::CallbackType> MediaSource::onsourceclose()
{
    return event_handler_attribute(EventNames::sourceclose);
}

// https://w3c.github.io/media-source/#dom-mediasource-istypesupported
bool MediaSource::is_type_supported(JS::VM&, String const& type)
{
    // 1. If type is an empty string, then return false.
    if (type.is_empty())
        return false;

    // 2. If type does not contain a valid MIME type string, then return false.
    auto mime_type = MimeSniff::MimeType::parse(type);
    if (!mime_type.has_value())
        return false;

    // No SourceBuffer parser/decoder capability is connected yet. A syntactically
    // valid MIME type must not be advertised as supported without that owner.
    return false;
}

}
