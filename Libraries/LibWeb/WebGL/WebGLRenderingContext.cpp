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
#include <LibWeb/Bindings/WebGLRenderingContextPrototype.h>
#include <LibWeb/HTML/HTMLCanvasElement.h>
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
bool fire_webgl_context_event(HTML::HTMLCanvasElement& canvas_element, FlyString const& type, bool cancelable)
{
    // `webglcontextlost` is cancelable so script may opt in to recovery;
    // creation-error and restored notifications are observations only.
    // FIXME: Consider setting a status message.
    auto event = WebGLContextEvent::create(canvas_element.realm(), type, WebGLContextEventInit {});
    event->set_is_trusted(true);
    event->set_cancelable(cancelable);
    return canvas_element.dispatch_event(*event);
}

// https://www.khronos.org/registry/webgl/specs/latest/1.0/#fire-a-webgl-context-creation-error
void fire_webgl_context_creation_error(HTML::HTMLCanvasElement& canvas_element)
{
    // 1. Fire a WebGL context event named "webglcontextcreationerror" at canvas, optionally with its statusMessage attribute set to a platform dependent string about the nature of the failure.
    fire_webgl_context_event(canvas_element, EventNames::webglcontextcreationerror);
}

JS::ThrowCompletionOr<GC::Ptr<WebGLRenderingContext>> WebGLRenderingContext::create(JS::Realm& realm, HTML::HTMLCanvasElement& canvas_element, JS::Value options)
{
    // We should be coming here from getContext being called on a wrapped <canvas> element.
    auto context_attributes = TRY(convert_value_to_context_attributes_dictionary(canvas_element.vm(), options));
#ifdef AK_OS_RINOS
    // RinGPU currently supplies a synchronous software BGRA target. Do not
    // claim multisampling, an opaque backing store, or desynchronized
    // presentation until those paths exist. A caller that explicitly rejects
    // a major performance caveat must not receive this software context.
    if (context_attributes.fail_if_major_performance_caveat) {
        fire_webgl_context_creation_error(canvas_element);
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
        .depth = actual_context_attributes.depth,
        .stencil = actual_context_attributes.stencil,
        .antialias = actual_context_attributes.antialias,
    };
#ifdef AK_OS_RINOS
    auto context = OpenGLContext::create(OpenGLContext::WebGLVersion::WebGL1, context_options);
#else
    auto skia_backend_context = Gfx::SkiaBackendContext::the();
    if (!skia_backend_context) {
        fire_webgl_context_creation_error(canvas_element);
        return GC::Ptr<WebGLRenderingContext> { nullptr };
    }
    auto context = OpenGLContext::create(*skia_backend_context, OpenGLContext::WebGLVersion::WebGL1, context_options);
#endif
    if (!context) {
        fire_webgl_context_creation_error(canvas_element);
        return GC::Ptr<WebGLRenderingContext> { nullptr };
    }

    context->set_size(canvas_element.bitmap_size_for_canvas(1, 1));

    return realm.create<WebGLRenderingContext>(realm, canvas_element, context.release_nonnull(), context_attributes, actual_context_attributes);
}

WebGLRenderingContext::WebGLRenderingContext(JS::Realm& realm, HTML::HTMLCanvasElement& canvas_element, NonnullOwnPtr<OpenGLContext> context, WebGLContextAttributes context_creation_parameters, WebGLContextAttributes actual_context_parameters)
    : WebGLRenderingContextOverloads(realm, move(context))
    , m_canvas_element(canvas_element)
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
    visitor.visit(m_canvas_element);
}

void WebGLRenderingContext::present()
{
    context().present(m_context_creation_parameters.preserve_drawing_buffer);
#ifdef AK_OS_RINOS
    if (context().is_context_lost())
        report_context_loss();
#endif
}

GC::Ref<HTML::HTMLCanvasElement> WebGLRenderingContext::canvas_for_binding() const
{
    return *m_canvas_element;
}

void WebGLRenderingContext::needs_to_present()
{
    m_canvas_element->set_canvas_content_dirty();

    m_canvas_element->set_needs_repaint();
}

#ifdef AK_OS_RINOS
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
    m_context_restore_eligible = !fire_webgl_context_event(*m_canvas_element, EventNames::webglcontextlost, true);
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
    m_canvas_element->queue_an_element_task(HTML::Task::Source::DOMManipulation, [this] {
        const_cast<WebGLRenderingContext*>(this)->restore_context_after_loss();
    });
}

void WebGLRenderingContext::restore_context_after_loss()
{
    OpenGLContext::DrawingBufferOptions context_options {
        .depth = m_actual_context_parameters.depth,
        .stencil = m_actual_context_parameters.stencil,
        .antialias = m_actual_context_parameters.antialias,
    };
    auto replacement = OpenGLContext::create(OpenGLContext::WebGLVersion::WebGL1, context_options);

    m_context_restore_pending = false;
    if (!m_context_lost || !replacement) {
        m_context_restore_requested = false;
        return;
    }

    replacement->set_size(m_canvas_element->bitmap_size_for_canvas(1, 1));
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
    m_canvas_element->set_canvas_content_dirty();
    m_canvas_element->set_needs_repaint();
    fire_webgl_context_event(*m_canvas_element, EventNames::webglcontextrestored);
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
    auto size = canvas_for_binding()->bitmap_size_for_canvas();
    return size.width();
}

WebIDL::Long WebGLRenderingContext::drawing_buffer_height() const
{
    auto size = canvas_for_binding()->bitmap_size_for_canvas();
    return size.height();
}

}
