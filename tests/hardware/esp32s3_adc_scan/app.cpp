// ESP32-S3 ADC scan hardware test on the Waveshare ESP32-S3-Zero. The scan
// runs on GPIO3 and GPIO4; the checks judge the ring's geometry and freshness,
// not the voltages: GPIO3 is pulled down and up internally and the newest
// sample has to follow, read through hal_adc_scan_latest() and through
// hal_adc_read() without any block being taken in between. Flash sector
// writes (the last sector of the NVS partition, which nothing else uses
// here) run while the scan continues, then the scan is stopped and
// restarted. The
// WS2812 on GPIO21 is blue during the checks, green on PASS, red on FAIL.

#include "hal/analog/hal_adc.h"
#include "hal/analog/hal_adc_scan.h"
#include "hal/core/hal_app.h"
#include "hal/core/hal_status.h"
#include "hal/core/hal_target.h"
#include "hal/gpio/hal_gpio.h"
#include "hal/gpio/hal_rgb_led.h"
#include "hal/serial/hal_serial.h"
#include "hal/system/hal_system.h"
#include "jh_board_config.h"
#include "jh_link_contract.h"

#include <esp_flash.h>
#include <sdkconfig.h>

#include <inttypes.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#if !HAL_TARGET_IS_ESP32_S3
#error "The ESP32-S3 ADC scan probe requires the exact esp32s3 target"
#endif

#if !HAL_BOARD_IS_WAVESHARE_ESP32_S3_ZERO
#error "The ESP32-S3 ADC scan probe requires waveshare-esp32-s3-zero"
#endif

namespace {

constexpr uint8_t kPinA = 3u; /* ADC1 channel 2, pulled down and up */
constexpr uint8_t kPinB = 4u; /* ADC1 channel 3 */
constexpr uint8_t kPinUnscanned = 5u;
constexpr uint8_t kLedPin = 21u;
constexpr uint8_t kPins = 2u;
constexpr uint32_t kConversionNs = 12000u;
constexpr uint32_t kBlockFrames = 400u; /* 2 x 12 us x 400 = 9.6 ms */
constexpr uint32_t kBlockWindowMs = 300u;
constexpr uint32_t kMinBlocks = 20u;
constexpr uint32_t kSettleMs = 30u;
constexpr uint32_t kFlashWrites = 4u;
constexpr uint32_t kFlashSector = 4096u;
constexpr uint32_t kReportPeriodMs = 500u;
constexpr int kLowLimit = 600;
constexpr int kHighLimit = 3400;

uint16_t s_ring[2u * kBlockFrames * kPins] __attribute__((aligned(4)));

struct Results {
  bool start;
  bool blocks;
  bool fresh;
  bool unscanned;
  bool flash;
  bool restart;
  uint32_t period_ns;
  uint32_t block_count;
  uint32_t last_sequence;
  int lo;
  int hi;
  int lo_read;
  int hi_read;
  int unscanned_value;
  uint32_t flash_writes;
  uint32_t flash_max_us;
  uint32_t flash_lost;
  uint32_t flash_fail;   /* bit 0 erase, 1 write, 2 read/compare, 3 stalled,
                            4 latest, 5 no partition */
  uint32_t restart_step; /* first failing step, 0 when all passed */
  int oneshot;           /* one-shot read of the pulled-up pin after stop */
  bool done;
};

Results s_results = {};
uint32_t s_last_report_ms = 0u;
uint32_t s_sequence = 0u;

hal_status_t start_scan(void) {
  hal_adc_scan_config_t config;
  memset(&config, 0, sizeof(config));
  config.pins[0] = kPinA;
  config.pins[1] = kPinB;
  config.pin_count = kPins;
  config.conversion_period_ns = kConversionNs;
  config.buffer = s_ring;
  config.block_frames = kBlockFrames;
  return hal_adc_scan_start(&config);
}

/* Take every block published so far; counts blocks and sequence gaps. */
uint32_t drain_blocks(uint32_t *lost) {
  hal_adc_scan_block_t block;
  uint32_t taken = 0u;
  while (hal_adc_scan_take(&block) == HAL_OK) {
    if (s_results.last_sequence != 0u && lost != nullptr &&
        block.sequence > s_results.last_sequence + 1u) {
      *lost += block.sequence - s_results.last_sequence - 1u;
    }
    s_results.last_sequence = block.sequence;
    ++taken;
  }
  return taken;
}

bool check_blocks(void) {
  const uint32_t started = hal_millis();
  uint32_t previous_us = 0u;
  bool ordered = true;
  while (hal_millis() - started < kBlockWindowMs) {
    hal_adc_scan_block_t block;
    if (hal_adc_scan_take(&block) == HAL_OK) {
      ordered = ordered && block.sequence > s_results.last_sequence &&
                block.frames == kBlockFrames && block.pin_count == kPins &&
                (previous_us == 0u ||
                 (int32_t)(block.completed_us - previous_us) > 0);
      previous_us = block.completed_us;
      s_results.last_sequence = block.sequence;
      ++s_results.block_count;
    }
    hal_delay_ms(1u);
  }
  return ordered && s_results.block_count >= kMinBlocks;
}

/* The newest sample follows the pin without a single take() in between. */
bool check_freshness(void) {
  uint16_t raw = 0u;
  hal_gpio_set_mode(kPinA, HAL_GPIO_INPUT_PULLDOWN);
  hal_delay_ms(kSettleMs);
  const bool low_ok = hal_adc_scan_latest(kPinA, &raw) == HAL_OK;
  s_results.lo = low_ok ? (int)raw : -1;
  hal_gpio_set_mode(kPinA, HAL_GPIO_INPUT_PULLUP);
  hal_delay_ms(kSettleMs);
  const bool high_ok = hal_adc_scan_latest(kPinA, &raw) == HAL_OK;
  s_results.hi = high_ok ? (int)raw : -1;
  s_results.hi_read = hal_adc_read(kPinA);
  hal_gpio_set_mode(kPinA, HAL_GPIO_INPUT_PULLDOWN);
  hal_delay_ms(kSettleMs);
  s_results.lo_read = hal_adc_read(kPinA);
  return low_ok && high_ok && s_results.lo < kLowLimit &&
         s_results.hi > kHighLimit && s_results.hi_read > kHighLimit &&
         s_results.lo_read < kLowLimit;
}

bool check_unscanned(void) {
  s_results.unscanned_value = hal_adc_read(kPinUnscanned);
  return s_results.unscanned_value == 0;
}

/* The NVS data partition from the table in flash (32-byte entries behind
 * the 0x50AA magic); the fixture reads the table itself because the main
 * component has the flash driver but not the partition API. */
bool find_nvs_partition(uint32_t *offset, uint32_t *size) {
  /* One entry at a time: the checks run on the main task's small stack. */
  for (uint32_t at = 0u; at < 0xC00u; at += 32u) {
    uint8_t entry[32];
    if (esp_flash_read(nullptr, entry, CONFIG_PARTITION_TABLE_OFFSET + at,
                       sizeof(entry)) != ESP_OK) {
      return false;
    }
    if (entry[0] != 0xAAu || entry[1] != 0x50u) {
      break;
    }
    if (entry[2] == 0x01u && entry[3] == 0x02u) { /* data, nvs */
      memcpy(offset, entry + 4, sizeof(*offset));
      memcpy(size, entry + 8, sizeof(*size));
      return true;
    }
  }
  return false;
}

/* Flash writes while the scan runs: each one erases and programs the last
 * sector of the NVS partition (unused by this probe) and reads it back; the
 * ring must go on, and lost blocks, if any, are counted from the sequence.
 * The cache is off while the flash is busy, which is what a storage backend
 * will do to the scan later. */
bool check_flash_during_scan(void) {
  uint32_t partition_offset = 0u;
  uint32_t partition_size = 0u;
  if (!find_nvs_partition(&partition_offset, &partition_size) ||
      partition_size < kFlashSector) {
    s_results.flash_fail |= 1u << 5;
    return false;
  }
  const uint32_t offset = partition_offset + partition_size - kFlashSector;
  (void)drain_blocks(&s_results.flash_lost);
  bool ok = true;
  for (uint32_t i = 0u; i < kFlashWrites; ++i) {
    static uint8_t pattern[64];
    static uint8_t readback[64];
    memset(pattern, (int)(0xA0u + i), sizeof(pattern));
    const uint32_t before = s_results.last_sequence;
    const uint32_t started = hal_micros();
    const esp_err_t erased =
        esp_flash_erase_region(nullptr, offset, kFlashSector);
    const esp_err_t written =
        esp_flash_write(nullptr, pattern, offset, sizeof(pattern));
    const uint32_t elapsed = hal_micros() - started;
    if (elapsed > s_results.flash_max_us) {
      s_results.flash_max_us = elapsed;
    }
    const esp_err_t read =
        esp_flash_read(nullptr, readback, offset, sizeof(readback));
    hal_delay_ms(30u);
    (void)drain_blocks(&s_results.flash_lost);
    uint16_t raw = 0u;
    const bool advanced = s_results.last_sequence > before;
    const bool latest_ok = hal_adc_scan_latest(kPinA, &raw) == HAL_OK;
    const bool compared =
        read == ESP_OK && memcmp(pattern, readback, sizeof(pattern)) == 0;
    if (erased == ESP_OK && written == ESP_OK && compared && advanced &&
        latest_ok) {
      ++s_results.flash_writes;
    } else {
      ok = false;
      s_results.flash_fail |= (erased != ESP_OK ? 1u : 0u) |
                              (written != ESP_OK ? 2u : 0u) |
                              (!compared ? 4u : 0u) | (!advanced ? 8u : 0u) |
                              (!latest_ok ? 16u : 0u);
    }
  }
  return ok;
}

/* Stop hands the converter back to one-shot reads; a second start works. */
bool check_restart(void) {
  if (hal_adc_scan_stop() != HAL_OK || hal_adc_scan_is_running()) {
    s_results.restart_step = 1u;
    return false;
  }
  // The first one-shot read of a pin configures its pad and drops the pull,
  // so prime the channel before asking for the pulled-up level.
  (void)hal_adc_read(kPinA);
  hal_gpio_set_mode(kPinA, HAL_GPIO_INPUT_PULLUP);
  hal_delay_ms(5u);
  const int oneshot = hal_adc_read(kPinA);
  s_results.oneshot = oneshot;
  if (oneshot <= kHighLimit) {
    s_results.restart_step = 2u;
    return false;
  }
  if (start_scan() != HAL_OK) {
    s_results.restart_step = 3u;
    return false;
  }
  hal_delay_ms(kSettleMs);
  hal_adc_scan_block_t block;
  if (hal_adc_scan_take(&block) != HAL_OK) {
    s_results.restart_step = 4u;
    (void)hal_adc_scan_stop();
    return false;
  }
  if (hal_adc_scan_stop() != HAL_OK) {
    s_results.restart_step = 5u;
    return false;
  }
  return true;
}

void report(void) {
  ++s_sequence;
  const bool pass = s_results.done && s_results.start && s_results.blocks &&
                    s_results.fresh && s_results.unscanned && s_results.flash &&
                    s_results.restart;
  char line[512] = {};
  (void)snprintf(
      line, sizeof(line),
      "JH_ESP32_ADC_SCAN sequence=%" PRIu32 " target=%s board=%s start=%u"
      " period_ns=%" PRIu32 " blocks=%" PRIu32 " blocks_ok=%u fresh=%u lo=%d"
      " hi=%d lo_read=%d hi_read=%d unscanned=%d unscanned_ok=%u flash=%u"
      " flash_writes=%" PRIu32 " flash_max_us=%" PRIu32 " flash_lost=%" PRIu32
      " flash_fail=%" PRIu32 " restart=%u restart_step=%" PRIu32
      " oneshot=%d status=%s",
      s_sequence, HAL_TARGET_DESCRIPTOR_ID, HAL_BOARD_PROFILE_NAME,
      s_results.start ? 1u : 0u, s_results.period_ns, s_results.block_count,
      s_results.blocks ? 1u : 0u, s_results.fresh ? 1u : 0u, s_results.lo,
      s_results.hi, s_results.lo_read, s_results.hi_read,
      s_results.unscanned_value, s_results.unscanned ? 1u : 0u,
      s_results.flash ? 1u : 0u, s_results.flash_writes, s_results.flash_max_us,
      s_results.flash_lost, s_results.flash_fail, s_results.restart ? 1u : 0u,
      s_results.restart_step, s_results.oneshot, pass ? "PASS" : "FAIL");
  hal_serial_println(line);
}

} // namespace

extern "C" void app_start(void) {
  JH_BOARD_CONTRACT_SYMBOL();
  hal_serial_begin(115200u);
  hal_serial_set_flush(true);
  hal_fault_subsystem_init();
  (void)hal_rgb_led_init_ex(kLedPin, 1u, HAL_RGB_LED_PIXEL_GRB_KHZ800);
  hal_rgb_led_set_brightness(16u);
  (void)hal_rgb_led_set_color(HAL_RGB_LED_BLUE);
  hal_adc_set_resolution(12u);

  s_results.start = start_scan() == HAL_OK;
  s_results.period_ns = hal_adc_scan_frame_period_ns();
  if (s_results.start) {
    s_results.blocks = check_blocks();
    s_results.fresh = check_freshness();
    s_results.unscanned = check_unscanned();
    s_results.flash = check_flash_during_scan();
    s_results.restart = check_restart();
  }
  s_results.done = true;
  const bool pass = s_results.start && s_results.blocks && s_results.fresh &&
                    s_results.unscanned && s_results.flash && s_results.restart;
  (void)hal_rgb_led_set_color(pass ? HAL_RGB_LED_GREEN : HAL_RGB_LED_RED);
  (void)hal_watchdog_enable(30000u, false);
}

extern "C" void app_task0(void) {
  hal_watchdog_feed();
  hal_alive_mark();
  const uint32_t now = hal_millis();
  if (hal_millis_interval_elapsed(now, &s_last_report_ms, kReportPeriodMs)) {
    report();
  }
  hal_delay_ms(5u);
}
