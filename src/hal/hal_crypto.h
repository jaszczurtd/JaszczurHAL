#pragma once

/**
 * @file hal_crypto.h
 * @brief Backend-agnostic Base64, MD5, SHA-256 / HMAC-SHA256 and ChaCha20 helpers.
 *
 * Implements RFC 4648 "base64" alphabet (`A-Z`, `a-z`, `0-9`, `+`, `/`)
 * with `=` padding, RFC 1321 MD5 hashing, FIPS 180-4 SHA-256 with the
 * RFC 2104 HMAC construction, and IETF ChaCha20 (RFC 8439).
 *
 * The whole module is opt-in: define @c HAL_ENABLE_CRYPTO in
 * `hal_project_config.h` (or via `-D`) to compile it in. Without that
 * flag this header is empty and `hal_crypto.cpp` produces an empty TU,
 * so projects that never touch crypto pay zero code/RAM cost.
 */

#include "hal_config.h"

#ifdef HAL_ENABLE_CRYPTO

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/** @brief ChaCha20 key length in bytes (256 bits). */
#define HAL_CHACHA20_KEY_BYTES 32u

/** @brief ChaCha20 IETF nonce length in bytes (96 bits). */
#define HAL_CHACHA20_NONCE_BYTES 12u

/** @brief ChaCha20 block size in bytes. */
#define HAL_CHACHA20_BLOCK_BYTES 64u

/** @brief ChaCha20-Poly1305 authentication tag length in bytes. */
#define HAL_CHACHA20_POLY1305_TAG_BYTES 16u

/**
 * @brief Generate one ChaCha20 keystream block.
 *
 * Uses IETF ChaCha20 state layout (`constants || key || counter || nonce`).
 *
 * @param key      32-byte key.
 * @param counter  Initial block counter.
 * @param nonce    12-byte nonce.
 * @param out_block Output buffer for 64-byte keystream block.
 * @return true on success, false on invalid args.
 */
bool hal_chacha20_block(const uint8_t key[HAL_CHACHA20_KEY_BYTES],
                        uint32_t counter,
                        const uint8_t nonce[HAL_CHACHA20_NONCE_BYTES],
                        uint8_t out_block[HAL_CHACHA20_BLOCK_BYTES]);

/**
 * @brief Encrypt/decrypt data using ChaCha20 stream XOR.
 *
 * Encryption and decryption are identical operations: `output = input XOR stream`.
 * In-place operation is supported (`output == input`).
 *
 * @param key             32-byte key.
 * @param counter         Initial block counter.
 * @param nonce           12-byte nonce.
 * @param input           Input buffer (may be NULL only when @p input_len is 0).
 * @param input_len       Number of bytes to process.
 * @param output          Output buffer (must have at least @p input_len bytes).
 * @return true on success, false on invalid args.
 */
bool hal_chacha20_xor(const uint8_t key[HAL_CHACHA20_KEY_BYTES],
                      uint32_t counter,
                      const uint8_t nonce[HAL_CHACHA20_NONCE_BYTES],
                      const uint8_t *input,
                      size_t input_len,
                      uint8_t *output);

/**
 * @brief Encrypt data with ChaCha20-Poly1305 (AEAD, RFC 8439).
 *
 * Produces ciphertext and a 16-byte authentication tag.
 *
 * @param key         32-byte key.
 * @param nonce       12-byte nonce (must be unique per key).
 * @param aad         Associated data pointer (may be NULL only when @p aad_len is 0).
 * @param aad_len     Associated data length in bytes.
 * @param plaintext   Plaintext pointer (may be NULL only when @p text_len is 0).
 * @param text_len    Plaintext length in bytes.
 * @param ciphertext  Output ciphertext buffer (at least @p text_len bytes).
 *                    May be NULL only when @p text_len is 0.
 * @param tag         Output tag buffer of @ref HAL_CHACHA20_POLY1305_TAG_BYTES bytes.
 * @return true on success, false on invalid args or counter overflow.
 */
bool hal_chacha20_poly1305_encrypt(
    const uint8_t key[HAL_CHACHA20_KEY_BYTES],
    const uint8_t nonce[HAL_CHACHA20_NONCE_BYTES],
    const uint8_t *aad,
    size_t aad_len,
    const uint8_t *plaintext,
    size_t text_len,
    uint8_t *ciphertext,
    uint8_t tag[HAL_CHACHA20_POLY1305_TAG_BYTES]);

/**
 * @brief Decrypt and authenticate data with ChaCha20-Poly1305 (AEAD, RFC 8439).
 *
 * The tag is verified before decryption. If authentication fails, the
 * function returns false and plaintext output is left unchanged.
 *
 * @param key         32-byte key.
 * @param nonce       12-byte nonce used during encryption.
 * @param aad         Associated data pointer (may be NULL only when @p aad_len is 0).
 * @param aad_len     Associated data length in bytes.
 * @param ciphertext  Ciphertext pointer (may be NULL only when @p text_len is 0).
 * @param text_len    Ciphertext/plaintext length in bytes.
 * @param tag         Input tag buffer of @ref HAL_CHACHA20_POLY1305_TAG_BYTES bytes.
 * @param plaintext   Output plaintext buffer (at least @p text_len bytes).
 *                    May be NULL only when @p text_len is 0.
 * @return true on success, false on invalid args, tag mismatch, or overflow.
 */
bool hal_chacha20_poly1305_decrypt(
    const uint8_t key[HAL_CHACHA20_KEY_BYTES],
    const uint8_t nonce[HAL_CHACHA20_NONCE_BYTES],
    const uint8_t *aad,
    size_t aad_len,
    const uint8_t *ciphertext,
    size_t text_len,
    const uint8_t tag[HAL_CHACHA20_POLY1305_TAG_BYTES],
    uint8_t *plaintext);

/** @brief MD5 digest length in bytes. */
#define HAL_MD5_DIGEST_BYTES 16u

/** @brief Required buffer size for MD5 lowercase hex string (`32 + NUL`). */
#define HAL_MD5_HEX_BUF_SIZE 33u

/**
 * @brief Compute MD5 digest of input data.
 *
 * @param input      Input data pointer (may be NULL only when @p input_len=0).
 * @param input_len  Input size in bytes.
 * @param out_digest Output buffer of @ref HAL_MD5_DIGEST_BYTES bytes.
 * @return true on success, false on invalid args.
 */
bool hal_md5(const uint8_t *input,
             size_t input_len,
             uint8_t out_digest[HAL_MD5_DIGEST_BYTES]);

/**
 * @brief Compute MD5 digest and format it as lowercase hex.
 *
 * Example output: `900150983cd24fb0d6963f7d28e17f72`.
 *
 * @param input      Input data pointer (may be NULL only when @p input_len=0).
 * @param input_len  Input size in bytes.
 * @param output     Output string buffer.
 * @param out_size   Size of @p output in bytes.
 * @return true on success, false on invalid args or insufficient buffer.
 */
bool hal_md5_hex(const uint8_t *input,
                 size_t input_len,
                 char *output,
                 size_t out_size);

/**
 * @brief Return Base64 text length (without trailing NUL) for binary input.
 *
 * @param input_len Input size in bytes.
 * @return Required Base64 character count (without NUL), or 0 on overflow.
 */
size_t hal_base64_encoded_len(size_t input_len);

/**
 * @brief Return maximum decoded byte count for a Base64 text length.
 *
 * This helper does not validate Base64 padding/content; it only computes
 * an upper bound using `floor(input_len / 4) * 3`.
 *
 * @param input_len Base64 text size in bytes.
 * @return Maximum decoded bytes, or 0 on overflow.
 */
size_t hal_base64_decoded_max_len(size_t input_len);

/**
 * @brief Encode binary data to Base64 text.
 *
 * The output is always NUL-terminated on success.
 *
 * @param input      Input data pointer (may be NULL only when @p input_len=0).
 * @param input_len  Input size in bytes.
 * @param output     Output buffer for Base64 text + trailing NUL.
 * @param out_size   Size of @p output in bytes.
 * @param out_len    Optional: receives Base64 text length (without NUL).
 * @return true on success, false on invalid args or insufficient buffer.
 */
bool hal_base64_encode(const uint8_t *input,
                       size_t input_len,
                       char *output,
                       size_t out_size,
                       size_t *out_len);

/**
 * @brief Decode Base64 text to binary data.
 *
 * This function accepts only strict RFC 4648 Base64 with proper `=` padding:
 * - input length must be divisible by 4,
 * - `=` is allowed only in the final quartet (`xx==` or `xxx=`).
 *
 * @param input      Base64 text pointer (may be NULL only when @p input_len=0).
 * @param input_len  Base64 text size in bytes.
 * @param output     Output buffer for decoded bytes.
 *                   May be NULL only when @p out_size is 0 (length query mode).
 * @param out_size   Size of @p output in bytes.
 * @param out_len    Optional: receives decoded byte count.
 * @return true on success, false on invalid input or insufficient buffer.
 */
bool hal_base64_decode(const char *input,
                       size_t input_len,
                       uint8_t *output,
                       size_t out_size,
                       size_t *out_len);

/** @brief SHA-256 digest length in bytes. */
#define HAL_SHA256_DIGEST_BYTES 32u

/** @brief Required buffer size for SHA-256 lowercase hex string (`64 + NUL`). */
#define HAL_SHA256_HEX_BUF_SIZE 65u

/** @brief HMAC-SHA256 block size in bytes (matches the inner SHA-256 block). */
#define HAL_HMAC_SHA256_BLOCK_BYTES 64u

/**
 * @brief Compute SHA-256 digest of input data (FIPS 180-4).
 *
 * @param input      Input data pointer (may be NULL only when @p input_len=0).
 * @param input_len  Input size in bytes.
 * @param out_digest Output buffer of @ref HAL_SHA256_DIGEST_BYTES bytes.
 * @return true on success, false on invalid args.
 */
bool hal_sha256(const uint8_t *input,
                size_t input_len,
                uint8_t out_digest[HAL_SHA256_DIGEST_BYTES]);

/**
 * @brief Compute SHA-256 digest and format it as lowercase hex.
 *
 * Example output: `ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad`.
 *
 * @param input      Input data pointer (may be NULL only when @p input_len=0).
 * @param input_len  Input size in bytes.
 * @param output     Output string buffer (at least @ref HAL_SHA256_HEX_BUF_SIZE bytes).
 * @param out_size   Size of @p output in bytes.
 * @return true on success, false on invalid args or insufficient buffer.
 */
bool hal_sha256_hex(const uint8_t *input,
                    size_t input_len,
                    char *output,
                    size_t out_size);

/**
 * @brief Compute HMAC-SHA256 (RFC 2104).
 *
 * @param key         Key bytes (may be NULL only when @p key_len=0).
 * @param key_len     Key size in bytes (no upper bound; longer keys are
 *                    pre-hashed per RFC 2104).
 * @param message     Message bytes (may be NULL only when @p message_len=0).
 * @param message_len Message size in bytes.
 * @param out_mac     Output buffer of @ref HAL_SHA256_DIGEST_BYTES bytes.
 * @return true on success, false on invalid args.
 */
bool hal_hmac_sha256(const uint8_t *key,
                     size_t key_len,
                     const uint8_t *message,
                     size_t message_len,
                     uint8_t out_mac[HAL_SHA256_DIGEST_BYTES]);

/**
 * @brief Compute HMAC-SHA256 and format the tag as lowercase hex.
 *
 * @param key         Key bytes (may be NULL only when @p key_len=0).
 * @param key_len     Key size in bytes.
 * @param message     Message bytes (may be NULL only when @p message_len=0).
 * @param message_len Message size in bytes.
 * @param output      Output string buffer (at least @ref HAL_SHA256_HEX_BUF_SIZE bytes).
 * @param out_size    Size of @p output in bytes.
 * @return true on success, false on invalid args or insufficient buffer.
 */
bool hal_hmac_sha256_hex(const uint8_t *key,
                         size_t key_len,
                         const uint8_t *message,
                         size_t message_len,
                         char *output,
                         size_t out_size);

#ifdef __cplusplus
}
#endif

#endif /* HAL_ENABLE_CRYPTO */
