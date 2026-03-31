/*
 * Copyright (c) 2025, RinOS Contributors
 *
 * SPDX-License-Identifier: BSD-2-Clause
 *
 * Self-contained crypto implementations for algorithms not in rintls.
 * Reference implementations — correctness over performance.
 */

#ifdef AK_OS_RINOS

#include <LibCrypto/RinCryptoImpl.h>
#include <string.h>

extern "C" {
#include <crypto/aes.h>
#include <crypto/hmac.h>
}

// ═══════════════════════════════════════════════════════════════════════════════
// MD5 (RFC 1321)
// ═══════════════════════════════════════════════════════════════════════════════

static constexpr u32 md5_k[64] = {
    0xd76aa478, 0xe8c7b756, 0x242070db, 0xc1bdceee, 0xf57c0faf, 0x4787c62a,
    0xa8304613, 0xfd469501, 0x698098d8, 0x8b44f7af, 0xffff5bb1, 0x895cd7be,
    0x6b901122, 0xfd987193, 0xa679438e, 0x49b40821, 0xf61e2562, 0xc040b340,
    0x265e5a51, 0xe9b6c7aa, 0xd62f105d, 0x02441453, 0xd8a1e681, 0xe7d3fbc8,
    0x21e1cde6, 0xc33707d6, 0xf4d50d87, 0x455a14ed, 0xa9e3e905, 0xfcefa3f8,
    0x676f02d9, 0x8d2a4c8a, 0xfffa3942, 0x8771f681, 0x6d9d6122, 0xfde5380c,
    0xa4beea44, 0x4bdecfa9, 0xf6bb4b60, 0xbebfbc70, 0x289b7ec6, 0xeaa127fa,
    0xd4ef3085, 0x04881d05, 0xd9d4d039, 0xe6db99e5, 0x1fa27cf8, 0xc4ac5665,
    0xf4292244, 0x432aff97, 0xab9423a7, 0xfc93a039, 0x655b59c3, 0x8f0ccc92,
    0xffeff47d, 0x85845dd1, 0x6fa87e4f, 0xfe2ce6e0, 0xa3014314, 0x4e0811a1,
    0xf7537e82, 0xbd3af235, 0x2ad7d2bb, 0xeb86d391,
};

static constexpr u8 md5_s[64] = {
    7, 12, 17, 22, 7, 12, 17, 22, 7, 12, 17, 22, 7, 12, 17, 22,
    5, 9, 14, 20, 5, 9, 14, 20, 5, 9, 14, 20, 5, 9, 14, 20,
    4, 11, 16, 23, 4, 11, 16, 23, 4, 11, 16, 23, 4, 11, 16, 23,
    6, 10, 15, 21, 6, 10, 15, 21, 6, 10, 15, 21, 6, 10, 15, 21,
};

static inline u32 md5_rotl(u32 x, u8 n) { return (x << n) | (x >> (32 - n)); }

static void md5_transform(u32* state, u8 const* block)
{
    u32 m[16];
    for (int i = 0; i < 16; i++)
        m[i] = static_cast<u32>(block[i * 4]) | (static_cast<u32>(block[i * 4 + 1]) << 8) | (static_cast<u32>(block[i * 4 + 2]) << 16) | (static_cast<u32>(block[i * 4 + 3]) << 24);

    u32 a = state[0], b = state[1], c = state[2], d = state[3];

    for (int i = 0; i < 64; i++) {
        u32 f, g;
        if (i < 16) {
            f = (b & c) | (~b & d);
            g = i;
        } else if (i < 32) {
            f = (d & b) | (~d & c);
            g = (5 * i + 1) % 16;
        } else if (i < 48) {
            f = b ^ c ^ d;
            g = (3 * i + 5) % 16;
        } else {
            f = c ^ (b | ~d);
            g = (7 * i) % 16;
        }
        u32 temp = d;
        d = c;
        c = b;
        b = b + md5_rotl(a + f + md5_k[i] + m[g], md5_s[i]);
        a = temp;
    }

    state[0] += a;
    state[1] += b;
    state[2] += c;
    state[3] += d;
}

void rin_md5_init(rin_md5_ctx* ctx)
{
    ctx->state[0] = 0x67452301;
    ctx->state[1] = 0xefcdab89;
    ctx->state[2] = 0x98badcfe;
    ctx->state[3] = 0x10325476;
    ctx->count = 0;
    memset(ctx->buffer, 0, sizeof(ctx->buffer));
}

void rin_md5_update(rin_md5_ctx* ctx, u8 const* data, size_t len)
{
    size_t index = ctx->count % 64;
    ctx->count += len;

    size_t i = 0;
    if (index) {
        size_t part = 64 - index;
        if (len >= part) {
            memcpy(ctx->buffer + index, data, part);
            md5_transform(ctx->state, ctx->buffer);
            i = part;
        } else {
            memcpy(ctx->buffer + index, data, len);
            return;
        }
    }

    for (; i + 64 <= len; i += 64)
        md5_transform(ctx->state, data + i);

    if (i < len)
        memcpy(ctx->buffer, data + i, len - i);
}

void rin_md5_final(rin_md5_ctx* ctx, u8* digest)
{
    u64 bits = ctx->count * 8;
    size_t index = ctx->count % 64;

    u8 pad = 0x80;
    rin_md5_update(ctx, &pad, 1);

    u8 zero = 0;
    while (ctx->count % 64 != 56)
        rin_md5_update(ctx, &zero, 1);

    u8 len_buf[8];
    for (int i = 0; i < 8; i++)
        len_buf[i] = static_cast<u8>(bits >> (i * 8));
    rin_md5_update(ctx, len_buf, 8);

    for (int i = 0; i < 4; i++) {
        digest[i * 4 + 0] = static_cast<u8>(ctx->state[i]);
        digest[i * 4 + 1] = static_cast<u8>(ctx->state[i] >> 8);
        digest[i * 4 + 2] = static_cast<u8>(ctx->state[i] >> 16);
        digest[i * 4 + 3] = static_cast<u8>(ctx->state[i] >> 24);
    }
}

// ═══════════════════════════════════════════════════════════════════════════════
// BLAKE2b (RFC 7693)
// ═══════════════════════════════════════════════════════════════════════════════

static constexpr u64 blake2b_iv[8] = {
    0x6A09E667F3BCC908ULL, 0xBB67AE8584CAA73BULL,
    0x3C6EF372FE94F82BULL, 0xA54FF53A5F1D36F1ULL,
    0x510E527FADE682D1ULL, 0x9B05688C2B3E6C1FULL,
    0x1F83D9ABFB41BD6BULL, 0x5BE0CD19137E2179ULL,
};

static constexpr u8 blake2b_sigma[12][16] = {
    { 0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15 },
    { 14, 10, 4, 8, 9, 15, 13, 6, 1, 12, 0, 2, 11, 7, 5, 3 },
    { 11, 8, 12, 0, 5, 2, 15, 13, 10, 14, 3, 6, 7, 1, 9, 4 },
    { 7, 9, 3, 1, 13, 12, 11, 14, 2, 6, 5, 10, 4, 0, 15, 8 },
    { 9, 0, 5, 7, 2, 4, 10, 15, 14, 1, 11, 12, 6, 8, 3, 13 },
    { 2, 12, 6, 10, 0, 11, 8, 3, 4, 13, 7, 5, 15, 14, 1, 9 },
    { 12, 5, 1, 15, 14, 13, 4, 10, 0, 7, 6, 3, 9, 2, 8, 11 },
    { 13, 11, 7, 14, 12, 1, 3, 9, 5, 0, 15, 4, 8, 6, 2, 10 },
    { 6, 15, 14, 9, 11, 3, 0, 8, 12, 2, 13, 7, 1, 4, 10, 5 },
    { 10, 2, 8, 4, 7, 6, 1, 5, 15, 11, 9, 14, 3, 12, 13, 0 },
    { 0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15 },
    { 14, 10, 4, 8, 9, 15, 13, 6, 1, 12, 0, 2, 11, 7, 5, 3 },
};

static inline u64 blake2b_rotr64(u64 x, int n) { return (x >> n) | (x << (64 - n)); }

#define BLAKE2B_G(r, i, a, b, c, d, m)                                     \
    do {                                                                    \
        a = a + b + m[blake2b_sigma[r][2 * i + 0]];                        \
        d = blake2b_rotr64(d ^ a, 32);                                     \
        c = c + d;                                                          \
        b = blake2b_rotr64(b ^ c, 24);                                     \
        a = a + b + m[blake2b_sigma[r][2 * i + 1]];                        \
        d = blake2b_rotr64(d ^ a, 16);                                     \
        c = c + d;                                                          \
        b = blake2b_rotr64(b ^ c, 63);                                     \
    } while (0)

static void blake2b_compress(rin_blake2b_ctx* ctx, u8 const* block)
{
    u64 m[16], v[16];
    for (int i = 0; i < 16; i++)
        memcpy(&m[i], block + i * 8, 8);

    for (int i = 0; i < 8; i++)
        v[i] = ctx->h[i];
    v[8] = blake2b_iv[0];
    v[9] = blake2b_iv[1];
    v[10] = blake2b_iv[2];
    v[11] = blake2b_iv[3];
    v[12] = blake2b_iv[4] ^ ctx->t[0];
    v[13] = blake2b_iv[5] ^ ctx->t[1];
    v[14] = blake2b_iv[6] ^ ctx->f[0];
    v[15] = blake2b_iv[7] ^ ctx->f[1];

    for (int r = 0; r < 12; r++) {
        BLAKE2B_G(r, 0, v[0], v[4], v[8], v[12], m);
        BLAKE2B_G(r, 1, v[1], v[5], v[9], v[13], m);
        BLAKE2B_G(r, 2, v[2], v[6], v[10], v[14], m);
        BLAKE2B_G(r, 3, v[3], v[7], v[11], v[15], m);
        BLAKE2B_G(r, 4, v[0], v[5], v[10], v[15], m);
        BLAKE2B_G(r, 5, v[1], v[6], v[11], v[12], m);
        BLAKE2B_G(r, 6, v[2], v[7], v[8], v[13], m);
        BLAKE2B_G(r, 7, v[3], v[4], v[9], v[14], m);
    }

    for (int i = 0; i < 8; i++)
        ctx->h[i] ^= v[i] ^ v[i + 8];
}

#undef BLAKE2B_G

void rin_blake2b_init(rin_blake2b_ctx* ctx, size_t outlen)
{
    memset(ctx, 0, sizeof(*ctx));
    for (int i = 0; i < 8; i++)
        ctx->h[i] = blake2b_iv[i];
    ctx->h[0] ^= 0x01010000 ^ static_cast<u64>(outlen);
    ctx->outlen = outlen;
}

void rin_blake2b_update(rin_blake2b_ctx* ctx, u8 const* data, size_t len)
{
    for (size_t i = 0; i < len; i++) {
        if (ctx->buflen == 128) {
            ctx->t[0] += 128;
            if (ctx->t[0] < 128)
                ctx->t[1]++;
            blake2b_compress(ctx, ctx->buf);
            ctx->buflen = 0;
        }
        ctx->buf[ctx->buflen++] = data[i];
    }
}

void rin_blake2b_final(rin_blake2b_ctx* ctx, u8* digest)
{
    ctx->t[0] += ctx->buflen;
    if (ctx->t[0] < ctx->buflen)
        ctx->t[1]++;
    ctx->f[0] = ~static_cast<u64>(0);
    memset(ctx->buf + ctx->buflen, 0, 128 - ctx->buflen);
    blake2b_compress(ctx, ctx->buf);
    memcpy(digest, ctx->h, ctx->outlen);
}

// ═══════════════════════════════════════════════════════════════════════════════
// SHA-3 / Keccak (FIPS 202)
// ═══════════════════════════════════════════════════════════════════════════════

static constexpr u64 keccak_rc[24] = {
    0x0000000000000001ULL, 0x0000000000008082ULL, 0x800000000000808AULL,
    0x8000000080008000ULL, 0x000000000000808BULL, 0x0000000080000001ULL,
    0x8000000080008081ULL, 0x8000000000008009ULL, 0x000000000000008AULL,
    0x0000000000000088ULL, 0x0000000080008009ULL, 0x000000008000000AULL,
    0x000000008000808BULL, 0x800000000000008BULL, 0x8000000000008089ULL,
    0x8000000000008003ULL, 0x8000000000008002ULL, 0x8000000000000080ULL,
    0x000000000000800AULL, 0x800000008000000AULL, 0x8000000080008081ULL,
    0x8000000000008080ULL, 0x0000000080000001ULL, 0x8000000080008008ULL,
};

static constexpr int keccak_rot[25] = {
    0, 1, 62, 28, 27, 36, 44, 6, 55, 20,
    3, 10, 43, 25, 39, 41, 45, 15, 21, 8,
    18, 2, 61, 56, 14,
};

static constexpr int keccak_pi[25] = {
    0, 10, 20, 5, 15, 16, 1, 11, 21, 6,
    7, 17, 2, 12, 22, 23, 8, 18, 3, 13,
    14, 24, 9, 19, 4,
};

static inline u64 keccak_rotl64(u64 x, int n) { return (x << n) | (x >> (64 - n)); }

static void keccak_f1600(u64* state)
{
    u64 bc[5], t;
    for (int round = 0; round < 24; round++) {
        // Theta
        for (int i = 0; i < 5; i++)
            bc[i] = state[i] ^ state[i + 5] ^ state[i + 10] ^ state[i + 15] ^ state[i + 20];
        for (int i = 0; i < 5; i++) {
            t = bc[(i + 4) % 5] ^ keccak_rotl64(bc[(i + 1) % 5], 1);
            for (int j = 0; j < 25; j += 5)
                state[j + i] ^= t;
        }
        // Rho and Pi
        u64 tmp[25];
        for (int i = 0; i < 25; i++)
            tmp[keccak_pi[i]] = keccak_rotl64(state[i], keccak_rot[i]);
        // Chi
        for (int j = 0; j < 25; j += 5) {
            for (int i = 0; i < 5; i++)
                state[j + i] = tmp[j + i] ^ (~tmp[j + (i + 1) % 5] & tmp[j + (i + 2) % 5]);
        }
        // Iota
        state[0] ^= keccak_rc[round];
    }
}

static void keccak_init(rin_keccak_ctx* ctx, size_t rate, size_t digest_len, bool xof)
{
    memset(ctx, 0, sizeof(*ctx));
    ctx->rate = rate;
    ctx->capacity = 200 - rate;
    ctx->digest_len = digest_len;
    ctx->xof = xof;
}

void rin_sha3_256_init(rin_keccak_ctx* ctx) { keccak_init(ctx, 136, 32, false); }
void rin_sha3_384_init(rin_keccak_ctx* ctx) { keccak_init(ctx, 104, 48, false); }
void rin_sha3_512_init(rin_keccak_ctx* ctx) { keccak_init(ctx, 72, 64, false); }

void rin_keccak_update(rin_keccak_ctx* ctx, u8 const* data, size_t len)
{
    for (size_t i = 0; i < len; i++) {
        ctx->buf[ctx->buflen++] = data[i];
        if (ctx->buflen == ctx->rate) {
            for (size_t j = 0; j < ctx->rate / 8; j++) {
                u64 lane;
                memcpy(&lane, ctx->buf + j * 8, 8);
                ctx->state[j] ^= lane;
            }
            keccak_f1600(ctx->state);
            ctx->buflen = 0;
        }
    }
}

void rin_keccak_final(rin_keccak_ctx* ctx, u8* digest)
{
    // Pad: SHA-3 uses 0x06, SHAKE uses 0x1f
    u8 pad_byte = ctx->xof ? 0x1f : 0x06;
    ctx->buf[ctx->buflen++] = pad_byte;
    memset(ctx->buf + ctx->buflen, 0, ctx->rate - ctx->buflen);
    ctx->buf[ctx->rate - 1] |= 0x80;

    for (size_t j = 0; j < ctx->rate / 8; j++) {
        u64 lane;
        memcpy(&lane, ctx->buf + j * 8, 8);
        ctx->state[j] ^= lane;
    }
    keccak_f1600(ctx->state);

    memcpy(digest, ctx->state, ctx->digest_len);
}

void rin_shake128_init(rin_keccak_ctx* ctx) { keccak_init(ctx, 168, 0, true); }
void rin_shake256_init(rin_keccak_ctx* ctx) { keccak_init(ctx, 136, 0, true); }

void rin_shake_squeeze(rin_keccak_ctx* ctx, u8* out, size_t outlen)
{
    // First, finalize if not yet done (indicated by digest_len == 0 and buflen > 0 or first call)
    // For simplicity, caller should finalize first via rin_keccak_final, then squeeze
    // This is a simplified XOF squeeze
    size_t offset = 0;
    while (offset < outlen) {
        size_t chunk = outlen - offset;
        if (chunk > ctx->rate)
            chunk = ctx->rate;
        memcpy(out + offset, ctx->state, chunk);
        offset += chunk;
        if (offset < outlen)
            keccak_f1600(ctx->state);
    }
}

// ═══════════════════════════════════════════════════════════════════════════════
// ChaCha20-Poly1305 (RFC 8439)
// ═══════════════════════════════════════════════════════════════════════════════

static inline u32 chacha_rotl32(u32 x, int n) { return (x << n) | (x >> (32 - n)); }

#define CHACHA_QR(a, b, c, d) \
    a += b; d ^= a; d = chacha_rotl32(d, 16); \
    c += d; b ^= c; b = chacha_rotl32(b, 12); \
    a += b; d ^= a; d = chacha_rotl32(d, 8);  \
    c += d; b ^= c; b = chacha_rotl32(b, 7);

static void chacha20_block(u32 const* input, u8* output)
{
    u32 x[16];
    memcpy(x, input, 64);
    for (int i = 0; i < 10; i++) {
        CHACHA_QR(x[0], x[4], x[8], x[12]);
        CHACHA_QR(x[1], x[5], x[9], x[13]);
        CHACHA_QR(x[2], x[6], x[10], x[14]);
        CHACHA_QR(x[3], x[7], x[11], x[15]);
        CHACHA_QR(x[0], x[5], x[10], x[15]);
        CHACHA_QR(x[1], x[6], x[11], x[12]);
        CHACHA_QR(x[2], x[7], x[8], x[13]);
        CHACHA_QR(x[3], x[4], x[9], x[14]);
    }
    for (int i = 0; i < 16; i++) {
        x[i] += input[i];
        memcpy(output + i * 4, &x[i], 4);
    }
}

#undef CHACHA_QR

static void chacha20_encrypt(u8 const* key, u8 const* nonce, u32 counter,
    u8 const* input, size_t len, u8* output)
{
    u32 state[16];
    state[0] = 0x61707865; state[1] = 0x3320646e;
    state[2] = 0x79622d32; state[3] = 0x6b206574;
    memcpy(&state[4], key, 32);
    state[12] = counter;
    memcpy(&state[13], nonce, 12);

    u8 block[64];
    for (size_t off = 0; off < len; off += 64) {
        chacha20_block(state, block);
        state[12]++;
        size_t chunk = len - off;
        if (chunk > 64) chunk = 64;
        for (size_t i = 0; i < chunk; i++)
            output[off + i] = input[off + i] ^ block[i];
    }
}

// Poly1305 (RFC 8439 §2.5)
static void poly1305_clamp(u8* r)
{
    r[3] &= 15; r[7] &= 15; r[11] &= 15; r[15] &= 15;
    r[4] &= 252; r[8] &= 252; r[12] &= 252;
}

// Minimal Poly1305 using 64-bit arithmetic with 130-bit accumulator
// r,s: 16 bytes each from the Poly1305 key
static void poly1305_mac(u8 const* data, size_t len, u8 const* key_r, u8 const* key_s, u8* tag)
{
    // Accumulator in 5 u32 limbs (radix 2^26)
    u32 r[5], h[5], pad[4];

    // Clamp r
    u8 r_clamped[16];
    memcpy(r_clamped, key_r, 16);
    poly1305_clamp(r_clamped);

    // Convert r to 26-bit limbs
    r[0] = (static_cast<u32>(r_clamped[0]) | (static_cast<u32>(r_clamped[1]) << 8) | (static_cast<u32>(r_clamped[2]) << 16) | (static_cast<u32>(r_clamped[3]) << 24)) & 0x3ffffff;
    r[1] = ((static_cast<u32>(r_clamped[3]) >> 2) | (static_cast<u32>(r_clamped[4]) << 6) | (static_cast<u32>(r_clamped[5]) << 14) | (static_cast<u32>(r_clamped[6]) << 22)) & 0x3ffffff;
    r[2] = ((static_cast<u32>(r_clamped[6]) >> 4) | (static_cast<u32>(r_clamped[7]) << 4) | (static_cast<u32>(r_clamped[8]) << 12) | (static_cast<u32>(r_clamped[9]) << 20)) & 0x3ffffff;
    r[3] = ((static_cast<u32>(r_clamped[9]) >> 6) | (static_cast<u32>(r_clamped[10]) << 2) | (static_cast<u32>(r_clamped[11]) << 10) | (static_cast<u32>(r_clamped[12]) << 18)) & 0x3ffffff;
    r[4] = ((static_cast<u32>(r_clamped[12]) >> 8) | (static_cast<u32>(r_clamped[13])) | (static_cast<u32>(r_clamped[14]) << 8) | (static_cast<u32>(r_clamped[15]) << 16)) & 0x3ffffff;

    // s pad
    memcpy(pad, key_s, 16);

    memset(h, 0, sizeof(h));

    // Pre-compute r*5 for limbs 1-4
    u32 s1 = r[1] * 5, s2 = r[2] * 5, s3 = r[3] * 5, s4 = r[4] * 5;

    u8 block[16];
    for (size_t off = 0; off < len; off += 16) {
        size_t chunk = len - off;
        if (chunk > 16) chunk = 16;
        memcpy(block, data + off, chunk);
        if (chunk < 16) memset(block + chunk, 0, 16 - chunk);

        // Add block to accumulator
        u32 hibit = (chunk < 16) ? 0 : (1 << 24);
        h[0] += (static_cast<u32>(block[0]) | (static_cast<u32>(block[1]) << 8) | (static_cast<u32>(block[2]) << 16) | (static_cast<u32>(block[3]) << 24)) & 0x3ffffff;
        h[1] += ((static_cast<u32>(block[3]) >> 2) | (static_cast<u32>(block[4]) << 6) | (static_cast<u32>(block[5]) << 14) | (static_cast<u32>(block[6]) << 22)) & 0x3ffffff;
        h[2] += ((static_cast<u32>(block[6]) >> 4) | (static_cast<u32>(block[7]) << 4) | (static_cast<u32>(block[8]) << 12) | (static_cast<u32>(block[9]) << 20)) & 0x3ffffff;
        h[3] += ((static_cast<u32>(block[9]) >> 6) | (static_cast<u32>(block[10]) << 2) | (static_cast<u32>(block[11]) << 10) | (static_cast<u32>(block[12]) << 18)) & 0x3ffffff;
        h[4] += ((static_cast<u32>(block[12]) >> 8) | (static_cast<u32>(block[13])) | (static_cast<u32>(block[14]) << 8) | (static_cast<u32>(block[15]) << 16)) | hibit;

        // h *= r mod 2^130-5
        u64 d0 = static_cast<u64>(h[0]) * r[0] + static_cast<u64>(h[1]) * s4 + static_cast<u64>(h[2]) * s3 + static_cast<u64>(h[3]) * s2 + static_cast<u64>(h[4]) * s1;
        u64 d1 = static_cast<u64>(h[0]) * r[1] + static_cast<u64>(h[1]) * r[0] + static_cast<u64>(h[2]) * s4 + static_cast<u64>(h[3]) * s3 + static_cast<u64>(h[4]) * s2;
        u64 d2 = static_cast<u64>(h[0]) * r[2] + static_cast<u64>(h[1]) * r[1] + static_cast<u64>(h[2]) * r[0] + static_cast<u64>(h[3]) * s4 + static_cast<u64>(h[4]) * s3;
        u64 d3 = static_cast<u64>(h[0]) * r[3] + static_cast<u64>(h[1]) * r[2] + static_cast<u64>(h[2]) * r[1] + static_cast<u64>(h[3]) * r[0] + static_cast<u64>(h[4]) * s4;
        u64 d4 = static_cast<u64>(h[0]) * r[4] + static_cast<u64>(h[1]) * r[3] + static_cast<u64>(h[2]) * r[2] + static_cast<u64>(h[3]) * r[1] + static_cast<u64>(h[4]) * r[0];

        // Carry propagation
        u32 c;
        c = static_cast<u32>(d0 >> 26); h[0] = static_cast<u32>(d0) & 0x3ffffff; d1 += c;
        c = static_cast<u32>(d1 >> 26); h[1] = static_cast<u32>(d1) & 0x3ffffff; d2 += c;
        c = static_cast<u32>(d2 >> 26); h[2] = static_cast<u32>(d2) & 0x3ffffff; d3 += c;
        c = static_cast<u32>(d3 >> 26); h[3] = static_cast<u32>(d3) & 0x3ffffff; d4 += c;
        c = static_cast<u32>(d4 >> 26); h[4] = static_cast<u32>(d4) & 0x3ffffff; h[0] += c * 5;
        c = h[0] >> 26; h[0] &= 0x3ffffff; h[1] += c;
    }

    // Final reduction
    u32 c = h[1] >> 26; h[1] &= 0x3ffffff; h[2] += c;
    c = h[2] >> 26; h[2] &= 0x3ffffff; h[3] += c;
    c = h[3] >> 26; h[3] &= 0x3ffffff; h[4] += c;
    c = h[4] >> 26; h[4] &= 0x3ffffff; h[0] += c * 5;
    c = h[0] >> 26; h[0] &= 0x3ffffff; h[1] += c;

    // Compute h + -p = h - (2^130 - 5)
    u32 g[5];
    g[0] = h[0] + 5; c = g[0] >> 26; g[0] &= 0x3ffffff;
    g[1] = h[1] + c; c = g[1] >> 26; g[1] &= 0x3ffffff;
    g[2] = h[2] + c; c = g[2] >> 26; g[2] &= 0x3ffffff;
    g[3] = h[3] + c; c = g[3] >> 26; g[3] &= 0x3ffffff;
    g[4] = h[4] + c - (1 << 26);

    // Select h or g based on whether g >= 2^130
    u32 mask = (g[4] >> 31) - 1; // 0xffffffff if g[4] >= 0 (no borrow), 0 otherwise
    for (int i = 0; i < 5; i++)
        h[i] = (h[i] & ~mask) | (g[i] & mask);

    // Convert to 32-bit limbs and add s
    u64 f;
    f = static_cast<u64>(h[0]) | (static_cast<u64>(h[1]) << 26);
    u32 h0 = static_cast<u32>(f) + pad[0]; u64 carry = (static_cast<u64>(h0) < pad[0]) ? 1 : 0;
    f = (f >> 32) | (static_cast<u64>(h[2]) << 20);
    u32 h1 = static_cast<u32>(f) + pad[1] + static_cast<u32>(carry);
    carry = (static_cast<u64>(h1) < static_cast<u64>(pad[1]) + carry) ? 1 : 0;
    f = (f >> 32) | (static_cast<u64>(h[3]) << 14);
    u32 h2 = static_cast<u32>(f) + pad[2] + static_cast<u32>(carry);
    carry = (static_cast<u64>(h2) < static_cast<u64>(pad[2]) + carry) ? 1 : 0;
    f = (f >> 32) | (static_cast<u64>(h[4]) << 8);
    u32 h3 = static_cast<u32>(f) + pad[3] + static_cast<u32>(carry);

    memcpy(tag + 0, &h0, 4);
    memcpy(tag + 4, &h1, 4);
    memcpy(tag + 8, &h2, 4);
    memcpy(tag + 12, &h3, 4);
}

static void poly1305_pad_len(u8* out, size_t len)
{
    memset(out, 0, 8);
    for (int i = 0; i < 8; i++)
        out[i] = static_cast<u8>(len >> (i * 8));
}

int rin_chacha20_poly1305_encrypt(
    u8 const* key, u8 const* nonce,
    u8 const* aad, size_t aad_len,
    u8 const* plaintext, size_t pt_len,
    u8* ciphertext, u8* tag)
{
    // Generate Poly1305 key
    u8 poly_key[64];
    u8 zeros[64] = {};
    chacha20_encrypt(key, nonce, 0, zeros, 64, poly_key);

    // Encrypt
    chacha20_encrypt(key, nonce, 1, plaintext, pt_len, ciphertext);

    // Build Poly1305 input: aad || pad(aad) || ciphertext || pad(ct) || len(aad) || len(ct)
    // Compute MAC incrementally for memory efficiency
    size_t aad_padded = (aad_len + 15) & ~static_cast<size_t>(15);
    size_t ct_padded = (pt_len + 15) & ~static_cast<size_t>(15);
    size_t mac_input_len = aad_padded + ct_padded + 16;

    // Allocate temp buffer for MAC computation
    u8* mac_input = new u8[mac_input_len];
    memset(mac_input, 0, mac_input_len);
    memcpy(mac_input, aad, aad_len);
    memcpy(mac_input + aad_padded, ciphertext, pt_len);
    poly1305_pad_len(mac_input + aad_padded + ct_padded, aad_len);
    poly1305_pad_len(mac_input + aad_padded + ct_padded + 8, pt_len);

    poly1305_mac(mac_input, mac_input_len, poly_key, poly_key + 16, tag);
    delete[] mac_input;
    return 0;
}

int rin_chacha20_poly1305_decrypt(
    u8 const* key, u8 const* nonce,
    u8 const* aad, size_t aad_len,
    u8 const* ciphertext, size_t ct_len,
    u8 const* tag, u8* plaintext)
{
    // Generate Poly1305 key
    u8 poly_key[64];
    u8 zeros[64] = {};
    chacha20_encrypt(key, nonce, 0, zeros, 64, poly_key);

    // Verify tag
    size_t aad_padded = (aad_len + 15) & ~static_cast<size_t>(15);
    size_t ct_padded = (ct_len + 15) & ~static_cast<size_t>(15);
    size_t mac_input_len = aad_padded + ct_padded + 16;

    u8* mac_input = new u8[mac_input_len];
    memset(mac_input, 0, mac_input_len);
    memcpy(mac_input, aad, aad_len);
    memcpy(mac_input + aad_padded, ciphertext, ct_len);
    poly1305_pad_len(mac_input + aad_padded + ct_padded, aad_len);
    poly1305_pad_len(mac_input + aad_padded + ct_padded + 8, ct_len);

    u8 computed_tag[16];
    poly1305_mac(mac_input, mac_input_len, poly_key, poly_key + 16, computed_tag);
    delete[] mac_input;

    // Constant-time compare
    u8 diff = 0;
    for (int i = 0; i < 16; i++)
        diff |= computed_tag[i] ^ tag[i];
    if (diff != 0)
        return -1;

    // Decrypt
    chacha20_encrypt(key, nonce, 1, ciphertext, ct_len, plaintext);
    return 0;
}

// ═══════════════════════════════════════════════════════════════════════════════
// PBKDF2 (RFC 8018) — using rintls HMAC
// ═══════════════════════════════════════════════════════════════════════════════

int rin_pbkdf2_hmac_sha256(
    u8 const* password, size_t pwd_len,
    u8 const* salt, size_t salt_len,
    u32 iterations,
    u8* output, size_t output_len)
{
    u32 block_num = 1;
    size_t offset = 0;

    while (offset < output_len) {
        // U1 = HMAC(password, salt || INT(block_num))
        u8 salt_block[4];
        salt_block[0] = static_cast<u8>(block_num >> 24);
        salt_block[1] = static_cast<u8>(block_num >> 16);
        salt_block[2] = static_cast<u8>(block_num >> 8);
        salt_block[3] = static_cast<u8>(block_num);

        hmac_sha256_ctx hmac_ctx;
        hmac_sha256_init(&hmac_ctx, password, pwd_len);
        hmac_sha256_update(&hmac_ctx, salt, salt_len);
        hmac_sha256_update(&hmac_ctx, salt_block, 4);

        u8 u[32], t[32];
        hmac_sha256_final(&hmac_ctx, u);
        memcpy(t, u, 32);

        for (u32 i = 1; i < iterations; i++) {
            hmac_sha256(&hmac_ctx, password, pwd_len);
            hmac_sha256_init(&hmac_ctx, password, pwd_len);
            hmac_sha256_update(&hmac_ctx, u, 32);
            hmac_sha256_final(&hmac_ctx, u);
            for (int j = 0; j < 32; j++)
                t[j] ^= u[j];
        }

        size_t copy_len = output_len - offset;
        if (copy_len > 32) copy_len = 32;
        memcpy(output + offset, t, copy_len);
        offset += copy_len;
        block_num++;
    }
    return 0;
}

int rin_pbkdf2_hmac_sha384(
    u8 const* password, size_t pwd_len,
    u8 const* salt, size_t salt_len,
    u32 iterations,
    u8* output, size_t output_len)
{
    u32 block_num = 1;
    size_t offset = 0;

    while (offset < output_len) {
        u8 salt_block[4];
        salt_block[0] = static_cast<u8>(block_num >> 24);
        salt_block[1] = static_cast<u8>(block_num >> 16);
        salt_block[2] = static_cast<u8>(block_num >> 8);
        salt_block[3] = static_cast<u8>(block_num);

        hmac_sha384_ctx hmac_ctx;
        hmac_sha384_init(&hmac_ctx, password, pwd_len);
        hmac_sha384_update(&hmac_ctx, salt, salt_len);
        hmac_sha384_update(&hmac_ctx, salt_block, 4);

        u8 u[48], t[48];
        hmac_sha384_final(&hmac_ctx, u);
        memcpy(t, u, 48);

        for (u32 i = 1; i < iterations; i++) {
            hmac_sha384_init(&hmac_ctx, password, pwd_len);
            hmac_sha384_update(&hmac_ctx, u, 48);
            hmac_sha384_final(&hmac_ctx, u);
            for (int j = 0; j < 48; j++)
                t[j] ^= u[j];
        }

        size_t copy_len = output_len - offset;
        if (copy_len > 48) copy_len = 48;
        memcpy(output + offset, t, copy_len);
        offset += copy_len;
        block_num++;
    }
    return 0;
}

// ═══════════════════════════════════════════════════════════════════════════════
// AES-CBC (using rintls block cipher)
// ═══════════════════════════════════════════════════════════════════════════════

int rin_aes_cbc_encrypt(
    u8 const* key, int key_size,
    u8 const* iv,
    u8 const* plaintext, size_t pt_len,
    u8* ciphertext, size_t* ct_len,
    int pkcs7_pad)
{
    aes_ctx ctx;
    if (aes_init(&ctx, key, key_size) != 0) return -1;

    u8 prev[16];
    memcpy(prev, iv, 16);

    size_t full_blocks = pt_len / 16;
    size_t remainder = pt_len % 16;

    for (size_t i = 0; i < full_blocks; i++) {
        u8 block[16];
        for (int j = 0; j < 16; j++)
            block[j] = plaintext[i * 16 + j] ^ prev[j];
        aes_encrypt_block(&ctx, block, ciphertext + i * 16);
        memcpy(prev, ciphertext + i * 16, 16);
    }

    if (pkcs7_pad) {
        u8 pad_byte = static_cast<u8>(16 - remainder);
        u8 block[16];
        if (remainder > 0)
            memcpy(block, plaintext + full_blocks * 16, remainder);
        memset(block + remainder, pad_byte, 16 - remainder);
        for (int j = 0; j < 16; j++)
            block[j] ^= prev[j];
        aes_encrypt_block(&ctx, block, ciphertext + full_blocks * 16);
        *ct_len = (full_blocks + 1) * 16;
    } else {
        *ct_len = full_blocks * 16;
    }
    return 0;
}

int rin_aes_cbc_decrypt(
    u8 const* key, int key_size,
    u8 const* iv,
    u8 const* ciphertext, size_t ct_len,
    u8* plaintext, size_t* pt_len,
    int pkcs7_pad)
{
    if (ct_len % 16 != 0) return -1;

    aes_ctx ctx;
    if (aes_init(&ctx, key, key_size) != 0) return -1;

    u8 prev[16];
    memcpy(prev, iv, 16);

    size_t blocks = ct_len / 16;
    for (size_t i = 0; i < blocks; i++) {
        u8 decrypted[16];
        aes_decrypt_block(&ctx, ciphertext + i * 16, decrypted);
        for (int j = 0; j < 16; j++)
            plaintext[i * 16 + j] = decrypted[j] ^ prev[j];
        memcpy(prev, ciphertext + i * 16, 16);
    }

    if (pkcs7_pad && blocks > 0) {
        u8 pad_byte = plaintext[ct_len - 1];
        if (pad_byte == 0 || pad_byte > 16) return -1;
        // Constant-time padding validation
        u8 bad = 0;
        for (size_t i = ct_len - pad_byte; i < ct_len; i++)
            bad |= plaintext[i] ^ pad_byte;
        if (bad != 0) return -1;
        *pt_len = ct_len - pad_byte;
    } else {
        *pt_len = ct_len;
    }
    return 0;
}

// ═══════════════════════════════════════════════════════════════════════════════
// AES-CTR (using rintls block cipher)
// ═══════════════════════════════════════════════════════════════════════════════

int rin_aes_ctr_crypt(
    u8 const* key, int key_size,
    u8 const* iv,
    u8 const* input, size_t len,
    u8* output)
{
    aes_ctx ctx;
    if (aes_init(&ctx, key, key_size) != 0) return -1;

    u8 counter[16], keystream[16];
    memcpy(counter, iv, 16);

    for (size_t off = 0; off < len; off += 16) {
        aes_encrypt_block(&ctx, counter, keystream);

        size_t chunk = len - off;
        if (chunk > 16) chunk = 16;
        for (size_t i = 0; i < chunk; i++)
            output[off + i] = input[off + i] ^ keystream[i];

        // Increment counter (big-endian, last 4 bytes)
        for (int i = 15; i >= 0; i--) {
            if (++counter[i] != 0) break;
        }
    }
    return 0;
}

// ═══════════════════════════════════════════════════════════════════════════════
// AES-KW (RFC 3394 Key Wrap)
// ═══════════════════════════════════════════════════════════════════════════════

static constexpr u8 aes_kw_default_iv[8] = { 0xA6, 0xA6, 0xA6, 0xA6, 0xA6, 0xA6, 0xA6, 0xA6 };

int rin_aes_kw_wrap(
    u8 const* key, int key_size,
    u8 const* plaintext, size_t pt_len,
    u8* ciphertext, size_t* ct_len)
{
    if (pt_len % 8 != 0 || pt_len < 16) return -1;
    size_t n = pt_len / 8;

    aes_ctx ctx;
    if (aes_init(&ctx, key, key_size) != 0) return -1;

    u8 a[8];
    memcpy(a, aes_kw_default_iv, 8);
    memcpy(ciphertext + 8, plaintext, pt_len);

    u8 block[16], enc[16];
    for (int j = 0; j < 6; j++) {
        for (size_t i = 0; i < n; i++) {
            memcpy(block, a, 8);
            memcpy(block + 8, ciphertext + 8 + i * 8, 8);
            aes_encrypt_block(&ctx, block, enc);
            u64 t_val = static_cast<u64>(n * j + i + 1);
            for (int k = 7; k >= 0; k--) {
                enc[k] ^= static_cast<u8>(t_val);
                t_val >>= 8;
            }
            memcpy(a, enc, 8);
            memcpy(ciphertext + 8 + i * 8, enc + 8, 8);
        }
    }
    memcpy(ciphertext, a, 8);
    *ct_len = pt_len + 8;
    return 0;
}

int rin_aes_kw_unwrap(
    u8 const* key, int key_size,
    u8 const* ciphertext, size_t ct_len,
    u8* plaintext, size_t* pt_len)
{
    if (ct_len % 8 != 0 || ct_len < 24) return -1;
    size_t n = (ct_len / 8) - 1;

    aes_ctx ctx;
    if (aes_init(&ctx, key, key_size) != 0) return -1;

    u8 a[8];
    memcpy(a, ciphertext, 8);
    memcpy(plaintext, ciphertext + 8, ct_len - 8);

    u8 block[16], dec[16];
    for (int j = 5; j >= 0; j--) {
        for (int i = static_cast<int>(n) - 1; i >= 0; i--) {
            u64 t_val = static_cast<u64>(n * j + i + 1);
            u8 a_xor[8];
            memcpy(a_xor, a, 8);
            for (int k = 7; k >= 0; k--) {
                a_xor[k] ^= static_cast<u8>(t_val);
                t_val >>= 8;
            }
            memcpy(block, a_xor, 8);
            memcpy(block + 8, plaintext + i * 8, 8);
            aes_decrypt_block(&ctx, block, dec);
            memcpy(a, dec, 8);
            memcpy(plaintext + i * 8, dec + 8, 8);
        }
    }

    // Verify IV
    u8 diff = 0;
    for (int i = 0; i < 8; i++)
        diff |= a[i] ^ aes_kw_default_iv[i];
    if (diff != 0) return -1;

    *pt_len = ct_len - 8;
    return 0;
}

#endif // AK_OS_RINOS
