// The flash safe zone's DMA rule on simulated DMA registers: only a busy
// channel whose transfer touches the XIP window may block a flash operation.

#include "hal/impl/rp2040/drivers/flash/rp_flash_dma_guard.h"
#include "rp2040_dma_fake.h"
#include "utils/unity.h"

#define RAM_A 0x20001000u
#define RAM_B 0x20030000u
#define ADC_FIFO 0x4004000Cu

void setUp(void) { jh_fake_dma_reset(); }
void tearDown(void) {}

static void set_channel(uint channel, bool busy, uintptr_t read_addr,
                        uintptr_t write_addr) {
  jh_fake_dma_hw.ch[channel].ctrl_trig =
      busy ? DMA_CH0_CTRL_TRIG_BUSY_BITS : 0u;
  jh_fake_dma_hw.ch[channel].read_addr = read_addr;
  jh_fake_dma_hw.ch[channel].write_addr = write_addr;
}

void test_idle_channels_never_block_whatever_they_point_at(void) {
  TEST_ASSERT_FALSE(jh_rp_flash_dma_blocks());
  set_channel(2u, false, XIP_BASE + 0x4000u, RAM_A);
  set_channel(7u, false, RAM_A, XIP_SRAM_BASE);
  TEST_ASSERT_FALSE(jh_rp_flash_dma_blocks());
}

void test_busy_peripheral_and_ram_rings_keep_running(void) {
  set_channel(0u, true, ADC_FIFO, RAM_A);
  set_channel(1u, true, ADC_FIFO, RAM_B);
  set_channel(5u, true, RAM_A, RAM_B);
  TEST_ASSERT_FALSE(jh_rp_flash_dma_blocks());
  for (uint channel = 0u; channel < NUM_DMA_CHANNELS; ++channel) {
    TEST_ASSERT_FALSE(jh_rp_flash_dma_channel_blocks(channel));
  }
}

void test_a_busy_transfer_touching_flash_blocks(void) {
  set_channel(0u, true, ADC_FIFO, RAM_A);
  set_channel(9u, true, XIP_BASE + 0x100000u, RAM_B);
  TEST_ASSERT_TRUE(jh_rp_flash_dma_channel_blocks(9u));
  TEST_ASSERT_TRUE(jh_rp_flash_dma_blocks());
  set_channel(9u, false, XIP_BASE + 0x100000u, RAM_B);
  TEST_ASSERT_FALSE(jh_rp_flash_dma_blocks());
  // The XIP cache SRAM is unreachable with XIP disabled as well.
  set_channel(11u, true, RAM_A, XIP_SRAM_END - 4u);
  TEST_ASSERT_TRUE(jh_rp_flash_dma_blocks());
}

void test_xip_window_boundaries(void) {
  TEST_ASSERT_FALSE(jh_rp_flash_address_is_xip(XIP_BASE - 1u));
  TEST_ASSERT_TRUE(jh_rp_flash_address_is_xip(XIP_BASE));
  TEST_ASSERT_TRUE(jh_rp_flash_address_is_xip(XIP_CTRL_BASE - 1u));
  TEST_ASSERT_FALSE(jh_rp_flash_address_is_xip(XIP_CTRL_BASE));
  TEST_ASSERT_FALSE(jh_rp_flash_address_is_xip(XIP_SRAM_BASE - 1u));
  TEST_ASSERT_TRUE(jh_rp_flash_address_is_xip(XIP_SRAM_BASE));
  TEST_ASSERT_TRUE(jh_rp_flash_address_is_xip(XIP_SRAM_END - 1u));
  TEST_ASSERT_FALSE(jh_rp_flash_address_is_xip(XIP_SRAM_END));
  TEST_ASSERT_FALSE(jh_rp_flash_address_is_xip(SRAM_BASE));
  TEST_ASSERT_FALSE(jh_rp_flash_address_is_xip(ADC_FIFO));
}

int main(void) {
  UNITY_BEGIN();
  RUN_TEST(test_idle_channels_never_block_whatever_they_point_at);
  RUN_TEST(test_busy_peripheral_and_ram_rings_keep_running);
  RUN_TEST(test_a_busy_transfer_touching_flash_blocks);
  RUN_TEST(test_xip_window_boundaries);
  return UNITY_END();
}
