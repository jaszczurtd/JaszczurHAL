/**
 * @file app.cpp
 * @brief Combined MFRC522 RFID and PN532 NFC example on one SPI bus.
 */

#include <hal/core/hal_app.h>
#include <hal/core/hal_target.h>
#include <hal/nfc/hal_mfrc522.h>
#include <hal/nfc/hal_pn532.h>
#include <hal/spi/hal_spi.h>
#include <hal/system/hal_system.h>
#include <tools.h>

#include <cstdio>
#include <new>

#if HAL_TARGET_IS_RP
#define EXAMPLE_SPI_MISO 16u
#define EXAMPLE_SPI_MOSI 19u
#define EXAMPLE_SPI_SCK 18u
#define EXAMPLE_MFRC522_CS 17u
#define EXAMPLE_MFRC522_RST 20u
#define EXAMPLE_PN532_CS 21u
#define EXAMPLE_PN532_RST 22u
#elif HAL_TARGET_IS_STM32G474
/* STM32 pin id = port * 16 + pin: SPI1, primary CS on Nucleo D10. */
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

alignas(
    MFRC522_SPI) static unsigned char s_mfrc_bus_storage[sizeof(MFRC522_SPI)];
alignas(MFRC522) static unsigned char s_mfrc_storage[sizeof(MFRC522)];
alignas(PN532_SPI) static unsigned char s_pn532_bus_storage[sizeof(PN532_SPI)];
alignas(PN532) static unsigned char s_pn532_storage[sizeof(PN532)];

static MFRC522_SPI *s_mfrc_bus = nullptr;
static MFRC522 *s_mfrc = nullptr;
static PN532_SPI *s_pn532_bus = nullptr;
static PN532 *s_pn532 = nullptr;
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
  if (!s_mfrc_ready || !s_mfrc->PICC_IsNewCardPresent()) {
    return;
  }
  if (!s_mfrc->PICC_ReadCardSerial()) {
    derr("MFRC522 card present but UID read failed");
    return;
  }

  char uid_text[32] = {};
  format_uid(s_mfrc->uid.uidByte, s_mfrc->uid.size, uid_text, sizeof(uid_text));
  const MFRC522::PICC_Type type = MFRC522::PICC_GetType(s_mfrc->uid.sak);
  deb("MFRC522 UID=%s type=%s", uid_text, MFRC522::PICC_GetTypeName(type));
  (void)s_mfrc->PICC_HaltA();
  s_mfrc->PCD_StopCrypto1();
}

static void poll_pn532(void) {
  if (!s_pn532_ready) {
    return;
  }

  uint8_t uid[7] = {};
  uint8_t uid_len = 0u;
  const hal_status_t status =
      s_pn532->readPassiveTargetID(PN532_MIFARE_ISO14443A, uid, &uid_len, 20u);
  if (status == HAL_OK) {
    char uid_text[32] = {};
    format_uid(uid, uid_len, uid_text, sizeof(uid_text));
    deb("PN532 UID=%s", uid_text);
  } else if (status != HAL_ETIMEOUT && status != HAL_ENOENT) {
    derr("PN532 read failed: %d", (int)status);
  }
}

void app_start(void) {
  debugInit();
  deb("");
  deb("=== JaszczurHAL RFID + NFC example ===");
  hal_spi_init(0u, EXAMPLE_SPI_MISO, EXAMPLE_SPI_MOSI, EXAMPLE_SPI_SCK);

  s_mfrc_bus = new (s_mfrc_bus_storage)
      MFRC522_SPI(EXAMPLE_MFRC522_CS, EXAMPLE_MFRC522_RST, 0u);
  s_mfrc = new (s_mfrc_storage) MFRC522(s_mfrc_bus);
  s_mfrc->PCD_Init();
  const byte version = s_mfrc->PCD_GetVersion();
  s_mfrc_ready = version != 0x00u && version != 0xffu;
  if (s_mfrc_ready) {
    deb("MFRC522 ready, version=0x%02X", (unsigned)version);
  } else {
    derr("MFRC522 not detected; continuing with PN532");
  }

  s_pn532_bus = new (s_pn532_bus_storage)
      PN532_SPI(EXAMPLE_PN532_CS, EXAMPLE_PN532_RST, 0u);
  s_pn532 = new (s_pn532_storage) PN532(s_pn532_bus);
  hal_status_t status = s_pn532->begin();
  uint32_t version_data = 0u;
  if (status == HAL_OK) {
    status = s_pn532->getFirmwareVersion(&version_data);
  }
  if (status == HAL_OK) {
    status = s_pn532->SAMConfig();
  }
  s_pn532_ready = status == HAL_OK;
  if (s_pn532_ready) {
    deb("PN532 ready, firmware=%u.%u", (unsigned)((version_data >> 16) & 0xffu),
        (unsigned)((version_data >> 8) & 0xffu));
  } else {
    derr("PN532 init failed: %d; MFRC522 state is unchanged", (int)status);
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
