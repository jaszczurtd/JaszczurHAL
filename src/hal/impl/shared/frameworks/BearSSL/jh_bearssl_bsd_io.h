#pragma once

#include "hal/hal_status.h"

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

hal_status_t jh_bearssl_bsd_io_init(jh_bearssl_bsd_io_t *io, int fd,
                                    uint32_t timeout_ms,
                                    jh_bearssl_cancel_fn is_cancelled,
                                    jh_bearssl_service_fn service,
                                    void *callback_context);
int jh_bearssl_bsd_read(void *context, unsigned char *buffer, size_t length);
int jh_bearssl_bsd_write(void *context, const unsigned char *data,
                         size_t length);
