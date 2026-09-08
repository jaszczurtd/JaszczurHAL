#include "hal/nfc/hal_mfrc522.h"

#include <stdint.h>
#include <string.h>

int main(void) {
  hal_mfrc522_spi_config_t config = hal_mfrc522_spi_default_config(7u);
  hal_mfrc522_transport_t transport = 0;
  hal_mfrc522_t reader = 0;
  hal_mfrc522_t second_reader = (hal_mfrc522_t)(uintptr_t)1u;
  hal_mfrc522_spi_config_t invalid_config = hal_mfrc522_spi_default_config(64u);
  if (hal_mfrc522_transport_create_spi(&invalid_config, &transport) !=
          HAL_EINVAL ||
      transport != 0) {
    return 1;
  }
  invalid_config = config;
  invalid_config.reset_pin = 64u;
  if (hal_mfrc522_transport_create_spi(&invalid_config, &transport) !=
          HAL_EINVAL ||
      transport != 0) {
    return 1;
  }
  invalid_config = config;
  invalid_config.reset_pin = invalid_config.chip_select_pin;
  if (hal_mfrc522_transport_create_spi(&invalid_config, &transport) !=
          HAL_EINVAL ||
      transport != 0) {
    return 1;
  }
  if (hal_mfrc522_transport_create_spi(&config, &transport) != HAL_OK ||
      hal_mfrc522_create(transport, &reader) != HAL_OK ||
      hal_mfrc522_create(transport, &second_reader) != HAL_EBUSY ||
      second_reader != 0 ||
      hal_mfrc522_transport_destroy(transport) != HAL_EBUSY) {
    return 1;
  }

  uint8_t version = 0u;
  if (hal_mfrc522_get_version(reader, &version) != HAL_ESTATE ||
      hal_mfrc522_begin(reader) != HAL_ENOENT) {
    return 2;
  }

  hal_mfrc522_uid_t short_uid = {{0}, 3u, 0u};
  hal_mfrc522_uid_t new_uid = {{0}, 4u, 0u};
  hal_mfrc522_mifare_key_t key = {{0}};
  const char *unknown_name =
      hal_mfrc522_card_type_name(HAL_MFRC522_CARD_UNKNOWN);
  if (hal_mfrc522_get_version(reader, &version) != HAL_ESTATE ||
      hal_mfrc522_card_type_from_sak(0x08u) != HAL_MFRC522_CARD_MIFARE_1K ||
      hal_mfrc522_card_type_name(HAL_MFRC522_CARD_MIFARE_1K) == 0 ||
      strcmp(hal_mfrc522_card_type_name((hal_mfrc522_card_type_t)-1),
             unknown_name) != 0 ||
      strcmp(hal_mfrc522_card_type_name((hal_mfrc522_card_type_t)257),
             unknown_name) != 0 ||
      hal_mfrc522_mifare_authenticate(reader, HAL_MFRC522_KEY_A, 0u, &key,
                                      &short_uid) != HAL_EINVAL ||
      hal_mfrc522_mifare_set_uid(reader, &new_uid, false) != HAL_ESTATE) {
    return 3;
  }

  const hal_mfrc522_t stale_reader = reader;
  if (hal_mfrc522_destroy(reader) != HAL_OK ||
      hal_mfrc522_create(transport, &second_reader) != HAL_OK ||
      hal_mfrc522_begin(stale_reader) != HAL_EINVAL ||
      hal_mfrc522_destroy(stale_reader) != HAL_EINVAL ||
      hal_mfrc522_destroy(second_reader) != HAL_OK) {
    return 4;
  }

  const hal_mfrc522_transport_t stale_transport = transport;
  transport = 0;
  if (hal_mfrc522_transport_destroy(stale_transport) != HAL_OK ||
      hal_mfrc522_transport_create_spi(&config, &transport) != HAL_OK ||
      hal_mfrc522_transport_destroy(stale_transport) != HAL_EINVAL ||
      hal_mfrc522_transport_destroy(transport) != HAL_OK) {
    return 5;
  }

  if (hal_mfrc522_begin(stale_reader) != HAL_EINVAL ||
      hal_mfrc522_destroy(reader) != HAL_EINVAL ||
      hal_mfrc522_transport_destroy(stale_transport) != HAL_EINVAL) {
    return 6;
  }
  return 0;
}
