/*
 * Copyright (c) 2026, RinOS contributors.
 *
 * SPDX-License-Identifier: BSD-2-Clause
 */

#include <AK/Checked.h>
#include <AK/Memory.h>
#include <AK/Vector.h>
#include <LibGfx/ImageFormats/JPEGLoader.h>
#include "../../../../jpeg/rinjpeg.h"

namespace Gfx {

struct JPEGLoadingContext {
    enum class State {
        NotDecoded,
        Error,
        Decoded,
    };

    explicit JPEGLoadingContext(ReadonlyBytes image_data)
        : data(image_data)
    {
    }

    ErrorOr<void> decode()
    {
        Checked<size_t> pixel_count = static_cast<size_t>(size.width());
        pixel_count *= static_cast<size_t>(size.height());
        if (pixel_count.has_overflow())
            return Error::from_string_literal("JPEGImageDecoderPlugin: Pixel count overflow");

        Vector<u32> pixels;
        TRY(pixels.try_resize(pixel_count.value()));

        auto rc = rjpeg_decode(data.data(), data.size(), pixels.data(), size.width(), size.height());
        if (rc != RJPEG_OK)
            return Error::from_string_literal("JPEGImageDecoderPlugin: Failed to decode JPEG data");

        bitmap = TRY(Bitmap::create(BitmapFormat::BGRx8888, size));
        for (int y = 0; y < size.height(); ++y)
            __builtin_memcpy(bitmap->scanline(y), pixels.data() + static_cast<size_t>(y) * size.width(), static_cast<size_t>(size.width()) * sizeof(u32));

        return {};
    }

    State state { State::NotDecoded };
    ReadonlyBytes data;
    IntSize size;
    RefPtr<Bitmap> bitmap;
};

JPEGImageDecoderPlugin::JPEGImageDecoderPlugin(NonnullOwnPtr<JPEGLoadingContext> context)
    : m_context(move(context))
{
}

JPEGImageDecoderPlugin::~JPEGImageDecoderPlugin() = default;

bool JPEGImageDecoderPlugin::sniff(ReadonlyBytes data)
{
    return data.size() > 3
        && data[0] == 0xff
        && data[1] == 0xd8
        && data[2] == 0xff;
}

ErrorOr<NonnullOwnPtr<ImageDecoderPlugin>> JPEGImageDecoderPlugin::create(ReadonlyBytes data)
{
    int width = 0;
    int height = 0;
    if (rjpeg_get_info(data.data(), data.size(), &width, &height) != RJPEG_OK || width <= 0 || height <= 0)
        return Error::from_string_literal("JPEGImageDecoderPlugin: Failed to read JPEG header");

    auto context = make<JPEGLoadingContext>(data);
    context->size = { width, height };
    return adopt_own(*new JPEGImageDecoderPlugin(move(context)));
}

IntSize JPEGImageDecoderPlugin::size()
{
    return m_context->size;
}

ErrorOr<ImageFrameDescriptor> JPEGImageDecoderPlugin::frame(size_t index, Optional<IntSize>)
{
    if (index != 0)
        return Error::from_string_literal("JPEGImageDecoderPlugin: Invalid frame index");

    if (m_context->state == JPEGLoadingContext::State::Error)
        return Error::from_string_literal("JPEGImageDecoderPlugin: Decoding failed");

    if (m_context->state == JPEGLoadingContext::State::NotDecoded) {
        auto result = m_context->decode();
        if (result.is_error()) {
            m_context->state = JPEGLoadingContext::State::Error;
            return result.release_error();
        }
        m_context->state = JPEGLoadingContext::State::Decoded;
    }

    return ImageFrameDescriptor { *m_context->bitmap, 0 };
}

Optional<Metadata const&> JPEGImageDecoderPlugin::metadata()
{
    return OptionalNone {};
}

ErrorOr<Optional<ReadonlyBytes>> JPEGImageDecoderPlugin::icc_data()
{
    return OptionalNone {};
}

NaturalFrameFormat JPEGImageDecoderPlugin::natural_frame_format() const
{
    return NaturalFrameFormat::RGB;
}

ErrorOr<NonnullRefPtr<CMYKBitmap>> JPEGImageDecoderPlugin::cmyk_frame()
{
    return Error::from_string_literal("JPEGImageDecoderPlugin: CMYK not supported on RinOS");
}

}
