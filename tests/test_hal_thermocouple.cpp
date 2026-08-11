#include "hal/i2c/hal_i2c.h"
#include "hal/impl/.mock/hal_mock.h"
#include "hal/temperature/hal_thermocouple.h"
#include "hal/temperature/jh_thermocouple_provider.h"
#include "utils/unity.h"
#include <math.h>

#ifdef HAL_ENABLE_MCP9600
static hal_thermocouple_t mcp;
#endif
#ifdef HAL_ENABLE_MAX6675
static hal_thermocouple_t max;
#endif

#ifdef HAL_ENABLE_MCP9600
static hal_thermocouple_config_t mcp_cfg(void) {
  hal_thermocouple_config_t cfg = {};
  cfg.chip = HAL_THERMOCOUPLE_CHIP_MCP9600;
  cfg.bus.i2c = {4, 5, HAL_I2C_CLOCK_STANDARD_HZ, 0, 0x67};
  return cfg;
}
#endif

#ifdef HAL_ENABLE_MAX6675
static hal_thermocouple_config_t max_cfg(void) {
  hal_thermocouple_config_t cfg = {};
  cfg.chip = HAL_THERMOCOUPLE_CHIP_MAX6675;
  cfg.bus.spi = {2, 3, 4};
  return cfg;
}
#endif

void setUp(void) {
#ifdef HAL_ENABLE_MCP9600
  hal_thermocouple_config_t c = mcp_cfg();
  mcp = hal_thermocouple_init(&c);
#endif
#ifdef HAL_ENABLE_MAX6675
  hal_thermocouple_config_t x = max_cfg();
  max = hal_thermocouple_init(&x);
#endif
}

void tearDown(void) {
#ifdef HAL_ENABLE_MCP9600
  hal_thermocouple_deinit(mcp);
  mcp = nullptr;
#endif
#ifdef HAL_ENABLE_MAX6675
  hal_thermocouple_deinit(max);
  max = nullptr;
#endif
}

void test_init_returns_valid_handles(void) {
#ifdef HAL_ENABLE_MCP9600
  TEST_ASSERT_NOT_NULL(mcp);
  hal_thermocouple_config_t c = mcp_cfg();
  hal_thermocouple_t tmp = nullptr;
  TEST_ASSERT_EQUAL_INT(HAL_OK, hal_thermocouple_init_ex(&c, &tmp));
  TEST_ASSERT_NOT_NULL(tmp);
  hal_thermocouple_deinit(tmp);
#endif
#ifdef HAL_ENABLE_MAX6675
  TEST_ASSERT_NOT_NULL(max);
#endif
}

void test_init_ex_reports_invalid_arguments(void) {
  hal_thermocouple_t tmp = nullptr;
  TEST_ASSERT_EQUAL_INT(HAL_EINVAL, hal_thermocouple_init_ex(nullptr, &tmp));
#ifdef HAL_ENABLE_MCP9600
  hal_thermocouple_config_t c = mcp_cfg();
#elif defined(HAL_ENABLE_MAX6675)
  hal_thermocouple_config_t c = max_cfg();
#endif
  TEST_ASSERT_EQUAL_INT(HAL_EINVAL, hal_thermocouple_init_ex(&c, nullptr));
  c.chip = (hal_thermocouple_chip_t)99;
  TEST_ASSERT_EQUAL_INT(HAL_EUNSUPPORTED, hal_thermocouple_init_ex(&c, &tmp));
}

void test_provider_registry_matches_enabled_chips(void) {
#ifdef HAL_ENABLE_MCP9600
  const jh_thermocouple_provider_t *mcp_provider =
      jh_thermocouple_provider_get(HAL_THERMOCOUPLE_CHIP_MCP9600);
  TEST_ASSERT_NOT_NULL(mcp_provider);
  TEST_ASSERT_EQUAL_INT(HAL_THERMOCOUPLE_CHIP_MCP9600, mcp_provider->chip);
  TEST_ASSERT_NOT_NULL(mcp_provider->ops->read_ambient);
  TEST_ASSERT_NOT_NULL(mcp_provider->ops->set_alert);
  TEST_ASSERT_TRUE(mcp_provider->context_size <=
                   JH_THERMOCOUPLE_PROVIDER_CONTEXT_SIZE);
#endif
#ifdef HAL_ENABLE_MAX6675
  const jh_thermocouple_provider_t *max_provider =
      jh_thermocouple_provider_get(HAL_THERMOCOUPLE_CHIP_MAX6675);
  TEST_ASSERT_NOT_NULL(max_provider);
  TEST_ASSERT_EQUAL_INT(HAL_THERMOCOUPLE_CHIP_MAX6675, max_provider->chip);
#ifdef HAL_ENABLE_MCP9600
  TEST_ASSERT_NULL(max_provider->ops->read_ambient);
  TEST_ASSERT_NULL(max_provider->ops->set_alert);
#endif
  TEST_ASSERT_TRUE(max_provider->context_size <=
                   JH_THERMOCOUPLE_PROVIDER_CONTEXT_SIZE);
#endif
  TEST_ASSERT_NULL(
      jh_thermocouple_provider_get(static_cast<hal_thermocouple_chip_t>(99)));
}

void test_pool_exhaustion_and_slot_reuse(void) {
  hal_thermocouple_t handles[HAL_THERMOCOUPLE_MAX_INSTANCES] = {};
#if defined(HAL_ENABLE_MCP9600) && defined(HAL_ENABLE_MAX6675)
  const int preallocated = 2;
#else
  const int preallocated = 1;
#endif
#ifdef HAL_ENABLE_MCP9600
  hal_thermocouple_config_t config = mcp_cfg();
#else
  hal_thermocouple_config_t config = max_cfg();
#endif

  int created = 0;
  for (; created < HAL_THERMOCOUPLE_MAX_INSTANCES - preallocated; ++created) {
    TEST_ASSERT_EQUAL_INT(HAL_OK,
                          hal_thermocouple_init_ex(&config, &handles[created]));
  }
  hal_thermocouple_t exhausted = nullptr;
  TEST_ASSERT_EQUAL_INT(HAL_ENOMEM,
                        hal_thermocouple_init_ex(&config, &exhausted));
  TEST_ASSERT_NULL(exhausted);

  TEST_ASSERT_TRUE(created > 0);
  hal_thermocouple_deinit(handles[0]);
  handles[0] = nullptr;
  TEST_ASSERT_EQUAL_INT(HAL_OK, hal_thermocouple_init_ex(&config, &handles[0]));
  for (int i = 0; i < created; ++i) {
    hal_thermocouple_deinit(handles[i]);
  }
}

#ifdef HAL_ENABLE_MCP9600
void test_mcp_default_temp(void) {
  TEST_ASSERT_FLOAT_WITHIN(0.01f, 25.0f, hal_thermocouple_read(mcp));
}

void test_mcp_supports_consecutive_reads(void) {
  float first = NAN;
  float second = NAN;
  TEST_ASSERT_EQUAL_INT(HAL_OK, hal_thermocouple_read_ex(mcp, &first));
  TEST_ASSERT_EQUAL_INT(HAL_OK, hal_thermocouple_read_ex(mcp, &second));
  TEST_ASSERT_FLOAT_WITHIN(0.01f, 25.0f, first);
  TEST_ASSERT_FLOAT_WITHIN(0.01f, 25.0f, second);
}
#endif

#ifdef HAL_ENABLE_MAX6675
void test_max_default_temp(void) {
  TEST_ASSERT_FLOAT_WITHIN(0.01f, 25.0f, hal_thermocouple_read(max));
}
#endif

#ifdef HAL_ENABLE_MCP9600
void test_mcp_inject_temp(void) {
  float temp = NAN;
  hal_mock_thermocouple_set_temp(mcp, 150.5f);
  TEST_ASSERT_EQUAL_INT(HAL_OK, hal_thermocouple_read_ex(mcp, &temp));
  TEST_ASSERT_FLOAT_WITHIN(0.01f, 150.5f, temp);
  TEST_ASSERT_FLOAT_WITHIN(0.01f, 150.5f, hal_thermocouple_read(mcp));
}

void test_mcp_inject_ambient(void) {
  hal_mock_thermocouple_set_ambient(mcp, 30.0f);
  TEST_ASSERT_FLOAT_WITHIN(0.01f, 30.0f, hal_thermocouple_read_ambient(mcp));
}
#endif

#if defined(HAL_ENABLE_MAX6675) && defined(HAL_ENABLE_MCP9600)
void test_max_ambient_unsupported_returns_nan(void) {
  TEST_ASSERT_TRUE(isnan(hal_thermocouple_read_ambient(max)));
}
#endif

#ifdef HAL_ENABLE_MCP9600
void test_mcp_set_get_type(void) {
  hal_thermocouple_type_t type = HAL_THERMOCOUPLE_TYPE_K;
  TEST_ASSERT_EQUAL_INT(
      HAL_OK, hal_thermocouple_set_type(mcp, HAL_THERMOCOUPLE_TYPE_J));
  TEST_ASSERT_EQUAL_INT(HAL_OK, hal_thermocouple_get_type_ex(mcp, &type));
  TEST_ASSERT_EQUAL_INT(HAL_THERMOCOUPLE_TYPE_J, type);
  TEST_ASSERT_EQUAL_INT(HAL_THERMOCOUPLE_TYPE_J,
                        hal_thermocouple_get_type(mcp));
}
#endif

#ifdef HAL_ENABLE_MAX6675
void test_max_type_always_k(void) {
  TEST_ASSERT_EQUAL_INT(HAL_THERMOCOUPLE_TYPE_K,
                        hal_thermocouple_get_type(max));
}
#endif

#ifdef HAL_ENABLE_MCP9600
void test_mcp_set_get_filter(void) {
  uint8_t filter = 0;
  TEST_ASSERT_EQUAL_INT(HAL_OK, hal_thermocouple_set_filter(mcp, 4));
  TEST_ASSERT_EQUAL_INT(HAL_OK, hal_thermocouple_get_filter_ex(mcp, &filter));
  TEST_ASSERT_EQUAL_UINT8(4, filter);
  TEST_ASSERT_EQUAL_UINT8(4, hal_thermocouple_get_filter(mcp));
}

void test_mcp_set_get_adc_resolution(void) {
  hal_thermocouple_adc_res_t res = HAL_THERMOCOUPLE_ADC_RES_18;
  TEST_ASSERT_EQUAL_INT(HAL_OK, hal_thermocouple_set_adc_resolution(
                                    mcp, HAL_THERMOCOUPLE_ADC_RES_14));
  TEST_ASSERT_EQUAL_INT(HAL_OK,
                        hal_thermocouple_get_adc_resolution_ex(mcp, &res));
  TEST_ASSERT_EQUAL_INT(HAL_THERMOCOUPLE_ADC_RES_14, res);
  TEST_ASSERT_EQUAL_INT(HAL_THERMOCOUPLE_ADC_RES_14,
                        hal_thermocouple_get_adc_resolution(mcp));
}

void test_mcp_enable_disable_cycle(void) {
  bool enabled = false;
  TEST_ASSERT_TRUE(hal_thermocouple_is_enabled(mcp));
  TEST_ASSERT_EQUAL_INT(HAL_OK, hal_thermocouple_is_enabled_ex(mcp, &enabled));
  TEST_ASSERT_TRUE(enabled);

  TEST_ASSERT_EQUAL_INT(HAL_OK, hal_thermocouple_enable(mcp, false));
  TEST_ASSERT_FALSE(hal_thermocouple_is_enabled(mcp));

  TEST_ASSERT_EQUAL_INT(HAL_OK, hal_thermocouple_enable(mcp, true));
  TEST_ASSERT_TRUE(hal_thermocouple_is_enabled(mcp));
}

void test_mcp_set_alert_and_read_status(void) {
  hal_thermocouple_alert_cfg_t cfg = {};
  cfg.temperature = 123.5f;
  cfg.rising = true;
  cfg.alert_cold_junction = false;
  cfg.active_high = true;
  cfg.interrupt_mode = false;

  float alert = NAN;
  uint8_t status = 0;
  TEST_ASSERT_EQUAL_INT(HAL_OK, hal_thermocouple_set_alert(mcp, 2, true, &cfg));
  TEST_ASSERT_EQUAL_INT(HAL_OK,
                        hal_thermocouple_get_alert_temp_ex(mcp, 2, &alert));
  TEST_ASSERT_FLOAT_WITHIN(0.01f, 123.5f, alert);
  TEST_ASSERT_FLOAT_WITHIN(0.01f, 123.5f,
                           hal_thermocouple_get_alert_temp(mcp, 2));

  hal_mock_thermocouple_set_status(mcp, 0x5A);
  TEST_ASSERT_EQUAL_INT(HAL_OK, hal_thermocouple_get_status_ex(mcp, &status));
  TEST_ASSERT_EQUAL_UINT8(0x5A, status);
  TEST_ASSERT_EQUAL_UINT8(0x5A, hal_thermocouple_get_status(mcp));
}

void test_mcp_set_ambient_resolution_does_not_break_reads(void) {
  hal_mock_thermocouple_set_ambient(mcp, 26.25f);
  TEST_ASSERT_EQUAL_INT(HAL_OK, hal_thermocouple_set_ambient_resolution(
                                    mcp, HAL_THERMOCOUPLE_AMBIENT_RES_0_125));
  TEST_ASSERT_FLOAT_WITHIN(0.01f, 26.25f, hal_thermocouple_read_ambient(mcp));
}
#endif

#ifdef HAL_ENABLE_MAX6675
void test_max_is_always_enabled(void) {
  TEST_ASSERT_TRUE(hal_thermocouple_is_enabled(max));
}
#endif

void test_null_handle_read_returns_nan(void) {
  float temp = 0.0f;
  TEST_ASSERT_EQUAL_INT(HAL_EINVAL, hal_thermocouple_read_ex(nullptr, &temp));
#ifdef HAL_ENABLE_MCP9600
  TEST_ASSERT_EQUAL_INT(HAL_EINVAL, hal_thermocouple_read_ex(mcp, nullptr));
#elif defined(HAL_ENABLE_MAX6675)
  TEST_ASSERT_EQUAL_INT(HAL_EINVAL, hal_thermocouple_read_ex(max, nullptr));
#endif
  TEST_ASSERT_TRUE(isnan(hal_thermocouple_read(nullptr)));
}

#if defined(HAL_ENABLE_MAX6675) && defined(HAL_ENABLE_MCP9600)
void test_max_reports_unsupported_for_mcp_only_status_calls(void) {
  float ambient = 0.0f;
  uint8_t filter = 0;
  TEST_ASSERT_EQUAL_INT(HAL_EUNSUPPORTED,
                        hal_thermocouple_read_ambient_ex(max, &ambient));
  TEST_ASSERT_TRUE(isnan(ambient));
  TEST_ASSERT_EQUAL_INT(HAL_EUNSUPPORTED, hal_thermocouple_set_type(
                                              max, HAL_THERMOCOUPLE_TYPE_J));
  TEST_ASSERT_EQUAL_INT(HAL_EUNSUPPORTED,
                        hal_thermocouple_get_filter_ex(max, &filter));
}
#endif

int main(void) {
  UNITY_BEGIN();
  RUN_TEST(test_init_returns_valid_handles);
  RUN_TEST(test_init_ex_reports_invalid_arguments);
  RUN_TEST(test_provider_registry_matches_enabled_chips);
  RUN_TEST(test_pool_exhaustion_and_slot_reuse);
#ifdef HAL_ENABLE_MCP9600
  RUN_TEST(test_mcp_default_temp);
  RUN_TEST(test_mcp_supports_consecutive_reads);
#endif
#ifdef HAL_ENABLE_MAX6675
  RUN_TEST(test_max_default_temp);
#endif
#ifdef HAL_ENABLE_MCP9600
  RUN_TEST(test_mcp_inject_temp);
  RUN_TEST(test_mcp_inject_ambient);
#endif
#if defined(HAL_ENABLE_MAX6675) && defined(HAL_ENABLE_MCP9600)
  RUN_TEST(test_max_ambient_unsupported_returns_nan);
#endif
#ifdef HAL_ENABLE_MCP9600
  RUN_TEST(test_mcp_set_get_type);
#endif
#ifdef HAL_ENABLE_MAX6675
  RUN_TEST(test_max_type_always_k);
#endif
#ifdef HAL_ENABLE_MCP9600
  RUN_TEST(test_mcp_set_get_filter);
  RUN_TEST(test_mcp_set_get_adc_resolution);
  RUN_TEST(test_mcp_enable_disable_cycle);
  RUN_TEST(test_mcp_set_alert_and_read_status);
  RUN_TEST(test_mcp_set_ambient_resolution_does_not_break_reads);
#endif
#ifdef HAL_ENABLE_MAX6675
  RUN_TEST(test_max_is_always_enabled);
#endif
  RUN_TEST(test_null_handle_read_returns_nan);
#if defined(HAL_ENABLE_MAX6675) && defined(HAL_ENABLE_MCP9600)
  RUN_TEST(test_max_reports_unsupported_for_mcp_only_status_calls);
#endif
  return UNITY_END();
}
