#pragma once

#include "hal/network/cyw43/jh_cyw43_gspi_transport.h"

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/** STM32G474 pin profile for a CYW43 one-wire gSPI connection. */
typedef struct {
  uint8_t pin_chip_select;
  uint8_t pin_clock;
  uint8_t pin_wl_on;
  uint8_t pin_data;
  size_t max_transaction_bytes;
} jh_stm32g474_cyw43_gspi_config_t;

hal_status_t
jh_stm32g474_cyw43_gspi_init(const jh_stm32g474_cyw43_gspi_config_t *config);
hal_status_t jh_stm32g474_cyw43_gspi_deinit(void);
jh_cyw43_gspi_transport_t *jh_stm32g474_cyw43_gspi_transport(void);

/** Synthetic EXTI one-shot check used only by the private hardware harness. */
hal_status_t jh_stm32g474_cyw43_gspi_host_wake_self_test(void);

#ifdef __cplusplus
}
#endif
