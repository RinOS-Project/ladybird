/*
 * Copyright (c) 2025, Altomani Gianluca <altomanigianluca@gmail.com>
 *
 * SPDX-License-Identifier: BSD-2-Clause
 */

#ifdef AK_OS_RINOS

#include <LibCrypto/Hash/HKDF.h>
#include <LibCrypto/Authentication/HMAC.h>
#include <string.h>

namespace Crypto::Hash {

HKDF::HKDF(HashKind hash_kind)
    : m_hash_kind(hash_kind)
{
}

ErrorOr<ByteBuffer> HKDF::derive_key(Optional<ReadonlyBytes> maybe_salt, ReadonlyBytes ikm, ReadonlyBytes info, u32 key_length_bytes)
{
    // HKDF-Extract: PRK = HMAC-Hash(salt, IKM)
    Authentication::HMAC extract_hmac(m_hash_kind, maybe_salt.has_value() ? *maybe_salt : ReadonlyBytes {});
    auto prk = extract_hmac.process(ikm.data(), ikm.size());
    size_t hash_len = prk.size();

    // HKDF-Expand
    size_t n = (key_length_bytes + hash_len - 1) / hash_len;
    if (n > 255)
        return Error::from_string_literal("HKDF output too long");

    auto okm = TRY(ByteBuffer::create_uninitialized(key_length_bytes));
    u8 t_buf[64] = {}; // max digest size
    size_t t_len = 0;
    size_t offset = 0;

    for (size_t i = 1; i <= n; i++) {
        Authentication::HMAC hmac(m_hash_kind, prk);
        if (t_len > 0)
            hmac.update(t_buf, t_len);
        hmac.update(info.data(), info.size());
        u8 counter = static_cast<u8>(i);
        hmac.update(&counter, 1);
        auto t = hmac.digest();
        t_len = t.size();
        memcpy(t_buf, t.data(), t_len);

        size_t copy_len = min(t_len, static_cast<size_t>(key_length_bytes) - offset);
        memcpy(okm.data() + offset, t_buf, copy_len);
        offset += copy_len;
    }

    return okm;
}

}

#else // !AK_OS_RINOS

#include <LibCrypto/Hash/HKDF.h>
#include <LibCrypto/OpenSSL.h>

#include <openssl/core_names.h>
#include <openssl/kdf.h>
#include <openssl/params.h>

namespace Crypto::Hash {

HKDF::HKDF(HashKind hash_kind)
    : m_kdf(EVP_KDF_fetch(nullptr, "HKDF", nullptr))
    , m_hash_kind(hash_kind)
{
}

ErrorOr<ByteBuffer> HKDF::derive_key(Optional<ReadonlyBytes> maybe_salt, ReadonlyBytes key, ReadonlyBytes info, u32 key_length_bytes)
{
    auto hash_name = TRY(hash_kind_to_openssl_digest_name(m_hash_kind));

    auto ctx = TRY(OpenSSL_KDF_CTX::wrap(EVP_KDF_CTX_new(m_kdf)));

    OSSL_PARAM params[] = {
        OSSL_PARAM_utf8_string(OSSL_KDF_PARAM_DIGEST, const_cast<char*>(hash_name.characters_without_null_termination()), hash_name.length()),
        OSSL_PARAM_octet_string(OSSL_KDF_PARAM_KEY, const_cast<u8*>(key.data()), key.size()),
        OSSL_PARAM_octet_string(OSSL_KDF_PARAM_INFO, const_cast<u8*>(info.data()), info.size()),
        OSSL_PARAM_END,
        OSSL_PARAM_END,
    };

    if (maybe_salt.has_value()) {
        static constexpr u8 empty_salt[0] {};

        // FIXME: As of openssl 3.5.1, we can no longer pass a null salt pointer. This seems like a mistake; we should
        //        check if this is still the case in the next openssl release. See:
        //        https://github.com/openssl/openssl/pull/27305#discussion_r2198316685
        auto salt = maybe_salt->is_null() ? ReadonlySpan<u8> { empty_salt, 0 } : *maybe_salt;

        params[3] = OSSL_PARAM_octet_string(OSSL_KDF_PARAM_SALT, const_cast<u8*>(salt.data()), salt.size());
    }

    auto buf = TRY(ByteBuffer::create_uninitialized(key_length_bytes));
    OPENSSL_TRY(EVP_KDF_derive(ctx.ptr(), buf.data(), key_length_bytes, params));

    return buf;
}

}

#endif // AK_OS_RINOS
