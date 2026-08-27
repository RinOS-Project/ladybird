/*
 * Copyright (c) 2022, Luke Wilde <lukew@serenityos.org>
 * Copyright (c) 2023, Aliaksandr Kalenik <kalenik.aliaksandr@gmail.com>
 *
 * SPDX-License-Identifier: BSD-2-Clause
 */

#ifndef AK_OS_RINOS
#    include <LibGfx/SkiaBackendContext.h>
#endif
#include <LibJS/Runtime/ArrayBuffer.h>
#include <LibJS/Runtime/TypedArray.h>
#include <LibWeb/Bindings/Intrinsics.h>
#include <LibWeb/HTML/EventLoop/EventLoop.h>
#include <LibWeb/Bindings/WebGLRenderingContextPrototype.h>
#include <LibWeb/HTML/HTMLCanvasElement.h>
#include <LibWeb/HTML/OffscreenCanvas.h>
#include <LibWeb/HTML/EventLoop/Task.h>
#include <LibWeb/Infra/Strings.h>
#include <LibWeb/Painting/Paintable.h>
#include <LibWeb/WebGL/EventNames.h>
#include <LibWeb/WebGL/OpenGLContext.h>
#include <LibWeb/WebGL/WebGLContextEvent.h>
#include <LibWeb/WebGL/WebGLRenderingContext.h>
#include <LibWeb/WebGL/WebGLShader.h>
#include <LibWeb/WebIDL/Buffers.h>

#ifdef AK_OS_RINOS
extern "C" {
#    include <ringl/ringl.h>
}
#else
#include <GLES2/gl2.h>
#include <GLES2/gl2ext.h>
#endif

namespace Web::WebGL {

GC_DEFINE_ALLOCATOR(WebGLRenderingContext);

// https://www.khronos.org/registry/webgl/specs/latest/1.0/#fire-a-webgl-context-event
bool fire_webgl_context_event(DOM::EventTarget& canvas, FlyString const& type, bool cancelable)
{
    // `webglcontextlost` is cancelable so script may opt in to recovery;
    // creation-error and restored notifications are observations only.
    // FIXME: Consider setting a status message.
    auto event = WebGLContextEvent::create(canvas.realm(), type, WebGLContextEventInit {});
    event->set_is_trusted(true);
    event->set_cancelable(cancelable);
    return canvas.dispatch_event(*event);
}

// https://www.khronos.org/registry/webgl/specs/latest/1.0/#fire-a-webgl-context-creation-error
void fire_webgl_context_creation_error(DOM::EventTarget& canvas)
{
    // 1. Fire a WebGL context event named "webglcontextcreationerror" at canvas, optionally with its statusMessage attribute set to a platform dependent string about the nature of the failure.
    fire_webgl_context_event(canvas, EventNames::webglcontextcreationerror);
}

JS::ThrowCompletionOr<GC::Ptr<WebGLRenderingContext>> WebGLRenderingContext::create(JS::Realm& realm, HTML::HTMLCanvasElement& canvas_element, JS::Value options)
{
    return create_impl(realm, WebGLCanvas { canvas_element }, options);
}

#ifdef AK_OS_RINOS
JS::ThrowCompletionOr<GC::Ptr<WebGLRenderingContext>> WebGLRenderingContext::create(JS::Realm& realm, HTML::OffscreenCanvas& canvas, JS::Value options)
{
    return create_impl(realm, WebGLCanvas { canvas }, options);
}
#endif

JS::ThrowCompletionOr<GC::Ptr<WebGLRenderingContext>> WebGLRenderingContext::create_impl(JS::Realm& realm, WebGLCanvas canvas, JS::Value options)
{
    auto context_attributes = TRY(convert_value_to_context_attributes_dictionary(realm.vm(), options));
    auto fire_creation_error = [&] {
        canvas.visit([](auto const& canvas) {
            fire_webgl_context_creation_error(*canvas);
        });
    };
#ifdef AK_OS_RINOS
    // RinGPU currently supplies a synchronous software BGRA target. Do not
    // claim multisampling, an opaque backing store, or desynchronized
    // presentation until those paths exist. A caller that explicitly rejects
    // a major performance caveat must not receive this software context.
    if (context_attributes.fail_if_major_performance_caveat) {
        fire_creation_error();
        return GC::Ptr<WebGLRenderingContext> { nullptr };
    }
    auto actual_context_attributes = context_attributes;
    actual_context_attributes.alpha = true;
    actual_context_attributes.antialias = false;
    actual_context_attributes.premultiplied_alpha = true;
    actual_context_attributes.desynchronized = false;
#else
    auto actual_context_attributes = context_attributes;
#endif

    OpenGLContext::DrawingBufferOptions context_options {
        .alpha = actual_context_attributes.alpha,
        .depth = actual_context_attributes.depth,
        .stencil = actual_context_attributes.stencil,
        .antialias = actual_context_attributes.antialias,
        .premultiplied_alpha = actual_context_attributes.premultiplied_alpha,
    };
#ifdef AK_OS_RINOS
    auto context = OpenGLContext::create(OpenGLContext::WebGLVersion::WebGL1, context_options);
#else
    auto skia_backend_context = Gfx::SkiaBackendContext::the();
    if (!skia_backend_context) {
        fire_creation_error();
        return GC::Ptr<WebGLRenderingContext> { nullptr };
    }
    auto context = OpenGLContext::create(*skia_backend_context, OpenGLContext::WebGLVersion::WebGL1, context_options);
#endif
    if (!context) {
        fire_creation_error();
        return GC::Ptr<WebGLRenderingContext> { nullptr };
    }

    auto size = canvas.visit(
        [](GC::Ref<HTML::HTMLCanvasElement> const& canvas) { return canvas->bitmap_size_for_canvas(1, 1); },
        [](GC::Ref<HTML::OffscreenCanvas> const& canvas) { return canvas->bitmap_size_for_canvas(); });
    // RinGL's caller-owned drawing target has no zero-sized representation.
    // Keep a 1x1 physical target for zero-sized OffscreenCanvas instances;
    // canvas_size() continues to expose the original logical dimensions and
    // OffscreenCanvas refuses to snapshot the physical padding.
    context->set_size({ max(size.width(), 1), max(size.height(), 1) });
#ifdef AK_OS_RINOS
    if (canvas.has<GC::Ref<HTML::OffscreenCanvas>>()) {
        // Unlike a DOM canvas, an OffscreenCanvas has no compositor callback
        // to lazily realize its drawing buffer. Materialize the RinGL surface
        // now so bitmap consumers observe initialized transparent pixels.
        context->make_current();
        if (!context->rin_gl_is_ready()) {
            fire_creation_error();
            return GC::Ptr<WebGLRenderingContext> { nullptr };
        }
    }
#endif

    return realm.create<WebGLRenderingContext>(realm, move(canvas), context.release_nonnull(), context_attributes, actual_context_attributes);
}

WebGLRenderingContext::WebGLRenderingContext(JS::Realm& realm, WebGLCanvas canvas, NonnullOwnPtr<OpenGLContext> context, WebGLContextAttributes context_creation_parameters, WebGLContextAttributes actual_context_parameters)
    : WebGLRenderingContextOverloads(realm, move(context))
    , m_canvas(move(canvas))
    , m_context_creation_parameters(context_creation_parameters)
    , m_actual_context_parameters(actual_context_parameters)
{
}

WebGLRenderingContext::~WebGLRenderingContext() = default;

void WebGLRenderingContext::initialize(JS::Realm& realm)
{
    WEB_SET_PROTOTYPE_FOR_INTERFACE(WebGLRenderingContext);
    Base::initialize(realm);
}

void WebGLRenderingContext::visit_edges(Cell::Visitor& visitor)
{
    Base::visit_edges(visitor);
    WebGLRenderingContextImpl::visit_edges(visitor);
    m_canvas.visit([&](auto const& canvas) {
        visitor.visit(canvas);
    });
}

void WebGLRenderingContext::present()
{
    context().present(m_context_creation_parameters.preserve_drawing_buffer);
#ifdef AK_OS_RINOS
    if (context().is_context_lost())
        report_context_loss();
#endif
}

JS::Object const* WebGLRenderingContext::canvas_for_binding() const
{
    return m_canvas.visit([](auto const& canvas) -> JS::Object const* {
        return canvas.ptr();
    });
}

Gfx::IntSize WebGLRenderingContext::canvas_size() const
{
    return m_canvas.visit(
        [](GC::Ref<HTML::HTMLCanvasElement> const& canvas) { return canvas->bitmap_size_for_canvas(1, 1); },
        [](GC::Ref<HTML::OffscreenCanvas> const& canvas) { return canvas->bitmap_size_for_canvas(); });
}

void WebGLRenderingContext::notify_canvas_needs_present()
{
    m_canvas.visit(
        [](GC::Ref<HTML::HTMLCanvasElement> const& canvas) {
            canvas->set_canvas_content_dirty();
            canvas->set_needs_repaint();
        },
        [](GC::Ref<HTML::OffscreenCanvas> const&) {
            // OffscreenCanvas has no compositor. Its RinGL surface is snapped
            // only by its explicit bitmap-consuming APIs.
        });
}

void WebGLRenderingContext::needs_to_present()
{
    notify_canvas_needs_present();
}

#ifdef AK_OS_RINOS
void WebGLRenderingContext::release_drawing_buffer_after_compositing()
{
    context().release_drawing_buffer_after_compositing();
    if (context().is_context_lost())
        report_context_loss();
}

void WebGLRenderingContext::report_context_loss() const
{
    if (m_context_lost)
        return;

    // The native RinGL/RinGPU loss condition becomes the WebGL-visible lost
    // flag before script observes the event. That makes re-entrant event
    // handlers safe: every subsequent command sees the same lost context.
    m_context_lost = true;
    // A cancelled event is the sole condition that makes this context
    // restorable. WEBGL_lose_context additionally requires an explicit
    // restoreContext() after event dispatch has completed.
    m_context_restore_eligible = !m_canvas.visit([](auto const& canvas) {
        return fire_webgl_context_event(*canvas, EventNames::webglcontextlost, true);
    });
    if (m_context_restore_eligible && !m_context_lost_by_extension)
        queue_context_restore();
}

void WebGLRenderingContext::lose_context_from_extension()
{
    if (m_context_lost) {
        set_error_without_backend_check(RINGL_INVALID_OPERATION);
        return;
    }

    // A native loss might have been discovered between WebGL calls. Publish
    // it before rejecting the duplicate extension request, so the canvas lost
    // event cannot be skipped by this call.
    if (context().is_context_lost()) {
        report_context_loss();
        set_error_without_backend_check(RINGL_INVALID_OPERATION);
        return;
    }

    m_context_lost_by_extension = true;
    m_context_restore_eligible = false;
    m_context_restore_requested = false;
    context().lose_context();
    report_context_loss();
}

void WebGLRenderingContext::restore_context_from_extension()
{
    // A restore request is valid only for this extension's cancelled loss and
    // only after the lost-event dispatch. It remains lost until the queued
    // task has installed a fully initialized replacement RinGL context.
    if (!m_context_lost || !m_context_lost_by_extension || !m_context_restore_eligible
        || m_context_restore_requested || m_context_restore_pending) {
        set_error_without_backend_check(RINGL_INVALID_OPERATION);
        return;
    }

    m_context_restore_requested = true;
    queue_context_restore();
}

void WebGLRenderingContext::queue_context_restore() const
{
    if (m_context_restore_pending)
        return;
    m_context_restore_pending = true;
    m_canvas.visit(
        [this](GC::Ref<HTML::HTMLCanvasElement> const& canvas) {
            canvas->queue_an_element_task(HTML::Task::Source::DOMManipulation, [this] {
                const_cast<WebGLRenderingContext*>(this)->restore_context_after_loss();
            });
        },
        [this](GC::Ref<HTML::OffscreenCanvas> const& canvas) {
            HTML::queue_global_task(HTML::Task::Source::DOMManipulation, HTML::relevant_global_object(*canvas), GC::create_function(heap(), [this] {
                const_cast<WebGLRenderingContext*>(this)->restore_context_after_loss();
            }));
        });
}

void WebGLRenderingContext::restore_context_after_loss()
{
    OpenGLContext::DrawingBufferOptions context_options {
        .alpha = m_actual_context_parameters.alpha,
        .depth = m_actual_context_parameters.depth,
        .stencil = m_actual_context_parameters.stencil,
        .antialias = m_actual_context_parameters.antialias,
        .premultiplied_alpha = m_actual_context_parameters.premultiplied_alpha,
    };
    auto replacement = OpenGLContext::create(OpenGLContext::WebGLVersion::WebGL1, context_options);

    m_context_restore_pending = false;
    if (!m_context_lost || !replacement) {
        m_context_restore_requested = false;
        return;
    }

    // RinGL's caller-owned target has no zero-sized representation. This is
    // the same physical-size normalization used while initially creating an
    // OffscreenCanvas WebGL context; keep the canvas' logical 0×N/N×0 size
    // observable, but do not make a cancelled loss permanently unrestorable
    // merely because restoration happens while it has a zero dimension.
    auto replacement_size = canvas_size();
    replacement->set_size({ max(replacement_size.width(), 1), max(replacement_size.height(), 1) });
    replacement->make_current();
    if (!replacement->rin_gl_is_ready()) {
        m_context_restore_requested = false;
        return;
    }
    if (!restore_rin_gl_context(replacement.release_nonnull())) {
        m_context_restore_requested = false;
        return;
    }

    m_context_lost = false;
    m_context_lost_by_extension = false;
    m_context_restore_eligible = false;
    m_context_restore_requested = false;
    notify_canvas_needs_present();
    m_canvas.visit([](auto const& canvas) {
        fire_webgl_context_event(*canvas, EventNames::webglcontextrestored);
    });
}
#endif

bool WebGLRenderingContext::is_context_lost() const
{
    dbgln_if(WEBGL_CONTEXT_DEBUG, "WebGLRenderingContext::is_context_lost()");
#ifdef AK_OS_RINOS
    if (m_context_lost)
        return true;
    if (!context().is_context_lost())
        return false;
    report_context_loss();
    return true;
#else
    return m_context_lost;
#endif
}

Optional<WebGLContextAttributes> WebGLRenderingContext::get_context_attributes()
{
    if (is_context_lost())
        return {};
    return m_actual_context_parameters;
}

void WebGLRenderingContext::set_size(Gfx::IntSize const& size)
{
    Gfx::IntSize final_size;
    final_size.set_width(max(size.width(), 1));
    final_size.set_height(max(size.height(), 1));
    context().set_size(final_size);
}

void WebGLRenderingContext::reset_to_default_state()
{
}

RefPtr<Gfx::PaintingSurface> WebGLRenderingContext::surface()
{
    return context().surface();
}

void WebGLRenderingContext::allocate_painting_surface_if_needed()
{
    context().allocate_painting_surface_if_needed();
}

WebIDL::Long WebGLRenderingContext::drawing_buffer_width() const
{
    auto size = canvas_size();
    return size.width();
}

WebIDL::Long WebGLRenderingContext::drawing_buffer_height() const
{
    auto size = canvas_size();
    return size.height();
}

WebIDL::UnsignedLong WebGLRenderingContext::drawing_buffer_format() const
{
    // The return value describes the actual drawing buffer. On RinOS the
    // RinGL creation path deliberately forces alpha on, so a request for an
    // opaque buffer still reports RGBA8 instead of claiming RGB8.
    return m_actual_context_parameters.alpha ? 0x8058 : 0x8051;
}

}
