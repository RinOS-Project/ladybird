/*
 * Copyright (c) 2020, the SerenityOS developers.
 * Copyright (c) 2024, Jamie Mansfield <jmansfield@cadixdev.org>
 * Copyright (c) 2025, Tim Ledbetter <tim.ledbetter@ladybird.org>
 *
 * SPDX-License-Identifier: BSD-2-Clause
 */

#include <AK/Math.h>
#include <AK/StringBuilder.h>
#include <LibTextCodec/Decoder.h>
#include <LibURL/Parser.h>
#include <LibGC/RootVector.h>
#include <LibWeb/Bindings/HTMLTrackElementPrototype.h>
#include <LibWeb/Bindings/Intrinsics.h>
#include <LibWeb/DOM/Document.h>
#include <LibWeb/DOM/Event.h>
#include <LibWeb/Fetch/Fetching/Fetching.h>
#include <LibWeb/Fetch/Infrastructure/FetchAlgorithms.h>
#include <LibWeb/Fetch/Infrastructure/FetchController.h>
#include <LibWeb/Fetch/Infrastructure/HTTP/Responses.h>
#include <LibWeb/Fetch/Infrastructure/HTTP/Statuses.h>
#include <LibWeb/HTML/HTMLMediaElement.h>
#include <LibWeb/HTML/HTMLTrackElement.h>
#include <LibWeb/HTML/PotentialCORSRequest.h>
#include <LibWeb/HTML/TextTrack.h>
#include <LibWeb/HTML/TextTrackObserver.h>
#include <LibWeb/Infra/CharacterTypes.h>
#include <LibWeb/Platform/EventLoopPlugin.h>
#include <LibWeb/WebVTT/VTTCue.h>

namespace Web::HTML {

namespace {

struct ParsedWebVTTCue {
    String id;
    double start_time { 0 };
    double end_time { 0 };
    String text;
};

static Optional<double> parse_webvtt_timestamp(StringView input)
{
    auto timestamp = input.trim_whitespace();
    auto fields = timestamp.split_view(':', SplitBehavior::KeepEmpty);
    if (fields.size() != 2 && fields.size() != 3)
        return {};

    auto parse_unsigned = [](StringView value, size_t minimum_digits) -> Optional<u64> {
        if (value.length() < minimum_digits)
            return {};
        for (auto code_unit : value) {
            if (!is_ascii_digit(code_unit))
                return {};
        }
        return value.to_number<u64>();
    };

    auto seconds_field = fields[fields.size() - 1];
    auto seconds_parts = seconds_field.split_view('.', SplitBehavior::KeepEmpty);
    if (seconds_parts.size() != 2 || seconds_parts[1].length() != 3)
        return {};

    auto seconds = parse_unsigned(seconds_parts[0], 2);
    auto milliseconds = parse_unsigned(seconds_parts[1], 3);
    if (!seconds.has_value() || !milliseconds.has_value() || seconds.value() >= 60)
        return {};

    u64 minutes = 0;
    u64 hours = 0;
    if (fields.size() == 2) {
        auto parsed_minutes = parse_unsigned(fields[0], 2);
        if (!parsed_minutes.has_value() || parsed_minutes.value() >= 60)
            return {};
        minutes = parsed_minutes.value();
    } else {
        auto parsed_hours = parse_unsigned(fields[0], 2);
        auto parsed_minutes = parse_unsigned(fields[1], 2);
        if (!parsed_hours.has_value() || !parsed_minutes.has_value() || parsed_minutes.value() >= 60)
            return {};
        hours = parsed_hours.value();
        minutes = parsed_minutes.value();
    }

    auto result = (static_cast<double>(hours) * 3600.0)
        + (static_cast<double>(minutes) * 60.0)
        + static_cast<double>(seconds.value())
        + (static_cast<double>(milliseconds.value()) / 1000.0);
    if (!isfinite(result))
        return {};
    return result;
}

static StringView strip_webvtt_line_ending(StringView line)
{
    if (line.ends_with('\r'))
        return line.substring_view(0, line.length() - 1);
    return line;
}

static bool is_webvtt_block_start(StringView line, StringView name)
{
    if (!line.starts_with_bytes(name))
        return false;
    if (line.length() == name.length())
        return true;
    auto separator = line.code_unit_at(name.length());
    return separator == ' ' || separator == '\t';
}

static ErrorOr<Vector<ParsedWebVTTCue>> parse_webvtt(String const& source)
{
    auto lines = source.bytes_as_string_view().split_view('\n', SplitBehavior::KeepEmpty);
    if (lines.is_empty())
        return Error::from_string_literal("empty WebVTT resource");

    auto header = strip_webvtt_line_ending(lines[0]);
    if (!header.starts_with_bytes("WEBVTT"sv)
        || (header.length() > 6 && header.code_unit_at(6) != ' ' && header.code_unit_at(6) != '\t'))
        return Error::from_string_literal("invalid WebVTT header");

    Vector<ParsedWebVTTCue> cues;
    constexpr size_t max_cues = 4096;
    constexpr size_t max_cue_text_bytes = 1024 * 1024;
    size_t total_text_bytes = 0;

    size_t index = 1;
    while (index < lines.size()) {
        auto line = strip_webvtt_line_ending(lines[index]);
        if (line.trim_whitespace().is_empty()) {
            ++index;
            continue;
        }

        // NOTE/STYLE/REGION blocks are metadata and do not publish cues.
        if (is_webvtt_block_start(line, "NOTE"sv)
            || is_webvtt_block_start(line, "STYLE"sv)
            || is_webvtt_block_start(line, "REGION"sv)) {
            do {
                ++index;
            } while (index < lines.size() && !strip_webvtt_line_ending(lines[index]).trim_whitespace().is_empty());
            continue;
        }

        if (cues.size() >= max_cues)
            return Error::from_string_literal("WebVTT cue limit exceeded");

        String id;
        auto timing_line = line;
        auto timing_marker = timing_line.find(" --> "sv);
        if (!timing_marker.has_value()) {
            id = TRY(line.to_string());
            ++index;
            if (index >= lines.size())
                return Error::from_string_literal("WebVTT cue has no timing line");
            timing_line = strip_webvtt_line_ending(lines[index]);
            timing_marker = timing_line.find(" --> "sv);
        }

        if (!timing_marker.has_value())
            return Error::from_string_literal("WebVTT cue has invalid timing");

        auto start = parse_webvtt_timestamp(timing_line.substring_view(0, timing_marker.value()));
        auto settings = timing_line.substring_view(timing_marker.value() + 5).trim_whitespace();
        auto setting_tokens = settings.split_view_if(Infra::is_ascii_whitespace);
        if (!start.has_value() || setting_tokens.is_empty())
            return Error::from_string_literal("WebVTT cue has invalid timing");
        auto end = parse_webvtt_timestamp(setting_tokens[0]);
        if (!end.has_value() || end.value() <= start.value())
            return Error::from_string_literal("WebVTT cue has invalid interval");

        ++index;
        StringBuilder text_builder;
        bool first_text_line = true;
        while (index < lines.size()) {
            auto text_line = strip_webvtt_line_ending(lines[index]);
            if (text_line.trim_whitespace().is_empty())
                break;
            if (!first_text_line)
                TRY(text_builder.try_append("\n"sv));
            TRY(text_builder.try_append(text_line));
            first_text_line = false;
            total_text_bytes += text_line.length();
            if (total_text_bytes > max_cue_text_bytes)
                return Error::from_string_literal("WebVTT text limit exceeded");
            ++index;
        }

        TRY(cues.try_append(ParsedWebVTTCue {
            .id = move(id),
            .start_time = start.value(),
            .end_time = end.value(),
            .text = TRY(text_builder.to_string()),
        }));
    }

    return cues;
}

}

GC_DEFINE_ALLOCATOR(HTMLTrackElement);

HTMLTrackElement::HTMLTrackElement(DOM::Document& document, DOM::QualifiedName qualified_name)
    : HTMLElement(document, move(qualified_name))
{
}

HTMLTrackElement::~HTMLTrackElement() = default;

void HTMLTrackElement::initialize(JS::Realm& realm)
{
    WEB_SET_PROTOTYPE_FOR_INTERFACE(HTMLTrackElement);
    Base::initialize(realm);

    m_track = TextTrack::create(realm);
    m_track_observer = realm.create<TextTrackObserver>(realm, *m_track);
}

void HTMLTrackElement::visit_edges(Cell::Visitor& visitor)
{
    Base::visit_edges(visitor);
    visitor.visit(m_track);
    visitor.visit(m_track_observer);
    visitor.visit(m_fetch_algorithms);
    visitor.visit(m_fetch_controller);
}

void HTMLTrackElement::attribute_changed(FlyString const& name, Optional<String> const& old_value, Optional<String> const& value, Optional<FlyString> const& namespace_)
{
    Base::attribute_changed(name, old_value, value, namespace_);

    // https://html.spec.whatwg.org/multipage/media.html#sourcing-out-of-band-text-tracks
    // As the kind, label, and srclang attributes are set, changed, or removed, the text track must update accordingly, as per the definitions above.
    if (name.equals_ignoring_ascii_case(HTML::AttributeNames::kind)) {
        m_track->set_kind(text_track_kind_from_string(value.value_or({})));
    } else if (name.equals_ignoring_ascii_case(HTML::AttributeNames::label)) {
        m_track->set_label(value.value_or({}));
    } else if (name.equals_ignoring_ascii_case(HTML::AttributeNames::srclang)) {
        m_track->set_language(value.value_or({}));
    } else if (name.equals_ignoring_ascii_case(HTML::AttributeNames::src)) {
        // https://html.spec.whatwg.org/multipage/media.html#sourcing-out-of-band-text-tracks:attr-track-src
        // FIXME: Whenever a track element has its src attribute set, changed, or removed, the user agent must immediately empty the element's text track's text track list of cues.
        //        (This also causes the algorithm above to stop adding cues from the resource being obtained using the previously given URL, if any.)

        if (!value.has_value())
            return;

        // https://html.spec.whatwg.org/multipage/media.html#attr-track-src
        // When the element's src attribute is set, run these steps:
        // 1. Let trackURL be failure.
        Optional<String> track_url;

        // 2. Let value be the element's src attribute value.
        // 3. If value is not the empty string, then set trackURL to the result of encoding-parsing-and-serializing a URL given value, relative to the element's node document.
        if (!value->is_empty())
            track_url = document().encoding_parse_and_serialize_url(value.value_or({}));

        // 4. Set the element's track URL to trackURL if it is not failure; otherwise to the empty string.
        set_track_url(track_url.value_or({}));
    }
    // https://html.spec.whatwg.org/multipage/media.html#dom-texttrack-id
    // For tracks that correspond to track elements, the track's identifier is the value of the element's id attribute, if any.
    if (name.equals_ignoring_ascii_case(HTML::AttributeNames::id)) {
        m_track->set_id(value.value_or({}));
    }
}

void HTMLTrackElement::inserted()
{
    HTMLElement::inserted();

    if (is<HTMLMediaElement>(parent_element().ptr()))
        as<HTMLMediaElement>(parent_element())->add_text_track_element(*m_track);

    // AD-HOC: This is a hack to allow tracks to start loading, without needing to implement the entire
    //         "honor user preferences for automatic text track selection" AO detailed here:
    //         https://html.spec.whatwg.org/multipage/media.html#honor-user-preferences-for-automatic-text-track-selection
    m_track->set_mode(Bindings::TextTrackMode::Hidden);

    start_the_track_processing_model();
}

void HTMLTrackElement::removed_from(DOM::Node* old_parent, DOM::Node& old_root)
{
    if (is<HTMLMediaElement>(old_parent))
        as<HTMLMediaElement>(old_parent)->remove_text_track_element(*m_track);

    HTMLElement::removed_from(old_parent, old_root);
}

// https://html.spec.whatwg.org/multipage/media.html#dom-track-readystate
WebIDL::UnsignedShort HTMLTrackElement::ready_state()
{
    // The readyState attribute must return the numeric value corresponding to the text track readiness state of the track element's text track, as defined by the following list:
    switch (m_track->readiness_state()) {
    case TextTrack::ReadinessState::NotLoaded:
        // NONE (numeric value 0)
        //    The text track not loaded state.
        return 0;
    case TextTrack::ReadinessState::Loading:
        // LOADING (numeric value 1)
        //    The text track loading state.
        return 1;
    case TextTrack::ReadinessState::Loaded:
        // LOADED (numeric value 2)
        //    The text track loaded state.
        return 2;
    case TextTrack::ReadinessState::FailedToLoad:
        // ERROR (numeric value 3)
        //    The text track failed to load state.
        return 3;
    }

    VERIFY_NOT_REACHED();
}

void HTMLTrackElement::set_track_url(String track_url)
{
    if (m_track_url == track_url)
        return;

    m_track_url = move(track_url);

    auto track_is_hidden_or_showing = first_is_one_of(m_track->mode(), Bindings::TextTrackMode::Hidden, Bindings::TextTrackMode::Showing);

    // https://html.spec.whatwg.org/multipage/media.html#start-the-track-processing-model
    if (m_loading && m_fetch_controller && track_is_hidden_or_showing) {
        m_loading = false;
        m_fetch_controller->abort(realm(), {});
    }

    // https://html.spec.whatwg.org/multipage/media.html#start-the-track-processing-model
    if (m_awaiting_track_url_change && track_is_hidden_or_showing) {
        m_awaiting_track_url_change = false;

        // 13. Jump to the step labeled top.
        start_the_track_processing_model_parallel_steps();
    }
}

// https://html.spec.whatwg.org/multipage/media.html#start-the-track-processing-model
void HTMLTrackElement::start_the_track_processing_model()
{
    auto& realm = this->realm();

    // 1. If another occurrence of this algorithm is already running for this text track and its track element, return,
    //    letting that other algorithm take care of this element.
    if (m_loading)
        return;

    // 2. If the text track's text track mode is not set to one of hidden or showing, then return.
    if (!first_is_one_of(m_track->mode(), Bindings::TextTrackMode::Hidden, Bindings::TextTrackMode::Showing))
        return;

    // 3. If the text track's track element does not have a media element as a parent, return.
    if (!is<HTMLMediaElement>(parent_element().ptr()))
        return;

    m_loading = true;

    // 4. Run the remainder of these steps in parallel, allowing whatever caused these steps to run to continue.
    Platform::EventLoopPlugin::the().deferred_invoke(GC::create_function(realm.heap(), [this]() {
        start_the_track_processing_model_parallel_steps();
    }));
}

void HTMLTrackElement::start_the_track_processing_model_parallel_steps()
{
    auto& realm = this->realm();

    // 5. Top: Await a stable state. The synchronous section consists of the following steps.

    // 6. ⌛ Set the text track readiness state to loading.
    m_track->set_readiness_state(TextTrack::ReadinessState::Loading);

    // 7. ⌛ Let URL be the track URL of the track element.
    auto url = track_url();

    // 8. ⌛ If the track element's parent is a media element, then let corsAttributeState be the state of the
    //    parent media element's crossorigin content attribute. Otherwise, let corsAttributeState be No CORS.
    auto cors_attribute_state = CORSSettingAttribute::NoCORS;
    if (is<HTMLMediaElement>(parent())) {
        cors_attribute_state = as<HTMLMediaElement>(parent())->crossorigin();
    }

    // 9. End the synchronous section, continuing the remaining steps in parallel.

    // 10. If URL is not the empty string, then:
    if (!url.is_empty()) {
        // 1. Let request be the result of creating a potential-CORS request given URL, "track", and corsAttributeState,
        // and with the same-origin fallback flag set.
        auto parsed_url = URL::Parser::basic_parse(url);
        VERIFY(parsed_url.has_value());
        auto request = create_potential_CORS_request(realm.vm(), parsed_url.release_value(), Fetch::Infrastructure::Request::Destination::Track, cors_attribute_state, SameOriginFallbackFlag::Yes);

        // 2. Set request's client to the track element's node document's relevant settings object.
        request->set_client(&document().relevant_settings_object());

        // 3. Set request's initiator type to "track".
        request->set_initiator_type(Fetch::Infrastructure::Request::InitiatorType::Track);

        Fetch::Infrastructure::FetchAlgorithms::Input fetch_algorithms_input {};
        fetch_algorithms_input.process_response_consume_body = [this, &realm](auto response, auto body_bytes) {
            m_loading = false;

            // If fetching fails for any reason (network error, the server returns an error code, CORS fails, etc.),
            // or if URL is the empty string, then queue an element task on the DOM manipulation task source given the media element
            // to first change the text track readiness state to failed to load and then fire an event named error at the track element.
            if (!response->url().has_value() || body_bytes.template has<Empty>() || body_bytes.template has<Fetch::Infrastructure::FetchAlgorithms::ConsumeBodyFailureTag>() || !Fetch::Infrastructure::is_ok_status(response->status()) || response->is_network_error()) {
                track_failed_to_load();
                return;
            }

            auto* bytes = body_bytes.template get_pointer<ByteBuffer>();
            if (!bytes || bytes->size() > 4 * 1024 * 1024) {
                track_failed_to_load();
                return;
            }

            auto decoder = TextCodec::decoder_for("UTF-8"sv);
            if (!decoder.has_value()) {
                track_failed_to_load();
                return;
            }
            auto source_text = TextCodec::convert_input_to_utf8_using_given_decoder_unless_there_is_a_byte_order_mark(*decoder, *bytes);
            if (source_text.is_error()) {
                track_failed_to_load();
                return;
            }

            auto parsed_cues = parse_webvtt(source_text.release_value());
            if (parsed_cues.is_error()) {
                track_failed_to_load();
                return;
            }

            // Construct every cue before publishing any of them. A malformed cue
            // must not leave a partially replaced track visible to script.
            GC::RootVector<GC::Ref<WebVTT::VTTCue>> new_cues(realm.heap());
            for (auto const& parsed : parsed_cues.value()) {
                auto cue = WebVTT::VTTCue::construct_impl(realm, parsed.start_time, parsed.end_time, parsed.text);
                if (cue.is_error()) {
                    track_failed_to_load();
                    return;
                }
                cue.value()->set_id(parsed.id);
                if (new_cues.try_append(cue.release_value()).is_error()) {
                    track_failed_to_load();
                    return;
                }
            }

            m_track->clear_cues();
            for (auto cue : new_cues) {
                if (m_track->add_cue(cue).is_error()) {
                    track_failed_to_load();
                    return;
                }
            }

            queue_an_element_task(Task::Source::Networking, [this, &realm]() {
                m_track->set_readiness_state(TextTrack::ReadinessState::Loaded);
                dispatch_event(DOM::Event::create(realm, HTML::EventNames::load));
            });
        };

        // 4. Fetch request.
        m_fetch_algorithms = Fetch::Infrastructure::FetchAlgorithms::create(vm(), move(fetch_algorithms_input));
        m_fetch_controller = Fetch::Fetching::fetch(realm, request, *m_fetch_algorithms);
    } else {
        track_failed_to_load();
        return;
    }

    // 11. Wait until the text track readiness state is no longer set to loading.
    if (m_track->readiness_state() == TextTrack::ReadinessState::Loading) {
        m_track_observer->set_track_readiness_observer([this, url = move(url)](TextTrack::ReadinessState) mutable {
            if (m_track->readiness_state() != TextTrack::ReadinessState::Loading)
                track_became_ready();
        });
    } else {
        track_became_ready();
    }
}

void HTMLTrackElement::track_became_ready()
{
    m_track_observer->set_track_readiness_observer({});

    // 12. Wait until the track URL is no longer equal to URL, at the same time as the text track mode is set to hidden or showing.
    m_awaiting_track_url_change = true;
}

void HTMLTrackElement::track_failed_to_load()
{
    queue_an_element_task(Task::Source::DOMManipulation, [this]() {
        auto& realm = this->realm();

        m_track->set_readiness_state(TextTrack::ReadinessState::FailedToLoad);
        dispatch_event(DOM::Event::create(realm, HTML::EventNames::error));
    });
}

}
