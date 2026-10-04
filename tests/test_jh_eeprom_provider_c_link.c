/* The EEPROM region functions are reachable from C: an application keeps its
 * own A/B configuration slots in the EEPROM reservation and rewrites one slot
 * with jh_eeprom_replace_region() (STM32 flash layout of the mock: 2 KB
 * pages, 8-byte programming). */
#include "hal/storage/hal_eeprom.h"
#include "hal/storage/jh_eeprom_provider.h"

#include <stdbool.h>
#include <string.h>

#define PAGE 2048u
#define SLOT_SIZE (2u * PAGE)
#define HEADER 24u

static uint8_t s_slot[SLOT_SIZE];
static uint8_t s_back[SLOT_SIZE];

static bool all_bytes(const uint8_t *data, size_t len, uint8_t value) {
  for (size_t i = 0u; i < len; i++) {
    if (data[i] != value) {
      return false;
    }
  }
  return true;
}

int main(void) {
  if (hal_eeprom_init(HAL_EEPROM_STM32_FLASH, 4u * PAGE, 0u) != HAL_OK) {
    return 1;
  }

  /* Page 0 holds the other slot; the replace must leave it alone. */
  memset(s_slot, 0x11, PAGE);
  if (hal_eeprom_write_bytes(0u, s_slot, PAGE) != HAL_OK ||
      hal_eeprom_commit() != HAL_OK) {
    return 2;
  }

  for (size_t i = 0u; i < SLOT_SIZE; i++) {
    s_slot[i] = (uint8_t)(i * 7u);
  }
  if (jh_eeprom_validate_region(PAGE, SLOT_SIZE, HEADER) != HAL_OK ||
      jh_eeprom_validate_region(PAGE + 8u, SLOT_SIZE, HEADER) == HAL_OK ||
      jh_eeprom_replace_region(PAGE, s_slot, SLOT_SIZE, HEADER) != HAL_OK) {
    return 3;
  }
  if (hal_eeprom_read_bytes(PAGE, s_back, SLOT_SIZE) != HAL_OK ||
      memcmp(s_back, s_slot, SLOT_SIZE) != 0) {
    return 4;
  }
  if (hal_eeprom_read_bytes(0u, s_back, PAGE) != HAL_OK ||
      !all_bytes(s_back, PAGE, 0x11u)) {
    return 5;
  }

  uint16_t append_size = 0u;
  bool erased = false;
  if (jh_eeprom_append_size(&append_size) != HAL_OK || append_size != 8u ||
      jh_eeprom_erase_region(3u * PAGE, PAGE) != HAL_OK ||
      jh_eeprom_region_erased(3u * PAGE, PAGE, &erased) != HAL_OK || !erased) {
    return 6;
  }
  return 0;
}
