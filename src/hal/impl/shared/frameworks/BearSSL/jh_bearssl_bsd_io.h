#pragma once

#include "hal/hal_config.h"

#if defined(HAL_ENABLE_TLS) && defined(HAL_ENABLE_BSD_SOCKETS)

#include "hal/hal_status.h"
#include "jh_bearssl_transport.h"
#include <bearssl.h>

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

typedef bool (*jh_bearssl_cancel_fn)(void *context);
typedef void (*jh_bearssl_service_fn)(void *context);

typedef struct {
  int fd;
  uint32_t timeout_ms;
  int last_errno;
  hal_status_t last_status;
  jh_bearssl_cancel_fn is_cancelled;
  jh_bearssl_service_fn service;
  void *callback_context;
} jh_bearssl_bsd_io_t;

typedef struct {
  jh_bearssl_transport_t transport;
  int fd;
} jh_bearssl_bsd_transport_t;

typedef struct {
  br_sslio_context io;
  jh_bearssl_bsd_io_t transport;
} jh_bearssl_blocking_io_t;

hal_status_t jh_bearssl_bsd_transport_init(jh_bearssl_bsd_transport_t *adapter,
                                           int fd);
hal_status_t jh_bearssl_bsd_io_init(jh_bearssl_bsd_io_t *io, int fd,
                                    uint32_t timeout_ms,
                                    jh_bearssl_cancel_fn is_cancelled,
                                    jh_bearssl_service_fn service,
                                    void *callback_context);
int jh_bearssl_bsd_read(void *context, unsigned char *buffer, size_t length);
int jh_bearssl_bsd_write(void *context, const unsigned char *data,
                         size_t length);
hal_status_t jh_bearssl_blocking_io_init(jh_bearssl_blocking_io_t *provider,
                                         br_ssl_engine_context *engine, int fd,
                                         uint32_t timeout_ms,
                                         jh_bearssl_cancel_fn is_cancelled,
                                         jh_bearssl_service_fn service,
                                         void *callback_context);

#endif
