#include "jh_bearssl_provider.h"
#include "hal/hal_config.h"

#if defined(HAL_ENABLE_TLS) && defined(HAL_ENABLE_BSD_SOCKETS)

#include <string.h>

static bool anchor_is_valid(const hal_tls_trust_anchor_t *anchor) {
  if (anchor == NULL || anchor->subject_dn == NULL ||
      anchor->subject_dn_length == 0u) {
    return false;
  }
  if (anchor->key_type == HAL_TLS_TRUST_KEY_RSA) {
    return anchor->key.rsa.modulus != NULL &&
           anchor->key.rsa.modulus_length > 0u &&
           anchor->key.rsa.exponent != NULL &&
           anchor->key.rsa.exponent_length > 0u;
  }
  if (anchor->key_type == HAL_TLS_TRUST_KEY_EC) {
    return anchor->key.ec.curve > 0 && anchor->key.ec.point != NULL &&
           anchor->key.ec.point_length > 0u;
  }
  return false;
}

hal_status_t jh_bearssl_client_init(jh_bearssl_client_t *provider,
                                    const hal_tls_trust_anchor_t *trust_anchors,
                                    size_t trust_anchor_count,
                                    const char *hostname, uint64_t unix_seconds,
                                    const void *entropy,
                                    size_t entropy_length) {
  if (provider == NULL || trust_anchors == NULL || trust_anchor_count == 0u ||
      trust_anchor_count > HAL_TLS_MAX_TRUST_ANCHORS || hostname == NULL ||
      hostname[0] == '\0' || unix_seconds < HAL_TLS_MIN_VALID_UNIX_TIME ||
      entropy == NULL || entropy_length < JH_BEARSSL_ENTROPY_SIZE) {
    return HAL_ECONFIG;
  }

  memset(provider, 0, sizeof(*provider));
  for (size_t index = 0u; index < trust_anchor_count; ++index) {
    const hal_tls_trust_anchor_t *source = &trust_anchors[index];
    if (!anchor_is_valid(source)) {
      memset(provider, 0, sizeof(*provider));
      return HAL_ECONFIG;
    }
    br_x509_trust_anchor *target = &provider->anchors[index];
    target->dn.data = const_cast<unsigned char *>(source->subject_dn);
    target->dn.len = source->subject_dn_length;
    target->flags = BR_X509_TA_CA;
    if (source->key_type == HAL_TLS_TRUST_KEY_RSA) {
      target->pkey.key_type = BR_KEYTYPE_RSA;
      target->pkey.key.rsa.n =
          const_cast<unsigned char *>(source->key.rsa.modulus);
      target->pkey.key.rsa.nlen = source->key.rsa.modulus_length;
      target->pkey.key.rsa.e =
          const_cast<unsigned char *>(source->key.rsa.exponent);
      target->pkey.key.rsa.elen = source->key.rsa.exponent_length;
    } else {
      target->pkey.key_type = BR_KEYTYPE_EC;
      target->pkey.key.ec.curve = source->key.ec.curve;
      target->pkey.key.ec.q = const_cast<unsigned char *>(source->key.ec.point);
      target->pkey.key.ec.qlen = source->key.ec.point_length;
    }
  }

  br_ssl_client_init_full(&provider->client, &provider->x509, provider->anchors,
                          trust_anchor_count);
  const uint64_t unix_days = unix_seconds / 86400u;
  if (unix_days > UINT32_MAX - 719528u) {
    memset(provider, 0, sizeof(*provider));
    return HAL_EOVERFLOW;
  }
  br_x509_minimal_set_time(&provider->x509, (uint32_t)unix_days + 719528u,
                           (uint32_t)(unix_seconds % 86400u));
  br_ssl_engine_set_buffer(&provider->client.eng, provider->io_buffer,
                           sizeof(provider->io_buffer), 0);
  br_ssl_engine_inject_entropy(&provider->client.eng, entropy, entropy_length);
  if (!br_ssl_client_reset(&provider->client, hostname, 0)) {
    memset(provider, 0, sizeof(*provider));
    return HAL_EAUTH;
  }
  return HAL_OK;
}

hal_status_t jh_bearssl_error_to_hal(int32_t error) {
  if (error == BR_ERR_OK) {
    return HAL_OK;
  }
  if (error >= BR_ERR_X509_OK) {
    return HAL_EAUTH;
  }
  return error == BR_ERR_IO ? HAL_EIO : HAL_EPROTO;
}

hal_status_t
jh_bearssl_verify_server_key_pin(const jh_bearssl_client_t *provider,
                                 const uint8_t expected_sha256[32]) {
  if (provider == NULL || expected_sha256 == NULL) {
    return HAL_EINVAL;
  }
  unsigned usages = 0u;
  const br_x509_pkey *key =
      provider->x509.vtable->get_pkey(&provider->x509.vtable, &usages);
  if (key == NULL || usages == 0u) {
    return HAL_EAUTH;
  }
  br_sha256_context hash = {};
  br_sha256_init(&hash);
  const uint8_t key_type = (uint8_t)key->key_type;
  br_sha256_update(&hash, &key_type, sizeof(key_type));
  if (key->key_type == BR_KEYTYPE_RSA) {
    br_sha256_update(&hash, key->key.rsa.n, key->key.rsa.nlen);
    br_sha256_update(&hash, key->key.rsa.e, key->key.rsa.elen);
  } else if (key->key_type == BR_KEYTYPE_EC) {
    const uint32_t curve = (uint32_t)key->key.ec.curve;
    const uint8_t curve_be[4] = {(uint8_t)(curve >> 24u),
                                 (uint8_t)(curve >> 16u),
                                 (uint8_t)(curve >> 8u), (uint8_t)curve};
    br_sha256_update(&hash, curve_be, sizeof(curve_be));
    br_sha256_update(&hash, key->key.ec.q, key->key.ec.qlen);
  } else {
    return HAL_EAUTH;
  }
  uint8_t actual[32] = {};
  br_sha256_out(&hash, actual);
  uint8_t difference = 0u;
  for (size_t index = 0u; index < sizeof(actual); ++index) {
    difference |= actual[index] ^ expected_sha256[index];
  }
  memset(actual, 0, sizeof(actual));
  return difference == 0u ? HAL_OK : HAL_EAUTH;
}

hal_status_t jh_bearssl_blocking_io_init(jh_bearssl_blocking_io_t *provider,
                                         br_ssl_engine_context *engine, int fd,
                                         uint32_t timeout_ms,
                                         jh_bearssl_cancel_fn is_cancelled,
                                         jh_bearssl_service_fn service,
                                         void *callback_context) {
  if (provider == NULL || engine == NULL) {
    return HAL_EINVAL;
  }
  memset(provider, 0, sizeof(*provider));
  hal_status_t status =
      jh_bearssl_bsd_io_init(&provider->transport, fd, timeout_ms, is_cancelled,
                             service, callback_context);
  if (status != HAL_OK) {
    return status;
  }
  br_sslio_init(&provider->io, engine, jh_bearssl_bsd_read,
                &provider->transport, jh_bearssl_bsd_write,
                &provider->transport);
  return HAL_OK;
}

const char *jh_bearssl_provider_source_revision(void) {
  return "aca13833b6f9ddffaea2041a01facc76829dc03b";
}

#endif
