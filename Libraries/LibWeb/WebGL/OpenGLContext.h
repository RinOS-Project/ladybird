/*
 * Copyright (c) 2024, Aliaksandr Kalenik <kalenik.aliaksandr@gmail.com>
 *
 * SPDX-License-Identifier: BSD-2-Clause
 */

#pragma once

#include <AK/NonnullOwnPtr.h>
#include <AK/NonnullRefPtr.h>
#include <AK/RefPtr.h>
#include <AK/Vector.h>
#include <LibGfx/Forward.h>
#include <LibGfx/Size.h>
#include <LibWeb/Export.h>

namespace Web::WebGL {

class WEB_API OpenGLContext {
public:
    enum class WebGLVersion {
        WebGL1,
        WebGL2,
    };

    struct DrawingBufferOptions {
        bool depth;
        bool stencil;
        bool antialias;
    };

#ifdef AK_OS_RINOS
    static OwnPtr<OpenGLContext> create(WebGLVersion, DrawingBufferOptions);
#else
    static OwnPtr<OpenGLContext> create(NonnullRefPtr<Gfx::SkiaBackendContext>, WebGLVersion, DrawingBufferOptions);
#endif

    void notify_content_will_change();
    void clear_buffer_to_default_values();
    void allocate_painting_surface_if_needed();

    struct Impl;
#ifdef AK_OS_RINOS
    OpenGLContext(Impl, WebGLVersion, DrawingBufferOptions);
#else
    OpenGLContext(NonnullRefPtr<Gfx::SkiaBackendContext>, Impl, WebGLVersion, DrawingBufferOptions);
#endif

    ~OpenGLContext();

    void make_current();

#ifdef AK_OS_RINOS
    bool is_context_lost() const;
    // Deliberately retire the RinGL context and its private drawing surface.
    // WEBGL_lose_context recovery always creates a fresh native context.
    void lose_context();
    bool rin_gl_is_ready() const;
    u32 rin_gl_get_error();
    // OES_texture_float_linear enables a context-local RinGL capability only
    // after JavaScript has acquired the extension object.
    void enable_rin_gl_float_texture_linear();
    // WEBGL_color_buffer_float enables unclamped blendColor state only after
    // its WebGL extension object has been acquired.
    void enable_rin_gl_float_color_buffer();
    // EXT_blend_minmax enables MIN/MAX equations only after its WebGL
    // extension object has been acquired.
    void enable_rin_gl_blend_minmax();
    u64 rin_gl_get_shader_source_length(u32 shader);
    u64 rin_gl_copy_shader_source(u32 shader, char* buffer, u64 buffer_size);
    // HTMLCanvasElement calls this only after it has taken the immutable
    // compositor snapshot of a successfully presented drawing buffer.
    void release_drawing_buffer_after_compositing();
#endif

    void present(bool preserve_drawing_buffer);

    void set_size(Gfx::IntSize const&);

    RefPtr<Gfx::PaintingSurface> surface();

    u32 default_framebuffer() const;
    u32 default_renderbuffer() const;

    Vector<String> get_supported_opengl_extensions();
    void request_extension(char const* extension_name);

    WebGLVersion webgl_version() const { return m_webgl_version; }

private:
#ifndef AK_OS_RINOS
    NonnullRefPtr<Gfx::SkiaBackendContext> m_skia_backend_context;
#endif
    Gfx::IntSize m_size;
    RefPtr<Gfx::PaintingSurface> m_painting_surface;
    NonnullOwnPtr<Impl> m_impl;
    Optional<Vector<String>> m_requestable_extensions;
    WebGLVersion m_webgl_version;
    [[maybe_unused]] DrawingBufferOptions m_drawing_buffer_options;

    void free_surface_resources();
#ifdef AK_OS_RINOS
    // Preserve the WebGL distinction between a native device loss and a
    // failed drawing-surface realization. Both stop rendering safely, but
    // only the former may publish CONTEXT_LOST_WEBGL to script.
    void fail_rin_gl_surface(int result);
#endif
#if defined(AK_OS_MACOS)
    void allocate_iosurface_painting_surface();
#elif defined(USE_VULKAN_IMAGES)
    void allocate_vkimage_painting_surface();
#endif
};

}
