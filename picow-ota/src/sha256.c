// FIPS 180-4 SHA-256. Public-domain style transcription — buffered
// streaming API sized for our OTA workload (chunks up to a few KB).
#include "pico_ota/sha256.h"

#include <string.h>

// Round constants (first 32 bits of the fractional parts of the cube
// roots of the first 64 primes).
static const uint32_t K[64] = {
    0x428a2f98, 0x71374491, 0xb5c0fbcf, 0xe9b5dba5, 0x3956c25b, 0x59f111f1, 0x923f82a4, 0xab1c5ed5,
    0xd807aa98, 0x12835b01, 0x243185be, 0x550c7dc3, 0x72be5d74, 0x80deb1fe, 0x9bdc06a7, 0xc19bf174,
    0xe49b69c1, 0xefbe4786, 0x0fc19dc6, 0x240ca1cc, 0x2de92c6f, 0x4a7484aa, 0x5cb0a9dc, 0x76f988da,
    0x983e5152, 0xa831c66d, 0xb00327c8, 0xbf597fc7, 0xc6e00bf3, 0xd5a79147, 0x06ca6351, 0x14292967,
    0x27b70a85, 0x2e1b2138, 0x4d2c6dfc, 0x53380d13, 0x650a7354, 0x766a0abb, 0x81c2c92e, 0x92722c85,
    0xa2bfe8a1, 0xa81a664b, 0xc24b8b70, 0xc76c51a3, 0xd192e819, 0xd6990624, 0xf40e3585, 0x106aa070,
    0x19a4c116, 0x1e376c08, 0x2748774c, 0x34b0bcb5, 0x391c0cb3, 0x4ed8aa4a, 0x5b9cca4f, 0x682e6ff3,
    0x748f82ee, 0x78a5636f, 0x84c87814, 0x8cc70208, 0x90befffa, 0xa4506ceb, 0xbef9a3f7, 0xc67178f2,
};

static inline uint32_t rotr(uint32_t x, unsigned n) {
    return (x >> n) | (x << (32 - n));
}

static void transform(uint32_t state[8], const uint8_t block[64]) {
    uint32_t w[64];

    // Load big-endian words from the block.
    for (int i = 0; i < 16; i++) {
        w[i] = ((uint32_t)block[i * 4]     << 24) |
               ((uint32_t)block[i * 4 + 1] << 16) |
               ((uint32_t)block[i * 4 + 2] <<  8) |
               ((uint32_t)block[i * 4 + 3]);
    }
    for (int i = 16; i < 64; i++) {
        uint32_t s0 = rotr(w[i - 15], 7) ^ rotr(w[i - 15], 18) ^ (w[i - 15] >> 3);
        uint32_t s1 = rotr(w[i - 2], 17) ^ rotr(w[i - 2], 19) ^ (w[i - 2] >> 10);
        w[i] = w[i - 16] + s0 + w[i - 7] + s1;
    }

    uint32_t a = state[0], b = state[1], c = state[2], d = state[3];
    uint32_t e = state[4], f = state[5], g = state[6], h = state[7];

    for (int i = 0; i < 64; i++) {
        uint32_t S1 = rotr(e, 6) ^ rotr(e, 11) ^ rotr(e, 25);
        uint32_t ch = (e & f) ^ ((~e) & g);
        uint32_t t1 = h + S1 + ch + K[i] + w[i];
        uint32_t S0 = rotr(a, 2) ^ rotr(a, 13) ^ rotr(a, 22);
        uint32_t mj = (a & b) ^ (a & c) ^ (b & c);
        uint32_t t2 = S0 + mj;
        h = g; g = f; f = e; e = d + t1;
        d = c; c = b; b = a; a = t1 + t2;
    }

    state[0] += a; state[1] += b; state[2] += c; state[3] += d;
    state[4] += e; state[5] += f; state[6] += g; state[7] += h;
}

void sha256_init(sha256_ctx_t *ctx) {
    static const uint32_t H0[8] = {
        0x6a09e667, 0xbb67ae85, 0x3c6ef372, 0xa54ff53a,
        0x510e527f, 0x9b05688c, 0x1f83d9ab, 0x5be0cd19,
    };
    memcpy(ctx->state, H0, sizeof(H0));
    ctx->total_bits = 0;
    ctx->buf_len = 0;
}

void sha256_update(sha256_ctx_t *ctx, const void *data, size_t len) {
    const uint8_t *p = data;
    ctx->total_bits += (uint64_t)len * 8u;

    // Top up the partial buffer first.
    if (ctx->buf_len) {
        size_t need = 64 - ctx->buf_len;
        size_t take = len < need ? len : need;
        memcpy(ctx->buf + ctx->buf_len, p, take);
        ctx->buf_len += take;
        p += take; len -= take;
        if (ctx->buf_len == 64) {
            transform(ctx->state, ctx->buf);
            ctx->buf_len = 0;
        }
    }

    // Bulk full blocks straight from input.
    while (len >= 64) {
        transform(ctx->state, p);
        p += 64; len -= 64;
    }

    // Stash the tail.
    if (len) {
        memcpy(ctx->buf, p, len);
        ctx->buf_len = len;
    }
}

void sha256_final(sha256_ctx_t *ctx, uint8_t out[SHA256_DIGEST_LEN]) {
    // Append 0x80 then 0-pad so the length lives in the last 8 bytes.
    ctx->buf[ctx->buf_len++] = 0x80;
    if (ctx->buf_len > 56) {
        memset(ctx->buf + ctx->buf_len, 0, 64 - ctx->buf_len);
        transform(ctx->state, ctx->buf);
        ctx->buf_len = 0;
    }
    memset(ctx->buf + ctx->buf_len, 0, 56 - ctx->buf_len);

    // Big-endian length in bits.
    uint64_t bits = ctx->total_bits;
    for (int i = 0; i < 8; i++) {
        ctx->buf[56 + i] = (uint8_t)(bits >> (56 - i * 8));
    }
    transform(ctx->state, ctx->buf);

    // Big-endian state → digest.
    for (int i = 0; i < 8; i++) {
        out[i * 4]     = (uint8_t)(ctx->state[i] >> 24);
        out[i * 4 + 1] = (uint8_t)(ctx->state[i] >> 16);
        out[i * 4 + 2] = (uint8_t)(ctx->state[i] >>  8);
        out[i * 4 + 3] = (uint8_t)(ctx->state[i]);
    }
}

void sha256(const void *data, size_t len, uint8_t out[SHA256_DIGEST_LEN]) {
    sha256_ctx_t ctx;
    sha256_init(&ctx);
    sha256_update(&ctx, data, len);
    sha256_final(&ctx, out);
}

// ---- hex helpers ------------------------------------------------------------

static int hex_nibble(char c) {
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return 10 + (c - 'a');
    if (c >= 'A' && c <= 'F') return 10 + (c - 'A');
    return -1;
}

bool sha256_parse_hex(const char *hex, uint8_t out[SHA256_DIGEST_LEN]) {
    if (!hex) return false;
    for (int i = 0; i < SHA256_DIGEST_LEN; i++) {
        int hi = hex_nibble(hex[i * 2]);
        int lo = hex_nibble(hex[i * 2 + 1]);
        if (hi < 0 || lo < 0) return false;
        out[i] = (uint8_t)((hi << 4) | lo);
    }
    return hex[64] == '\0';
}

bool sha256_equal(const uint8_t a[SHA256_DIGEST_LEN],
                  const uint8_t b[SHA256_DIGEST_LEN]) {
    // Constant-time — protects against timing side channels on the
    // comparison. Cheap here so we may as well.
    uint8_t diff = 0;
    for (int i = 0; i < SHA256_DIGEST_LEN; i++) diff |= a[i] ^ b[i];
    return diff == 0;
}

void sha256_format_hex(const uint8_t in[SHA256_DIGEST_LEN], char *buf) {
    static const char lut[] = "0123456789abcdef";
    for (int i = 0; i < SHA256_DIGEST_LEN; i++) {
        buf[i * 2]     = lut[(in[i] >> 4) & 0xF];
        buf[i * 2 + 1] = lut[in[i] & 0xF];
    }
    buf[SHA256_DIGEST_LEN * 2] = '\0';
}
