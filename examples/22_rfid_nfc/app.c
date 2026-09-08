/**
 * @file app.c
 * @brief Read RFID and NFC cards using MFRC522 and PN532 devices on one SPI
 * bus.
 *
 * Each reader has separate chip-select and reset signals.
 */

#include <hal/core/hal_app.h>
#include <hal/core/hal_target.h>
#include <hal/nfc/hal_mfrc522.h>
#include <hal/nfc/hal_pn532.h>
#include <hal/serial/hal_serial.h>
#include <hal/spi/hal_spi.h>
#include <hal/system/hal_system.h>

#include <stdio.h>

#if HAL_TARGET_IS_RP
#define EXAMPLE_SPI_MISO 16u
#define EXAMPLE_SPI_MOSI 19u
#define EXAMPLE_SPI_SCK 18u
#define EXAMPLE_MFRC522_CS 17u
#define EXAMPLE_MFRC522_RST 20u
#define EXAMPLE_PN532_CS 21u
#define EXAMPLE_PN532_RST 22u
#elif HAL_TARGET_IS_STM32G474
/* STM32 pin numbers use port * 16 + pin. SPI1 and the MFRC522 CS use the Nucleo
 * SPI/D10 pins. */
#define EXAMPLE_SPI_MISO 6u
#define EXAMPLE_SPI_MOSI 7u
#define EXAMPLE_SPI_SCK 5u
#define EXAMPLE_MFRC522_CS 22u
#define EXAMPLE_MFRC522_RST 17u
#define EXAMPLE_PN532_CS 18u
#define EXAMPLE_PN532_RST 19u
#else
#define EXAMPLE_SPI_MISO 6u
#define EXAMPLE_SPI_MOSI 7u
#define EXAMPLE_SPI_SCK 5u
#define EXAMPLE_MFRC522_CS 16u
#define EXAMPLE_MFRC522_RST 17u
#define EXAMPLE_PN532_CS 18u
#define EXAMPLE_PN532_RST 19u
#endif

static hal_mfrc522_transport_t s_mfrc_transport = NULL;
static hal_mfrc522_t s_mfrc = NULL;
static hal_pn532_transport_t s_pn532_transport = NULL;
static hal_pn532_t s_pn532 = NULL;
static bool s_mfrc_ready = false;
static bool s_pn532_ready = false;
static uint32_t s_last_idle_log_ms = 0u;

static void format_uid(const uint8_t *uid, uint8_t uid_len, char *text,
                       size_t text_size) {
  size_t pos = 0u;
  if (text_size == 0u) {
    return;
  }
  text[0] = '\0';
  for (uint8_t i = 0u; i < uid_len && pos + 3u < text_size; ++i) {
    const int written =
        snprintf(&text[pos], text_size - pos, "%02X", (unsigned)uid[i]);
    if (written <= 0) {
      break;
    }
    pos += (size_t)written;
    if (i + 1u < uid_len && pos + 1u < text_size) {
      text[pos++] = ':';
      text[pos] = '\0';
    }
  }
}

static void poll_mfrc522(void) {
  bool present = false;
  if (!s_mfrc_ready ||
      hal_mfrc522_is_new_card_present(s_mfrc, &present) != HAL_OK || !present) {
    return;
  }
  hal_mfrc522_uid_t uid = {0};
  if (hal_mfrc522_read_uid(s_mfrc, &uid) != HAL_OK) {
    derr("MFRC522 card present but UID read failed");
    return;
  }

  char uid_text[32] = {0};
  format_uid(uid.bytes, uid.size, uid_text, sizeof(uid_text));
  const hal_mfrc522_card_type_t type = hal_mfrc522_card_type_from_sak(uid.sak);
  deb("MFRC522 UID=%s type=%s", uid_text, hal_mfrc522_card_type_name(type));
  (void)hal_mfrc522_halt(s_mfrc);
  (void)hal_mfrc522_mifare_stop_crypto(s_mfrc);
}

static void poll_pn532(void) {
  if (!s_pn532_ready) {
    return;
  }

  hal_pn532_uid_t uid = {0};
  const hal_status_t status = hal_pn532_read_passive_target(
      s_pn532, HAL_PN532_MODULATION_ISO14443A, 20u, &uid);
  if (status == HAL_OK) {
    char uid_text[32] = {0};
    format_uid(uid.bytes, uid.size, uid_text, sizeof(uid_text));
    deb("PN532 UID=%s", uid_text);
  } else if (status != HAL_ETIMEOUT && status != HAL_ENOENT) {
    derr("PN532 read failed: %d", (int)status);
  }
}

void app_start(void) {
  hal_debug_init_default();
  deb("");
  deb("=== JaszczurHAL RFID + NFC example ===");
  hal_spi_init(0u, EXAMPLE_SPI_MISO, EXAMPLE_SPI_MOSI, EXAMPLE_SPI_SCK);

  hal_mfrc522_spi_config_t mfrc_config =
      hal_mfrc522_spi_default_config(EXAMPLE_MFRC522_CS);
  mfrc_config.reset_pin = EXAMPLE_MFRC522_RST;
  hal_status_t status =
      hal_mfrc522_transport_create_spi(&mfrc_config, &s_mfrc_transport);
  if (status == HAL_OK) {
    status = hal_mfrc522_create(s_mfrc_transport, &s_mfrc);
  }
  if (status == HAL_OK) {
    status = hal_mfrc522_begin(s_mfrc);
  }
  uint8_t mfrc_version = 0u;
  if (status == HAL_OK) {
    status = hal_mfrc522_get_version(s_mfrc, &mfrc_version);
  }
  s_mfrc_ready = status == HAL_OK;
  if (s_mfrc_ready) {
    deb("MFRC522 ready, version=0x%02X", (unsigned)mfrc_version);
  } else {
    derr("MFRC522 not detected: %s; continuing with PN532",
         hal_status_to_string(status));
    if (s_mfrc != NULL) {
      (void)hal_mfrc522_destroy(s_mfrc);
      s_mfrc = NULL;
    }
    if (s_mfrc_transport != NULL) {
      (void)hal_mfrc522_transport_destroy(s_mfrc_transport);
      s_mfrc_transport = NULL;
    }
  }

  hal_pn532_spi_config_t pn532_config =
      hal_pn532_spi_default_config(EXAMPLE_PN532_CS);
  pn532_config.reset_pin = EXAMPLE_PN532_RST;
  status = hal_pn532_transport_create_spi(&pn532_config, &s_pn532_transport);
  if (status == HAL_OK) {
    status = hal_pn532_create(s_pn532_transport, &s_pn532);
  }
  if (status == HAL_OK) {
    status = hal_pn532_begin(s_pn532);
  }
  uint32_t version_data = 0u;
  if (status == HAL_OK) {
    status = hal_pn532_get_firmware_version(s_pn532, &version_data);
  }
  if (status == HAL_OK) {
    status = hal_pn532_sam_configure(s_pn532);
  }
  s_pn532_ready = status == HAL_OK;
  if (s_pn532_ready) {
    deb("PN532 ready, firmware=%u.%u", (unsigned)((version_data >> 16) & 0xffu),
        (unsigned)((version_data >> 8) & 0xffu));
  } else {
    derr("PN532 init failed: %s; MFRC522 state is unchanged",
         hal_status_to_string(status));
    if (s_pn532 != NULL) {
      (void)hal_pn532_destroy(s_pn532);
      s_pn532 = NULL;
    }
    if (s_pn532_transport != NULL) {
      (void)hal_pn532_transport_destroy(s_pn532_transport);
      s_pn532_transport = NULL;
    }
  }
}

void app_task0(void) {
  poll_mfrc522();
  poll_pn532();

  const uint32_t now = hal_millis();
  if ((now - s_last_idle_log_ms) >= 3000u) {
    s_last_idle_log_ms = now;
    deb("Waiting for RFID/NFC tag...");
  }
  hal_delay_ms(50u);
}
