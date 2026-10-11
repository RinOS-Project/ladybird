/*
 * Copyright (c) 2025, Jelle Raaijmakers <jelle@ladybird.org>
 *
 * SPDX-License-Identifier: BSD-2-Clause
 */

#include <AK/Time.h>
#include <LibWeb/Bindings/GeolocationPrototype.h>
#include <LibWeb/Bindings/Intrinsics.h>
#include <LibWeb/DOM/Document.h>
#include <LibWeb/DOM/DocumentObserver.h>
#include <LibWeb/Geolocation/Geolocation.h>
#include <LibWeb/Geolocation/GeolocationPosition.h>
#include <LibWeb/HTML/EventLoop/Task.h>
#include <LibWeb/HTML/Scripting/Environments.h>
#include <LibWeb/HTML/TraversableNavigable.h>
#include <LibWeb/HTML/Window.h>
#include <LibWeb/Platform/EventLoopPlugin.h>
#include <LibWeb/Platform/Timer.h>
#include <LibWeb/Page/Page.h>
#include <LibWeb/WebIDL/AbstractOperations.h>
#include <limits.h>

namespace Web::Geolocation {

static constexpr u32 VISIBILITY_STATE_TIMEOUT_MS = 5'000;

static WebIDL::UnsignedLong s_next_watch_id = 0;

GC_DEFINE_ALLOCATOR(Geolocation);

Geolocation::Geolocation(JS::Realm& realm)
    : PlatformObject(realm)
{
}

void Geolocation::initialize(JS::Realm& realm)
{
    WEB_SET_PROTOTYPE_FOR_INTERFACE(Geolocation);
    Base::initialize(realm);
}

void Geolocation::visit_edges(Visitor& visitor)
{
    Base::visit_edges(visitor);
    visitor.visit(m_cached_position);
    visitor.visit(m_timeout_timers);
    for (auto& watch_timer : m_watch_timers)
        visitor.visit(watch_timer.timer);
    for (auto& request : m_pending_position_requests)
        visitor.visit(request.timeout_timer);
}

// https://w3c.github.io/geolocation/#dom-geolocation-getcurrentposition
void Geolocation::get_current_position(GC::Ref<WebIDL::CallbackType> success_callback,
    GC::Ptr<WebIDL::CallbackType> error_callback, PositionOptions options)
{
    // 1. If this's relevant global object's associated Document is not fully active:
    auto& window = as<HTML::Window>(HTML::relevant_global_object(*this));
    if (!window.associated_document().is_fully_active()) {
        // 1. Call back with error errorCallback and POSITION_UNAVAILABLE.
        call_back_with_error(error_callback, GeolocationPositionError::ErrorCode::PositionUnavailable);

        // 2. Terminate this algorithm.
        return;
    }

    // 2. Request a position passing this, successCallback, errorCallback, and options.
    request_a_position(success_callback, error_callback, options);
}

// https://w3c.github.io/geolocation/#watchposition-method
WebIDL::Long Geolocation::watch_position(GC::Ref<WebIDL::CallbackType> success_callback,
    GC::Ptr<WebIDL::CallbackType> error_callback, PositionOptions options)
{
    // 1. If this's relevant global object's associated Document is not fully active:
    auto& window = as<HTML::Window>(HTML::relevant_global_object(*this));
    if (!window.associated_document().is_fully_active()) {
        // 1. Call back with error passing errorCallback and POSITION_UNAVAILABLE.
        call_back_with_error(error_callback, GeolocationPositionError::ErrorCode::PositionUnavailable);

        // 2. Return 0.
        return 0;
    }

    if (m_watch_ids.size() >= 16u)
        return 0;

    // 2. Let watchId be an implementation-defined unsigned long that is greater than zero.
    WebIDL::UnsignedLong watch_id = 0;
    for (size_t attempt = 0; attempt <= m_watch_ids.size(); ++attempt) {
        watch_id = ++s_next_watch_id;
        if (watch_id != 0u && !m_watch_ids.contains(watch_id))
            break;
        watch_id = 0;
    }
    if (watch_id == 0u)
        return 0;

    // 3. Append watchId to this's [[watchIDs]].
    m_watch_ids.set(watch_id);

    // 4. Request a position passing this, successCallback, errorCallback, options, and watchId.
    request_a_position(success_callback, error_callback, options, watch_id);

    // 5. Return watchId.
    return watch_id;
}

// https://w3c.github.io/geolocation/#clearwatch-method
void Geolocation::clear_watch(WebIDL::Long watch_id)
{
    // 1. Remove watchId from this's [[watchIDs]].
    m_watch_ids.remove(watch_id);
    m_watch_timers.remove_first_matching([&](auto const& item) {
        if (item.watch_id != static_cast<WebIDL::UnsignedLong>(watch_id))
            return false;
        item.timer->stop();
        return true;
    });
    m_pending_position_requests.remove_all_matching([&](auto const& request) {
        if (!request.watch_id.has_value() ||
            request.watch_id.value() != static_cast<WebIDL::UnsignedLong>(watch_id))
            return false;
        if (request.timeout_timer)
            request.timeout_timer->stop();
        auto& document = as<HTML::Window>(
            HTML::relevant_global_object(*this)).associated_document();
        document.page().client().page_did_cancel_geolocation_request(
            request.id);
        return true;
    });
}

bool Geolocation::finish_position_request(u64 request_id)
{
    return m_pending_position_requests.remove_first_matching([&](auto const& request) {
        if (request.id != request_id)
            return false;
        auto& document = as<HTML::Window>(
            HTML::relevant_global_object(*this)).associated_document();
        /* A normal result has already retired its embedder request. Timeout
         * and other local terminal paths still need to stop a live provider. */
        document.page().client().page_did_cancel_geolocation_request(request.id);
        if (request.timeout_timer)
            request.timeout_timer->stop();
        return true;
    });
}

void Geolocation::schedule_watch_position(
    GC::Ref<WebIDL::CallbackType> success_callback,
    GC::Ptr<WebIDL::CallbackType> error_callback, PositionOptions options,
    WebIDL::UnsignedLong watch_id)
{
    if (!m_watch_ids.contains(watch_id))
        return;
    auto timer = Platform::Timer::create_single_shot(
        heap(), 1'000, {});
    m_watch_timers.append({ watch_id, timer });
    timer->on_timeout = GC::create_function(heap(),
        [this, success_callback, error_callback, options, watch_id] {
            m_watch_timers.remove_first_matching([&](auto const& item) {
                return item.watch_id == watch_id;
            });
            if (m_watch_ids.contains(watch_id))
                request_a_position(success_callback, error_callback, options,
                                   watch_id);
        });
    timer->start();
}

// https://w3c.github.io/geolocation/#dfn-acquire-a-position
void Geolocation::acquire_a_position(GC::Ref<WebIDL::CallbackType> success_callback,
    GC::Ptr<WebIDL::CallbackType> error_callback, PositionOptions options, Optional<WebIDL::UnsignedLong> watch_id)
{
    if (watch_id.has_value() && !m_watch_ids.contains(watch_id.value()))
        return;

    auto& document = as<HTML::Window>(
        HTML::relevant_global_object(*this)).associated_document();
    if (is_non_secure_context(HTML::relevant_settings_object(*this)) ||
        !document.is_allowed_to_use_feature(
            DOM::PolicyControlledFeature::Geolocation)) {
        if (watch_id.has_value())
            m_watch_ids.remove(watch_id.value());
        call_back_with_error(
            error_callback,
            GeolocationPositionError::ErrorCode::PermissionDenied);
        return;
    }

    u64 request_id = 0;
    do {
        request_id = ++m_next_position_request_id;
    } while (request_id == 0);

    GC::Ptr<Platform::Timer> timeout_timer;
    if (options.timeout != 0xFFFFFFFFu) {
        auto timeout_ms = options.timeout > static_cast<u32>(INT_MAX)
            ? INT_MAX
            : static_cast<int>(options.timeout);
        timeout_timer = Platform::Timer::create_single_shot(heap(), timeout_ms, {});
    }
    m_pending_position_requests.append({ request_id, watch_id, timeout_timer });

    if (timeout_timer) {
        timeout_timer->on_timeout = GC::create_function(heap(),
            [this, request_id, error_callback, success_callback, options, watch_id] {
                if (!finish_position_request(request_id))
                    return;
                if (watch_id.has_value() &&
                    !m_watch_ids.contains(watch_id.value()))
                    return;
                call_back_with_error(
                    error_callback,
                    GeolocationPositionError::ErrorCode::Timeout);
                if (watch_id.has_value())
                    schedule_watch_position(
                        success_callback, error_callback, options,
                        watch_id.value());
            });
        timeout_timer->start();
    }

    GC::Weak<DOM::Document> pending_document { document };
    if (!pending_document || !pending_document->is_fully_active()) {
        (void)finish_position_request(request_id);
        return;
    }

    auto acquisition_time = AK::UnixDateTime::now().milliseconds_since_epoch();
    auto cached_position = m_cached_position;
    pending_document->page().client().page_did_request_geolocation(
        request_id,
        GC::create_function(heap(),
            [this, pending_document, success_callback, error_callback, options,
             watch_id, request_id, acquisition_time,
             cached_position](Web::GeolocationPositionResult result) {
                if (!finish_position_request(request_id))
                    return;
                if (watch_id.has_value() &&
                    !m_watch_ids.contains(watch_id.value()))
                    return;
                if (!pending_document ||
                    !pending_document->is_fully_active() ||
                    !pending_document->is_allowed_to_use_feature(
                        DOM::PolicyControlledFeature::Geolocation))
                    return;

                if (result.status ==
                    Web::GeolocationPositionResult::Status::PermissionDenied) {
                    if (watch_id.has_value())
                        m_watch_ids.remove(watch_id.value());
                    call_back_with_error(
                        error_callback,
                        GeolocationPositionError::ErrorCode::PermissionDenied);
                    return;
                }

                auto queue_success = [this, success_callback, error_callback,
                                      options, watch_id](
                                         GC::Ref<GeolocationPosition> position) {
                    HTML::queue_a_task(
                        HTML::Task::Source::Geolocation, nullptr, nullptr,
                        GC::create_function(heap(),
                            [success_callback, position] {
                                (void)WebIDL::invoke_callback(
                                    success_callback, {},
                                    WebIDL::ExceptionBehavior::Report,
                                    { { position } });
                            }));
                    if (watch_id.has_value())
                        schedule_watch_position(
                            success_callback, error_callback, options,
                            watch_id.value());
                };

                if (result.status ==
                    Web::GeolocationPositionResult::Status::Available) {
                    CoordinatesData coordinates;
                    coordinates.latitude =
                        static_cast<double>(result.latitude_e7) / 10000000.0;
                    coordinates.longitude =
                        static_cast<double>(result.longitude_e7) / 10000000.0;
                    coordinates.accuracy =
                        static_cast<double>(result.horizontal_accuracy_mm) /
                        1000.0;
                    if ((result.flags &
                         Web::GeolocationPositionResult::ValidAltitude) != 0u) {
                        coordinates.altitude = Optional<double> {
                            static_cast<double>(result.altitude_mm) / 1000.0 };
                        coordinates.altitude_accuracy = Optional<double> {
                            static_cast<double>(result.vertical_accuracy_mm) /
                            1000.0 };
                    }
                    if ((result.flags &
                         Web::GeolocationPositionResult::ValidSpeed) != 0u)
                        coordinates.speed = Optional<double> {
                            static_cast<double>(result.speed_mm_per_second) /
                            1000.0 };
                    if ((result.flags &
                         Web::GeolocationPositionResult::ValidHeading) != 0u)
                        coordinates.heading = Optional<double> {
                            static_cast<double>(result.heading_millidegrees) /
                            1000.0 };

                    auto position_data = realm().create<GeolocationCoordinates>(
                        realm(), move(coordinates));
                    auto position = realm().create<GeolocationPosition>(
                        realm(), position_data,
                        AK::UnixDateTime::now().milliseconds_since_epoch(),
                        options.enable_high_accuracy);
                    m_cached_position = *position;
                    queue_success(position);
                    return;
                }

                if (result.status !=
                    Web::GeolocationPositionResult::Status::PermissionGrantedPositionUnavailable) {
                    call_back_with_error(
                        error_callback,
                        GeolocationPositionError::ErrorCode::PositionUnavailable);
                    if (watch_id.has_value())
                        schedule_watch_position(
                            success_callback, error_callback, options,
                            watch_id.value());
                    return;
                }

                // Site permission is resolved before using emulated or cached
                // coordinates, so neither path bypasses the permission owner.
                auto emulated_position_data = get_emulated_position_data();
                if (emulated_position_data.has<GeolocationPositionError::ErrorCode>()) {
                    auto code = emulated_position_data.get<
                        GeolocationPositionError::ErrorCode>();
                    if (code ==
                        GeolocationPositionError::ErrorCode::PermissionDenied) {
                        if (watch_id.has_value())
                            m_watch_ids.remove(watch_id.value());
                    }
                    call_back_with_error(error_callback, code);
                    if (watch_id.has_value() &&
                        code != GeolocationPositionError::ErrorCode::PermissionDenied)
                        schedule_watch_position(
                            success_callback, error_callback, options,
                            watch_id.value());
                    return;
                }
                if (emulated_position_data.has<GC::Ref<GeolocationCoordinates>>()) {
                    auto position = realm().create<GeolocationPosition>(
                        realm(),
                        emulated_position_data.get<
                            GC::Ref<GeolocationCoordinates>>(),
                        acquisition_time, options.enable_high_accuracy);
                    m_cached_position = *position;
                    queue_success(position);
                    return;
                }

                if (cached_position && options.maximum_age > 0) {
                    auto cache_time = acquisition_time - options.maximum_age;
                    if (cached_position->timestamp() > cache_time &&
                        cached_position->is_high_accuracy() ==
                            options.enable_high_accuracy) {
                        queue_success(cached_position);
                        return;
                    }
                }

                call_back_with_error(
                    error_callback,
                    GeolocationPositionError::ErrorCode::PositionUnavailable);
                if (watch_id.has_value())
                    schedule_watch_position(
                        success_callback, error_callback, options,
                        watch_id.value());
            }),
        options.timeout,
        *pending_document);
}
EmulatedPositionData Geolocation::get_emulated_position_data() const
{
    // 1. Let navigable be geolocation's relevant global object's associated Document's node navigable.
    auto navigable = as<HTML::Window>(HTML::relevant_global_object(*this)).navigable();

    // 2. If navigable is null, return null.
    if (!navigable)
        return Empty {};

    // 3. Let traversable be navigable’s top-level traversable.
    auto traversable = navigable->top_level_traversable();

    // 4. If traversable is null, return null.
    if (!traversable)
        return Empty {};

    // 5. Return traversable's associated emulated position data.
    return traversable->emulated_position_data();
}

// https://w3c.github.io/geolocation/#dfn-request-a-position
void Geolocation::request_a_position(GC::Ref<WebIDL::CallbackType> success_callback,
    GC::Ptr<WebIDL::CallbackType> error_callback, PositionOptions options, Optional<WebIDL::UnsignedLong> watch_id)
{
    auto& document = as<HTML::Window>(
        HTML::relevant_global_object(*this)).associated_document();
    if (!document.is_allowed_to_use_feature(
            DOM::PolicyControlledFeature::Geolocation) ||
        is_non_secure_context(HTML::relevant_settings_object(*this))) {
        if (watch_id.has_value())
            m_watch_ids.remove(watch_id.value());
        call_back_with_error(
            error_callback,
            GeolocationPositionError::ErrorCode::PermissionDenied);
        return;
    }

    run_in_parallel_when_document_is_visible(
        document,
        GC::create_function(heap(),
            [this, success_callback, error_callback, options, watch_id] {
                if (watch_id.has_value() &&
                    !m_watch_ids.contains(watch_id.value()))
                    return;
                acquire_a_position(
                    success_callback, error_callback, options, watch_id);
            }));
}
void Geolocation::run_in_parallel_when_document_is_visible(DOM::Document& document, GC::Ref<GC::Function<void()>> callback)
{
    // Run callback in parallel if the document is already visible.
    if (document.visibility_state_value() == HTML::VisibilityState::Visible) {
        Platform::EventLoopPlugin::the().deferred_invoke(callback);
        return;
    }

    // Run the callback as soon as the document becomes visible. If we time out, do not run the callback at all.
    auto document_observer = realm().create<DOM::DocumentObserver>(realm(), document);
    auto timeout_timer = Platform::Timer::create_single_shot(heap(), VISIBILITY_STATE_TIMEOUT_MS, {});
    m_timeout_timers.append(timeout_timer);
    auto clear_observer_and_timer = [this, document_observer, timeout_timer] {
        document_observer->set_document_visibility_state_observer({});
        timeout_timer->stop();
        m_timeout_timers.remove_first_matching([&](auto timer) { return timer == timeout_timer; });
    };
    timeout_timer->on_timeout = GC::create_function(heap(), [clear_observer_and_timer] {
        dbgln("Geolocation: Waiting for visibility state update timed out");
        clear_observer_and_timer();
    });

    document_observer->set_document_visibility_state_observer([clear_observer_and_timer, callback](HTML::VisibilityState state) {
        if (state == HTML::VisibilityState::Visible) {
            clear_observer_and_timer();
            callback->function()();
        }
    });
    timeout_timer->start();
}

}
