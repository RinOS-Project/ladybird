/*
 * Copyright (c) 2024, Noah Bright <noah.bright.1@gmail.com>
 *
 * SPDX-License-Identifier: BSD-2-Clause
 */

#include <LibWeb/Bindings/Intrinsics.h>
#include <LibWeb/Bindings/PerformanceEventTimingPrototype.h>
#include <LibWeb/DOM/Event.h>
#include <LibWeb/DOM/Node.h>
#include <LibWeb/EventTiming/PerformanceEventTiming.h>
#include <LibWeb/PerformanceTimeline/EntryTypes.h>

namespace Web::EventTiming {

GC_DEFINE_ALLOCATOR(PerformanceEventTiming);

// https://www.w3.org/TR/event-timing/#sec-init-event-timing
PerformanceEventTiming::PerformanceEventTiming(
    JS::Realm& realm,
    String const& name,
    HighResolutionTime::DOMHighResTimeStamp start_time,
    HighResolutionTime::DOMHighResTimeStamp duration,
    DOM::Event const& event,
    HighResolutionTime::DOMHighResTimeStamp processing_start,
    unsigned long long interaction_id)
    : PerformanceTimeline::PerformanceEntry(realm, name, start_time, duration)
    , m_entry_type(PerformanceTimeline::EntryTypes::event)
    , m_event_target(event.target())
    , m_start_time(event.time_stamp())
    , m_processing_start(processing_start)
    // The constructor receives the duration measured from the event's start
    // timestamp. Keep the end timestamp stable for the lifetime of the entry;
    // the dispatch-pending/final-event-timing algorithms can replace this
    // value once they provide a real completion timestamp.
    , m_processing_end(start_time + duration)
    , m_cancelable(event.cancelable())
    , m_interaction_id(interaction_id)

{
}

PerformanceEventTiming::~PerformanceEventTiming() = default;

FlyString const& PerformanceEventTiming::entry_type() const
{
    return m_entry_type;
}

HighResolutionTime::DOMHighResTimeStamp PerformanceEventTiming::processing_end() const
{
    return m_processing_end;
}

HighResolutionTime::DOMHighResTimeStamp PerformanceEventTiming::processing_start() const
{
    return m_processing_start;
}

bool PerformanceEventTiming::cancelable() const
{
    return m_cancelable;
}

JS::ThrowCompletionOr<GC::Ptr<DOM::Node>> PerformanceEventTiming::target()
{
    // Event Timing exposes only Node targets. Window, worker and other
    // EventTarget instances are intentionally represented as null.
    if (!m_event_target || !m_event_target->is_dom_node())
        return nullptr;
    return static_cast<DOM::Node*>(m_event_target.ptr());
}

unsigned long long PerformanceEventTiming::interaction_id()
{
    return m_interaction_id;
}

// https://www.w3.org/TR/event-timing/#sec-should-add-performanceeventtiming
PerformanceTimeline::ShouldAddEntry PerformanceEventTiming::should_add_performance_event_timing() const
{
    // 1. If entry’s entryType attribute value equals to "first-input", return true.
    if (entry_type() == "first-input")
        return PerformanceTimeline::ShouldAddEntry::Yes;

    // 2. Assert that entry’s entryType attribute value equals "event".
    VERIFY(entry_type() == "event");

    /* PerformanceObserverInit currently has no durationThreshold member in
     * this pinned tree. Use the Web Event Timing default (104ms) until that
     * dictionary is extended; short events must not fill the timeline. */
    constexpr double default_duration_threshold = 104.0;
    return duration() >= default_duration_threshold
        ? PerformanceTimeline::ShouldAddEntry::Yes
        : PerformanceTimeline::ShouldAddEntry::No;
}

// https://w3c.github.io/timing-entrytypes-registry/#dfn-availablefromtimeline
// FIXME: the output here depends on the type of the object instance, but this function is static
//        the commented out if statement won't compile
PerformanceTimeline::AvailableFromTimeline PerformanceEventTiming::available_from_timeline()
{
    /* Both `event` and `first-input` entries are exposed by this timeline.
     * The registry API is static in the current binding, so the type-specific
     * distinction is enforced by should_add_performance_event_timing(). */
    return PerformanceTimeline::AvailableFromTimeline::Yes;
}

// https://w3c.github.io/timing-entrytypes-registry/#dfn-maxbuffersize
// FIXME: Same issue as available_from_timeline() above
Optional<u64> PerformanceEventTiming::max_buffer_size()
{
    /* The registry hook is static although this class has two entry types.
     * Reserve the larger event buffer; first-input remains bounded by its
     * admission rule and does not make the allocation unbounded. */
    return 150;
}

// https://w3c.github.io/timing-entrytypes-registry/#dfn-should-add-entry
PerformanceTimeline::ShouldAddEntry PerformanceEventTiming::should_add_entry(Optional<PerformanceTimeline::PerformanceObserverInit const&>) const
{
    return should_add_performance_event_timing();
}

void PerformanceEventTiming::initialize(JS::Realm& realm)
{
    WEB_SET_PROTOTYPE_FOR_INTERFACE(PerformanceEventTiming);
    Base::initialize(realm);
}

void PerformanceEventTiming::visit_edges(JS::Cell::Visitor& visitor)
{
    Base::visit_edges(visitor);
    visitor.visit(m_event_target);
}

}
