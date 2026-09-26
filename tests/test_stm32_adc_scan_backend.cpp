// The STM32G474 ADC scan backend on a register table: the circular DMA ring
// is programmed as documented, the completion interrupt publishes the half
// that finished, and with both flags pending it publishes the half the
// channel is not filling, so a late interrupt never hands out the half being
// overwritten.

#include "hal/analog/jh_adc_scan_backend.h"
#include "hal/impl/stm32g474/port/stm32g474_regs.h"
#include "hal/impl/stm32g474/stm32g474_adc_shared.h"
#include "hal/system/hal_system.h"
#include "jh_stm32g474_host_regs.h"
#include "utils/unity.h"

#include <string.h>

#define PINS 3u
#define FRAMES 4u
#define BLOCK (FRAMES * PINS)
#define CHANNEL 2u /* DMA1 channel 3 */

extern "C" void DMA1_Channel3_IRQHandler(void);

static uint16_t s_ring[2u * BLOCK] __attribute__((aligned(4)));
static uint32_t s_micros;
static uint32_t s_acquired;
static uint32_t s_released;
static stm32g474_adc_scan_reader_fn s_reader;
static uint8_t s_positions[HAL_ADC_SCAN_MAX_PINS];
static uint32_t s_period_ns;

extern "C" {
uint32_t hal_micros(void) { return s_micros; }
void hal_delay_us(uint32_t us) { s_micros += us; }
hal_status_t stm32g474_adc_acquire_dma(void) {
  ++s_acquired;
  return HAL_OK;
}
void stm32g474_adc_release_dma(void) { ++s_released; }
void stm32g474_adc_set_scan_reader(stm32g474_adc_scan_reader_fn reader) {
  s_reader = reader;
}
}

static hal_adc_scan_config_t config(void) {
  hal_adc_scan_config_t cfg = {};
  cfg.pins[0] = 0u;    /* PA0 -> IN1 */
  cfg.pins[1] = 1u;    /* PA1 -> IN2 */
  cfg.pins[2] = 0x10u; /* PB0 -> IN15 */
  cfg.pin_count = PINS;
  cfg.conversion_period_ns = 1000u;
  cfg.buffer = s_ring;
  cfg.block_frames = FRAMES;
  return cfg;
}

static void start(void) {
  const hal_adc_scan_config_t cfg = config();
  TEST_ASSERT_EQUAL_INT(HAL_OK,
                        jh_adc_scan_start(&cfg, s_positions, &s_period_ns));
}

void setUp(void) {
  (void)jh_adc_scan_stop();
  jh_stm32g474_host_regs_reset();
  memset(s_ring, 0, sizeof(s_ring));
  s_micros = 1000u;
  s_acquired = 0u;
  s_released = 0u;
  s_reader = NULL;
}

void tearDown(void) { (void)jh_adc_scan_stop(); }

// The channel counts down over both halves: `written` samples have landed.
static void dma_at(uint32_t written) {
  DMA_CNDTR(DMA1_BASE, CHANNEL) = (2u * BLOCK) - written;
}

static void interrupt(uint32_t flags) {
  DMA_ISR(DMA1_BASE) = flags;
  DMA1_Channel3_IRQHandler();
}

void test_start_programs_a_circular_ring_and_stop_releases_it(void) {
  start();
  TEST_ASSERT_EQUAL_UINT32(1u, s_acquired);
  TEST_ASSERT_NOT_NULL(s_reader);
  const uint32_t ccr = DMA_CCR(DMA1_BASE, CHANNEL);
  TEST_ASSERT_EQUAL_HEX32(DMA_CCR_MINC | DMA_CCR_CIRC | DMA_CCR_PSIZE_16 |
                              DMA_CCR_MSIZE_16 | DMA_CCR_PL_HIGH |
                              DMA_CCR_HTIE | DMA_CCR_TCIE | DMA_CCR_TEIE |
                              DMA_CCR_EN,
                          ccr);
  TEST_ASSERT_EQUAL_UINT32(2u * BLOCK, DMA_CNDTR(DMA1_BASE, CHANNEL));
  TEST_ASSERT_EQUAL_UINT32((uint32_t)(uintptr_t)s_ring,
                           DMA_CMAR(DMA1_BASE, CHANNEL));
  // The peripheral address is ADC1_DR: on the host, that register's cell.
  TEST_ASSERT_EQUAL_UINT32(
      (uint32_t)(uintptr_t)jh_stm32g474_host_reg32(ADC1_BASE + 0x40u),
      DMA_CPAR(DMA1_BASE, CHANNEL));
  TEST_ASSERT_EQUAL_UINT32(DMA_REQUEST_ADC1, DMAMUX_CCR(CHANNEL));
  TEST_ASSERT_TRUE((NVIC_ISER(DMA1_Channel3_IRQn / 32u) &
                    (1u << (DMA1_Channel3_IRQn % 32u))) != 0u);
  TEST_ASSERT_TRUE(
      (ADC1_CFGR & (ADC_CFGR_DMAEN | ADC_CFGR_DMACFG | ADC_CFGR_CONT)) ==
      (ADC_CFGR_DMAEN | ADC_CFGR_DMACFG | ADC_CFGR_CONT));
  TEST_ASSERT_TRUE((ADC1_CR & ADC_CR_ADSTART) != 0u);
  TEST_ASSERT_EQUAL_UINT32(PINS - 1u, ADC1_SQR1 & ADC_SQR1_L_MASK);
  TEST_ASSERT_EQUAL_UINT32(1u, (ADC1_SQR1 >> ADC_SQR1_SQ1_POS) & 0x1Fu);
  TEST_ASSERT_EQUAL_UINT32(2u, (ADC1_SQR1 >> ADC_SQR1_SQ2_POS) & 0x1Fu);
  TEST_ASSERT_EQUAL_UINT32(15u, (ADC1_SQR1 >> ADC_SQR1_SQ3_POS) & 0x1Fu);
  TEST_ASSERT_EQUAL_UINT8(0u, s_positions[0]);
  TEST_ASSERT_EQUAL_UINT8(1u, s_positions[1]);
  TEST_ASSERT_EQUAL_UINT8(2u, s_positions[2]);
  // 1 us asks for 85 half cycles at 42.5 MHz: sample time 47.5 + 12.5 cycles
  // is the shortest that is not faster, 120 half cycles per conversion.
  TEST_ASSERT_EQUAL_UINT32(
      (uint32_t)((120ull * PINS * 1000000000ull) / (2ull * 42500000ull)),
      s_period_ns);

  TEST_ASSERT_EQUAL_INT(HAL_OK, jh_adc_scan_stop());
  TEST_ASSERT_EQUAL_UINT32(1u, s_released);
  TEST_ASSERT_NULL(s_reader);
  TEST_ASSERT_EQUAL_UINT32(0u, DMA_CCR(DMA1_BASE, CHANNEL) & DMA_CCR_EN);
  TEST_ASSERT_EQUAL_UINT32(0u, DMAMUX_CCR(CHANNEL));
  TEST_ASSERT_TRUE((NVIC_ICER(DMA1_Channel3_IRQn / 32u) &
                    (1u << (DMA1_Channel3_IRQn % 32u))) != 0u);
  TEST_ASSERT_EQUAL_UINT32(0u, ADC1_CFGR & (ADC_CFGR_DMAEN | ADC_CFGR_CONT));
}

void test_each_flag_publishes_its_half_and_clears_itself(void) {
  start();
  hal_adc_scan_block_t block = {};
  TEST_ASSERT_FALSE(jh_adc_scan_completed(&block));

  s_micros = 2000u;
  dma_at(BLOCK + 2u);
  interrupt(DMA_FLAG_HTIF(CHANNEL) | DMA_FLAG_GIF(CHANNEL));
  TEST_ASSERT_EQUAL_HEX32(DMA_IFCR_CLEAR_ALL(CHANNEL) &
                              (DMA_FLAG_HTIF(CHANNEL) | DMA_FLAG_GIF(CHANNEL)),
                          DMA_IFCR(DMA1_BASE));
  TEST_ASSERT_TRUE(jh_adc_scan_completed(&block));
  TEST_ASSERT_EQUAL_UINT32(1u, block.sequence);
  TEST_ASSERT_EQUAL_PTR(&s_ring[0], block.samples);
  TEST_ASSERT_EQUAL_UINT32(2000u, block.completed_us);
  TEST_ASSERT_EQUAL_UINT32(FRAMES, block.frames);
  TEST_ASSERT_EQUAL_UINT8(PINS, block.pin_count);

  s_micros = 3000u;
  dma_at(2u);
  interrupt(DMA_FLAG_TCIF(CHANNEL) | DMA_FLAG_GIF(CHANNEL));
  TEST_ASSERT_TRUE(jh_adc_scan_completed(&block));
  TEST_ASSERT_EQUAL_UINT32(2u, block.sequence);
  TEST_ASSERT_EQUAL_PTR(&s_ring[BLOCK], block.samples);
  TEST_ASSERT_EQUAL_UINT32(3000u, block.completed_us);

  // A transfer error publishes nothing and stops the channel.
  interrupt(DMA_FLAG_TEIF(CHANNEL) | DMA_FLAG_GIF(CHANNEL));
  TEST_ASSERT_TRUE(jh_adc_scan_completed(&block));
  TEST_ASSERT_EQUAL_UINT32(2u, block.sequence);
  TEST_ASSERT_EQUAL_UINT32(0u, DMA_CCR(DMA1_BASE, CHANNEL) & DMA_CCR_EN);
}

void test_both_flags_pending_publish_the_half_not_being_written(void) {
  start();
  hal_adc_scan_block_t block = {};
  dma_at(BLOCK + 1u);
  interrupt(DMA_FLAG_HTIF(CHANNEL));
  TEST_ASSERT_TRUE(jh_adc_scan_completed(&block));
  TEST_ASSERT_EQUAL_UINT32(1u, block.sequence);

  // The handler was held off for more than a block: half 1 and then half 0
  // finished, the channel is back in half 1. Half 0 is the newest complete
  // one; publishing half 1 would hand out the block being overwritten.
  dma_at(BLOCK + 3u);
  interrupt(DMA_FLAG_HTIF(CHANNEL) | DMA_FLAG_TCIF(CHANNEL) |
            DMA_FLAG_GIF(CHANNEL));
  TEST_ASSERT_TRUE(jh_adc_scan_completed(&block));
  TEST_ASSERT_EQUAL_UINT32(3u, block.sequence);
  TEST_ASSERT_EQUAL_PTR(&s_ring[0], block.samples);

  // And the mirror image: the channel is back in half 0, half 1 is newest.
  dma_at(5u);
  interrupt(DMA_FLAG_HTIF(CHANNEL) | DMA_FLAG_TCIF(CHANNEL));
  TEST_ASSERT_TRUE(jh_adc_scan_completed(&block));
  TEST_ASSERT_EQUAL_UINT32(5u, block.sequence);
  TEST_ASSERT_EQUAL_PTR(&s_ring[BLOCK], block.samples);

  // Exactly at the wrap the channel starts half 0 again: half 1 is newest.
  dma_at(0u);
  interrupt(DMA_FLAG_HTIF(CHANNEL) | DMA_FLAG_TCIF(CHANNEL));
  TEST_ASSERT_TRUE(jh_adc_scan_completed(&block));
  TEST_ASSERT_EQUAL_UINT32(7u, block.sequence);
  TEST_ASSERT_EQUAL_PTR(&s_ring[BLOCK], block.samples);
}

void test_latest_follows_the_transfer_count_not_the_interrupt(void) {
  start();
  uint16_t raw = 0u;
  for (uint32_t i = 0u; i < 2u * BLOCK; ++i) {
    s_ring[i] = (uint16_t)(0x100u + i);
  }
  dma_at(0u);
  TEST_ASSERT_EQUAL_INT(HAL_EAGAIN, jh_adc_scan_latest(0u, &raw));
  dma_at(PINS - 1u);
  TEST_ASSERT_EQUAL_INT(HAL_EAGAIN, jh_adc_scan_latest(0u, &raw));
  dma_at((2u * PINS) + 1u);
  TEST_ASSERT_EQUAL_INT(HAL_OK, jh_adc_scan_latest(1u, &raw));
  TEST_ASSERT_EQUAL_HEX16(0x100u + PINS + 1u, raw);
  // At the half boundary the other half is complete before its interrupt.
  dma_at(BLOCK);
  TEST_ASSERT_EQUAL_INT(HAL_OK, jh_adc_scan_latest(2u, &raw));
  TEST_ASSERT_EQUAL_HEX16(0x100u + BLOCK - 1u, raw);
  dma_at(BLOCK + PINS);
  TEST_ASSERT_EQUAL_INT(HAL_OK, jh_adc_scan_latest(0u, &raw));
  TEST_ASSERT_EQUAL_HEX16(0x100u + BLOCK, raw);
  TEST_ASSERT_EQUAL_INT(HAL_ESTATE, jh_adc_scan_latest(PINS, &raw));
  TEST_ASSERT_TRUE(s_reader(0u, &raw));
  TEST_ASSERT_FALSE(s_reader(0x11u, &raw));
}

int main(void) {
  UNITY_BEGIN();
  RUN_TEST(test_start_programs_a_circular_ring_and_stop_releases_it);
  RUN_TEST(test_each_flag_publishes_its_half_and_clears_itself);
  RUN_TEST(test_both_flags_pending_publish_the_half_not_being_written);
  RUN_TEST(test_latest_follows_the_transfer_count_not_the_interrupt);
  return UNITY_END();
}
