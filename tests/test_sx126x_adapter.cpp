#include "hal/hal_gpio.h"
#include "hal/hal_spi.h"
#include "hal/hal_system.h"
#include "hal/impl/.mock/hal_mock.h"
#include "hal/impl/shared/drivers/sx126x/jh_sx126x_adapter.h"
#include "utils/unity.h"

#include <string.h>

static jh_lora_radio_context_t adapter_context(void) {
  jh_lora_radio_context_t context = {};
  context.config.model = HAL_LORA_RADIO_SX1262;
  context.config.spi_bus = 1u;
  context.config.spi_miso_pin = 24u;
  context.config.spi_mosi_pin = 15u;
  context.config.spi_sck_pin = 14u;
  context.config.cs_pin = 3u;
  context.config.spi_clock_hz = UINT32_C(4000000);
  context.config.hardware.sx126x.reset_pin = 15u;
  context.config.hardware.sx126x.busy_pin = 2u;
  context.config.hardware.sx126x.dio1_pin = 20u;
  context.config.hardware.sx126x.rf_switch_pin_a = 21u;
  context.config.hardware.sx126x.rf_switch_pin_b = 22u;
  context.config.hardware.sx126x.rf_switch_idle_level_a = false;
  context.config.hardware.sx126x.rf_switch_idle_level_b = false;
  context.config.hardware.sx126x.rf_switch_rx_level_a = true;
  context.config.hardware.sx126x.rf_switch_rx_level_b = false;
  context.config.hardware.sx126x.rf_switch_tx_level_a = false;
  context.config.hardware.sx126x.rf_switch_tx_level_b = true;
  context.provider_last_status = HAL_NONE;
  return context;
}

static bool contains_bytes(const uint8_t *bytes, size_t length,
                           const uint8_t *expected, size_t expected_length) {
  if (expected_length == 0u || expected_length > length) {
    return false;
  }
  for (size_t offset = 0u; offset <= length - expected_length; ++offset) {
    if (memcmp(&bytes[offset], expected, expected_length) == 0) {
      return true;
    }
  }
  return false;
}

static void configure_context_modem(jh_lora_radio_context_t *context) {
  context->config.hardware.sx126x.min_frequency_hz = UINT32_C(850000000);
  context->config.hardware.sx126x.max_frequency_hz = UINT32_C(930000000);
  context->config.hardware.sx126x.min_tx_power_dbm = -9;
  context->config.hardware.sx126x.max_tx_power_dbm = 22;
  context->modem = hal_lora_default_eu868();
  context->modem.tx_power_dbm = 10;
}

void setUp(void) {
  hal_mock_spi_reset();
  hal_mock_set_millis(0u);
  hal_mock_gpio_inject_level(2u, false);
  TEST_ASSERT_EQUAL_INT(HAL_OK, hal_spi_init(1u, 12u, 11u, 10u));
  hal_gpio_set_mode(3u, HAL_GPIO_OUTPUT_HIGH);
}

void tearDown(void) {}

void test_adapter_write_uses_one_mode0_msb_transaction(void) {
  jh_lora_radio_context_t context = adapter_context();
  const uint8_t command[] = {0x8Au, 0x01u};
  const uint8_t data[] = {0xA5u, 0x5Au};
  TEST_ASSERT_EQUAL_INT(
      SX126X_HAL_STATUS_OK,
      sx126x_hal_write(&context, command, sizeof(command), data, sizeof(data)));
  uint8_t tx[8] = {};
  TEST_ASSERT_EQUAL_UINT(4u, hal_mock_spi_get_tx(1u, tx, sizeof(tx)));
  const uint8_t expected[] = {0x8Au, 0x01u, 0xA5u, 0x5Au};
  TEST_ASSERT_EQUAL_MEMORY(expected, tx, sizeof(expected));
  TEST_ASSERT_EQUAL_UINT32(UINT32_C(4000000), hal_mock_spi_get_clock_hz(1u));
  TEST_ASSERT_EQUAL_UINT8(HAL_SPI_MSBFIRST, hal_mock_spi_get_bit_order(1u));
  TEST_ASSERT_EQUAL_UINT8(HAL_SPI_MODE0, hal_mock_spi_get_data_mode(1u));
  TEST_ASSERT_EQUAL_INT(0, hal_mock_spi_get_lock_depth(1u));
  TEST_ASSERT_FALSE(hal_mock_spi_transaction_active(1u));
  TEST_ASSERT_TRUE(hal_mock_gpio_get_state(3u));
  TEST_ASSERT_EQUAL_INT(HAL_OK, context.provider_last_status);
}

void test_adapter_read_clocks_response_and_releases_bus(void) {
  jh_lora_radio_context_t context = adapter_context();
  const uint8_t command[] = {0x1Du, 0x00u, 0x42u, 0x00u};
  const uint8_t scripted[] = {0u, 0u, 0u, 0u, 0x12u, 0x34u};
  hal_mock_spi_push_rx(1u, scripted, sizeof(scripted));
  uint8_t response[2] = {};
  TEST_ASSERT_EQUAL_INT(SX126X_HAL_STATUS_OK,
                        sx126x_hal_read(&context, command, sizeof(command),
                                        response, sizeof(response)));
  TEST_ASSERT_EQUAL_HEX8(0x12u, response[0]);
  TEST_ASSERT_EQUAL_HEX8(0x34u, response[1]);
  TEST_ASSERT_EQUAL_INT(0, hal_mock_spi_get_lock_depth(1u));
  TEST_ASSERT_FALSE(hal_mock_spi_transaction_active(1u));
  TEST_ASSERT_TRUE(hal_mock_gpio_get_state(3u));
}

void test_adapter_cleans_up_failed_begin_write_and_end(void) {
  jh_lora_radio_context_t context = adapter_context();
  const uint8_t command[] = {0x80u, 0x00u};

  hal_mock_spi_fail_next_begin(1u, true);
  TEST_ASSERT_EQUAL_INT(
      SX126X_HAL_STATUS_ERROR,
      sx126x_hal_write(&context, command, sizeof(command), NULL, 0u));
  TEST_ASSERT_EQUAL_INT(0, hal_mock_spi_get_lock_depth(1u));
  TEST_ASSERT_FALSE(hal_mock_spi_transaction_active(1u));
  TEST_ASSERT_EQUAL_INT(HAL_EBUS, context.provider_last_status);

  hal_mock_spi_fail_next_write(1u, true);
  TEST_ASSERT_EQUAL_INT(
      SX126X_HAL_STATUS_ERROR,
      sx126x_hal_write(&context, command, sizeof(command), NULL, 0u));
  TEST_ASSERT_EQUAL_INT(0, hal_mock_spi_get_lock_depth(1u));
  TEST_ASSERT_TRUE(hal_mock_gpio_get_state(3u));
  TEST_ASSERT_EQUAL_INT(HAL_EBUS, context.provider_last_status);

  hal_mock_spi_fail_next_end(1u, true);
  TEST_ASSERT_EQUAL_INT(
      SX126X_HAL_STATUS_ERROR,
      sx126x_hal_write(&context, command, sizeof(command), NULL, 0u));
  TEST_ASSERT_EQUAL_INT(0, hal_mock_spi_get_lock_depth(1u));
  TEST_ASSERT_FALSE(hal_mock_spi_transaction_active(1u));
  TEST_ASSERT_EQUAL_INT(HAL_EBUS, context.provider_last_status);
}

void test_busy_timeout_is_bounded_and_reported(void) {
  jh_lora_radio_context_t context = adapter_context();
  hal_mock_gpio_inject_level(context.config.hardware.sx126x.busy_pin, true);
  const uint64_t started_us = hal_micros64();
  TEST_ASSERT_EQUAL_INT(HAL_ETIMEOUT, jh_sx126x_wait_while_busy(&context, 1u));
  TEST_ASSERT_GREATER_OR_EQUAL_UINT64(UINT64_C(1000),
                                      hal_micros64() - started_us);
  TEST_ASSERT_EQUAL_INT(HAL_ETIMEOUT, context.provider_last_status);
  TEST_ASSERT_EQUAL_UINT(0u, hal_mock_spi_get_tx(1u, NULL, 0u));
}

void test_rf_switch_helpers_drive_declared_dual_gpio_truth_table(void) {
  jh_lora_radio_context_t context = adapter_context();
  jh_sx126x_set_rf_rx(&context);
  TEST_ASSERT_TRUE(hal_mock_gpio_get_state(21u));
  TEST_ASSERT_FALSE(hal_mock_gpio_get_state(22u));
  jh_sx126x_set_rf_tx(&context);
  TEST_ASSERT_FALSE(hal_mock_gpio_get_state(21u));
  TEST_ASSERT_TRUE(hal_mock_gpio_get_state(22u));
  jh_sx126x_set_rf_idle(&context);
  TEST_ASSERT_FALSE(hal_mock_gpio_get_state(21u));
  TEST_ASSERT_FALSE(hal_mock_gpio_get_state(22u));
}

void test_provider_initialize_resets_and_emits_electrical_profile_commands(
    void) {
  jh_lora_radio_context_t context = adapter_context();
  context.config.hardware.sx126x.rf_switch_mode = HAL_LORA_RF_SWITCH_DUAL_GPIO;
  context.config.hardware.sx126x.regulator_mode = HAL_LORA_REGULATOR_DCDC;
  context.config.hardware.sx126x.tcxo_control = HAL_LORA_TCXO_CONTROL_DIO3;
  context.config.hardware.sx126x.tcxo_voltage = HAL_LORA_TCXO_1V8;
  context.config.hardware.sx126x.tcxo_startup_us = 5000u;
  uint8_t responses[256];
  /* 0x22 is the observed post-init STBY_RC status on RP2040-LoRa-LF. */
  memset(responses, 0x22, sizeof(responses));
  hal_mock_spi_push_rx(1u, responses, sizeof(responses));

  const jh_lora_radio_provider_ops_t *provider = jh_sx126x_provider_ops();
  TEST_ASSERT_EQUAL_INT(HAL_OK, provider->initialize(&context));
  TEST_ASSERT_TRUE(hal_mock_spi_is_initialized());
  TEST_ASSERT_EQUAL_UINT8(context.config.spi_miso_pin,
                          hal_mock_spi_get_rx_pin());
  TEST_ASSERT_EQUAL_UINT8(context.config.spi_mosi_pin,
                          hal_mock_spi_get_tx_pin());
  TEST_ASSERT_EQUAL_UINT8(context.config.spi_sck_pin,
                          hal_mock_spi_get_sck_pin());
  TEST_ASSERT_TRUE(
      hal_mock_gpio_get_state(context.config.hardware.sx126x.reset_pin));
  TEST_ASSERT_GREATER_OR_EQUAL_UINT64(UINT64_C(10200), hal_micros64());
  uint8_t tx[512] = {};
  const size_t tx_length = hal_mock_spi_get_tx(1u, tx, sizeof(tx));
  const uint8_t standby[] = {0x80u, 0x00u};
  const uint8_t regulator[] = {0x96u, 0x01u};
  const uint8_t dio2_rf_switch[] = {0x9Du, 0x00u};
  const uint8_t clear_device_errors[] = {0x07u, 0x00u, 0x00u};
  const uint8_t tcxo[] = {0x97u, 0x02u, 0x00u, 0x01u, 0x40u};
  const uint8_t pa[] = {0x95u, 0x04u, 0x07u, 0x00u, 0x01u};
  TEST_ASSERT_TRUE(contains_bytes(tx, tx_length, standby, sizeof(standby)));
  TEST_ASSERT_TRUE(contains_bytes(tx, tx_length, regulator, sizeof(regulator)));
  TEST_ASSERT_TRUE(
      contains_bytes(tx, tx_length, dio2_rf_switch, sizeof(dio2_rf_switch)));
  TEST_ASSERT_TRUE(contains_bytes(tx, tx_length, clear_device_errors,
                                  sizeof(clear_device_errors)));
  TEST_ASSERT_TRUE(contains_bytes(tx, tx_length, tcxo, sizeof(tcxo)));
  TEST_ASSERT_TRUE(contains_bytes(tx, tx_length, pa, sizeof(pa)));
}

void test_provider_encodes_lora_configuration_and_tx_timeout(void) {
  jh_lora_radio_context_t context = adapter_context();
  configure_context_modem(&context);
  const jh_lora_radio_provider_ops_t *provider = jh_sx126x_provider_ops();
  TEST_ASSERT_EQUAL_INT(HAL_OK, provider->configure(&context));

  uint8_t tx[512] = {};
  size_t tx_length = hal_mock_spi_get_tx(1u, tx, sizeof(tx));
  const uint8_t packet_type[] = {0x8Au, 0x01u};
  const uint8_t image_calibration[] = {0x98u, 0xD7u, 0xDBu};
  const uint8_t modulation[] = {0x8Bu, 0x09u, 0x04u, 0x01u, 0x00u};
  const uint8_t tx_params[] = {0x8Eu, 0x0Au, 0x04u};
  TEST_ASSERT_TRUE(
      contains_bytes(tx, tx_length, packet_type, sizeof(packet_type)));
  TEST_ASSERT_TRUE(contains_bytes(tx, tx_length, image_calibration,
                                  sizeof(image_calibration)));
  TEST_ASSERT_TRUE(
      contains_bytes(tx, tx_length, modulation, sizeof(modulation)));
  TEST_ASSERT_TRUE(contains_bytes(tx, tx_length, tx_params, sizeof(tx_params)));

  context.config.hardware.sx126x.min_frequency_hz = UINT32_C(410000000);
  context.modem.frequency_hz = UINT32_C(434000000);
  TEST_ASSERT_EQUAL_INT(HAL_OK, provider->configure(&context));
  tx_length = hal_mock_spi_get_tx(1u, tx, sizeof(tx));
  const uint8_t lf_image_calibration[] = {0x98u, 0x6Bu, 0x6Fu};
  TEST_ASSERT_TRUE(contains_bytes(tx, tx_length, lf_image_calibration,
                                  sizeof(lf_image_calibration)));

  context.tx_buffer[0] = 0xA5u;
  context.tx_buffer[1] = 0x5Au;
  context.tx_length = 2u;
  const uint8_t no_irq[256] = {};
  hal_mock_spi_push_rx(1u, no_irq, sizeof(no_irq));
  TEST_ASSERT_EQUAL_INT(HAL_ETIMEOUT, provider->transmit(&context, 2u));
  tx_length = hal_mock_spi_get_tx(1u, tx, sizeof(tx));
  const uint8_t write_buffer[] = {0x0Eu, 0x00u, 0xA5u, 0x5Au};
  const uint8_t set_tx[] = {0x83u};
  TEST_ASSERT_TRUE(
      contains_bytes(tx, tx_length, write_buffer, sizeof(write_buffer)));
  TEST_ASSERT_TRUE(contains_bytes(tx, tx_length, set_tx, sizeof(set_tx)));
  TEST_ASSERT_FALSE(hal_mock_gpio_get_state(21u));
  TEST_ASSERT_FALSE(hal_mock_gpio_get_state(22u));
}

void test_provider_receive_poll_maps_crc_irq(void) {
  jh_lora_radio_context_t context = adapter_context();
  configure_context_modem(&context);
  const jh_lora_radio_provider_ops_t *provider = jh_sx126x_provider_ops();
  TEST_ASSERT_EQUAL_INT(HAL_OK, provider->configure(&context));
  context.receive_started_ms = hal_millis();
  context.receive_timeout_ms = 50u;
  context.receive_continuous = false;
  TEST_ASSERT_EQUAL_INT(HAL_OK, provider->receive_start(&context, 50u, false));
  const uint8_t crc_irq[] = {0u, 0u, 0x00u, 0x40u};
  hal_mock_spi_push_rx(1u, crc_irq, sizeof(crc_irq));
  TEST_ASSERT_EQUAL_INT(HAL_EPROTO, provider->receive_poll(&context));
  TEST_ASSERT_FALSE(hal_mock_gpio_get_state(21u));
  TEST_ASSERT_FALSE(hal_mock_gpio_get_state(22u));
}

int main(void) {
  UNITY_BEGIN();
  RUN_TEST(test_adapter_write_uses_one_mode0_msb_transaction);
  RUN_TEST(test_adapter_read_clocks_response_and_releases_bus);
  RUN_TEST(test_adapter_cleans_up_failed_begin_write_and_end);
  RUN_TEST(test_busy_timeout_is_bounded_and_reported);
  RUN_TEST(test_rf_switch_helpers_drive_declared_dual_gpio_truth_table);
  RUN_TEST(
      test_provider_initialize_resets_and_emits_electrical_profile_commands);
  RUN_TEST(test_provider_encodes_lora_configuration_and_tx_timeout);
  RUN_TEST(test_provider_receive_poll_maps_crc_irq);
  return UNITY_END();
}
