#include <hal/hal_app.h>
#include <hal/hal_eeprom.h>
#include <hal/hal_littlefs.h>
#include <hal/hal_status.h>
#include <hal/hal_system.h>
#include <hal/hal_usb.h>

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>

#define EEPROM_BYTES 1024u
#define EEPROM_MAGIC_ADDR 0u
#define EEPROM_COUNT_ADDR 4u
#define EEPROM_MAGIC ((int32_t)0x4a485354)

static uint8_t s_response[192];
static size_t s_response_length;
static size_t s_response_offset;
static hal_status_t s_eeprom_status = HAL_NONE;
static hal_status_t s_commit_status = HAL_NONE;
static hal_status_t s_littlefs_status = HAL_NONE;
static uint32_t s_boot_count;
static bool s_startup_formatted;

static void prepare_status(void) {
  const int length = snprintf(
      (char *)s_response, sizeof(s_response),
      "JHSTORAGE1 eeprom=%d commit=%d count=%lu littlefs=%d "
      "startup_format=%u total=%lu used=%lu\n",
      (int)s_eeprom_status, (int)s_commit_status, (unsigned long)s_boot_count,
      (int)s_littlefs_status, s_startup_formatted ? 1u : 0u,
      (unsigned long)hal_littlefs_total_bytes(),
      (unsigned long)hal_littlefs_used_bytes());
  s_response_length =
      length > 0 && (size_t)length < sizeof(s_response) ? (size_t)length : 0u;
  s_response_offset = 0u;
}

static void prepare_format_result(void) {
  const hal_status_t format_status = hal_littlefs_format_ex();
  const hal_status_t mount_status =
      format_status == HAL_OK ? hal_littlefs_begin_ex() : HAL_EIO;
  const int length =
      snprintf((char *)s_response, sizeof(s_response),
               "JHSTORAGE-FORMAT format=%d mount=%d total=%lu used=%lu\n",
               (int)format_status, (int)mount_status,
               (unsigned long)hal_littlefs_total_bytes(),
               (unsigned long)hal_littlefs_used_bytes());
  s_response_length =
      length > 0 && (size_t)length < sizeof(s_response) ? (size_t)length : 0u;
  s_response_offset = 0u;
}

void app_start(void) {
  s_eeprom_status = hal_eeprom_init(HAL_EEPROM_FLASH, EEPROM_BYTES, 0u);
  if (s_eeprom_status == HAL_OK) {
    int32_t magic = 0;
    int32_t count = 0;
    if (hal_eeprom_read_int_ex(EEPROM_MAGIC_ADDR, &magic) != HAL_OK ||
        hal_eeprom_read_int_ex(EEPROM_COUNT_ADDR, &count) != HAL_OK ||
        magic != EEPROM_MAGIC || count < 0) {
      count = 0;
    }
    s_boot_count = (uint32_t)count + 1u;
    s_eeprom_status = hal_eeprom_write_int(EEPROM_MAGIC_ADDR, EEPROM_MAGIC);
    if (s_eeprom_status == HAL_OK) {
      s_eeprom_status =
          hal_eeprom_write_int(EEPROM_COUNT_ADDR, (int32_t)s_boot_count);
    }
    if (s_eeprom_status == HAL_OK) {
      s_commit_status = hal_eeprom_commit();
    }
  }

  s_littlefs_status = hal_littlefs_begin_ex();
  if (s_littlefs_status != HAL_OK) {
    s_startup_formatted = true;
    s_littlefs_status = hal_littlefs_format_ex();
    if (s_littlefs_status == HAL_OK) {
      s_littlefs_status = hal_littlefs_begin_ex();
    }
  }
}

void app_task0(void) {
  if (s_response_offset < s_response_length) {
    size_t written = 0u;
    (void)hal_usb_cdc_write(s_response + s_response_offset,
                            s_response_length - s_response_offset, 100u,
                            &written);
    s_response_offset += written;
    return;
  }

  uint8_t command = 0u;
  size_t received = 0u;
  if (hal_usb_cdc_read(&command, 1u, &received) != HAL_OK || received != 1u) {
    hal_delay_ms(1u);
    return;
  }
  if (command == (uint8_t)'S') {
    prepare_status();
  } else if (command == (uint8_t)'F') {
    prepare_format_result();
  } else if (command == (uint8_t)'R') {
    (void)hal_watchdog_enable(10u, false);
    hal_delay_ms(100u);
  }
}
