#include "hal/hal_can.h"
#include "hal/hal_system.h"
#include "hal/impl/.mock/hal_mock.h"
#include "utils/unity.h"

static hal_can_t can;
static int s_frame_count;
static uint32_t s_last_id;
static uint8_t s_last_len;
static uint8_t s_last_payload[HAL_CAN_MAX_DATA_LEN];
static int s_retry_idle_calls;

static void test_can_frame_cb(uint32_t id, uint8_t len, const uint8_t *data) {
  s_frame_count++;
  s_last_id = id;
  s_last_len = len;
  for (uint8_t i = 0; i < len && i < HAL_CAN_MAX_DATA_LEN; i++) {
    s_last_payload[i] = data[i];
  }
}

static void test_retry_idle_cb(void) { s_retry_idle_calls++; }

static hal_can_config_t test_mcp251xfd_config(void) {
  hal_can_config_t cfg = {};
  cfg.backend = HAL_CAN_BACKEND_MCP251XFD;
  cfg.mcp251xfd.spi_bus = 0u;
  cfg.mcp251xfd.cs_pin = 10u;
  cfg.mcp251xfd.arbitration_bitrate_hz = 500000u;
  cfg.mcp251xfd.data_bitrate_hz = 2000000u;
  cfg.mcp251xfd.oscillator_hz = 40000000u;
  cfg.mcp251xfd.spi_clock_hz = 10000000u;
  cfg.mcp251xfd.enable_fd = true;
  cfg.mcp251xfd.one_shot_tx = true;
  cfg.mcp251xfd.sleep_wakeup = true;
  return cfg;
}

void setUp(void) {
  hal_can_config_t cfg = hal_can_default_config();
  can = hal_can_create(&cfg);
  s_frame_count = 0;
  s_last_id = 0;
  s_last_len = 0;
  s_retry_idle_calls = 0;
  for (int i = 0; i < HAL_CAN_MAX_DATA_LEN; i++) {
    s_last_payload[i] = 0;
  }
}

void tearDown(void) {
  hal_can_destroy(can);
  can = nullptr;
}

void test_create_returns_valid_handle(void) { TEST_ASSERT_NOT_NULL(can); }

void test_default_config_selects_mcp2515_backend(void) {
  hal_can_config_t cfg = hal_can_default_config();

  TEST_ASSERT_EQUAL(HAL_CAN_BACKEND_MCP2515, cfg.backend);
  TEST_ASSERT_EQUAL_UINT8(0, cfg.mcp2515.spi_bus);
  TEST_ASSERT_EQUAL_UINT8(0, cfg.mcp2515.cs_pin);
  TEST_ASSERT_EQUAL_UINT32(500000, cfg.mcp2515.bitrate_hz);
  TEST_ASSERT_EQUAL_UINT32(8000000, cfg.mcp2515.oscillator_hz);
  TEST_ASSERT_TRUE(cfg.mcp2515.one_shot_tx);
  TEST_ASSERT_TRUE(cfg.mcp2515.sleep_wakeup);
}

void test_mcp251xfd_config_can_create_fd_capable_handle(void) {
  hal_can_config_t cfg = test_mcp251xfd_config();
  hal_can_t h = hal_can_create(&cfg);

  TEST_ASSERT_NOT_NULL(h);
  hal_can_mode_t mode = HAL_CAN_MODE_NORMAL;
  TEST_ASSERT_TRUE(hal_can_get_mode(h, &mode));
  TEST_ASSERT_EQUAL_UINT32(HAL_CAN_MODE_ONE_SHOT | HAL_CAN_MODE_FD, mode);
  TEST_ASSERT_TRUE(hal_can_set_mode(h, HAL_CAN_MODE_FD));
  hal_can_destroy(h);
}

void test_create_rejects_unsupported_backend(void) {
  hal_can_config_t cfg = hal_can_default_config();
  cfg.backend = (hal_can_backend_t)255;

  hal_can_t h = hal_can_create(&cfg);

  TEST_ASSERT_NULL(h);
}

void test_send_stores_frame(void) {
  uint8_t data[] = {0x01, 0x02, 0x03};
  TEST_ASSERT_TRUE(hal_can_send(can, 0x100, 3, data));

  uint32_t id;
  uint8_t len, buf[8];
  TEST_ASSERT_TRUE(hal_mock_can_get_sent(can, &id, &len, buf));
  TEST_ASSERT_EQUAL_HEX32(0x100, id);
  TEST_ASSERT_EQUAL_UINT8(3, len);
  TEST_ASSERT_EQUAL_HEX8(0x01, buf[0]);
  TEST_ASSERT_EQUAL_HEX8(0x03, buf[2]);
}

void test_receive_injected_frame(void) {
  uint8_t payload[] = {0xAB, 0xCD};
  hal_mock_can_inject(can, 0x200, 2, payload);

  TEST_ASSERT_TRUE(hal_can_available(can));

  uint32_t id;
  uint8_t len, buf[8];
  TEST_ASSERT_TRUE(hal_can_receive(can, &id, &len, buf));
  TEST_ASSERT_EQUAL_HEX32(0x200, id);
  TEST_ASSERT_EQUAL_UINT8(2, len);
  TEST_ASSERT_EQUAL_HEX8(0xAB, buf[0]);
  TEST_ASSERT_EQUAL_HEX8(0xCD, buf[1]);
}

void test_available_false_when_empty(void) {
  TEST_ASSERT_FALSE(hal_can_available(can));
}

void test_receive_consumes_frame(void) {
  uint8_t data[] = {0xFF};
  hal_mock_can_inject(can, 0x1, 1, data);
  uint32_t id;
  uint8_t len, buf[8];
  hal_can_receive(can, &id, &len, buf);
  TEST_ASSERT_FALSE(hal_can_available(can));
}

void test_reset_clears_buffers(void) {
  uint8_t data[] = {0x01};
  hal_mock_can_inject(can, 0x1, 1, data);
  hal_can_send(can, 0x2, 1, data);
  hal_mock_can_reset(can);
  TEST_ASSERT_FALSE(hal_can_available(can));
  uint32_t id;
  uint8_t len, buf[8];
  TEST_ASSERT_FALSE(hal_mock_can_get_sent(can, &id, &len, buf));
}

void test_send_null_handle_returns_false(void) {
  uint8_t data[] = {0x01};
  TEST_ASSERT_FALSE(hal_can_send(nullptr, 0x1, 1, data));
}

void test_send_null_data_with_nonzero_len_returns_false(void) {
  TEST_ASSERT_FALSE(hal_can_send(can, 0x123, 1, nullptr));
}

void test_send_clamps_payload_len_to_max(void) {
  uint8_t data[12];
  for (int i = 0; i < 12; i++) {
    data[i] = (uint8_t)(i + 1);
  }

  TEST_ASSERT_TRUE(hal_can_send(can, 0x321, 12, data));

  uint32_t id;
  uint8_t len, buf[8] = {};
  TEST_ASSERT_TRUE(hal_mock_can_get_sent(can, &id, &len, buf));
  TEST_ASSERT_EQUAL_HEX32(0x321, id);
  TEST_ASSERT_EQUAL_UINT8(HAL_CAN_MAX_DATA_LEN, len);
  TEST_ASSERT_EQUAL_HEX8(0x01, buf[0]);
  TEST_ASSERT_EQUAL_HEX8(0x08, buf[7]);
}

void test_dlc_helpers_cover_classic_and_fd_lengths(void) {
  TEST_ASSERT_EQUAL_UINT8(0, hal_can_dlc_to_bytes(0));
  TEST_ASSERT_EQUAL_UINT8(8, hal_can_dlc_to_bytes(8));
  TEST_ASSERT_EQUAL_UINT8(12, hal_can_dlc_to_bytes(9));
  TEST_ASSERT_EQUAL_UINT8(64, hal_can_dlc_to_bytes(15));
  TEST_ASSERT_EQUAL_UINT8(0, hal_can_dlc_to_bytes(16));

  TEST_ASSERT_EQUAL_UINT8(0, hal_can_bytes_to_dlc(0));
  TEST_ASSERT_EQUAL_UINT8(8, hal_can_bytes_to_dlc(8));
  TEST_ASSERT_EQUAL_UINT8(9, hal_can_bytes_to_dlc(9));
  TEST_ASSERT_EQUAL_UINT8(9, hal_can_bytes_to_dlc(12));
  TEST_ASSERT_EQUAL_UINT8(10, hal_can_bytes_to_dlc(13));
  TEST_ASSERT_EQUAL_UINT8(15, hal_can_bytes_to_dlc(64));
  TEST_ASSERT_EQUAL_UINT8(HAL_CAN_DLC_INVALID, hal_can_bytes_to_dlc(65));
}

void test_frame_validation_rejects_invalid_id_flags_and_lengths(void) {
  hal_can_frame_t frame = {};
  frame.id = 0x7FFu;
  frame.dlc = 8;
  frame.len = 8;
  TEST_ASSERT_TRUE(hal_can_validate_frame(&frame));

  frame.id = 0x800u;
  TEST_ASSERT_FALSE(hal_can_validate_frame(&frame));

  frame.id = 0x1FFFFFFFu;
  frame.flags = HAL_CAN_FRAME_EXTENDED;
  TEST_ASSERT_TRUE(hal_can_validate_frame(&frame));

  frame.flags = HAL_CAN_FRAME_BRS;
  TEST_ASSERT_FALSE(hal_can_validate_frame(&frame));

  frame.flags = 0x80u;
  TEST_ASSERT_FALSE(hal_can_validate_frame(&frame));

  frame.flags = HAL_CAN_FRAME_FD | HAL_CAN_FRAME_RTR;
  TEST_ASSERT_FALSE(hal_can_validate_frame(&frame));

  frame.flags = HAL_CAN_FRAME_FD;
  frame.dlc = 9;
  frame.len = 11;
  TEST_ASSERT_FALSE(hal_can_validate_frame(&frame));
}

void test_filter_validation_and_frame_matching(void) {
  hal_can_frame_t frame = {};
  frame.id = 0x123u;
  frame.dlc = 2;
  frame.len = 2;

  hal_can_filter_t filter = {0x120u, 0x7F0u, 0u};
  TEST_ASSERT_TRUE(hal_can_validate_filter(&filter));
  TEST_ASSERT_TRUE(hal_can_frame_matches_filter(&frame, &filter));

  filter.id = 0x130u;
  TEST_ASSERT_FALSE(hal_can_frame_matches_filter(&frame, &filter));

  filter.id = 0x123u;
  filter.flags = HAL_CAN_FILTER_EXTENDED;
  TEST_ASSERT_FALSE(hal_can_frame_matches_filter(&frame, &filter));

  filter.id = 0x20000000u;
  TEST_ASSERT_FALSE(hal_can_validate_filter(&filter));
}

void test_send_frame_stores_extended_rtr_frame(void) {
  hal_can_frame_t frame = {};
  frame.id = 0x1ABCDEFu;
  frame.flags = HAL_CAN_FRAME_EXTENDED | HAL_CAN_FRAME_RTR;
  frame.dlc = 4;
  frame.len = 4;

  TEST_ASSERT_TRUE(hal_can_send_frame(can, &frame));

  hal_can_frame_t sent = {};
  TEST_ASSERT_TRUE(hal_mock_can_get_sent_frame(can, &sent));
  TEST_ASSERT_EQUAL_HEX32(frame.id, sent.id);
  TEST_ASSERT_EQUAL_UINT8(frame.flags, sent.flags);
  TEST_ASSERT_EQUAL_UINT8(4, sent.dlc);
  TEST_ASSERT_EQUAL_UINT8(4, sent.len);
}

void test_send_receive_can_fd_frame_via_mock_frame_api(void) {
  hal_can_config_t cfg = test_mcp251xfd_config();
  hal_can_t fd_can = hal_can_create(&cfg);
  TEST_ASSERT_NOT_NULL(fd_can);

  hal_can_frame_t frame = {};
  frame.id = 0x123u;
  frame.flags = HAL_CAN_FRAME_FD | HAL_CAN_FRAME_BRS;
  frame.len = 64;
  frame.dlc = hal_can_bytes_to_dlc(frame.len);
  for (uint8_t i = 0; i < frame.len; i++) {
    frame.data[i] = i;
  }

  TEST_ASSERT_TRUE(hal_can_send_frame(fd_can, &frame));
  hal_can_frame_t sent = {};
  TEST_ASSERT_TRUE(hal_mock_can_get_sent_frame(fd_can, &sent));
  TEST_ASSERT_EQUAL_UINT8(HAL_CAN_FRAME_FD | HAL_CAN_FRAME_BRS, sent.flags);
  TEST_ASSERT_EQUAL_UINT8(15, sent.dlc);
  TEST_ASSERT_EQUAL_UINT8(64, sent.len);
  TEST_ASSERT_EQUAL_UINT8(63, sent.data[63]);

  hal_mock_can_inject_frame(fd_can, &frame);
  hal_can_frame_t rx = {};
  TEST_ASSERT_TRUE(hal_can_receive_frame(fd_can, &rx));
  TEST_ASSERT_EQUAL_HEX32(0x123u, rx.id);
  TEST_ASSERT_EQUAL_UINT8(HAL_CAN_FRAME_FD | HAL_CAN_FRAME_BRS, rx.flags);
  TEST_ASSERT_EQUAL_UINT8(64, rx.len);
  TEST_ASSERT_EQUAL_UINT8(42, rx.data[42]);
  hal_can_destroy(fd_can);
}

void test_mcp2515_rejects_can_fd_frame(void) {
  hal_can_frame_t frame = {};
  frame.id = 0x123u;
  frame.flags = HAL_CAN_FRAME_FD;
  frame.len = 12u;
  frame.dlc = hal_can_bytes_to_dlc(frame.len);

  TEST_ASSERT_FALSE(hal_can_send_frame(can, &frame));
}

void test_send_frame_rejects_invalid_fd_shape(void) {
  hal_can_frame_t frame = {};
  frame.id = 0x123u;
  frame.flags = HAL_CAN_FRAME_BRS;
  frame.dlc = 9;
  frame.len = 12;
  TEST_ASSERT_FALSE(hal_can_send_frame(can, &frame));

  frame.flags = HAL_CAN_FRAME_FD;
  frame.dlc = 9;
  frame.len = 11;
  TEST_ASSERT_FALSE(hal_can_send_frame(can, &frame));
}

void test_legacy_receive_rejects_fd_frame(void) {
  hal_can_frame_t frame = {};
  frame.id = 0x123u;
  frame.flags = HAL_CAN_FRAME_FD;
  frame.dlc = 9;
  frame.len = 12;
  hal_mock_can_inject_frame(can, &frame);

  uint32_t id;
  uint8_t len;
  uint8_t buf[HAL_CAN_MAX_DATA_LEN] = {};
  TEST_ASSERT_FALSE(hal_can_receive(can, &id, &len, buf));
}

void test_set_std_filters_validates_handle(void) {
  TEST_ASSERT_TRUE(hal_can_set_std_filters(can, 0x7E0, 0x7DF));
  TEST_ASSERT_FALSE(hal_can_set_std_filters(nullptr, 0x7E0, 0x7DF));
}

void test_static_filter_accepts_matching_frames_only(void) {
  hal_can_filter_t filter = {0x120u, 0x7F0u, 0u};
  TEST_ASSERT_TRUE(hal_can_set_filter(can, 0u, &filter));
  TEST_ASSERT_FALSE(hal_can_set_filter(can, HAL_CAN_MAX_FILTERS, &filter));

  uint8_t payload[] = {0x42};
  hal_mock_can_inject(can, 0x130u, 1u, payload);
  TEST_ASSERT_FALSE(hal_can_available(can));

  hal_mock_can_inject(can, 0x123u, 1u, payload);
  TEST_ASSERT_TRUE(hal_can_available(can));

  uint32_t id;
  uint8_t len;
  uint8_t buf[HAL_CAN_MAX_DATA_LEN] = {};
  TEST_ASSERT_TRUE(hal_can_receive(can, &id, &len, buf));
  TEST_ASSERT_EQUAL_HEX32(0x123u, id);
  TEST_ASSERT_EQUAL_UINT8(1u, len);
  TEST_ASSERT_EQUAL_HEX8(0x42u, buf[0]);
}

void test_extended_filter_matches_only_extended_frames(void) {
  hal_can_filter_t filter = {0x1ABCDE0u, 0x1FFFFFF0u, HAL_CAN_FILTER_EXTENDED};
  TEST_ASSERT_TRUE(hal_can_set_filter(can, 2u, &filter));

  hal_can_frame_t frame = {};
  frame.id = 0x5E0u;
  frame.dlc = 1;
  frame.len = 1;
  frame.data[0] = 0x11u;
  hal_mock_can_inject_frame(can, &frame);
  TEST_ASSERT_FALSE(hal_can_available(can));

  frame.id = 0x1ABCDE3u;
  frame.flags = HAL_CAN_FRAME_EXTENDED;
  hal_mock_can_inject_frame(can, &frame);
  TEST_ASSERT_TRUE(hal_can_available(can));
}

void test_start_stop_and_modes_control_send_path(void) {
  hal_can_mode_t mode = 0xFFFFFFFFu;
  TEST_ASSERT_TRUE(hal_can_get_mode(can, &mode));
  TEST_ASSERT_EQUAL_UINT32(HAL_CAN_MODE_ONE_SHOT, mode);

  TEST_ASSERT_TRUE(
      hal_can_set_mode(can, HAL_CAN_MODE_LOOPBACK | HAL_CAN_MODE_ONE_SHOT));
  TEST_ASSERT_TRUE(hal_can_get_mode(can, &mode));
  TEST_ASSERT_EQUAL_UINT32(HAL_CAN_MODE_LOOPBACK | HAL_CAN_MODE_ONE_SHOT, mode);

  TEST_ASSERT_FALSE(
      hal_can_set_mode(can, HAL_CAN_MODE_LOOPBACK | HAL_CAN_MODE_LISTEN_ONLY));
  TEST_ASSERT_FALSE(hal_can_set_mode(can, HAL_CAN_MODE_FD));

  uint8_t payload[] = {0x01};
  TEST_ASSERT_TRUE(hal_can_stop(can));
  TEST_ASSERT_FALSE(hal_can_send(can, 0x123u, 1u, payload));

  hal_can_state_t state = HAL_CAN_STATE_ERROR_ACTIVE;
  TEST_ASSERT_TRUE(hal_can_get_state(can, &state));
  TEST_ASSERT_EQUAL(HAL_CAN_STATE_STOPPED, state);

  TEST_ASSERT_TRUE(hal_can_start(can));
  TEST_ASSERT_TRUE(hal_can_send(can, 0x123u, 1u, payload));

  TEST_ASSERT_TRUE(hal_can_set_mode(can, HAL_CAN_MODE_SLEEP));
  TEST_ASSERT_FALSE(hal_can_send(can, 0x123u, 1u, payload));
}

void test_state_and_error_counters_are_reported(void) {
  hal_can_state_t state = HAL_CAN_STATE_STOPPED;
  TEST_ASSERT_TRUE(hal_can_get_state(can, &state));
  TEST_ASSERT_EQUAL(HAL_CAN_STATE_ERROR_ACTIVE, state);

  hal_mock_can_set_state(can, HAL_CAN_STATE_ERROR_PASSIVE);
  TEST_ASSERT_TRUE(hal_can_get_state(can, &state));
  TEST_ASSERT_EQUAL(HAL_CAN_STATE_ERROR_PASSIVE, state);

  hal_mock_can_set_error_counters(can, 17u, 23u);
  hal_can_error_counters_t counters = {};
  TEST_ASSERT_TRUE(hal_can_get_error_counters(can, &counters));
  TEST_ASSERT_EQUAL_UINT8(17u, counters.tx);
  TEST_ASSERT_EQUAL_UINT8(23u, counters.rx);

  TEST_ASSERT_FALSE(hal_can_get_state(nullptr, &state));
  TEST_ASSERT_FALSE(hal_can_get_error_counters(can, nullptr));
}

void test_encode_temp_i8_positive_values(void) {
  TEST_ASSERT_EQUAL_UINT8(25u, hal_can_encode_temp_i8(25.0f));
  TEST_ASSERT_EQUAL_UINT8(100u, hal_can_encode_temp_i8(100.0f));
  TEST_ASSERT_EQUAL_UINT8(0u, hal_can_encode_temp_i8(0.0f));
}

void test_encode_temp_i8_negative_values(void) {
  TEST_ASSERT_EQUAL_UINT8((uint8_t)(int8_t)-10, hal_can_encode_temp_i8(-10.0f));
  TEST_ASSERT_EQUAL_UINT8((uint8_t)(int8_t)-40, hal_can_encode_temp_i8(-40.0f));
  TEST_ASSERT_EQUAL_UINT8((uint8_t)(int8_t)-1, hal_can_encode_temp_i8(-1.0f));
}

void test_encode_temp_i8_boundary_values(void) {
  TEST_ASSERT_EQUAL_UINT8((uint8_t)(int8_t)127, hal_can_encode_temp_i8(127.0f));
  TEST_ASSERT_EQUAL_UINT8((uint8_t)(int8_t)-128,
                          hal_can_encode_temp_i8(-128.0f));
}

void test_encode_temp_i8_saturates_out_of_range(void) {
  TEST_ASSERT_EQUAL_UINT8((uint8_t)(int8_t)127, hal_can_encode_temp_i8(200.0f));
  TEST_ASSERT_EQUAL_UINT8((uint8_t)(int8_t)127,
                          hal_can_encode_temp_i8(1000.0f));
  TEST_ASSERT_EQUAL_UINT8((uint8_t)(int8_t)-128,
                          hal_can_encode_temp_i8(-200.0f));
  TEST_ASSERT_EQUAL_UINT8((uint8_t)(int8_t)-128,
                          hal_can_encode_temp_i8(-1000.0f));
}

void test_encode_temp_i8_truncates_fractional_values(void) {
  TEST_ASSERT_EQUAL_UINT8((uint8_t)(int8_t)-10, hal_can_encode_temp_i8(-10.9f));
  TEST_ASSERT_EQUAL_UINT8(99u, hal_can_encode_temp_i8(99.9f));
}

void test_process_all_drains_queue_and_skips_invalid_frames(void) {
  uint8_t a[] = {0x11, 0x22};
  uint8_t b[] = {0x33, 0x44};
  uint8_t c[] = {0x55};
  uint8_t d[] = {0xAA};

  hal_mock_can_inject(can, 0x123, 2, a); // valid
  hal_mock_can_inject(can, 0x000, 2, b); // invalid (id == 0)
  hal_mock_can_inject(can, 0x456, 0, c); // invalid (len == 0)
  hal_mock_can_inject(can, 0x321, 1, d); // valid

  int processed = hal_can_process_all(can, test_can_frame_cb);

  TEST_ASSERT_EQUAL_INT(2, processed);
  TEST_ASSERT_EQUAL_INT(2, s_frame_count);
  TEST_ASSERT_EQUAL_HEX32(0x321, s_last_id);
  TEST_ASSERT_EQUAL_UINT8(1, s_last_len);
  TEST_ASSERT_EQUAL_HEX8(0xAA, s_last_payload[0]);
  TEST_ASSERT_FALSE(hal_can_available(can));
}

void test_process_all_returns_zero_on_null_args(void) {
  TEST_ASSERT_EQUAL_INT(0, hal_can_process_all(nullptr, test_can_frame_cb));
  TEST_ASSERT_EQUAL_INT(0, hal_can_process_all(can, nullptr));
}

void test_create_with_retry_returns_handle_and_sets_irq_pin_mode(void) {
  // Pre-set mode so we can verify helper reconfigures it.
  hal_gpio_set_mode(7, HAL_GPIO_OUTPUT);
  TEST_ASSERT_TRUE(hal_mock_gpio_is_output(7));

  hal_mock_set_millis(0);
  hal_can_config_t cfg = hal_can_default_config();
  hal_can_t h =
      hal_can_create_with_retry(&cfg, 7, nullptr, 3, test_retry_idle_cb);

  TEST_ASSERT_NOT_NULL(h);
  TEST_ASSERT_EQUAL(HAL_GPIO_INPUT, hal_mock_gpio_get_mode(7));
  TEST_ASSERT_EQUAL_UINT32(0, hal_millis());
  TEST_ASSERT_EQUAL_INT(0, s_retry_idle_calls);
  hal_can_destroy(h);
}

void test_create_with_retry_skips_irq_setup_with_no_int_pin(void) {
  // Keep pin in OUTPUT mode; helper should not touch it with NO_INT sentinel.
  hal_gpio_set_mode(9, HAL_GPIO_OUTPUT);
  TEST_ASSERT_TRUE(hal_mock_gpio_is_output(9));

  hal_can_config_t cfg = hal_can_default_config();
  hal_can_t h = hal_can_create_with_retry(&cfg, HAL_CAN_NO_INT_PIN, nullptr, 2,
                                          test_retry_idle_cb);

  TEST_ASSERT_NOT_NULL(h);
  TEST_ASSERT_EQUAL(HAL_GPIO_OUTPUT, hal_mock_gpio_get_mode(9));
  TEST_ASSERT_EQUAL_INT(0, s_retry_idle_calls);
  hal_can_destroy(h);
}

int main(void) {
  UNITY_BEGIN();
  RUN_TEST(test_create_returns_valid_handle);
  RUN_TEST(test_default_config_selects_mcp2515_backend);
  RUN_TEST(test_mcp251xfd_config_can_create_fd_capable_handle);
  RUN_TEST(test_create_rejects_unsupported_backend);
  RUN_TEST(test_send_stores_frame);
  RUN_TEST(test_receive_injected_frame);
  RUN_TEST(test_available_false_when_empty);
  RUN_TEST(test_receive_consumes_frame);
  RUN_TEST(test_reset_clears_buffers);
  RUN_TEST(test_send_null_handle_returns_false);
  RUN_TEST(test_send_null_data_with_nonzero_len_returns_false);
  RUN_TEST(test_send_clamps_payload_len_to_max);
  RUN_TEST(test_dlc_helpers_cover_classic_and_fd_lengths);
  RUN_TEST(test_frame_validation_rejects_invalid_id_flags_and_lengths);
  RUN_TEST(test_filter_validation_and_frame_matching);
  RUN_TEST(test_send_frame_stores_extended_rtr_frame);
  RUN_TEST(test_send_receive_can_fd_frame_via_mock_frame_api);
  RUN_TEST(test_mcp2515_rejects_can_fd_frame);
  RUN_TEST(test_send_frame_rejects_invalid_fd_shape);
  RUN_TEST(test_legacy_receive_rejects_fd_frame);
  RUN_TEST(test_set_std_filters_validates_handle);
  RUN_TEST(test_static_filter_accepts_matching_frames_only);
  RUN_TEST(test_extended_filter_matches_only_extended_frames);
  RUN_TEST(test_start_stop_and_modes_control_send_path);
  RUN_TEST(test_state_and_error_counters_are_reported);
  RUN_TEST(test_encode_temp_i8_positive_values);
  RUN_TEST(test_encode_temp_i8_negative_values);
  RUN_TEST(test_encode_temp_i8_boundary_values);
  RUN_TEST(test_encode_temp_i8_saturates_out_of_range);
  RUN_TEST(test_encode_temp_i8_truncates_fractional_values);
  RUN_TEST(test_process_all_drains_queue_and_skips_invalid_frames);
  RUN_TEST(test_process_all_returns_zero_on_null_args);
  RUN_TEST(test_create_with_retry_returns_handle_and_sets_irq_pin_mode);
  RUN_TEST(test_create_with_retry_skips_irq_setup_with_no_int_pin);
  return UNITY_END();
}
