/*
 * Copyright (c) 2025, mikiubo <michele.uboldi@gmail.com>
 *
 * SPDX-License-Identifier: BSD-2-Clause
 */

#ifdef AK_OS_RINOS

#include <AK/ByteBuffer.h>
#include <LibCrypto/Hash/SHAKE.h>
#include <LibCrypto/RinCryptoImpl.h>

namespace Crypto::Hash {

SHAKE::SHAKE(SHAKEKind kind)
    : m_kind(kind)
{
}

ErrorOr<ByteBuffer> SHAKE::digest(
    ReadonlyBytes data,
    u32 length,
    Optional<ReadonlyBytes> customization,
    Optional<ReadonlyBytes> function_name) const
{
    bool wants_cshake = (customization.has_value() && !customization->is_empty()) || (function_name.has_value() && !function_name->is_empty());
    if (wants_cshake)
        return Error::from_string_literal("cSHAKE with non-empty N or S is not supported on RinOS");

    if (length % 8 != 0)
        return Error::from_string_literal("SHAKE output length must be a multiple of 8 bits");

    size_t output_bytes = length / 8;

    rin_keccak_ctx ctx;
    if (m_kind == SHAKEKind::CSHAKE128)
        rin_shake128_init(&ctx);
    else
        rin_shake256_init(&ctx);

    rin_keccak_update(&ctx, data.data(), data.size());

    // Finalize with XOF padding
    ctx.digest_len = output_bytes;
    auto buf = TRY(ByteBuffer::create_uninitialized(output_bytes));
    rin_keccak_final(&ctx, buf.data());

    // If output > rate, squeeze additional blocks
    if (output_bytes > ctx.rate) {
        rin_shake_squeeze(&ctx, buf.data() + ctx.rate, output_bytes - ctx.rate);
    }

    return buf;
}

}

#else // !AK_OS_RINOS

#include <AK/ByteBuffer.h>
#include <LibCrypto/Hash/SHAKE.h>
#include <LibCrypto/OpenSSL.h>
#include <openssl/evp.h>

namespace Crypto::Hash {

SHAKE::SHAKE(SHAKEKind kind)
{
    m_md = (kind == SHAKEKind::CSHAKE128) ? EVP_shake128() : EVP_shake256();
}

ErrorOr<ByteBuffer> SHAKE::digest(
    ReadonlyBytes data,
    u32 length,
    Optional<ReadonlyBytes> customization,
    Optional<ReadonlyBytes> function_name) const
{
    bool wants_cshake = (customization.has_value() && !customization->is_empty()) || (function_name.has_value() && !function_name->is_empty());

    if (wants_cshake) {
        // FIXME: Implement cSHAKE with non-empty N or S
        return Error::from_string_literal("cSHAKE with non-empty N or S is not supported yet (OpenSSL EVP limitation)");
    }

    if (length % 8 != 0) {
        return Error::from_string_literal("SHAKE output length must be a multiple of 8 bits");
    }

    size_t output_bytes = length / 8;
    auto buf = TRY(ByteBuffer::create_uninitialized(output_bytes));

    auto ctx = TRY(OpenSSL_MD_CTX::wrap(EVP_MD_CTX_new()));

    OPENSSL_TRY(EVP_DigestInit_ex(ctx.ptr(), m_md, nullptr));

    OPENSSL_TRY(EVP_DigestUpdate(ctx.ptr(), data.data(), data.size()));

    OPENSSL_TRY(EVP_DigestFinalXOF(ctx.ptr(), buf.data(), output_bytes));

    return buf;
}

}

#endif // AK_OS_RINOS
