#pragma once

#include "../../../shared/drivers/cyw43-driver/jh_cyw43_gspi_transport.h"

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
  uint16_t pio_clock_div_int;
  uint8_t pio_clock_div_frac8;
  size_t max_transaction_bytes;
} jh_rp2040_cyw43_gspi_config_t;

hal_status_t
jh_rp2040_cyw43_gspi_init(const jh_rp2040_cyw43_gspi_config_t *config);
hal_status_t jh_rp2040_cyw43_gspi_deinit(void);
jh_cyw43_gspi_transport_t *jh_rp2040_cyw43_gspi_transport(void);

#ifdef __cplusplus
}
#endif
