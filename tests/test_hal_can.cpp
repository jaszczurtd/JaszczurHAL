#include "utils/unity.h"
#include "hal/hal_can.h"
#include "hal/hal_system.h"
#include "hal/impl/.mock/hal_mock.h"

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

static void test_retry_idle_cb(void) {
    s_retry_idle_calls++;
}

void setUp(void) {
    can = hal_can_create(0);
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

void test_create_returns_valid_handle(void) {
    TEST_ASSERT_NOT_NULL(can);
}

void test_send_stores_frame(void) {
    uint8_t data[] = {0x01, 0x02, 0x03};
    TEST_ASSERT_TRUE(hal_can_send(can, 0x100, 3, data));

    uint32_t id; uint8_t len, buf[8];
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

    uint32_t id; uint8_t len, buf[8];
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
    uint32_t id; uint8_t len, buf[8];
    hal_can_receive(can, &id, &len, buf);
    TEST_ASSERT_FALSE(hal_can_available(can));
}

void test_reset_clears_buffers(void) {
    uint8_t data[] = {0x01};
    hal_mock_can_inject(can, 0x1, 1, data);
    hal_can_send(can, 0x2, 1, data);
    hal_mock_can_reset(can);
    TEST_ASSERT_FALSE(hal_can_available(can));
    uint32_t id; uint8_t len, buf[8];
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

    uint32_t id; uint8_t len, buf[8] = {};
    TEST_ASSERT_TRUE(hal_mock_can_get_sent(can, &id, &len, buf));
    TEST_ASSERT_EQUAL_HEX32(0x321, id);
    TEST_ASSERT_EQUAL_UINT8(HAL_CAN_MAX_DATA_LEN, len);
    TEST_ASSERT_EQUAL_HEX8(0x01, buf[0]);
    TEST_ASSERT_EQUAL_HEX8(0x08, buf[7]);
}

void test_set_std_filters_validates_handle(void) {
    TEST_ASSERT_TRUE(hal_can_set_std_filters(can, 0x7E0, 0x7DF));
    TEST_ASSERT_FALSE(hal_can_set_std_filters(nullptr, 0x7E0, 0x7DF));
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
    TEST_ASSERT_EQUAL_UINT8((uint8_t)(int8_t)-128, hal_can_encode_temp_i8(-128.0f));
}

void test_encode_temp_i8_saturates_out_of_range(void) {
    TEST_ASSERT_EQUAL_UINT8((uint8_t)(int8_t)127, hal_can_encode_temp_i8(200.0f));
    TEST_ASSERT_EQUAL_UINT8((uint8_t)(int8_t)127, hal_can_encode_temp_i8(1000.0f));
    TEST_ASSERT_EQUAL_UINT8((uint8_t)(int8_t)-128, hal_can_encode_temp_i8(-200.0f));
    TEST_ASSERT_EQUAL_UINT8((uint8_t)(int8_t)-128, hal_can_encode_temp_i8(-1000.0f));
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

    hal_mock_can_inject(can, 0x123, 2, a);   // valid
    hal_mock_can_inject(can, 0x000, 2, b);   // invalid (id == 0)
    hal_mock_can_inject(can, 0x456, 0, c);   // invalid (len == 0)
    hal_mock_can_inject(can, 0x321, 1, d);   // valid

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
    hal_can_t h = hal_can_create_with_retry(0, 7, nullptr, 3, test_retry_idle_cb);

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

    hal_can_t h = hal_can_create_with_retry(0, HAL_CAN_NO_INT_PIN, nullptr, 2, test_retry_idle_cb);

    TEST_ASSERT_NOT_NULL(h);
    TEST_ASSERT_EQUAL(HAL_GPIO_OUTPUT, hal_mock_gpio_get_mode(9));
    TEST_ASSERT_EQUAL_INT(0, s_retry_idle_calls);
    hal_can_destroy(h);
}

int main(void) {
    UNITY_BEGIN();
    RUN_TEST(test_create_returns_valid_handle);
    RUN_TEST(test_send_stores_frame);
    RUN_TEST(test_receive_injected_frame);
    RUN_TEST(test_available_false_when_empty);
    RUN_TEST(test_receive_consumes_frame);
    RUN_TEST(test_reset_clears_buffers);
    RUN_TEST(test_send_null_handle_returns_false);
    RUN_TEST(test_send_null_data_with_nonzero_len_returns_false);
    RUN_TEST(test_send_clamps_payload_len_to_max);
    RUN_TEST(test_set_std_filters_validates_handle);
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
