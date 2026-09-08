#include "hal/nfc/hal_pn532.h"

_Static_assert(HAL_PN532_UID_MAX_SIZE == 7u, "PN532 UID capacity");
_Static_assert(HAL_PN532_MIFARE_KEY_SIZE == 6u, "PN532 key size");
_Static_assert(HAL_PN532_MIFARE_BLOCK_SIZE == 16u, "PN532 block size");

int main(void) {
  hal_pn532_transport_t transport = 0;
  hal_pn532_t reader = 0;
  hal_pn532_uid_t uid = {{0}, 0u};
  hal_pn532_spi_config_t config = {0};
  (void)transport;
  (void)reader;
  (void)uid;
  (void)config;
  return 0;
}
