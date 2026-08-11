#include "hal/security/hal_sc_auth.h"

#ifdef HAL_ENABLE_CRYPTO

#include "hal/security/jh_secure_random.h"

#include <string.h>

bool hal_sc_auth_derive_device_key(const uint8_t *uid, size_t uid_len,
                                   uint8_t out_key[HAL_SC_AUTH_KEY_BYTES]) {
  if (out_key == nullptr) {
    return false;
  }
  jh_secure_zeroize(out_key, HAL_SC_AUTH_KEY_BYTES);
  if (uid == nullptr || uid_len == 0u) {
    return false;
  }
  const bool ok = hal_hmac_sha256(HAL_SC_AUTH_SALT, HAL_SC_AUTH_SALT_LEN, uid,
                                  uid_len, out_key);
  if (!ok) {
    jh_secure_zeroize(out_key, HAL_SC_AUTH_KEY_BYTES);
  }
  return ok;
}

bool hal_sc_auth_compute_response(
    const uint8_t device_key[HAL_SC_AUTH_KEY_BYTES], const uint8_t *challenge,
    size_t challenge_len, uint32_t session_id,
    uint8_t out_response[HAL_SC_AUTH_RESPONSE_BYTES]) {
  if (out_response == nullptr) {
    return false;
  }
  jh_secure_zeroize(out_response, HAL_SC_AUTH_RESPONSE_BYTES);
  if (device_key == nullptr || challenge == nullptr || challenge_len == 0u ||
      challenge_len > HAL_SC_AUTH_CHALLENGE_BYTES) {
    return false;
  }

  uint8_t message[HAL_SC_AUTH_CHALLENGE_BYTES + 4u] = {0u};
  memcpy(message, challenge, challenge_len);
  hal_u32_to_bytes_be(session_id, &message[challenge_len]);
  const bool ok = hal_hmac_sha256(device_key, HAL_SC_AUTH_KEY_BYTES, message,
                                  challenge_len + 4u, out_response);
  jh_secure_zeroize(message, sizeof(message));
  if (!ok) {
    jh_secure_zeroize(out_response, HAL_SC_AUTH_RESPONSE_BYTES);
  }
  return ok;
}

bool hal_sc_auth_macs_equal(const uint8_t *a, const uint8_t *b, size_t len) {
  return jh_constant_time_compare(a, b, len);
}

#endif /* HAL_ENABLE_CRYPTO */
