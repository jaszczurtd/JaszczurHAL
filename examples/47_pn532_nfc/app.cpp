/**
 * @file app.cpp
 * @brief PN532 NFC/RFID reader example over JaszczurHAL SPI.
 */

#include <hal/hal_app.h>
#include <hal/hal_pn532.h>
#include <hal/hal_spi.h>
#include <hal/hal_system.h>
#include <hal/hal_target.h>
#include <tools.h>

#include <cstdio>
#include <new>

#if HAL_TARGET_IS_RP2040
#define EXAMPLE_SPI_MISO 16u
#define EXAMPLE_SPI_MOSI 19u
#define EXAMPLE_SPI_SCK 18u
#define EXAMPLE_PN532_CS 17u
#define EXAMPLE_PN532_RST 20u
#else
/* STM32 pin id = port * 16 + pin: PA5/PA6/PA7 for SPI1, PB0/PB1 for CS/RST. */
#define EXAMPLE_SPI_MISO 6u
#define EXAMPLE_SPI_MOSI 7u
#define EXAMPLE_SPI_SCK 5u
#define EXAMPLE_PN532_CS 16u
#define EXAMPLE_PN532_RST 17u
#endif

alignas(PN532_SPI) static unsigned char s_pn532_spi_storage[sizeof(PN532_SPI)];
alignas(PN532) static unsigned char s_pn532_storage[sizeof(PN532)];

static PN532_SPI *s_pn532_bus = nullptr;
static PN532 *s_pn532 = nullptr;
static uint32_t s_last_idle_log_ms = 0u;

static void print_uid(const uint8_t *uid, uint8_t uid_len) {
  char uid_text[32] = {};
  size_t pos = 0u;
  for (uint8_t i = 0; i < uid_len && pos + 3u < sizeof(uid_text); ++i) {
    int written = snprintf(&uid_text[pos], sizeof(uid_text) - pos, "%02X",
                           (unsigned)uid[i]);
    if (written <= 0) {
      break;
    }
    pos += (size_t)written;
    if (i + 1u < uid_len && pos + 1u < sizeof(uid_text)) {
      uid_text[pos++] = ':';
      uid_text[pos] = '\0';
    }
  }

  deb("PN532 UID=%s len=%u", uid_text, (unsigned)uid_len);
}

void app_start(void) {
  debugInit();
  deb("");
  deb("=== JaszczurHAL PN532 NFC example ===");

  hal_spi_init(0u, EXAMPLE_SPI_MISO, EXAMPLE_SPI_MOSI, EXAMPLE_SPI_SCK);

  s_pn532_bus =
      new (s_pn532_spi_storage) PN532_SPI(EXAMPLE_PN532_CS, EXAMPLE_PN532_RST);
  s_pn532 = new (s_pn532_storage) PN532(s_pn532_bus);

  hal_status_t status = s_pn532->begin();
  if (status != HAL_OK) {
    derr("PN532 begin failed: %d", (int)status);
    return;
  }

  uint32_t version = 0;
  status = s_pn532->getFirmwareVersion(&version);
  if (status != HAL_OK) {
    derr("PN532 communication check failed: %d", (int)status);
    return;
  }

  deb("PN532 firmware IC=0x%02X ver=%u.%u support=0x%02X",
      (unsigned)((version >> 24) & 0xFFu), (unsigned)((version >> 16) & 0xFFu),
      (unsigned)((version >> 8) & 0xFFu), (unsigned)(version & 0xFFu));

  status = s_pn532->SAMConfig();
  if (status != HAL_OK) {
    derr("PN532 SAMConfig failed: %d", (int)status);
  }
}

void app_task0(void) {
  if (s_pn532 == nullptr) {
    hal_delay_ms(500u);
    return;
  }

  uint8_t uid[7] = {};
  uint8_t uid_len = 0u;
  hal_status_t status =
      s_pn532->readPassiveTargetID(PN532_MIFARE_ISO14443A, uid, &uid_len, 100u);

  if (status == HAL_OK) {
    print_uid(uid, uid_len);
    hal_delay_ms(500u);
    return;
  }

  if (status != HAL_ETIMEOUT && status != HAL_ENOENT) {
    derr("PN532 read target failed: %d", (int)status);
    hal_delay_ms(250u);
    return;
  }

  uint32_t now = hal_millis();
  if ((now - s_last_idle_log_ms) >= 3000u) {
    s_last_idle_log_ms = now;
    deb("Waiting for NFC tag...");
  }
  hal_delay_ms(50u);
}
