/*
 * Copyright (c) 2025, RinOS Contributors
 *
 * SPDX-License-Identifier: BSD-2-Clause
 *
 * Self-contained crypto implementations for algorithms not provided by rintls.
 * Used only when building for RinOS (AK_OS_RINOS).
 *
 * Algorithms: MD5 (RFC 1321), BLAKE2b (RFC 7693), SHA-3/Keccak (FIPS 202),
 *             PBKDF2 (RFC 8018), ChaCha20-Poly1305 (RFC 8439),
 *             AES-CBC/CTR/OCB/KW helpers (using rintls AES block cipher)
 */

#pragma once

#ifdef AK_OS_RINOS

#include <AK/Types.h>
#include <string.h>

// ── MD5 (RFC 1321) ────────────────────────────────────────────────────────────

struct rin_md5_ctx {
    u32 state[4];
    u64 count;
    u8 buffer[64];
};

void rin_md5_init(rin_md5_ctx* ctx);
void rin_md5_update(rin_md5_ctx* ctx, u8 const* data, size_t len);
void rin_md5_final(rin_md5_ctx* ctx, u8* digest);

// ── BLAKE2b (RFC 7693) ───────────────────────────────────────────────────────

struct rin_blake2b_ctx {
    u64 h[8];
    u64 t[2];
    u64 f[2];
    u8 buf[128];
    size_t buflen;
    size_t outlen;
};

void rin_blake2b_init(rin_blake2b_ctx* ctx, size_t outlen);
void rin_blake2b_update(rin_blake2b_ctx* ctx, u8 const* data, size_t len);
void rin_blake2b_final(rin_blake2b_ctx* ctx, u8* digest);

// Default 64-byte output versions for Ladybird BLAKE2b (512-bit)
inline void rin_blake2b_init_512(rin_blake2b_ctx* ctx) { rin_blake2b_init(ctx, 64); }
inline void rin_blake2b_final_512(rin_blake2b_ctx* ctx, u8* digest) { rin_blake2b_final(ctx, digest); }

// ── SHA-3 / Keccak (FIPS 202) ────────────────────────────────────────────────

struct rin_keccak_ctx {
    u64 state[25]; // 1600-bit state
    u8 buf[200];   // max rate = 1600/8
    size_t buflen;
    size_t rate;       // rate in bytes (e.g. 136 for SHA3-256)
    size_t capacity;   // capacity in bytes
    size_t digest_len; // output length in bytes
    bool xof;          // true for SHAKE, false for SHA-3
};

void rin_sha3_256_init(rin_keccak_ctx* ctx);
void rin_sha3_384_init(rin_keccak_ctx* ctx);
void rin_sha3_512_init(rin_keccak_ctx* ctx);
void rin_keccak_update(rin_keccak_ctx* ctx, u8 const* data, size_t len);
void rin_keccak_final(rin_keccak_ctx* ctx, u8* digest);

// SHAKE XOF
void rin_shake128_init(rin_keccak_ctx* ctx);
void rin_shake256_init(rin_keccak_ctx* ctx);
void rin_shake_squeeze(rin_keccak_ctx* ctx, u8* out, size_t outlen);

// ── ChaCha20-Poly1305 (RFC 8439) ─────────────────────────────────────────────

int rin_chacha20_poly1305_encrypt(
    u8 const* key, // 32 bytes
    u8 const* nonce, // 12 bytes
    u8 const* aad, size_t aad_len,
    u8 const* plaintext, size_t pt_len,
    u8* ciphertext, // pt_len bytes
    u8* tag);       // 16 bytes

int rin_chacha20_poly1305_decrypt(
    u8 const* key, // 32 bytes
    u8 const* nonce, // 12 bytes
    u8 const* aad, size_t aad_len,
    u8 const* ciphertext, size_t ct_len,
    u8 const* tag, // 16 bytes
    u8* plaintext); // ct_len bytes, returns 0 on success, -1 on auth failure

// ── PBKDF2 (RFC 8018) ────────────────────────────────────────────────────────
// Uses rintls HMAC-SHA256 / HMAC-SHA384 internally

int rin_pbkdf2_hmac_sha256(
    u8 const* password, size_t pwd_len,
    u8 const* salt, size_t salt_len,
    u32 iterations,
    u8* output, size_t output_len);

int rin_pbkdf2_hmac_sha384(
    u8 const* password, size_t pwd_len,
    u8 const* salt, size_t salt_len,
    u32 iterations,
    u8* output, size_t output_len);

// ── AES-CBC (using rintls block cipher) ──────────────────────────────────────

int rin_aes_cbc_encrypt(
    u8 const* key, int key_size,
    u8 const* iv, // AES_BLOCK_SIZE=16
    u8 const* plaintext, size_t pt_len,
    u8* ciphertext, size_t* ct_len,
    int pkcs7_pad); // 1=pad, 0=no pad

int rin_aes_cbc_decrypt(
    u8 const* key, int key_size,
    u8 const* iv,
    u8 const* ciphertext, size_t ct_len,
    u8* plaintext, size_t* pt_len,
    int pkcs7_pad);

// ── AES-CTR (using rintls block cipher) ──────────────────────────────────────

int rin_aes_ctr_crypt(
    u8 const* key, int key_size,
    u8 const* iv, // 16 bytes
    u8 const* input, size_t len,
    u8* output);

// ── AES-KW (RFC 3394 Key Wrap) ──────────────────────────────────────────────

int rin_aes_kw_wrap(
    u8 const* key, int key_size,
    u8 const* plaintext, size_t pt_len,
    u8* ciphertext, size_t* ct_len);

int rin_aes_kw_unwrap(
    u8 const* key, int key_size,
    u8 const* ciphertext, size_t ct_len,
    u8* plaintext, size_t* pt_len);

#endif // AK_OS_RINOS
