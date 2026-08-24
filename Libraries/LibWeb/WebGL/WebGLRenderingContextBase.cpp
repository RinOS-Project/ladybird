/*
 * Copyright (c) 2024-2025, Aliaksandr Kalenik <kalenik.aliaksandr@gmail.com>
 * Copyright (c) 2024-2025, Luke Wilde <luke@ladybird.org>
 *
 * SPDX-License-Identifier: BSD-2-Clause
 */

#define GL_GLEXT_PROTOTYPES 1
#ifdef AK_OS_RINOS
extern "C" {
#    include <ringl/ringl.h>
}
#else
#include <GLES2/gl2.h>
#include <GLES2/gl2ext.h>
extern "C" {
#include <GLES2/gl2ext_angle.h>
}
#endif

#include <AK/Checked.h>
#include <LibGfx/Bitmap.h>
#include <LibGfx/ImmutableBitmap.h>
#ifndef AK_OS_RINOS
#include <LibGfx/SkiaUtils.h>
#endif
#include <LibWeb/HTML/HTMLCanvasElement.h>
#include <LibWeb/HTML/HTMLImageElement.h>
#include <LibWeb/HTML/HTMLVideoElement.h>
#include <LibWeb/HTML/ImageBitmap.h>
#include <LibWeb/HTML/ImageData.h>
#include <LibWeb/HTML/UniversalGlobalScope.h>
#include <LibWeb/WebGL/Extensions/ANGLEInstancedArrays.h>
#include <LibWeb/WebGL/Extensions/EXTBlendMinMax.h>
#ifndef AK_OS_RINOS
#include <LibWeb/WebGL/Extensions/EXTColorBufferFloat.h>
#include <LibWeb/WebGL/Extensions/EXTRenderSnorm.h>
#include <LibWeb/WebGL/Extensions/EXTTextureFilterAnisotropic.h>
#include <LibWeb/WebGL/Extensions/EXTTextureNorm16.h>
#include <LibWeb/WebGL/Extensions/OESStandardDerivatives.h>
#include <LibWeb/WebGL/Extensions/OESVertexArrayObject.h>
#include <LibWeb/WebGL/Extensions/WebGLCompressedTextureS3tc.h>
#include <LibWeb/WebGL/Extensions/WebGLCompressedTextureS3tcSrgb.h>
#include <LibWeb/WebGL/Extensions/WebGLDrawBuffers.h>
#endif
#include <LibWeb/WebGL/Extensions/OESElementIndexUint.h>
#ifdef AK_OS_RINOS
#include <LibWeb/WebGL/Extensions/EXTColorBufferHalfFloat.h>
#include <LibWeb/WebGL/Extensions/OESFboRenderMipmap.h>
#include <LibWeb/WebGL/Extensions/OESStandardDerivatives.h>
#include <LibWeb/WebGL/Extensions/OESTextureFloat.h>
#include <LibWeb/WebGL/Extensions/OESTextureFloatLinear.h>
#include <LibWeb/WebGL/Extensions/OESTextureHalfFloat.h>
#include <LibWeb/WebGL/Extensions/OESTextureHalfFloatLinear.h>
#include <LibWeb/WebGL/Extensions/OESVertexArrayObject.h>
#include <LibWeb/WebGL/Extensions/WebGLCompressedTextureEtc1.h>
#include <LibWeb/WebGL/Extensions/WebGLCompressedTextureS3tc.h>
#include <LibWeb/WebGL/Extensions/WebGLCompressedTextureS3tcSrgb.h>
#include <LibWeb/WebGL/Extensions/WebGLColorBufferFloat.h>
#include <LibWeb/WebGL/Extensions/WebGLDepthTexture.h>
#include <LibWeb/WebGL/Extensions/WebGLDrawBuffers.h>
#include <LibWeb/WebGL/Extensions/WebGLLoseContext.h>
#endif
#include <LibWeb/WebGL/OpenGLContext.h>
#include <LibWeb/WebGL/WebGLRenderingContextBase.h>

#ifndef AK_OS_RINOS
#include <core/SkCanvas.h>
#include <core/SkColorSpace.h>
#include <core/SkColorType.h>
#include <core/SkImage.h>
#include <core/SkPixmap.h>
#include <core/SkSurface.h>
#endif

namespace Web::WebGL {

#ifdef AK_OS_RINOS
static constexpr GLenum webgl_rgb = RINGL_RGB;
static constexpr GLenum webgl_rgba = RINGL_RGBA;
static constexpr GLenum webgl_alpha = RINGL_ALPHA;
static constexpr GLenum webgl_luminance = RINGL_LUMINANCE;
static constexpr GLenum webgl_luminance_alpha = RINGL_LUMINANCE_ALPHA;
static constexpr GLenum webgl_unsigned_byte = RINGL_UNSIGNED_BYTE;
static constexpr GLenum webgl_float = RINGL_FLOAT;
static constexpr GLenum webgl_half_float_oes = RINGL_HALF_FLOAT_OES;
static constexpr GLenum webgl_unsigned_short_5_6_5 = RINGL_UNSIGNED_SHORT_5_6_5;
static constexpr GLenum webgl_unsigned_short_4_4_4_4 = RINGL_UNSIGNED_SHORT_4_4_4_4;
static constexpr GLenum webgl_unsigned_short_5_5_5_1 = RINGL_UNSIGNED_SHORT_5_5_5_1;
#else
static constexpr GLenum webgl_rgb = GL_RGB;
static constexpr GLenum webgl_rgba = GL_RGBA;
static constexpr GLenum webgl_alpha = GL_ALPHA;
static constexpr GLenum webgl_luminance = GL_LUMINANCE;
static constexpr GLenum webgl_luminance_alpha = GL_LUMINANCE_ALPHA;
static constexpr GLenum webgl_unsigned_byte = GL_UNSIGNED_BYTE;
static constexpr GLenum webgl_unsigned_short_5_6_5 = GL_UNSIGNED_SHORT_5_6_5;
static constexpr GLenum webgl_unsigned_short_4_4_4_4 = GL_UNSIGNED_SHORT_4_4_4_4;
static constexpr GLenum webgl_unsigned_short_5_5_5_1 = GL_UNSIGNED_SHORT_5_5_5_1;
#endif

static constexpr Optional<Gfx::ExportFormat> determine_export_format(WebIDL::UnsignedLong format, WebIDL::UnsignedLong type)
{
    switch (format) {
    case webgl_rgb:
        switch (type) {
        case webgl_unsigned_byte:
            return Gfx::ExportFormat::RGB888;
        case webgl_unsigned_short_5_6_5:
            return Gfx::ExportFormat::RGB565;
        default:
            break;
        }
        break;
    case webgl_rgba:
        switch (type) {
        case webgl_unsigned_byte:
            return Gfx::ExportFormat::RGBA8888;
        case webgl_unsigned_short_4_4_4_4:
            // FIXME: This is not exactly the same as RGBA.
            return Gfx::ExportFormat::RGBA4444;
        case webgl_unsigned_short_5_5_5_1:
            return Gfx::ExportFormat::RGBA5551;
            break;
        default:
            break;
        }
        break;
    case webgl_alpha:
        switch (type) {
        case webgl_unsigned_byte:
            return Gfx::ExportFormat::Alpha8;
        default:
            break;
        }
        break;
    case webgl_luminance:
        switch (type) {
        case webgl_unsigned_byte:
            return Gfx::ExportFormat::Gray8;
        default:
            break;
        }
        break;
    default:
        break;
    }

    dbgln("WebGL: Unsupported format and type combination. format: 0x{:04x}, type: 0x{:04x}", format, type);
    return {};
}

#ifdef AK_OS_RINOS
/* DOM image sources are 8-bit bitmaps. Keep their existing color-managed,
 * flip, and premultiply path, then widen to Float32 or encode the resulting
 * normalized components as IEEE binary16 for the corresponding WebGL upload. */
static u16 unorm8_to_half_float_bits(u8 component)
{
    float normalized = static_cast<float>(component) / 255.0f;
    u32 bits {};
    u32 mantissa;
    u32 result;

    if (component == 0)
        return 0;
    memcpy(&bits, &normalized, sizeof(bits));
    mantissa = bits & 0x007fffffu;
    result = (((bits >> 23u) - 112u) << 10u) | (mantissa >> 13u);
    // Round to nearest-even at the binary16 mantissa boundary. Every nonzero
    // UNORM8 value is a normal binary16 value, so no subnormal branch is
    // required here.
    if ((mantissa & 0x1fffu) > 0x1000u
        || ((mantissa & 0x1fffu) == 0x1000u && (result & 1u) != 0u))
        ++result;
    return static_cast<u16>(result);
}

static Optional<Gfx::BitmapExportResult> export_floating_texture_image(Gfx::ImmutableBitmap const& bitmap, WebIDL::UnsignedLong format, WebIDL::UnsignedLong type, int export_flags, Optional<int> destination_width, Optional<int> destination_height)
{
    Gfx::ExportFormat primary_format;
    size_t component_count;
    bool needs_alpha_plane = false;

    switch (format) {
    case webgl_rgba:
        primary_format = Gfx::ExportFormat::RGBA8888;
        component_count = 4u;
        break;
    case webgl_rgb:
        primary_format = Gfx::ExportFormat::RGB888;
        component_count = 3u;
        break;
    case webgl_alpha:
        primary_format = Gfx::ExportFormat::Alpha8;
        component_count = 1u;
        break;
    case webgl_luminance:
        primary_format = Gfx::ExportFormat::Gray8;
        component_count = 1u;
        break;
    case webgl_luminance_alpha:
        primary_format = Gfx::ExportFormat::Gray8;
        component_count = 2u;
        needs_alpha_plane = true;
        break;
    default:
        return {};
    }

    auto primary_or_error = bitmap.export_to_byte_buffer(primary_format, export_flags, destination_width, destination_height);
    if (primary_or_error.is_error()) {
        dbgln("WebGL: Could not export floating texture source: {}", primary_or_error.release_error());
        return {};
    }
    auto primary = primary_or_error.release_value();
    if (primary.width < 0 || primary.height < 0)
        return {};

    Optional<Gfx::BitmapExportResult> alpha;
    if (needs_alpha_plane) {
        auto alpha_or_error = bitmap.export_to_byte_buffer(Gfx::ExportFormat::Alpha8, export_flags, destination_width, destination_height);
        if (alpha_or_error.is_error()) {
            dbgln("WebGL: Could not export floating texture alpha: {}", alpha_or_error.release_error());
            return {};
        }
        alpha = alpha_or_error.release_value();
        if (alpha->width != primary.width || alpha->height != primary.height)
            return {};
    }

    Checked<size_t> pixel_count = static_cast<size_t>(primary.width);
    pixel_count *= static_cast<size_t>(primary.height);
    size_t component_bytes;
    Checked<size_t> output_bytes = pixel_count;

    if (type == webgl_float)
        component_bytes = sizeof(float);
    else if (type == webgl_half_float_oes)
        component_bytes = sizeof(u16);
    else
        return {};
    output_bytes *= component_count;
    output_bytes *= component_bytes;
    if (pixel_count.has_overflow() || output_bytes.has_overflow())
        return {};
    auto pixel_count_value = pixel_count.value_unchecked();
    auto output_bytes_value = output_bytes.value_unchecked();
    auto converted_buffer_or_error = ByteBuffer::create_zeroed(output_bytes_value);
    if (converted_buffer_or_error.is_error())
        return {};
    auto converted_buffer = converted_buffer_or_error.release_value();
    if (primary.buffer.size() < pixel_count_value *
            (primary_format == Gfx::ExportFormat::RGBA8888 ? 4u
             : primary_format == Gfx::ExportFormat::RGB888 ? 3u : 1u) ||
        (alpha.has_value() && alpha->buffer.size() < pixel_count_value)) {
        return {};
    }

    for (size_t pixel = 0u; pixel < pixel_count_value; ++pixel) {
        size_t source_offset = pixel * (primary_format == Gfx::ExportFormat::RGBA8888 ? 4u
                                      : primary_format == Gfx::ExportFormat::RGB888 ? 3u : 1u);

        for (size_t component = 0u; component < component_count; ++component) {
            u8 source_component = needs_alpha_plane && component == 1u
                ? alpha->buffer.data()[pixel]
                : primary.buffer.data()[source_offset + component];
            auto destination = converted_buffer.data()
                + (pixel * component_count + component) * component_bytes;

            if (type == webgl_float) {
                float value = static_cast<float>(source_component) / 255.0f;
                memcpy(destination, &value, sizeof(value));
            } else {
                auto value = unorm8_to_half_float_bits(source_component);
                memcpy(destination, &value, sizeof(value));
            }
        }
    }
    return Gfx::BitmapExportResult {
        .buffer = move(converted_buffer),
        .width = primary.width,
        .height = primary.height,
    };
}
#endif

WebGLRenderingContextBase::WebGLRenderingContextBase(JS::Realm& realm)
    : Bindings::PlatformObject(realm)
{
}

#ifndef AK_OS_RINOS
struct Extension {
    Vector<StringView> required_angle_extensions;
    JS::ThrowCompletionOr<GC::Ref<JS::Object>> (*factory)(JS::Realm&, GC::Ref<WebGLRenderingContextBase>);
    Optional<OpenGLContext::WebGLVersion> only_for_webgl_version { OptionalNone {} };
};

static HashMap<String, Extension, AK::ASCIICaseInsensitiveStringTraits> s_available_webgl_extensions {
    // Khronos ratified WebGL Extensions
    { "ANGLE_instanced_arrays"_string, { { "GL_ANGLE_instanced_arrays"sv }, Extensions::ANGLEInstancedArrays::create, OpenGLContext::WebGLVersion::WebGL1 } },
    { "EXT_blend_minmax"_string, { { "GL_EXT_blend_minmax"sv }, Extensions::EXTBlendMinMax::create, OpenGLContext::WebGLVersion::WebGL1 } },
    { "EXT_frag_depth"_string, { { "GL_EXT_frag_depth"sv }, nullptr, OpenGLContext::WebGLVersion::WebGL1 } },
    { "EXT_shader_texture_lod"_string, { { "GL_EXT_shader_texture_lod"sv }, nullptr, OpenGLContext::WebGLVersion::WebGL1 } },
    { "EXT_texture_filter_anisotropic"_string, { { "GL_EXT_texture_filter_anisotropic"sv }, Extensions::EXTTextureFilterAnisotropic::create } },
    { "OES_element_index_uint"_string, { { "GL_OES_element_index_uint"sv }, Extensions::OESElementIndexUint::create, OpenGLContext::WebGLVersion::WebGL1 } },
    { "OES_standard_derivatives"_string, { { "GL_OES_standard_derivatives"sv }, Extensions::OESStandardDerivatives::create, OpenGLContext::WebGLVersion::WebGL1 } },
    { "OES_texture_float"_string, { { "GL_OES_texture_float"sv }, nullptr, OpenGLContext::WebGLVersion::WebGL1 } },
    { "OES_texture_float_linear"_string, { { "GL_OES_texture_float_linear"sv }, nullptr } },
    { "OES_texture_half_float"_string, { { "GL_OES_texture_half_float"sv }, nullptr, OpenGLContext::WebGLVersion::WebGL1 } },
    { "OES_texture_half_float_linear"_string, { { "GL_OES_texture_half_float_linear"sv }, nullptr, OpenGLContext::WebGLVersion::WebGL1 } },
    { "OES_vertex_array_object"_string, { { "GL_OES_vertex_array_object"sv }, Extensions::OESVertexArrayObject::create, OpenGLContext::WebGLVersion::WebGL1 } },
    { "WEBGL_compressed_texture_s3tc"_string, { { "GL_EXT_texture_compression_dxt1"sv, "GL_ANGLE_texture_compression_dxt3"sv, "GL_ANGLE_texture_compression_dxt5"sv }, Extensions::WebGLCompressedTextureS3tc::create } },
    { "WEBGL_debug_renderer_info"_string, { {}, nullptr } },
    { "WEBGL_debug_shaders"_string, { {}, nullptr } },
    { "WEBGL_depth_texture"_string, { { "GL_ANGLE_depth_texture"sv }, nullptr, OpenGLContext::WebGLVersion::WebGL1 } },
    { "WEBGL_draw_buffers"_string, { { "GL_EXT_draw_buffers"sv }, Extensions::WebGLDrawBuffers::create, OpenGLContext::WebGLVersion::WebGL1 } },
    { "WEBGL_lose_context"_string, { {}, nullptr } },

    // Community approved WebGL Extensions
    { "EXT_clip_control"_string, { { "GL_EXT_clip_control"sv }, nullptr } },
    { "EXT_color_buffer_float"_string, { { "GL_EXT_color_buffer_float"sv }, Extensions::EXTColorBufferFloat::create, OpenGLContext::WebGLVersion::WebGL2 } },
    { "EXT_color_buffer_half_float"_string, { { "GL_EXT_color_buffer_half_float"sv }, nullptr } },
    { "EXT_conservative_depth"_string, { { "GL_EXT_conservative_depth"sv }, nullptr, OpenGLContext::WebGLVersion::WebGL2 } },
    { "EXT_depth_clamp"_string, { { "GL_EXT_depth_clamp"sv }, nullptr } },
    { "EXT_disjoint_timer_query"_string, { { "GL_EXT_disjoint_timer_query"sv }, nullptr, OpenGLContext::WebGLVersion::WebGL1 } },
    { "EXT_disjoint_timer_query_webgl2"_string, { { "GL_EXT_disjoint_timer_query"sv }, nullptr, OpenGLContext::WebGLVersion::WebGL2 } },
    { "EXT_float_blend"_string, { { "GL_EXT_float_blend"sv }, nullptr } },
    { "EXT_polygon_offset_clamp"_string, { { "GL_EXT_polygon_offset_clamp"sv }, nullptr } },
    { "EXT_render_snorm"_string, { { "GL_EXT_render_snorm"sv }, Extensions::EXTRenderSnorm::create, OpenGLContext::WebGLVersion::WebGL2 } },
    { "EXT_sRGB"_string, { { "GL_EXT_sRGB"sv }, nullptr, OpenGLContext::WebGLVersion::WebGL1 } },
    { "EXT_texture_compression_bptc"_string, { { "GL_EXT_texture_compression_bptc"sv }, nullptr } },
    { "EXT_texture_compression_rgtc"_string, { { "GL_EXT_texture_compression_rgtc"sv }, nullptr } },
    { "EXT_texture_mirror_clamp_to_edge"_string, { { "GL_EXT_texture_mirror_clamp_to_edge"sv }, nullptr } },
    { "EXT_texture_norm16"_string, { { "GL_EXT_texture_norm16"sv }, Extensions::EXTTextureNorm16::create, OpenGLContext::WebGLVersion::WebGL2 } },
    { "KHR_parallel_shader_compile"_string, { { "GL_KHR_parallel_shader_compile"sv }, nullptr } },
    { "NV_shader_noperspective_interpolation"_string, { { "GL_NV_shader_noperspective_interpolation"sv }, nullptr, OpenGLContext::WebGLVersion::WebGL2 } },
    { "OES_draw_buffers_indexed"_string, { { "GL_OES_draw_buffers_indexed"sv }, nullptr } },
    { "OES_fbo_render_mipmap"_string, { { "GL_OES_fbo_render_mipmap"sv }, nullptr, OpenGLContext::WebGLVersion::WebGL1 } },
    { "OES_sample_variables"_string, { { "GL_OES_sample_variables"sv }, nullptr, OpenGLContext::WebGLVersion::WebGL2 } },
    { "OES_shader_multisample_interpolation"_string, { { "GL_OES_shader_multisample_interpolation"sv }, nullptr, OpenGLContext::WebGLVersion::WebGL2 } },
    { "OVR_multiview2"_string, { { "GL_OVR_multiview2"sv }, nullptr, OpenGLContext::WebGLVersion::WebGL2 } },
    { "WEBGL_blend_func_extended"_string, { { "GL_EXT_blend_func_extended"sv }, nullptr } },
    { "WEBGL_clip_cull_distance"_string, { { "GL_EXT_clip_cull_distance"sv }, nullptr, OpenGLContext::WebGLVersion::WebGL2 } },
    { "WEBGL_color_buffer_float"_string, { { "EXT_color_buffer_half_float"sv, "OES_texture_float"sv }, nullptr, OpenGLContext::WebGLVersion::WebGL1 } },
    { "WEBGL_compressed_texture_astc"_string, { { "KHR_texture_compression_astc_hdr"sv, "KHR_texture_compression_astc_ldr"sv }, nullptr } },
    { "WEBGL_compressed_texture_etc"_string, { { "GL_ANGLE_compressed_texture_etc"sv }, nullptr } },
    { "WEBGL_compressed_texture_etc1"_string, { { "GL_OES_compressed_ETC1_RGB8_texture"sv }, nullptr } },
    { "WEBGL_compressed_texture_pvrtc"_string, { { "GL_IMG_texture_compression_pvrtc"sv }, nullptr } },
    { "WEBGL_compressed_texture_s3tc_srgb"_string, { { "GL_EXT_texture_compression_s3tc_srgb"sv }, Extensions::WebGLCompressedTextureS3tcSrgb::create } },
    { "WEBGL_multi_draw"_string, { { "GL_ANGLE_multi_draw"sv }, nullptr } },
    { "WEBGL_polygon_mode"_string, { { "GL_ANGLE_polygon_mode"sv }, nullptr } },
    { "WEBGL_provoking_vertex"_string, { { "GL_ANGLE_provoking_vertex"sv }, nullptr, OpenGLContext::WebGLVersion::WebGL2 } },
    { "WEBGL_render_shared_exponent"_string, { { "GL_QCOM_render_shared_exponent"sv }, nullptr, OpenGLContext::WebGLVersion::WebGL2 } },
    { "WEBGL_stencil_texturing"_string, { { "GL_ANGLE_stencil_texturing"sv }, nullptr, OpenGLContext::WebGLVersion::WebGL2 } },
};
#endif

Optional<Vector<String>> WebGLRenderingContextBase::get_supported_extensions()
{
#ifdef AK_OS_RINOS
    Vector<String> webgl_extensions;
    if (context().webgl_version() == OpenGLContext::WebGLVersion::WebGL1) {
        webgl_extensions.append("ANGLE_instanced_arrays"_string);
        webgl_extensions.append("EXT_blend_minmax"_string);
        webgl_extensions.append("OES_fbo_render_mipmap"_string);
        webgl_extensions.append("OES_element_index_uint"_string);
        webgl_extensions.append("OES_standard_derivatives"_string);
        webgl_extensions.append("OES_texture_float"_string);
        webgl_extensions.append("OES_texture_float_linear"_string);
        webgl_extensions.append("OES_texture_half_float"_string);
        webgl_extensions.append("OES_texture_half_float_linear"_string);
        webgl_extensions.append("OES_vertex_array_object"_string);
        webgl_extensions.append("WEBGL_compressed_texture_etc1"_string);
        webgl_extensions.append("WEBGL_compressed_texture_s3tc"_string);
        webgl_extensions.append("WEBGL_compressed_texture_s3tc_srgb"_string);
        webgl_extensions.append("WEBGL_color_buffer_float"_string);
        webgl_extensions.append("EXT_color_buffer_half_float"_string);
        webgl_extensions.append("WEBGL_depth_texture"_string);
        webgl_extensions.append("WEBGL_draw_buffers"_string);
        webgl_extensions.append("WEBGL_lose_context"_string);
    }
    return webgl_extensions;
#else
    auto opengl_extensions = context().get_supported_opengl_extensions();
    Vector<String> webgl_extensions;

    for (auto const& [available_extension_name, available_extension_info] : s_available_webgl_extensions) {
        bool supported = !available_extension_info.only_for_webgl_version.has_value()
            || context().webgl_version() == available_extension_info.only_for_webgl_version;

        if (!available_extension_info.factory && !HTML::UniversalGlobalScopeMixin::expose_experimental_interfaces()) {
            supported = false;
        }

        if (supported) {
            for (auto const& required_extension : available_extension_info.required_angle_extensions) {
                if (!opengl_extensions.contains_slow(required_extension)) {
                    supported = false;
                    break;
                }
            }
        }

        if (supported)
            webgl_extensions.append(available_extension_name);
    }

    return webgl_extensions;
#endif
}

JS::Object* WebGLRenderingContextBase::get_extension(String const& name)
{
#ifdef AK_OS_RINOS
    if (context().webgl_version() != OpenGLContext::WebGLVersion::WebGL1)
        return nullptr;

    bool const is_element_index_uint_extension = name.equals_ignoring_ascii_case("OES_element_index_uint"sv);
    bool const is_instanced_arrays_extension = name.equals_ignoring_ascii_case("ANGLE_instanced_arrays"sv);
    bool const is_blend_minmax_extension = name.equals_ignoring_ascii_case("EXT_blend_minmax"sv);
    bool const is_fbo_render_mipmap_extension = name.equals_ignoring_ascii_case("OES_fbo_render_mipmap"sv);
    bool const is_standard_derivatives_extension = name.equals_ignoring_ascii_case("OES_standard_derivatives"sv);
    bool const is_texture_float_extension = name.equals_ignoring_ascii_case("OES_texture_float"sv);
    bool const is_texture_float_linear_extension = name.equals_ignoring_ascii_case("OES_texture_float_linear"sv);
    bool const is_texture_half_float_extension = name.equals_ignoring_ascii_case("OES_texture_half_float"sv);
    bool const is_texture_half_float_linear_extension = name.equals_ignoring_ascii_case("OES_texture_half_float_linear"sv);
    bool const is_vertex_array_object_extension = name.equals_ignoring_ascii_case("OES_vertex_array_object"sv);
    bool const is_compressed_texture_etc1_extension = name.equals_ignoring_ascii_case("WEBGL_compressed_texture_etc1"sv);
    bool const is_compressed_texture_s3tc_extension = name.equals_ignoring_ascii_case("WEBGL_compressed_texture_s3tc"sv);
    bool const is_compressed_texture_s3tc_srgb_extension = name.equals_ignoring_ascii_case("WEBGL_compressed_texture_s3tc_srgb"sv);
    bool const is_color_buffer_float_extension = name.equals_ignoring_ascii_case("WEBGL_color_buffer_float"sv);
    bool const is_color_buffer_half_float_extension = name.equals_ignoring_ascii_case("EXT_color_buffer_half_float"sv);
    bool const is_depth_texture_extension = name.equals_ignoring_ascii_case("WEBGL_depth_texture"sv);
    bool const is_draw_buffers_extension = name.equals_ignoring_ascii_case("WEBGL_draw_buffers"sv);

    // The WebGL extension algorithm compares names case-insensitively. Store
    // every RinOS extension under its standard spelling, otherwise a caller
    // that enables a lower-case alias would receive an object but leave the
    // command-side extension gates disabled. WEBGL_lose_context additionally
    // defines two draft-era aliases.
    bool const is_lose_context_extension = name.equals_ignoring_ascii_case("WEBGL_lose_context"sv)
        || name.equals_ignoring_ascii_case("WEBKIT_WEBGL_lose_context"sv)
        || name.equals_ignoring_ascii_case("MOZ_WEBGL_lose_context"sv);
    auto cache_key = name;
    if (is_element_index_uint_extension)
        cache_key = "OES_element_index_uint"_string;
    else if (is_instanced_arrays_extension)
        cache_key = "ANGLE_instanced_arrays"_string;
    else if (is_blend_minmax_extension)
        cache_key = "EXT_blend_minmax"_string;
    else if (is_fbo_render_mipmap_extension)
        cache_key = "OES_fbo_render_mipmap"_string;
    else if (is_standard_derivatives_extension)
        cache_key = "OES_standard_derivatives"_string;
    else if (is_texture_float_extension)
        cache_key = "OES_texture_float"_string;
    else if (is_texture_float_linear_extension)
        cache_key = "OES_texture_float_linear"_string;
    else if (is_texture_half_float_extension)
        cache_key = "OES_texture_half_float"_string;
    else if (is_texture_half_float_linear_extension)
        cache_key = "OES_texture_half_float_linear"_string;
    else if (is_vertex_array_object_extension)
        cache_key = "OES_vertex_array_object"_string;
    else if (is_compressed_texture_etc1_extension)
        cache_key = "WEBGL_compressed_texture_etc1"_string;
    else if (is_compressed_texture_s3tc_extension)
        cache_key = "WEBGL_compressed_texture_s3tc"_string;
    else if (is_compressed_texture_s3tc_srgb_extension)
        cache_key = "WEBGL_compressed_texture_s3tc_srgb"_string;
    else if (is_color_buffer_float_extension)
        cache_key = "WEBGL_color_buffer_float"_string;
    else if (is_color_buffer_half_float_extension)
        cache_key = "EXT_color_buffer_half_float"_string;
    else if (is_depth_texture_extension)
        cache_key = "WEBGL_depth_texture"_string;
    else if (is_draw_buffers_extension)
        cache_key = "WEBGL_draw_buffers"_string;
    else if (is_lose_context_extension)
        cache_key = "WEBGL_lose_context"_string;
    if (auto extension = m_enabled_extensions.get(cache_key); extension.has_value())
        return extension.release_value();

    if (is_element_index_uint_extension) {
        // RinGL executes a bounded uint32_t element stream, but WebGL 1
        // exposes that type only after this extension has been enabled.
        auto extension = MUST(Extensions::OESElementIndexUint::create(realm(), *this));
        m_enabled_extensions.set(cache_key, extension);
        return extension;
    }
    if (is_instanced_arrays_extension) {
        auto extension = MUST(Extensions::ANGLEInstancedArrays::create(realm(), *this));
        m_enabled_extensions.set(cache_key, extension);
        return extension;
    }
    if (is_blend_minmax_extension) {
        context().enable_rin_gl_blend_minmax();
        auto extension = MUST(Extensions::EXTBlendMinMax::create(realm(), *this));
        m_enabled_extensions.set(cache_key, extension);
        return extension;
    }
    if (is_fbo_render_mipmap_extension) {
        // RinGL records the selected image subresource through its versioned
        // RinGPU route. This object is the WebGL 1 opt-in; no direct surface
        // command path is exposed here.
        auto extension = MUST(Extensions::OESFboRenderMipmap::create(realm(), *this));
        m_enabled_extensions.set(cache_key, extension);
        return extension;
    }
    if (is_standard_derivatives_extension) {
        context().enable_rin_gl_standard_derivatives();
        auto extension = MUST(Extensions::OESStandardDerivatives::create(realm(), *this));
        m_enabled_extensions.set(cache_key, extension);
        return extension;
    }
    if (is_texture_float_extension) {
        auto extension = MUST(Extensions::OESTextureFloat::create(realm(), *this));
        m_enabled_extensions.set(cache_key, extension);
        // This RinGL implementation supports Float color attachments, so
        // OES_texture_float also makes WEBGL_color_buffer_float available.
        // Cache its extension object here so FBO completeness does not depend
        // on the order in which the two extension names are requested.
        if (!m_enabled_extensions.contains("WEBGL_color_buffer_float"_string)) {
            auto color_buffer_extension = MUST(Extensions::WebGLColorBufferFloat::create(realm(), *this));
            m_enabled_extensions.set("WEBGL_color_buffer_float"_string,
                                     color_buffer_extension);
        }
        context().enable_rin_gl_float_color_buffer();
        return extension;
    }
    if (is_texture_float_linear_extension) {
        // The native sampler supports Float filtering, but keep it WebGL
        // incomplete until this exact extension has been requested.
        context().enable_rin_gl_float_texture_linear();
        auto extension = MUST(Extensions::OESTextureFloatLinear::create(realm(), *this));
        m_enabled_extensions.set(cache_key, extension);
        return extension;
    }
    if (is_texture_half_float_extension) {
        auto extension = MUST(Extensions::OESTextureHalfFloat::create(realm(), *this));
        m_enabled_extensions.set(cache_key, extension);
        // OES_texture_half_float implicitly enables
        // EXT_color_buffer_half_float when RGBA16F rendering is available.
        if (!m_enabled_extensions.contains("EXT_color_buffer_half_float"_string)) {
            auto color_buffer_extension = MUST(Extensions::EXTColorBufferHalfFloat::create(realm(), *this));
            m_enabled_extensions.set("EXT_color_buffer_half_float"_string,
                                     color_buffer_extension);
        }
        context().enable_rin_gl_half_float_color_buffer();
        return extension;
    }
    if (is_texture_half_float_linear_extension) {
        // The shared RinGPU sampler has linear support, but WebGL exposes it
        // for binary16 inputs only after this extension object is acquired.
        context().enable_rin_gl_half_float_texture_linear();
        auto extension = MUST(Extensions::OESTextureHalfFloatLinear::create(realm(), *this));
        m_enabled_extensions.set(cache_key, extension);
        return extension;
    }
    if (is_vertex_array_object_extension) {
        auto extension = MUST(Extensions::OESVertexArrayObject::create(realm(), *this));
        m_enabled_extensions.set(cache_key, extension);
        return extension;
    }
    if (is_compressed_texture_etc1_extension) {
        auto extension = MUST(Extensions::WebGLCompressedTextureEtc1::create(realm(), *this));
        m_enabled_extensions.set(cache_key, extension);
        return extension;
    }
    if (is_compressed_texture_s3tc_extension) {
        auto extension = MUST(Extensions::WebGLCompressedTextureS3tc::create(realm(), *this));
        m_enabled_extensions.set(cache_key, extension);
        return extension;
    }
    if (is_compressed_texture_s3tc_srgb_extension) {
        auto extension = MUST(Extensions::WebGLCompressedTextureS3tcSrgb::create(realm(), *this));
        m_enabled_extensions.set(cache_key, extension);
        return extension;
    }
    if (is_color_buffer_float_extension) {
        context().enable_rin_gl_float_color_buffer();
        auto extension = MUST(Extensions::WebGLColorBufferFloat::create(realm(), *this));
        m_enabled_extensions.set(cache_key, extension);
        return extension;
    }
    if (is_color_buffer_half_float_extension) {
        context().enable_rin_gl_half_float_color_buffer();
        auto extension = MUST(Extensions::EXTColorBufferHalfFloat::create(realm(), *this));
        m_enabled_extensions.set(cache_key, extension);
        return extension;
    }
    if (is_depth_texture_extension) {
        auto extension = MUST(Extensions::WebGLDepthTexture::create(realm(), *this));
        m_enabled_extensions.set(cache_key, extension);
        return extension;
    }
    if (is_draw_buffers_extension) {
        context().enable_rin_gl_draw_buffers();
        auto extension = MUST(Extensions::WebGLDrawBuffers::create(realm(), *this));
        m_enabled_extensions.set(cache_key, extension);
        return extension;
    }
    if (is_lose_context_extension) {
        auto extension = MUST(Extensions::WebGLLoseContext::create(realm(), *this));
        m_enabled_extensions.set(cache_key, extension);
        return extension;
    }
    return nullptr;
#else
    // Returns an object if, and only if, name is an ASCII case-insensitive match [HTML] for one of the names returned
    // from getSupportedExtensions; otherwise, returns null. The object returned from getExtension contains any constants
    // or functions provided by the extension. A returned object may have no constants or functions if the extension does
    // not define any, but a unique object must still be returned. That object is used to indicate that the extension has
    // been enabled.
    auto supported_extensions = get_supported_extensions();
    auto supported_extension_iterator = supported_extensions->find_if([&name](String const& supported_extension) {
        return supported_extension.equals_ignoring_ascii_case(name);
    });
    if (supported_extension_iterator == supported_extensions->end())
        return nullptr;

    auto maybe_extension = m_enabled_extensions.get(name);
    if (maybe_extension.has_value())
        return maybe_extension.release_value();

    // If we pass the check above this will always return a value
    auto const& extension_info = s_available_webgl_extensions.get(name).release_value();

    if (!extension_info.factory)
        return nullptr;

    for (auto const& required_extension : extension_info.required_angle_extensions) {
        context().request_extension(null_terminated_string(required_extension).data());
    }

    auto extension = MUST(extension_info.factory(realm(), *this));
    m_enabled_extensions.set(name, extension);
    return extension;
#endif
}

void WebGLRenderingContextBase::enable_compressed_texture_format(WebIDL::UnsignedLong format)
{
    m_enabled_compressed_texture_formats.append(format);
}

void WebGLRenderingContextBase::visit_edges(Cell::Visitor& visitor)
{
    Base::visit_edges(visitor);
    visitor.visit(m_enabled_extensions);
}

bool WebGLRenderingContextBase::extension_enabled(StringView extension) const
{
    return m_enabled_extensions.contains(MUST(String::from_utf8(extension)));
}

ReadonlySpan<WebIDL::UnsignedLong> WebGLRenderingContextBase::enabled_compressed_texture_formats() const
{
    return m_enabled_compressed_texture_formats;
}

Optional<Gfx::BitmapExportResult> WebGLRenderingContextBase::read_and_pixel_convert_texture_image_source(TexImageSource const& source, WebIDL::UnsignedLong format, WebIDL::UnsignedLong type, Optional<int> destination_width, Optional<int> destination_height)
{
    // FIXME: If this function is called with an ImageData whose data attribute has been neutered,
    //        an INVALID_VALUE error is generated.
    // FIXME: If this function is called with an ImageBitmap that has been neutered, an INVALID_VALUE
    //        error is generated.
    // FIXME: If this function is called with an HTMLImageElement or HTMLVideoElement whose origin
    //        differs from the origin of the containing Document, or with an HTMLCanvasElement,
    //        ImageBitmap or OffscreenCanvas whose bitmap's origin-clean flag is set to false,
    //        a SECURITY_ERR exception must be thrown. See Origin Restrictions.
    // FIXME: If source is null then an INVALID_VALUE error is generated.
    auto bitmap = source.visit(
        [](GC::Root<HTML::HTMLImageElement> const& source) -> RefPtr<Gfx::ImmutableBitmap> {
            return source->immutable_bitmap();
        },
        [](GC::Root<HTML::HTMLCanvasElement> const& source) -> RefPtr<Gfx::ImmutableBitmap> {
            // Presentation creates an immutable copy before the RinGL-backed
            // `preserveDrawingBuffer: false` cleanup. Sampling the mutable
            // surface after it has been presented would otherwise upload a
            // cleared texture.
            source->present();
            if (auto bitmap = source->ensure_external_content_source().current_bitmap())
                return bitmap;
            auto surface = source->surface();
            if (!surface)
                return Gfx::ImmutableBitmap::create(*source->get_bitmap_from_surface());
            return Gfx::ImmutableBitmap::create_snapshot_from_painting_surface(*surface);
        },
        [](GC::Root<HTML::OffscreenCanvas> const& source) -> RefPtr<Gfx::ImmutableBitmap> {
            return Gfx::ImmutableBitmap::create(*source->bitmap());
        },
        [](GC::Root<HTML::HTMLVideoElement> const& source) -> RefPtr<Gfx::ImmutableBitmap> {
            return source->bitmap();
        },
        [](GC::Root<HTML::ImageBitmap> const& source) -> RefPtr<Gfx::ImmutableBitmap> {
            return Gfx::ImmutableBitmap::create(*source->bitmap());
        },
        [](GC::Root<HTML::ImageData> const& source) -> RefPtr<Gfx::ImmutableBitmap> {
            return Gfx::ImmutableBitmap::create(source->bitmap());
        });
    if (!bitmap)
        return OptionalNone {};

    // FIXME: Respect unpackColorSpace
    auto export_flags = 0;
    if (m_unpack_flip_y && !source.has<GC::Root<HTML::ImageBitmap>>())
        // The first pixel transferred from the source to the WebGL implementation corresponds to the upper left corner of
        // the source. This behavior is modified by the UNPACK_FLIP_Y_WEBGL pixel storage parameter, except for ImageBitmap
        // arguments, as described in the abovementioned section.
        export_flags |= Gfx::ExportFlags::FlipY;
    if (m_unpack_premultiply_alpha)
        export_flags |= Gfx::ExportFlags::PremultiplyAlpha;

#ifdef AK_OS_RINOS
    if (type == webgl_float || type == webgl_half_float_oes)
        return export_floating_texture_image(*bitmap, format, type, export_flags,
                                             destination_width, destination_height);
#endif

    auto export_format = determine_export_format(format, type);
    if (!export_format.has_value())
        return OptionalNone {};
    auto result = bitmap->export_to_byte_buffer(export_format.value(), export_flags, destination_width, destination_height);
    if (result.is_error()) {
        dbgln("Could not export bitmap: {}", result.release_error());
        return OptionalNone {};
    }

    return result.release_value();
}

// TODO: The glGetError spec allows for queueing errors which is something we should probably do, for now
//       this just keeps track of one error which is also fine by the spec
GLenum WebGLRenderingContextBase::get_error_value()
{
#ifdef AK_OS_RINOS
    if (m_error == RINGL_NO_ERROR)
        return context().rin_gl_get_error();

    auto error = m_error;
    m_error = RINGL_NO_ERROR;
    return error;
#else
    if (m_error == GL_NO_ERROR)
        return glGetError();

    auto error = m_error;
    m_error = GL_NO_ERROR;
    return error;
#endif
}

void WebGLRenderingContextBase::set_error(GLenum error)
{
#ifdef AK_OS_RINOS
    if (m_error != RINGL_NO_ERROR)
        return;

    auto context_error = context().rin_gl_get_error();
    if (context_error != RINGL_NO_ERROR)
        m_error = context_error;
    else
        m_error = error;
#else
    if (m_error != GL_NO_ERROR)
        return;

    auto context_error = glGetError();
    if (context_error != GL_NO_ERROR)
        m_error = context_error;
    else
        m_error = error;
#endif
}

void WebGLRenderingContextBase::set_error_without_backend_check(GLenum error)
{
#ifdef AK_OS_RINOS
    if (m_error != RINGL_NO_ERROR)
        return;
#else
    if (m_error != GL_NO_ERROR)
        return;
#endif
    m_error = error;
}

void WebGLRenderingContextBase::reset_webgl_base_state_after_context_restore()
{
    m_unpack_flip_y = false;
    m_unpack_premultiply_alpha = false;
    m_unpack_colorspace_conversion = BROWSER_DEFAULT_WEBGL;
#ifdef AK_OS_RINOS
    m_error = RINGL_NO_ERROR;
#else
    m_error = GL_NO_ERROR;
#endif
    m_enabled_compressed_texture_formats.clear();
    m_enabled_extensions.clear();
}

}
