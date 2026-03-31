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
#include <crypto/sha1.h>
}

namespace Crypto::Hash {

class SHA1 final : public RinTLSHashFunction<SHA1, 512, 160, sha1_ctx_t> {
    AK_MAKE_NONCOPYABLE(SHA1);

public:
    SHA1() = default;

    virtual ByteString class_name() const override { return "SHA1"; }

    static void rin_init(sha1_ctx_t* ctx) { sha1_init(ctx); }
    static void rin_update(sha1_ctx_t* ctx, u8 const* data, size_t len) { sha1_update(ctx, data, (rin_size_t)len); }
    static void rin_final(sha1_ctx_t* ctx, u8* digest) { sha1_final(ctx, digest); }
};

}

#else // !AK_OS_RINOS

#include <AK/ByteString.h>
#include <LibCrypto/Hash/OpenSSLHashFunction.h>

namespace Crypto::Hash {

class SHA1 final : public OpenSSLHashFunction<SHA1, 512, 160> {
    AK_MAKE_NONCOPYABLE(SHA1);

public:
    explicit SHA1(EVP_MD_CTX* context);

    virtual ByteString class_name() const override
    {
        return "SHA1";
    }
};

}

#endif // AK_OS_RINOS
