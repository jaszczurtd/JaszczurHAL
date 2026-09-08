#include "hal/nfc/hal_pn532.h"

#include <stdint.h>

int main(void) {
  hal_pn532_spi_config_t config = hal_pn532_spi_default_config(9u);
  hal_pn532_transport_t transport = 0;
  hal_pn532_t reader = 0;
  hal_pn532_t second_reader = (hal_pn532_t)(uintptr_t)1u;
  hal_pn532_spi_config_t invalid_config = hal_pn532_spi_default_config(64u);
  if (hal_pn532_transport_create_spi(&invalid_config, &transport) !=
          HAL_EINVAL ||
      transport != 0) {
    return 1;
  }
  invalid_config = config;
  invalid_config.reset_pin = 64u;
  if (hal_pn532_transport_create_spi(&invalid_config, &transport) !=
          HAL_EINVAL ||
      transport != 0) {
    return 1;
  }
  invalid_config = config;
  invalid_config.reset_pin = invalid_config.chip_select_pin;
  if (hal_pn532_transport_create_spi(&invalid_config, &transport) !=
          HAL_EINVAL ||
      transport != 0) {
    return 1;
  }
#ifdef HAL_ENABLE_UART
  hal_pn532_uart_config_t invalid_uart =
      hal_pn532_uart_default_config(HAL_UART_PORT_1, 64u, 2u);
  if (hal_pn532_transport_create_uart(&invalid_uart, &transport) !=
          HAL_EINVAL ||
      transport != 0) {
    return 1;
  }
  invalid_uart = hal_pn532_uart_default_config(HAL_UART_PORT_1, 2u, 2u);
  if (hal_pn532_transport_create_uart(&invalid_uart, &transport) !=
          HAL_EINVAL ||
      transport != 0) {
    return 1;
  }
  invalid_uart = hal_pn532_uart_default_config(HAL_UART_PORT_1, 1u, 2u);
  invalid_uart.reset_pin = invalid_uart.rx_pin;
  if (hal_pn532_transport_create_uart(&invalid_uart, &transport) !=
          HAL_EINVAL ||
      transport != 0) {
    return 1;
  }
#endif
  if (hal_pn532_transport_create_spi(&config, &transport) != HAL_OK ||
      hal_pn532_create(transport, &reader) != HAL_OK ||
      hal_pn532_create(transport, &second_reader) != HAL_EBUSY ||
      second_reader != 0 ||
      hal_pn532_transport_destroy(transport) != HAL_EBUSY ||
      hal_pn532_wakeup(reader) != HAL_ESTATE ||
      hal_pn532_begin(reader) != HAL_OK) {
    return 1;
  }

  const uint8_t frame[] = {0x00u, 0x00u, 0xffu, 0x02u, 0xfeu,
                           0xd5u, 0x03u, 0x28u, 0x00u};
  if (hal_pn532_check_response_frame(frame, sizeof(frame), 0x02u) != HAL_OK) {
    return 2;
  }

  const uint8_t oversized_request[55] = {0};
  uint8_t response[1] = {0};
  size_t response_size = sizeof(response);
  hal_pn532_uid_t short_uid = {{0}, 3u};
  const uint8_t key[HAL_PN532_MIFARE_KEY_SIZE] = {0};
  if (hal_pn532_data_exchange(reader, oversized_request,
                              sizeof(oversized_request), response,
                              &response_size) != HAL_EINVAL ||
      hal_pn532_mifare_classic_authenticate(
          reader, &short_uid, 0u, HAL_PN532_KEY_A, key) != HAL_EINVAL) {
    return 3;
  }

  const hal_pn532_t stale_reader = reader;
  if (hal_pn532_destroy(reader) != HAL_OK ||
      hal_pn532_create(transport, &second_reader) != HAL_OK ||
      hal_pn532_begin(stale_reader) != HAL_EINVAL ||
      hal_pn532_destroy(stale_reader) != HAL_EINVAL ||
      hal_pn532_destroy(second_reader) != HAL_OK) {
    return 4;
  }

  const hal_pn532_transport_t stale_transport = transport;
  transport = 0;
  if (hal_pn532_transport_destroy(stale_transport) != HAL_OK ||
      hal_pn532_transport_create_spi(&config, &transport) != HAL_OK ||
      hal_pn532_transport_destroy(stale_transport) != HAL_EINVAL ||
      hal_pn532_transport_destroy(transport) != HAL_OK) {
    return 5;
  }

  if (hal_pn532_begin(stale_reader) != HAL_EINVAL ||
      hal_pn532_destroy(reader) != HAL_EINVAL ||
      hal_pn532_transport_destroy(stale_transport) != HAL_EINVAL) {
    return 6;
  }
  return 0;
}
