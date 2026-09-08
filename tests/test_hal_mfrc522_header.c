#include "hal/nfc/hal_mfrc522.h"

_Static_assert(HAL_MFRC522_UID_MAX_SIZE == 10u, "MFRC522 UID capacity");
_Static_assert(HAL_MFRC522_MIFARE_KEY_SIZE == 6u, "MFRC522 key size");
_Static_assert(HAL_MFRC522_MIFARE_BLOCK_SIZE == 16u, "MFRC522 block size");

int main(void) {
  hal_mfrc522_transport_t transport = 0;
  hal_mfrc522_t reader = 0;
  hal_mfrc522_uid_t uid = {{0}, 0u, 0u};
  hal_mfrc522_spi_config_t config = {0};
  (void)transport;
  (void)reader;
  (void)uid;
  (void)config;
  return 0;
}
