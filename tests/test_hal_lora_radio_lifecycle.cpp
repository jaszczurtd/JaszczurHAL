#include "hal/radio/hal_lora_radio.h"
#include "hal/radio/jh_lora_radio_internal.h"
#include "utils/unity.h"

#include <string.h>

static hal_status_t s_initialize_status;
static hal_status_t s_deinitialize_status;
static size_t s_initialize_calls;
static size_t s_deinitialize_calls;
static hal_lora_radio_config_t s_seen_config;
static bool s_initialize_saw_mutex;
static hal_lora_radio_state_t s_initialize_state;

static hal_status_t fake_initialize(jh_lora_radio_context_t *context) {
  ++s_initialize_calls;
  s_seen_config = context->config;
  s_initialize_saw_mutex = context->mutex != NULL;
  s_initialize_state = context->state;
  return s_initialize_status;
}

static hal_status_t fake_deinitialize(jh_lora_radio_context_t *) {
  ++s_deinitialize_calls;
  return s_deinitialize_status;
}

static hal_status_t fake_configure(jh_lora_radio_context_t *) { return HAL_OK; }

static hal_status_t
fake_get_capabilities(jh_lora_radio_context_t *,
                      hal_lora_radio_capabilities_t *out_capabilities) {
  if (out_capabilities == NULL) {
    return HAL_EINVAL;
  }
  memset(out_capabilities, 0, sizeof(*out_capabilities));
  out_capabilities->model = HAL_LORA_RADIO_SX1262;
  return HAL_OK;
}

static hal_status_t fake_get_instant_rssi(jh_lora_radio_context_t *,
                                          int16_t *out_rssi_dbm) {
  if (out_rssi_dbm == NULL) {
    return HAL_EINVAL;
  }
  *out_rssi_dbm = -100;
  return HAL_OK;
}

static hal_status_t fake_transmit_start(jh_lora_radio_context_t *, uint32_t) {
  return HAL_OK;
}

static hal_status_t fake_receive_start(jh_lora_radio_context_t *, uint32_t,
                                       bool) {
  return HAL_OK;
}

static hal_status_t
fake_channel_activity_detect_start(jh_lora_radio_context_t *, uint32_t) {
  return HAL_OK;
}

static hal_status_t fake_process(jh_lora_radio_context_t *,
                                 jh_lora_provider_events_t *) {
  return HAL_EAGAIN;
}

static hal_status_t fake_cancel(jh_lora_radio_context_t *) { return HAL_OK; }

static hal_status_t fake_sleep(jh_lora_radio_context_t *) { return HAL_OK; }

static hal_status_t fake_standby(jh_lora_radio_context_t *) { return HAL_OK; }

static hal_status_t fake_calibrate(jh_lora_radio_context_t *) { return HAL_OK; }

static const jh_lora_radio_provider_ops_t s_fake_provider = {
    fake_initialize,
    fake_deinitialize,
    fake_configure,
    fake_get_capabilities,
    fake_get_instant_rssi,
    fake_transmit_start,
    fake_receive_start,
    fake_channel_activity_detect_start,
    fake_process,
    fake_cancel,
    fake_sleep,
    fake_standby,
    fake_calibrate,
};

static hal_lora_radio_config_t valid_config(void) {
  hal_lora_radio_config_t config = {};
  config.model = HAL_LORA_RADIO_SX1262;
  config.spi_bus = 1u;
  config.spi_miso_pin = 1u;
  config.spi_mosi_pin = 2u;
  config.spi_sck_pin = 3u;
  config.cs_pin = 4u;
  config.spi_clock_hz = UINT32_C(8000000);

  hal_lora_sx126x_hardware_config_t *hardware = &config.hardware.sx126x;
  hardware->reset_pin = 5u;
  hardware->dio1_pin = 6u;
  hardware->busy_pin = 7u;
  hardware->rf_switch_mode = HAL_LORA_RF_SWITCH_DIO2_SINGLE_GPIO;
  hardware->rf_switch_pin_a = 8u;
  hardware->rf_switch_pin_b = HAL_LORA_PIN_NONE;
  hardware->rf_switch_idle_level_a = true;
  hardware->rf_switch_rx_level_a = true;
  hardware->rf_switch_tx_level_a = false;
  hardware->regulator_mode = HAL_LORA_REGULATOR_DCDC;
  hardware->tcxo_control = HAL_LORA_TCXO_CONTROL_DIO3;
  hardware->tcxo_voltage = HAL_LORA_TCXO_1V7;
  hardware->tcxo_startup_us = UINT32_C(5000);
  hardware->min_frequency_hz = UINT32_C(410000000);
  hardware->max_frequency_hz = UINT32_C(450000000);
  hardware->max_spi_clock_hz = UINT32_C(17999999);
  hardware->min_tx_power_dbm = -9;
  hardware->max_tx_power_dbm = 22;
  return config;
}

static void install_fake_provider(void) {
  TEST_ASSERT_EQUAL_INT(HAL_OK,
                        jh_lora_radio_set_provider_for_test(&s_fake_provider));
}

void setUp(void) {
  s_initialize_status = HAL_OK;
  s_deinitialize_status = HAL_OK;
  s_initialize_calls = 0u;
  s_deinitialize_calls = 0u;
  memset(&s_seen_config, 0, sizeof(s_seen_config));
  s_initialize_saw_mutex = false;
  s_initialize_state = HAL_LORA_RADIO_STATE_STANDBY;
  TEST_ASSERT_EQUAL_INT(HAL_OK, jh_lora_radio_set_provider_for_test(NULL));
}

void tearDown(void) {
  TEST_ASSERT_EQUAL_INT(HAL_OK, jh_lora_radio_set_provider_for_test(NULL));
}

void test_create_validates_hardware_before_provider_dispatch(void) {
  install_fake_provider();
  hal_lora_radio_config_t config = valid_config();
  hal_lora_radio_t radio =
      reinterpret_cast<hal_lora_radio_t>(static_cast<uintptr_t>(1u));

  TEST_ASSERT_EQUAL_INT(HAL_EINVAL, hal_lora_radio_create(NULL, &radio));
  TEST_ASSERT_NULL(radio);
  TEST_ASSERT_EQUAL_INT(HAL_EINVAL, hal_lora_radio_create(&config, NULL));

  config.spi_bus = 2u;
  TEST_ASSERT_EQUAL_INT(HAL_EINVAL, hal_lora_radio_create(&config, &radio));
  config = valid_config();
  config.cs_pin = config.spi_sck_pin;
  TEST_ASSERT_EQUAL_INT(HAL_EINVAL, hal_lora_radio_create(&config, &radio));
  config = valid_config();
  config.hardware.sx126x.rf_switch_pin_a = HAL_LORA_PIN_NONE;
  TEST_ASSERT_EQUAL_INT(HAL_EINVAL, hal_lora_radio_create(&config, &radio));
  config = valid_config();
  config.spi_clock_hz = config.hardware.sx126x.max_spi_clock_hz + 1u;
  TEST_ASSERT_EQUAL_INT(HAL_EINVAL, hal_lora_radio_create(&config, &radio));
  config = valid_config();
  config.hardware.sx126x.min_frequency_hz =
      config.hardware.sx126x.max_frequency_hz + 1u;
  TEST_ASSERT_EQUAL_INT(HAL_EINVAL, hal_lora_radio_create(&config, &radio));
  config = valid_config();
  config.hardware.sx126x.tcxo_startup_us = 0u;
  TEST_ASSERT_EQUAL_INT(HAL_EINVAL, hal_lora_radio_create(&config, &radio));

  TEST_ASSERT_EQUAL_UINT32(0u, s_initialize_calls);
}

void test_default_mock_provider_constructs_and_releases_radio(void) {
  const hal_lora_radio_config_t config = valid_config();
  hal_lora_radio_t radio = NULL;
  TEST_ASSERT_EQUAL_INT(HAL_OK, hal_lora_radio_create(&config, &radio));
  TEST_ASSERT_NOT_NULL(radio);
  TEST_ASSERT_EQUAL_INT(HAL_OK, hal_lora_radio_destroy(radio));
}

void test_create_copies_config_and_constructs_instance_mutex(void) {
  install_fake_provider();
  const hal_lora_radio_config_t config = valid_config();
  hal_lora_radio_t radio = NULL;
  TEST_ASSERT_EQUAL_INT(HAL_OK, hal_lora_radio_create(&config, &radio));
  TEST_ASSERT_NOT_NULL(radio);
  TEST_ASSERT_EQUAL_UINT32(1u, s_initialize_calls);
  TEST_ASSERT_TRUE(s_initialize_saw_mutex);
  TEST_ASSERT_EQUAL_INT(HAL_LORA_RADIO_STATE_ERROR, s_initialize_state);
  TEST_ASSERT_EQUAL_UINT8(config.spi_bus, s_seen_config.spi_bus);
  TEST_ASSERT_EQUAL_UINT8(config.hardware.sx126x.rf_switch_pin_a,
                          s_seen_config.hardware.sx126x.rf_switch_pin_a);
  TEST_ASSERT_EQUAL_INT(HAL_OK, hal_lora_radio_destroy(radio));
  TEST_ASSERT_EQUAL_UINT32(1u, s_deinitialize_calls);
  TEST_ASSERT_EQUAL_INT(HAL_EUNINIT, hal_lora_radio_destroy(radio));
  TEST_ASSERT_EQUAL_INT(HAL_EUNINIT, hal_lora_radio_destroy(NULL));
}

void test_static_pool_is_bounded_and_keeps_distinct_handles(void) {
  install_fake_provider();
  const hal_lora_radio_config_t config = valid_config();
  hal_lora_radio_t handles[HAL_LORA_RADIO_MAX_INSTANCES] = {};
  for (size_t index = 0u; index < HAL_LORA_RADIO_MAX_INSTANCES; ++index) {
    TEST_ASSERT_EQUAL_INT(HAL_OK,
                          hal_lora_radio_create(&config, &handles[index]));
    TEST_ASSERT_NOT_NULL(handles[index]);
    for (size_t previous = 0u; previous < index; ++previous) {
      TEST_ASSERT_NOT_EQUAL(handles[previous], handles[index]);
    }
  }
  hal_lora_radio_t extra =
      reinterpret_cast<hal_lora_radio_t>(static_cast<uintptr_t>(1u));
  TEST_ASSERT_EQUAL_INT(HAL_ENOMEM, hal_lora_radio_create(&config, &extra));
  TEST_ASSERT_NULL(extra);
  TEST_ASSERT_EQUAL_INT(HAL_EBUSY, jh_lora_radio_set_provider_for_test(NULL));

  for (size_t index = 0u; index < HAL_LORA_RADIO_MAX_INSTANCES; ++index) {
    TEST_ASSERT_EQUAL_INT(HAL_OK, hal_lora_radio_destroy(handles[index]));
  }
}

void test_generation_ticket_rejects_stale_handle_after_reuse(void) {
  install_fake_provider();
  const hal_lora_radio_config_t config = valid_config();
  hal_lora_radio_t stale = NULL;
  TEST_ASSERT_EQUAL_INT(HAL_OK, hal_lora_radio_create(&config, &stale));
  TEST_ASSERT_EQUAL_INT(HAL_OK, hal_lora_radio_destroy(stale));

  hal_lora_radio_t current = NULL;
  TEST_ASSERT_EQUAL_INT(HAL_OK, hal_lora_radio_create(&config, &current));
  TEST_ASSERT_NOT_EQUAL(stale, current);
  TEST_ASSERT_EQUAL_INT(HAL_EUNINIT, hal_lora_radio_destroy(stale));
  TEST_ASSERT_EQUAL_INT(HAL_OK, hal_lora_radio_destroy(current));
}

void test_provider_failures_release_and_invalidate_slot(void) {
  install_fake_provider();
  const hal_lora_radio_config_t config = valid_config();
  hal_lora_radio_t radio = NULL;

  s_initialize_status = HAL_EHW;
  TEST_ASSERT_EQUAL_INT(HAL_EHW, hal_lora_radio_create(&config, &radio));
  TEST_ASSERT_NULL(radio);

  s_initialize_status = HAL_OK;
  TEST_ASSERT_EQUAL_INT(HAL_OK, hal_lora_radio_create(&config, &radio));
  s_deinitialize_status = HAL_EIO;
  TEST_ASSERT_EQUAL_INT(HAL_EIO, hal_lora_radio_destroy(radio));
  TEST_ASSERT_EQUAL_INT(HAL_EUNINIT, hal_lora_radio_destroy(radio));

  s_deinitialize_status = HAL_OK;
  TEST_ASSERT_EQUAL_INT(HAL_OK, hal_lora_radio_create(&config, &radio));
  TEST_ASSERT_EQUAL_INT(HAL_OK, hal_lora_radio_destroy(radio));
}

void test_test_provider_rejects_incomplete_operation_table(void) {
  const jh_lora_radio_provider_ops_t incomplete = {fake_initialize, NULL};
  TEST_ASSERT_EQUAL_INT(HAL_EINVAL,
                        jh_lora_radio_set_provider_for_test(&incomplete));
}

int main(void) {
  UNITY_BEGIN();
  RUN_TEST(test_create_validates_hardware_before_provider_dispatch);
  RUN_TEST(test_default_mock_provider_constructs_and_releases_radio);
  RUN_TEST(test_create_copies_config_and_constructs_instance_mutex);
  RUN_TEST(test_static_pool_is_bounded_and_keeps_distinct_handles);
  RUN_TEST(test_generation_ticket_rejects_stale_handle_after_reuse);
  RUN_TEST(test_provider_failures_release_and_invalidate_slot);
  RUN_TEST(test_test_provider_rejects_incomplete_operation_table);
  return UNITY_END();
}
