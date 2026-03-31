/*
 * Copyright (c) 2020, Ali Mohammad Pur <mpfard@serenityos.org>
 *
 * SPDX-License-Identifier: BSD-2-Clause
 */

#pragma once

#ifdef AK_OS_RINOS

#include <AK/ByteString.h>
#include <LibCrypto/RinTLSHashFunction.h>
#include <LibCrypto/RinCryptoImpl.h>

namespace Crypto::Hash {

class MD5 final : public RinTLSHashFunction<MD5, 512, 128, rin_md5_ctx> {
    AK_MAKE_NONCOPYABLE(MD5);

public:
    MD5() = default;

    virtual ByteString class_name() const override { return "MD5"; }

    static void rin_init(rin_md5_ctx* ctx) { rin_md5_init(ctx); }
    static void rin_update(rin_md5_ctx* ctx, u8 const* data, size_t len) { rin_md5_update(ctx, data, len); }
    static void rin_final(rin_md5_ctx* ctx, u8* digest) { rin_md5_final(ctx, digest); }
};

}

#else // !AK_OS_RINOS

#include <AK/ByteString.h>
#include <LibCrypto/Hash/OpenSSLHashFunction.h>

namespace Crypto::Hash {

class MD5 final : public OpenSSLHashFunction<MD5, 512, 128> {
    AK_MAKE_NONCOPYABLE(MD5);

public:
    explicit MD5(EVP_MD_CTX* context);

    virtual ByteString class_name() const override
    {
        return "MD5";
    }
};

}

#endif // AK_OS_RINOS
