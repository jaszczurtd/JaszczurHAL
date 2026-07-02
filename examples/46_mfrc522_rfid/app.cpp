/**
 * @file app.cpp
 * @brief MFRC522 RFID reader example over JaszczurHAL SPI.
 */

#include <hal/hal_app.h>
#include <hal/hal_mfrc522.h>
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
#define EXAMPLE_MFRC522_CS 17u
#define EXAMPLE_MFRC522_RST 20u
#else
/* STM32 pin id = port * 16 + pin: PA5/PA6/PA7 for SPI1, PB0/PB1 for CS/RST. */
#define EXAMPLE_SPI_MISO 6u
#define EXAMPLE_SPI_MOSI 7u
#define EXAMPLE_SPI_SCK 5u
#define EXAMPLE_MFRC522_CS 16u
#define EXAMPLE_MFRC522_RST 17u
#endif

alignas(MFRC522_SPI) static unsigned char s_mfrc522_spi_storage[sizeof(
    MFRC522_SPI)];
alignas(MFRC522) static unsigned char s_mfrc522_storage[sizeof(MFRC522)];

static MFRC522_SPI *s_mfrc522_bus = nullptr;
static MFRC522 *s_mfrc522 = nullptr;
static uint32_t s_last_idle_log_ms = 0u;

static void print_uid(const MFRC522::Uid &uid) {
  char uid_text[32] = {};
  size_t pos = 0u;
  for (byte i = 0; i < uid.size && pos + 3u < sizeof(uid_text); ++i) {
    int written = snprintf(&uid_text[pos], sizeof(uid_text) - pos, "%02X",
                           (unsigned)uid.uidByte[i]);
    if (written <= 0) {
      break;
    }
    pos += (size_t)written;
    if (i + 1u < uid.size && pos + 1u < sizeof(uid_text)) {
      uid_text[pos++] = ':';
      uid_text[pos] = '\0';
    }
  }

  MFRC522::PICC_Type type = MFRC522::PICC_GetType(uid.sak);
  deb("UID=%s SAK=0x%02X type=%s", uid_text, (unsigned)uid.sak,
      MFRC522::PICC_GetTypeName(type));
}

void app_start(void) {
  debugInit();
  deb("");
  deb("=== JaszczurHAL MFRC522 RFID example ===");

  hal_spi_init(0u, EXAMPLE_SPI_MISO, EXAMPLE_SPI_MOSI, EXAMPLE_SPI_SCK);

  s_mfrc522_bus = new (s_mfrc522_spi_storage)
      MFRC522_SPI(EXAMPLE_MFRC522_CS, EXAMPLE_MFRC522_RST, 0u);
  s_mfrc522 = new (s_mfrc522_storage) MFRC522(s_mfrc522_bus);
  s_mfrc522->PCD_Init();

  byte version = s_mfrc522->PCD_GetVersion();
  deb("MFRC522 version=0x%02X", (unsigned)version);
  if (version == 0x00u || version == 0xFFu) {
    derr("MFRC522 communication check failed");
  }
}

void app_task0(void) {
  if (s_mfrc522 == nullptr) {
    hal_delay_ms(500u);
    return;
  }

  if (!s_mfrc522->PICC_IsNewCardPresent()) {
    uint32_t now = hal_millis();
    if ((now - s_last_idle_log_ms) >= 3000u) {
      s_last_idle_log_ms = now;
      deb("Waiting for card...");
    }
    hal_delay_ms(50u);
    return;
  }

  if (!s_mfrc522->PICC_ReadCardSerial()) {
    derr("Card present but UID read failed");
    hal_delay_ms(200u);
    return;
  }

  print_uid(s_mfrc522->uid);
  (void)s_mfrc522->PICC_HaltA();
  s_mfrc522->PCD_StopCrypto1();
  hal_delay_ms(500u);
}
