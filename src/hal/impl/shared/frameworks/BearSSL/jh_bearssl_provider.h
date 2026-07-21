#pragma once

#include "jh_bearssl_bsd_io.h"
#include "vendor/inc/bearssl.h"

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
