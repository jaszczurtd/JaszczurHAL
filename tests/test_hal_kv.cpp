#include "utils/unity.h"
#include "hal/hal_eeprom.h"
#include "hal/hal_kv.h"
#include "hal/impl/.mock/hal_mock.h"

#include <atomic>
#include <thread>
#include <vector>

void setUp(void) {
    hal_mock_serial_reset();
    hal_mock_eeprom_reset();
    hal_eeprom_init(HAL_EEPROM_RP2040, 1024, 0x50);
    TEST_ASSERT_TRUE(hal_kv_init(0, 512));
}

void tearDown(void) {}

void test_set_get_u32_and_reinit(void) {
    TEST_ASSERT_TRUE(hal_kv_set_u32(100, 0x12345678u));

    uint32_t out = 0;
    TEST_ASSERT_TRUE(hal_kv_get_u32(100, &out));
    TEST_ASSERT_EQUAL_HEX32(0x12345678u, out);

    TEST_ASSERT_TRUE(hal_kv_init(0, 512));
    out = 0;
    TEST_ASSERT_TRUE(hal_kv_get_u32(100, &out));
    TEST_ASSERT_EQUAL_HEX32(0x12345678u, out);
}

void test_blob_roundtrip_and_length_query(void) {
    const uint8_t data[] = {1, 2, 3, 4, 5, 6, 7};
    TEST_ASSERT_TRUE(hal_kv_set_blob(200, data, sizeof(data)));

    uint16_t out_len = 0;
    TEST_ASSERT_TRUE(hal_kv_get_blob(200, NULL, 0, &out_len));
    TEST_ASSERT_EQUAL_UINT16((uint16_t)sizeof(data), out_len);

    uint8_t out[16] = {0};
    TEST_ASSERT_TRUE(hal_kv_get_blob(200, out, sizeof(out), &out_len));
    TEST_ASSERT_EQUAL_UINT16((uint16_t)sizeof(data), out_len);
    TEST_ASSERT_EQUAL_UINT8_ARRAY(data, out, sizeof(data));
}

void test_delete_removes_key(void) {
    TEST_ASSERT_TRUE(hal_kv_set_u32(300, 42));

    uint32_t out = 0;
    TEST_ASSERT_TRUE(hal_kv_get_u32(300, &out));
    TEST_ASSERT_EQUAL_UINT32(42u, out);

    TEST_ASSERT_TRUE(hal_kv_delete(300));
    TEST_ASSERT_FALSE(hal_kv_get_u32(300, &out));
}

void test_unchanged_value_skips_writes(void) {
    TEST_ASSERT_TRUE(hal_kv_set_u32(400, 777u));

    hal_mock_eeprom_clear_write_count();
    TEST_ASSERT_TRUE(hal_kv_set_u32(400, 777u));
    TEST_ASSERT_EQUAL_UINT32(0u, hal_mock_eeprom_get_write_count());
}

void test_gc_and_concurrent_updates(void) {
    std::atomic<bool> failed(false);

    auto worker = [&failed](uint16_t key, uint32_t base) {
        for (uint32_t i = 0; i < 40; i++) {
            if (!hal_kv_set_u32(key, base + i)) {
                failed.store(true);
                return;
            }
        }
    };

    std::vector<std::thread> threads;
    threads.emplace_back(worker, 501, 1000);
    threads.emplace_back(worker, 502, 2000);
    threads.emplace_back(worker, 503, 3000);

    for (auto &t : threads) {
        t.join();
    }

    TEST_ASSERT_FALSE(failed.load());

    TEST_ASSERT_TRUE(hal_kv_gc());

    uint32_t v1 = 0, v2 = 0, v3 = 0;
    TEST_ASSERT_TRUE(hal_kv_get_u32(501, &v1));
    TEST_ASSERT_TRUE(hal_kv_get_u32(502, &v2));
    TEST_ASSERT_TRUE(hal_kv_get_u32(503, &v3));
    TEST_ASSERT_EQUAL_UINT32(1039u, v1);
    TEST_ASSERT_EQUAL_UINT32(2039u, v2);
    TEST_ASSERT_EQUAL_UINT32(3039u, v3);

    hal_kv_stats_t st = {};
    TEST_ASSERT_TRUE(hal_kv_get_stats(&st));
    TEST_ASSERT_TRUE(st.generation >= 2u);
    TEST_ASSERT_TRUE(st.key_count >= 3u);
}

int main(void) {
    UNITY_BEGIN();
    RUN_TEST(test_set_get_u32_and_reinit);
    RUN_TEST(test_blob_roundtrip_and_length_query);
    RUN_TEST(test_delete_removes_key);
    RUN_TEST(test_unchanged_value_skips_writes);
    RUN_TEST(test_gc_and_concurrent_updates);
    return UNITY_END();
}
