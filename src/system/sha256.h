// Compact SHA-256. Self-contained — no mbedtls dependency.
//
// The OTA path uses this to verify a downloaded image against an
// expected digest passed on the command line. Not authenticated (no
// HMAC/signature) — anyone who can serve the download URL controls the
// image. For real deployments layer an out-of-band signature on top.
#ifndef PICOBLE_SYSTEM_SHA256_H
#define PICOBLE_SYSTEM_SHA256_H

#include <stddef.h>
#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

#define SHA256_DIGEST_LEN 32

typedef struct {
    uint32_t state[8];
    uint64_t total_bits;
    uint8_t  buf[64];
    size_t   buf_len;
} sha256_ctx_t;

void sha256_init(sha256_ctx_t *ctx);
void sha256_update(sha256_ctx_t *ctx, const void *data, size_t len);
void sha256_final(sha256_ctx_t *ctx, uint8_t out[SHA256_DIGEST_LEN]);

// One-shot convenience.
void sha256(const void *data, size_t len, uint8_t out[SHA256_DIGEST_LEN]);

// Parse a hex digest string (case-insensitive). Returns true if exactly
// 64 hex characters translated cleanly into 32 bytes.
bool sha256_parse_hex(const char *hex, uint8_t out[SHA256_DIGEST_LEN]);

// Constant-time compare of two digests.
bool sha256_equal(const uint8_t a[SHA256_DIGEST_LEN],
                  const uint8_t b[SHA256_DIGEST_LEN]);

// Format 32 bytes as 64-char lowercase hex + NUL. buf must be >= 65 bytes.
void sha256_format_hex(const uint8_t in[SHA256_DIGEST_LEN], char *buf);

#ifdef __cplusplus
}
#endif

#endif  // PICOBLE_SYSTEM_SHA256_H
