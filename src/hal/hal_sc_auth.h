#pragma once

/**
 * @file hal_sc_auth.h
 * @brief Compile-time salt + per-device key derivation for the
 *        SerialConfigurator authentication handshake (Phase 3).
 *
 * Threat model and design notes live in the SerialConfigurator context
 * provider (sections 7 and the Phase 3 roadmap entry). In short:
 *
 *   - The firmware never stores a long-term shared secret in flash KV.
 *     A per-device key is derived at boot from the immutable factory UID
 *     and a compile-time salt baked into the firmware image.
 *   - The host (desktop tool) ships with the same salt and derives the
 *     same key from the module's reported UID after `HELLO`.
 *   - Authentication is a single-round challenge/response: the module
 *     hands out a fresh challenge nonce and the host must reply with a
 *     valid `HMAC-SHA256(K_device, challenge || session_id_be32)`.
 *
 * Constructions:
 *   - K_device = HMAC-SHA256(key=salt, message=uid_bytes)
 *   - response = HMAC-SHA256(key=K_device, message=challenge || session_id_be32)
 *
 * The salt value @ref HAL_SC_AUTH_SALT must stay byte-for-byte identical
 * with the host-side mirror (`src/SerialConfigurator/src/core/sc_auth.h`).
 * Changing it on one side without the other invalidates every existing
 * deployment.
 *
 * The salt is intentionally a public, project-wide constant. It does not
 * grant any authority by itself — the secret is the per-device UID +
 * salt mixture. Treating the salt as confidential would only obscure
 * design intent; integrity of the derivation depends on HMAC-SHA256, not
 * on salt secrecy.
 */

#include "hal_config.h"
#include "hal_crypto.h"
#include "hal_system.h"

#ifdef HAL_ENABLE_CRYPTO

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

#ifdef __cplusplus
extern "C" {
#endif

/** @brief Authentication scheme identifier embedded in the salt. */
#define HAL_SC_AUTH_SCHEME_TAG "FIESTA-SC-AUTH-v1"

/** @brief Length of @ref HAL_SC_AUTH_SCHEME_TAG (excluding NUL). */
#define HAL_SC_AUTH_SCHEME_TAG_LEN 17u

/** @brief Compile-time salt for K_device derivation (no NUL terminator). */
#define HAL_SC_AUTH_SALT \
    ((const uint8_t *)HAL_SC_AUTH_SCHEME_TAG)

/** @brief Length of the salt in bytes. */
#define HAL_SC_AUTH_SALT_LEN HAL_SC_AUTH_SCHEME_TAG_LEN

/** @brief Per-device authentication key length (HMAC-SHA256 output). */
#define HAL_SC_AUTH_KEY_BYTES HAL_SHA256_DIGEST_BYTES

/** @brief Challenge nonce length in bytes (128 bits). */
#define HAL_SC_AUTH_CHALLENGE_BYTES 16u

/** @brief Buffer size for hex-encoded challenge (32 hex + NUL). */
#define HAL_SC_AUTH_CHALLENGE_HEX_BUF_SIZE 33u

/** @brief Response MAC length in bytes (HMAC-SHA256 output). */
#define HAL_SC_AUTH_RESPONSE_BYTES HAL_SHA256_DIGEST_BYTES

/** @brief Buffer size for hex-encoded response MAC (64 hex + NUL). */
#define HAL_SC_AUTH_RESPONSE_HEX_BUF_SIZE HAL_SHA256_HEX_BUF_SIZE

/**
 * @brief Derive per-device authentication key from the device UID.
 *
 * @param uid     UID bytes (must not be NULL).
 * @param uid_len UID length in bytes (typically @ref HAL_DEVICE_UID_BYTES).
 * @param out_key Output buffer of @ref HAL_SC_AUTH_KEY_BYTES bytes.
 * @return true on success, false on invalid args.
 */
static inline bool hal_sc_auth_derive_device_key(
    const uint8_t *uid,
    size_t uid_len,
    uint8_t out_key[HAL_SC_AUTH_KEY_BYTES])
{
    if (uid == NULL || uid_len == 0u || out_key == NULL) {
        return false;
    }
    return hal_hmac_sha256(HAL_SC_AUTH_SALT, HAL_SC_AUTH_SALT_LEN,
                           uid, uid_len,
                           out_key);
}

/**
 * @brief Compute the expected challenge response.
 *
 * MAC = HMAC-SHA256(key=device_key,
 *                   message=challenge || session_id_be32)
 *
 * The session id is serialised in big-endian for cross-platform stability
 * (matches @ref hal_u32_to_bytes_be).
 *
 * @param device_key       Per-device key from @ref hal_sc_auth_derive_device_key.
 * @param challenge        Challenge nonce (must not be NULL).
 * @param challenge_len    Challenge length (typically @ref HAL_SC_AUTH_CHALLENGE_BYTES).
 * @param session_id       Session id echoed in the challenge.
 * @param out_response     Output MAC of @ref HAL_SC_AUTH_RESPONSE_BYTES bytes.
 * @return true on success, false on invalid args.
 */
static inline bool hal_sc_auth_compute_response(
    const uint8_t device_key[HAL_SC_AUTH_KEY_BYTES],
    const uint8_t *challenge,
    size_t challenge_len,
    uint32_t session_id,
    uint8_t out_response[HAL_SC_AUTH_RESPONSE_BYTES])
{
    if (device_key == NULL || challenge == NULL || challenge_len == 0u ||
        out_response == NULL) {
        return false;
    }

    uint8_t message[HAL_SC_AUTH_CHALLENGE_BYTES + 4u];
    if (challenge_len > HAL_SC_AUTH_CHALLENGE_BYTES) {
        return false;
    }
    memcpy(message, challenge, challenge_len);
    hal_u32_to_bytes_be(session_id, &message[challenge_len]);

    return hal_hmac_sha256(device_key, HAL_SC_AUTH_KEY_BYTES,
                           message, challenge_len + 4u,
                           out_response);
}

/**
 * @brief Constant-time comparison of two MAC tags.
 *
 * Returns true iff @p a and @p b are equal. Uses an OR-accumulator over the
 * full length to keep timing independent of where the bytes diverge.
 */
static inline bool hal_sc_auth_macs_equal(const uint8_t *a,
                                          const uint8_t *b,
                                          size_t len)
{
    if (a == NULL || b == NULL) {
        return false;
    }
    uint8_t diff = 0u;
    for (size_t i = 0u; i < len; ++i) {
        diff = (uint8_t)(diff | (a[i] ^ b[i]));
    }
    return diff == 0u;
}

#ifdef __cplusplus
}
#endif

#endif /* HAL_ENABLE_CRYPTO */
