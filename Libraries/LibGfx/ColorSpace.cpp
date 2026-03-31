/*
 * Copyright (c) 2024, Lucas Chollet <lucas.chollet@serenityos.org>
 *
 * SPDX-License-Identifier: BSD-2-Clause
 */

#include <AK/ByteBuffer.h>
#include <AK/Types.h>
#include <LibGfx/ColorSpace.h>
#include <LibIPC/Decoder.h>
#include <LibIPC/Encoder.h>

#ifndef AK_OS_RINOS
#include <core/SkColorSpace.h>
#include <core/SkData.h>
#endif

namespace Gfx {

namespace Details {

#ifdef AK_OS_RINOS
struct ColorSpaceImpl {
    bool is_srgb { true };
};
#else
struct ColorSpaceImpl {
    sk_sp<SkColorSpace> color_space;
};
#endif

}

ColorSpace::ColorSpace()
    : m_color_space(make<Details::ColorSpaceImpl>())
{
}

#ifdef AK_OS_RINOS
ColorSpace::ColorSpace(ColorSpace const& other)
    : m_color_space(make<Details::ColorSpaceImpl>(Details::ColorSpaceImpl { other.m_color_space->is_srgb }))
{
}

ColorSpace& ColorSpace::operator=(ColorSpace const& other)
{
    m_color_space = make<Details::ColorSpaceImpl>(Details::ColorSpaceImpl { other.m_color_space->is_srgb });
    return *this;
}
#else
ColorSpace::ColorSpace(ColorSpace const& other)
    : m_color_space(make<Details::ColorSpaceImpl>(other.m_color_space->color_space))
{
}

ColorSpace& ColorSpace::operator=(ColorSpace const& other)
{
    m_color_space = make<Details::ColorSpaceImpl>(other.m_color_space->color_space);
    return *this;
}
#endif

ColorSpace::ColorSpace(ColorSpace&& other) = default;
ColorSpace& ColorSpace::operator=(ColorSpace&&) = default;
ColorSpace::~ColorSpace() = default;

ColorSpace::ColorSpace(NonnullOwnPtr<Details::ColorSpaceImpl>&& color_space)
    : m_color_space(move(color_space))
{
}

#ifdef AK_OS_RINOS
ErrorOr<ColorSpace> ColorSpace::from_cicp(Media::CodingIndependentCodePoints cicp)
{
    if (cicp.matrix_coefficients() != Media::MatrixCoefficients::Identity)
        return Error::from_string_literal("Unsupported matrix coefficients for CICP");

    if (cicp.video_full_range_flag() != Media::VideoFullRangeFlag::Full)
        return Error::from_string_literal("Unsupported matrix coefficients for CICP");

    switch (cicp.color_primaries()) {
    case Media::ColorPrimaries::BT709:
        break;
    default:
        return Error::from_string_literal("FIXME: Unsupported color primaries");
    }

    switch (cicp.transfer_characteristics()) {
    case Media::TransferCharacteristics::SRGB:
        break;
    default:
        return Error::from_string_literal("FIXME: Unsupported transfer function");
    }

    return ColorSpace {};
}

ErrorOr<ColorSpace> ColorSpace::load_from_icc_bytes(ReadonlyBytes)
{
    // Without Skia/skcms we cannot parse ICC profiles; return default sRGB.
    return ColorSpace {};
}
#else
ErrorOr<ColorSpace> ColorSpace::from_cicp(Media::CodingIndependentCodePoints cicp)
{
    if (cicp.matrix_coefficients() != Media::MatrixCoefficients::Identity)
        return Error::from_string_literal("Unsupported matrix coefficients for CICP");

    if (cicp.video_full_range_flag() != Media::VideoFullRangeFlag::Full)
        return Error::from_string_literal("Unsupported matrix coefficients for CICP");

    skcms_Matrix3x3 gamut = SkNamedGamut::kSRGB;
    switch (cicp.color_primaries()) {
    case Media::ColorPrimaries::BT709:
        gamut = SkNamedGamut::kSRGB;
        break;
    case Media::ColorPrimaries::BT2020:
        gamut = SkNamedGamut::kRec2020;
        break;
    case Media::ColorPrimaries::XYZ:
        gamut = SkNamedGamut::kXYZ;
        break;
    case Media::ColorPrimaries::SMPTE432:
        gamut = SkNamedGamut::kDisplayP3;
        break;
    default:
        return Error::from_string_literal("FIXME: Unsupported color primaries");
    }

    skcms_TransferFunction transfer_function = SkNamedTransferFn::kSRGB;
    switch (cicp.transfer_characteristics()) {
    case Media::TransferCharacteristics::Linear:
        transfer_function = SkNamedTransferFn::kLinear;
        break;
    case Media::TransferCharacteristics::SRGB:
        transfer_function = SkNamedTransferFn::kSRGB;
        break;
    case Media::TransferCharacteristics::SMPTE2084:
        transfer_function = SkNamedTransferFn::kPQ;
        break;
    case Media::TransferCharacteristics::HLG:
        transfer_function = SkNamedTransferFn::kHLG;
        break;
    default:
        return Error::from_string_literal("FIXME: Unsupported transfer function");
    }

    return ColorSpace { make<Details::ColorSpaceImpl>(SkColorSpace::MakeRGB(transfer_function, gamut)) };
}

ErrorOr<ColorSpace> ColorSpace::load_from_icc_bytes(ReadonlyBytes icc_bytes)
{
    if (icc_bytes.size() != 0) {
        skcms_ICCProfile icc_profile {};
        if (!skcms_Parse(icc_bytes.data(), icc_bytes.size(), &icc_profile))
            return Error::from_string_literal("Failed to parse the ICC profile");

        auto color_space_result = SkColorSpace::Make(icc_profile);

        if (!color_space_result) {
            if (icc_profile.has_trc && icc_profile.has_toXYZD50) {
                skcms_TransferFunction transfer_function;
                float max_error;

                if (skcms_ApproximateCurve(&icc_profile.trc[0], &transfer_function, &max_error)) {
                    color_space_result = SkColorSpace::MakeRGB(transfer_function, icc_profile.toXYZD50);
                }
            }
        }

        return ColorSpace { make<Details::ColorSpaceImpl>(color_space_result) };
    }
    return ColorSpace {};
}

template<>
sk_sp<SkColorSpace>& ColorSpace::color_space()
{
    return m_color_space->color_space;
}
#endif

}

namespace IPC {

template<>
ErrorOr<void> encode(Encoder& encoder, Gfx::ColorSpace const& color_space)
{
#ifdef AK_OS_RINOS
    TRY(encoder.encode<u64>(color_space.m_color_space->is_srgb ? 1 : 0));
    return {};
#else
    if (!color_space.m_color_space->color_space) {
        TRY(encoder.encode<u64>(0));
        return {};
    }
    auto serialized = color_space.m_color_space->color_space->serialize();
    TRY(encoder.encode<u64>(serialized->size()));
    TRY(encoder.append(serialized->bytes(), serialized->size()));
    return {};
#endif
}

template<>
ErrorOr<Gfx::ColorSpace> decode(Decoder& decoder)
{
#ifdef AK_OS_RINOS
    auto marker = TRY(decoder.decode<u64>());
    (void)marker;
    return Gfx::ColorSpace {};
#else
    // Color space profiles shouldn't be larger than 1 MiB
    static constexpr u64 MAX_COLOR_SPACE_SIZE = 1 * MiB;

    auto size = TRY(decoder.decode<u64>());
    if (size == 0)
        return Gfx::ColorSpace {};

    if (size > MAX_COLOR_SPACE_SIZE)
        return Error::from_string_literal("IPC: Color space size exceeds maximum allowed");

    auto buffer = TRY(ByteBuffer::create_uninitialized(size));
    TRY(decoder.decode_into(buffer.bytes()));

    auto color_space = SkColorSpace::Deserialize(buffer.data(), buffer.size());
    if (!color_space)
        return Error::from_string_literal("IPC: Failed to deserialize color space");

    return Gfx::ColorSpace { make<::Gfx::Details::ColorSpaceImpl>(move(color_space)) };
#endif
}

}
