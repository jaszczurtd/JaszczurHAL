#ifndef JH_SHA256_H
#define JH_SHA256_H

/**
 * @file jh_sha256.h
 * @brief SHA-256 core (FIPS 180-4) behind the SHA-256 and HMAC-SHA256
 * functions of hal/security/hal_crypto.h.
 *
 * The core needs no HAL configuration, so host tools of this repository
 * compile it on its own. Applications use hal/security/hal_crypto.h, which
 * validates arguments and reports errors.
 */

#include "hal/core/hal_layout_check.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/** @brief SHA-256 block size in bytes. */
#define JH_SHA256_BLOCK_BYTES 64u

/** @brief SHA-256 digest size in bytes. */
#define JH_SHA256_DIGEST_BYTES 32u

/**
 * @brief Incremental SHA-256 state.
 *
 * Treat the fields as private implementation state. The concrete layout keeps
 * the context allocation deterministic on embedded targets.
 */
typedef struct {
  uint32_t state[8];
  uint64_t bit_count;
  uint8_t buffer[JH_SHA256_BLOCK_BYTES];
  size_t buffer_len;
  bool initialized;
} jh_sha256_context_t;

/**
 * @brief Start a digest in @p context.
 * @param context Required context; any previous state is replaced.
 */
void jh_sha256_init(jh_sha256_context_t *context);

/**
 * @brief Add @p length bytes to an initialized digest.
 * @param context Required context started by jh_sha256_init().
 * @param data    Input bytes; NULL only when @p length is 0.
 * @param length  Input size in bytes.
 */
void jh_sha256_update(jh_sha256_context_t *context, const uint8_t *data,
                      size_t length);

/**
 * @brief Finish the digest and invalidate @p context.
 * @param context Required context started by jh_sha256_init().
 * @param digest  Output of @ref JH_SHA256_DIGEST_BYTES bytes.
 */
void jh_sha256_final(jh_sha256_context_t *context,
                     uint8_t digest[JH_SHA256_DIGEST_BYTES]);

#ifdef __cplusplus
}
#endif

#endif /* JH_SHA256_H */
