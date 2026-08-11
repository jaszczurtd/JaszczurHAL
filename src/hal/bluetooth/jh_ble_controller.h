#pragma once

#include "hal/core/hal_status.h"

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef hal_status_t (*jh_ble_controller_service_fn)(void *context);
typedef void (*jh_ble_controller_invalidation_fn)(void *context,
                                                  uint32_t generation);

typedef struct {
  void *context;
  hal_status_t (*start)(void *context, jh_ble_controller_service_fn service,
                        void *service_context,
                        jh_ble_controller_invalidation_fn invalidation,
                        void *invalidation_context);
  hal_status_t (*stop)(void *context);
  hal_status_t (*service)(void *context);
  hal_status_t (*hci_init)(void *context);
  hal_status_t (*hci_read)(void *context, uint8_t *buffer, uint32_t capacity,
                           uint32_t *out_length);
  hal_status_t (*hci_write)(void *context, uint8_t *buffer, size_t length);
  hal_status_t (*read_factory_address)(void *context, uint8_t address[6]);
} jh_ble_controller_t;

/** Return the BLE controller selected by the active target backend. */
const jh_ble_controller_t *jh_ble_controller_backend(void);

/** Shared CYW43 implementation selected by RP and STM32 target backends. */
const jh_ble_controller_t *jh_ble_controller_cyw43_instance(void);

#ifdef __cplusplus
}
#endif
