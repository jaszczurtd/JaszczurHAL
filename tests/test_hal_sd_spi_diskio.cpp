/* Host test of the FatFs SD SPI adapter on a scripted card model.
 *
 * The model stands in for hal_spi, hal_gpio and hal_millis: it answers the SD
 * commands the adapter issues, records the chip-select level each command was
 * sent at and can fail single bus transfers. The adapter therefore runs its
 * production command flow; the test only sees what the card would see. */
#include "hal/gpio/hal_gpio.h"
#include "hal/spi/hal_spi.h"
#include "hal/storage/filesystem/ff16/diskio.h"
#include "hal/storage/filesystem/ff16/ff.h"
#include "hal/storage/filesystem/ff16/hal_sd_spi_diskio.h"
#include "hal/system/hal_system.h"
#include "utils/unity.h"

#include <string.h>

namespace {

constexpr uint8_t kCommandMask = 0xC0u;
constexpr uint8_t kCommandStart = 0x40u;
constexpr uint8_t kCmd0 = 0u;
constexpr uint8_t kCmd8 = 8u;
constexpr uint8_t kCmd9 = 9u;
constexpr uint8_t kCmd17 = 17u;
constexpr uint8_t kCmd24 = 24u;
constexpr uint8_t kCmd55 = 55u;
constexpr uint8_t kCmd58 = 58u;
constexpr uint8_t kAcmd41 = 41u;
constexpr uint8_t kDataToken = 0xFEu;
constexpr uint8_t kDataAccepted = 0x05u;
constexpr size_t kSector = 512u;
constexpr uint32_t kCsdSectors = 16384u; /* CSD v2 with C_SIZE 15 */

struct card_model_t {
  bool cs_high;
  int lock_depth;
  bool fail_begin;
  bool fail_payload_read; /* hal_spi_transfer_txrx fails */
  bool fail_block_write;  /* hal_spi_write fails */
  uint8_t command[6];
  size_t command_bytes;
  uint8_t reply[32];
  size_t reply_head;
  size_t reply_count;
  unsigned commands;
  unsigned commands_while_unselected;
  uint8_t last_command;
  uint32_t last_argument;
  uint32_t read_arguments[4];
  size_t reads;
  uint8_t written[kSector];
  size_t written_length;
  bool selected_without_lock;
  uint32_t millis;
};

card_model_t g_card;

void queue_reply(uint8_t byte) {
  TEST_ASSERT_LESS_THAN_size_t(sizeof(g_card.reply), g_card.reply_count);
  g_card
      .reply[(g_card.reply_head + g_card.reply_count) % sizeof(g_card.reply)] =
      byte;
  ++g_card.reply_count;
}

/* An idle MISO line reads 0xFF. */
uint8_t next_reply(void) {
  if (g_card.reply_count == 0u) {
    return 0xFFu;
  }
  const uint8_t byte = g_card.reply[g_card.reply_head];
  g_card.reply_head = (g_card.reply_head + 1u) % sizeof(g_card.reply);
  --g_card.reply_count;
  return byte;
}

/* Replies queued after the sixth command byte: one idle byte, R1, then the
 * bytes the adapter reads one at a time. Multi-byte payloads are served by
 * the buffer transfer below. */
void answer_command(void) {
  const uint8_t command = (uint8_t)(g_card.command[0] & 0x3Fu);
  const uint32_t argument = ((uint32_t)g_card.command[1] << 24) |
                            ((uint32_t)g_card.command[2] << 16) |
                            ((uint32_t)g_card.command[3] << 8) |
                            (uint32_t)g_card.command[4];
  ++g_card.commands;
  g_card.last_command = command;
  g_card.last_argument = argument;
  queue_reply(0xFFu);
  switch (command) {
  case kCmd0:
  case kCmd55:
    queue_reply(0x01u); /* idle */
    break;
  case kCmd8: {
    static const uint8_t r7[] = {0x01u, 0x00u, 0x00u, 0x01u, 0xAAu};
    for (size_t index = 0u; index < sizeof(r7); ++index) {
      queue_reply(r7[index]);
    }
    break;
  }
  case kAcmd41:
    queue_reply(0x00u);
    break;
  case kCmd58: {
    static const uint8_t ocr[] = {0x00u, 0xC0u, 0x00u, 0x00u, 0x00u};
    for (size_t index = 0u; index < sizeof(ocr); ++index) {
      queue_reply(ocr[index]);
    }
    break;
  }
  case kCmd9: {
    static const uint8_t csd[16] = {0x40u, 0u,  0u, 0u, 0u, 0u, 0u, 0u,
                                    0u,    15u, 0u, 0u, 0u, 0u, 0u, 0u};
    queue_reply(0x00u);
    queue_reply(kDataToken);
    for (size_t index = 0u; index < sizeof(csd); ++index) {
      queue_reply(csd[index]);
    }
    break;
  }
  case kCmd17:
    if (g_card.reads <
        sizeof(g_card.read_arguments) / sizeof(g_card.read_arguments[0])) {
      g_card.read_arguments[g_card.reads] = argument;
    }
    ++g_card.reads;
    queue_reply(0x00u);
    queue_reply(kDataToken);
    break;
  case kCmd24:
    queue_reply(0x00u);
    break;
  default:
    queue_reply(0x04u); /* illegal command */
    break;
  }
}

void reset_card(void) {
  memset(&g_card, 0, sizeof(g_card));
  g_card.cs_high = true;
}

} // namespace

extern "C" void hal_gpio_set_mode(uint8_t pin, hal_gpio_mode_t mode) {
  (void)pin;
  (void)mode;
}

extern "C" void hal_gpio_write(uint8_t pin, bool high) {
  (void)pin;
  g_card.cs_high = high;
  if (!high && g_card.lock_depth == 0) {
    g_card.selected_without_lock = true;
  }
}

extern "C" void hal_spi_lock(uint8_t bus) {
  (void)bus;
  ++g_card.lock_depth;
}

extern "C" void hal_spi_unlock(uint8_t bus) {
  (void)bus;
  --g_card.lock_depth;
}

extern "C" hal_status_t
hal_spi_begin_transaction(uint8_t bus, const hal_spi_settings_t *settings) {
  (void)bus;
  (void)settings;
  return g_card.fail_begin ? HAL_EIO : HAL_OK;
}

extern "C" hal_status_t hal_spi_end_transaction(uint8_t bus) {
  (void)bus;
  return HAL_OK;
}

extern "C" hal_status_t hal_spi_transfer_ex(uint8_t bus, uint8_t data,
                                            uint8_t *out_received) {
  (void)bus;
  *out_received = 0xFFu;
  const bool command_byte = (data & kCommandMask) == kCommandStart;
  if (g_card.cs_high) {
    /* A deselected card ignores everything; count what it missed. */
    if (command_byte) {
      ++g_card.commands_while_unselected;
    }
    return HAL_OK;
  }
  if (g_card.command_bytes > 0u) {
    g_card.command[g_card.command_bytes++] = data;
    if (g_card.command_bytes == sizeof(g_card.command)) {
      g_card.command_bytes = 0u;
      answer_command();
    }
    return HAL_OK;
  }
  if (command_byte) {
    g_card.command[0] = data;
    g_card.command_bytes = 1u;
    return HAL_OK;
  }
  *out_received = next_reply();
  return HAL_OK;
}

/* Register payloads come from the reply queue; a sector read fills the
 * pattern (block address + offset) unless the transfer is set to fail. */
extern "C" hal_status_t hal_spi_transfer_txrx(uint8_t bus, const uint8_t *tx,
                                              uint8_t *rx, size_t len) {
  (void)bus;
  (void)tx;
  if (g_card.cs_high) {
    return HAL_OK;
  }
  if (len <= g_card.reply_count) {
    for (size_t index = 0u; index < len; ++index) {
      rx[index] = next_reply();
    }
    return HAL_OK;
  }
  if (g_card.fail_payload_read) {
    return HAL_EIO;
  }
  for (size_t index = 0u; index < len; ++index) {
    rx[index] = (uint8_t)(g_card.last_argument + index);
  }
  return HAL_OK;
}

extern "C" hal_status_t hal_spi_write(uint8_t bus, const uint8_t *data,
                                      size_t len) {
  (void)bus;
  if (g_card.fail_block_write) {
    return HAL_EIO;
  }
  TEST_ASSERT_EQUAL_size_t(kSector, len);
  memcpy(g_card.written, data, len);
  g_card.written_length = len;
  /* Two CRC bytes are clocked before the data response is read. */
  queue_reply(0xFFu);
  queue_reply(0xFFu);
  queue_reply(kDataAccepted);
  return HAL_OK;
}

extern "C" uint32_t hal_millis(void) { return ++g_card.millis; }

void setUp(void) {
  reset_card();
  hal_sd_spi_diskio_configure_pins(0u, 5u);
  TEST_ASSERT_EQUAL_UINT(0u, disk_initialize(HAL_SD_SPI_DRIVE));
}

void tearDown(void) {
  TEST_ASSERT_FALSE(g_card.selected_without_lock);
  TEST_ASSERT_TRUE(g_card.cs_high);
  TEST_ASSERT_EQUAL_INT(0, g_card.lock_depth);
}

void test_initialisation_detects_an_sdhc_card_with_the_card_selected(void) {
  TEST_ASSERT_TRUE(hal_sd_spi_diskio_is_initialized());
  TEST_ASSERT_EQUAL_INT(HAL_SD_SPI_CARD_SDHC, hal_sd_spi_diskio_card_type());
  TEST_ASSERT_EQUAL_UINT32(kCsdSectors, hal_sd_spi_diskio_sector_count());
  TEST_ASSERT_EQUAL_UINT(0u, g_card.commands_while_unselected);
  TEST_ASSERT_EQUAL_UINT(0u, disk_status(HAL_SD_SPI_DRIVE));
}

/* Audit F03: the first CMD17 of a read went out with CS high, so a real
 * card never answered and every read failed. */
void test_read_selects_the_card_before_its_first_command(void) {
  uint8_t buffer[kSector] = {};
  TEST_ASSERT_EQUAL_INT(RES_OK, disk_read(HAL_SD_SPI_DRIVE, buffer, 7u, 1u));
  TEST_ASSERT_EQUAL_UINT(0u, g_card.commands_while_unselected);
  TEST_ASSERT_EQUAL_UINT8(kCmd17, g_card.last_command);
  TEST_ASSERT_EQUAL_UINT32(7u, g_card.last_argument); /* SDHC: block index */
  for (size_t index = 0u; index < kSector; ++index) {
    TEST_ASSERT_EQUAL_UINT8((uint8_t)(7u + index), buffer[index]);
  }
}

void test_multi_sector_read_addresses_each_sector(void) {
  static uint8_t buffer[2u * kSector];
  memset(buffer, 0, sizeof(buffer));
  TEST_ASSERT_EQUAL_INT(RES_OK, disk_read(HAL_SD_SPI_DRIVE, buffer, 7u, 2u));
  TEST_ASSERT_EQUAL_UINT(0u, g_card.commands_while_unselected);
  TEST_ASSERT_EQUAL_size_t(2u, g_card.reads);
  TEST_ASSERT_EQUAL_UINT32(7u, g_card.read_arguments[0]);
  TEST_ASSERT_EQUAL_UINT32(8u, g_card.read_arguments[1]);
  TEST_ASSERT_EQUAL_UINT8(7u, buffer[0]);
  TEST_ASSERT_EQUAL_UINT8(8u, buffer[kSector]);
}

void test_write_selects_the_card_and_sends_the_block(void) {
  uint8_t block[kSector];
  for (size_t index = 0u; index < kSector; ++index) {
    block[index] = (uint8_t)(index * 3u);
  }
  TEST_ASSERT_EQUAL_INT(RES_OK, disk_write(HAL_SD_SPI_DRIVE, block, 9u, 1u));
  TEST_ASSERT_EQUAL_UINT(0u, g_card.commands_while_unselected);
  TEST_ASSERT_EQUAL_UINT8(kCmd24, g_card.last_command);
  TEST_ASSERT_EQUAL_UINT32(9u, g_card.last_argument);
  TEST_ASSERT_EQUAL_size_t(kSector, g_card.written_length);
  TEST_ASSERT_EQUAL_MEMORY(block, g_card.written, kSector);
}

/* Audit F03: a failed 512-byte transfer after a valid data token was
 * reported as RES_OK with the stale buffer. */
void test_payload_transfer_error_fails_the_read(void) {
  uint8_t buffer[kSector];
  memset(buffer, 0xEE, sizeof(buffer));
  g_card.fail_payload_read = true;
  TEST_ASSERT_EQUAL_INT(RES_ERROR, disk_read(HAL_SD_SPI_DRIVE, buffer, 7u, 1u));
  TEST_ASSERT_EQUAL_UINT8(0xEEu, buffer[0]);
  TEST_ASSERT_EQUAL_UINT8(0xEEu, buffer[kSector - 1u]);
}

void test_block_transfer_error_fails_the_write(void) {
  uint8_t block[kSector] = {};
  g_card.fail_block_write = true;
  TEST_ASSERT_EQUAL_INT(RES_ERROR, disk_write(HAL_SD_SPI_DRIVE, block, 9u, 1u));
  TEST_ASSERT_EQUAL_size_t(0u, g_card.written_length);
}

void test_bus_configuration_error_fails_and_releases_the_bus(void) {
  uint8_t buffer[kSector] = {};
  g_card.fail_begin = true;
  g_card.commands = 0u;
  TEST_ASSERT_EQUAL_INT(RES_ERROR, disk_read(HAL_SD_SPI_DRIVE, buffer, 7u, 1u));
  TEST_ASSERT_EQUAL_INT(RES_ERROR,
                        disk_write(HAL_SD_SPI_DRIVE, buffer, 7u, 1u));
  TEST_ASSERT_EQUAL_INT(RES_ERROR,
                        disk_ioctl(HAL_SD_SPI_DRIVE, CTRL_SYNC, NULL));
  TEST_ASSERT_EQUAL_UINT(0u, g_card.commands);
  TEST_ASSERT_EQUAL_INT(0, g_card.lock_depth);
}

void test_sync_waits_for_the_card_with_the_card_selected(void) {
  TEST_ASSERT_EQUAL_INT(RES_OK, disk_ioctl(HAL_SD_SPI_DRIVE, CTRL_SYNC, NULL));
  TEST_ASSERT_EQUAL_UINT(0u, g_card.commands_while_unselected);
}

int main(void) {
  UNITY_BEGIN();
  RUN_TEST(test_initialisation_detects_an_sdhc_card_with_the_card_selected);
  RUN_TEST(test_read_selects_the_card_before_its_first_command);
  RUN_TEST(test_multi_sector_read_addresses_each_sector);
  RUN_TEST(test_write_selects_the_card_and_sends_the_block);
  RUN_TEST(test_payload_transfer_error_fails_the_read);
  RUN_TEST(test_block_transfer_error_fails_the_write);
  RUN_TEST(test_bus_configuration_error_fails_and_releases_the_bus);
  RUN_TEST(test_sync_waits_for_the_card_with_the_card_selected);
  return UNITY_END();
}
