#pragma once

#include "hal/core/hal_status.h"

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef hal_status_t (*jh_flash_transaction_operation_t)(void *context);

typedef struct {
  hal_status_t (*acquire)(void *backend, uint32_t timeout_ms);
  hal_status_t (*quiesce)(void *backend, uint32_t timeout_ms);
  hal_status_t (*execute)(void *backend,
                          jh_flash_transaction_operation_t operation,
                          void *operation_context, uint32_t timeout_ms);
  hal_status_t (*resume)(void *backend);
  hal_status_t (*release)(void *backend);
} jh_flash_transaction_backend_t;

hal_status_t jh_flash_transaction_engine_execute(
    const jh_flash_transaction_backend_t *backend, void *backend_context,
    jh_flash_transaction_operation_t operation, void *operation_context,
    uint32_t timeout_ms);

#ifdef __cplusplus
}
#endif
