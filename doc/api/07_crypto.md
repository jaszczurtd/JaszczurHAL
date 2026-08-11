# Cryptography - `hal_crypto`

> **Part of [JaszczurHAL API Reference](../JaszczurHAL_API.md)**

## `hal_crypto` - Base64, MD5, SHA-256 / HMAC-SHA256, ChaCha20, ChaCha20-Poly1305  *(opt-in - `HAL_ENABLE_CRYPTO`)*

This module is **opt-in**. Define `HAL_ENABLE_CRYPTO` in
`hal_project_config.h` (or via `-D`) to compile it in. Without the
flag the header below expands to nothing, `hal_crypto.cpp` becomes an
empty translation unit, and any caller of these helpers fails at link
time with an undefined-reference error.

```c
#include <hal/security/hal_crypto.h>

// ChaCha20 / AEAD constants
#define HAL_CHACHA20_KEY_BYTES            32u
#define HAL_CHACHA20_NONCE_BYTES          12u
#define HAL_CHACHA20_BLOCK_BYTES          64u
#define HAL_CHACHA20_POLY1305_TAG_BYTES   16u

// MD5 constants
#define HAL_MD5_DIGEST_BYTES              16u
#define HAL_MD5_HEX_BUF_SIZE              33u  // 32 hex chars + NUL

// SHA-256 / HMAC-SHA256 constants
#define HAL_SHA256_DIGEST_BYTES           32u
#define HAL_SHA256_HEX_BUF_SIZE           65u  // 64 hex chars + NUL
#define HAL_HMAC_SHA256_BLOCK_BYTES       64u

// Base64 helpers
size_t hal_base64_encoded_len(size_t input_len);
size_t hal_base64_decoded_max_len(size_t input_len);
bool hal_base64_encode(const uint8_t *input, size_t input_len,
                       char *output, size_t out_size, size_t *out_len);
bool hal_base64_decode(const char *input, size_t input_len,
                       uint8_t *output, size_t out_size, size_t *out_len);

// MD5 helpers
bool hal_md5(const uint8_t *input, size_t input_len,
             uint8_t out_digest[HAL_MD5_DIGEST_BYTES]);
bool hal_md5_hex(const uint8_t *input, size_t input_len,
                 char *output, size_t out_size);

// ChaCha20 stream helpers (IETF RFC 8439)
bool hal_chacha20_block(const uint8_t key[HAL_CHACHA20_KEY_BYTES],
                        uint32_t counter,
                        const uint8_t nonce[HAL_CHACHA20_NONCE_BYTES],
                        uint8_t out_block[HAL_CHACHA20_BLOCK_BYTES]);
bool hal_chacha20_xor(const uint8_t key[HAL_CHACHA20_KEY_BYTES],
                      uint32_t counter,
                      const uint8_t nonce[HAL_CHACHA20_NONCE_BYTES],
                      const uint8_t *input,
                      size_t input_len,
                      uint8_t *output);

// ChaCha20-Poly1305 AEAD (RFC 8439)
bool hal_chacha20_poly1305_encrypt(
    const uint8_t key[HAL_CHACHA20_KEY_BYTES],
    const uint8_t nonce[HAL_CHACHA20_NONCE_BYTES],
    const uint8_t *aad, size_t aad_len,
    const uint8_t *plaintext, size_t text_len,
    uint8_t *ciphertext,
    uint8_t tag[HAL_CHACHA20_POLY1305_TAG_BYTES]);

bool hal_chacha20_poly1305_decrypt(
    const uint8_t key[HAL_CHACHA20_KEY_BYTES],
    const uint8_t nonce[HAL_CHACHA20_NONCE_BYTES],
    const uint8_t *aad, size_t aad_len,
    const uint8_t *ciphertext, size_t text_len,
    const uint8_t tag[HAL_CHACHA20_POLY1305_TAG_BYTES],
    uint8_t *plaintext);

// SHA-256 / HMAC-SHA256 (FIPS 180-4 + RFC 2104)
typedef struct {
    uint32_t state[8];
    uint64_t bit_count;
    uint8_t buffer[HAL_HMAC_SHA256_BLOCK_BYTES];
    size_t buffer_len;
    bool initialized;
} hal_sha256_context_t;

hal_status_t hal_sha256_init_ex(hal_sha256_context_t *context);
hal_status_t hal_sha256_update_ex(hal_sha256_context_t *context,
                                  const uint8_t *input, size_t input_len);
hal_status_t hal_sha256_final_ex(
    hal_sha256_context_t *context,
    uint8_t out_digest[HAL_SHA256_DIGEST_BYTES]);

bool hal_sha256(const uint8_t *input, size_t input_len,
                uint8_t out_digest[HAL_SHA256_DIGEST_BYTES]);
bool hal_sha256_hex(const uint8_t *input, size_t input_len,
                    char *output, size_t out_size);
bool hal_hmac_sha256(const uint8_t *key, size_t key_len,
                     const uint8_t *message, size_t message_len,
                     uint8_t out_mac[HAL_SHA256_DIGEST_BYTES]);
bool hal_hmac_sha256_hex(const uint8_t *key, size_t key_len,
                         const uint8_t *message, size_t message_len,
                         char *output, size_t out_size);
```

**Behavior notes:**
- Base64 is strict RFC 4648 (`A-Z a-z 0-9 + /` and `=` padding), no whitespace tolerance.
- `hal_md5_hex(...)` and `hal_sha256_hex(...)` / `hal_hmac_sha256_hex(...)` output lowercase hex.
- `hal_chacha20_xor(...)` supports in-place processing (`output == input`).
- `hal_chacha20_poly1305_decrypt(...)` verifies tag before decryption and returns `false` on mismatch.
- ChaCha20 / Poly1305 paths are delegated to the shared `hal/network/wireguard/core/crypto` backend so HAL and WireGuard use the same source-of-truth primitive implementation.
- For ChaCha20/AEAD, nonce must be unique per key; nonce reuse breaks security.
- `hal_hmac_sha256(...)` follows RFC 2104 - keys longer than the block size (64 B) are pre-hashed; shorter keys are zero-padded.
- The `_ex` SHA-256 API supports bounded-memory streaming. `final_ex`
  invalidates the context; update/final calls before initialization or after
  finalization return `HAL_ESTATE`.
- SHA-256 / HMAC-SHA256 are validated against FIPS 180-2 and RFC 4231 vectors and stay bit-stable with companion host-side mirror implementations (for example `sc_sha256.c`).

**Security note:** MD5 is provided for legacy checksum compatibility and non-security fingerprints. Do not use MD5 where collision resistance is required. Prefer SHA-256 / HMAC-SHA256 for any new integrity or authentication need.

**Thread safety:** Stateless implementation; safe for multicore use when caller-provided buffers do not alias across threads unexpectedly.

---

## Examples

**Example: Base64 encoding and decoding**
```c
#include <hal/security/hal_crypto.h>
#include <string.h>

void example_base64(void) {
    const char *plain = "Hello, World!";
    size_t plain_len = strlen(plain);

    // Encode: calculate buffer size and encode
    size_t encoded_max = hal_base64_encoded_len(plain_len);
    char encoded[64] = {};
    size_t encoded_len = 0;

    if (hal_base64_encode((const uint8_t *)plain, plain_len,
                          encoded, sizeof(encoded), &encoded_len)) {
        hal_deb("Base64 encoded: %s (len=%zu)", encoded, encoded_len);
    }

    // Decode: convert back to plaintext
    uint8_t decoded[64] = {};
    size_t decoded_len = 0;

    if (hal_base64_decode(encoded, encoded_len,
                          decoded, sizeof(decoded), &decoded_len)) {
        hal_deb("Base64 decoded: %s (len=%zu)", (const char *)decoded, decoded_len);
    }
}
```

**Example: MD5 hash (legacy checksum)**
```c
#include <hal/security/hal_crypto.h>
#include <string.h>

void example_md5(void) {
    const char *message = "Hello, World!";
    size_t msg_len = strlen(message);

    // Compute MD5 as raw bytes
    uint8_t digest[HAL_MD5_DIGEST_BYTES];
    if (hal_md5((const uint8_t *)message, msg_len, digest)) {
        hal_deb("MD5 raw digest computed (%d bytes)", HAL_MD5_DIGEST_BYTES);
    }

    // Compute MD5 as hex string (more readable)
    char hex_output[HAL_MD5_HEX_BUF_SIZE];
    if (hal_md5_hex((const uint8_t *)message, msg_len, hex_output, sizeof(hex_output))) {
        hal_deb("MD5 hex: %s", hex_output);
        // Output: "65a8e27d8d55e25146f23e4c59f6ff9e"
    }
}
```

**Example: SHA-256 hash (preferred for security)**
```c
#include <hal/security/hal_crypto.h>
#include <string.h>

void example_sha256(void) {
    const char *message = "Hello, World!";
    size_t msg_len = strlen(message);

    // Compute SHA-256 as hex string
    char hex_output[HAL_SHA256_HEX_BUF_SIZE];
    if (hal_sha256_hex((const uint8_t *)message, msg_len,
                       hex_output, sizeof(hex_output))) {
        hal_deb("SHA-256 hex: %s", hex_output);
        // Output: "dffd6021bb2bd5b0af676290809ec3a53191dd81c7f70a4b28688a362182986f"
    }

    // Compute SHA-256 as raw bytes for further processing
    uint8_t digest[HAL_SHA256_DIGEST_BYTES];
    if (hal_sha256((const uint8_t *)message, msg_len, digest)) {
        hal_deb("SHA-256 raw digest computed (%d bytes)", HAL_SHA256_DIGEST_BYTES);
    }
}
```

**Example: HMAC-SHA256 (message authentication)**
```c
#include <hal/security/hal_crypto.h>
#include <string.h>

void example_hmac_sha256(void) {
    const char *key = "secret_key_12345";
    const char *message = "Hello, World!";

    // Compute HMAC-SHA256 as hex string
    char hmac_hex[HAL_SHA256_HEX_BUF_SIZE];
    if (hal_hmac_sha256_hex((const uint8_t *)key, strlen(key),
                            (const uint8_t *)message, strlen(message),
                            hmac_hex, sizeof(hmac_hex))) {
        hal_deb("HMAC-SHA256: %s", hmac_hex);
    }

    // Compute HMAC-SHA256 as raw bytes
    uint8_t hmac_digest[HAL_SHA256_DIGEST_BYTES];
    if (hal_hmac_sha256((const uint8_t *)key, strlen(key),
                        (const uint8_t *)message, strlen(message),
                        hmac_digest)) {
        hal_deb("HMAC-SHA256 raw computed (%d bytes)", HAL_SHA256_DIGEST_BYTES);
    }
}
```

**Example: ChaCha20 stream encryption**
```c
#include <hal/security/hal_crypto.h>
#include <string.h>

void example_chacha20(void) {
    // Key must be exactly 32 bytes, nonce exactly 12 bytes (RFC 8439)
    const uint8_t key[HAL_CHACHA20_KEY_BYTES] = {
        0x00, 0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07,
        0x08, 0x09, 0x0a, 0x0b, 0x0c, 0x0d, 0x0e, 0x0f,
        0x10, 0x11, 0x12, 0x13, 0x14, 0x15, 0x16, 0x17,
        0x18, 0x19, 0x1a, 0x1b, 0x1c, 0x1d, 0x1e, 0x1f
    };
    const uint8_t nonce[HAL_CHACHA20_NONCE_BYTES] = {
        0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x4a,
        0x00, 0x00, 0x00, 0x00
    };

    const char *plaintext = "Hello, World!";
    uint8_t ciphertext[256];
    uint8_t decrypted[256];

    // Encrypt: counter starts at 1, stream xor's with plaintext
    if (hal_chacha20_xor(key, 1, nonce,
                         (const uint8_t *)plaintext, strlen(plaintext),
                         ciphertext)) {
        hal_deb("ChaCha20 encrypted %zu bytes", strlen(plaintext));
    }

    // Decrypt: same operation (ChaCha20 is symmetric XOR stream)
    if (hal_chacha20_xor(key, 1, nonce,
                         ciphertext, strlen(plaintext),
                         decrypted)) {
        hal_deb("ChaCha20 decrypted: %s", (const char *)decrypted);
    }
}
```

**Example: ChaCha20-Poly1305 AEAD (authenticated encryption)**
```c
#include <hal/security/hal_crypto.h>
#include <string.h>

void example_chacha20_poly1305(void) {
    const uint8_t key[HAL_CHACHA20_KEY_BYTES] = {
        0x80, 0x81, 0x82, 0x83, 0x84, 0x85, 0x86, 0x87,
        0x88, 0x89, 0x8a, 0x8b, 0x8c, 0x8d, 0x8e, 0x8f,
        0x90, 0x91, 0x92, 0x93, 0x94, 0x95, 0x96, 0x97,
        0x98, 0x99, 0x9a, 0x9b, 0x9c, 0x9d, 0x9e, 0x9f
    };
    const uint8_t nonce[HAL_CHACHA20_NONCE_BYTES] = {
        0x07, 0x00, 0x00, 0x00, 0x40, 0x41, 0x42, 0x43,
        0x44, 0x45, 0x46, 0x47
    };

    const char *plaintext = "Hello, World!";
    const char *aad = "Additional authenticated data";  // optional
    uint8_t ciphertext[256];
    uint8_t tag[HAL_CHACHA20_POLY1305_TAG_BYTES];
    uint8_t decrypted[256];
    uint8_t computed_tag[HAL_CHACHA20_POLY1305_TAG_BYTES];

    // Encrypt with authentication
    if (hal_chacha20_poly1305_encrypt(
        key, nonce,
        (const uint8_t *)aad, strlen(aad),
        (const uint8_t *)plaintext, strlen(plaintext),
        ciphertext, tag)) {
        hal_deb("ChaCha20-Poly1305 encrypted + tagged");
    }

    // Decrypt with verification: tag is verified before decryption
    if (hal_chacha20_poly1305_decrypt(
        key, nonce,
        (const uint8_t *)aad, strlen(aad),
        ciphertext, strlen(plaintext),
        tag,
        decrypted)) {
        hal_deb("ChaCha20-Poly1305 decrypted + verified: %s", (const char *)decrypted);
    } else {
        hal_derr("Authentication failed - tag mismatch!");
    }
}
```

---
---

*Next: [Sync and serial](08_sync_serial.md)*
