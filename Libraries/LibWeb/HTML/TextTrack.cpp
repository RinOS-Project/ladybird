/*
 * Copyright (c) 2024, Jamie Mansfield <jmansfield@cadixdev.org>
 *
 * SPDX-License-Identifier: BSD-2-Clause
 */

#include <LibJS/Runtime/Realm.h>
#include <LibWeb/Bindings/Intrinsics.h>
#include <LibWeb/HTML/EventNames.h>
#include <LibWeb/HTML/HTMLMediaElement.h>
#include <LibWeb/HTML/TextTrack.h>
#include <LibWeb/HTML/TextTrackCue.h>
#include <LibWeb/HTML/TextTrackCueList.h>
#include <LibWeb/HTML/TextTrackObserver.h>

namespace Web::HTML {

GC_DEFINE_ALLOCATOR(TextTrack);

GC::Ref<TextTrack> TextTrack::create(JS::Realm& realm)
{
    auto text_track = realm.create<TextTrack>(realm);
    text_track->m_cues = TextTrackCueList::create(realm);
    text_track->m_active_cues = TextTrackCueList::create(realm);
    return text_track;
}

TextTrack::TextTrack(JS::Realm& realm)
    : DOM::EventTarget(realm)
{
}

TextTrack::~TextTrack() = default;

void TextTrack::initialize(JS::Realm& realm)
{
    WEB_SET_PROTOTYPE_FOR_INTERFACE(TextTrack);
    Base::initialize(realm);
}

void TextTrack::visit_edges(Cell::Visitor& visitor)
{
    Base::visit_edges(visitor);
    visitor.visit(m_observers);
    visitor.visit(m_cues);
    visitor.visit(m_active_cues);
    visitor.visit(m_media_element);
}

// https://html.spec.whatwg.org/multipage/media.html#dom-texttrack-kind
Bindings::TextTrackKind TextTrack::kind()
{
    return m_kind;
}

void TextTrack::set_kind(Bindings::TextTrackKind kind)
{
    m_kind = kind;
}

// https://html.spec.whatwg.org/multipage/media.html#dom-texttrack-label
String TextTrack::label()
{
    return m_label;
}

void TextTrack::set_label(String label)
{
    m_label = label;
}

// https://html.spec.whatwg.org/multipage/media.html#dom-texttrack-language
String TextTrack::language()
{
    return m_language;
}

void TextTrack::set_language(String language)
{
    m_language = language;
}

// https://html.spec.whatwg.org/multipage/media.html#dom-texttrack-id
String TextTrack::id()
{
    return m_id;
}

void TextTrack::set_id(String id)
{
    m_id = id;
}

// https://html.spec.whatwg.org/multipage/media.html#dom-texttrack-mode
Bindings::TextTrackMode TextTrack::mode()
{
    return m_mode;
}

void TextTrack::set_mode(Bindings::TextTrackMode mode)
{
    if (m_mode == mode)
        return;
    m_mode = mode;
    if (m_media_element)
        m_media_element->text_track_cues_changed();
}

// https://html.spec.whatwg.org/multipage/media.html#handler-texttrack-oncuechange
void TextTrack::set_oncuechange(WebIDL::CallbackType* event_handler)
{
    set_event_handler_attribute(HTML::EventNames::cuechange, event_handler);
}

// https://html.spec.whatwg.org/multipage/media.html#handler-texttrack-oncuechange
WebIDL::CallbackType* TextTrack::oncuechange()
{
    return event_handler_attribute(HTML::EventNames::cuechange);
}

void TextTrack::set_readiness_state(ReadinessState readiness_state)
{
    m_readiness_state = readiness_state;

    for (auto observer : m_observers) {
        if (auto callback = observer->track_readiness_observer())
            callback->function()(m_readiness_state);
    }
}

GC::Ref<TextTrackCueList> TextTrack::cues() const
{
    VERIFY(m_cues);
    return *m_cues;
}

GC::Ref<TextTrackCueList> TextTrack::active_cues() const
{
    VERIFY(m_active_cues);
    m_active_cues->clear();
    if (!m_media_element || m_mode == Bindings::TextTrackMode::Disabled)
        return *m_active_cues;

    auto current_time = m_media_element->current_time();
    if (!isfinite(current_time))
        return *m_active_cues;

    for (size_t index = 0; index < m_cues->length(); ++index) {
        auto cue = m_cues->at(index);
        if (cue->start_time() <= current_time && current_time < cue->end_time())
            m_active_cues->append(cue);
    }
    return *m_active_cues;
}

WebIDL::ExceptionOr<void> TextTrack::add_cue(GC::Ref<TextTrackCue> cue)
{
    if (cue->track() && cue->track() != this)
        return WebIDL::InvalidStateError::create(realm(), "Cue already belongs to another text track"_utf16);
    if (!m_cues->contains(*cue)) {
        m_cues->insert_sorted_by_start_time(cue);
        cue->set_track(this);
        if (m_media_element)
            m_media_element->text_track_cues_changed();
    }
    return {};
}

void TextTrack::remove_cue(GC::Ref<TextTrackCue> cue)
{
    if (!m_cues->remove(*cue))
        return;
    cue->set_track(nullptr);
    if (m_media_element)
        m_media_element->text_track_cues_changed();
}

void TextTrack::cue_time_changed(TextTrackCue& cue)
{
    if (!m_cues->contains(cue))
        return;
    m_cues->resort_by_start_time();
    if (m_media_element)
        m_media_element->text_track_cues_changed();
}

void TextTrack::set_media_element(HTMLMediaElement& media_element)
{
    m_media_element = media_element;
    media_element.text_track_cues_changed();
}

void TextTrack::register_observer(Badge<TextTrackObserver>, TextTrackObserver& observer)
{
    auto result = m_observers.set(observer);
    VERIFY(result == AK::HashSetResult::InsertedNewEntry);
}

void TextTrack::unregister_observer(Badge<TextTrackObserver>, TextTrackObserver& observer)
{
    bool was_removed = m_observers.remove(observer);
    VERIFY(was_removed);
}

Bindings::TextTrackKind text_track_kind_from_string(String value)
{
    // https://html.spec.whatwg.org/multipage/media.html#attr-track-kind

    if (value.is_empty() || value.equals_ignoring_ascii_case("subtitles"sv)) {
        return Bindings::TextTrackKind::Subtitles;
    }
    if (value.equals_ignoring_ascii_case("captions"sv)) {
        return Bindings::TextTrackKind::Captions;
    }
    if (value.equals_ignoring_ascii_case("descriptions"sv)) {
        return Bindings::TextTrackKind::Descriptions;
    }
    if (value.equals_ignoring_ascii_case("chapters"sv)) {
        return Bindings::TextTrackKind::Chapters;
    }
    if (value.equals_ignoring_ascii_case("metadata"sv)) {
        return Bindings::TextTrackKind::Metadata;
    }

    return Bindings::TextTrackKind::Metadata;
}

}
