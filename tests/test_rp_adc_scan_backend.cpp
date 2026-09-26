// The RP2040 ADC scan backend on a simulated DMA, DMA_IRQ_0 and ADC FIFO:
// the ring must stay inside its buffer whatever the interrupt does, the
// interrupt must publish the newest complete half, and the newest-sample
// reader must never hand out an unwritten cell or a sample older than the
// newest complete frame the ring held when the read began, whichever
// register access the DMA moves between.

#include "hal/analog/jh_adc_scan_backend.h"
#include "hal/impl/rp2040/rp2040_adc_shared.h"
#include "hal/system/hal_system.h"
#include "hardware/dma.h"
#include "hardware/irq.h"
#include "hardware/sync.h"
#include "rp2040_dma_fake.h"
#include "utils/unity.h"

#include <string.h>

#define PINS 3u
#define FRAMES 4u
#define BLOCK (FRAMES * PINS)
#define GUARD 8u

static struct {
  uint16_t before[GUARD];
  uint16_t ring[2u * BLOCK];
  uint16_t after[GUARD];
} s_memory __attribute__((aligned(4)));

static uint32_t s_micros;
static uint32_t s_marker_calls;
static uint32_t s_marker_base = 500u;
static uint8_t s_positions[HAL_ADC_SCAN_MAX_PINS];
static uint32_t s_period_ns;

extern "C" uint32_t hal_micros(void) { return s_micros; }

static uint32_t marker(void *user) {
  ++s_marker_calls;
  return *(const uint32_t *)user + s_marker_calls;
}

static void idle_handler(void) {}

static hal_adc_scan_config_t config(void) {
  hal_adc_scan_config_t cfg = {};
  cfg.pins[0] = 26u;
  cfg.pins[1] = 27u;
  cfg.pins[2] = 28u;
  cfg.pin_count = PINS;
  cfg.conversion_period_ns = 2000u;
  cfg.buffer = s_memory.ring;
  cfg.block_frames = FRAMES;
  cfg.marker = marker;
  cfg.marker_user = &s_marker_base;
  return cfg;
}

static void start(void) {
  const hal_adc_scan_config_t cfg = config();
  TEST_ASSERT_EQUAL_INT(HAL_OK,
                        jh_adc_scan_start(&cfg, s_positions, &s_period_ns));
}

// Cells the DMA never wrote keep a poison no sample index reaches here.
#define UNWRITTEN 0xFFFFu

static void fresh_ring(void) {
  (void)jh_adc_scan_stop();
  jh_fake_dma_reset();
  memset(&s_memory, 0xFF, sizeof(s_memory));
  for (uint32_t i = 0u; i < GUARD; ++i) {
    s_memory.before[i] = 0xA5A5u;
    s_memory.after[i] = 0x5A5Au;
  }
  jh_fake_dma_set_legal_region(s_memory.ring, sizeof(s_memory.ring));
}

void setUp(void) {
  fresh_ring();
  s_micros = 1000u;
  s_marker_calls = 0u;
}

void tearDown(void) { (void)jh_adc_scan_stop(); }

static void assert_guards_intact(void) {
  for (uint32_t i = 0u; i < GUARD; ++i) {
    TEST_ASSERT_EQUAL_HEX16(0xA5A5u, s_memory.before[i]);
    TEST_ASSERT_EQUAL_HEX16(0x5A5Au, s_memory.after[i]);
  }
  TEST_ASSERT_EQUAL_UINT32(0u, jh_fake_dma_writes_outside_region());
}

// Samples carry their own index, so a block is checked against the index of
// its first sample.
static void assert_block_holds(const uint16_t *samples, uint32_t first) {
  for (uint32_t i = 0u; i < BLOCK; ++i) {
    TEST_ASSERT_EQUAL_HEX16((uint16_t)((first + i) & 0xFFFFu), samples[i]);
  }
}

void test_start_claims_the_ring_and_stop_returns_everything(void) {
  start();
  TEST_ASSERT_EQUAL_UINT32(4u, jh_fake_dma_claimed_channels());
  TEST_ASSERT_TRUE(jh_fake_adc_running());
  TEST_ASSERT_EQUAL_UINT(0x7u, jh_fake_adc_round_robin());
  TEST_ASSERT_TRUE(irq_has_handler(DMA_IRQ_0));
  TEST_ASSERT_TRUE(jh_fake_irq_enabled());
  TEST_ASSERT_EQUAL_UINT8(0u, s_positions[0]);
  TEST_ASSERT_EQUAL_UINT8(1u, s_positions[1]);
  TEST_ASSERT_EQUAL_UINT8(2u, s_positions[2]);
  // 2000 ns at 48 MHz is 96 cycles, the converter's minimum: back to back.
  TEST_ASSERT_EQUAL_FLOAT(0.0f, jh_fake_adc_clkdiv());
  TEST_ASSERT_EQUAL_UINT32(6000u, s_period_ns);
  TEST_ASSERT_EQUAL_INT(HAL_EBUSY, rp2040_adc_acquire_dma());

  TEST_ASSERT_EQUAL_INT(HAL_OK, jh_adc_scan_stop());
  TEST_ASSERT_EQUAL_UINT32(0u, jh_fake_dma_claimed_channels());
  TEST_ASSERT_FALSE(jh_fake_adc_running());
  TEST_ASSERT_EQUAL_UINT(0u, jh_fake_adc_round_robin());
  TEST_ASSERT_FALSE(irq_has_handler(DMA_IRQ_0));
  TEST_ASSERT_EQUAL_INT(HAL_OK, rp2040_adc_acquire_dma());
  rp2040_adc_release_dma();
  for (uint channel = 0u; channel < NUM_DMA_CHANNELS; ++channel) {
    TEST_ASSERT_FALSE(dma_channel_is_busy(channel));
  }
}

void test_temperature_and_pin_order_follow_the_converter_inputs(void) {
  hal_adc_scan_config_t cfg = config();
  cfg.pins[0] = 28u;
  cfg.pins[1] = HAL_ADC_SCAN_PIN_TEMPERATURE;
  cfg.pins[2] = 26u;
  TEST_ASSERT_EQUAL_INT(HAL_OK,
                        jh_adc_scan_start(&cfg, s_positions, &s_period_ns));
  // The FIFO delivers inputs in ascending order: 26 (0), 28 (2), sensor (4).
  TEST_ASSERT_EQUAL_UINT8(1u, s_positions[0]);
  TEST_ASSERT_EQUAL_UINT8(2u, s_positions[1]);
  TEST_ASSERT_EQUAL_UINT8(0u, s_positions[2]);
  TEST_ASSERT_EQUAL_UINT(0x15u, jh_fake_adc_round_robin());
  TEST_ASSERT_TRUE(jh_fake_adc_temperature_enabled());
  TEST_ASSERT_EQUAL_INT(HAL_OK, jh_adc_scan_stop());
  TEST_ASSERT_FALSE(jh_fake_adc_temperature_enabled());

  cfg.pins[0] = 25u;
  TEST_ASSERT_EQUAL_INT(HAL_EINVAL,
                        jh_adc_scan_start(&cfg, s_positions, &s_period_ns));
}

void test_start_without_channels_or_with_a_taken_interrupt_cleans_up(void) {
  for (uint i = 0u; i < 9u; ++i) {
    TEST_ASSERT_TRUE(dma_claim_unused_channel(false) >= 0);
  }
  const hal_adc_scan_config_t cfg = config();
  TEST_ASSERT_EQUAL_INT(HAL_ENOMEM,
                        jh_adc_scan_start(&cfg, s_positions, &s_period_ns));
  TEST_ASSERT_EQUAL_INT(HAL_OK, jh_adc_scan_stop());
  TEST_ASSERT_EQUAL_UINT32(9u, jh_fake_dma_claimed_channels());
  TEST_ASSERT_EQUAL_INT(HAL_OK, rp2040_adc_acquire_dma());
  rp2040_adc_release_dma();

  fresh_ring();
  irq_set_exclusive_handler(DMA_IRQ_0, idle_handler);
  TEST_ASSERT_EQUAL_INT(HAL_EBUSY,
                        jh_adc_scan_start(&cfg, s_positions, &s_period_ns));
  TEST_ASSERT_EQUAL_INT(HAL_OK, jh_adc_scan_stop());
  TEST_ASSERT_EQUAL_UINT32(0u, jh_fake_dma_claimed_channels());
  TEST_ASSERT_TRUE(irq_has_handler(DMA_IRQ_0));
  TEST_ASSERT_EQUAL_INT(HAL_OK, rp2040_adc_acquire_dma());
  rp2040_adc_release_dma();
}

void test_each_block_is_published_once_and_the_ring_wraps_on_its_own(void) {
  start();
  hal_adc_scan_block_t block = {};
  jh_fake_adc_feed(BLOCK - 1u);
  TEST_ASSERT_FALSE(jh_adc_scan_completed(&block));
  TEST_ASSERT_EQUAL_UINT32(0u, jh_fake_irq_handler_calls());

  s_micros = 2000u;
  jh_fake_adc_feed(1u);
  TEST_ASSERT_EQUAL_UINT32(1u, jh_fake_irq_handler_calls());
  TEST_ASSERT_TRUE(jh_adc_scan_completed(&block));
  TEST_ASSERT_EQUAL_UINT32(1u, block.sequence);
  TEST_ASSERT_EQUAL_PTR(&s_memory.ring[0], block.samples);
  TEST_ASSERT_EQUAL_UINT32(FRAMES, block.frames);
  TEST_ASSERT_EQUAL_UINT8(PINS, block.pin_count);
  TEST_ASSERT_EQUAL_UINT32(2000u, block.completed_us);
  TEST_ASSERT_EQUAL_UINT32(s_marker_base + 1u, block.marker);
  assert_block_holds(block.samples, 0u);

  s_micros = 3000u;
  jh_fake_adc_feed(BLOCK);
  TEST_ASSERT_TRUE(jh_adc_scan_completed(&block));
  TEST_ASSERT_EQUAL_UINT32(2u, block.sequence);
  TEST_ASSERT_EQUAL_PTR(&s_memory.ring[BLOCK], block.samples);
  TEST_ASSERT_EQUAL_UINT32(3000u, block.completed_us);
  assert_block_holds(block.samples, BLOCK);

  // The third block lands in the first half again: the control channels
  // moved the write pointer back without any help from the CPU.
  jh_fake_adc_feed(BLOCK);
  TEST_ASSERT_TRUE(jh_adc_scan_completed(&block));
  TEST_ASSERT_EQUAL_UINT32(3u, block.sequence);
  TEST_ASSERT_EQUAL_PTR(&s_memory.ring[0], block.samples);
  assert_block_holds(block.samples, 2u * BLOCK);
  assert_guards_intact();
  TEST_ASSERT_EQUAL_UINT32(0u, jh_fake_adc_samples_dropped());
}

void test_masked_interrupts_lose_blocks_but_never_the_buffer(void) {
  start();
  hal_adc_scan_block_t block = {};
  jh_fake_adc_feed(BLOCK);
  TEST_ASSERT_TRUE(jh_adc_scan_completed(&block));
  TEST_ASSERT_EQUAL_UINT32(1u, block.sequence);

  // A flash transaction keeps this core's interrupts masked for several
  // blocks; the converter and the DMA go on. Six more blocks finish and the
  // eighth is two samples in, so the newest complete block sits in half 0
  // while half 1 is being overwritten.
  const uint32_t masked = save_and_disable_interrupts();
  jh_fake_adc_feed((6u * BLOCK) + 2u);
  TEST_ASSERT_EQUAL_UINT32(1u, jh_fake_irq_handler_calls());
  assert_guards_intact();
  TEST_ASSERT_EQUAL_UINT32(0u, jh_fake_adc_samples_dropped());
  s_micros = 9000u;
  restore_interrupts(masked);
  TEST_ASSERT_EQUAL_UINT32(2u, jh_fake_irq_handler_calls());

  TEST_ASSERT_TRUE(jh_adc_scan_completed(&block));
  // Two interrupt bits were pending, so the count moves by two, not six.
  TEST_ASSERT_EQUAL_UINT32(3u, block.sequence);
  TEST_ASSERT_EQUAL_UINT32(9000u, block.completed_us);
  TEST_ASSERT_EQUAL_PTR(&s_memory.ring[0], block.samples);
  assert_block_holds(block.samples, 6u * BLOCK);

  // The ring goes on normally afterwards.
  jh_fake_adc_feed(BLOCK - 2u);
  TEST_ASSERT_TRUE(jh_adc_scan_completed(&block));
  TEST_ASSERT_EQUAL_UINT32(4u, block.sequence);
  TEST_ASSERT_EQUAL_PTR(&s_memory.ring[BLOCK], block.samples);
  assert_block_holds(block.samples, 7u * BLOCK);
  assert_guards_intact();
}

// One read of the newest sample with the DMA moving by `advance` samples
// right before register access `access`, after `fed` samples had landed. A
// value is its sample index, so it tells its position and how fresh it is:
// it must have been written, belong to the requested position and be no
// older than that position's sample in the newest frame complete at the
// start of the read (a sample from the frame in progress is fine).
static void check_one_read(uint32_t fed, uint32_t access, uint32_t advance,
                           uint8_t position) {
  fresh_ring();
  start();
  jh_fake_adc_feed(fed);
  const bool complete_at_start = fed >= PINS;
  const uint32_t oldest_allowed =
      complete_at_start ? (((fed / PINS) - 1u) * PINS) + position : 0u;

  jh_fake_dma_reset_access_count();
  jh_fake_dma_schedule_feed(access, advance);
  uint16_t raw = UNWRITTEN;
  const hal_status_t status = jh_adc_scan_latest(position, &raw);

  const uint32_t fed_at_end = jh_fake_adc_samples_fed();
  char where[96];
  (void)snprintf(where, sizeof(where),
                 "fed=%lu access=%lu advance=%lu position=%u raw=%u",
                 (unsigned long)fed, (unsigned long)access,
                 (unsigned long)advance, (unsigned)position, (unsigned)raw);
  if (complete_at_start) {
    TEST_ASSERT_EQUAL_INT_MESSAGE(HAL_OK, status, where);
  } else if (status != HAL_OK) {
    TEST_ASSERT_EQUAL_INT_MESSAGE(HAL_EAGAIN, status, where);
    return;
  }
  TEST_ASSERT_NOT_EQUAL_MESSAGE(UNWRITTEN, raw, where);
  TEST_ASSERT_EQUAL_UINT32_MESSAGE(position, raw % PINS, where);
  TEST_ASSERT_TRUE_MESSAGE(raw < fed_at_end, where);
  TEST_ASSERT_TRUE_MESSAGE(raw >= oldest_allowed, where);
  assert_guards_intact();
}

void test_latest_never_serves_an_unwritten_or_stale_sample(void) {
  static const uint32_t kFed[] = {0u,
                                  1u,
                                  PINS - 1u,
                                  PINS,
                                  PINS + 1u,
                                  BLOCK - 1u,
                                  BLOCK,
                                  BLOCK + 1u,
                                  BLOCK + PINS,
                                  (2u * BLOCK) - 1u,
                                  2u * BLOCK,
                                  (2u * BLOCK) + 1u,
                                  (3u * BLOCK) - 2u,
                                  3u * BLOCK,
                                  (4u * BLOCK) + PINS};
  static const uint32_t kAdvance[] = {1u,
                                      2u,
                                      PINS,
                                      PINS + 1u,
                                      BLOCK - 1u,
                                      BLOCK,
                                      BLOCK + 1u,
                                      BLOCK + PINS,
                                      (2u * BLOCK) - 1u,
                                      2u * BLOCK,
                                      2u * BLOCK + 1u,
                                      (3u * BLOCK) + 2u};
  for (uint32_t f = 0u; f < sizeof(kFed) / sizeof(kFed[0]); ++f) {
    for (uint32_t access = 0u; access < 16u; ++access) {
      for (uint32_t a = 0u; a < sizeof(kAdvance) / sizeof(kAdvance[0]); ++a) {
        for (uint8_t position = 0u; position < PINS; ++position) {
          check_one_read(kFed[f], access, kAdvance[a], position);
        }
      }
    }
  }
}

void test_latest_reads_the_block_in_progress_and_falls_back_to_the_other(void) {
  start();
  uint16_t raw = 0u;
  TEST_ASSERT_EQUAL_INT(HAL_EAGAIN, jh_adc_scan_latest(0u, &raw));
  jh_fake_adc_feed(PINS - 1u);
  TEST_ASSERT_EQUAL_INT(HAL_EAGAIN, jh_adc_scan_latest(0u, &raw));
  jh_fake_adc_feed(1u);
  TEST_ASSERT_EQUAL_INT(HAL_OK, jh_adc_scan_latest(2u, &raw));
  TEST_ASSERT_EQUAL_HEX16(2u, raw);
  jh_fake_adc_feed(BLOCK - PINS);
  // Block 1 just completed; the second half has no frame yet.
  TEST_ASSERT_EQUAL_INT(HAL_OK, jh_adc_scan_latest(1u, &raw));
  TEST_ASSERT_EQUAL_HEX16(BLOCK - PINS + 1u, raw);
  jh_fake_adc_feed(PINS + 1u);
  TEST_ASSERT_EQUAL_INT(HAL_OK, jh_adc_scan_latest(0u, &raw));
  TEST_ASSERT_EQUAL_HEX16(BLOCK, raw);
  TEST_ASSERT_EQUAL_INT(HAL_ESTATE, jh_adc_scan_latest(PINS, &raw));
  TEST_ASSERT_EQUAL_INT(HAL_OK, jh_adc_scan_stop());
  TEST_ASSERT_EQUAL_INT(HAL_ESTATE, jh_adc_scan_latest(0u, &raw));
}

int main(void) {
  UNITY_BEGIN();
  RUN_TEST(test_start_claims_the_ring_and_stop_returns_everything);
  RUN_TEST(test_temperature_and_pin_order_follow_the_converter_inputs);
  RUN_TEST(test_start_without_channels_or_with_a_taken_interrupt_cleans_up);
  RUN_TEST(test_each_block_is_published_once_and_the_ring_wraps_on_its_own);
  RUN_TEST(test_masked_interrupts_lose_blocks_but_never_the_buffer);
  RUN_TEST(test_latest_never_serves_an_unwritten_or_stale_sample);
  RUN_TEST(test_latest_reads_the_block_in_progress_and_falls_back_to_the_other);
  return UNITY_END();
}
