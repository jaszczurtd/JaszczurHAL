#pragma once

#include "hal/hal_status.h"

#include <stddef.h>

/**
 * Internal non-blocking transport seam used by the shared BearSSL engine.
 *
 * HAL TCP and BSD sockets provide independent adapters for this contract.
 * A successful operation must transfer at least one byte; HAL_EAGAIN reports
 * that the caller should service the network and poll again.
 */
typedef struct {
  void *context;
  hal_status_t (*send)(void *context, const void *data, size_t length,
                       size_t *out_sent);
  hal_status_t (*receive)(void *context, void *buffer, size_t capacity,
                          size_t *out_received);
} jh_bearssl_transport_t;
