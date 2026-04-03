/*
 * Copyright (c) 2026, RinOS contributors.
 *
 * SPDX-License-Identifier: BSD-2-Clause
 */

#include <AK/Memory.h>
#include <LibGfx/ImageFormats/WebPLoader.h>
#include "../../../../webp/src/webp/decode.h"

namespace Gfx {

struct WebPLoadingContext {
    enum class State {
        NotDecoded,
        Error,
        Decoded,
    };

    explicit WebPLoadingContext(ReadonlyBytes image_data)
        : data(image_data)
    {
    }

    ErrorOr<void> decode()
    {
        bitmap = TRY(Bitmap::create(BitmapFormat::BGRA8888, AlphaType::Unpremultiplied, size));
        auto* decode_result = WebPDecodeBGRAInto(
            data.data(),
            data.size(),
            bitmap->scanline_u8(0),
            bitmap->data_size(),
            static_cast<int>(bitmap->pitch()));
        if (!decode_result)
            return Error::from_string_literal("WebPImageDecoderPlugin: Failed to decode WebP data");
        return {};
    }

    State state { State::NotDecoded };
    ReadonlyBytes data;
    IntSize size;
    RefPtr<Bitmap> bitmap;
};

WebPImageDecoderPlugin::WebPImageDecoderPlugin(ReadonlyBytes data, OwnPtr<WebPLoadingContext> context)
    : m_context(move(context))
{
    m_context->data = data;
}

WebPImageDecoderPlugin::~WebPImageDecoderPlugin() = default;

bool WebPImageDecoderPlugin::sniff(ReadonlyBytes data)
{
    return data.size() >= 12
        && data[0] == 'R'
        && data[1] == 'I'
        && data[2] == 'F'
        && data[3] == 'F'
        && data[8] == 'W'
        && data[9] == 'E'
        && data[10] == 'B'
        && data[11] == 'P';
}

ErrorOr<NonnullOwnPtr<ImageDecoderPlugin>> WebPImageDecoderPlugin::create(ReadonlyBytes data)
{
    int width = 0;
    int height = 0;
    if (!WebPGetInfo(data.data(), data.size(), &width, &height) || width <= 0 || height <= 0)
        return Error::from_string_literal("WebPImageDecoderPlugin: Failed to read WebP header");

    auto context = adopt_own(*new WebPLoadingContext(data));
    context->size = { width, height };
    return adopt_own(*new WebPImageDecoderPlugin(data, move(context)));
}

IntSize WebPImageDecoderPlugin::size()
{
    return m_context->size;
}

bool WebPImageDecoderPlugin::is_animated()
{
    return false;
}

size_t WebPImageDecoderPlugin::loop_count()
{
    return 0;
}

size_t WebPImageDecoderPlugin::frame_count()
{
    return 1;
}

size_t WebPImageDecoderPlugin::first_animated_frame_index()
{
    return 0;
}

ErrorOr<ImageFrameDescriptor> WebPImageDecoderPlugin::frame(size_t index, Optional<IntSize>)
{
    if (index != 0)
        return Error::from_string_literal("WebPImageDecoderPlugin: Invalid frame index");

    if (m_context->state == WebPLoadingContext::State::Error)
        return Error::from_string_literal("WebPImageDecoderPlugin: Decoding failed");

    if (m_context->state == WebPLoadingContext::State::NotDecoded) {
        auto result = m_context->decode();
        if (result.is_error()) {
            m_context->state = WebPLoadingContext::State::Error;
            return result.release_error();
        }
        m_context->state = WebPLoadingContext::State::Decoded;
    }

    return ImageFrameDescriptor { *m_context->bitmap, 0 };
}

int WebPImageDecoderPlugin::frame_duration(size_t)
{
    return 0;
}

ErrorOr<Optional<ReadonlyBytes>> WebPImageDecoderPlugin::icc_data()
{
    return OptionalNone {};
}

}
