#include "hal/nfc/hal_pn532.h"

#include <type_traits>

static_assert(std::is_class_v<PN532>);
static_assert(std::is_class_v<PN532_SPI>);
static_assert(std::is_pointer_v<hal_pn532_t>);
static_assert(
    std::is_same_v<decltype(&hal_pn532_begin), hal_status_t (*)(hal_pn532_t)>);
static_assert(
    std::is_same_v<decltype(&hal_pn532_read_passive_target),
                   hal_status_t (*)(hal_pn532_t, hal_pn532_modulation_t,
                                    uint16_t, hal_pn532_uid_t *)>);

int main() {
  hal_pn532_spi_config_t config = {};
  PN532_SPI *legacy_transport = nullptr;
  PN532 *legacy_reader = nullptr;
  (void)config;
  (void)legacy_transport;
  (void)legacy_reader;
  return 0;
}
