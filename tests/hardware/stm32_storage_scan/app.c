/* STM32G474 storage and scan hardware test. Key-value store over the flash
 * EEPROM reservation, persistence across a watchdog reset, and the ADC scan
 * ring while interrupts stay masked and while KV publishes to flash. The
 * fixture drives itself: an odd boot runs the checks, stores its verdict in
 * KV and resets through the watchdog; an even boot confirms what survived and
 * reports every two seconds. The LED (PA5) toggles per scan block during the
 * checks and blinks at 1 Hz while reporting. */

#include <hal/analog/hal_adc_scan.h>
#include <hal/core/hal_app.h>
#include <hal/core/hal_target.h>
#include <hal/gpio/hal_gpio.h>
#include <hal/serial/hal_serial.h>
#include <hal/storage/hal_eeprom.h>
#include <hal/storage/hal_kv.h>
#include <hal/system/hal_sync.h>
#include <hal/system/hal_system.h>

#include <stdbool.h>
#include <stdint.h>
#include <string.h>

#if HAL_TARGET_IS_STM32G474
/* Fixture-only peeks: which half DMA1 channel 3 is filling right now, and
 * the cycle counter for a delay that needs no interrupt. */
#include "hal/impl/stm32g474/port/stm32g474_regs.h"
#endif

#define LED_PIN 5u    /* PA5, LD2 */
#define SCAN_PIN_A 0u /* PA0, Arduino A0 */
#define SCAN_PIN_B 1u /* PA1, Arduino A1 */
#define SCAN_PINS 3u
#define SCAN_CONVERSION_NS 15000u
#define SCAN_BLOCK_FRAMES 444u /* 3 x 15 us x 444 = 20 ms per block */
#define SCAN_BLOCK_SAMPLES (SCAN_BLOCK_FRAMES * SCAN_PINS)
#define SCAN_DMA_CHANNEL 2u /* DMA1 channel 3 */

#define KV_KEY_BOOT 1u
#define KV_KEY_NAME 2u
#define KV_KEY_VERDICT 3u
#define KV_KEY_SCRATCH 4u

#define MASK_RUNS 10u
#define KV_WRITES 4u
#define WARMUP_BLOCKS 5u
#define REPORT_PERIOD_MS 2000u

/* Verdict word stored after the checks. */
#define VERDICT_KV_BOOT (1u << 0)    /* init, counter write, name write */
#define VERDICT_NAME (1u << 1)       /* name read back */
#define VERDICT_SCAN_START (1u << 2) /* scan started */
#define VERDICT_BLOCKS (1u << 3)     /* warm-up blocks arrived */
#define VERDICT_MASK_ALL                                                       \
  (1u << 4) /* every masked run published the right half */
#define VERDICT_KV_SCAN_ALL                                                    \
  (1u << 5) /* every KV write during the scan passed */
#define VERDICT_MASK_SHIFT 8u
#define VERDICT_KV_SHIFT 16u
#define VERDICT_COMMIT_MS_SHIFT 24u

static const char kName[] = "JaszczurHAL stm32 storage scan";

static uint16_t s_ring[2u * SCAN_BLOCK_SAMPLES] __attribute__((aligned(4)));

typedef enum {
  PHASE_CHECKS_WARMUP,
  PHASE_CHECKS_MASK,
  PHASE_CHECKS_KV,
  PHASE_CHECKS_STORE,
  PHASE_REPORT
} phase_t;

static phase_t s_phase;
static uint32_t s_boot;
static hal_status_t s_eeprom_status = HAL_NONE;
static hal_status_t s_kv_status = HAL_NONE;
static hal_status_t s_scan_status = HAL_NONE;
static bool s_name_ok;
static bool s_persist_ok;
static bool s_watchdog_reboot;
static uint32_t s_verdict;
static uint32_t s_stored_verdict;
static uint32_t s_blocks;
static uint32_t s_last_sequence;
static uint32_t s_mask_matches;
static uint32_t s_mask_runs;
static uint32_t s_kv_ok;
static uint32_t s_kv_runs;
static uint32_t s_commit_max_us;
static uint32_t s_step_ms;
static uint32_t s_last_report_ms;
static bool s_led;

static void led(bool on) {
  s_led = on;
  hal_gpio_write(LED_PIN, on);
}

static const uint16_t *half(uint8_t index) {
  return s_ring + ((uint32_t)index * SCAN_BLOCK_SAMPLES);
}

#if HAL_TARGET_IS_STM32G474
static uint8_t half_being_written(void) {
  const uint32_t total = 2u * SCAN_BLOCK_SAMPLES;
  const uint32_t remaining = DMA_CNDTR(DMA1_BASE, SCAN_DMA_CHANNEL);
  const uint32_t written = remaining <= total ? total - remaining : 0u;
  return written >= SCAN_BLOCK_SAMPLES ? 1u : 0u;
}

/* Spin on the cycle counter: the millisecond clock needs its interrupt. */
static void spin_us(uint32_t us) {
  const uint32_t cycles = us * (JH_G474_CORE_CLOCK_HZ / 1000000u);
  const uint32_t start = DWT_CYCCNT;
  while ((uint32_t)(DWT_CYCCNT - start) < cycles) {
    /* wait */
  }
}
#else
static uint8_t half_being_written(void) { return 0u; }
static void spin_us(uint32_t us) { hal_delay_us(us); }
#endif

/* Take every block published so far; true when at least one arrived. */
static bool drain_blocks(void) {
  hal_adc_scan_block_t block;
  bool any = false;
  while (hal_adc_scan_take(&block) == HAL_OK) {
    s_last_sequence = block.sequence;
    ++s_blocks;
    any = true;
    led(!s_led);
  }
  return any;
}

static void init_storage(void) {
  s_eeprom_status = hal_eeprom_init(HAL_EEPROM_FLASH, 0u, 0u);
  if (s_eeprom_status != HAL_OK) {
    derr("EEPROM init failed: %s", hal_status_to_string(s_eeprom_status));
    return;
  }
  s_kv_status = hal_kv_init_ex(0u, HAL_STM32_FLASH_EEPROM_SIZE);
  if (s_kv_status != HAL_OK) {
    derr("KV init failed: %s", hal_status_to_string(s_kv_status));
    return;
  }
  uint32_t boot = 0u;
  hal_status_t status = hal_kv_get_u32_ex(KV_KEY_BOOT, &boot);
  if (status != HAL_OK && status != HAL_ENOENT) {
    s_kv_status = status;
    return;
  }
  s_boot = boot + 1u;
  s_kv_status = hal_kv_set_u32_ex(KV_KEY_BOOT, s_boot);
  if (s_kv_status != HAL_OK) {
    return;
  }
  char name[sizeof(kName)] = {0};
  uint16_t length = 0u;
  status =
      hal_kv_get_blob_ex(KV_KEY_NAME, (uint8_t *)name, sizeof(name), &length);
  s_name_ok = status == HAL_OK && length == sizeof(kName) &&
              memcmp(name, kName, sizeof(kName)) == 0;
  if (!s_name_ok) {
    s_kv_status = hal_kv_set_blob_ex(KV_KEY_NAME, (const uint8_t *)kName,
                                     (uint16_t)sizeof(kName));
    memset(name, 0, sizeof(name));
    status =
        hal_kv_get_blob_ex(KV_KEY_NAME, (uint8_t *)name, sizeof(name), &length);
    s_name_ok = status == HAL_OK && length == sizeof(kName) &&
                memcmp(name, kName, sizeof(kName)) == 0;
  }
  status = hal_kv_get_u32_ex(KV_KEY_VERDICT, &s_stored_verdict);
  if (status != HAL_OK) {
    s_stored_verdict = 0u;
  }
  /* An even boot follows the checks of the boot before: what it stored must
   * be there, along with the name, after the watchdog reset. */
  s_persist_ok = (s_boot % 2u == 0u) && status == HAL_OK && s_name_ok &&
                 (s_stored_verdict & VERDICT_KV_BOOT) != 0u;
}

static void start_scan(void) {
  hal_adc_scan_config_t config;
  memset(&config, 0, sizeof(config));
  config.pins[0] = SCAN_PIN_A;
  config.pins[1] = SCAN_PIN_B;
  config.pins[2] = HAL_ADC_SCAN_PIN_TEMPERATURE;
  config.pin_count = SCAN_PINS;
  config.conversion_period_ns = SCAN_CONVERSION_NS;
  config.buffer = s_ring;
  config.block_frames = SCAN_BLOCK_FRAMES;
  s_scan_status = hal_adc_scan_start(&config);
  if (s_scan_status != HAL_OK) {
    derr("scan start failed: %s", hal_status_to_string(s_scan_status));
  }
}

static uint32_t block_us(void) {
  return (hal_adc_scan_frame_period_ns() / 1000u) * SCAN_BLOCK_FRAMES;
}

/* Interrupts masked for longer than a block, then the block the late
 * interrupt publishes must be the half the channel is not filling. */
static void run_mask_check(uint32_t index) {
  (void)drain_blocks();
  const uint32_t mask_us = (block_us() * (125u + (30u * index))) / 100u;
  hal_critical_section_enter();
  spin_us(mask_us);
  hal_critical_section_exit();
  const uint8_t writing = half_being_written();
  hal_adc_scan_block_t block;
  const hal_status_t status = hal_adc_scan_take(&block);
  const bool match =
      status == HAL_OK && block.samples == half((uint8_t)(1u - writing));
  ++s_mask_runs;
  if (match) {
    ++s_mask_matches;
    s_last_sequence = block.sequence;
    ++s_blocks;
  }
  deb("JHSTM32MASK run=%lu mask_us=%lu take=%d writing=%u published=%d seq=%lu",
      (unsigned long)index, (unsigned long)mask_us, (int)status,
      (unsigned)writing,
      status == HAL_OK ? (block.samples == half(1u) ? 1 : 0) : -1,
      (unsigned long)block.sequence);
}

/* One KV publication to flash while the scan runs: the write must succeed
 * and the ring must go on afterwards. */
static void run_kv_check(uint32_t index) {
  (void)drain_blocks();
  const uint32_t before = s_last_sequence;
  const uint32_t value = 0x5CA10000u + (s_boot << 8) + index;
  const uint32_t started = hal_micros();
  const hal_status_t status = hal_kv_set_u32_ex(KV_KEY_SCRATCH, value);
  const uint32_t elapsed = hal_micros() - started;
  if (elapsed > s_commit_max_us) {
    s_commit_max_us = elapsed;
  }
  uint32_t readback = 0u;
  const hal_status_t back = hal_kv_get_u32_ex(KV_KEY_SCRATCH, &readback);
  hal_delay_ms(50u);
  const bool advanced = drain_blocks() && s_last_sequence > before;
  uint16_t raw = 0u;
  const hal_status_t latest = hal_adc_scan_latest(SCAN_PIN_A, &raw);
  ++s_kv_runs;
  const bool ok = status == HAL_OK && back == HAL_OK && readback == value &&
                  advanced && latest == HAL_OK;
  if (ok) {
    ++s_kv_ok;
  }
  deb("JHSTM32KVSCAN run=%lu set=%d get=%d value_ok=%d elapsed_us=%lu "
      "seq=%lu->%lu latest=%d raw=%u",
      (unsigned long)index, (int)status, (int)back, readback == value ? 1 : 0,
      (unsigned long)elapsed, (unsigned long)before,
      (unsigned long)s_last_sequence, (int)latest, (unsigned)raw);
}

static void store_verdict(void) {
  s_verdict = 0u;
  if (s_eeprom_status == HAL_OK && s_kv_status == HAL_OK) {
    s_verdict |= VERDICT_KV_BOOT;
  }
  if (s_name_ok) {
    s_verdict |= VERDICT_NAME;
  }
  if (s_scan_status == HAL_OK) {
    s_verdict |= VERDICT_SCAN_START;
  }
  if (s_blocks >= WARMUP_BLOCKS) {
    s_verdict |= VERDICT_BLOCKS;
  }
  if (s_mask_matches == MASK_RUNS) {
    s_verdict |= VERDICT_MASK_ALL;
  }
  if (s_kv_ok == KV_WRITES) {
    s_verdict |= VERDICT_KV_SCAN_ALL;
  }
  const uint32_t commit_ms = (s_commit_max_us + 999u) / 1000u;
  s_verdict |= (s_mask_matches & 0xFFu) << VERDICT_MASK_SHIFT;
  s_verdict |= (s_kv_ok & 0xFFu) << VERDICT_KV_SHIFT;
  s_verdict |= (commit_ms > 255u ? 255u : commit_ms) << VERDICT_COMMIT_MS_SHIFT;
  const hal_status_t status = hal_kv_set_u32_ex(KV_KEY_VERDICT, s_verdict);
  deb("JHSTM32P1 boot=%lu verdict=0x%08lx stored=%d mask=%lu/%u kv=%lu/%u "
      "commit_max_us=%lu blocks=%lu",
      (unsigned long)s_boot, (unsigned long)s_verdict, (int)status,
      (unsigned long)s_mask_matches, (unsigned)MASK_RUNS,
      (unsigned long)s_kv_ok, (unsigned)KV_WRITES,
      (unsigned long)s_commit_max_us, (unsigned long)s_blocks);
  hal_delay_ms(20u);
  (void)hal_watchdog_enable(10u, false);
  hal_delay_ms(100u);
}

static void report(void) {
  hal_kv_stats_t stats;
  const bool stats_ok = hal_kv_get_stats(&stats);
  deb("JHSTM32REPORT boot=%lu verdict=0x%08lx persist=%d wdg=%d keys=%u "
      "cap=%u gen=%lu scan=%d blocks=%lu period_ns=%lu",
      (unsigned long)s_boot, (unsigned long)s_stored_verdict,
      s_persist_ok ? 1 : 0, s_watchdog_reboot ? 1 : 0,
      stats_ok ? (unsigned)stats.key_count : 0u,
      stats_ok ? (unsigned)stats.key_capacity : 0u,
      stats_ok ? (unsigned long)stats.generation : 0ul,
      s_scan_status == HAL_OK ? 1 : 0, (unsigned long)s_blocks,
      (unsigned long)hal_adc_scan_frame_period_ns());
}

void app_start(void) {
  hal_gpio_set_mode(LED_PIN, HAL_GPIO_OUTPUT);
  led(true);
  s_watchdog_reboot = hal_watchdog_caused_reboot();
  init_storage();
  start_scan();
  deb("JHSTM32BOOT boot=%lu eeprom=%d kv=%d name=%d scan=%d wdg=%d "
      "period_ns=%lu",
      (unsigned long)s_boot, (int)s_eeprom_status, (int)s_kv_status,
      s_name_ok ? 1 : 0, (int)s_scan_status, s_watchdog_reboot ? 1 : 0,
      (unsigned long)hal_adc_scan_frame_period_ns());
  const bool checks =
      (s_boot % 2u) == 1u && s_kv_status == HAL_OK && s_scan_status == HAL_OK;
  s_phase = checks ? PHASE_CHECKS_WARMUP : PHASE_REPORT;
  s_step_ms = hal_millis();
}

void app_task0(void) {
  const uint32_t now = hal_millis();
  switch (s_phase) {
  case PHASE_CHECKS_WARMUP:
    (void)drain_blocks();
    if (s_blocks >= WARMUP_BLOCKS || now - s_step_ms > 2000u) {
      deb("JHSTM32SCAN blocks=%lu seq=%lu block_us=%lu",
          (unsigned long)s_blocks, (unsigned long)s_last_sequence,
          (unsigned long)block_us());
      s_phase = PHASE_CHECKS_MASK;
    }
    break;
  case PHASE_CHECKS_MASK:
    if (s_mask_runs < MASK_RUNS) {
      run_mask_check(s_mask_runs);
      hal_delay_ms(30u);
    } else {
      s_phase = PHASE_CHECKS_KV;
    }
    break;
  case PHASE_CHECKS_KV:
    if (s_kv_runs < KV_WRITES) {
      run_kv_check(s_kv_runs);
    } else {
      s_phase = PHASE_CHECKS_STORE;
    }
    break;
  case PHASE_CHECKS_STORE:
    store_verdict();
    s_phase = PHASE_REPORT; /* not reached: the watchdog resets */
    break;
  case PHASE_REPORT:
  default:
    (void)drain_blocks();
    if (now - s_last_report_ms >= REPORT_PERIOD_MS) {
      s_last_report_ms = now;
      report();
    }
    led(((now / 500u) % 2u) == 0u);
    hal_delay_ms(5u);
    break;
  }
}
