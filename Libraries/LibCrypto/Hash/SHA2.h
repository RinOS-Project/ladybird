/*
 * Copyright (c) 2020, Ali Mohammad Pur <mpfard@serenityos.org>
 *
 * SPDX-License-Identifier: BSD-2-Clause
 */

#pragma once

#ifdef AK_OS_RINOS

#include <AK/ByteString.h>
#include <LibCrypto/RinTLSHashFunction.h>

extern "C" {
#include <crypto/sha256.h>
}

namespace Crypto::Hash {

class SHA256 final : public RinTLSHashFunction<SHA256, 512, 256, sha256_ctx> {
    AK_MAKE_NONCOPYABLE(SHA256);

public:
    SHA256() = default;

    virtual ByteString class_name() const override { return "SHA256"; }

    static void rin_init(sha256_ctx* ctx) { sha256_init(ctx); }
    static void rin_update(sha256_ctx* ctx, u8 const* data, size_t len) { sha256_update(ctx, data, (rin_size_t)len); }
    static void rin_final(sha256_ctx* ctx, u8* digest) { sha256_final(ctx, digest); }
};

class SHA384 final : public RinTLSHashFunction<SHA384, 1024, 384, sha384_ctx> {
    AK_MAKE_NONCOPYABLE(SHA384);

public:
    SHA384() = default;

    virtual ByteString class_name() const override { return "SHA384"; }

    static void rin_init(sha384_ctx* ctx) { sha384_init(ctx); }
    static void rin_update(sha384_ctx* ctx, u8 const* data, size_t len) { sha384_update(ctx, data, (rin_size_t)len); }
    static void rin_final(sha384_ctx* ctx, u8* digest) { sha384_final(ctx, digest); }
};

class SHA512 final : public RinTLSHashFunction<SHA512, 1024, 512, sha512_ctx> {
    AK_MAKE_NONCOPYABLE(SHA512);

public:
    SHA512() = default;

    virtual ByteString class_name() const override { return "SHA512"; }

    static void rin_init(sha512_ctx* ctx) { sha512_init(ctx); }
    static void rin_update(sha512_ctx* ctx, u8 const* data, size_t len) { sha512_update(ctx, data, (rin_size_t)len); }
    static void rin_final(sha512_ctx* ctx, u8* digest) { sha512_final(ctx, digest); }
};

}

#else // !AK_OS_RINOS

#include <AK/ByteString.h>
#include <LibCrypto/Hash/OpenSSLHashFunction.h>

namespace Crypto::Hash {

class SHA256 final : public OpenSSLHashFunction<SHA256, 512, 256> {
    AK_MAKE_NONCOPYABLE(SHA256);

public:
    explicit SHA256(EVP_MD_CTX* context);

    virtual ByteString class_name() const override
    {
        return "SHA256";
    }
};

class SHA384 final : public OpenSSLHashFunction<SHA384, 1024, 384> {
    AK_MAKE_NONCOPYABLE(SHA384);

public:
    explicit SHA384(EVP_MD_CTX* context);

    virtual ByteString class_name() const override
    {
        return "SHA384";
    }
};

class SHA512 final : public OpenSSLHashFunction<SHA512, 1024, 512> {
    AK_MAKE_NONCOPYABLE(SHA512);

public:
    explicit SHA512(EVP_MD_CTX* context);

    virtual ByteString class_name() const override
    {
        return "SHA512";
    }
};

}

#endif // AK_OS_RINOS
