/*
 * Copyright (c) 2026, mikiubo <michele.uboldi@gmail.com>
 *
 * SPDX-License-Identifier: BSD-2-Clause
 */

#ifdef AK_OS_RINOS

#include <LibCrypto/Cipher/ChaCha.h>
#include <LibCrypto/RinCryptoImpl.h>

namespace Crypto::Cipher {

ErrorOr<ByteBuffer> ChaCha20Poly1305::encrypt(ReadonlyBytes key, ReadonlyBytes nonce, ReadonlyBytes plaintext, ReadonlyBytes aad)
{
    if (key.size() != key_size)
        return Error::from_string_literal("ChaCha20-Poly1305 key must be 32 bytes");
    if (nonce.size() != nonce_size)
        return Error::from_string_literal("ChaCha20-Poly1305 nonce must be 12 bytes");

    auto ciphertext = TRY(ByteBuffer::create_uninitialized(plaintext.size()));
    u8 tag[16];

    if (rin_chacha20_poly1305_encrypt(key.data(), nonce.data(),
            aad.data(), aad.size(), plaintext.data(), plaintext.size(),
            ciphertext.data(), tag)
        != 0)
        return Error::from_string_literal("ChaCha20-Poly1305 encrypt failed");

    auto result = TRY(ByteBuffer::create_uninitialized(plaintext.size() + tag_size));
    result.overwrite(0, ciphertext.data(), ciphertext.size());
    result.overwrite(ciphertext.size(), tag, tag_size);
    return result;
}

ErrorOr<ByteBuffer> ChaCha20Poly1305::decrypt(ReadonlyBytes key, ReadonlyBytes nonce, ReadonlyBytes ciphertext_and_tag, ReadonlyBytes aad)
{
    if (key.size() != key_size)
        return Error::from_string_literal("ChaCha20-Poly1305 key must be 32 bytes");
    if (nonce.size() != nonce_size)
        return Error::from_string_literal("ChaCha20-Poly1305 nonce must be 12 bytes");
    if (ciphertext_and_tag.size() < tag_size)
        return Error::from_string_literal("Ciphertext too short");

    auto ct_size = ciphertext_and_tag.size() - tag_size;
    auto ct = ciphertext_and_tag.slice(0, ct_size);
    auto tag = ciphertext_and_tag.slice(ct_size, tag_size);

    auto plaintext = TRY(ByteBuffer::create_uninitialized(ct_size));
    if (rin_chacha20_poly1305_decrypt(key.data(), nonce.data(),
            aad.data(), aad.size(), ct.data(), ct_size,
            tag.data(), plaintext.data())
        != 0)
        return Error::from_string_literal("ChaCha20-Poly1305 authentication failed");

    return plaintext;
}

}

#else // !AK_OS_RINOS

#include <LibCrypto/Cipher/ChaCha.h>
#include <LibCrypto/OpenSSL.h>

#include <openssl/evp.h>

namespace Crypto::Cipher {

static EVP_CIPHER const* chacha20_poly1305_cipher()
{
    return EVP_chacha20_poly1305();
}

ErrorOr<ByteBuffer> ChaCha20Poly1305::encrypt(ReadonlyBytes key, ReadonlyBytes nonce, ReadonlyBytes plaintext, ReadonlyBytes aad)
{
    if (key.size() != key_size)
        return Error::from_string_literal("ChaCha20-Poly1305 key must be 32 bytes");

    if (nonce.size() != nonce_size)
        return Error::from_string_literal("ChaCha20-Poly1305 nonce must be 12 bytes");

    auto ctx = TRY(OpenSSL_CIPHER_CTX::create());

    OPENSSL_TRY(EVP_EncryptInit_ex(ctx.ptr(), chacha20_poly1305_cipher(), nullptr, nullptr, nullptr));

    OPENSSL_TRY(EVP_CIPHER_CTX_ctrl(ctx.ptr(), EVP_CTRL_AEAD_SET_IVLEN, nonce.size(), nullptr));

    OPENSSL_TRY(EVP_EncryptInit_ex(ctx.ptr(), nullptr, nullptr, key.data(), nonce.data()));

    if (!aad.is_empty()) {
        int aad_len = 0;
        OPENSSL_TRY(EVP_EncryptUpdate(ctx.ptr(), nullptr, &aad_len, aad.data(), aad.size()));
    }

    auto ciphertext = TRY(ByteBuffer::create_uninitialized(plaintext.size()));
    int out_len = 0;

    OPENSSL_TRY(EVP_EncryptUpdate(ctx.ptr(), ciphertext.data(), &out_len, plaintext.data(), plaintext.size()));

    int final_len = 0;
    OPENSSL_TRY(EVP_EncryptFinal_ex(ctx.ptr(), ciphertext.data() + out_len, &final_len));

    auto tag = TRY(ByteBuffer::create_uninitialized(tag_size));
    OPENSSL_TRY(EVP_CIPHER_CTX_ctrl(ctx.ptr(), EVP_CTRL_AEAD_GET_TAG, tag_size, tag.data()));

    auto result = TRY(ByteBuffer::create_uninitialized(ciphertext.size() + tag_size));
    result.overwrite(0, ciphertext.data(), ciphertext.size());
    result.overwrite(ciphertext.size(), tag.data(), tag.size());

    return result;
}

ErrorOr<ByteBuffer> ChaCha20Poly1305::decrypt(ReadonlyBytes key, ReadonlyBytes nonce, ReadonlyBytes ciphertext_and_tag, ReadonlyBytes aad)
{
    if (key.size() != key_size)
        return Error::from_string_literal("ChaCha20-Poly1305 key must be 32 bytes");

    if (nonce.size() != nonce_size)
        return Error::from_string_literal("ChaCha20-Poly1305 nonce must be 12 bytes");

    if (ciphertext_and_tag.size() < tag_size)
        return Error::from_string_literal("Ciphertext too short");

    auto ciphertext_size = ciphertext_and_tag.size() - tag_size;
    auto ciphertext = ciphertext_and_tag.slice(0, ciphertext_size);
    auto tag = ciphertext_and_tag.slice(ciphertext_size, tag_size);

    auto ctx = TRY(OpenSSL_CIPHER_CTX::create());

    OPENSSL_TRY(EVP_DecryptInit_ex(ctx.ptr(), chacha20_poly1305_cipher(), nullptr, nullptr, nullptr));

    OPENSSL_TRY(EVP_CIPHER_CTX_ctrl(ctx.ptr(), EVP_CTRL_AEAD_SET_IVLEN, nonce.size(), nullptr));

    OPENSSL_TRY(EVP_DecryptInit_ex(ctx.ptr(), nullptr, nullptr, key.data(), nonce.data()));

    OPENSSL_TRY(EVP_CIPHER_CTX_ctrl(ctx.ptr(), EVP_CTRL_AEAD_SET_TAG, tag.size(), const_cast<u8*>(tag.data())));

    if (!aad.is_empty()) {
        int aad_len = 0;
        OPENSSL_TRY(EVP_DecryptUpdate(ctx.ptr(), nullptr, &aad_len, aad.data(), aad.size()));
    }

    auto plaintext = TRY(ByteBuffer::create_uninitialized(ciphertext.size()));
    int out_len = 0;

    OPENSSL_TRY(EVP_DecryptUpdate(ctx.ptr(), plaintext.data(), &out_len, ciphertext.data(), ciphertext.size()));

    int final_len = 0;
    OPENSSL_TRY(EVP_DecryptFinal_ex(ctx.ptr(), plaintext.data() + out_len, &final_len));

    return plaintext.slice(0, out_len + final_len);
}

}

#endif // AK_OS_RINOS
