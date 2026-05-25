#include "utils/unity.h"
#include "hal/hal_ds18b20.h"
#include "hal/impl/.mock/hal_mock.h"

#include <atomic>
#include <thread>
#include <vector>

#ifdef HAL_ENABLE_DS18B20

static hal_ds18b20_t s_sensor = nullptr;

static hal_ds18b20_config_t default_cfg(void) {
    hal_ds18b20_config_t cfg = {};
    cfg.data_pin = 7;
    cfg.use_rom = false;
    cfg.resolution_hint = HAL_DS18B20_RES_12_BIT;
    return cfg;
}

void setUp(void) {
    hal_mock_set_micros(0);
    hal_ds18b20_config_t cfg = default_cfg();
    s_sensor = hal_ds18b20_init(&cfg);
}

void tearDown(void) {
    hal_ds18b20_deinit(s_sensor);
    s_sensor = nullptr;
}

void test_init_returns_handle(void) {
    TEST_ASSERT_NOT_NULL(s_sensor);
}

void test_init_with_rom_returns_handle(void) {
    hal_ds18b20_config_t cfg = default_cfg();
    cfg.use_rom = true;
    cfg.rom_code[0] = 0x28;
    cfg.rom_code[1] = 0xAA;
    cfg.rom_code[2] = 0xBB;
    cfg.rom_code[3] = 0xCC;
    cfg.rom_code[4] = 0xDD;
    cfg.rom_code[5] = 0xEE;
    cfg.rom_code[6] = 0x11;
    cfg.rom_code[7] = 0x22;

    hal_ds18b20_t h_rom = hal_ds18b20_init(&cfg);
    TEST_ASSERT_NOT_NULL(h_rom);
    hal_ds18b20_deinit(h_rom);
}

void test_request_marks_sensor_busy(void) {
    TEST_ASSERT_TRUE(hal_ds18b20_request(s_sensor));
    TEST_ASSERT_TRUE(hal_ds18b20_is_busy(s_sensor));
    TEST_ASSERT_EQUAL_UINT32(1u, hal_mock_ds18b20_get_request_count(s_sensor));
}

void test_poll_before_deadline_keeps_busy_and_no_sample(void) {
    float t = 0.0f;
    bool fresh = false;

    TEST_ASSERT_TRUE(hal_ds18b20_request(s_sensor));
    hal_mock_advance_micros(200000u);
    hal_ds18b20_poll(s_sensor);

    TEST_ASSERT_TRUE(hal_ds18b20_is_busy(s_sensor));
    TEST_ASSERT_FALSE(hal_ds18b20_take_latest(s_sensor, &t, &fresh));
}

void test_poll_after_deadline_publishes_fresh_sample(void) {
    float t = 0.0f;
    bool fresh = false;

    hal_mock_ds18b20_set_next_temp(s_sensor, 42.5f);
    TEST_ASSERT_TRUE(hal_ds18b20_request(s_sensor));
    hal_mock_advance_micros(750000u);
    hal_ds18b20_poll(s_sensor);

    TEST_ASSERT_FALSE(hal_ds18b20_is_busy(s_sensor));
    TEST_ASSERT_TRUE(hal_ds18b20_take_latest(s_sensor, &t, &fresh));
    TEST_ASSERT_TRUE(fresh);
    TEST_ASSERT_FLOAT_WITHIN(0.01f, 42.5f, t);

    TEST_ASSERT_TRUE(hal_ds18b20_take_latest(s_sensor, &t, &fresh));
    TEST_ASSERT_FALSE(fresh);
    TEST_ASSERT_FLOAT_WITHIN(0.01f, 42.5f, t);
}

void test_request_while_busy_returns_false(void) {
    TEST_ASSERT_TRUE(hal_ds18b20_request(s_sensor));
    TEST_ASSERT_FALSE(hal_ds18b20_request(s_sensor));
    TEST_ASSERT_EQUAL_UINT32(1u, hal_mock_ds18b20_get_request_count(s_sensor));
}

void test_missing_presence_rejects_request(void) {
    hal_mock_ds18b20_set_presence(s_sensor, false);
    TEST_ASSERT_FALSE(hal_ds18b20_request(s_sensor));
    TEST_ASSERT_FALSE(hal_ds18b20_is_busy(s_sensor));
}

void test_crc_error_keeps_previous_sample_and_not_fresh(void) {
    float t = 0.0f;
    bool fresh = false;

    hal_mock_ds18b20_set_next_temp(s_sensor, 10.0f);
    TEST_ASSERT_TRUE(hal_ds18b20_request(s_sensor));
    hal_mock_advance_micros(750000u);
    hal_ds18b20_poll(s_sensor);
    TEST_ASSERT_TRUE(hal_ds18b20_take_latest(s_sensor, &t, &fresh));
    TEST_ASSERT_TRUE(fresh);
    TEST_ASSERT_FLOAT_WITHIN(0.01f, 10.0f, t);

    hal_mock_ds18b20_set_crc_ok(s_sensor, false);
    hal_mock_ds18b20_set_next_temp(s_sensor, 20.0f);
    TEST_ASSERT_TRUE(hal_ds18b20_request(s_sensor));
    hal_mock_advance_micros(750000u);
    hal_ds18b20_poll(s_sensor);

    TEST_ASSERT_TRUE(hal_ds18b20_take_latest(s_sensor, &t, &fresh));
    TEST_ASSERT_FALSE(fresh);
    TEST_ASSERT_FLOAT_WITHIN(0.01f, 10.0f, t);
}

void test_take_latest_is_thread_safe_for_concurrent_readers(void) {
    hal_mock_ds18b20_set_next_temp(s_sensor, 24.75f);
    TEST_ASSERT_TRUE(hal_ds18b20_request(s_sensor));
    hal_mock_advance_micros(750000u);
    hal_ds18b20_poll(s_sensor);

    std::atomic<bool> ok(true);
    auto worker = [&ok]() {
        for (int i = 0; i < 1500; ++i) {
            float t = 0.0f;
            bool fresh = false;
            if (!hal_ds18b20_take_latest(s_sensor, &t, &fresh)) {
                ok.store(false);
                return;
            }
            if (hal_ds18b20_is_busy(s_sensor)) {
                ok.store(false);
                return;
            }
        }
    };

    std::vector<std::thread> threads;
    threads.emplace_back(worker);
    threads.emplace_back(worker);
    threads.emplace_back(worker);

    for (auto &t : threads) {
        t.join();
    }

    TEST_ASSERT_TRUE(ok.load());
}

int main(void) {
    UNITY_BEGIN();
    RUN_TEST(test_init_returns_handle);
    RUN_TEST(test_init_with_rom_returns_handle);
    RUN_TEST(test_request_marks_sensor_busy);
    RUN_TEST(test_poll_before_deadline_keeps_busy_and_no_sample);
    RUN_TEST(test_poll_after_deadline_publishes_fresh_sample);
    RUN_TEST(test_request_while_busy_returns_false);
    RUN_TEST(test_missing_presence_rejects_request);
    RUN_TEST(test_crc_error_keeps_previous_sample_and_not_fresh);
    RUN_TEST(test_take_latest_is_thread_safe_for_concurrent_readers);
    return UNITY_END();
}

#else

int main(void) {
    UNITY_BEGIN();
    return UNITY_END();
}

#endif /* HAL_ENABLE_DS18B20 */
