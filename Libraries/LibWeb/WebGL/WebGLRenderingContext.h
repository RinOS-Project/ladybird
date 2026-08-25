/*
 * Copyright (c) 2022, Luke Wilde <lukew@serenityos.org>
 * Copyright (c) 2024, Aliaksandr Kalenik <kalenik.aliaksandr@gmail.com>
 *
 * SPDX-License-Identifier: BSD-2-Clause
 */

#pragma once

#include <LibGC/Ptr.h>
#include <LibWeb/Bindings/PlatformObject.h>
#include <LibWeb/Forward.h>
#include <LibWeb/WebGL/Types.h>
#include <LibWeb/WebGL/WebGLContextAttributes.h>
#include <LibWeb/WebGL/WebGLRenderingContextOverloads.h>

namespace Web::WebGL {

class WebGLRenderingContext final : public WebGLRenderingContextOverloads {
    WEB_PLATFORM_OBJECT(WebGLRenderingContext, WebGLRenderingContextOverloads);
    GC_DECLARE_ALLOCATOR(WebGLRenderingContext);

public:
    static JS::ThrowCompletionOr<GC::Ptr<WebGLRenderingContext>> create(JS::Realm&, HTML::HTMLCanvasElement& canvas_element, JS::Value options);

    virtual ~WebGLRenderingContext() override;

    void present() override;
    void needs_to_present() override;
#ifdef AK_OS_RINOS
    void report_context_loss() const override;
    void lose_context_from_extension() override;
    void restore_context_from_extension() override;
    void release_drawing_buffer_after_compositing();
#endif

    GC::Ref<HTML::HTMLCanvasElement> canvas_for_binding() const;

    bool is_context_lost() const;
    Optional<WebGLContextAttributes> get_context_attributes();

    RefPtr<Gfx::PaintingSurface> surface();
    void allocate_painting_surface_if_needed();

    void set_size(Gfx::IntSize const&);
    void reset_to_default_state();

    WebIDL::Long drawing_buffer_width() const;
    WebIDL::Long drawing_buffer_height() const;
    WebIDL::UnsignedLong drawing_buffer_format() const;

private:
    virtual void initialize(JS::Realm&) override;

    WebGLRenderingContext(JS::Realm&, HTML::HTMLCanvasElement&, NonnullOwnPtr<OpenGLContext> context, WebGLContextAttributes context_creation_parameters, WebGLContextAttributes actual_context_parameters);

#ifdef AK_OS_RINOS
    void queue_context_restore() const;
    void restore_context_after_loss();
#endif

    virtual void visit_edges(Cell::Visitor&) override;

    GC::Ref<HTML::HTMLCanvasElement> m_canvas_element;

    // https://www.khronos.org/registry/webgl/specs/latest/1.0/#context-creation-parameters
    // Each WebGLRenderingContext has context creation parameters, set upon creation, in a WebGLContextAttributes object.
    WebGLContextAttributes m_context_creation_parameters {};

    // https://www.khronos.org/registry/webgl/specs/latest/1.0/#actual-context-parameters
    // Each WebGLRenderingContext has actual context parameters, set each time the drawing buffer is created, in a WebGLContextAttributes object.
    WebGLContextAttributes m_actual_context_parameters {};

    // https://www.khronos.org/registry/webgl/specs/latest/1.0/#webgl-context-lost-flag
    // Each WebGLRenderingContext has a webgl context lost flag, which is initially unset.
    mutable bool m_context_lost { false };
#ifdef AK_OS_RINOS
    mutable bool m_context_restore_pending { false };
    // A WEBGL_lose_context loss becomes restorable only when its lost event
    // was cancelled and script subsequently requests restoration. Native loss
    // keeps the browser's automatic recovery policy.
    mutable bool m_context_lost_by_extension { false };
    mutable bool m_context_restore_eligible { false };
    mutable bool m_context_restore_requested { false };
#endif
};

// Returns false only when a requested cancelable event was cancelled by
// script. `webglcontextlost` is the only event in this embedding that permits
// context restoration.
bool fire_webgl_context_event(HTML::HTMLCanvasElement& canvas_element, FlyString const& type, bool cancelable = false);
void fire_webgl_context_creation_error(HTML::HTMLCanvasElement& canvas_element);

}
