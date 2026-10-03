// hal_can_board_config() over the generated board facts. Built twice: on the
// host mock board, which declares no CAN channels, and with the
// nucleo-g474re-canhat profile selected, whose channels are written out here
// from the CAN-FD HAT schematic.

#include "hal/can/hal_can.h"
#include "hal/gpio/hal_gpio.h"
#include "hal/serial/hal_serial.h"
#include "utils/unity.h"

#include <stdint.h>

// hal_can_util.cpp also holds helpers that create and read channels.
extern "C" {
hal_status_t hal_can_create(const hal_can_config_t *, hal_can_t *out) {
  *out = nullptr;
  return HAL_EUNSUPPORTED;
}
hal_status_t hal_can_receive(hal_can_t, uint32_t *, uint8_t *, uint8_t *) {
  return HAL_EAGAIN;
}
void hal_gpio_set_mode(uint8_t, hal_gpio_mode_t) {}
void hal_gpio_attach_interrupt(uint8_t, void (*)(void), hal_gpio_irq_mode_t) {}
void hal_delay_ms(uint32_t) {}
void hal_derr_limited(const char *, const char *, ...) {}
}

void setUp(void) {}
void tearDown(void) {}

void test_null_output_is_refused(void) {
  TEST_ASSERT_EQUAL_INT(HAL_EINVAL, hal_can_board_config(0u, nullptr));
}

#if defined(HAL_BOARD_PROFILE_STM32G474_NUCLEO_CANHAT)

#include "hal/impl/stm32g474/port/stm32g474_clock.h"

struct Expected {
  uint8_t instance, rx, tx, standby;
};

void test_canhat_channels_follow_the_hat_wiring(void) {
  /* CN5 FDCAN1 PA11/PA12 STBY PB11, CN6 FDCAN2 PB12/PB13 STBY PC7,
   * CN7 FDCAN3 PA8/PB4 STBY PB6; pins are port * 16 + n. */
  const Expected expected[] = {
      {1u, 11u, 12u, 27u}, {2u, 28u, 29u, 39u}, {3u, 8u, 20u, 22u}};
  for (uint8_t ch = 0; ch < 3u; ++ch) {
    hal_can_config_t cfg = {};
    TEST_ASSERT_EQUAL_INT(HAL_OK, hal_can_board_config(ch, &cfg));
    TEST_ASSERT_EQUAL(HAL_CAN_BACKEND_STM32G474_FDCAN, cfg.backend);
    const hal_can_stm32g474_fdcan_config_t &f = cfg.stm32g474_fdcan;
    TEST_ASSERT_EQUAL_UINT8(expected[ch].instance, f.instance);
    TEST_ASSERT_EQUAL_UINT8(expected[ch].rx, f.rx_pin);
    TEST_ASSERT_EQUAL_UINT8(expected[ch].tx, f.tx_pin);
    TEST_ASSERT_TRUE(f.has_standby);
    TEST_ASSERT_EQUAL_UINT8(expected[ch].standby, f.standby_pin);
    TEST_ASSERT_TRUE(f.standby_high); /* MCP2562FD: high = standby */
    TEST_ASSERT_EQUAL_UINT32(500000u, f.arbitration_bitrate_hz);
    TEST_ASSERT_EQUAL_UINT32(2000000u, f.data_bitrate_hz);
    TEST_ASSERT_EQUAL_UINT32(5000000u, f.transceiver_max_bitrate_hz);
    TEST_ASSERT_TRUE(f.enable_fd);
    TEST_ASSERT_FALSE(f.one_shot_tx);
  }
}

void test_canhat_has_three_channels_and_room_for_them(void) {
  hal_can_config_t cfg = {};
  cfg.backend = HAL_CAN_BACKEND_MCP2515;
  TEST_ASSERT_EQUAL_INT(HAL_ENOENT, hal_can_board_config(3u, &cfg));
  TEST_ASSERT_EQUAL(HAL_CAN_BACKEND_MCP2515, cfg.backend); /* untouched */
  TEST_ASSERT_EQUAL_UINT32(3u, HAL_BOARD_CAN_CHANNEL_COUNT);
  TEST_ASSERT_EQUAL_INT(3, HAL_CAN_MAX_INSTANCES);
}

void test_canhat_runs_the_hse_tree_with_its_rdy_led(void) {
  TEST_ASSERT_EQUAL_UINT32(160000000u, JH_G474_CORE_CLOCK_HZ);
  TEST_ASSERT_EQUAL_UINT32(80000000u, JH_G474_FDCAN_CLOCK_HZ);
  TEST_ASSERT_EQUAL_UINT8(33u, HAL_LED_BUILTIN); /* PC1, LED RDY */
}

#else

void test_board_without_can_channels_has_none(void) {
  hal_can_config_t cfg = {};
  TEST_ASSERT_EQUAL_INT(HAL_ENOENT, hal_can_board_config(0u, &cfg));
  TEST_ASSERT_EQUAL_UINT32(0u, HAL_BOARD_CAN_CHANNEL_COUNT);
}

#endif

int main(void) {
  UNITY_BEGIN();
  RUN_TEST(test_null_output_is_refused);
#if defined(HAL_BOARD_PROFILE_STM32G474_NUCLEO_CANHAT)
  RUN_TEST(test_canhat_channels_follow_the_hat_wiring);
  RUN_TEST(test_canhat_has_three_channels_and_room_for_them);
  RUN_TEST(test_canhat_runs_the_hse_tree_with_its_rdy_led);
#else
  RUN_TEST(test_board_without_can_channels_has_none);
#endif
  return UNITY_END();
}
