/*
 * Copyright (c) 2023-2024, Aliaksandr Kalenik <kalenik.aliaksandr@gmail.com>
 * Copyright (c) 2026, Gregory Bertilson <gregory@ladybird.org>
 *
 * SPDX-License-Identifier: BSD-2-Clause
 */

#include <AK/OwnPtr.h>
#include <AK/Try.h>
#include <LibGfx/Bitmap.h>
#include <LibGfx/ImmutableBitmap.h>
#include <LibGfx/PaintingSurface.h>
#include <LibGfx/YUVData.h>

#ifndef AK_OS_RINOS
#include <LibGfx/SkiaBackendContext.h>
#include <LibGfx/SkiaUtils.h>

#include <core/SkBitmap.h>
#include <core/SkCanvas.h>
#include <core/SkColorSpace.h>
#include <core/SkImage.h>
#include <core/SkSurface.h>
#include <core/SkYUVAPixmaps.h>
#include <gpu/ganesh/GrDirectContext.h>
#include <gpu/ganesh/SkImageGanesh.h>
#endif // !AK_OS_RINOS

namespace Gfx {

StringView export_format_name(ExportFormat format)
{
    switch (format) {
#define ENUMERATE_EXPORT_FORMAT(format) \
    case Gfx::ExportFormat::format:     \
        return #format##sv;
        ENUMERATE_EXPORT_FORMATS(ENUMERATE_EXPORT_FORMAT)
#undef ENUMERATE_EXPORT_FORMAT
    }
    VERIFY_NOT_REACHED();
}

#ifdef AK_OS_RINOS

struct ImmutableBitmapImpl {
    RefPtr<Gfx::Bitmap> bitmap;
    ColorSpace color_space;
    OwnPtr<YUVData> yuv_data;
    IntSize cached_size;
};

static void write_little_endian_u16(u8* destination, u16 value)
{
    destination[0] = static_cast<u8>(value);
    destination[1] = static_cast<u8>(value >> 8u);
}

static u16 quantize_unorm8(u8 component, u16 maximum)
{
    // Match the normalised fixed-point conversion used by the generic
    // exporter. Bit truncation darkens values such as 51/255 in the green
    // channel of RGB565, so round to the nearest representable value first.
    return static_cast<u16>((static_cast<u32>(component) * maximum + 127u) / 255u);
}

int ImmutableBitmap::width() const
{
    if (m_impl->bitmap)
        return m_impl->bitmap->width();
    if (m_impl->yuv_data)
        return m_impl->yuv_data->size().width();
    return m_impl->cached_size.width();
}

int ImmutableBitmap::height() const
{
    if (m_impl->bitmap)
        return m_impl->bitmap->height();
    if (m_impl->yuv_data)
        return m_impl->yuv_data->size().height();
    return m_impl->cached_size.height();
}

IntRect ImmutableBitmap::rect() const { return { {}, size() }; }
IntSize ImmutableBitmap::size() const { return { width(), height() }; }

AlphaType ImmutableBitmap::alpha_type() const
{
    return m_impl->bitmap ? m_impl->bitmap->alpha_type() : AlphaType::Premultiplied;
}

static u8 export_premultiplied_component(u8 component, u8 alpha)
{
    return static_cast<u8>((static_cast<u32>(component) * alpha) / 255u);
}

static u8 export_unpremultiplied_component(u8 component, u8 alpha)
{
    u32 value;

    if (alpha == 0)
        return 0;
    value = (static_cast<u32>(component) * 255u) / alpha;
    return static_cast<u8>(value > 255u ? 255u : value);
}

// ImageData's Display-P3 values use the sRGB transfer function. Store its
// 8-bit decode curve as Q0.16 values so WebGL image uploads do not need a
// hosted libm dependency in the freestanding build.
static constexpr u16 srgb_to_linear_q16[] = {
    0, 20, 40, 60, 80, 99, 119, 139,
    159, 179, 199, 219, 241, 264, 288, 313,
    340, 367, 396, 427, 458, 491, 526, 562,
    599, 637, 677, 718, 761, 805, 851, 898,
    947, 997, 1048, 1101, 1156, 1212, 1270, 1330,
    1391, 1453, 1517, 1583, 1651, 1720, 1790, 1863,
    1937, 2013, 2090, 2170, 2250, 2333, 2418, 2504,
    2592, 2681, 2773, 2866, 2961, 3058, 3157, 3258,
    3360, 3464, 3570, 3678, 3788, 3900, 4014, 4129,
    4247, 4366, 4488, 4611, 4736, 4864, 4993, 5124,
    5257, 5392, 5530, 5669, 5810, 5953, 6099, 6246,
    6395, 6547, 6700, 6856, 7014, 7174, 7335, 7500,
    7666, 7834, 8004, 8177, 8352, 8528, 8708, 8889,
    9072, 9258, 9445, 9635, 9828, 10022, 10219, 10417,
    10619, 10822, 11028, 11235, 11446, 11658, 11873, 12090,
    12309, 12530, 12754, 12980, 13209, 13440, 13673, 13909,
    14146, 14387, 14629, 14874, 15122, 15371, 15623, 15878,
    16135, 16394, 16656, 16920, 17187, 17456, 17727, 18001,
    18277, 18556, 18837, 19121, 19407, 19696, 19987, 20281,
    20577, 20876, 21177, 21481, 21787, 22096, 22407, 22721,
    23038, 23357, 23678, 24002, 24329, 24658, 24990, 25325,
    25662, 26001, 26344, 26688, 27036, 27386, 27739, 28094,
    28452, 28813, 29176, 29542, 29911, 30282, 30656, 31033,
    31412, 31794, 32179, 32567, 32957, 33350, 33745, 34143,
    34544, 34948, 35355, 35764, 36176, 36591, 37008, 37429,
    37852, 38278, 38706, 39138, 39572, 40009, 40449, 40891,
    41337, 41785, 42236, 42690, 43147, 43606, 44069, 44534,
    45002, 45473, 45947, 46423, 46903, 47385, 47871, 48359,
    48850, 49344, 49841, 50341, 50844, 51349, 51858, 52369,
    52884, 53401, 53921, 54445, 54971, 55500, 56032, 56567,
    57105, 57646, 58190, 58737, 59287, 59840, 60396, 60955,
    61517, 62082, 62650, 63221, 63795, 64372, 64952, 65535,
};
static_assert(sizeof(srgb_to_linear_q16) / sizeof(srgb_to_linear_q16[0]) == 256);

static u8 linear_q16_to_srgb(u16 linear)
{
    if (linear == 0)
        return 0;
    if (linear == 65535)
        return 255;

    size_t lower = 0;
    size_t upper = 255;
    while (lower < upper) {
        auto middle = (lower + upper) / 2;
        if (srgb_to_linear_q16[middle] < linear)
            lower = middle + 1;
        else
            upper = middle;
    }

    auto previous = srgb_to_linear_q16[lower - 1];
    auto next = srgb_to_linear_q16[lower];
    if (linear - previous <= next - linear)
        return static_cast<u8>(lower - 1);
    return static_cast<u8>(lower);
}

static u16 clamp_linear_q16(i64 component)
{
    if (component <= 0)
        return 0;

    component = (component + 32768) >> 16;
    if (component >= 65535)
        return 65535;
    return static_cast<u16>(component);
}

static void convert_display_p3_to_srgb(u8& red, u8& green, u8& blue)
{
    auto linear_red = srgb_to_linear_q16[red];
    auto linear_green = srgb_to_linear_q16[green];
    auto linear_blue = srgb_to_linear_q16[blue];

    // Display-P3 (D65) to sRGB (D65), Q16.16. Clamp only after the gamut
    // conversion: input P3 primaries may lie outside the sRGB gamut.
    auto converted_red = clamp_linear_q16(
        static_cast<i64>(linear_red) * 80265 - static_cast<i64>(linear_green) * 14739);
    auto converted_green = clamp_linear_q16(
        -static_cast<i64>(linear_red) * 2756 + static_cast<i64>(linear_green) * 68294);
    auto converted_blue = clamp_linear_q16(
        -static_cast<i64>(linear_red) * 1287 - static_cast<i64>(linear_green) * 5155
        + static_cast<i64>(linear_blue) * 71994);

    red = linear_q16_to_srgb(converted_red);
    green = linear_q16_to_srgb(converted_green);
    blue = linear_q16_to_srgb(converted_blue);
}

static void export_pixel_components(Color source, AlphaType source_alpha_type,
    bool destination_is_premultiplied, bool convert_display_p3, u8& red, u8& green, u8& blue, u8& alpha)
{
    red = source.red();
    green = source.green();
    blue = source.blue();
    alpha = source.alpha();

    if (source_alpha_type == AlphaType::Premultiplied) {
        red = export_unpremultiplied_component(red, alpha);
        green = export_unpremultiplied_component(green, alpha);
        blue = export_unpremultiplied_component(blue, alpha);
    }

    if (convert_display_p3)
        convert_display_p3_to_srgb(red, green, blue);

    if (destination_is_premultiplied) {
        red = export_premultiplied_component(red, alpha);
        green = export_premultiplied_component(green, alpha);
        blue = export_premultiplied_component(blue, alpha);
    }
}

ErrorOr<BitmapExportResult> ImmutableBitmap::export_to_byte_buffer(ExportFormat format, int flags, Optional<int> target_width, Optional<int> target_height) const
{
    if (!m_impl->bitmap)
        return Error::from_string_literal("No bitmap data available for export");

    int w = target_width.value_or(width());
    int h = target_height.value_or(height());
    // For simplicity, ignore scaling on RinOS and use original size if mismatch
    if (w != width() || h != height()) {
        w = width();
        h = height();
    }

    int bpp = 4;
    switch (format) {
    case ExportFormat::Gray8: case ExportFormat::Alpha8: bpp = 1; break;
    case ExportFormat::RGB565: case ExportFormat::RGBA5551: case ExportFormat::RGBA4444: bpp = 2; break;
    case ExportFormat::RGB888: bpp = 3; break;
    case ExportFormat::RGBA8888: bpp = 4; break;
    default: VERIFY_NOT_REACHED();
    }

    Checked<size_t> buffer_pitch = w;
    buffer_pitch *= bpp;
    if (buffer_pitch.has_overflow() || Checked<size_t>::multiplication_would_overflow(buffer_pitch.value(), h))
        return Error::from_string_literal("ImmutableBitmap::export_to_byte_buffer size overflow");

    auto buffer = TRY(ByteBuffer::create_zeroed(buffer_pitch.value() * h));
    auto* raw = buffer.data();
    bool const convert_display_p3 = (flags & ExportFlags::ConvertDisplayP3ToSRGB) != 0;

    for (int y = 0; y < h; ++y) {
        auto target_y = (flags & ExportFlags::FlipY) ? h - y - 1 : y;
        for (int x = 0; x < w; ++x) {
            auto target_has_alpha = format == ExportFormat::Alpha8
                || format == ExportFormat::RGBA5551
                || format == ExportFormat::RGBA4444
                || format == ExportFormat::RGBA8888;
            // Exporting to an RGB-only layout drops alpha. Preserve the
            // represented color by premultiplying an unpremultiplied source
            // before that loss; a source already stored premultiplied remains
            // unchanged. RGBA targets instead honor WebGL's explicit unpack
            // premultiply flag.
            auto destination_is_premultiplied = target_has_alpha
                ? (flags & ExportFlags::PremultiplyAlpha) != 0
                : true;
            auto offset = target_y * buffer_pitch.value() + x * bpp;
            u8 red;
            u8 green;
            u8 blue;
            u8 alpha;

            // WebGL's TexImageSource route requests this conversion through
            // UNPACK_PREMULTIPLY_ALPHA_WEBGL. The bitmap's alpha metadata is
            // the source representation; do the conversion before packing so
            // RGBA8 and all packed WebGL texture types agree.
            export_pixel_components(get_pixel(x, y), alpha_type(), destination_is_premultiplied, convert_display_p3,
                red, green, blue, alpha);
            switch (format) {
            case ExportFormat::RGBA8888:
                raw[offset + 0] = red;
                raw[offset + 1] = green;
                raw[offset + 2] = blue;
                raw[offset + 3] = alpha;
                break;
            case ExportFormat::RGB888:
                raw[offset + 0] = red;
                raw[offset + 1] = green;
                raw[offset + 2] = blue;
                break;
            case ExportFormat::Gray8:
                raw[offset] = static_cast<u8>(0.299f * red + 0.587f * green + 0.114f * blue);
                break;
            case ExportFormat::Alpha8:
                raw[offset] = alpha;
                break;
            case ExportFormat::RGB565: {
                auto packed = static_cast<u16>((quantize_unorm8(red, 31u) << 11u)
                    | (quantize_unorm8(green, 63u) << 5u)
                    | quantize_unorm8(blue, 31u));
                write_little_endian_u16(raw + offset, packed);
                break;
            }
            case ExportFormat::RGBA5551: {
                auto packed = static_cast<u16>((quantize_unorm8(red, 31u) << 11u)
                    | (quantize_unorm8(green, 31u) << 6u)
                    | (quantize_unorm8(blue, 31u) << 1u)
                    | static_cast<u16>(alpha >> 7u));
                write_little_endian_u16(raw + offset, packed);
                break;
            }
            case ExportFormat::RGBA4444: {
                auto packed = static_cast<u16>((quantize_unorm8(red, 15u) << 12u)
                    | (quantize_unorm8(green, 15u) << 8u)
                    | (quantize_unorm8(blue, 15u) << 4u)
                    | quantize_unorm8(alpha, 15u));
                write_little_endian_u16(raw + offset, packed);
                break;
            }
            }
        }
    }

    return BitmapExportResult { .buffer = move(buffer), .width = w, .height = h };
}

RefPtr<Gfx::Bitmap const> ImmutableBitmap::bitmap() const
{
    return m_impl->bitmap;
}

bool ImmutableBitmap::is_yuv_backed() const
{
    return m_impl->yuv_data != nullptr;
}

ErrorOr<NonnullRefPtr<ImmutableBitmap>> ImmutableBitmap::create_from_yuv(NonnullOwnPtr<YUVData> yuv_data)
{
    auto size = yuv_data->size();
    ImmutableBitmapImpl impl {
        .bitmap = nullptr,
        .color_space = {},
        .yuv_data = move(yuv_data),
        .cached_size = size,
    };
    return adopt_ref(*new ImmutableBitmap(make<ImmutableBitmapImpl>(move(impl))));
}

Color ImmutableBitmap::get_pixel(int x, int y) const
{
    if (m_impl->bitmap)
        return m_impl->bitmap->get_pixel(x, y);
    return Color::Black;
}

NonnullRefPtr<ImmutableBitmap> ImmutableBitmap::create(NonnullRefPtr<Bitmap> bitmap, ColorSpace color_space)
{
    auto size = bitmap->size();
    ImmutableBitmapImpl impl {
        .bitmap = move(bitmap),
        .color_space = move(color_space),
        .yuv_data = nullptr,
        .cached_size = size,
    };
    return adopt_ref(*new ImmutableBitmap(make<ImmutableBitmapImpl>(move(impl))));
}

NonnullRefPtr<ImmutableBitmap> ImmutableBitmap::create(NonnullRefPtr<Bitmap> bitmap, AlphaType alpha_type, ColorSpace color_space)
{
    auto source_bitmap = bitmap;
    if (source_bitmap->alpha_type() != alpha_type) {
        source_bitmap = MUST(bitmap->clone());
        source_bitmap->set_alpha_type_destructive(alpha_type);
    }
    return create(source_bitmap, move(color_space));
}

NonnullRefPtr<ImmutableBitmap> ImmutableBitmap::create_snapshot_from_painting_surface(NonnullRefPtr<PaintingSurface> painting_surface)
{
    auto size = painting_surface->size();
    // A RinOS WebGL surface may explicitly opt out of premultiplied alpha.
    // Preserve the source surface's declaration in the compositor snapshot
    // instead of re-labeling straight-alpha pixels as premultiplied.
    auto const alpha_type = painting_surface->bitmap()
        ? painting_surface->bitmap()->alpha_type()
        : Gfx::AlphaType::Premultiplied;
    auto bitmap = MUST(Gfx::Bitmap::create(Gfx::BitmapFormat::BGRA8888, alpha_type, size));
    painting_surface->read_into_bitmap(*bitmap);
    return create(move(bitmap));
}

ImmutableBitmap::ImmutableBitmap(NonnullOwnPtr<ImmutableBitmapImpl> impl)
    : m_impl(move(impl))
{
}

ImmutableBitmap::~ImmutableBitmap() = default;

void ImmutableBitmap::lock_context() { }
void ImmutableBitmap::unlock_context() { }

#else // !AK_OS_RINOS

struct ImmutableBitmapImpl {
    RefPtr<SkiaBackendContext> context;
    sk_sp<SkImage> sk_image;
    SkBitmap sk_bitmap;
    RefPtr<Gfx::Bitmap> bitmap;
    ColorSpace color_space;
    OwnPtr<YUVData> yuv_data;
};

int ImmutableBitmap::width() const
{
    if (m_impl->yuv_data)
        return m_impl->yuv_data->size().width();
    return m_impl->sk_image->width();
}

int ImmutableBitmap::height() const
{
    if (m_impl->yuv_data)
        return m_impl->yuv_data->size().height();
    return m_impl->sk_image->height();
}

IntRect ImmutableBitmap::rect() const
{
    return { {}, size() };
}

IntSize ImmutableBitmap::size() const
{
    return { width(), height() };
}

AlphaType ImmutableBitmap::alpha_type() const
{
    // We assume premultiplied alpha type for opaque surfaces since that is Skia's preferred alpha type and the
    // effective pixel data is identical between premultiplied and unpremultiplied in that case.
    return m_impl->sk_image->alphaType() == kUnpremul_SkAlphaType ? AlphaType::Unpremultiplied : AlphaType::Premultiplied;
}

SkImage const* ImmutableBitmap::sk_image() const
{
    return m_impl->sk_image.get();
}

static int bytes_per_pixel_for_export_format(ExportFormat format)
{
    switch (format) {
    case ExportFormat::Gray8:
    case ExportFormat::Alpha8:
        return 1;
    case ExportFormat::RGB565:
    case ExportFormat::RGBA5551:
    case ExportFormat::RGBA4444:
        return 2;
    case ExportFormat::RGB888:
        return 3;
    case ExportFormat::RGBA8888:
        return 4;
    default:
        VERIFY_NOT_REACHED();
    }
}

static SkColorType export_format_to_skia_color_type(ExportFormat format)
{
    switch (format) {
    case ExportFormat::Gray8:
        return SkColorType::kGray_8_SkColorType;
    case ExportFormat::Alpha8:
        return SkColorType::kAlpha_8_SkColorType;
    case ExportFormat::RGB565:
        return SkColorType::kRGB_565_SkColorType;
    case ExportFormat::RGBA5551:
        dbgln("FIXME: Support conversion to RGBA5551.");
        return SkColorType::kUnknown_SkColorType;
    case ExportFormat::RGBA4444:
        return SkColorType::kARGB_4444_SkColorType;
    case ExportFormat::RGB888:
        // This one needs to be converted manually because Skia has no valid 24-bit color type.
        VERIFY_NOT_REACHED();
    case ExportFormat::RGBA8888:
        return SkColorType::kRGBA_8888_SkColorType;
    default:
        VERIFY_NOT_REACHED();
    }
}

ErrorOr<BitmapExportResult> ImmutableBitmap::export_to_byte_buffer(ExportFormat format, int flags, Optional<int> target_width, Optional<int> target_height) const
{
    int width = target_width.value_or(this->width());
    int height = target_height.value_or(this->height());

    if (format == ExportFormat::RGB888 && (width != this->width() || height != this->height())) {
        dbgln("FIXME: Ignoring target width and height because scaling is not implemented for this export format.");
        width = this->width();
        height = this->height();
    }

    Checked<size_t> buffer_pitch = width;
    int number_of_bytes = bytes_per_pixel_for_export_format(format);
    buffer_pitch *= number_of_bytes;
    if (buffer_pitch.has_overflow())
        return Error::from_string_literal("Gfx::ImmutableBitmap::export_to_byte_buffer size overflow");

    if (Checked<size_t>::multiplication_would_overflow(buffer_pitch.value(), height))
        return Error::from_string_literal("Gfx::ImmutableBitmap::export_to_byte_buffer size overflow");

    auto buffer = MUST(ByteBuffer::create_zeroed(buffer_pitch.value() * height));

    if (width > 0 && height > 0) {
        if (format == ExportFormat::RGB888) {
            // 24 bit RGB is not supported by Skia, so we need to handle this format ourselves.
            auto raw_buffer = buffer.data();
            for (auto y = 0; y < height; y++) {
                auto target_y = flags & ExportFlags::FlipY ? height - y - 1 : y;
                for (auto x = 0; x < width; x++) {
                    auto pixel = get_pixel(x, y);
                    auto buffer_offset = (target_y * buffer_pitch.value()) + (x * 3ull);
                    raw_buffer[buffer_offset + 0] = pixel.red();
                    raw_buffer[buffer_offset + 1] = pixel.green();
                    raw_buffer[buffer_offset + 2] = pixel.blue();
                }
            }
        } else {
            auto skia_format = export_format_to_skia_color_type(format);
            auto color_space = SkColorSpace::MakeSRGB();

            auto image_info = SkImageInfo::Make(width, height, skia_format, flags & ExportFlags::PremultiplyAlpha ? SkAlphaType::kPremul_SkAlphaType : SkAlphaType::kUnpremul_SkAlphaType, color_space);
            auto surface = SkSurfaces::WrapPixels(image_info, buffer.data(), buffer_pitch.value());
            VERIFY(surface);
            auto* surface_canvas = surface->getCanvas();
            auto dst_rect = Gfx::to_skia_rect(Gfx::Rect { 0, 0, width, height });

            if (flags & ExportFlags::FlipY) {
                surface_canvas->translate(0, dst_rect.height());
                surface_canvas->scale(1, -1);
            }

            surface_canvas->drawImageRect(sk_image(), dst_rect, Gfx::to_skia_sampling_options(Gfx::ScalingMode::NearestNeighbor));
        }
    } else {
        VERIFY(buffer.is_empty());
    }

    return BitmapExportResult {
        .buffer = move(buffer),
        .width = width,
        .height = height,
    };
}

RefPtr<Gfx::Bitmap const> ImmutableBitmap::bitmap() const
{
    if (!m_impl->bitmap && m_impl->sk_image) {
        auto bitmap = MUST(Gfx::Bitmap::create(Gfx::BitmapFormat::BGRA8888, Gfx::AlphaType::Premultiplied, { m_impl->sk_image->width(), m_impl->sk_image->height() }));
        auto image_info = SkImageInfo::Make(bitmap->width(), bitmap->height(), kBGRA_8888_SkColorType, kPremul_SkAlphaType, SkColorSpace::MakeSRGB());
        SkPixmap pixmap(image_info, bitmap->begin(), bitmap->pitch());
        if (m_impl->context)
            m_impl->context->lock();
        m_impl->sk_image->readPixels(pixmap, 0, 0);
        if (m_impl->context)
            m_impl->context->unlock();
        m_impl->bitmap = move(bitmap);
    }
    return m_impl->bitmap;
}

bool ImmutableBitmap::is_yuv_backed() const
{
    return m_impl->yuv_data != nullptr;
}

ErrorOr<NonnullRefPtr<ImmutableBitmap>> ImmutableBitmap::create_from_yuv(NonnullOwnPtr<YUVData> yuv_data)
{
    // Hold onto the YUVData to lazily create the SkImage later.
    ImmutableBitmapImpl impl {
        .context = nullptr,
        .sk_image = nullptr,
        .sk_bitmap = {},
        .bitmap = nullptr,
        .color_space = {},
        .yuv_data = move(yuv_data),
    };
    return adopt_ref(*new ImmutableBitmap(make<ImmutableBitmapImpl>(move(impl))));
}

static sk_sp<SkColorSpace> color_space_from_cicp(Media::CodingIndependentCodePoints const& cicp)
{
    auto gamut = [&] {
        if (cicp.color_primaries() == Media::ColorPrimaries::XYZ)
            return SkNamedGamut::kXYZ;

        auto primaries = [&] {
            switch (cicp.color_primaries()) {
            case Media::ColorPrimaries::Reserved:
            case Media::ColorPrimaries::Unspecified:
                return SkNamedPrimaries::kRec709;
            case Media::ColorPrimaries::XYZ:
                VERIFY_NOT_REACHED();
            case Media::ColorPrimaries::BT709:
                return SkNamedPrimaries::kRec709;
            case Media::ColorPrimaries::BT470M:
                return SkNamedPrimaries::kRec470SystemM;
            case Media::ColorPrimaries::BT470BG:
                return SkNamedPrimaries::kRec470SystemBG;
            case Media::ColorPrimaries::BT601:
                return SkNamedPrimaries::kRec601;
            case Media::ColorPrimaries::SMPTE240:
                return SkNamedPrimaries::kSMPTE_ST_240;
            case Media::ColorPrimaries::GenericFilm:
                return SkNamedPrimaries::kGenericFilm;
            case Media::ColorPrimaries::BT2020:
                return SkNamedPrimaries::kRec2020;
            case Media::ColorPrimaries::SMPTE431:
                return SkNamedPrimaries::kSMPTE_RP_431_2;
            case Media::ColorPrimaries::SMPTE432:
                return SkNamedPrimaries::kSMPTE_EG_432_1;
            case Media::ColorPrimaries::EBU3213:
                return SkNamedPrimaries::kITU_T_H273_Value22;
            }
            return SkNamedPrimaries::kRec709;
        }();
        skcms_Matrix3x3 result;
        VERIFY(primaries.toXYZD50(&result));
        return result;
    }();

    auto transfer_function = [&] {
        switch (cicp.transfer_characteristics()) {
        case Media::TransferCharacteristics::Unspecified:
        case Media::TransferCharacteristics::Reserved:
            return SkNamedTransferFn::kRec709;
        case Media::TransferCharacteristics::BT709:
            return SkNamedTransferFn::kRec709;
        case Media::TransferCharacteristics::BT470M:
            return SkNamedTransferFn::kRec470SystemM;
        case Media::TransferCharacteristics::BT470BG:
            return SkNamedTransferFn::kRec470SystemBG;
        case Media::TransferCharacteristics::BT601:
            return SkNamedTransferFn::kRec601;
        case Media::TransferCharacteristics::SMPTE240:
            return SkNamedTransferFn::kSMPTE_ST_240;
        case Media::TransferCharacteristics::Linear:
            return SkNamedTransferFn::kLinear;
        case Media::TransferCharacteristics::Log100:
        case Media::TransferCharacteristics::Log100Sqrt10:
            dbgln("Logarithmic transfer characteristics are not supported, using sRGB.");
            return SkNamedTransferFn::kSRGB;
        case Media::TransferCharacteristics::IEC61966:
            return SkNamedTransferFn::kIEC61966_2_4;
        case Media::TransferCharacteristics::BT1361:
            dbgln("BT.1361 transfer characteristics are not supported, using sRGB.");
            return SkNamedTransferFn::kSRGB;
        case Media::TransferCharacteristics::SRGB:
            return SkNamedTransferFn::kSRGB;
        case Media::TransferCharacteristics::BT2020BitDepth10:
            return SkNamedTransferFn::kRec2020_10bit;
        case Media::TransferCharacteristics::BT2020BitDepth12:
            return SkNamedTransferFn::kRec2020_12bit;
        case Media::TransferCharacteristics::SMPTE2084:
            return SkNamedTransferFn::kPQ;
        case Media::TransferCharacteristics::SMPTE428:
            return SkNamedTransferFn::kSMPTE_ST_428_1;
        case Media::TransferCharacteristics::HLG:
            return SkNamedTransferFn::kHLG;
        }
        return SkNamedTransferFn::kRec709;
    }();

    return SkColorSpace::MakeRGB(transfer_function, gamut);
}

bool ImmutableBitmap::ensure_sk_image(SkiaBackendContext& context) const
{
    if (m_impl->context) {
        VERIFY(m_impl->context.ptr() == &context);
        return true;
    }

    context.lock();
    ScopeGuard unlock_guard = [&context] {
        context.unlock();
    };

    auto* gr_context = context.sk_context();

    // Bitmap-backed: try to upload raster image to GPU texture
    if (m_impl->sk_image) {
        if (!gr_context)
            return true; // No GPU, but raster image is still usable
        auto gpu_image = SkImages::TextureFromImage(gr_context, m_impl->sk_image.get(), skgpu::Mipmapped::kNo, skgpu::Budgeted::kYes);
        if (gpu_image) {
            m_impl->context = context;
            m_impl->sk_image = move(gpu_image);
        }
        return true;
    }

    // YUV-backed: GPU is required to decode YUV to RGB
    VERIFY(m_impl->yuv_data);

    if (!gr_context)
        return false; // No GPU, cannot create image from YUV data

    auto const& pixmaps = m_impl->yuv_data->skia_yuva_pixmaps();
    auto color_space = color_space_from_cicp(m_impl->yuv_data->cicp());

    auto sk_image = SkImages::TextureFromYUVAPixmaps(
        gr_context,
        pixmaps,
        skgpu::Mipmapped::kNo,
        false,
        color_space);

    if (!sk_image)
        return false;

    m_impl->context = context;
    m_impl->sk_image = move(sk_image);
    return true;
}

Color ImmutableBitmap::get_pixel(int x, int y) const
{
    return m_impl->bitmap->get_pixel(x, y);
}

static SkAlphaType to_skia_alpha_type(Gfx::AlphaType alpha_type)
{
    switch (alpha_type) {
    case AlphaType::Premultiplied:
        return kPremul_SkAlphaType;
    case AlphaType::Unpremultiplied:
        return kUnpremul_SkAlphaType;
    default:
        VERIFY_NOT_REACHED();
    }
}

NonnullRefPtr<ImmutableBitmap> ImmutableBitmap::create(NonnullRefPtr<Bitmap> bitmap, ColorSpace color_space)
{
    SkBitmap sk_bitmap;
    auto info = SkImageInfo::Make(bitmap->width(), bitmap->height(), to_skia_color_type(bitmap->format()), to_skia_alpha_type(bitmap->alpha_type()), color_space.color_space<sk_sp<SkColorSpace>>());
    sk_bitmap.installPixels(info, const_cast<void*>(static_cast<void const*>(bitmap->scanline(0))), bitmap->pitch());
    sk_bitmap.setImmutable();
    auto sk_image = sk_bitmap.asImage();

    ImmutableBitmapImpl impl {
        .context = nullptr,
        .sk_image = move(sk_image),
        .sk_bitmap = move(sk_bitmap),
        .bitmap = move(bitmap),
        .color_space = move(color_space),
        .yuv_data = nullptr,
    };
    return adopt_ref(*new ImmutableBitmap(make<ImmutableBitmapImpl>(move(impl))));
}

NonnullRefPtr<ImmutableBitmap> ImmutableBitmap::create(NonnullRefPtr<Bitmap> bitmap, AlphaType alpha_type, ColorSpace color_space)
{
    // Convert the source bitmap to the right alpha type on a mismatch. We want to do this when converting from a
    // Bitmap to an ImmutableBitmap, since at that point we usually know the right alpha type to use in context.
    auto source_bitmap = bitmap;
    if (source_bitmap->alpha_type() != alpha_type) {
        source_bitmap = MUST(bitmap->clone());
        source_bitmap->set_alpha_type_destructive(alpha_type);
    }

    return create(source_bitmap, move(color_space));
}

NonnullRefPtr<ImmutableBitmap> ImmutableBitmap::create_snapshot_from_painting_surface(NonnullRefPtr<PaintingSurface> painting_surface)
{
    painting_surface->lock_context();
    auto sk_image = painting_surface->sk_image_snapshot<sk_sp<SkImage>>();
    painting_surface->unlock_context();

    ImmutableBitmapImpl impl {
        .context = painting_surface->skia_backend_context(),
        .sk_image = move(sk_image),
        .sk_bitmap = {},
        .bitmap = nullptr,
        .color_space = {},
        .yuv_data = nullptr,
    };
    return adopt_ref(*new ImmutableBitmap(make<ImmutableBitmapImpl>(move(impl))));
}

ImmutableBitmap::ImmutableBitmap(NonnullOwnPtr<ImmutableBitmapImpl> impl)
    : m_impl(move(impl))
{
}

ImmutableBitmap::~ImmutableBitmap()
{
    lock_context();
    m_impl->sk_image = nullptr;
    unlock_context();
}

void ImmutableBitmap::lock_context()
{
    auto& context = m_impl->context;
    if (context)
        context->lock();
}

void ImmutableBitmap::unlock_context()
{
    auto& context = m_impl->context;
    if (context)
        context->unlock();
}

#endif // AK_OS_RINOS

}
