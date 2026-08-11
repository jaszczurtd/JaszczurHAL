#include "hal/radio/hal_lora_radio.h"
#include "hal/radio/jh_lora_radio_internal.h"
#include "hal/system/hal_system.h"
#include "utils/unity.h"

#include <FreeRTOS.h>
#include <semphr.h>
#include <task.h>

#include <stdint.h>

namespace {

SemaphoreHandle_t s_done;
hal_lora_radio_t s_radio;
volatile bool s_start;
hal_status_t s_results[2];
volatile int s_failures;

void fail(void) { __atomic_fetch_add(&s_failures, 1, __ATOMIC_RELAXED); }

hal_status_t provider_initialize(jh_lora_radio_context_t *) { return HAL_OK; }
hal_status_t provider_deinitialize(jh_lora_radio_context_t *) { return HAL_OK; }
hal_status_t provider_configure(jh_lora_radio_context_t *) { return HAL_OK; }

hal_status_t
provider_get_capabilities(jh_lora_radio_context_t *context,
                          hal_lora_radio_capabilities_t *out_capabilities) {
  return jh_lora_radio_describe_capabilities(
      context, JH_LORA_PROVIDER_CAP_SX1262, out_capabilities);
}

hal_status_t provider_get_instant_rssi(jh_lora_radio_context_t *,
                                       int16_t *out_rssi_dbm) {
  if (out_rssi_dbm == nullptr) {
    return HAL_EINVAL;
  }
  *out_rssi_dbm = -100;
  return HAL_OK;
}

hal_status_t provider_transmit_start(jh_lora_radio_context_t *, uint32_t) {
  hal_delay_ms(2u);
  return HAL_OK;
}

hal_status_t provider_receive_start(jh_lora_radio_context_t *, uint32_t, bool) {
  return HAL_OK;
}

hal_status_t provider_channel_activity_detect_start(jh_lora_radio_context_t *,
                                                    uint32_t) {
  return HAL_OK;
}

hal_status_t provider_process(jh_lora_radio_context_t *context,
                              jh_lora_provider_events_t *out_events) {
  if (out_events == nullptr) {
    return HAL_EINVAL;
  }
  *out_events =
      context->state == HAL_LORA_RADIO_STATE_TX
          ? JH_LORA_PROVIDER_EVENT_TX_DONE | JH_LORA_PROVIDER_EVENT_IRQ
          : JH_LORA_PROVIDER_EVENT_NONE;
  return *out_events == JH_LORA_PROVIDER_EVENT_NONE ? HAL_EAGAIN : HAL_OK;
}

hal_status_t provider_cancel(jh_lora_radio_context_t *) { return HAL_OK; }
hal_status_t provider_sleep(jh_lora_radio_context_t *) { return HAL_OK; }
hal_status_t provider_standby(jh_lora_radio_context_t *) { return HAL_OK; }
hal_status_t provider_calibrate(jh_lora_radio_context_t *) { return HAL_OK; }

const jh_lora_radio_provider_ops_t s_provider = {
    provider_initialize,
    provider_deinitialize,
    provider_configure,
    provider_get_capabilities,
    provider_get_instant_rssi,
    provider_transmit_start,
    provider_receive_start,
    provider_channel_activity_detect_start,
    provider_process,
    provider_cancel,
    provider_sleep,
    provider_standby,
    provider_calibrate,
};

hal_lora_radio_config_t radio_config(void) {
  hal_lora_radio_config_t config = {};
  config.model = HAL_LORA_RADIO_SX1262;
  config.spi_bus = 0u;
  config.spi_miso_pin = 1u;
  config.spi_mosi_pin = 2u;
  config.spi_sck_pin = 3u;
  config.cs_pin = 4u;
  config.spi_clock_hz = HAL_LORA_SPI_CLOCK_DEFAULT_HZ;
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
  hardware->tcxo_control = HAL_LORA_TCXO_CONTROL_NONE;
  hardware->min_frequency_hz = UINT32_C(410000000);
  hardware->max_frequency_hz = UINT32_C(450000000);
  hardware->max_spi_clock_hz = UINT32_C(18000000);
  hardware->min_tx_power_dbm = -9;
  hardware->max_tx_power_dbm = 22;
  return config;
}

void transmit_worker(void *argument) {
  const size_t index = (size_t)(uintptr_t)argument;
  static const uint8_t payload[] = {0xA5u};
  while (!__atomic_load_n(&s_start, __ATOMIC_ACQUIRE)) {
    taskYIELD();
  }
  s_results[index] =
      hal_lora_radio_transmit_start(s_radio, payload, sizeof(payload));
  (void)xSemaphoreGive(s_done);
  vTaskDelete(nullptr);
}

void supervisor(void *) {
  if (jh_lora_radio_set_provider_for_test(&s_provider) != HAL_OK) {
    fail();
  }
  const hal_lora_radio_config_t config = radio_config();
  hal_lora_modem_config_t modem = hal_lora_default_fast_eu868();
  modem.frequency_hz = UINT32_C(434000000);
  modem.tx_power_dbm = 10;
  if (hal_lora_radio_create(&config, &s_radio) != HAL_OK ||
      hal_lora_radio_configure(s_radio, &modem) != HAL_OK) {
    fail();
  }

  s_done = xSemaphoreCreateCounting(2u, 0u);
  if (s_done == nullptr ||
      xTaskCreate(transmit_worker, "lora-a", 768u, reinterpret_cast<void *>(0u),
                  3u, nullptr) != pdPASS ||
      xTaskCreate(transmit_worker, "lora-b", 768u, reinterpret_cast<void *>(1u),
                  3u, nullptr) != pdPASS) {
    fail();
  }
  __atomic_store_n(&s_start, true, __ATOMIC_RELEASE);
  for (size_t index = 0u; index < 2u; ++index) {
    if (xSemaphoreTake(s_done, pdMS_TO_TICKS(1000u)) != pdTRUE) {
      fail();
    }
  }

  const bool serialized =
      (s_results[0] == HAL_OK && s_results[1] == HAL_EBUSY) ||
      (s_results[1] == HAL_OK && s_results[0] == HAL_EBUSY);
  if (!serialized || hal_lora_radio_process(s_radio) != HAL_OK) {
    fail();
  }
  hal_lora_operation_status_t operation = {};
  if (hal_lora_radio_get_tx_status(s_radio, &operation) != HAL_OK ||
      operation.state != HAL_LORA_OPERATION_SUCCEEDED ||
      operation.result != HAL_OK || hal_lora_radio_destroy(s_radio) != HAL_OK ||
      jh_lora_radio_set_provider_for_test(nullptr) != HAL_OK) {
    fail();
  }
  s_radio = nullptr;
  vSemaphoreDelete(s_done);
  s_done = nullptr;
  vTaskEndScheduler();
  vTaskDelete(nullptr);
}

} // namespace

void setUp(void) {
  s_done = nullptr;
  s_radio = nullptr;
  s_start = false;
  s_results[0] = HAL_NONE;
  s_results[1] = HAL_NONE;
  s_failures = 0;
}

void tearDown(void) {}

void test_lora_facade_serializes_tasks_with_freertos_mutexes(void) {
  TEST_ASSERT_EQUAL_INT(pdPASS, xTaskCreate(supervisor, "lora-supervisor",
                                            1536u, nullptr, 4u, nullptr));
  vTaskStartScheduler();
  TEST_ASSERT_EQUAL_INT(0, s_failures);
}

int main(void) {
  UNITY_BEGIN();
  RUN_TEST(test_lora_facade_serializes_tasks_with_freertos_mutexes);
  return UNITY_END();
}
