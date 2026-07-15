#include "hal/hal_ds18b20.h"
#include "hal/impl/.mock/hal_mock.h"
#include "utils/unity.h"

#include <atomic>
#include <thread>
#include <vector>

/* Datasheet anchors used by these tests (DS18B20):
 * - Programmable conversion times vs. resolution:
 *   9-bit 93.75ms, 10-bit 187.5ms, 11-bit 375ms, 12-bit 750ms (tCONV)
 * - Family/ROM and scratchpad/CRC concepts are represented by backend state
 *   in the mock; these tests validate the public HAL state machine behavior
 *   (request/poll/take_latest, busy/fresh/error paths) against those facts.
 */

#ifdef HAL_ENABLE_DS18B20

static hal_ds18b20_t s_sensor = nullptr;

static hal_ds18b20_config_t default_cfg(void) {
  hal_ds18b20_config_t cfg = {};
  cfg.data_pin = 7;
  cfg.use_rom = false;
  cfg.resolution_hint = HAL_DS18B20_RES_12_BIT;
  return cfg;
}

static void require_ready_sample(hal_ds18b20_t h, float expected) {
  float t = 0.0f;
  bool fresh = false;
  TEST_ASSERT_EQUAL_INT(HAL_OK, hal_ds18b20_take_latest_ex(h, &t, &fresh));
  TEST_ASSERT_TRUE(fresh);
  TEST_ASSERT_FLOAT_WITHIN(0.01f, expected, t);
}

void setUp(void) {
  hal_mock_critical_section_reset();
  hal_mock_set_micros(0);
  hal_ds18b20_config_t cfg = default_cfg();
  s_sensor = hal_ds18b20_init(&cfg);
}

void tearDown(void) {
  hal_ds18b20_deinit(s_sensor);
  s_sensor = nullptr;
  /* The init + deinit pool-slot critical sections must balance to zero;
   * a leaked enter/exit in the HAL would surface here. */
  TEST_ASSERT_EQUAL_UINT32(0, hal_mock_critical_depth());
  TEST_ASSERT_TRUE(hal_mock_irq_enabled());
}

void test_init_returns_handle(void) { TEST_ASSERT_NOT_NULL(s_sensor); }

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

  hal_ds18b20_t h_rom = nullptr;
  TEST_ASSERT_EQUAL_INT(HAL_OK, hal_ds18b20_init_ex(&cfg, &h_rom));
  TEST_ASSERT_NOT_NULL(h_rom);
  TEST_ASSERT_EQUAL_INT(HAL_OK, hal_ds18b20_deinit(h_rom));
}

void test_init_rejects_null_config(void) {
  TEST_ASSERT_NULL(hal_ds18b20_init(nullptr));
  hal_ds18b20_t h = (hal_ds18b20_t)0x1;
  TEST_ASSERT_EQUAL_INT(HAL_EINVAL, hal_ds18b20_init_ex(nullptr, &h));
  TEST_ASSERT_NULL(h);
  hal_ds18b20_config_t cfg = default_cfg();
  TEST_ASSERT_EQUAL_INT(HAL_EINVAL, hal_ds18b20_init_ex(&cfg, nullptr));
}

void test_request_marks_sensor_busy(void) {
  TEST_ASSERT_EQUAL_INT(HAL_OK, hal_ds18b20_request_ex(s_sensor));
  TEST_ASSERT_TRUE(hal_ds18b20_is_busy(s_sensor));
  TEST_ASSERT_EQUAL_UINT32(1u, hal_mock_ds18b20_get_request_count(s_sensor));
}

void test_poll_before_deadline_keeps_busy_and_no_sample(void) {
  float t = 0.0f;
  bool fresh = false;

  TEST_ASSERT_EQUAL_INT(HAL_OK, hal_ds18b20_request_ex(s_sensor));
  hal_mock_advance_micros(200000u);
  TEST_ASSERT_EQUAL_INT(HAL_EAGAIN, hal_ds18b20_poll(s_sensor));

  TEST_ASSERT_TRUE(hal_ds18b20_is_busy(s_sensor));
  TEST_ASSERT_EQUAL_INT(HAL_ENOENT,
                        hal_ds18b20_take_latest_ex(s_sensor, &t, &fresh));
}

void test_poll_after_deadline_publishes_fresh_sample(void) {
  float t = 0.0f;
  bool fresh = false;

  hal_mock_ds18b20_set_next_temp(s_sensor, 42.5f);
  TEST_ASSERT_EQUAL_INT(HAL_OK, hal_ds18b20_request_ex(s_sensor));
  hal_mock_advance_micros(750000u);
  TEST_ASSERT_EQUAL_INT(HAL_OK, hal_ds18b20_poll(s_sensor));

  TEST_ASSERT_FALSE(hal_ds18b20_is_busy(s_sensor));
  TEST_ASSERT_EQUAL_INT(HAL_OK,
                        hal_ds18b20_take_latest_ex(s_sensor, &t, &fresh));
  TEST_ASSERT_TRUE(fresh);
  TEST_ASSERT_FLOAT_WITHIN(0.01f, 42.5f, t);

  TEST_ASSERT_EQUAL_INT(HAL_OK,
                        hal_ds18b20_take_latest_ex(s_sensor, &t, &fresh));
  TEST_ASSERT_FALSE(fresh);
  TEST_ASSERT_FLOAT_WITHIN(0.01f, 42.5f, t);
}

void test_resolution_hint_controls_conversion_deadline_9_to_12_bit(void) {
  struct {
    hal_ds18b20_resolution_t res;
    uint32_t tconv_us;
  } const vectors[] = {
      {HAL_DS18B20_RES_9_BIT, 93750u},
      {HAL_DS18B20_RES_10_BIT, 187500u},
      {HAL_DS18B20_RES_11_BIT, 375000u},
      {HAL_DS18B20_RES_12_BIT, 750000u},
  };

  for (size_t i = 0; i < (sizeof(vectors) / sizeof(vectors[0])); ++i) {
    hal_ds18b20_config_t cfg = default_cfg();
    cfg.resolution_hint = vectors[i].res;
    hal_ds18b20_t h = hal_ds18b20_init(&cfg);
    TEST_ASSERT_NOT_NULL(h);

    hal_mock_ds18b20_set_next_temp(h, 30.0f + (float)i);
    TEST_ASSERT_EQUAL_INT(HAL_OK, hal_ds18b20_request_ex(h));

    if (vectors[i].tconv_us > 0u) {
      hal_mock_advance_micros(vectors[i].tconv_us - 1u);
    }
    TEST_ASSERT_EQUAL_INT(HAL_EAGAIN, hal_ds18b20_poll(h));
    TEST_ASSERT_TRUE(hal_ds18b20_is_busy(h));

    hal_mock_advance_micros(1u);
    TEST_ASSERT_EQUAL_INT(HAL_OK, hal_ds18b20_poll(h));
    TEST_ASSERT_FALSE(hal_ds18b20_is_busy(h));
    require_ready_sample(h, 30.0f + (float)i);

    TEST_ASSERT_EQUAL_INT(HAL_OK, hal_ds18b20_deinit(h));
  }
}

void test_invalid_resolution_hint_falls_back_to_12_bit_timing(void) {
  hal_ds18b20_config_t cfg = default_cfg();
  cfg.resolution_hint = (hal_ds18b20_resolution_t)123;
  hal_ds18b20_t h = hal_ds18b20_init(&cfg);
  TEST_ASSERT_NOT_NULL(h);

  hal_mock_ds18b20_set_next_temp(h, 11.5f);
  TEST_ASSERT_EQUAL_INT(HAL_OK, hal_ds18b20_request_ex(h));
  hal_mock_advance_micros(749999u);
  TEST_ASSERT_EQUAL_INT(HAL_EAGAIN, hal_ds18b20_poll(h));
  TEST_ASSERT_TRUE(hal_ds18b20_is_busy(h));

  hal_mock_advance_micros(1u);
  TEST_ASSERT_EQUAL_INT(HAL_OK, hal_ds18b20_poll(h));
  TEST_ASSERT_FALSE(hal_ds18b20_is_busy(h));
  require_ready_sample(h, 11.5f);

  TEST_ASSERT_EQUAL_INT(HAL_OK, hal_ds18b20_deinit(h));
}

void test_request_while_busy_returns_false(void) {
  TEST_ASSERT_EQUAL_INT(HAL_OK, hal_ds18b20_request_ex(s_sensor));
  TEST_ASSERT_EQUAL_INT(HAL_EBUSY, hal_ds18b20_request_ex(s_sensor));
  TEST_ASSERT_FALSE(hal_ds18b20_request(s_sensor));
  TEST_ASSERT_EQUAL_UINT32(1u, hal_mock_ds18b20_get_request_count(s_sensor));
}

void test_missing_presence_rejects_request(void) {
  hal_mock_ds18b20_set_presence(s_sensor, false);
  TEST_ASSERT_EQUAL_INT(HAL_ENOENT, hal_ds18b20_request_ex(s_sensor));
  TEST_ASSERT_FALSE(hal_ds18b20_request(s_sensor));
  TEST_ASSERT_FALSE(hal_ds18b20_is_busy(s_sensor));
}

void test_crc_error_keeps_previous_sample_and_not_fresh(void) {
  float t = 0.0f;
  bool fresh = false;

  hal_mock_ds18b20_set_next_temp(s_sensor, 10.0f);
  TEST_ASSERT_EQUAL_INT(HAL_OK, hal_ds18b20_request_ex(s_sensor));
  hal_mock_advance_micros(750000u);
  TEST_ASSERT_EQUAL_INT(HAL_OK, hal_ds18b20_poll(s_sensor));
  TEST_ASSERT_EQUAL_INT(HAL_OK,
                        hal_ds18b20_take_latest_ex(s_sensor, &t, &fresh));
  TEST_ASSERT_TRUE(fresh);
  TEST_ASSERT_FLOAT_WITHIN(0.01f, 10.0f, t);

  hal_mock_ds18b20_set_crc_ok(s_sensor, false);
  hal_mock_ds18b20_set_next_temp(s_sensor, 20.0f);
  TEST_ASSERT_EQUAL_INT(HAL_OK, hal_ds18b20_request_ex(s_sensor));
  hal_mock_advance_micros(750000u);
  TEST_ASSERT_EQUAL_INT(HAL_EPROTO, hal_ds18b20_poll(s_sensor));

  TEST_ASSERT_EQUAL_INT(HAL_OK,
                        hal_ds18b20_take_latest_ex(s_sensor, &t, &fresh));
  TEST_ASSERT_FALSE(fresh);
  TEST_ASSERT_FLOAT_WITHIN(0.01f, 10.0f, t);
}

void test_take_latest_is_thread_safe_for_concurrent_readers(void) {
  hal_mock_ds18b20_set_next_temp(s_sensor, 24.75f);
  TEST_ASSERT_EQUAL_INT(HAL_OK, hal_ds18b20_request_ex(s_sensor));
  hal_mock_advance_micros(750000u);
  TEST_ASSERT_EQUAL_INT(HAL_OK, hal_ds18b20_poll(s_sensor));

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

void test_take_latest_argument_validation_and_empty_cache(void) {
  float t = 0.0f;
  bool fresh = false;

  TEST_ASSERT_EQUAL_INT(HAL_EINVAL,
                        hal_ds18b20_take_latest_ex(nullptr, &t, &fresh));
  TEST_ASSERT_EQUAL_INT(HAL_EINVAL,
                        hal_ds18b20_take_latest_ex(s_sensor, nullptr, &fresh));
  TEST_ASSERT_EQUAL_INT(HAL_ENOENT,
                        hal_ds18b20_take_latest_ex(s_sensor, &t, &fresh));

  hal_mock_ds18b20_set_next_temp(s_sensor, 5.0f);
  TEST_ASSERT_EQUAL_INT(HAL_OK, hal_ds18b20_request_ex(s_sensor));
  hal_mock_advance_micros(750000u);
  TEST_ASSERT_EQUAL_INT(HAL_OK, hal_ds18b20_poll(s_sensor));
  TEST_ASSERT_EQUAL_INT(HAL_OK,
                        hal_ds18b20_take_latest_ex(s_sensor, &t, nullptr));
  TEST_ASSERT_FLOAT_WITHIN(0.01f, 5.0f, t);
}

void test_invalid_handle_guards_for_request_poll_busy(void) {
  TEST_ASSERT_EQUAL_INT(HAL_EINVAL, hal_ds18b20_request_ex(nullptr));
  TEST_ASSERT_FALSE(hal_ds18b20_request(nullptr));
  TEST_ASSERT_EQUAL_INT(HAL_EINVAL, hal_ds18b20_poll(nullptr));
  TEST_ASSERT_FALSE(hal_ds18b20_is_busy(nullptr));
}

void test_deinit_null_is_noop(void) {
  TEST_ASSERT_EQUAL_INT(HAL_OK, hal_ds18b20_deinit(nullptr));
}

void test_instance_pool_limit_is_enforced(void) {
  hal_ds18b20_config_t cfg = default_cfg();
  hal_ds18b20_t hs[HAL_DS18B20_MAX_INSTANCES] = {};

  /* setUp() already owns one slot in s_sensor. */
  for (int i = 0; i < (HAL_DS18B20_MAX_INSTANCES - 1); ++i) {
    cfg.data_pin = (uint8_t)(20 + i);
    hs[i] = hal_ds18b20_init(&cfg);
    TEST_ASSERT_NOT_NULL(hs[i]);
  }

  cfg.data_pin = 99u;
  TEST_ASSERT_NULL(hal_ds18b20_init(&cfg));
  hal_ds18b20_t extra = nullptr;
  TEST_ASSERT_EQUAL_INT(HAL_ENOMEM, hal_ds18b20_init_ex(&cfg, &extra));
  TEST_ASSERT_NULL(extra);

  for (int i = 0; i < (HAL_DS18B20_MAX_INSTANCES - 1); ++i) {
    TEST_ASSERT_EQUAL_INT(HAL_OK, hal_ds18b20_deinit(hs[i]));
  }
}

int main(void) {
  UNITY_BEGIN();
  RUN_TEST(test_init_returns_handle);
  RUN_TEST(test_init_with_rom_returns_handle);
  RUN_TEST(test_init_rejects_null_config);
  RUN_TEST(test_request_marks_sensor_busy);
  RUN_TEST(test_poll_before_deadline_keeps_busy_and_no_sample);
  RUN_TEST(test_poll_after_deadline_publishes_fresh_sample);
  RUN_TEST(test_resolution_hint_controls_conversion_deadline_9_to_12_bit);
  RUN_TEST(test_invalid_resolution_hint_falls_back_to_12_bit_timing);
  RUN_TEST(test_request_while_busy_returns_false);
  RUN_TEST(test_missing_presence_rejects_request);
  RUN_TEST(test_crc_error_keeps_previous_sample_and_not_fresh);
  RUN_TEST(test_take_latest_is_thread_safe_for_concurrent_readers);
  RUN_TEST(test_take_latest_argument_validation_and_empty_cache);
  RUN_TEST(test_invalid_handle_guards_for_request_poll_busy);
  RUN_TEST(test_deinit_null_is_noop);
  RUN_TEST(test_instance_pool_limit_is_enforced);
  return UNITY_END();
}

#else

int main(void) {
  UNITY_BEGIN();
  return UNITY_END();
}

#endif /* HAL_ENABLE_DS18B20 */
