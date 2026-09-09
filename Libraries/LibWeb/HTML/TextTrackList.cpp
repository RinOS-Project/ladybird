/*
 * Copyright (c) 2024, Jamie Mansfield <jmansfield@cadixdev.org>
 *
 * SPDX-License-Identifier: BSD-2-Clause
 */

#include <LibJS/Runtime/Realm.h>
#include <LibWeb/Bindings/Intrinsics.h>
#include <LibWeb/Bindings/TextTrackListPrototype.h>
#include <LibWeb/HTML/EventNames.h>
#include <LibWeb/HTML/HTMLMediaElement.h>
#include <LibWeb/HTML/TextTrackList.h>

namespace Web::HTML {

GC_DEFINE_ALLOCATOR(TextTrackList);

GC::Ref<TextTrackList> TextTrackList::create(JS::Realm& realm)
{
    return realm.create<TextTrackList>(realm);
}

TextTrackList::TextTrackList(JS::Realm& realm)
    : DOM::EventTarget(realm, MayInterfereWithIndexedPropertyAccess::Yes)
{
}

TextTrackList::~TextTrackList() = default;

void TextTrackList::initialize(JS::Realm& realm)
{
    WEB_SET_PROTOTYPE_FOR_INTERFACE(TextTrackList);
    Base::initialize(realm);
}

void TextTrackList::visit_edges(JS::Cell::Visitor& visitor)
{
    Base::visit_edges(visitor);
    visitor.visit(m_text_tracks);
}

// https://html.spec.whatwg.org/multipage/media.html#dom-texttracklist-item
JS::ThrowCompletionOr<Optional<JS::PropertyDescriptor>> TextTrackList::internal_get_own_property(JS::PropertyKey const& property_name) const
{
    // To determine the value of an indexed property of a TextTrackList object for a given index index, the user
    // agent must return the indexth text track in the list represented by the TextTrackList object.
    if (property_name.is_number()) {
        if (auto index = property_name.as_number(); index < m_text_tracks.size()) {
            JS::PropertyDescriptor descriptor;
            descriptor.value = m_text_tracks.at(index);

            return descriptor;
        }
    }

    return Base::internal_get_own_property(property_name);
}

// https://html.spec.whatwg.org/multipage/media.html#dom-texttracklist-length
size_t TextTrackList::length() const
{
    return m_text_tracks.size();
}

// https://html.spec.whatwg.org/multipage/media.html#dom-texttracklist-gettrackbyid
GC::Ptr<TextTrack> TextTrackList::get_track_by_id(StringView id) const
{
    // The getTrackById(id) method must return the first TextTrack in the TextTrackList object whose id
    // IDL attribute would return a value equal to the value of the id argument.
    auto it = m_text_tracks.find_if([&](auto const& text_track) {
        return text_track->id() == id;
    });

    // When no tracks match the given argument, the method must return null.
    if (it == m_text_tracks.end())
        return nullptr;

    return *it;
}

bool TextTrackList::contains(TextTrack const& track) const
{
    return m_text_tracks.find_if([&](auto const& candidate) {
               return candidate.ptr() == &track;
           }) != m_text_tracks.end();
}

void TextTrackList::add_track(Badge<HTMLMediaElement>, GC::Ref<TextTrack> track,
                              HTMLMediaElement& media_element)
{
    if (contains(*track))
        return;
    m_text_tracks.append(track);
    track->set_media_element(media_element);
}

bool TextTrackList::remove_track(Badge<HTMLMediaElement>, TextTrack& track)
{
    auto removed = m_text_tracks.remove_first_matching([&](auto const& candidate) {
        return candidate.ptr() == &track;
    });
    if (removed)
        track.clear_media_element();
    return removed;
}

size_t TextTrackList::index_of(TextTrack const& track) const
{
    for (size_t index = 0; index < m_text_tracks.size(); ++index)
        if (m_text_tracks.at(index).ptr() == &track)
            return index;
    return m_text_tracks.size();
}

// https://html.spec.whatwg.org/multipage/media.html#handler-texttracklist-onchange
void TextTrackList::set_onchange(WebIDL::CallbackType* event_handler)
{
    set_event_handler_attribute(HTML::EventNames::change, event_handler);
}

// https://html.spec.whatwg.org/multipage/media.html#handler-texttracklist-onchange
WebIDL::CallbackType* TextTrackList::onchange()
{
    return event_handler_attribute(HTML::EventNames::change);
}

// https://html.spec.whatwg.org/multipage/media.html#handler-texttracklist-onaddtrack
void TextTrackList::set_onaddtrack(WebIDL::CallbackType* event_handler)
{
    set_event_handler_attribute(HTML::EventNames::addtrack, event_handler);
}

// https://html.spec.whatwg.org/multipage/media.html#handler-texttracklist-onaddtrack
WebIDL::CallbackType* TextTrackList::onaddtrack()
{
    return event_handler_attribute(HTML::EventNames::addtrack);
}

// https://html.spec.whatwg.org/multipage/media.html#handler-texttracklist-onremovetrack
void TextTrackList::set_onremovetrack(WebIDL::CallbackType* event_handler)
{
    set_event_handler_attribute(HTML::EventNames::removetrack, event_handler);
}

// https://html.spec.whatwg.org/multipage/media.html#handler-texttracklist-onremovetrack
WebIDL::CallbackType* TextTrackList::onremovetrack()
{
    return event_handler_attribute(HTML::EventNames::removetrack);
}

}
