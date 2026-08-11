#ifndef JH_TLS_TEST_HELPERS_H
#define JH_TLS_TEST_HELPERS_H

#include "hal/network/tls/hal_tls.h"

static hal_tls_trust_anchor_t jh_test_tls_rsa_anchor(void) {
  static const uint8_t distinguished_name[] = {0x30u, 0x00u};
  static const uint8_t modulus[] = {0x01u};
  static const uint8_t exponent[] = {0x03u};
  hal_tls_trust_anchor_t anchor = {};
  anchor.subject_dn = distinguished_name;
  anchor.subject_dn_length = sizeof(distinguished_name);
  anchor.key_type = HAL_TLS_TRUST_KEY_RSA;
  anchor.key.rsa.modulus = modulus;
  anchor.key.rsa.modulus_length = sizeof(modulus);
  anchor.key.rsa.exponent = exponent;
  anchor.key.rsa.exponent_length = sizeof(exponent);
  return anchor;
}

static hal_tls_security_config_t jh_test_tls_security_config(
    const hal_tls_trust_anchor_t *anchor, hal_tls_time_fn get_time,
    hal_tls_entropy_fn get_entropy, void *callback_context = nullptr) {
  hal_tls_security_config_t security = {};
  security.trust_anchors = anchor;
  security.trust_anchor_count = 1u;
  security.get_time = get_time;
  security.get_entropy = get_entropy;
  security.callback_context = callback_context;
  return security;
}

#endif
