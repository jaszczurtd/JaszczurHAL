#include "utils/unity.h"
#include "hal/hal_onewire.h"
#include "hal/impl/.mock/hal_mock.h"

#include <thread>
#include <vector>

#ifdef HAL_ENABLE_ONEWIRE

static hal_onewire_t s_bus = NULL;

void setUp(void) {
    s_bus = hal_onewire_init(6);
}

void tearDown(void) {
    hal_onewire_deinit(s_bus);
    s_bus = NULL;
}

void test_init_returns_handle_and_presence_by_default(void) {
    TEST_ASSERT_NOT_NULL(s_bus);
    TEST_ASSERT_TRUE(hal_onewire_reset(s_bus));
    TEST_ASSERT_EQUAL_UINT32(1u, hal_mock_onewire_get_reset_count(s_bus));
}

void test_read_write_and_bit_operations(void) {
    const uint8_t write_payload[] = {0xA1, 0xB2, 0xC3};
    const uint8_t read_payload[] = {0x11, 0x22, 0x03};

    hal_onewire_write(s_bus, 0x5A, true);
    TEST_ASSERT_EQUAL_UINT8(0x5A, hal_mock_onewire_get_last_write(s_bus));

    TEST_ASSERT_EQUAL_UINT32(3u, (uint32_t)hal_onewire_write_bytes(s_bus, write_payload, 3u, false));
    TEST_ASSERT_EQUAL_UINT8(0xC3, hal_mock_onewire_get_last_write(s_bus));

    hal_onewire_write_bit(s_bus, 1u);
    TEST_ASSERT_EQUAL_UINT8(1u, hal_mock_onewire_get_last_write_bit(s_bus));

    hal_mock_onewire_inject_read(s_bus, read_payload, 3);

    TEST_ASSERT_EQUAL_UINT8(0x11, hal_onewire_read(s_bus));

    uint8_t out[2] = {0, 0};
    TEST_ASSERT_EQUAL_UINT32(2u, (uint32_t)hal_onewire_read_bytes(s_bus, out, 2u));
    TEST_ASSERT_EQUAL_UINT8(0x22, out[0]);
    TEST_ASSERT_EQUAL_UINT8(0x03, out[1]);
}

void test_select_skip_and_search_with_target_family(void) {
    const uint8_t rom_a[8] = {0x28, 0xAA, 0x01, 0x02, 0x03, 0x04, 0x05, 0x06};
    const uint8_t rom_b[8] = {0x10, 0xBB, 0x10, 0x20, 0x30, 0x40, 0x50, 0x60};

    hal_onewire_select(s_bus, rom_a);
    uint8_t selected[8] = {0};
    TEST_ASSERT_TRUE(hal_mock_onewire_get_last_selected_rom(s_bus, selected));
    TEST_ASSERT_EQUAL_UINT8_ARRAY(rom_a, selected, 8);

    hal_onewire_skip(s_bus);
    TEST_ASSERT_EQUAL_UINT32(1u, hal_mock_onewire_get_skip_count(s_bus));

    hal_mock_onewire_reset_search_roms(s_bus);
    TEST_ASSERT_TRUE(hal_mock_onewire_push_search_rom(s_bus, rom_a));
    TEST_ASSERT_TRUE(hal_mock_onewire_push_search_rom(s_bus, rom_b));

    hal_onewire_reset_search(s_bus);
    uint8_t found[8] = {0};
    TEST_ASSERT_TRUE(hal_onewire_search(s_bus, found, true));
    TEST_ASSERT_EQUAL_UINT8_ARRAY(rom_a, found, 8);

    hal_onewire_target_search(s_bus, 0x10);
    TEST_ASSERT_TRUE(hal_onewire_search(s_bus, found, true));
    TEST_ASSERT_EQUAL_UINT8_ARRAY(rom_b, found, 8);
    TEST_ASSERT_FALSE(hal_onewire_search(s_bus, found, true));
}

void test_presence_can_be_forced_false(void) {
    hal_mock_onewire_set_presence(s_bus, false);
    TEST_ASSERT_FALSE(hal_onewire_reset(s_bus));
}

void test_thread_safety_for_concurrent_calls(void) {
    static const int kThreads = 3;
    static const int kIterations = 1200;

    auto worker = []() {
        for (int i = 0; i < kIterations; ++i) {
            hal_onewire_write(s_bus, (uint8_t)(i & 0xFF), false);
            (void)hal_onewire_reset(s_bus);
            hal_onewire_skip(s_bus);
        }
    };

    std::vector<std::thread> threads;
    for (int i = 0; i < kThreads; ++i) {
        threads.emplace_back(worker);
    }
    for (auto &t : threads) {
        t.join();
    }

    TEST_ASSERT_EQUAL_UINT32((uint32_t)(kThreads * kIterations), hal_mock_onewire_get_reset_count(s_bus));
    TEST_ASSERT_EQUAL_UINT32((uint32_t)(kThreads * kIterations), hal_mock_onewire_get_skip_count(s_bus));
    TEST_ASSERT_EQUAL_INT(1, hal_mock_onewire_get_max_lock_depth(s_bus));
}

void test_crc8_matches_reference_vector(void) {
    const uint8_t data[7] = {0x28, 0xFF, 0x6C, 0x92, 0x61, 0x16, 0x03};
    TEST_ASSERT_EQUAL_HEX8(0x34, hal_onewire_crc8(data, 7u));
}

int main(void) {
    UNITY_BEGIN();
    RUN_TEST(test_init_returns_handle_and_presence_by_default);
    RUN_TEST(test_read_write_and_bit_operations);
    RUN_TEST(test_select_skip_and_search_with_target_family);
    RUN_TEST(test_presence_can_be_forced_false);
    RUN_TEST(test_thread_safety_for_concurrent_calls);
    RUN_TEST(test_crc8_matches_reference_vector);
    return UNITY_END();
}

#else

int main(void) {
    UNITY_BEGIN();
    return UNITY_END();
}

#endif /* HAL_ENABLE_ONEWIRE */
