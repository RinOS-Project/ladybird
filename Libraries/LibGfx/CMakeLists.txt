set(SOURCES
    AffineTransform.cpp
    Bitmap.cpp
    BitmapExport.cpp
    BitmapSequence.cpp
    CMYKBitmap.cpp
    CanvasCommandList.cpp
    CanvasCommandPlayer.cpp
    Color.cpp
    ColorConversion.cpp
    ColorSpace.cpp
    CornerRadii.cpp
    Cursor.cpp
    Filter.cpp
    Font/Font.cpp
    Font/FontDatabase.cpp
    Font/FontSupport.cpp
    Font/FontVariationSettings.cpp
    Font/PathFontProvider.cpp
    Font/Typeface.cpp
    Font/WOFF/Loader.cpp
    Font/WOFF2/Loader.cpp
    FontCascadeList.cpp
    GradientPainting.cpp
    Gradients.cpp
    Matrix4x4.cpp
    DecodedImageFrame.cpp
    ImageFormats/BMPWriter.cpp
    ImageFormats/JPEGWriter.cpp
    ImageFormats/PNGWriter.cpp
    ImageFormats/WebPSharedLossless.cpp
    ImageFormats/WebPWriter.cpp
    ImageFormats/WebPWriterLossless.cpp
    PaintStyle.cpp
    Painter.cpp
    PaintingSurface.cpp
    Palette.cpp
    Path.cpp
    Point.cpp
    Rect.cpp
    SharedImage.cpp
    SharedImageBuffer.cpp
    ShareableBitmap.cpp
    ShapeFeature.cpp
    Size.cpp
    SystemTheme.cpp
    TextLayout.cpp
    Triangle.cpp
    VectorGraphic.cpp
    YUVData.cpp
)

if (AK_OS_RINOS)
    list(REMOVE_ITEM SOURCES
        Font/WOFF2/Loader.cpp
        ImageFormats/AVIFLoader.cpp
        ImageFormats/JPEGLoader.cpp
        ImageFormats/JPEGWriter.cpp
        ImageFormats/JPEGXLLoader.cpp
        ImageFormats/PNGLoader.cpp
        ImageFormats/PNGWriter.cpp
        ImageFormats/WebPLoader.cpp
        ImageFormats/WebPWriter.cpp
        ImageFormats/WebPWriterLossless.cpp
    )
    list(APPEND SOURCES
        Font/TypefaceRinOS.cpp
        ImageFormats/JPEGLoaderRinOS.cpp
        PathAquamarine.cpp
        ImageFormats/PNGLoaderRinOS.cpp
        ImageFormats/PNGWriterRinOS.cpp
        ImageFormats/WebPLoaderRinOS.cpp
        ../../../png/rpng.c
        ../../../webp/src/dec/webp_dec.c
        ../../../webp/src/dec/vp8_dec.c
        ../../../webp/src/dec/vp8l_dec.c
    )
    set_source_files_properties(
        ../../../png/rpng.c
        ../../../webp/src/dec/webp_dec.c
        ../../../webp/src/dec/vp8_dec.c
        ../../../webp/src/dec/vp8l_dec.c
        PROPERTIES COMPILE_OPTIONS "-Wno-missing-prototypes;-Wno-unused-parameter;-Wno-unused-function;-Wno-unused-variable;-Wno-unused-but-set-variable;-Wno-missing-field-initializers"
    )
endif()

if (APPLE)
    list(APPEND SOURCES MetalContext.mm)
endif()

if (HAS_DIRECTX)
    list(APPEND SOURCES D3DSharedTexture.cpp Direct3DContext.cpp)
endif()

if (HAS_VULKAN)
    list(APPEND SOURCES SkiaVulkanMemoryAllocator.cpp VulkanContext.cpp VulkanImage.cpp)
endif()

if (HAS_FONTCONFIG)
    list(APPEND SOURCES Font/GlobalFontConfig.cpp)
endif()

set(LIBGFX_SKIA_SOURCES
    Font/TypefaceSkia.cpp
    PainterSkia.cpp
    PathSkia.cpp
    SkiaBackendContext.cpp
    SkiaUtils.cpp
)
if (RINOS_USE_SKIA_GRAPHICS)
    include(skia)
    list(APPEND SOURCES ${LIBGFX_SKIA_SOURCES})
else()
    message(STATUS "RinOS policy: Skia raster backend disabled in LibGfx (aquamarine path expected).")
    list(APPEND SOURCES PainterAquamarine.cpp)

    # Define 'aquamarine' as a static library if it doesn't already exist.
    # Aquamarine is a standalone C library with no build system.
    if (NOT TARGET aquamarine)
        set(AQUAMARINE_ROOT "${CMAKE_CURRENT_SOURCE_DIR}/../../../aquamarine")
        add_library(aquamarine STATIC
            ${AQUAMARINE_ROOT}/aq_surface.c
            ${AQUAMARINE_ROOT}/aq_color.c
            ${AQUAMARINE_ROOT}/aq_blend.c
            ${AQUAMARINE_ROOT}/aq_draw.c
            ${AQUAMARINE_ROOT}/aq_gradient.c
            ${AQUAMARINE_ROOT}/aq_text.c
            ${AQUAMARINE_ROOT}/aq_transform.c
            ${AQUAMARINE_ROOT}/aq_render3d.c
        )
        target_include_directories(aquamarine PUBLIC ${AQUAMARINE_ROOT})
        set_target_properties(aquamarine PROPERTIES C_STANDARD 11)
    endif()
endif()

ladybird_lib(LibGfx gfx)

target_link_libraries(LibGfx PRIVATE LibCompress LibCore LibCrypto LibFileSystem LibTextCodec LibIPC LibSync LibUnicode)

target_link_libraries(LibGfx PRIVATE PkgConfig::WOFF2 JPEG::JPEG PNG::PNG skia harfbuzz)

import_rust_crate(MANIFEST_PATH Rust/Cargo.toml CRATE_NAME libgfx_rust FEATURES allocator FFI_HEADER RustFFI.h)
target_link_libraries(LibGfx PRIVATE libgfx_rust)

if (HAS_FONTCONFIG)
    target_link_libraries(LibGfx PRIVATE Fontconfig::Fontconfig)
endif()

if (NOT ANDROID)
    # FIXME: Build vcpkg libraries with clang-cl so they can also build in sanitize.
    # https://github.com/LadybirdBrowser/ladybird/issues/5224
    if (WIN32 AND ENABLE_ADDRESS_SANITIZER)
        # We have to disable ASAN for the full target as fixing the issue in the initial SkiaBackendContext.cpp TU
        #  alone causes transitive issues in other TUs that depend on it
        target_compile_options(LibGfx PRIVATE -fno-sanitize=address)
    endif()
endif()

if (APPLE)
    target_link_libraries(LibCore PUBLIC "-framework Metal")
    target_link_libraries(LibCore PUBLIC "-framework Accelerate")
endif()

if (HAS_DIRECTX)
    target_link_libraries(LibGfx PRIVATE d3d12 dxgi d3dcompiler)
endif()

if (HAS_VULKAN)
    target_link_libraries(LibCore PUBLIC Vulkan::Vulkan Vulkan::Headers)
    target_link_libraries(LibGfx PRIVATE GPUOpen::VulkanMemoryAllocator)

    if (USE_VULKAN_DMABUF_IMAGES)
        pkg_check_modules(LibDRM REQUIRED libdrm)
        target_include_directories(LibGfx PUBLIC ${LibDRM_INCLUDE_DIRS})
    endif()
endif()
