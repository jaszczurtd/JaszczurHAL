/*
 * FatFs disk I/O glue for SD cards on SPI.
 *
 * The SD command sequence and token handling follow the battle-tested SdFat
 * SdSpiCard flow used by arduino-pico, but the transport is pure JaszczurHAL:
 * hal_spi + hal_gpio + hal_system.
 */

#include "../../../../hal_config.h"

#ifdef HAL_ENABLE_FAT

#include "diskio.h"
#include "ff.h"
#include "hal_sd_spi_diskio.h"

#include "../../../../hal_gpio.h"
#include "../../../../hal_spi.h"
#include "../../../../hal_system.h"

#include <stddef.h>
#include <stdint.h>
#include <string.h>

#ifndef HAL_SD_SPI_DEFAULT_BUS
#define HAL_SD_SPI_DEFAULT_BUS 0u
#endif

#ifndef HAL_SD_SPI_DEFAULT_CS_PIN
#define HAL_SD_SPI_DEFAULT_CS_PIN 5u
#endif

#ifndef HAL_SD_SPI_INIT_CLOCK_HZ
#define HAL_SD_SPI_INIT_CLOCK_HZ 400000u
#endif

#ifndef HAL_SD_SPI_CLOCK_HZ
#define HAL_SD_SPI_CLOCK_HZ 12000000u
#endif

#define SD_CMD_TIMEOUT_MS 300u
#define SD_INIT_TIMEOUT_MS 2000u
#define SD_READ_TIMEOUT_MS 300u
#define SD_WRITE_TIMEOUT_MS 600u

#define SD_CMD0 0x00u
#define SD_CMD8 0x08u
#define SD_CMD9 0x09u
#define SD_CMD17 0x11u
#define SD_CMD24 0x18u
#define SD_CMD55 0x37u
#define SD_CMD58 0x3Au
#define SD_ACMD41 0x29u

#define SD_R1_READY_STATE 0x00u
#define SD_R1_IDLE_STATE 0x01u
#define SD_R1_ILLEGAL_COMMAND 0x04u

#define SD_DATA_START_BLOCK 0xFEu
#define SD_DATA_RESPONSE_MASK 0x1Fu
#define SD_DATA_ACCEPTED 0x05u

typedef struct {
  bool configured;
  bool initialized;
  uint8_t spi_bus;
  uint8_t cs_pin;
  uint32_t init_clock_hz;
  uint32_t clock_hz;
  hal_sd_spi_card_type_t card_type;
  uint32_t sector_count;
  DSTATUS status;
} hal_sd_spi_state_t;

static hal_sd_spi_state_t s_sd = {
    false,
    false,
    HAL_SD_SPI_DEFAULT_BUS,
    HAL_SD_SPI_DEFAULT_CS_PIN,
    HAL_SD_SPI_INIT_CLOCK_HZ,
    HAL_SD_SPI_CLOCK_HZ,
    HAL_SD_SPI_CARD_NONE,
    0u,
    STA_NOINIT,
};

static hal_spi_settings_t sd_spi_settings(uint32_t clock_hz) {
  hal_spi_settings_t settings = {clock_hz, HAL_SPI_MSBFIRST, HAL_SPI_MODE0};
  return settings;
}

static bool sd_elapsed(uint32_t start_ms, uint32_t timeout_ms) {
  return (uint32_t)(hal_millis() - start_ms) >= timeout_ms;
}

static void sd_select(void) { hal_gpio_write(s_sd.cs_pin, false); }

static void sd_unselect(void) { hal_gpio_write(s_sd.cs_pin, true); }

static uint8_t sd_spi_rx(void) { return hal_spi_transfer(s_sd.spi_bus, 0xFFu); }

static void sd_spi_tx(uint8_t data) {
  (void)hal_spi_transfer(s_sd.spi_bus, data);
}

static void sd_spi_rx_buf(uint8_t *dst, size_t len) {
  hal_spi_transfer_txrx(s_sd.spi_bus, NULL, dst, len);
}

static void sd_spi_tx_buf(const uint8_t *src, size_t len) {
  hal_spi_write(s_sd.spi_bus, src, len);
}

static void sd_begin_transaction(uint32_t clock_hz) {
  const hal_spi_settings_t settings = sd_spi_settings(clock_hz);
  hal_spi_lock(s_sd.spi_bus);
  hal_spi_begin_transaction(s_sd.spi_bus, &settings);
}

static void sd_end_transaction(void) {
  hal_spi_end_transaction(s_sd.spi_bus);
  hal_spi_unlock(s_sd.spi_bus);
}

static void sd_stop_transaction(void) {
  sd_unselect();
  sd_spi_tx(0xFFu);
  sd_end_transaction();
}

static bool sd_wait_ready(uint32_t timeout_ms) {
  const uint32_t start = hal_millis();
  do {
    if (sd_spi_rx() == 0xFFu) {
      return true;
    }
  } while (!sd_elapsed(start, timeout_ms));
  return false;
}

static uint8_t sd_command(uint8_t cmd, uint32_t arg) {
  if (cmd != SD_CMD0 && !sd_wait_ready(SD_CMD_TIMEOUT_MS)) {
    return 0xFFu;
  }

  sd_spi_tx((uint8_t)(0x40u | cmd));
  sd_spi_tx((uint8_t)(arg >> 24));
  sd_spi_tx((uint8_t)(arg >> 16));
  sd_spi_tx((uint8_t)(arg >> 8));
  sd_spi_tx((uint8_t)arg);

  if (cmd == SD_CMD0) {
    sd_spi_tx(0x95u);
  } else if (cmd == SD_CMD8) {
    sd_spi_tx(0x87u);
  } else {
    sd_spi_tx(0xFFu);
  }

  (void)sd_spi_rx();
  for (uint8_t i = 0u; i < 10u; ++i) {
    const uint8_t response = sd_spi_rx();
    if ((response & 0x80u) == 0u) {
      return response;
    }
  }
  return 0xFFu;
}

static uint8_t sd_acmd(uint8_t cmd, uint32_t arg) {
  const uint8_t r1 = sd_command(SD_CMD55, 0u);
  if (r1 > SD_R1_IDLE_STATE) {
    return r1;
  }
  return sd_command(cmd, arg);
}

static bool sd_read_data(uint8_t *dst, size_t len) {
  const uint32_t start = hal_millis();
  uint8_t token = 0xFFu;
  do {
    token = sd_spi_rx();
    if (token == SD_DATA_START_BLOCK) {
      sd_spi_rx_buf(dst, len);
      (void)sd_spi_rx();
      (void)sd_spi_rx();
      return true;
    }
  } while (token == 0xFFu && !sd_elapsed(start, SD_READ_TIMEOUT_MS));
  return false;
}

static bool sd_write_data(const uint8_t *src) {
  if (!sd_wait_ready(SD_WRITE_TIMEOUT_MS)) {
    return false;
  }

  sd_spi_tx(SD_DATA_START_BLOCK);
  sd_spi_tx_buf(src, 512u);
  sd_spi_tx(0xFFu);
  sd_spi_tx(0xFFu);

  const uint8_t response = sd_spi_rx();
  return (response & SD_DATA_RESPONSE_MASK) == SD_DATA_ACCEPTED;
}

static bool sd_read_register(uint8_t cmd, uint8_t *dst, size_t len) {
  if (sd_command(cmd, 0u) != SD_R1_READY_STATE) {
    return false;
  }
  return sd_read_data(dst, len);
}

static uint32_t sd_csd_sector_count(const uint8_t csd[16]) {
  const uint8_t version = (uint8_t)(csd[0] >> 6);
  if (version == 0u) {
    uint32_t c_size = ((uint32_t)(csd[6] & 0x03u) << 10) |
                      ((uint32_t)csd[7] << 2) | (uint32_t)(csd[8] >> 6);
    const uint8_t c_size_mult =
        (uint8_t)(((csd[9] & 0x03u) << 1) | (csd[10] >> 7));
    const uint8_t read_bl_len = (uint8_t)(csd[5] & 0x0Fu);
    return (c_size + 1u) << (c_size_mult + read_bl_len + 2u - 9u);
  }
  if (version == 1u) {
    uint32_t c_size = ((uint32_t)(csd[7] & 0x3Fu) << 16) |
                      ((uint32_t)csd[8] << 8) | (uint32_t)csd[9];
    return (c_size + 1u) << 10;
  }
  return 0u;
}

static bool sd_refresh_sector_count(void) {
  uint8_t csd[16] = {};
  sd_begin_transaction(s_sd.clock_hz);
  sd_select();
  const bool ok = sd_read_register(SD_CMD9, csd, sizeof(csd));
  sd_stop_transaction();

  if (!ok) {
    return false;
  }

  s_sd.sector_count = sd_csd_sector_count(csd);
  return s_sd.sector_count != 0u;
}

static bool sd_card_init(void) {
  s_sd.initialized = false;
  s_sd.card_type = HAL_SD_SPI_CARD_NONE;
  s_sd.sector_count = 0u;
  s_sd.status = STA_NOINIT;

  hal_gpio_set_mode(s_sd.cs_pin, HAL_GPIO_OUTPUT_HIGH);
  sd_unselect();

  sd_begin_transaction(s_sd.init_clock_hz);
  for (uint8_t i = 0u; i < 10u; ++i) {
    sd_spi_rx();
  }
  sd_select();

  uint8_t response = 0xFFu;
  uint32_t start = hal_millis();
  do {
    response = sd_command(SD_CMD0, 0u);
    if (response == SD_R1_IDLE_STATE) {
      break;
    }
  } while (!sd_elapsed(start, SD_INIT_TIMEOUT_MS));

  if (response != SD_R1_IDLE_STATE) {
    sd_stop_transaction();
    return false;
  }

  hal_sd_spi_card_type_t card_type = HAL_SD_SPI_CARD_SD1;
  response = sd_command(SD_CMD8, 0x1AAu);
  if ((response & SD_R1_ILLEGAL_COMMAND) == 0u) {
    uint8_t r7[4] = {};
    sd_spi_rx_buf(r7, sizeof(r7));
    if (r7[2] != 0x01u || r7[3] != 0xAAu) {
      sd_stop_transaction();
      return false;
    }
    card_type = HAL_SD_SPI_CARD_SD2;
  }

  const uint32_t acmd_arg =
      (card_type == HAL_SD_SPI_CARD_SD2) ? 0x40000000u : 0u;
  start = hal_millis();
  do {
    response = sd_acmd(SD_ACMD41, acmd_arg);
    if (response == SD_R1_READY_STATE) {
      break;
    }
  } while (!sd_elapsed(start, SD_INIT_TIMEOUT_MS));

  if (response != SD_R1_READY_STATE) {
    sd_stop_transaction();
    return false;
  }

  if (card_type == HAL_SD_SPI_CARD_SD2) {
    if (sd_command(SD_CMD58, 0u) != SD_R1_READY_STATE) {
      sd_stop_transaction();
      return false;
    }
    const uint8_t ocr0 = sd_spi_rx();
    (void)sd_spi_rx();
    (void)sd_spi_rx();
    (void)sd_spi_rx();
    if ((ocr0 & 0xC0u) == 0xC0u) {
      card_type = HAL_SD_SPI_CARD_SDHC;
    }
  }

  sd_stop_transaction();

  s_sd.card_type = card_type;
  s_sd.initialized = true;
  s_sd.status = 0u;
  (void)sd_refresh_sector_count();
  return true;
}

void hal_sd_spi_diskio_configure(const hal_sd_spi_config_t *config) {
  if (config == NULL) {
    return;
  }
  s_sd.spi_bus = config->spi_bus;
  s_sd.cs_pin = config->cs_pin;
  s_sd.init_clock_hz =
      config->init_clock_hz ? config->init_clock_hz : HAL_SD_SPI_INIT_CLOCK_HZ;
  s_sd.clock_hz = config->clock_hz ? config->clock_hz : HAL_SD_SPI_CLOCK_HZ;
  s_sd.configured = true;
  hal_sd_spi_diskio_reset();
}

void hal_sd_spi_diskio_configure_pins(uint8_t spi_bus, uint8_t cs_pin) {
  hal_sd_spi_config_t config = {spi_bus, cs_pin, HAL_SD_SPI_INIT_CLOCK_HZ,
                                HAL_SD_SPI_CLOCK_HZ};
  hal_sd_spi_diskio_configure(&config);
}

bool hal_sd_spi_diskio_is_initialized(void) { return s_sd.initialized; }

hal_sd_spi_card_type_t hal_sd_spi_diskio_card_type(void) {
  return s_sd.card_type;
}

uint32_t hal_sd_spi_diskio_sector_count(void) { return s_sd.sector_count; }

void hal_sd_spi_diskio_reset(void) {
  s_sd.initialized = false;
  s_sd.card_type = HAL_SD_SPI_CARD_NONE;
  s_sd.sector_count = 0u;
  s_sd.status = STA_NOINIT;
}

DSTATUS disk_initialize(BYTE pdrv) {
  if (pdrv != HAL_SD_SPI_DRIVE) {
    return STA_NOINIT;
  }
  if (!s_sd.configured) {
    s_sd.configured = true;
  }
  return sd_card_init() ? 0u : STA_NOINIT;
}

DSTATUS disk_status(BYTE pdrv) {
  if (pdrv != HAL_SD_SPI_DRIVE) {
    return STA_NOINIT;
  }
  return s_sd.status;
}

DRESULT disk_read(BYTE pdrv, BYTE *buff, LBA_t sector, UINT count) {
  if (pdrv != HAL_SD_SPI_DRIVE || buff == NULL || count == 0u) {
    return RES_PARERR;
  }
  if (!s_sd.initialized) {
    return RES_NOTRDY;
  }

  sd_begin_transaction(s_sd.clock_hz);
  for (UINT i = 0u; i < count; ++i) {
    uint32_t arg = (uint32_t)(sector + i);
    if (s_sd.card_type != HAL_SD_SPI_CARD_SDHC) {
      arg <<= 9;
    }
    if (sd_command(SD_CMD17, arg) != SD_R1_READY_STATE ||
        !sd_read_data(buff + ((size_t)i * 512u), 512u)) {
      sd_stop_transaction();
      return RES_ERROR;
    }
    sd_unselect();
    sd_spi_tx(0xFFu);
    sd_select();
  }
  sd_stop_transaction();
  return RES_OK;
}

DRESULT disk_write(BYTE pdrv, const BYTE *buff, LBA_t sector, UINT count) {
  if (pdrv != HAL_SD_SPI_DRIVE || buff == NULL || count == 0u) {
    return RES_PARERR;
  }
  if (!s_sd.initialized) {
    return RES_NOTRDY;
  }

  sd_begin_transaction(s_sd.clock_hz);
  for (UINT i = 0u; i < count; ++i) {
    uint32_t arg = (uint32_t)(sector + i);
    if (s_sd.card_type != HAL_SD_SPI_CARD_SDHC) {
      arg <<= 9;
    }
    if (sd_command(SD_CMD24, arg) != SD_R1_READY_STATE ||
        !sd_write_data(buff + ((size_t)i * 512u)) ||
        !sd_wait_ready(SD_WRITE_TIMEOUT_MS)) {
      sd_stop_transaction();
      return RES_ERROR;
    }
    sd_unselect();
    sd_spi_tx(0xFFu);
    sd_select();
  }
  sd_stop_transaction();
  return RES_OK;
}

DRESULT disk_ioctl(BYTE pdrv, BYTE cmd, void *buff) {
  if (pdrv != HAL_SD_SPI_DRIVE) {
    return RES_PARERR;
  }
  if (!s_sd.initialized) {
    return RES_NOTRDY;
  }

  switch (cmd) {
  case CTRL_SYNC:
    sd_begin_transaction(s_sd.clock_hz);
    sd_select();
    if (!sd_wait_ready(SD_WRITE_TIMEOUT_MS)) {
      sd_stop_transaction();
      return RES_ERROR;
    }
    sd_stop_transaction();
    return RES_OK;

  case GET_SECTOR_COUNT:
    if (buff == NULL) {
      return RES_PARERR;
    }
    if (s_sd.sector_count == 0u) {
      (void)sd_refresh_sector_count();
    }
    if (s_sd.sector_count == 0u) {
      return RES_ERROR;
    }
    *(LBA_t *)buff = (LBA_t)s_sd.sector_count;
    return RES_OK;

  case GET_SECTOR_SIZE:
    if (buff == NULL) {
      return RES_PARERR;
    }
    *(WORD *)buff = 512u;
    return RES_OK;

  case GET_BLOCK_SIZE:
    if (buff == NULL) {
      return RES_PARERR;
    }
    *(DWORD *)buff = 1u;
    return RES_OK;

  default:
    return RES_PARERR;
  }
}

extern "C" __attribute__((weak)) DWORD get_fattime(void) {
  return ((DWORD)(2025u - 1980u) << 25) | ((DWORD)1u << 21) | ((DWORD)1u << 16);
}

#endif /* HAL_ENABLE_FAT */
