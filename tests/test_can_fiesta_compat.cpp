// Behaviour of the CAN API that the Fiesta modules rely on. A change of these
// tests is a change Fiesta has to follow.
#include "can_fiesta_usage.h"
#include "hal/gpio/hal_gpio.h"
#include "hal/impl/.mock/hal_mock.h"
#include "hal/system/hal_system.h"
#include "utils/unity.h"

#include <type_traits>

// Clocks and OilAndSpeed are C++: pin the exact signatures they call.
static_assert(std::is_same<decltype(&hal_can_send),
                           hal_status_t (*)(hal_can_t, uint32_t, uint8_t,
                                            const uint8_t *)>::value,
              "hal_can_send signature");
static_assert(std::is_same<decltype(&hal_can_set_filter),
                           hal_status_t (*)(hal_can_t, uint8_t,
                                            const hal_can_filter_t *)>::value,
              "hal_can_set_filter signature");
static_assert(std::is_same<decltype(&hal_can_process_all),
                           hal_status_t (*)(hal_can_t, hal_can_frame_cb_t,
                                            uint32_t *)>::value,
              "hal_can_process_all signature");
static_assert(std::is_same<hal_can_frame_cb_t,
                           void (*)(uint32_t, uint8_t, const uint8_t *)>::value,
              "frame callback shape");
static_assert(std::is_same<decltype(&hal_can_create_with_retry),
                           hal_status_t (*)(const hal_can_config_t *, uint8_t,
                                            void (*)(void), int, void (*)(void),
                                            hal_can_t *)>::value,
              "hal_can_create_with_retry signature");
static_assert(
    std::is_same<decltype(&hal_can_destroy), void (*)(hal_can_t)>::value,
    "hal_can_destroy signature");

static int s_isr_calls;
static int s_idle_calls;
static int s_frames;
static uint32_t s_last_id;
static uint8_t s_last_len;
static uint8_t s_last_data[HAL_CAN_MAX_DATA_LEN];

static void count_isr(void) { s_isr_calls++; }
static void count_idle(void) { s_idle_calls++; }
static void on_frame(uint32_t id, uint8_t len, const uint8_t *data) {
  s_frames++;
  s_last_id = id;
  s_last_len = len;
  for (uint8_t i = 0; i < len && i < HAL_CAN_MAX_DATA_LEN; i++) {
    s_last_data[i] = data[i];
  }
}

static const uint8_t kPayload[HAL_CAN_MAX_DATA_LEN] = {1, 2, 3, 4, 5, 6, 7, 8};

static hal_can_t can0(void) {
  hal_can_t h = nullptr;
  TEST_ASSERT_EQUAL_INT(HAL_OK, fiesta_ecu_can0_init(nullptr, 0, nullptr, &h));
  TEST_ASSERT_NOT_NULL(h);
  return h;
}

void setUp(void) {
  s_isr_calls = 0;
  s_idle_calls = 0;
  s_frames = 0;
  s_last_id = 0;
  s_last_len = 0;
  hal_mock_can_fail_creates(0u);
  hal_mock_set_millis(0);
}

void tearDown(void) { hal_mock_can_fail_creates(0u); }

void test_default_config_is_the_fiesta_mcp2515(void) {
  const hal_can_config_t cfg = hal_can_default_config();
  TEST_ASSERT_EQUAL(HAL_CAN_BACKEND_MCP2515, cfg.backend);
  TEST_ASSERT_EQUAL_UINT8(0u, cfg.mcp2515.spi_bus);
  TEST_ASSERT_EQUAL_UINT32(500000u, cfg.mcp2515.bitrate_hz);
  TEST_ASSERT_EQUAL_UINT32(8000000u, cfg.mcp2515.oscillator_hz);
  TEST_ASSERT_TRUE(cfg.mcp2515.one_shot_tx);
  TEST_ASSERT_TRUE(cfg.mcp2515.sleep_wakeup);
}

void test_can0_init_sets_int_pin_input_and_attaches_isr(void) {
  hal_gpio_set_mode(FIESTA_CAN0_INT, HAL_GPIO_OUTPUT);
  hal_can_t h = nullptr;
  TEST_ASSERT_EQUAL_INT(HAL_OK,
                        fiesta_ecu_can0_init(count_isr, 2, count_idle, &h));
  TEST_ASSERT_NOT_NULL(h);
  TEST_ASSERT_EQUAL(HAL_GPIO_INPUT, hal_mock_gpio_get_mode(FIESTA_CAN0_INT));
  hal_mock_gpio_fire_interrupt(FIESTA_CAN0_INT);
  TEST_ASSERT_EQUAL_INT(1, s_isr_calls);
  TEST_ASSERT_EQUAL_INT(0, s_idle_calls);
  hal_can_destroy(h);
}

void test_obd_init_sets_input_without_isr_and_two_exact_filters(void) {
  hal_gpio_set_mode(FIESTA_CAN1_INT, HAL_GPIO_OUTPUT);
  hal_can_t h = nullptr;
  TEST_ASSERT_EQUAL_INT(HAL_OK, fiesta_obd_init(count_idle, &h));
  TEST_ASSERT_NOT_NULL(h);
  TEST_ASSERT_EQUAL(HAL_GPIO_INPUT, hal_mock_gpio_get_mode(FIESTA_CAN1_INT));
  hal_mock_can_inject(h, 0x123u, 8u, kPayload);
  TEST_ASSERT_EQUAL_INT(HAL_EAGAIN, hal_can_available(h));
  hal_mock_can_inject(h, 0x7E0u, 8u, kPayload);
  TEST_ASSERT_EQUAL_INT(HAL_OK, hal_can_available(h));
  uint32_t id = 0;
  uint8_t len = 0;
  uint8_t buf[HAL_CAN_MAX_DATA_LEN] = {};
  TEST_ASSERT_EQUAL_INT(HAL_OK, fiesta_obd_receive(h, &id, &len, buf));
  TEST_ASSERT_EQUAL_HEX32(0x7E0u, id);
  TEST_ASSERT_EQUAL_UINT8(8u, len);
  TEST_ASSERT_EQUAL_UINT8_ARRAY(kPayload, buf, 8);
  hal_mock_can_inject(h, 0x7DFu, 2u, kPayload);
  TEST_ASSERT_EQUAL_INT(HAL_OK, fiesta_obd_receive(h, &id, &len, buf));
  TEST_ASSERT_EQUAL_HEX32(0x7DFu, id);
  TEST_ASSERT_EQUAL_INT(HAL_EAGAIN, fiesta_obd_receive(h, &id, &len, buf));
  hal_can_destroy(h);
}

void test_create_retry_feeds_idle_before_each_one_second_pause(void) {
  hal_mock_can_fail_creates(2u);
  hal_can_t h = nullptr;
  TEST_ASSERT_EQUAL_INT(HAL_OK,
                        fiesta_ecu_can0_init(nullptr, 2, count_idle, &h));
  TEST_ASSERT_NOT_NULL(h);
  TEST_ASSERT_EQUAL_INT(2, s_idle_calls);
  TEST_ASSERT_EQUAL_UINT32(2000u, hal_millis());
  hal_can_destroy(h);
}

void test_create_retry_gives_up_with_the_last_error(void) {
  hal_mock_can_fail_creates(3u);
  hal_can_t h = nullptr;
  TEST_ASSERT_EQUAL_INT(HAL_EIO,
                        fiesta_ecu_can0_init(nullptr, 2, count_idle, &h));
  TEST_ASSERT_NULL(h);
  TEST_ASSERT_EQUAL_INT(2, s_idle_calls);
  TEST_ASSERT_EQUAL_UINT32(2000u, hal_millis());
}

void test_send_tells_bus_off_from_stopped_and_sleeping(void) {
  hal_can_t h = can0();
  TEST_ASSERT_EQUAL_INT(HAL_OK, fiesta_send8(h, 0x123u, kPayload));
  TEST_ASSERT_FALSE(fiesta_bus_off(h));
  hal_mock_can_set_state(h, HAL_CAN_STATE_BUS_OFF);
  TEST_ASSERT_TRUE(fiesta_bus_off(h));
  TEST_ASSERT_EQUAL_INT(HAL_EBUS, fiesta_send8(h, 0x123u, kPayload));
  hal_mock_can_set_state(h, HAL_CAN_STATE_STOPPED);
  TEST_ASSERT_EQUAL_INT(HAL_EBUSY, fiesta_send8(h, 0x123u, kPayload));
  hal_mock_can_set_state(h, HAL_CAN_STATE_ERROR_ACTIVE);
  TEST_ASSERT_EQUAL_INT(HAL_OK, hal_can_set_mode(h, HAL_CAN_MODE_SLEEP));
  TEST_ASSERT_EQUAL_INT(HAL_EBUSY, fiesta_send8(h, 0x123u, kPayload));
  hal_can_destroy(h);
}

void test_sent_frame_reaches_the_bus_unchanged(void) {
  hal_can_t h = can0();
  TEST_ASSERT_EQUAL_INT(HAL_OK, fiesta_send8(h, 0x129u, kPayload));
  uint32_t id = 0;
  uint8_t len = 0;
  uint8_t buf[HAL_CAN_MAX_DATA_LEN] = {};
  TEST_ASSERT_EQUAL_INT(HAL_OK, hal_mock_can_get_sent(h, &id, &len, buf));
  TEST_ASSERT_EQUAL_HEX32(0x129u, id);
  TEST_ASSERT_EQUAL_UINT8(8u, len);
  TEST_ASSERT_EQUAL_UINT8_ARRAY(kPayload, buf, 8);
  TEST_ASSERT_EQUAL_INT(HAL_EAGAIN, hal_mock_can_get_sent(h, &id, &len, buf));
  hal_can_destroy(h);
}

void test_mode_reports_one_shot_from_config(void) {
  hal_can_t h = can0();
  hal_can_mode_t mode = HAL_CAN_MODE_NORMAL;
  TEST_ASSERT_EQUAL_INT(HAL_OK, hal_can_get_mode(h, &mode));
  TEST_ASSERT_TRUE((mode & HAL_CAN_MODE_ONE_SHOT) != 0u);
  hal_can_destroy(h);
}

void test_process_all_skips_zero_id_and_empty_frames(void) {
  hal_can_t h = can0();
  hal_mock_can_inject(h, 0x000u, 3u, kPayload);
  hal_mock_can_inject(h, 0x124u, 0u, kPayload);
  hal_mock_can_inject(h, 0x12Au, 8u, kPayload);
  uint32_t delivered = 0u;
  TEST_ASSERT_EQUAL_INT(HAL_OK, hal_can_process_all(h, on_frame, &delivered));
  TEST_ASSERT_EQUAL_UINT32(1u, delivered);
  TEST_ASSERT_EQUAL_INT(1, s_frames);
  TEST_ASSERT_EQUAL_HEX32(0x12Au, s_last_id);
  TEST_ASSERT_EQUAL_UINT8(8u, s_last_len);
  TEST_ASSERT_EQUAL_UINT8_ARRAY(kPayload, s_last_data, 8);
  TEST_ASSERT_EQUAL_INT(HAL_EAGAIN, hal_can_available(h));
  hal_can_destroy(h);
}

void test_process_all_reports_the_error_that_ended_the_drain(void) {
  hal_can_t h = can0();
  hal_can_frame_t fd = {};
  fd.id = 0x125u;
  fd.flags = HAL_CAN_FRAME_FD;
  fd.dlc = 9u;
  fd.len = 12u;
  hal_mock_can_inject(h, 0x12Au, 8u, kPayload);
  hal_mock_can_inject_frame(h, &fd);
  hal_mock_can_inject(h, 0x12Bu, 8u, kPayload);
  uint32_t delivered = 0u;
  TEST_ASSERT_EQUAL_INT(HAL_EUNSUPPORTED,
                        hal_can_process_all(h, on_frame, &delivered));
  TEST_ASSERT_EQUAL_UINT32(1u, delivered);
  TEST_ASSERT_EQUAL_HEX32(0x12Au, s_last_id);
  TEST_ASSERT_EQUAL_INT(HAL_OK, hal_can_process_all(h, on_frame, nullptr));
  TEST_ASSERT_EQUAL_HEX32(0x12Bu, s_last_id);
  TEST_ASSERT_EQUAL_INT(HAL_EINVAL,
                        hal_can_process_all(h, nullptr, &delivered));
  hal_can_destroy(h);
}

void test_clocks_filters_keep_only_the_fiesta_id_range(void) {
  hal_can_t h = can0();
  TEST_ASSERT_EQUAL_INT(HAL_OK, fiesta_clocks_filters(h));
  hal_mock_can_inject(h, 0x555u, 8u, kPayload);
  TEST_ASSERT_EQUAL_INT(HAL_EAGAIN, hal_can_available(h));
  hal_mock_can_inject(h, 0x13Fu, 8u, kPayload);
  TEST_ASSERT_EQUAL_INT(HAL_OK, hal_can_available(h));
  hal_can_destroy(h);
}

void test_ecu_gets_two_handles_at_once(void) {
  hal_can_t can = can0();
  hal_can_t obd = nullptr;
  TEST_ASSERT_EQUAL_INT(HAL_OK, fiesta_obd_init(nullptr, &obd));
  TEST_ASSERT_NOT_NULL(obd);
  TEST_ASSERT_TRUE(can != obd);
  hal_can_destroy(obd);
  hal_can_destroy(can);
}

void test_temperature_byte_saturates_and_truncates(void) {
  TEST_ASSERT_EQUAL_HEX8(0x80u, hal_can_encode_temp_i8(-200.0f));
  TEST_ASSERT_EQUAL_HEX8(25u, hal_can_encode_temp_i8(25.9f));
  TEST_ASSERT_EQUAL_HEX8(0xF6u, hal_can_encode_temp_i8(-10.7f));
  TEST_ASSERT_EQUAL_HEX8(127u, hal_can_encode_temp_i8(300.0f));
}

int main(void) {
  UNITY_BEGIN();
  RUN_TEST(test_default_config_is_the_fiesta_mcp2515);
  RUN_TEST(test_can0_init_sets_int_pin_input_and_attaches_isr);
  RUN_TEST(test_obd_init_sets_input_without_isr_and_two_exact_filters);
  RUN_TEST(test_create_retry_feeds_idle_before_each_one_second_pause);
  RUN_TEST(test_create_retry_gives_up_with_the_last_error);
  RUN_TEST(test_send_tells_bus_off_from_stopped_and_sleeping);
  RUN_TEST(test_sent_frame_reaches_the_bus_unchanged);
  RUN_TEST(test_mode_reports_one_shot_from_config);
  RUN_TEST(test_process_all_skips_zero_id_and_empty_frames);
  RUN_TEST(test_process_all_reports_the_error_that_ended_the_drain);
  RUN_TEST(test_clocks_filters_keep_only_the_fiesta_id_range);
  RUN_TEST(test_ecu_gets_two_handles_at_once);
  RUN_TEST(test_temperature_byte_saturates_and_truncates);
  return UNITY_END();
}
