#include "hal/hal_lora_radio.h"
#include "hal/impl/.mock/hal_mock.h"
#include "utils/unity.h"

#include <string.h>

static hal_lora_radio_config_t test_radio_config(void) {
  hal_lora_radio_config_t config = {};
  config.model = HAL_LORA_RADIO_SX1262;
  config.spi_bus = 0u;
  config.spi_miso_pin = 16u;
  config.spi_mosi_pin = 19u;
  config.spi_sck_pin = 18u;
  config.cs_pin = 17u;
  config.spi_clock_hz = HAL_LORA_SPI_CLOCK_DEFAULT_HZ;
  TEST_ASSERT_EQUAL_INT(
      HAL_OK, hal_lora_sx126x_core1262_hf_defaults(&config.hardware.sx126x));
  config.hardware.sx126x.reset_pin = 20u;
  config.hardware.sx126x.busy_pin = 21u;
  config.hardware.sx126x.dio1_pin = 22u;
  config.hardware.sx126x.rf_switch_pin_a = 10u;
  config.hardware.sx126x.rf_switch_pin_b = 11u;
  return config;
}

static hal_lora_radio_t create_configured_radio(void) {
  const hal_lora_radio_config_t hardware = test_radio_config();
  hal_lora_radio_t radio = NULL;
  TEST_ASSERT_EQUAL_INT(HAL_OK, hal_lora_radio_create(&hardware, &radio));
  TEST_ASSERT_NOT_NULL(radio);
  const hal_lora_modem_config_t modem = hal_lora_default_eu868();
  TEST_ASSERT_EQUAL_INT(HAL_OK, hal_lora_radio_configure(radio, &modem));
  return radio;
}

void setUp(void) {
  hal_mock_set_millis(0u);
  hal_mock_lora_reset();
}

void tearDown(void) { hal_mock_lora_reset(); }

void test_core1262_hf_defaults_and_presets_are_valid(void) {
  hal_lora_sx126x_hardware_config_t hardware = {};
  TEST_ASSERT_EQUAL_INT(HAL_OK,
                        hal_lora_sx126x_core1262_hf_defaults(&hardware));
  TEST_ASSERT_EQUAL_INT(HAL_LORA_RF_SWITCH_DUAL_GPIO, hardware.rf_switch_mode);
  TEST_ASSERT_EQUAL_INT(HAL_LORA_REGULATOR_DCDC, hardware.regulator_mode);
  TEST_ASSERT_EQUAL_INT(HAL_LORA_TCXO_CONTROL_DIO3, hardware.tcxo_control);
  TEST_ASSERT_EQUAL_INT(HAL_LORA_TCXO_1V8, hardware.tcxo_voltage);
  TEST_ASSERT_FALSE(hardware.rf_switch_rx_level_a);
  TEST_ASSERT_TRUE(hardware.rf_switch_rx_level_b);
  TEST_ASSERT_TRUE(hardware.rf_switch_tx_level_a);
  TEST_ASSERT_FALSE(hardware.rf_switch_tx_level_b);
  TEST_ASSERT_EQUAL_UINT32(UINT32_C(850000000), hardware.min_frequency_hz);
  TEST_ASSERT_EQUAL_UINT32(UINT32_C(930000000), hardware.max_frequency_hz);
  TEST_ASSERT_EQUAL_INT(HAL_EINVAL, hal_lora_sx126x_core1262_hf_defaults(NULL));

  const hal_lora_modem_config_t balanced = hal_lora_default_eu868();
  const hal_lora_modem_config_t long_range =
      hal_lora_default_long_range_eu868();
  const hal_lora_modem_config_t fast = hal_lora_default_fast_eu868();
  TEST_ASSERT_EQUAL_UINT32(UINT32_C(868100000), balanced.frequency_hz);
  TEST_ASSERT_EQUAL_UINT8(12u, long_range.spreading_factor);
  TEST_ASSERT_EQUAL_UINT8(7u, fast.spreading_factor);

  uint32_t balanced_ms = 0u;
  uint32_t long_range_ms = 0u;
  uint32_t fast_ms = 0u;
  TEST_ASSERT_EQUAL_INT(HAL_OK,
                        hal_lora_time_on_air(&balanced, 32u, &balanced_ms));
  TEST_ASSERT_EQUAL_INT(HAL_OK,
                        hal_lora_time_on_air(&long_range, 32u, &long_range_ms));
  TEST_ASSERT_EQUAL_INT(HAL_OK, hal_lora_time_on_air(&fast, 32u, &fast_ms));
  TEST_ASSERT_GREATER_THAN(fast_ms, balanced_ms);
  TEST_ASSERT_GREATER_THAN(balanced_ms, long_range_ms);
  TEST_ASSERT_EQUAL_INT(HAL_EINVAL,
                        hal_lora_time_on_air(&balanced, 256u, &fast_ms));
  TEST_ASSERT_EQUAL_INT(HAL_EINVAL, hal_lora_time_on_air(&balanced, 1u, NULL));
}

void test_transmit_copies_payload_and_updates_state_and_diagnostics(void) {
  const hal_lora_radio_t radio = create_configured_radio();
  const uint8_t payload[] = {0x01u, 0xA5u, 0x5Au, 0xFFu};
  TEST_ASSERT_EQUAL_INT(
      HAL_OK, hal_lora_radio_transmit(radio, payload, sizeof(payload), 0u));

  uint8_t captured[sizeof(payload)] = {};
  size_t captured_length = 0u;
  TEST_ASSERT_EQUAL_INT(
      HAL_OK, hal_mock_lora_get_last_transmit(radio, captured, sizeof(captured),
                                              &captured_length));
  TEST_ASSERT_EQUAL_UINT(sizeof(payload), captured_length);
  TEST_ASSERT_EQUAL_MEMORY(payload, captured, sizeof(payload));

  hal_lora_radio_state_t state = HAL_LORA_RADIO_STATE_ERROR;
  TEST_ASSERT_EQUAL_INT(HAL_OK, hal_lora_radio_get_state(radio, &state));
  TEST_ASSERT_EQUAL_INT(HAL_LORA_RADIO_STATE_STANDBY, state);
  hal_lora_radio_diagnostics_t diagnostics = {};
  TEST_ASSERT_EQUAL_INT(HAL_OK,
                        hal_lora_radio_get_diagnostics(radio, &diagnostics));
  TEST_ASSERT_EQUAL_UINT32(1u, diagnostics.transmitted_packets);
  TEST_ASSERT_EQUAL_UINT32(1u, diagnostics.resets);

  TEST_ASSERT_EQUAL_INT(
      HAL_OK, hal_mock_lora_set_next_status(radio, HAL_MOCK_LORA_TRANSMIT,
                                            HAL_ETIMEOUT));
  TEST_ASSERT_EQUAL_INT(
      HAL_ETIMEOUT,
      hal_lora_radio_transmit(radio, payload, sizeof(payload), 10u));
  TEST_ASSERT_EQUAL_INT(HAL_OK,
                        hal_lora_radio_get_diagnostics(radio, &diagnostics));
  TEST_ASSERT_EQUAL_UINT32(1u, diagnostics.tx_timeouts);
  TEST_ASSERT_EQUAL_INT(HAL_ETIMEOUT, diagnostics.last_error);
  TEST_ASSERT_EQUAL_INT(HAL_OK, hal_lora_radio_destroy(radio));
}

void test_bounded_receive_reports_progress_packet_overflow_and_timeout(void) {
  const hal_lora_radio_t radio = create_configured_radio();
  TEST_ASSERT_EQUAL_INT(HAL_OK, hal_lora_radio_receive_start(radio, 50u));
  uint8_t buffer[3] = {};
  size_t length = 99u;
  TEST_ASSERT_EQUAL_INT(
      HAL_EAGAIN,
      hal_lora_radio_receive(radio, buffer, sizeof(buffer), &length, NULL));
  TEST_ASSERT_EQUAL_UINT(0u, length);

  const uint8_t packet[] = {1u, 2u, 3u, 4u, 5u};
  const hal_lora_packet_info_t injected = {
      -91, 7, -94, UINT32_C(42), true,
  };
  TEST_ASSERT_EQUAL_INT(HAL_OK, hal_mock_lora_inject_receive(
                                    radio, packet, sizeof(packet), &injected));
  hal_lora_packet_info_t received = {};
  TEST_ASSERT_EQUAL_INT(HAL_EOVERFLOW,
                        hal_lora_radio_receive(radio, buffer, sizeof(buffer),
                                               &length, &received));
  TEST_ASSERT_EQUAL_UINT(sizeof(packet), length);
  TEST_ASSERT_EQUAL_MEMORY(packet, buffer, sizeof(buffer));
  TEST_ASSERT_EQUAL_INT(-91, received.rssi_dbm);
  TEST_ASSERT_EQUAL_INT(7, received.snr_db);

  hal_lora_radio_diagnostics_t diagnostics = {};
  TEST_ASSERT_EQUAL_INT(HAL_OK,
                        hal_lora_radio_get_diagnostics(radio, &diagnostics));
  TEST_ASSERT_EQUAL_UINT32(1u, diagnostics.received_packets);
  TEST_ASSERT_EQUAL_UINT32(1u, diagnostics.dropped_packets);
  TEST_ASSERT_EQUAL_INT(-91, diagnostics.last_rssi_dbm);

  TEST_ASSERT_EQUAL_INT(HAL_OK, hal_lora_radio_receive_start(radio, 10u));
  hal_mock_advance_millis(10u);
  TEST_ASSERT_EQUAL_INT(
      HAL_ETIMEOUT,
      hal_lora_radio_receive(radio, buffer, sizeof(buffer), &length, NULL));
  TEST_ASSERT_EQUAL_INT(HAL_OK,
                        hal_lora_radio_get_diagnostics(radio, &diagnostics));
  TEST_ASSERT_EQUAL_UINT32(1u, diagnostics.rx_timeouts);
  TEST_ASSERT_EQUAL_INT(HAL_OK, hal_lora_radio_destroy(radio));
}

void test_continuous_receive_survives_packets_and_counts_crc_errors(void) {
  const hal_lora_radio_t radio = create_configured_radio();
  TEST_ASSERT_EQUAL_INT(HAL_OK, hal_lora_radio_receive_start_continuous(radio));
  const uint8_t packet[] = {9u, 8u};
  hal_lora_packet_info_t info = {-60, 10, -62, 1u, false};
  TEST_ASSERT_EQUAL_INT(HAL_OK, hal_mock_lora_inject_receive(
                                    radio, packet, sizeof(packet), &info));
  uint8_t buffer[8] = {};
  size_t length = 0u;
  TEST_ASSERT_EQUAL_INT(
      HAL_EPROTO,
      hal_lora_radio_receive(radio, buffer, sizeof(buffer), &length, NULL));

  info.crc_valid = true;
  TEST_ASSERT_EQUAL_INT(HAL_OK, hal_mock_lora_inject_receive(
                                    radio, packet, sizeof(packet), &info));
  TEST_ASSERT_EQUAL_INT(
      HAL_OK,
      hal_lora_radio_receive(radio, buffer, sizeof(buffer), &length, NULL));
  hal_lora_radio_state_t state = HAL_LORA_RADIO_STATE_ERROR;
  TEST_ASSERT_EQUAL_INT(HAL_OK, hal_lora_radio_get_state(radio, &state));
  TEST_ASSERT_EQUAL_INT(HAL_LORA_RADIO_STATE_RX, state);
  hal_lora_radio_diagnostics_t diagnostics = {};
  TEST_ASSERT_EQUAL_INT(HAL_OK,
                        hal_lora_radio_get_diagnostics(radio, &diagnostics));
  TEST_ASSERT_EQUAL_UINT32(1u, diagnostics.crc_errors);
  TEST_ASSERT_EQUAL_UINT32(1u, diagnostics.received_packets);
  TEST_ASSERT_EQUAL_INT(HAL_OK, hal_lora_radio_standby(radio));
  TEST_ASSERT_EQUAL_INT(HAL_OK, hal_lora_radio_sleep(radio));
  TEST_ASSERT_EQUAL_INT(HAL_OK, hal_lora_radio_get_state(radio, &state));
  TEST_ASSERT_EQUAL_INT(HAL_LORA_RADIO_STATE_SLEEP, state);
  TEST_ASSERT_EQUAL_INT(HAL_OK, hal_lora_radio_standby(radio));
  TEST_ASSERT_EQUAL_INT(HAL_OK, hal_lora_radio_destroy(radio));
}

void test_connected_mock_radios_exchange_only_while_receiving(void) {
  const hal_lora_radio_t first = create_configured_radio();
  const hal_lora_radio_t second = create_configured_radio();
  TEST_ASSERT_EQUAL_INT(HAL_OK, hal_mock_lora_connect(first, second));
  TEST_ASSERT_EQUAL_INT(HAL_OK,
                        hal_lora_radio_receive_start_continuous(second));
  const uint8_t payload[] = {'p', 'i', 'n', 'g'};
  TEST_ASSERT_EQUAL_INT(
      HAL_OK, hal_lora_radio_transmit(first, payload, sizeof(payload), 100u));
  uint8_t received[8] = {};
  size_t length = 0u;
  TEST_ASSERT_EQUAL_INT(HAL_OK, hal_lora_radio_receive(second, received,
                                                       sizeof(received),
                                                       &length, NULL));
  TEST_ASSERT_EQUAL_UINT(sizeof(payload), length);
  TEST_ASSERT_EQUAL_MEMORY(payload, received, sizeof(payload));
  TEST_ASSERT_EQUAL_INT(HAL_OK, hal_lora_radio_destroy(first));
  TEST_ASSERT_EQUAL_INT(HAL_OK, hal_lora_radio_destroy(second));
}

int main(void) {
  UNITY_BEGIN();
  RUN_TEST(test_core1262_hf_defaults_and_presets_are_valid);
  RUN_TEST(test_transmit_copies_payload_and_updates_state_and_diagnostics);
  RUN_TEST(test_bounded_receive_reports_progress_packet_overflow_and_timeout);
  RUN_TEST(test_continuous_receive_survives_packets_and_counts_crc_errors);
  RUN_TEST(test_connected_mock_radios_exchange_only_while_receiving);
  return UNITY_END();
}
