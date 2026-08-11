#pragma once

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define HAL_SD_SPI_DRIVE 0u

typedef enum {
  HAL_SD_SPI_CARD_NONE = 0,
  HAL_SD_SPI_CARD_SD1 = 1,
  HAL_SD_SPI_CARD_SD2 = 2,
  HAL_SD_SPI_CARD_SDHC = 3,
} hal_sd_spi_card_type_t;

typedef struct {
  uint8_t spi_bus;
  uint8_t cs_pin;
  uint32_t init_clock_hz;
  uint32_t clock_hz;
} hal_sd_spi_config_t;

void hal_sd_spi_diskio_configure(const hal_sd_spi_config_t *config);
void hal_sd_spi_diskio_configure_pins(uint8_t spi_bus, uint8_t cs_pin);
bool hal_sd_spi_diskio_is_initialized(void);
hal_sd_spi_card_type_t hal_sd_spi_diskio_card_type(void);
uint32_t hal_sd_spi_diskio_sector_count(void);
void hal_sd_spi_diskio_reset(void);

#ifdef __cplusplus
}
#endif
