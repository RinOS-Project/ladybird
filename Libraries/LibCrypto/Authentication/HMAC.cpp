/*
 * Copyright (c) 2025, Altomani Gianluca <altomanigianluca@gmail.com>
 *
 * SPDX-License-Identifier: BSD-2-Clause
 */

#ifdef AK_OS_RINOS

#include <LibCrypto/Authentication/HMAC.h>
#include <string.h>

namespace Crypto::Authentication {

static size_t hash_block_size(Hash::HashKind kind)
{
    switch (kind) {
    case Hash::HashKind::MD5:
    case Hash::HashKind::SHA1:
    case Hash::HashKind::SHA256:
        return 64;
    case Hash::HashKind::SHA384:
    case Hash::HashKind::SHA512:
        return 128;
    case Hash::HashKind::SHA3_256:
        return 136;
    case Hash::HashKind::SHA3_384:
        return 104;
    case Hash::HashKind::SHA3_512:
        return 72;
    case Hash::HashKind::BLAKE2b:
        return 128;
    default:
        VERIFY_NOT_REACHED();
    }
}

static size_t hash_digest_size(Hash::HashKind kind)
{
    switch (kind) {
    case Hash::HashKind::MD5: return 16;
    case Hash::HashKind::SHA1: return 20;
    case Hash::HashKind::SHA256: return 32;
    case Hash::HashKind::SHA384: return 48;
    case Hash::HashKind::SHA512: return 64;
    case Hash::HashKind::SHA3_256: return 32;
    case Hash::HashKind::SHA3_384: return 48;
    case Hash::HashKind::SHA3_512: return 64;
    case Hash::HashKind::BLAKE2b: return 64;
    default: VERIFY_NOT_REACHED();
    }
}

HMAC::HMAC(Hash::HashKind hash, ReadonlyBytes key)
    : m_hash_kind(hash)
    , m_key(key)
    , m_inner_hash(hash)
    , m_block_size(hash_block_size(hash))
    , m_digest_size(hash_digest_size(hash))
{
    reset();
}

size_t HMAC::digest_size() const { return m_digest_size; }

void HMAC::update(u8 const* message, size_t length)
{
    m_inner_hash.update(message, length);
}

ByteBuffer HMAC::digest()
{
    auto inner_digest = m_inner_hash.digest();

    Hash::Manager outer(m_hash_kind);
    outer.update(m_opad_key, m_block_size);
    outer.update(inner_digest.immutable_data(), inner_digest.data_length());
    auto outer_digest = outer.digest();

    auto buf = MUST(ByteBuffer::create_uninitialized(m_digest_size));
    memcpy(buf.data(), outer_digest.immutable_data(), m_digest_size);
    return buf;
}

void HMAC::reset()
{
    // Derive key block
    u8 key_block[128] = {};
    if (m_key.size() > m_block_size) {
        Hash::Manager key_hash(m_hash_kind);
        key_hash.update(m_key.data(), m_key.size());
        auto hashed = key_hash.digest();
        memcpy(key_block, hashed.immutable_data(), hashed.data_length());
    } else {
        memcpy(key_block, m_key.data(), m_key.size());
    }

    for (size_t i = 0; i < m_block_size; i++) {
        m_ipad_key[i] = key_block[i] ^ 0x36;
        m_opad_key[i] = key_block[i] ^ 0x5c;
    }

    m_inner_hash.reset();
    m_inner_hash.update(m_ipad_key, m_block_size);
}

ByteString HMAC::class_name() const
{
    StringBuilder builder;
    builder.append("HMAC-"sv);
    switch (m_hash_kind) {
    case Hash::HashKind::MD5: builder.append("MD5"sv); break;
    case Hash::HashKind::SHA1: builder.append("SHA1"sv); break;
    case Hash::HashKind::SHA256: builder.append("SHA256"sv); break;
    case Hash::HashKind::SHA384: builder.append("SHA384"sv); break;
    case Hash::HashKind::SHA512: builder.append("SHA512"sv); break;
    default: builder.append("Unknown"sv); break;
    }
    return builder.to_byte_string();
}

}

#else // !AK_OS_RINOS

#include <LibCrypto/Authentication/HMAC.h>

#include <openssl/core_names.h>
#include <openssl/evp.h>

namespace Crypto::Authentication {

HMAC::HMAC(Hash::HashKind hash_kind, ReadonlyBytes key)
    : m_hash_kind(hash_kind)
    , m_key(key)
    , m_mac(EVP_MAC_fetch(nullptr, "HMAC", nullptr))
{
    reset();
}

HMAC::~HMAC()
{
    EVP_MAC_free(m_mac);
    EVP_MAC_CTX_free(m_ctx);
}

size_t HMAC::digest_size() const
{
    return EVP_MAC_CTX_get_mac_size(m_ctx);
}

void HMAC::update(u8 const* message, size_t length)
{
    if (EVP_MAC_update(m_ctx, message, length) != 1) {
        VERIFY_NOT_REACHED();
    }
}

ByteBuffer HMAC::digest()
{
    auto buf = MUST(ByteBuffer::create_uninitialized(digest_size()));

    auto size = digest_size();
    if (EVP_MAC_final(m_ctx, buf.data(), &size, size) != 1) {
        VERIFY_NOT_REACHED();
    }

    return MUST(buf.slice(0, size));
}

void HMAC::reset()
{
    EVP_MAC_CTX_free(m_ctx);
    m_ctx = EVP_MAC_CTX_new(m_mac);

    auto hash_name = MUST(hash_kind_to_openssl_digest_name(m_hash_kind));

    OSSL_PARAM params[] = {
        OSSL_PARAM_utf8_string(OSSL_MAC_PARAM_DIGEST, const_cast<char*>(hash_name.characters_without_null_termination()), hash_name.length()),
        OSSL_PARAM_END
    };

    if (EVP_MAC_init(m_ctx, m_key.data(), m_key.size(), params) != 1) {
        VERIFY_NOT_REACHED();
    }
}

}

#endif // AK_OS_RINOS
