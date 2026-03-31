/*
 * Copyright (c) 2025, Miguel Sacristán Izcue <miguel_tete17@hotmail.com>
 *
 * SPDX-License-Identifier: BSD-2-Clause
 */

#pragma once

#ifdef AK_OS_RINOS

#include <AK/Noncopyable.h>
#include <LibCrypto/RinTLSHashFunction.h>
#include <LibCrypto/RinCryptoImpl.h>

namespace Crypto::Hash {

class SHA3_256 final : public RinTLSHashFunction<SHA3_256, 1088, 256, rin_keccak_ctx> {
    AK_MAKE_NONCOPYABLE(SHA3_256);

public:
    SHA3_256() = default;

    virtual ByteString class_name() const override { return "SHA3-256"; }

    static void rin_init(rin_keccak_ctx* ctx) { rin_sha3_256_init(ctx); }
    static void rin_update(rin_keccak_ctx* ctx, u8 const* data, size_t len) { rin_keccak_update(ctx, data, len); }
    static void rin_final(rin_keccak_ctx* ctx, u8* digest) { rin_keccak_final(ctx, digest); }
};

class SHA3_384 final : public RinTLSHashFunction<SHA3_384, 832, 384, rin_keccak_ctx> {
    AK_MAKE_NONCOPYABLE(SHA3_384);

public:
    SHA3_384() = default;

    virtual ByteString class_name() const override { return "SHA3-384"; }

    static void rin_init(rin_keccak_ctx* ctx) { rin_sha3_384_init(ctx); }
    static void rin_update(rin_keccak_ctx* ctx, u8 const* data, size_t len) { rin_keccak_update(ctx, data, len); }
    static void rin_final(rin_keccak_ctx* ctx, u8* digest) { rin_keccak_final(ctx, digest); }
};

class SHA3_512 final : public RinTLSHashFunction<SHA3_512, 576, 512, rin_keccak_ctx> {
    AK_MAKE_NONCOPYABLE(SHA3_512);

public:
    SHA3_512() = default;

    virtual ByteString class_name() const override { return "SHA3-512"; }

    static void rin_init(rin_keccak_ctx* ctx) { rin_sha3_512_init(ctx); }
    static void rin_update(rin_keccak_ctx* ctx, u8 const* data, size_t len) { rin_keccak_update(ctx, data, len); }
    static void rin_final(rin_keccak_ctx* ctx, u8* digest) { rin_keccak_final(ctx, digest); }
};

}

#else // !AK_OS_RINOS

#include <AK/Noncopyable.h>
#include <LibCrypto/Hash/OpenSSLHashFunction.h>

namespace Crypto::Hash {

class SHA3_256 final : public OpenSSLHashFunction<SHA3_256, 1088, 256> {
    AK_MAKE_NONCOPYABLE(SHA3_256);

public:
    explicit SHA3_256(EVP_MD_CTX* context);

    virtual ByteString class_name() const override
    {
        return "SHA3-256";
    }
};

class SHA3_384 final : public OpenSSLHashFunction<SHA3_384, 832, 384> {
    AK_MAKE_NONCOPYABLE(SHA3_384);

public:
    explicit SHA3_384(EVP_MD_CTX* context);

    virtual ByteString class_name() const override
    {
        return "SHA3-384";
    }
};

class SHA3_512 final : public OpenSSLHashFunction<SHA3_512, 576, 512> {
    AK_MAKE_NONCOPYABLE(SHA3_512);

public:
    explicit SHA3_512(EVP_MD_CTX* context);

    virtual ByteString class_name() const override
    {
        return "SHA3-512";
    }
};

}

#endif // AK_OS_RINOS
