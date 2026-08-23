/*
 * Copyright (c) 2025, Miguel Sacristán Izcue <miguel_tete17@hotmail.com>
 *
 * SPDX-License-Identifier: BSD-2-Clause
 */

#include <LibCrypto/PK/MLKEM.h>

#include <AK/ScopeGuard.h>
#include <AK/Tuple.h>
#include <LibCrypto/ASN1/DER.h>
#include <LibCrypto/Curves/SECPxxxr1.h>

#ifdef AK_OS_RINOS

extern "C" {
#include "../../../../rintls/crypto/pqc.h"
}

namespace Crypto::PK {

static void zeroize_rintls_key_material(ByteBuffer& buffer)
{
    rintls_secure_zero(buffer.data(), buffer.size());
}

static ErrorOr<u32> rintls_mlkem_level(MLKEMSize size)
{
    switch (size) {
    case MLKEMSize::MLKEM512:
        return RINTLS_MLKEM_512;
    case MLKEMSize::MLKEM768:
        return RINTLS_MLKEM_768;
    case MLKEMSize::MLKEM1024:
        return RINTLS_MLKEM_1024;
    }
    VERIFY_NOT_REACHED();
}

static ErrorOr<Tuple<rin_size_t, rin_size_t, rin_size_t>> rintls_mlkem_sizes_for(MLKEMSize size)
{
    rin_size_t public_key_size;
    rin_size_t private_key_size;
    rin_size_t ciphertext_size;
    if (rintls_mlkem_sizes(TRY(rintls_mlkem_level(size)), &public_key_size, &private_key_size, &ciphertext_size) != 0)
        return Error::from_string_literal("Unsupported ML-KEM parameter set");
    return Tuple { public_key_size, private_key_size, ciphertext_size };
}

static ErrorOr<ByteBuffer> read_mlkem_seed(ASN1::Decoder& decoder, Vector<StringView>& current_scope)
{
    READ_OBJECT(OctetString, StringView, seed_bits);
    if (seed_bits.bytes().size() != RINTLS_MLKEM_SEED_SIZE)
        ERROR_WITH_SCOPE("Invalid seed length");
    POP_SCOPE();
    return ByteBuffer::copy(seed_bits.bytes());
}

static ErrorOr<ByteBuffer> read_mlkem_private_key(MLKEMSize size, ASN1::Decoder& decoder, Vector<StringView>& current_scope)
{
    ENTER_TYPED_SCOPE(OctetString, "expandedKey");
    READ_OBJECT(OctetString, StringView, private_key_bits);
    auto sizes = TRY(rintls_mlkem_sizes_for(size));
    auto private_key_size = sizes.get<1>();
    if (private_key_bits.bytes().size() != private_key_size)
        ERROR_WITH_SCOPE("Invalid expandedKey size");
    POP_SCOPE();
    return ByteBuffer::copy(private_key_bits.bytes());
}

ErrorOr<ByteBuffer> MLKEMPrivateKey::export_as_der() const
{
    ASN1::Encoder encoder;
    if (m_seed.is_empty())
        TRY(encoder.write<ReadonlyBytes>(m_private_key));
    else
        TRY(encoder.write<ReadonlyBytes>(m_seed, ASN1::Class::Context, static_cast<ASN1::Kind>(0)));
    return encoder.finish();
}

ErrorOr<MLKEM::KeyPairType> MLKEM::parse_mlkem_key(MLKEMSize size, ReadonlyBytes der, Vector<StringView> current_scope)
{
    ASN1::Decoder decoder(der);
    if (decoder.eof())
        return Error::from_string_literal("Input key is empty");
    auto const tag = TRY(decoder.peek());
    if (static_cast<u8>(tag.kind) == 0) {
        REWRITE_TAG(OctetString);
        return generate_key_pair(size, TRY(read_mlkem_seed(decoder, current_scope)));
    }
    if (tag.kind == ASN1::Kind::OctetString) {
        auto private_key = TRY(read_mlkem_private_key(size, decoder, current_scope));
        ArmedScopeGuard clear_private_key = [&] { zeroize_rintls_key_material(private_key); };
        auto sizes = TRY(rintls_mlkem_sizes_for(size));
        auto public_key_size = sizes.get<0>();
        auto public_key = TRY(ByteBuffer::create_uninitialized(public_key_size));
        ArmedScopeGuard clear_public_key = [&] { zeroize_rintls_key_material(public_key); };
        if (rintls_mlkem_public_from_private(TRY(rintls_mlkem_level(size)), public_key.data(), private_key.data()) != 0)
            return Error::from_string_literal("Invalid ML-KEM expanded key");
        clear_private_key.disarm();
        clear_public_key.disarm();
        return KeyPairType { MLKEMPublicKey { public_key }, { {}, move(public_key), move(private_key) } };
    }
    if (tag.kind == ASN1::Kind::Sequence) {
        ENTER_TYPED_SCOPE(Sequence, "both");
        ENTER_TYPED_SCOPE(OctetString, "seed");
        auto key_pair = TRY(generate_key_pair(size, TRY(read_mlkem_seed(decoder, current_scope))));
        POP_SCOPE();
        ENTER_TYPED_SCOPE(OctetString, "expandedKey");
        if (auto const expanded_key = TRY(read_mlkem_private_key(size, decoder, current_scope));
            key_pair.private_key.private_key() != expanded_key)
            ERROR_WITH_SCOPE("Invalid expanded_key");
        POP_SCOPE();
        POP_SCOPE();
        return key_pair;
    }
    return Error::from_string_literal("Invalid key format");
}

ErrorOr<MLKEMEncapsulation> MLKEM::encapsulate(MLKEMSize size, MLKEMPublicKey const& key)
{
    auto level = TRY(rintls_mlkem_level(size));
    auto sizes = TRY(rintls_mlkem_sizes_for(size));
    auto public_key_size = sizes.get<0>();
    auto ciphertext_size = sizes.get<2>();
    auto public_key = key.public_key();
    if (public_key.size() != public_key_size)
        return Error::from_string_literal("Invalid ML-KEM public key");
    auto ciphertext = TRY(ByteBuffer::create_uninitialized(ciphertext_size));
    auto shared_key = TRY(ByteBuffer::create_uninitialized(RINTLS_MLKEM_SHARED_SECRET_SIZE));
    ArmedScopeGuard clear_shared_key = [&] { zeroize_rintls_key_material(shared_key); };
    if (rintls_mlkem_encapsulate(level, ciphertext.data(), shared_key.data(), public_key.data()) != 0)
        return Error::from_string_literal("ML-KEM encapsulation failed");
    clear_shared_key.disarm();
    return MLKEMEncapsulation { move(shared_key), move(ciphertext) };
}

ErrorOr<ByteBuffer> MLKEM::decapsulate(MLKEMSize size, MLKEMPrivateKey const& key, ByteBuffer ciphertext)
{
    auto level = TRY(rintls_mlkem_level(size));
    auto sizes = TRY(rintls_mlkem_sizes_for(size));
    auto private_key_size = sizes.get<1>();
    auto ciphertext_size = sizes.get<2>();
    if (key.private_key().size() != private_key_size || ciphertext.size() != ciphertext_size)
        return Error::from_string_literal("Invalid ML-KEM ciphertext or private key");
    auto shared_key = TRY(ByteBuffer::create_uninitialized(RINTLS_MLKEM_SHARED_SECRET_SIZE));
    ArmedScopeGuard clear_shared_key = [&] { zeroize_rintls_key_material(shared_key); };
    if (rintls_mlkem_decapsulate(level, shared_key.data(), ciphertext.data(), key.private_key().data()) != 0)
        return Error::from_string_literal("ML-KEM decapsulation failed");
    clear_shared_key.disarm();
    return shared_key;
}

ErrorOr<MLKEM::KeyPairType> MLKEM::generate_key_pair(MLKEMSize size, ByteBuffer seed)
{
    auto level = TRY(rintls_mlkem_level(size));
    auto sizes = TRY(rintls_mlkem_sizes_for(size));
    auto public_key_size = sizes.get<0>();
    auto private_key_size = sizes.get<1>();
    if (!seed.is_empty() && seed.size() != RINTLS_MLKEM_SEED_SIZE)
        return Error::from_string_literal("Invalid ML-KEM seed length");
    bool generate_seed = seed.is_empty();
    if (generate_seed)
        seed = TRY(ByteBuffer::create_uninitialized(RINTLS_MLKEM_SEED_SIZE));
    ArmedScopeGuard clear_seed = [&] { zeroize_rintls_key_material(seed); };
    auto public_key = TRY(ByteBuffer::create_uninitialized(public_key_size));
    ArmedScopeGuard clear_public_key = [&] { zeroize_rintls_key_material(public_key); };
    auto private_key = TRY(ByteBuffer::create_uninitialized(private_key_size));
    ArmedScopeGuard clear_private_key = [&] { zeroize_rintls_key_material(private_key); };
    auto result = generate_seed
        ? rintls_mlkem_keygen(level, seed.data(), public_key.data(), private_key.data())
        : rintls_mlkem_keygen_from_seed(level, seed.data(), public_key.data(), private_key.data());
    if (result != 0)
        return Error::from_string_literal("ML-KEM key generation failed");
    clear_seed.disarm();
    clear_public_key.disarm();
    clear_private_key.disarm();
    return KeyPairType { MLKEMPublicKey { public_key }, { move(seed), move(public_key), move(private_key) } };
}

}

#else // !AK_OS_RINOS

#include <LibCrypto/OpenSSL.h>

#include <openssl/core_names.h>
#include <openssl/evp.h>
#include <openssl/param_build.h>

namespace Crypto::PK {

static char const* mlkem_size_to_openssl_name(MLKEMSize size)
{
    switch (size) {
    case MLKEMSize::MLKEM512:
        return "ML-KEM-512";
    case MLKEMSize::MLKEM768:
        return "ML-KEM-768";
    case MLKEMSize::MLKEM1024:
        return "ML-KEM-1024";
    default:
        VERIFY_NOT_REACHED();
    }
}

static ErrorOr<ByteBuffer> read_mlkem_seed(ASN1::Decoder& decoder, Vector<StringView>& current_scope)
{
    // seed ::= OCTET STRING (SIZE (64))
    READ_OBJECT(OctetString, StringView, seed_bits);

    auto const seed = seed_bits.bytes();
    if (seed.size() != 64) {
        ERROR_WITH_SCOPE("Invalid seed length");
    }
    POP_SCOPE();

    return ByteBuffer::copy(seed);
}

static ErrorOr<ByteBuffer> read_mlkem_private_key(MLKEMSize size, ASN1::Decoder& decoder, Vector<StringView>& current_scope)
{
    // expandedKey ::= OCTET STRING (SIZE (1632 | 2400 | 3168))

    ENTER_TYPED_SCOPE(OctetString, "expandedKey");
    READ_OBJECT(OctetString, StringView, expanded_key_bits);

    auto const expanded_key = expanded_key_bits.bytes();
    switch (size) {
    case MLKEMSize::MLKEM512:
        if (expanded_key.size() != 1632) {
            ERROR_WITH_SCOPE("Invalid expandedKey size");
        }
        break;
    case MLKEMSize::MLKEM768:
        if (expanded_key.size() != 2400) {
            ERROR_WITH_SCOPE("Invalid expandedKey size");
        }
        break;
    case MLKEMSize::MLKEM1024:
        if (expanded_key.size() != 3168) {
            ERROR_WITH_SCOPE("Invalid expandedKey size");
        }
        break;
    default:
        VERIFY_NOT_REACHED();
    }
    POP_SCOPE();

    return ByteBuffer::copy(expanded_key);
}

ErrorOr<ByteBuffer> MLKEMPrivateKey::export_as_der() const
{
    ASN1::Encoder encoder;

    TRY(encoder.write<ReadonlyBytes>(m_seed, ASN1::Class::Context, static_cast<ASN1::Kind>(0)));

    return encoder.finish();
}

// https://datatracker.ietf.org/doc/html/draft-ietf-lamps-kyber-certificates-11#autoid-7
ErrorOr<MLKEM::KeyPairType> MLKEM::parse_mlkem_key(MLKEMSize size, ReadonlyBytes der, Vector<StringView> current_scope)
{
    ASN1::Decoder decoder(der);

    // ML-KEM-PrivateKey ::= CHOICE {
    //      seed [0] IMPLICIT OCTET STRING (SIZE (64)),
    //      expandedKey OCTET STRING (SIZE (1632 | 2400 | 3168)),
    //      both SEQUENCE {
    //           seed OCTET STRING (SIZE (64)),
    //           expandedKey OCTET STRING (SIZE (1632 | 2400 | 3168))
    //      }
    // }

    if (decoder.eof()) {
        return Error::from_string_literal("Input key is empty");
    }

    auto const tag = TRY(decoder.peek());
    if (static_cast<u8>(tag.kind) == 0) {
        REWRITE_TAG(OctetString);
        return generate_key_pair(size, TRY(read_mlkem_seed(decoder, current_scope)));
    }
    if (tag.kind == ASN1::Kind::OctetString) {
        return KeyPairType {
            {},
            { {}, {}, TRY(read_mlkem_private_key(size, decoder, current_scope)) }
        };
    }
    if (tag.kind == ASN1::Kind::Sequence) {
        ENTER_TYPED_SCOPE(Sequence, "both");
        ENTER_TYPED_SCOPE(OctetString, "seed");
        auto key_pair = TRY(generate_key_pair(size, TRY(read_mlkem_seed(decoder, current_scope))));
        POP_SCOPE()

        ENTER_TYPED_SCOPE(OctetString, "expandedKey");
        if (auto const expanded_key = TRY(read_mlkem_private_key(size, decoder, current_scope));
            key_pair.private_key.private_key() != expanded_key) {
            ERROR_WITH_SCOPE("Invalid expanded_key");
        }
        POP_SCOPE();

        POP_SCOPE();
        return key_pair;
    }

    return Error::from_string_literal("Invalid key format");
}

ErrorOr<MLKEMEncapsulation> MLKEM::encapsulate(MLKEMSize size, MLKEMPublicKey const& key)
{
    auto public_key = TRY(OpenSSL_PKEY::wrap(EVP_PKEY_new_raw_public_key_ex(nullptr, mlkem_size_to_openssl_name(size), nullptr, key.public_key().data(), key.public_key().size())));

    auto ctx = TRY(OpenSSL_PKEY_CTX::wrap(EVP_PKEY_CTX_new_from_pkey(nullptr, public_key.ptr(), nullptr)));

    OPENSSL_TRY(EVP_PKEY_encapsulate_init(ctx.ptr(), nullptr));

    size_t shared_key_size;
    size_t ciphertext_length;
    OPENSSL_TRY(EVP_PKEY_encapsulate(ctx.ptr(), nullptr, &ciphertext_length, nullptr, &shared_key_size));

    auto shared_key = TRY(ByteBuffer::create_uninitialized(shared_key_size));
    auto ciphertext = TRY(ByteBuffer::create_uninitialized(ciphertext_length));

    OPENSSL_TRY(EVP_PKEY_encapsulate(ctx.ptr(), ciphertext.data(), &ciphertext_length, shared_key.data(), &shared_key_size));

    return MLKEMEncapsulation { shared_key, ciphertext };
}

static ErrorOr<OpenSSL_PKEY> private_key_to_openssl_pkey(MLKEMSize size, MLKEMPrivateKey const& private_key)
{
    auto ctx = TRY(OpenSSL_PKEY_CTX::wrap(EVP_PKEY_CTX_new_from_name(nullptr, mlkem_size_to_openssl_name(size), nullptr)));

    OPENSSL_TRY(EVP_PKEY_fromdata_init(ctx.ptr()));

    auto* params_bld = OPENSSL_TRY_PTR(OSSL_PARAM_BLD_new());
    ScopeGuard const free_params_bld = [&] { OSSL_PARAM_BLD_free(params_bld); };

    OPENSSL_TRY(OSSL_PARAM_BLD_push_octet_string(params_bld, OSSL_PKEY_PARAM_ML_KEM_SEED, private_key.seed().data(), private_key.seed().size()));
    OPENSSL_TRY(OSSL_PARAM_BLD_push_octet_string(params_bld, OSSL_PKEY_PARAM_ENCODED_PUBLIC_KEY, private_key.public_key().data(), private_key.public_key().size()));
    OPENSSL_TRY(OSSL_PARAM_BLD_push_octet_string(params_bld, OSSL_PKEY_PARAM_PRIV_KEY, private_key.private_key().data(), private_key.private_key().size()));

    auto* params = OSSL_PARAM_BLD_to_param(params_bld);
    ScopeGuard const free_params = [&] { OSSL_PARAM_free(params); };

    auto key = TRY(OpenSSL_PKEY::create());
    auto* key_ptr = key.ptr();
    OPENSSL_TRY(EVP_PKEY_fromdata(ctx.ptr(), &key_ptr, EVP_PKEY_KEYPAIR, params));

    return key;
}

ErrorOr<ByteBuffer> MLKEM::decapsulate(MLKEMSize size, MLKEMPrivateKey const& key, ByteBuffer ciphertext)
{
    auto private_key = TRY(private_key_to_openssl_pkey(size, key));

    auto ctx = TRY(OpenSSL_PKEY_CTX::wrap(EVP_PKEY_CTX_new_from_pkey(nullptr, private_key.ptr(), nullptr)));

    OPENSSL_TRY(EVP_PKEY_decapsulate_init(ctx.ptr(), nullptr));

    size_t shared_key_size;
    OPENSSL_TRY(EVP_PKEY_decapsulate(ctx.ptr(), nullptr, &shared_key_size, ciphertext.data(), ciphertext.size()));

    auto shared_key = TRY(ByteBuffer::create_uninitialized(shared_key_size));
    OPENSSL_TRY(EVP_PKEY_decapsulate(ctx.ptr(), shared_key.data(), &shared_key_size, ciphertext.data(), ciphertext.size()));

    return shared_key;
}

ErrorOr<MLKEM::KeyPairType> MLKEM::generate_key_pair(MLKEMSize size, ByteBuffer seed)
{
    auto ctx = TRY(OpenSSL_PKEY_CTX::wrap(EVP_PKEY_CTX_new_from_name(nullptr, mlkem_size_to_openssl_name(size), nullptr)));

    OPENSSL_TRY(EVP_PKEY_keygen_init(ctx.ptr()));

    OSSL_PARAM params[2] = {
        OSSL_PARAM_END,
        OSSL_PARAM_END
    };

    if (!seed.is_empty()) {
        params[0] = OSSL_PARAM_octet_string(OSSL_PKEY_PARAM_ML_KEM_SEED, seed.data(), seed.size());
    }

    OPENSSL_TRY(EVP_PKEY_CTX_set_params(ctx.ptr(), params));
    auto key = TRY(OpenSSL_PKEY::create());
    auto* key_ptr = key.ptr();
    OPENSSL_TRY(EVP_PKEY_generate(ctx.ptr(), &key_ptr));

    auto pub = TRY(get_byte_buffer_param_from_key(key, OSSL_PKEY_PARAM_PUB_KEY));
    auto priv = TRY(get_byte_buffer_param_from_key(key, OSSL_PKEY_PARAM_PRIV_KEY));
    seed = TRY(get_byte_buffer_param_from_key(key, OSSL_PKEY_PARAM_ML_KEM_SEED));

    return KeyPairType {
        MLKEMPublicKey { pub },
        { seed, pub, priv }
    };
}

}

#endif // AK_OS_RINOS
