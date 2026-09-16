/*
 * Scan two internal ADC inputs and the temperature sensor continuously with
 * hardware pacing and report block statistics once a second.
 */

#include <hal/analog/hal_adc.h>
#include <hal/core/hal_app.h>
#include <hal/core/hal_target.h>
#include <hal/serial/hal_serial.h>
#include <hal/system/hal_system.h>

/* The standalone native build also discovers this optional source. */
#ifdef HAL_ENABLE_ADC_SCAN
#include <hal/analog/hal_adc_scan.h>

#if HAL_TARGET_IS_RP
#define SCAN_PIN_0 26u
#define SCAN_PIN_1 27u
#else
#define SCAN_PIN_0 0u
#define SCAN_PIN_1 1u
#endif

#define SCAN_PIN_COUNT 3u
#define SCAN_CONVERSION_PERIOD_NS 4000u /* each pin every 12 us */
#define SCAN_BLOCK_FRAMES 400u          /* one block every 4.8 ms */
#define SCAN_REPORT_PERIOD_MS 1000u

static uint16_t s_blocks[2u * SCAN_BLOCK_FRAMES * SCAN_PIN_COUNT]
    __attribute__((aligned(4)));
static hal_status_t s_scan_status = HAL_EUNINIT;
static uint32_t s_last_report_ms = 0u;
static uint32_t s_blocks_taken = 0u;
static uint32_t s_blocks_missed = 0u;
static uint32_t s_last_sequence = 0u;
static uint32_t s_sum[SCAN_PIN_COUNT];
static uint16_t s_min[SCAN_PIN_COUNT];
static uint16_t s_max[SCAN_PIN_COUNT];

static void reset_stats(void) {
  for (uint8_t pin = 0u; pin < SCAN_PIN_COUNT; ++pin) {
    s_sum[pin] = 0u;
    s_min[pin] = UINT16_MAX;
    s_max[pin] = 0u;
  }
  s_blocks_taken = 0u;
  s_blocks_missed = 0u;
}

static void accumulate(const hal_adc_scan_block_t *block) {
  for (uint32_t frame = 0u; frame < block->frames; ++frame) {
    const uint16_t *samples = &block->samples[frame * block->pin_count];
    for (uint8_t pin = 0u; pin < block->pin_count; ++pin) {
      s_sum[pin] += samples[pin];
      if (samples[pin] < s_min[pin]) {
        s_min[pin] = samples[pin];
      }
      if (samples[pin] > s_max[pin]) {
        s_max[pin] = samples[pin];
      }
    }
  }
}

static void report(void) {
  static const uint8_t pins[SCAN_PIN_COUNT] = {SCAN_PIN_0, SCAN_PIN_1,
                                               HAL_ADC_SCAN_PIN_TEMPERATURE};
  if (s_scan_status != HAL_OK) {
    derr("scan: %s", hal_status_to_string(s_scan_status));
    return;
  }
  deb("scan: %lu blocks, %lu missed, frame period %lu ns",
      (unsigned long)s_blocks_taken, (unsigned long)s_blocks_missed,
      (unsigned long)hal_adc_scan_frame_period_ns());
  const uint32_t frames = s_blocks_taken * SCAN_BLOCK_FRAMES;
  for (uint8_t index = 0u; index < SCAN_PIN_COUNT; ++index) {
    const uint8_t position = hal_adc_scan_pin_position(pins[index]);
    if (position >= SCAN_PIN_COUNT || frames == 0u) {
      continue;
    }
    const uint32_t mean = s_sum[position] / frames;
    if (pins[index] == HAL_ADC_SCAN_PIN_TEMPERATURE) {
      deb("  temperature: mean=%lu min=%u max=%u", (unsigned long)mean,
          (unsigned)s_min[position], (unsigned)s_max[position]);
    } else {
      deb("  pin %u: mean=%lu (~%ld mV) min=%u max=%u", (unsigned)pins[index],
          (unsigned long)mean, (long)(mean * 3300UL / 4095UL),
          (unsigned)s_min[position], (unsigned)s_max[position]);
    }
  }
}

void app_start(void) {
  hal_debug_init_default();
  deb("");
  deb("=== JaszczurHAL ADC scan ===");

  hal_adc_scan_config_t config = {0};
  config.pins[0] = SCAN_PIN_0;
  config.pins[1] = SCAN_PIN_1;
  config.pins[2] = HAL_ADC_SCAN_PIN_TEMPERATURE;
  config.pin_count = SCAN_PIN_COUNT;
  config.conversion_period_ns = SCAN_CONVERSION_PERIOD_NS;
  config.buffer = s_blocks;
  config.block_frames = SCAN_BLOCK_FRAMES;
  reset_stats();
  s_scan_status = hal_adc_scan_start(&config);
  if (s_scan_status != HAL_OK) {
    derr("scan start: %s", hal_status_to_string(s_scan_status));
  }
}

void app_task0(void) {
  hal_adc_scan_block_t block;
  if (s_scan_status == HAL_OK && hal_adc_scan_take(&block) == HAL_OK) {
    if (s_last_sequence != 0u && block.sequence != s_last_sequence + 1u) {
      s_blocks_missed += block.sequence - s_last_sequence - 1u;
    }
    s_last_sequence = block.sequence;
    accumulate(&block);
    ++s_blocks_taken;
  }
  if (hal_millis_interval_elapsed_now(&s_last_report_ms,
                                      SCAN_REPORT_PERIOD_MS)) {
    report();
    reset_stats();
  }
  hal_delay_ms(1u);
}

#endif
