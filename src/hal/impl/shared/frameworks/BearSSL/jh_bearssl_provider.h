#pragma once

#include "hal/hal_tls.h"
#include "jh_bearssl_bsd_io.h"
#include "vendor/inc/bearssl.h"

#if defined(HAL_ENABLE_TLS) && defined(HAL_ENABLE_BSD_SOCKETS)

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

typedef struct {
  br_sslio_context io;
  jh_bearssl_bsd_io_t transport;
} jh_bearssl_blocking_io_t;

hal_status_t jh_bearssl_blocking_io_init(jh_bearssl_blocking_io_t *provider,
                                         br_ssl_engine_context *engine, int fd,
                                         uint32_t timeout_ms,
                                         jh_bearssl_cancel_fn is_cancelled,
                                         jh_bearssl_service_fn service,
                                         void *callback_context);

const char *jh_bearssl_provider_source_revision(void);

#endif
