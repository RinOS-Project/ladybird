/*
 * Copyright (c) 2020, Ali Mohammad Pur <mpfard@serenityos.org>
 * Copyright (c) 2025, Altomani Gianluca <altomanigianluca@gmail.com>
 *
 * SPDX-License-Identifier: BSD-2-Clause
 */

#include <AK/ByteBuffer.h>
#include <AK/ScopeGuard.h>
#include <LibCrypto/ASN1/DER.h>
#include <LibCrypto/ASN1/PEM.h>
#include <LibCrypto/Certificate/Certificate.h>
#include <LibCrypto/PK/RSA.h>

#ifdef AK_OS_RINOS
#    include "../../../../rintls/crypto/rsa_webcrypto.h"
#endif

#ifdef AK_OS_RINOS

namespace Crypto::PK {

static void rintls_secure_zeroize(void* pointer, size_t size)
{
    auto* bytes = static_cast<volatile u8*>(pointer);
    while (size-- != 0)
        *bytes++ = 0;
}

static ErrorOr<void> rintls_export_integer(UnsignedBigInteger const& value, u8* output, size_t maximum_size, u16& output_size)
{
    if (!output || value.is_zero() || value.byte_length() > maximum_size)
        return Error::from_string_literal("RSA integer is outside the rintls provider bounds");
    auto bytes = TRY(ByteBuffer::create_uninitialized(value.byte_length()));
    ScopeGuard zeroize_bytes = [&] { rintls_secure_zeroize(bytes.data(), bytes.size()); };
    auto exported = value.export_data(bytes.span());
    if (exported.size() != bytes.size() || exported.is_empty() || exported[0] == 0)
        return Error::from_string_literal("RSA integer has a non-canonical encoding");
    memcpy(output, exported.data(), exported.size());
    output_size = static_cast<u16>(exported.size());
    return {};
}

static ErrorOr<rintls_rsa_public_key> rintls_make_public_key(RSAPublicKey const& key)
{
    rintls_rsa_public_key provider_key {};
    auto modulus_bits = key.modulus().one_based_index_of_highest_set_bit();
    if (modulus_bits < RINTLS_RSA_MIN_MODULUS_BITS ||
        modulus_bits > RINTLS_RSA_MAX_MODULUS_BITS ||
        modulus_bits % 8 != 0)
        return Error::from_string_literal("RSA modulus is outside the rintls provider policy");

    TRY(rintls_export_integer(key.modulus(), provider_key.modulus,
                              RINTLS_RSA_MAX_MODULUS_BYTES,
                              provider_key.modulus_len));
    if (provider_key.modulus_len != modulus_bits / 8 ||
        (provider_key.modulus[0] & 0x80) == 0)
        return Error::from_string_literal("RSA modulus does not have an exact byte-aligned bit length");

    u16 exponent_size = 0;
    TRY(rintls_export_integer(key.public_exponent(), provider_key.public_exponent,
                              RINTLS_RSA_MAX_PUBLIC_EXPONENT_BYTES,
                              exponent_size));
    provider_key.public_exponent_len = static_cast<u8>(exponent_size);
    provider_key.modulus_bits = static_cast<u16>(modulus_bits);
    return provider_key;
}

static ErrorOr<rintls_rsa_private_key> rintls_make_private_key(RSAPrivateKey const& key)
{
    rintls_rsa_private_key provider_key {};
    provider_key.public_key = TRY(rintls_make_public_key(
        RSAPublicKey { key.modulus(), key.public_exponent() }));
    TRY(rintls_export_integer(key.private_exponent(), provider_key.private_exponent,
                              RINTLS_RSA_MAX_MODULUS_BYTES,
                              provider_key.private_exponent_len));
    TRY(rintls_export_integer(key.prime1(), provider_key.prime1,
                              RINTLS_RSA_MAX_FACTOR_BYTES,
                              provider_key.prime1_len));
    TRY(rintls_export_integer(key.prime2(), provider_key.prime2,
                              RINTLS_RSA_MAX_FACTOR_BYTES,
                              provider_key.prime2_len));
    TRY(rintls_export_integer(key.exponent1(), provider_key.exponent1,
                              RINTLS_RSA_MAX_FACTOR_BYTES,
                              provider_key.exponent1_len));
    TRY(rintls_export_integer(key.exponent2(), provider_key.exponent2,
                              RINTLS_RSA_MAX_FACTOR_BYTES,
                              provider_key.exponent2_len));
    TRY(rintls_export_integer(key.coefficient(), provider_key.coefficient,
                              RINTLS_RSA_MAX_FACTOR_BYTES,
                              provider_key.coefficient_len));
    return provider_key;
}

static ErrorOr<u32> rintls_rsa_hash_kind(Hash::HashKind hash_kind)
{
    switch (hash_kind) {
    case Hash::HashKind::SHA1:
        return RINTLS_RSA_HASH_SHA1;
    case Hash::HashKind::SHA256:
        return RINTLS_RSA_HASH_SHA256;
    case Hash::HashKind::SHA384:
        return RINTLS_RSA_HASH_SHA384;
    case Hash::HashKind::SHA512:
        return RINTLS_RSA_HASH_SHA512;
    default:
        return Error::from_string_literal("Hash is not supported by the rintls RSA provider");
    }
}

static RSA::KeyPairType rintls_keypair_from_provider(rintls_rsa_private_key const& provider_key)
{
    auto modulus = UnsignedBigInteger::import_data({ provider_key.public_key.modulus, provider_key.public_key.modulus_len });
    auto public_exponent = UnsignedBigInteger::import_data({ provider_key.public_key.public_exponent, provider_key.public_key.public_exponent_len });
    auto private_exponent = UnsignedBigInteger::import_data({ provider_key.private_exponent, provider_key.private_exponent_len });
    auto prime1 = UnsignedBigInteger::import_data({ provider_key.prime1, provider_key.prime1_len });
    auto prime2 = UnsignedBigInteger::import_data({ provider_key.prime2, provider_key.prime2_len });
    auto exponent1 = UnsignedBigInteger::import_data({ provider_key.exponent1, provider_key.exponent1_len });
    auto exponent2 = UnsignedBigInteger::import_data({ provider_key.exponent2, provider_key.exponent2_len });
    auto coefficient = UnsignedBigInteger::import_data({ provider_key.coefficient, provider_key.coefficient_len });

    return {
        { modulus, public_exponent },
        { move(modulus), move(private_exponent), move(public_exponent), move(prime1), move(prime2), move(exponent1), move(exponent2), move(coefficient) },
    };
}

ErrorOr<RSA::KeyPairType> RSA::parse_rsa_key(ReadonlyBytes der, bool is_private, Vector<StringView> current_scope)
{
    KeyPairType keypair;
    ASN1::Decoder decoder(der);

    if (is_private) {
        ENTER_TYPED_SCOPE(Sequence, "RSAPrivateKey");
        PUSH_SCOPE("version");
        READ_OBJECT(Integer, Crypto::UnsignedBigInteger, version);
        POP_SCOPE();
        if (version != 0) {
            ERROR_WITH_SCOPE(TRY(String::formatted("Invalid version value at {}", current_scope)));
        }
        PUSH_SCOPE("modulus");
        READ_OBJECT(Integer, Crypto::UnsignedBigInteger, modulus);
        POP_SCOPE();
        PUSH_SCOPE("publicExponent");
        READ_OBJECT(Integer, Crypto::UnsignedBigInteger, public_exponent);
        POP_SCOPE();
        PUSH_SCOPE("privateExponent");
        READ_OBJECT(Integer, Crypto::UnsignedBigInteger, private_exponent);
        POP_SCOPE();
        PUSH_SCOPE("prime1");
        READ_OBJECT(Integer, Crypto::UnsignedBigInteger, prime1);
        POP_SCOPE();
        PUSH_SCOPE("prime2");
        READ_OBJECT(Integer, Crypto::UnsignedBigInteger, prime2);
        POP_SCOPE();
        PUSH_SCOPE("exponent1");
        READ_OBJECT(Integer, Crypto::UnsignedBigInteger, exponent1);
        POP_SCOPE();
        PUSH_SCOPE("exponent2");
        READ_OBJECT(Integer, Crypto::UnsignedBigInteger, exponent2);
        POP_SCOPE();
        PUSH_SCOPE("coefficient");
        READ_OBJECT(Integer, Crypto::UnsignedBigInteger, coefficient);
        POP_SCOPE();
        keypair.private_key = {
            modulus, private_exponent, public_exponent,
            prime1, prime2, exponent1, exponent2, coefficient,
        };
        keypair.public_key = { modulus, public_exponent };
        EXIT_SCOPE();
        return keypair;
    } else {
        ENTER_TYPED_SCOPE(Sequence, "RSAPublicKey");
        PUSH_SCOPE("modulus");
        READ_OBJECT(Integer, Crypto::UnsignedBigInteger, modulus);
        POP_SCOPE();
        PUSH_SCOPE("publicExponent");
        READ_OBJECT(Integer, Crypto::UnsignedBigInteger, public_exponent);
        POP_SCOPE();
        keypair.public_key = { move(modulus), move(public_exponent) };
        EXIT_SCOPE();
        return keypair;
    }
}

ErrorOr<RSA::KeyPairType> RSA::generate_key_pair(size_t bits, UnsignedBigInteger public_exponent)
{
    if (public_exponent.is_zero() || public_exponent.byte_length() > RINTLS_RSA_MAX_PUBLIC_EXPONENT_BYTES)
        return Error::from_string_literal("RSA public exponent is outside the rintls provider bounds");
    auto exponent = public_exponent.to_u64();
    if (exponent > NumericLimits<u32>::max())
        return Error::from_string_literal("RSA public exponent is outside the rintls provider bounds");

    rintls_rsa_private_key provider_key {};
    ScopeGuard clear_provider_key = [&] { rintls_secure_zeroize(&provider_key, sizeof(provider_key)); };
    if (rintls_rsa_generate_keypair(bits, static_cast<u32>(exponent), &provider_key) != 0)
        return Error::from_string_literal("rintls RSA key generation failed");
    return rintls_keypair_from_provider(provider_key);
}

ErrorOr<bool> RSAPublicKey::is_valid() const
{
    if (!m_public_exponent.is_odd())
        return false;
    if (m_public_exponent < 3 || m_public_exponent >= m_modulus)
        return false;
    return true;
}

ErrorOr<bool> RSAPrivateKey::is_valid() const
{
    if (!m_public_exponent.is_odd())
        return false;
    if (m_public_exponent < 3 || m_public_exponent >= m_modulus)
        return false;

    if (!m_prime_1.is_zero() && !m_prime_2.is_zero()) {
        if (m_exponent_1 >= m_prime_1 || m_exponent_2 >= m_prime_2 || m_coefficient >= m_prime_1)
            return false;
        if (m_prime_1.multiplied_by(m_prime_2) != m_modulus)
            return false;
        return true;
    }

    if (!m_modulus.is_zero() && !m_private_exponent.is_zero()) {
        if (m_private_exponent >= m_modulus)
            return false;
        return true;
    }

    return false;
}

ErrorOr<ByteBuffer> RSA::encrypt(ReadonlyBytes in)
{
    (void)in;
    return Error::from_string_literal("Raw RSA is unavailable; use a padded rintls RSA algorithm");
}

ErrorOr<ByteBuffer> RSA::decrypt(ReadonlyBytes in)
{
    (void)in;
    return Error::from_string_literal("Raw RSA is unavailable; use a padded rintls RSA algorithm");
}

ErrorOr<ByteBuffer> RSA::sign(ReadonlyBytes message)
{
    (void)message;
    return Error::from_string_literal("Raw RSA is unavailable; use a padded rintls RSA algorithm");
}

ErrorOr<bool> RSA::verify(ReadonlyBytes message, ReadonlyBytes signature)
{
    (void)message;
    (void)signature;
    return Error::from_string_literal("Raw RSA is unavailable; use a padded rintls RSA algorithm");
}

void RSA::import_private_key(ReadonlyBytes bytes, bool pem)
{
    ByteBuffer decoded_bytes;
    ReadonlyBytes der = bytes;
    if (pem) {
        auto decoded = decode_pem(bytes);
        if (decoded.type == PEMType::RSAPrivateKey) {
            decoded_bytes = decoded.data;
            der = decoded_bytes.bytes();
        } else if (decoded.type == PEMType::PrivateKey) {
            ASN1::Decoder decoder(decoded.data);
            auto maybe_key = Certificate::parse_private_key_info(decoder, {});
            if (maybe_key.is_error()) {
                dbgln("Failed to parse private key info: {}", maybe_key.error());
                VERIFY_NOT_REACHED();
            }
            m_private_key = maybe_key.release_value().rsa;
            return;
        } else {
            dbgln("Expected a PEM encoded private key");
            VERIFY_NOT_REACHED();
        }
    }
    auto maybe_key = parse_rsa_key(der, true, {});
    if (maybe_key.is_error()) {
        dbgln("Failed to parse RSA private key: {}", maybe_key.error());
        VERIFY_NOT_REACHED();
    }
    m_private_key = maybe_key.release_value().private_key;
}

void RSA::import_public_key(ReadonlyBytes bytes, bool pem)
{
    ByteBuffer decoded_bytes;
    ReadonlyBytes der = bytes;
    if (pem) {
        auto decoded = decode_pem(bytes);
        if (decoded.type == PEMType::RSAPublicKey) {
            decoded_bytes = decoded.data;
            der = decoded_bytes.bytes();
        } else if (decoded.type == PEMType::PublicKey) {
            ASN1::Decoder decoder(decoded.data);
            auto maybe_key = Certificate::parse_subject_public_key_info(decoder, {});
            if (maybe_key.is_error()) {
                dbgln("Failed to parse subject public key info: {}", maybe_key.error());
                VERIFY_NOT_REACHED();
            }
            m_public_key = maybe_key.release_value().rsa;
            return;
        } else {
            dbgln("Expected a PEM encoded public key");
            VERIFY_NOT_REACHED();
        }
    }
    auto maybe_key = parse_rsa_key(der, false, {});
    if (maybe_key.is_error()) {
        dbgln("Failed to parse RSA public key: {}", maybe_key.error());
        VERIFY_NOT_REACHED();
    }
    m_public_key = maybe_key.release_value().public_key;
}

ErrorOr<bool> RSA_EMSA::verify(ReadonlyBytes message, ReadonlyBytes signature)
{
    (void)message;
    (void)signature;
    return Error::from_string_literal("Generic RSA EMSA is unavailable; select PKCS#1 v1.5 or a reviewed PSS provider");
}

ErrorOr<ByteBuffer> RSA_EMSA::sign(ReadonlyBytes message)
{
    (void)message;
    return Error::from_string_literal("Generic RSA EMSA is unavailable; select PKCS#1 v1.5 or a reviewed PSS provider");
}

ErrorOr<ByteBuffer> RSA_PKCS1_EMSA::sign(ReadonlyBytes message)
{
    auto provider_key = TRY(rintls_make_private_key(m_private_key));
    ScopeGuard clear_provider_key = [&] { rintls_secure_zeroize(&provider_key, sizeof(provider_key)); };
    auto signature = TRY(ByteBuffer::create_zeroed(RINTLS_RSA_MAX_MODULUS_BYTES));
    rin_size_t signature_length = 0;
    if (rintls_rsa_pkcs1_sign(TRY(rintls_rsa_hash_kind(m_hash_kind)), &provider_key,
                              message.data(), message.size(), signature.data(),
                              signature.size(), &signature_length) != 0)
        return Error::from_string_literal("rintls PKCS#1 v1.5 signing failed");
    return signature.slice(0, signature_length);
}

ErrorOr<bool> RSA_PKCS1_EMSA::verify(ReadonlyBytes message, ReadonlyBytes signature)
{
    auto provider_key = TRY(rintls_make_public_key(m_public_key));
    ScopeGuard clear_provider_key = [&] { rintls_secure_zeroize(&provider_key, sizeof(provider_key)); };
    auto result = rintls_rsa_pkcs1_verify(TRY(rintls_rsa_hash_kind(m_hash_kind)),
                                          &provider_key, message.data(), message.size(),
                                          signature.data(), signature.size());
    if (result < 0)
        return Error::from_string_literal("rintls PKCS#1 v1.5 verification failed");
    return result == RINTLS_RSA_VERIFY_VALID;
}

ErrorOr<ByteBuffer> RSA_OAEP_EME::encrypt(ReadonlyBytes plaintext)
{
    auto provider_key = TRY(rintls_make_public_key(m_public_key));
    ScopeGuard clear_provider_key = [&] { rintls_secure_zeroize(&provider_key, sizeof(provider_key)); };
    auto ciphertext = TRY(ByteBuffer::create_zeroed(RINTLS_RSA_MAX_MODULUS_BYTES));
    rin_size_t ciphertext_length = 0;
    auto label = m_label.value_or(ReadonlyBytes {});
    if (rintls_rsa_oaep_encrypt(TRY(rintls_rsa_hash_kind(m_hash_kind)), &provider_key,
                                label.data(), label.size(), plaintext.data(), plaintext.size(),
                                ciphertext.data(), ciphertext.size(),
                                &ciphertext_length) != 0)
        return Error::from_string_literal("rintls RSA-OAEP encryption failed");
    return ciphertext.slice(0, ciphertext_length);
}

ErrorOr<ByteBuffer> RSA_OAEP_EME::decrypt(ReadonlyBytes ciphertext)
{
    auto provider_key = TRY(rintls_make_private_key(m_private_key));
    ScopeGuard clear_provider_key = [&] { rintls_secure_zeroize(&provider_key, sizeof(provider_key)); };
    auto plaintext = TRY(ByteBuffer::create_zeroed(RINTLS_RSA_MAX_MODULUS_BYTES));
    rin_size_t plaintext_length = 0;
    auto label = m_label.value_or(ReadonlyBytes {});
    if (rintls_rsa_oaep_decrypt(TRY(rintls_rsa_hash_kind(m_hash_kind)), &provider_key,
                                ciphertext.data(), ciphertext.size(), label.data(), label.size(),
                                plaintext.data(), plaintext.size(),
                                &plaintext_length) != 0)
        return Error::from_string_literal("rintls RSA-OAEP decryption failed");
    return plaintext.slice(0, plaintext_length);
}

ErrorOr<ByteBuffer> RSA_PSS_EMSA::sign(ReadonlyBytes message)
{
    auto provider_key = TRY(rintls_make_private_key(m_private_key));
    ScopeGuard clear_provider_key = [&] { rintls_secure_zeroize(&provider_key, sizeof(provider_key)); };
    u32 salt_length = RINTLS_RSA_PSS_SALT_LENGTH_MAX;
    if (m_salt_length.has_value()) {
        if (*m_salt_length < 0)
            return Error::from_string_literal("RSA-PSS salt length must not be negative");
        salt_length = static_cast<u32>(*m_salt_length);
    }
    auto signature = TRY(ByteBuffer::create_zeroed(RINTLS_RSA_MAX_MODULUS_BYTES));
    rin_size_t signature_length = 0;
    if (rintls_rsa_pss_sign(TRY(rintls_rsa_hash_kind(m_hash_kind)), &provider_key,
                            message.data(), message.size(), salt_length,
                            signature.data(), signature.size(),
                            &signature_length) != 0)
        return Error::from_string_literal("rintls RSA-PSS signing failed");
    return signature.slice(0, signature_length);
}

ErrorOr<bool> RSA_PSS_EMSA::verify(ReadonlyBytes message, ReadonlyBytes signature)
{
    auto provider_key = TRY(rintls_make_public_key(m_public_key));
    ScopeGuard clear_provider_key = [&] { rintls_secure_zeroize(&provider_key, sizeof(provider_key)); };
    u32 salt_length = RINTLS_RSA_PSS_SALT_LENGTH_MAX;
    if (m_salt_length.has_value()) {
        if (*m_salt_length < 0)
            return Error::from_string_literal("RSA-PSS salt length must not be negative");
        salt_length = static_cast<u32>(*m_salt_length);
    }
    auto result = rintls_rsa_pss_verify(TRY(rintls_rsa_hash_kind(m_hash_kind)),
                                        &provider_key, message.data(), message.size(),
                                        salt_length, signature.data(), signature.size());
    if (result < 0)
        return Error::from_string_literal("rintls RSA-PSS verification failed");
    return result == RINTLS_RSA_VERIFY_VALID;
}

}

#else // !AK_OS_RINOS

#include <LibCrypto/OpenSSL.h>

#include <openssl/core_names.h>
#include <openssl/evp.h>
#include <openssl/param_build.h>
#include <openssl/rsa.h>

namespace Crypto::PK {

ErrorOr<RSA::KeyPairType> RSA::parse_rsa_key(ReadonlyBytes der, bool is_private, Vector<StringView> current_scope)
{
    KeyPairType keypair;

    ASN1::Decoder decoder(der);

    if (is_private) {
        // RSAPrivateKey ::= SEQUENCE {
        //      version             Version,
        //      modulus             INTEGER,
        //      publicExponent      INTEGER,
        //      privateExponent     INTEGER,
        //      prime1              INTEGER,
        //      prime2              INTEGER,
        //      exponent1           INTEGER,
        //      exponent2           INTEGER,
        //      coefficient         INTEGER,
        //      otherPrimeInfos     OtherPrimeInfos OPTIONAL
        // }

        ENTER_TYPED_SCOPE(Sequence, "RSAPrivateKey");

        PUSH_SCOPE("version");
        READ_OBJECT(Integer, Crypto::UnsignedBigInteger, version);
        POP_SCOPE();
        if (version != 0) {
            ERROR_WITH_SCOPE(TRY(String::formatted("Invalid version value at {}", current_scope)));
        }

        PUSH_SCOPE("modulus");
        READ_OBJECT(Integer, Crypto::UnsignedBigInteger, modulus);
        POP_SCOPE();

        PUSH_SCOPE("publicExponent");
        READ_OBJECT(Integer, Crypto::UnsignedBigInteger, public_exponent);
        POP_SCOPE();

        PUSH_SCOPE("privateExponent");
        READ_OBJECT(Integer, Crypto::UnsignedBigInteger, private_exponent);
        POP_SCOPE();

        PUSH_SCOPE("prime1");
        READ_OBJECT(Integer, Crypto::UnsignedBigInteger, prime1);
        POP_SCOPE();

        PUSH_SCOPE("prime2");
        READ_OBJECT(Integer, Crypto::UnsignedBigInteger, prime2);
        POP_SCOPE();

        PUSH_SCOPE("exponent1");
        READ_OBJECT(Integer, Crypto::UnsignedBigInteger, exponent1);
        POP_SCOPE();

        PUSH_SCOPE("exponent2");
        READ_OBJECT(Integer, Crypto::UnsignedBigInteger, exponent2);
        POP_SCOPE();

        PUSH_SCOPE("coefficient");
        READ_OBJECT(Integer, Crypto::UnsignedBigInteger, coefficient);
        POP_SCOPE();

        keypair.private_key = {
            modulus,
            private_exponent,
            public_exponent,
            prime1,
            prime2,
            exponent1,
            exponent2,
            coefficient,
        };
        keypair.public_key = { modulus, public_exponent };

        EXIT_SCOPE();
        return keypair;
    } else {
        // RSAPublicKey ::= SEQUENCE {
        //      modulus         INTEGER,
        //      publicExponent  INTEGER
        // }

        ENTER_TYPED_SCOPE(Sequence, "RSAPublicKey");

        PUSH_SCOPE("modulus");
        READ_OBJECT(Integer, Crypto::UnsignedBigInteger, modulus);
        POP_SCOPE();

        PUSH_SCOPE("publicExponent");
        READ_OBJECT(Integer, Crypto::UnsignedBigInteger, public_exponent);
        POP_SCOPE();

        keypair.public_key = { move(modulus), move(public_exponent) };

        EXIT_SCOPE();
        return keypair;
    }
}

ErrorOr<RSA::KeyPairType> RSA::generate_key_pair(size_t bits, UnsignedBigInteger e)
{
    auto ctx = TRY(OpenSSL_PKEY_CTX::wrap(EVP_PKEY_CTX_new_from_name(nullptr, "RSA", nullptr)));

    OPENSSL_TRY(EVP_PKEY_keygen_init(ctx.ptr()));

    auto e_bn = TRY(unsigned_big_integer_to_openssl_bignum(e));

    auto* params_bld = OPENSSL_TRY_PTR(OSSL_PARAM_BLD_new());
    ScopeGuard const free_params_bld = [&] { OSSL_PARAM_BLD_free(params_bld); };

    OPENSSL_TRY(OSSL_PARAM_BLD_push_size_t(params_bld, OSSL_PKEY_PARAM_RSA_BITS, bits));
    OPENSSL_TRY(OSSL_PARAM_BLD_push_BN(params_bld, OSSL_PKEY_PARAM_RSA_E, e_bn.ptr()));

    auto* params = OSSL_PARAM_BLD_to_param(params_bld);
    ScopeGuard const free_params = [&] { OSSL_PARAM_free(params); };

    OPENSSL_TRY(EVP_PKEY_CTX_set_params(ctx.ptr(), params));

    auto key = TRY(OpenSSL_PKEY::create());
    auto* key_ptr = key.ptr();
    OPENSSL_TRY(EVP_PKEY_generate(ctx.ptr(), &key_ptr));

#define OPENSSL_GET_KEY_PARAM(param, openssl_name)                                \
    auto param##_bn = TRY(OpenSSL_BN::create());                                  \
    auto* param##_bn_ptr = param##_bn.ptr();                                      \
    OPENSSL_TRY(EVP_PKEY_get_bn_param(key.ptr(), openssl_name, &param##_bn_ptr)); \
    auto param = TRY(openssl_bignum_to_unsigned_big_integer(param##_bn));

    OPENSSL_GET_KEY_PARAM(n, OSSL_PKEY_PARAM_RSA_N);
    OPENSSL_GET_KEY_PARAM(d, OSSL_PKEY_PARAM_RSA_D);
    OPENSSL_GET_KEY_PARAM(p, OSSL_PKEY_PARAM_RSA_FACTOR1);
    OPENSSL_GET_KEY_PARAM(q, OSSL_PKEY_PARAM_RSA_FACTOR2);
    OPENSSL_GET_KEY_PARAM(dp, OSSL_PKEY_PARAM_RSA_EXPONENT1);
    OPENSSL_GET_KEY_PARAM(dq, OSSL_PKEY_PARAM_RSA_EXPONENT2);
    OPENSSL_GET_KEY_PARAM(qinv, OSSL_PKEY_PARAM_RSA_COEFFICIENT1);

#undef OPENSSL_GET_KEY_PARAM

    RSAKeyPair<PublicKeyType, PrivateKeyType> keys {
        { n, e },
        { n, d, e, p, q, dp, dq, qinv }
    };
    return keys;
}

#define OPENSSL_SET_KEY_PARAM_NOT_ZERO(param, openssl_name, value)                       \
    auto param##_bn = TRY(unsigned_big_integer_to_openssl_bignum(value));                \
    if (!value.is_zero()) {                                                              \
        OPENSSL_TRY(OSSL_PARAM_BLD_push_BN(params_bld, openssl_name, param##_bn.ptr())); \
    }

static ErrorOr<OpenSSL_PKEY> public_key_to_openssl_pkey(RSAPublicKey const& public_key)
{
    auto ctx = TRY(OpenSSL_PKEY_CTX::wrap(EVP_PKEY_CTX_new_from_name(nullptr, "RSA", nullptr)));

    OPENSSL_TRY(EVP_PKEY_fromdata_init(ctx.ptr()));

    auto* params_bld = OPENSSL_TRY_PTR(OSSL_PARAM_BLD_new());
    ScopeGuard const free_params_bld = [&] { OSSL_PARAM_BLD_free(params_bld); };

    OPENSSL_SET_KEY_PARAM_NOT_ZERO(n, OSSL_PKEY_PARAM_RSA_N, public_key.modulus());
    OPENSSL_SET_KEY_PARAM_NOT_ZERO(e, OSSL_PKEY_PARAM_RSA_E, public_key.public_exponent());

    auto* params = OSSL_PARAM_BLD_to_param(params_bld);
    ScopeGuard const free_params = [&] { OSSL_PARAM_free(params); };

    auto key = TRY(OpenSSL_PKEY::create());
    auto* key_ptr = key.ptr();
    OPENSSL_TRY(EVP_PKEY_fromdata(ctx.ptr(), &key_ptr, EVP_PKEY_PUBLIC_KEY, params));
    return key;
}

static ErrorOr<OpenSSL_PKEY> private_key_to_openssl_pkey(RSAPrivateKey const& private_key)
{
    auto ctx = TRY(OpenSSL_PKEY_CTX::wrap(EVP_PKEY_CTX_new_from_name(nullptr, "RSA", nullptr)));

    OPENSSL_TRY(EVP_PKEY_fromdata_init(ctx.ptr()));

    auto* params_bld = OPENSSL_TRY_PTR(OSSL_PARAM_BLD_new());
    ScopeGuard const free_params_bld = [&] { OSSL_PARAM_BLD_free(params_bld); };

    OPENSSL_SET_KEY_PARAM_NOT_ZERO(n, OSSL_PKEY_PARAM_RSA_N, private_key.modulus());
    OPENSSL_SET_KEY_PARAM_NOT_ZERO(e, OSSL_PKEY_PARAM_RSA_E, private_key.public_exponent());
    OPENSSL_SET_KEY_PARAM_NOT_ZERO(d, OSSL_PKEY_PARAM_RSA_D, private_key.private_exponent());
    OPENSSL_SET_KEY_PARAM_NOT_ZERO(p, OSSL_PKEY_PARAM_RSA_FACTOR1, private_key.prime1());
    OPENSSL_SET_KEY_PARAM_NOT_ZERO(q, OSSL_PKEY_PARAM_RSA_FACTOR2, private_key.prime2());
    OPENSSL_SET_KEY_PARAM_NOT_ZERO(dp, OSSL_PKEY_PARAM_RSA_EXPONENT1, private_key.exponent1());
    OPENSSL_SET_KEY_PARAM_NOT_ZERO(dq, OSSL_PKEY_PARAM_RSA_EXPONENT2, private_key.exponent2());
    OPENSSL_SET_KEY_PARAM_NOT_ZERO(qinv, OSSL_PKEY_PARAM_RSA_COEFFICIENT1, private_key.coefficient());

    auto* params = OSSL_PARAM_BLD_to_param(params_bld);
    ScopeGuard const free_params = [&] { OSSL_PARAM_free(params); };

    auto key = TRY(OpenSSL_PKEY::create());
    auto* key_ptr = key.ptr();
    OPENSSL_TRY(EVP_PKEY_fromdata(ctx.ptr(), &key_ptr, EVP_PKEY_KEYPAIR, params));
    return key;
}

#undef OPENSSL_SET_KEY_PARAM_NOT_ZERO

// https://www.rfc-editor.org/rfc/rfc3447.html#section-3.1
ErrorOr<bool> RSAPublicKey::is_valid() const
{
    // In a valid RSA public key, the RSA modulus n is a product of u
    // distinct odd primes r_i, i = 1, 2, ..., u, where u >= 2, and the RSA
    // public exponent e is an integer between 3 and n - 1 satisfying GCD(e,
    // \lambda(n)) = 1, where \lambda(n) = LCM(r_1 - 1, ..., r_u - 1).

    if (!m_public_exponent.is_odd())
        return false;

    if (m_public_exponent < 3 || m_public_exponent >= m_modulus)
        return false;

    return true;
}

// https://www.rfc-editor.org/rfc/rfc3447.html#section-3.2
ErrorOr<bool> RSAPrivateKey::is_valid() const
{
    if (!m_public_exponent.is_odd())
        return false;

    if (m_public_exponent < 3 || m_public_exponent >= m_modulus)
        return false;

    if (!m_prime_1.is_zero() && !m_prime_2.is_zero() && !m_exponent_1.is_zero() && !m_exponent_2.is_zero() && !m_coefficient.is_zero()) {
        // In a valid RSA private key with the second representation, the two
        // factors p and q are the first two prime factors of the RSA modulus n
        // (i.e., r_1 and r_2), the CRT exponents dP and dQ are positive
        // integers less than p and q respectively satisfying
        //   e * dP == 1 (mod (p-1))
        //   e * dQ == 1 (mod (q-1)) ,
        // and the CRT coefficient qInv is a positive integer less than p
        // satisfying
        //   q * qInv == 1 (mod p).
        // If u > 2, the representation will include one or more triplets (r_i,
        // d_i, t_i), i = 3, ..., u.  The factors r_i are the additional prime
        // factors of the RSA modulus n.  Each CRT exponent d_i (i = 3, ..., u)
        // satisfies
        //   e * d_i == 1 (mod (r_i - 1)).
        // Each CRT coefficient t_i (i = 3, ..., u) is a positive integer less
        // than r_i satisfying
        //   R_i * t_i == 1 (mod r_i) ,
        // where R_i = r_1 * r_2 * ... * r_(i-1).

        if (m_exponent_1 >= m_prime_1 || m_exponent_2 >= m_prime_2 || m_coefficient >= m_prime_1)
            return false;

        if (m_prime_1.multiplied_by(m_prime_2) != m_modulus)
            return false;

        auto tmp_bn = TRY(OpenSSL_BN::create());

        auto e = TRY(unsigned_big_integer_to_openssl_bignum(m_public_exponent)),
             p = TRY(unsigned_big_integer_to_openssl_bignum(m_prime_1)),
             q = TRY(unsigned_big_integer_to_openssl_bignum(m_prime_2));

        auto dp = TRY(unsigned_big_integer_to_openssl_bignum(m_exponent_1)),
             dq = TRY(unsigned_big_integer_to_openssl_bignum(m_exponent_2));

        auto* bn_ctx = OPENSSL_TRY_PTR(BN_CTX_new());
        ScopeGuard const free_bn_ctx = [&] { BN_CTX_free(bn_ctx); };

        auto p1 = TRY(OpenSSL_BN::create());
        OPENSSL_TRY(BN_sub(p1.ptr(), p.ptr(), BN_value_one()));

        OPENSSL_TRY(BN_mod_mul(tmp_bn.ptr(), e.ptr(), dp.ptr(), p1.ptr(), bn_ctx));
        if (!BN_is_one(tmp_bn.ptr()))
            return false;

        auto q1 = TRY(OpenSSL_BN::create());
        OPENSSL_TRY(BN_sub(q1.ptr(), q.ptr(), BN_value_one()));

        OPENSSL_TRY(BN_mod_mul(tmp_bn.ptr(), e.ptr(), dq.ptr(), q1.ptr(), bn_ctx));
        if (!BN_is_one(tmp_bn.ptr()))
            return false;

        auto q_inv = TRY(unsigned_big_integer_to_openssl_bignum(m_coefficient));
        OPENSSL_TRY(BN_mod_mul(tmp_bn.ptr(), q.ptr(), q_inv.ptr(), p.ptr(), bn_ctx));
        if (!BN_is_one(tmp_bn.ptr()))
            return false;

        if (!m_private_exponent.is_zero()) {
            if (m_private_exponent >= m_modulus)
                return false;

            auto lambda = TRY(m_prime_1.minus(1)).lcm(TRY(m_prime_2.minus(1)));
            auto lambda_bn = TRY(unsigned_big_integer_to_openssl_bignum(lambda));

            auto d = TRY(unsigned_big_integer_to_openssl_bignum(m_private_exponent));

            OPENSSL_TRY(BN_mod_mul(tmp_bn.ptr(), d.ptr(), e.ptr(), lambda_bn.ptr(), bn_ctx));
            if (!BN_is_one(tmp_bn.ptr()))
                return false;
        }

        return true;
    }

    if (!m_modulus.is_zero() && !m_private_exponent.is_zero()) {
        // In a valid RSA private key with the first representation, the RSA
        // modulus n is the same as in the corresponding RSA public key and is
        // the product of u distinct odd primes r_i, i = 1, 2, ..., u, where u
        // >= 2.  The RSA private exponent d is a positive integer less than n
        // satisfying
        //   e * d == 1 (mod \lambda(n)),
        // where e is the corresponding RSA public exponent and \lambda(n) is
        // defined as in Section 3.1.

        if (m_private_exponent >= m_modulus)
            return false;

        return true;
    }

    return false;
}

ErrorOr<void> RSA::configure(OpenSSL_PKEY_CTX& ctx)
{
    OPENSSL_TRY(EVP_PKEY_CTX_set_rsa_padding(ctx.ptr(), RSA_NO_PADDING));
    return {};
}

ErrorOr<ByteBuffer> RSA::encrypt(ReadonlyBytes in)
{
    auto key = TRY(public_key_to_openssl_pkey(m_public_key));

    auto ctx = TRY(OpenSSL_PKEY_CTX::wrap(EVP_PKEY_CTX_new_from_pkey(nullptr, key.ptr(), nullptr)));

    OPENSSL_TRY(EVP_PKEY_encrypt_init(ctx.ptr()));
    TRY(configure(ctx));

    size_t out_size = 0;
    OPENSSL_TRY(EVP_PKEY_encrypt(ctx.ptr(), nullptr, &out_size, in.data(), in.size()));

    auto out = TRY(ByteBuffer::create_uninitialized(out_size));
    OPENSSL_TRY(EVP_PKEY_encrypt(ctx.ptr(), out.data(), &out_size, in.data(), in.size()));
    return out.slice(0, out_size);
}

ErrorOr<ByteBuffer> RSA::decrypt(ReadonlyBytes in)
{
    auto key = TRY(private_key_to_openssl_pkey(m_private_key));

    auto ctx = TRY(OpenSSL_PKEY_CTX::wrap(EVP_PKEY_CTX_new_from_pkey(nullptr, key.ptr(), nullptr)));

    OPENSSL_TRY(EVP_PKEY_decrypt_init(ctx.ptr()));
    TRY(configure(ctx));

    size_t out_size = 0;
    OPENSSL_TRY(EVP_PKEY_decrypt(ctx.ptr(), nullptr, &out_size, in.data(), in.size()));

    auto out = TRY(ByteBuffer::create_uninitialized(out_size));
    OPENSSL_TRY(EVP_PKEY_decrypt(ctx.ptr(), out.data(), &out_size, in.data(), in.size()));
    return out.slice(0, out_size);
}

ErrorOr<ByteBuffer> RSA::sign(ReadonlyBytes message)
{
    auto key = TRY(private_key_to_openssl_pkey(m_private_key));

    auto ctx = TRY(OpenSSL_PKEY_CTX::wrap(EVP_PKEY_CTX_new_from_pkey(nullptr, key.ptr(), nullptr)));

    OPENSSL_TRY(EVP_PKEY_sign_init(ctx.ptr()));
    TRY(configure(ctx));

    size_t signature_size = 0;
    OPENSSL_TRY(EVP_PKEY_sign(ctx.ptr(), nullptr, &signature_size, message.data(), message.size()));

    auto signature = TRY(ByteBuffer::create_uninitialized(signature_size));
    OPENSSL_TRY(EVP_PKEY_sign(ctx.ptr(), signature.data(), &signature_size, message.data(), message.size()));
    return signature.slice(0, signature_size);
}

ErrorOr<bool> RSA::verify(ReadonlyBytes message, ReadonlyBytes signature)
{
    auto key = TRY(public_key_to_openssl_pkey(m_public_key));

    auto ctx = TRY(OpenSSL_PKEY_CTX::wrap(EVP_PKEY_CTX_new_from_pkey(nullptr, key.ptr(), nullptr)));

    OPENSSL_TRY(EVP_PKEY_verify_init(ctx.ptr()));
    TRY(configure(ctx));

    auto ret = EVP_PKEY_verify(ctx.ptr(), signature.data(), signature.size(), message.data(), message.size());
    if (ret == 1)
        return true;
    if (ret == 0)
        return false;
    OPENSSL_TRY(ret);
    VERIFY_NOT_REACHED();
}

void RSA::import_private_key(ReadonlyBytes bytes, bool pem)
{
    ByteBuffer decoded_bytes;
    ReadonlyBytes der = bytes;
    if (pem) {
        auto decoded = decode_pem(bytes);
        if (decoded.type == PEMType::RSAPrivateKey) {
            decoded_bytes = decoded.data;
            der = decoded_bytes.bytes();
        } else if (decoded.type == PEMType::PrivateKey) {
            ASN1::Decoder decoder(decoded.data);
            auto maybe_key = Certificate::parse_private_key_info(decoder, {});
            if (maybe_key.is_error()) {
                dbgln("Failed to parse private key info: {}", maybe_key.error());
                VERIFY_NOT_REACHED();
            }

            m_private_key = maybe_key.release_value().rsa;
            return;
        } else {
            dbgln("Expected a PEM encoded private key");
            VERIFY_NOT_REACHED();
        }
    }

    auto maybe_key = parse_rsa_key(der, true, {});
    if (maybe_key.is_error()) {
        dbgln("Failed to parse RSA private key: {}", maybe_key.error());
        VERIFY_NOT_REACHED();
    }
    m_private_key = maybe_key.release_value().private_key;
}

void RSA::import_public_key(ReadonlyBytes bytes, bool pem)
{
    ByteBuffer decoded_bytes;
    ReadonlyBytes der = bytes;
    if (pem) {
        auto decoded = decode_pem(bytes);
        if (decoded.type == PEMType::RSAPublicKey) {
            decoded_bytes = decoded.data;
            der = decoded_bytes.bytes();
        } else if (decoded.type == PEMType::PublicKey) {
            ASN1::Decoder decoder(decoded.data);
            auto maybe_key = Certificate::parse_subject_public_key_info(decoder, {});
            if (maybe_key.is_error()) {
                dbgln("Failed to parse subject public key info: {}", maybe_key.error());
                VERIFY_NOT_REACHED();
            }

            m_public_key = maybe_key.release_value().rsa;
            return;
        } else {
            dbgln("Expected a PEM encoded public key");
            VERIFY_NOT_REACHED();
        }
    }

    auto maybe_key = parse_rsa_key(der, false, {});
    if (maybe_key.is_error()) {
        dbgln("Failed to parse RSA public key: {}", maybe_key.error());
        VERIFY_NOT_REACHED();
    }
    m_public_key = maybe_key.release_value().public_key;
}

ErrorOr<EVP_MD const*> hash_kind_to_hash_type(Hash::HashKind hash_kind)
{
    switch (hash_kind) {
    case Hash::HashKind::None:
        return nullptr;
    case Hash::HashKind::BLAKE2b:
        return EVP_blake2b512();
    case Hash::HashKind::MD5:
        return EVP_md5();
    case Hash::HashKind::SHA1:
        return EVP_sha1();
    case Hash::HashKind::SHA256:
        return EVP_sha256();
    case Hash::HashKind::SHA384:
        return EVP_sha384();
    case Hash::HashKind::SHA512:
        return EVP_sha512();
    default:
        return Error::from_string_literal("Unsupported hash kind");
    }
}

ErrorOr<bool> RSA_EMSA::verify(ReadonlyBytes message, ReadonlyBytes signature)
{
    auto key = TRY(public_key_to_openssl_pkey(m_public_key));
    auto const* hash_type = TRY(hash_kind_to_hash_type(m_hash_kind));

    auto ctx = TRY(OpenSSL_MD_CTX::create());

    auto key_ctx = TRY(OpenSSL_PKEY_CTX::wrap(EVP_PKEY_CTX_new(key.ptr(), nullptr)));
    EVP_MD_CTX_set_pkey_ctx(ctx.ptr(), key_ctx.ptr());

    OPENSSL_TRY(EVP_DigestVerifyInit(ctx.ptr(), nullptr, hash_type, nullptr, key.ptr()));
    TRY(configure(key_ctx));

    auto res = EVP_DigestVerify(ctx.ptr(), signature.data(), signature.size(), message.data(), message.size());
    if (res == 1)
        return true;
    if (res == 0)
        return false;
    OPENSSL_TRY(res);
    VERIFY_NOT_REACHED();
}

ErrorOr<ByteBuffer> RSA_EMSA::sign(ReadonlyBytes message)
{
    auto key = TRY(private_key_to_openssl_pkey(m_private_key));
    auto const* hash_type = TRY(hash_kind_to_hash_type(m_hash_kind));

    auto ctx = TRY(OpenSSL_MD_CTX::create());

    auto key_ctx = TRY(OpenSSL_PKEY_CTX::wrap(EVP_PKEY_CTX_new(key.ptr(), nullptr)));
    EVP_MD_CTX_set_pkey_ctx(ctx.ptr(), key_ctx.ptr());

    OPENSSL_TRY(EVP_DigestSignInit(ctx.ptr(), nullptr, hash_type, nullptr, key.ptr()));
    TRY(configure(key_ctx));

    size_t signature_size = 0;
    OPENSSL_TRY(EVP_DigestSign(ctx.ptr(), nullptr, &signature_size, message.data(), message.size()));

    auto signature = TRY(ByteBuffer::create_uninitialized(signature_size));
    OPENSSL_TRY(EVP_DigestSign(ctx.ptr(), signature.data(), &signature_size, message.data(), message.size()));
    return signature.slice(0, signature_size);
}

ErrorOr<void> RSA_PKCS1_EME::configure(OpenSSL_PKEY_CTX& ctx)
{
    OPENSSL_TRY(EVP_PKEY_CTX_set_rsa_padding(ctx.ptr(), RSA_PKCS1_PADDING));
    return {};
}

ErrorOr<void> RSA_PKCS1_EMSA::configure(OpenSSL_PKEY_CTX& ctx)
{
    OPENSSL_TRY(EVP_PKEY_CTX_set_rsa_padding(ctx.ptr(), RSA_PKCS1_PADDING));
    return {};
}

ErrorOr<void> RSA_OAEP_EME::configure(OpenSSL_PKEY_CTX& ctx)
{
    OPENSSL_TRY(EVP_PKEY_CTX_set_rsa_padding(ctx.ptr(), RSA_PKCS1_OAEP_PADDING));
    OPENSSL_TRY(EVP_PKEY_CTX_set_rsa_oaep_md(ctx.ptr(), TRY(hash_kind_to_hash_type(m_hash_kind))));
    OPENSSL_TRY(EVP_PKEY_CTX_set_rsa_mgf1_md(ctx.ptr(), TRY(hash_kind_to_hash_type(m_hash_kind))));

    if (m_label.has_value() && !m_label->is_empty()) {
        // https://docs.openssl.org/3.0/man3/EVP_PKEY_CTX_ctrl/#rsa-parameters
        // The library takes ownership of the label so the caller should not free the original memory pointed to by label.
        auto* label = OPENSSL_malloc(m_label->size());
        memcpy(label, m_label->data(), m_label->size());
        OPENSSL_TRY(EVP_PKEY_CTX_set0_rsa_oaep_label(ctx.ptr(), label, m_label->size()));
    }

    return {};
}

ErrorOr<void> RSA_PSS_EMSA::configure(OpenSSL_PKEY_CTX& ctx)
{
    OPENSSL_TRY(EVP_PKEY_CTX_set_rsa_padding(ctx.ptr(), RSA_PKCS1_PSS_PADDING));
    OPENSSL_TRY(EVP_PKEY_CTX_set_rsa_mgf1_md(ctx.ptr(), TRY(hash_kind_to_hash_type(m_hash_kind))));
    OPENSSL_TRY(EVP_PKEY_CTX_set_rsa_pss_saltlen(ctx.ptr(), m_salt_length.value_or(RSA_PSS_SALTLEN_MAX)));
    return {};
}

}

#endif // AK_OS_RINOS
