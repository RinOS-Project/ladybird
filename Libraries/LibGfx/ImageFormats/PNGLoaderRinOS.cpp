/*
 * Copyright (c) 2026, RinOS contributors.
 *
 * SPDX-License-Identifier: BSD-2-Clause
 */

#include <AK/Checked.h>
#include <AK/Memory.h>
#include <AK/Vector.h>
#include <LibGfx/ImageFormats/PNGLoader.h>
#include "../../../../png/rpng.h"

namespace Gfx {

struct PNGLoadingContext {
    enum class State {
        NotDecoded,
        Error,
        Decoded,
    };

    explicit PNGLoadingContext(ReadonlyBytes image_data)
        : data(image_data)
    {
    }

    ErrorOr<void> decode()
    {
        Checked<size_t> pixel_count = static_cast<size_t>(size.width());
        pixel_count *= static_cast<size_t>(size.height());
        if (pixel_count.has_overflow())
            return Error::from_string_literal("PNGImageDecoderPlugin: Pixel count overflow");

        Vector<u32> pixels;
        TRY(pixels.try_resize(pixel_count.value()));

        auto rc = rpng_decode_rgba(data.data(), data.size(), pixels.data(), size.width(), size.height());
        if (rc != RPNG_OK)
            return Error::from_string_literal("PNGImageDecoderPlugin: Failed to decode PNG data");

        bitmap = TRY(Bitmap::create(BitmapFormat::BGRA8888, AlphaType::Unpremultiplied, size));
        for (int y = 0; y < size.height(); ++y)
            __builtin_memcpy(bitmap->scanline(y), pixels.data() + static_cast<size_t>(y) * size.width(), static_cast<size_t>(size.width()) * sizeof(u32));

        return {};
    }

    State state { State::NotDecoded };
    ReadonlyBytes data;
    IntSize size;
    RefPtr<Bitmap> bitmap;
};

PNGImageDecoderPlugin::PNGImageDecoderPlugin(ReadonlyBytes data)
    : m_context(adopt_own(*new PNGLoadingContext(data)))
{
}

PNGImageDecoderPlugin::~PNGImageDecoderPlugin() = default;

bool PNGImageDecoderPlugin::sniff(ReadonlyBytes data)
{
    constexpr u8 signature[] = { 0x89, 0x50, 0x4e, 0x47, 0x0d, 0x0a, 0x1a, 0x0a };
    if (data.size() < sizeof(signature))
        return false;
    for (size_t i = 0; i < sizeof(signature); ++i) {
        if (data[i] != signature[i])
            return false;
    }
    return true;
}

ErrorOr<NonnullOwnPtr<ImageDecoderPlugin>> PNGImageDecoderPlugin::create(ReadonlyBytes bytes)
{
    auto decoder = adopt_own(*new PNGImageDecoderPlugin(bytes));
    TRY(decoder->initialize());
    return decoder;
}

ErrorOr<void> PNGImageDecoderPlugin::initialize()
{
    int width = 0;
    int height = 0;
    if (rpng_get_info(m_context->data.data(), m_context->data.size(), &width, &height) != RPNG_OK || width <= 0 || height <= 0)
        return Error::from_string_literal("PNGImageDecoderPlugin: Failed to read PNG header");

    m_context->size = { width, height };
    return {};
}

IntSize PNGImageDecoderPlugin::size()
{
    return m_context->size;
}

bool PNGImageDecoderPlugin::is_animated()
{
    return false;
}

size_t PNGImageDecoderPlugin::loop_count()
{
    return 0;
}

size_t PNGImageDecoderPlugin::frame_count()
{
    return 1;
}

size_t PNGImageDecoderPlugin::first_animated_frame_index()
{
    return 0;
}

ErrorOr<ImageFrameDescriptor> PNGImageDecoderPlugin::frame(size_t index, Optional<IntSize>)
{
    if (index != 0)
        return Error::from_string_literal("PNGImageDecoderPlugin: Invalid frame index");

    if (m_context->state == PNGLoadingContext::State::Error)
        return Error::from_string_literal("PNGImageDecoderPlugin: Decoding failed");

    if (m_context->state == PNGLoadingContext::State::NotDecoded) {
        auto result = m_context->decode();
        if (result.is_error()) {
            m_context->state = PNGLoadingContext::State::Error;
            return result.release_error();
        }
        m_context->state = PNGLoadingContext::State::Decoded;
    }

    return ImageFrameDescriptor { *m_context->bitmap, 0 };
}

int PNGImageDecoderPlugin::frame_duration(size_t)
{
    return 0;
}

Optional<Metadata const&> PNGImageDecoderPlugin::metadata()
{
    return OptionalNone {};
}

ErrorOr<Optional<Media::CodingIndependentCodePoints>> PNGImageDecoderPlugin::cicp()
{
    return OptionalNone {};
}

ErrorOr<Optional<ReadonlyBytes>> PNGImageDecoderPlugin::icc_data()
{
    return OptionalNone {};
}

}
