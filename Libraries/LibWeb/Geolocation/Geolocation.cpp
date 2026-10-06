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
    // 1. If watchId was passed and this's [[watchIDs]] does not contain watchId, terminate this algorithm.
    if (watch_id.has_value() && !m_watch_ids.contains(watch_id.value()))
        return;
    auto& document = as<HTML::Window>(
        HTML::relevant_global_object(*this)).associated_document();

    // 2. Let acquisitionTime be a new EpochTimeStamp that represents now.
    [[maybe_unused]] HighResolutionTime::EpochTimeStamp const acquisition_time = AK::UnixDateTime::now().milliseconds_since_epoch();

    // 3. Let timeoutTime be the sum of acquisitionTime and options.timeout.
    [[maybe_unused]] HighResolutionTime::EpochTimeStamp const timeout_time = acquisition_time + options.timeout;

    // 4. Let cachedPosition be this's [[cachedPosition]].
    [[maybe_unused]] auto cached_position = m_cached_position;

    // FIXME: 5. Create an implementation-specific timeout task that elapses at timeoutTime, during which it tries to acquire
    //    the device's position by running the following steps:
    {
#if defined(AK_OS_RINOS)
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

        GC::Weak<DOM::Document> pending_document { document };
        if (!pending_document || !pending_document->is_fully_active())
            return;
        pending_document->page().client().page_did_request_geolocation(
            GC::create_function(heap(),
                [this, pending_document, success_callback, error_callback,
                 options, watch_id](Web::GeolocationPositionResult result) {
                                if (!pending_document ||
                                    !pending_document->is_fully_active() ||
                                    !pending_document->is_allowed_to_use_feature(
                                        DOM::PolicyControlledFeature::Geolocation))
                                    return;
                                if (watch_id.has_value() &&
                                    !m_watch_ids.contains(watch_id.value()))
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
                                if (result.status !=
                                    Web::GeolocationPositionResult::Status::Available) {
                                    call_back_with_error(
                                        error_callback,
                                        GeolocationPositionError::ErrorCode::PositionUnavailable);
                                    if (watch_id.has_value())
                                        schedule_watch_position(
                                            success_callback, error_callback,
                                            options, watch_id.value());
                                    return;
                                }

                                CoordinatesData coordinates;
                                coordinates.latitude =
                                    static_cast<double>(result.latitude_e7) /
                                    10000000.0;
                                coordinates.longitude =
                                    static_cast<double>(result.longitude_e7) /
                                    10000000.0;
                                coordinates.accuracy =
                                    static_cast<double>(result.horizontal_accuracy_mm) /
                                    1000.0;
                                if ((result.flags &
                                     Web::GeolocationPositionResult::ValidAltitude) != 0u) {
                                    coordinates.altitude = Optional<double> {
                                        static_cast<double>(result.altitude_mm) /
                                        1000.0 };
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
                                HTML::queue_a_task(
                                    HTML::Task::Source::Geolocation, nullptr,
                                    nullptr, GC::create_function(heap(),
                                        [success_callback, position] {
                                            (void)WebIDL::invoke_callback(
                                                success_callback, {},
                                                WebIDL::ExceptionBehavior::Report,
                                                { { position } });
                                        }));
                                if (watch_id.has_value())
                                    schedule_watch_position(
                                        success_callback, error_callback,
                                        options, watch_id.value());
                }),
            *pending_document);
        return;
#else
        // FIXME: 1. Let permission be get the current permission state of "geolocation".

        if (!document.is_allowed_to_use_feature(
                DOM::PolicyControlledFeature::Geolocation)) {
            if (watch_id.has_value())
                m_watch_ids.remove(watch_id.value());
            call_back_with_error(
                error_callback,
                GeolocationPositionError::ErrorCode::PermissionDenied);
            return;
        }

        // FIXME: Consult the non-RinOS site permission owner before using
        // cached or emulated positions.
        {
            // 1. Check if an emulated position should be used by running the following steps:
            {
                // 1. Let emulatedPositionData be get emulated position data passing this.
                auto emulated_position_data = get_emulated_position_data();

                // 2. If emulatedPositionData is not null:
                if (!emulated_position_data.has<Empty>()) {
                    // 1. If emulatedPositionData is a GeolocationPositionError:
                    if (emulated_position_data.has<GeolocationPositionError::ErrorCode>()) {
                        // 1. Call back with error passing errorCallback and emulatedPositionData.
                        // FIXME: We pass along the code instead of the entire error object. Spec issue:
                        //        https://github.com/w3c/geolocation/issues/186
                        call_back_with_error(error_callback, emulated_position_data.get<GeolocationPositionError::ErrorCode>());

                        // 2. Terminate this algorithm.
                        return;
                    }

                    // 2. Let position be a new GeolocationPosition passing emulatedPositionData, acquisitionTime and
                    //    options.enableHighAccuracy.
                    auto position = realm().create<GeolocationPosition>(realm(),
                        emulated_position_data.get<GC::Ref<GeolocationCoordinates>>(),
                        acquisition_time,
                        options.enable_high_accuracy);

                    // 3. Queue a task on the geolocation task source with a step that invokes successCallback with
                    //    « position » and "report".
                    HTML::queue_a_task(HTML::Task::Source::Geolocation, nullptr, nullptr, GC::create_function(heap(), [success_callback, position] {
                        (void)WebIDL::invoke_callback(success_callback, {}, WebIDL::ExceptionBehavior::Report, { { position } });
                    }));

                    // 4. Terminate this algorithm.
                    return;
                }
            }

            // 3. If cachedPosition is not null, and options.maximumAge is greater than 0:
            if (cached_position && options.maximum_age > 0) {
                // 1. Let cacheTime be acquisitionTime minus the value of the options.maximumAge member.
                HighResolutionTime::EpochTimeStamp const cache_time = acquisition_time - options.maximum_age;

                // 2. If cachedPosition's timestamp's value is greater than cacheTime, and
                //    cachedPosition.[[isHighAccuracy]] equals options.enableHighAccuracy:
                if (cached_position->timestamp() > cache_time
                    && cached_position->is_high_accuracy() == options.enable_high_accuracy) {
                    // 1. Queue a task on the geolocation task source with a step that invokes successCallback with
                    //    « cachedPosition » and "report".
                    HTML::queue_a_task(HTML::Task::Source::Geolocation, nullptr, nullptr, GC::create_function(heap(), [success_callback, cached_position] {
                        (void)WebIDL::invoke_callback(success_callback, {}, WebIDL::ExceptionBehavior::Report, { { cached_position } });
                    }));

                    // 2. Terminate this algorithm.
                    return;
                }
            }

            // This embedding has no platform position owner yet. Do not turn
            // the default-initialized coordinates into a successful fix.
            call_back_with_error(
                error_callback,
                GeolocationPositionError::ErrorCode::PositionUnavailable);
        }
#endif
    }
}

// https://w3c.github.io/geolocation/#dfn-call-back-with-error
void Geolocation::call_back_with_error(GC::Ptr<WebIDL::CallbackType> callback, GeolocationPositionError::ErrorCode code) const
{
    // 1. If callback is null, return.
    if (!callback)
        return;

    // 2. Let error be a newly created GeolocationPositionError instance whose code attribute is initialized to code.
    auto error = realm().create<GeolocationPositionError>(realm(), code);

    // 3. Queue a task on the geolocation task source with a step that invokes callback with « error » and "report".
    HTML::queue_a_task(HTML::Task::Source::Geolocation, nullptr, nullptr, GC::create_function(heap(), [callback, error] {
        (void)WebIDL::invoke_callback(*callback, {}, WebIDL::ExceptionBehavior::Report,
            { { error } });
    }));
}

// https://w3c.github.io/geolocation/#dfn-get-emulated-position-data
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
    // 1. Let watchIDs be geolocation's [[watchIDs]].

    // 2. Let document be the geolocation's relevant global object's associated Document.
    [[maybe_unused]] auto& document = as<HTML::Window>(HTML::relevant_global_object(*this)).associated_document();

    // FIXME: 3. If document is not allowed to use the "geolocation" feature:
#if defined(AK_OS_RINOS)
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

    run_in_parallel_when_document_is_visible(document,
        GC::create_function(heap(),
            [this, success_callback, error_callback, options, watch_id] {
                if (watch_id.has_value() &&
                    !m_watch_ids.contains(watch_id.value()))
                    return;
                acquire_a_position(success_callback, error_callback, options,
                                   watch_id);
            }));
    return;
#else
    if (!document.is_allowed_to_use_feature(
            DOM::PolicyControlledFeature::Geolocation)) {
        // 1. If watchId was passed, remove watchId from watchIDs.
        if (watch_id.has_value())
            m_watch_ids.remove(watch_id.value());

        // 2. Call back with error passing errorCallback and PERMISSION_DENIED.
        call_back_with_error(error_callback, GeolocationPositionError::ErrorCode::PermissionDenied);

        // 3. Terminate this algorithm.
        return;
    }

    // 4. If geolocation's environment settings object is a non-secure context:
    if (is_non_secure_context(HTML::relevant_settings_object(*this))) {
        // 1. If watchId was passed, remove watchId from watchIDs.
        if (watch_id.has_value())
            m_watch_ids.remove(watch_id.value());

        // 2. Call back with error passing errorCallback and PERMISSION_DENIED.
        call_back_with_error(error_callback, GeolocationPositionError::ErrorCode::PermissionDenied);

        // 3. Terminate this algorithm.
        return;
    }

    // 5. If document's visibility state is "hidden", wait for the following page visibility change steps to run:
    run_in_parallel_when_document_is_visible(document, GC::create_function(heap(), [this, watch_id, success_callback, error_callback, options] {
        // 1. Assert: document's visibility state is "visible".
        // 2. Continue to the next steps below.
        // AD-HOC: This is implemented by run_in_parallel_when_document_is_visible().

        // FIXME: 6. Let descriptor be a new PermissionDescriptor whose name is "geolocation".

        // 7. In parallel:
        // AD-HOC: run_in_parallel_when_document_is_visible() already runs this in parallel.
        {
            // FIXME: 1. Set permission to request permission to use descriptor.

            // FIXME: 2. If permission is "denied", then:
            if (false) {
                // 1. If watchId was passed, remove watchId from watchIDs.
                if (watch_id.has_value())
                    m_watch_ids.remove(watch_id.value());

                // 2. Call back with error passing errorCallback and PERMISSION_DENIED.
                call_back_with_error(error_callback, GeolocationPositionError::ErrorCode::PermissionDenied);

                // 3. Terminate this algorithm.
                return;
            }

            // FIXME: 3. Wait to acquire a position passing successCallback, errorCallback, options, and watchId.
            acquire_a_position(success_callback, error_callback, options, watch_id);

            // 4. If watchId was not passed, terminate this algorithm.
            if (!watch_id.has_value())
                return;

            // FIXME: 5. While watchIDs contains watchId:
            {
                // FIXME: 1. Wait for a significant change of geographic position. What constitutes a significant change of
                //    geographic position is left to the implementation. User agents MAY impose a rate limit on how
                //    frequently position changes are reported. User agents MUST consider invoking set emulated position
                //    data as a significant change.

                // FIXME: 2. If document is not fully active or visibility state is not "visible", go back to the previous step
                //    and again wait for a significant change of geographic position.

                // FIXME: 3. Wait to acquire a position passing successCallback, errorCallback, options, and watchId.
            }
        }
    }));
#endif
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
