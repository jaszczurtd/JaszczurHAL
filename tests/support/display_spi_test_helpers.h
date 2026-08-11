#ifndef JH_DISPLAY_SPI_TEST_HELPERS_H
#define JH_DISPLAY_SPI_TEST_HELPERS_H

#include "hal/impl/.mock/hal_mock.h"

#include <string.h>

static bool tx_has_tail(const uint8_t *tail, size_t tail_len) {
  uint8_t tx[512] = {};
  const size_t tx_len = hal_mock_spi_get_tx(0u, tx, sizeof(tx));
  if (tx_len < tail_len) {
    return false;
  }
  return memcmp(&tx[tx_len - tail_len], tail, tail_len) == 0;
}

#endif
