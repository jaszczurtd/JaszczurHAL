#include "hal/nfc/hal_mfrc522.h"

#include <type_traits>

static_assert(std::is_class_v<MFRC522>);
static_assert(std::is_class_v<MFRC522_SPI>);
static_assert(std::is_pointer_v<hal_mfrc522_t>);
static_assert(std::is_same_v<decltype(&hal_mfrc522_begin),
                             hal_status_t (*)(hal_mfrc522_t)>);
static_assert(
    std::is_same_v<decltype(&hal_mfrc522_read_uid),
                   hal_status_t (*)(hal_mfrc522_t, hal_mfrc522_uid_t *)>);

int main() {
  hal_mfrc522_spi_config_t config = {};
  MFRC522_SPI *legacy_transport = nullptr;
  MFRC522 *legacy_reader = nullptr;
  (void)config;
  (void)legacy_transport;
  (void)legacy_reader;
  return 0;
}
