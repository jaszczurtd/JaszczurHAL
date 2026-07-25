#pragma once

#include "../../../shared/drivers/cyw43-driver/jh_cyw43_gspi_transport.h"
#include "rp2040_cyw43_gspi_clock.h"

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
  uint8_t pin_chip_select;
  uint8_t pin_clock;
  uint8_t pin_wl_on;
  uint8_t pin_data;
  uint32_t target_gspi_hz;
  uint32_t pio_clock_div_override_x256;
  size_t max_transaction_bytes;
} jh_rp2040_cyw43_gspi_config_t;

hal_status_t
jh_rp2040_cyw43_gspi_init(const jh_rp2040_cyw43_gspi_config_t *config);
hal_status_t jh_rp2040_cyw43_gspi_deinit(void);
hal_status_t
jh_rp2040_cyw43_gspi_get_clock(jh_rp2040_cyw43_gspi_clock_t *clock_config);
jh_cyw43_gspi_transport_t *jh_rp2040_cyw43_gspi_transport(void);

#ifdef __cplusplus
}
#endif
