/*
 * Copyright (c) 2023, the SerenityOS developers
 *
 * SPDX-License-Identifier: BSD-2-Clause
 */

#pragma once

#ifdef AK_OS_RINOS

#include <AK/ByteString.h>
#include <LibCrypto/RinTLSHashFunction.h>
#include <LibCrypto/RinCryptoImpl.h>

namespace Crypto::Hash {

class BLAKE2b final : public RinTLSHashFunction<BLAKE2b, 1024, 512, rin_blake2b_ctx> {
    AK_MAKE_NONCOPYABLE(BLAKE2b);

public:
    BLAKE2b() = default;

    virtual ByteString class_name() const override { return "BLAKE2b"; }

    static void rin_init(rin_blake2b_ctx* ctx) { rin_blake2b_init(ctx, 64); }
    static void rin_update(rin_blake2b_ctx* ctx, u8 const* data, size_t len) { rin_blake2b_update(ctx, data, len); }
    static void rin_final(rin_blake2b_ctx* ctx, u8* digest) { rin_blake2b_final(ctx, digest); }
};

};

#else // !AK_OS_RINOS

#include <AK/ByteString.h>
#include <LibCrypto/Hash/OpenSSLHashFunction.h>

namespace Crypto::Hash {

class BLAKE2b final : public OpenSSLHashFunction<BLAKE2b, 1024, 512> {
    AK_MAKE_NONCOPYABLE(BLAKE2b);

public:
    explicit BLAKE2b(EVP_MD_CTX* context);

    virtual ByteString class_name() const override
    {
        return "BLAKE2b";
    }
};

};

#endif // AK_OS_RINOS
