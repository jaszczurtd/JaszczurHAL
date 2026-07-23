#pragma once

#include "hal/hal_tls.h"
#include "vendor/inc/bearssl.h"

#ifdef HAL_ENABLE_TLS

#define JH_BEARSSL_ENTROPY_SIZE 32u

typedef struct {
  br_ssl_client_context client;
  br_x509_minimal_context x509;
  br_x509_trust_anchor anchors[HAL_TLS_MAX_TRUST_ANCHORS];
  unsigned char io_buffer[BR_SSL_BUFSIZE_MONO];
} jh_bearssl_client_t;

/**
 * Provider storage hook. Targets with non-contiguous SRAM may override the
 * weak default implementation to place the large BearSSL record/context block
 * in a suitable memory bank.
 */
jh_bearssl_client_t *jh_bearssl_client_allocate(void);
void jh_bearssl_client_release(jh_bearssl_client_t *provider);

hal_status_t jh_bearssl_client_init(jh_bearssl_client_t *provider,
                                    const hal_tls_trust_anchor_t *trust_anchors,
                                    size_t trust_anchor_count,
                                    const char *hostname, uint64_t unix_seconds,
                                    const void *entropy, size_t entropy_length);

hal_status_t jh_bearssl_error_to_hal(int32_t error);
hal_status_t
jh_bearssl_verify_server_key_pin(const jh_bearssl_client_t *provider,
                                 const uint8_t expected_sha256[32]);

const char *jh_bearssl_provider_source_revision(void);

#endif
