/*
 * Copyright (c) 2025, Altomani Gianluca <altomanigianluca@gmail.com>
 *
 * SPDX-License-Identifier: BSD-2-Clause
 */

#ifdef AK_OS_RINOS

#include <LibCrypto/Hash/PBKDF2.h>
#include <LibCrypto/Authentication/HMAC.h>
#include <string.h>

namespace Crypto::Hash {

PBKDF2::PBKDF2(HashKind hash_kind)
    : m_hash_kind(hash_kind)
{
}

ErrorOr<ByteBuffer> PBKDF2::derive_key(ReadonlyBytes password, ReadonlyBytes salt, u32 iterations, u32 key_length_bytes)
{
    // PBKDF2 (RFC 8018) using software HMAC
    Authentication::HMAC hmac_template(m_hash_kind, password);
    size_t h_len = hmac_template.digest_size();

    u32 block_count = (key_length_bytes + static_cast<u32>(h_len) - 1) / static_cast<u32>(h_len);
    auto output = TRY(ByteBuffer::create_uninitialized(key_length_bytes));
    size_t offset = 0;

    for (u32 i = 1; i <= block_count; i++) {
        // U1 = HMAC(password, salt || INT(i))
        Authentication::HMAC hmac(m_hash_kind, password);
        hmac.update(salt.data(), salt.size());
        u8 block_num[4];
        block_num[0] = static_cast<u8>(i >> 24);
        block_num[1] = static_cast<u8>(i >> 16);
        block_num[2] = static_cast<u8>(i >> 8);
        block_num[3] = static_cast<u8>(i);
        hmac.update(block_num, 4);
        auto u = hmac.digest();

        u8 t[64]; // max digest
        VERIFY(h_len <= sizeof(t));
        memcpy(t, u.data(), h_len);

        for (u32 j = 1; j < iterations; j++) {
            Authentication::HMAC hmac_iter(m_hash_kind, password);
            hmac_iter.update(u.data(), u.size());
            u = hmac_iter.digest();
            for (size_t k = 0; k < h_len; k++)
                t[k] ^= u[k];
        }

        size_t copy_len = min(h_len, static_cast<size_t>(key_length_bytes) - offset);
        memcpy(output.data() + offset, t, copy_len);
        offset += copy_len;
    }

    return output;
}

}

#else // !AK_OS_RINOS

#include <LibCrypto/Hash/PBKDF2.h>
#include <LibCrypto/OpenSSL.h>

#include <openssl/core_names.h>
#include <openssl/kdf.h>
#include <openssl/params.h>

namespace Crypto::Hash {

PBKDF2::PBKDF2(HashKind hash_kind)
    : m_kdf(EVP_KDF_fetch(nullptr, "PBKDF2", nullptr))
    , m_hash_kind(hash_kind)
{
}

ErrorOr<ByteBuffer> PBKDF2::derive_key(ReadonlyBytes password, ReadonlyBytes salt, u32 iterations, u32 key_length_bytes)
{
    auto hash_name = TRY(hash_kind_to_openssl_digest_name(m_hash_kind));

    auto ctx = TRY(OpenSSL_KDF_CTX::wrap(EVP_KDF_CTX_new(m_kdf)));

    OSSL_PARAM params[] = {
        OSSL_PARAM_utf8_string(OSSL_KDF_PARAM_DIGEST, const_cast<char*>(hash_name.characters_without_null_termination()), hash_name.length()),
        OSSL_PARAM_octet_string(OSSL_KDF_PARAM_PASSWORD, const_cast<u8*>(password.data()), password.size()),
        OSSL_PARAM_octet_string(OSSL_KDF_PARAM_SALT, const_cast<u8*>(salt.data()), salt.size()),
        OSSL_PARAM_uint32(OSSL_KDF_PARAM_ITER, &iterations),
        OSSL_PARAM_END,
    };

    auto buf = TRY(ByteBuffer::create_uninitialized(key_length_bytes));
    OPENSSL_TRY(EVP_KDF_derive(ctx.ptr(), buf.data(), key_length_bytes, params));

    return buf;
}

}

#endif // AK_OS_RINOS
