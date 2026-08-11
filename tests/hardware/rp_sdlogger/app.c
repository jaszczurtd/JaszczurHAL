#include <hal/core/hal_app.h>
#include <hal/core/hal_status.h>
#include <hal/core/hal_target.h>
#include <hal/spi/hal_spi.h>
#include <hal/storage/filesystem/sd/hal_sd_file.h>
#include <hal/storage/hal_eeprom.h>
#include <hal/storage/hal_sdlogger.h>
#include <hal/system/hal_board.h>
#include <hal/system/hal_system.h>
#include <hal/usb/hal_usb.h>

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#define SD_SPI_BUS 0u
#define SD_MISO_PIN 16u
#define SD_MOSI_PIN 19u
#define SD_SCK_PIN 18u
#define SD_CS_PIN 17u
#define EEPROM_BYTES 512u
#define CONTENT_RECORDS 3u
#define USB_WRITE_TIMEOUT_MS 5000u

#if defined(HAL_ENABLE_FREERTOS)
#define JH_RUNTIME_NAME "freertos"
#else
#define JH_RUNTIME_NAME "baremetal"
#endif

static uint8_t s_response[384];
static size_t s_response_length;
static size_t s_response_offset;
static hal_status_t s_eeprom_status = HAL_NONE;
static hal_status_t s_spi_status = HAL_NONE;

static unsigned bounded_log_number(int number) {
  return number < 0 ? 0u : (unsigned)number % 100000u;
}

static uint32_t content_token(unsigned file_number, unsigned record) {
  return ((uint32_t)file_number * 2654435761u) ^
         ((uint32_t)record * 0x9e3779b9u) ^ 0x4a485344u;
}

static uint32_t fnv1a(const uint8_t *data, size_t length) {
  uint32_t hash = 2166136261u;
  for (size_t index = 0u; index < length; ++index) {
    hash ^= data[index];
    hash *= 16777619u;
  }
  return hash;
}

static size_t build_expected_content(int log_number, char *buffer,
                                     size_t capacity) {
  const unsigned file_number = bounded_log_number(log_number);
  size_t offset = 0u;
  for (unsigned record = 0u; record < CONTENT_RECORDS; ++record) {
    if (offset >= capacity) {
      return 0u;
    }
    const int length =
        snprintf(buffer + offset, capacity - offset,
                 "JHSD1 file=%05u record=%u token=%08lx\n", file_number, record,
                 (unsigned long)content_token(file_number, record));
    if (length <= 0 || (size_t)length >= capacity - offset) {
      return 0u;
    }
    offset += (size_t)length;
  }
  return offset;
}

static void make_filename(int log_number, char *buffer, size_t capacity) {
  snprintf(buffer, capacity, "log%05u.txt", bounded_log_number(log_number));
}

static void set_response_length(int length) {
  s_response_length =
      length > 0 && (size_t)length < sizeof(s_response) ? (size_t)length : 0u;
  s_response_offset = 0u;
}

static void handle_write(void) {
  char expected[256] = {};
  const int before = hal_sdlogger_get_log_number();
  const size_t expected_length =
      build_expected_content(before, expected, sizeof(expected));
  hal_status_t append_status[CONTENT_RECORDS] = {HAL_NONE, HAL_NONE, HAL_NONE};
  const hal_status_t init_status = hal_sdlogger_init_ex((int)SD_CS_PIN);

  if (init_status == HAL_OK && expected_length != 0u) {
    const unsigned file_number = bounded_log_number(before);
    for (unsigned record = 0u; record < CONTENT_RECORDS; ++record) {
      char line[80] = {};
      snprintf(line, sizeof(line), "JHSD1 file=%05u record=%u token=%08lx",
               file_number, record,
               (unsigned long)content_token(file_number, record));
      append_status[record] = hal_sdlogger_append(line);
    }
  }

  const hal_status_t close_status =
      init_status == HAL_OK ? hal_sdlogger_close() : HAL_EUNINIT;
  const int after = hal_sdlogger_get_log_number();
  char filename[sizeof("log00000.txt")] = {};
  make_filename(before, filename, sizeof(filename));
  const uint32_t checksum = fnv1a((const uint8_t *)expected, expected_length);

  const int length = snprintf(
      (char *)s_response, sizeof(s_response),
      "JHSD1-WRITE target=%s board=%s runtime=%s eeprom=%d spi=%d init=%d "
      "append0=%d append1=%d append2=%d close=%d before=%d after=%d "
      "file=%s bytes=%lu checksum=%08lx\n",
      HAL_TARGET_NAME, HAL_BOARD_PROFILE_NAME, JH_RUNTIME_NAME,
      (int)s_eeprom_status, (int)s_spi_status, (int)init_status,
      (int)append_status[0], (int)append_status[1], (int)append_status[2],
      (int)close_status, before, after, filename,
      (unsigned long)expected_length, (unsigned long)checksum);
  set_response_length(length);
}

static void handle_verify(void) {
  char expected[256] = {};
  char actual[256] = {};
  const int counter = hal_sdlogger_get_log_number();
  const int log_number = counter - 1;
  const size_t expected_length =
      build_expected_content(log_number, expected, sizeof(expected));
  char filename[sizeof("log00000.txt")] = {};
  make_filename(log_number, filename, sizeof(filename));

  const bool mounted = hal_sd_file_begin(SD_SPI_BUS, SD_CS_PIN);
  hal_sd_file_t file = {};
  const bool opened =
      mounted && hal_sd_file_open(&file, filename, HAL_SD_FILE_READ);
  const size_t file_size = opened ? hal_sd_file_size(&file) : 0u;
  const bool seeked = opened && file_size >= expected_length &&
                      hal_sd_file_seek(&file, file_size - expected_length);
  const size_t read =
      seeked ? hal_sd_file_read(&file, actual, expected_length) : 0u;
  const bool matched =
      read == expected_length && memcmp(actual, expected, expected_length) == 0;
  const uint32_t checksum = fnv1a((const uint8_t *)actual, read);
  const bool closed = opened && hal_sd_file_close(&file);

  const int length = snprintf(
      (char *)s_response, sizeof(s_response),
      "JHSD1-VERIFY target=%s board=%s runtime=%s mount=%u open=%u seek=%u "
      "close=%u counter=%d file=%s size=%lu bytes=%lu expected=%lu "
      "checksum=%08lx match=%u\n",
      HAL_TARGET_NAME, HAL_BOARD_PROFILE_NAME, JH_RUNTIME_NAME,
      mounted ? 1u : 0u, opened ? 1u : 0u, seeked ? 1u : 0u, closed ? 1u : 0u,
      counter, filename, (unsigned long)file_size, (unsigned long)read,
      (unsigned long)expected_length, (unsigned long)checksum,
      matched ? 1u : 0u);
  set_response_length(length);
}

void app_start(void) {
  s_eeprom_status = hal_eeprom_init(HAL_EEPROM_FLASH, EEPROM_BYTES, 0u);
  s_spi_status = hal_spi_init(SD_SPI_BUS, SD_MISO_PIN, SD_MOSI_PIN, SD_SCK_PIN);
}

void app_task0(void) {
  if (s_response_offset < s_response_length) {
    size_t written = 0u;
    (void)hal_usb_cdc_write(s_response + s_response_offset,
                            s_response_length - s_response_offset,
                            USB_WRITE_TIMEOUT_MS, &written);
    s_response_offset += written;
    return;
  }

  uint8_t command = 0u;
  size_t received = 0u;
  if (hal_usb_cdc_read(&command, 1u, &received) != HAL_OK || received != 1u) {
    hal_delay_ms(1u);
    return;
  }

  if (command == (uint8_t)'W') {
    handle_write();
  } else if (command == (uint8_t)'V') {
    handle_verify();
  } else if (command == (uint8_t)'R') {
    (void)hal_watchdog_enable(10u, false);
    hal_delay_ms(100u);
  }
}
