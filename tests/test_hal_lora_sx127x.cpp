#include "hal/impl/.mock/hal_mock.h"
#include "hal/radio/hal_lora_radio.h"
#include "utils/unity.h"

static hal_lora_radio_config_t radio_config(hal_lora_radio_model_t model) {
  hal_lora_radio_config_t config = {};
  config.model = model;
  config.spi_miso_pin = 1u;
  config.spi_mosi_pin = 2u;
  config.spi_sck_pin = 3u;
  config.cs_pin = 4u;
  config.spi_clock_hz = UINT32_C(4000000);
  config.hardware.sx127x.reset_pin = 5u;
  config.hardware.sx127x.dio0_pin = 6u;
  config.hardware.sx127x.dio1_pin = 7u;
  config.hardware.sx127x.dio2_pin = HAL_LORA_PIN_NONE;
  config.hardware.sx127x.rf_switch_rx_pin = HAL_LORA_PIN_NONE;
  config.hardware.sx127x.rf_switch_tx_pin = HAL_LORA_PIN_NONE;
  config.hardware.sx127x.tcxo_enable_pin = HAL_LORA_PIN_NONE;
  config.hardware.sx127x.max_spi_clock_hz = UINT32_C(10000000);
  if (model == HAL_LORA_RADIO_SX1278) {
    config.hardware.sx127x.pa_output = HAL_LORA_SX127X_PA_RFO;
    config.hardware.sx127x.min_frequency_hz = UINT32_C(410000000);
    config.hardware.sx127x.max_frequency_hz = UINT32_C(525000000);
    config.hardware.sx127x.min_tx_power_dbm = -4;
    config.hardware.sx127x.max_tx_power_dbm = 15;
  } else {
    config.hardware.sx127x.pa_output = HAL_LORA_SX127X_PA_BOOST;
    config.hardware.sx127x.min_frequency_hz = UINT32_C(850000000);
    config.hardware.sx127x.max_frequency_hz = UINT32_C(930000000);
    config.hardware.sx127x.min_tx_power_dbm = 2;
    config.hardware.sx127x.max_tx_power_dbm = 20;
  }
  return config;
}

static hal_lora_modem_config_t modem_config(hal_lora_radio_model_t model) {
  hal_lora_modem_config_t modem = {};
  modem.frequency_hz = model == HAL_LORA_RADIO_SX1278 ? UINT32_C(433000000)
                                                      : UINT32_C(868100000);
  modem.bandwidth_hz = UINT32_C(125000);
  modem.spreading_factor = 7u;
  modem.coding_rate = 5u;
  modem.tx_power_dbm = 10;
  modem.preamble_symbols = 8u;
  modem.sync_word = 0x12u;
  modem.explicit_header = true;
  modem.crc_enabled = true;
  return modem;
}

void setUp(void) {
  hal_mock_set_millis(0u);
  hal_mock_lora_reset();
}

void tearDown(void) { hal_mock_lora_reset(); }

static void exercise_model(hal_lora_radio_model_t model) {
  hal_lora_radio_config_t hardware = radio_config(model);
  hal_lora_radio_t radio = NULL;
  TEST_ASSERT_EQUAL_INT(HAL_OK, hal_lora_radio_create(&hardware, &radio));
  hal_lora_modem_config_t modem = modem_config(model);
  TEST_ASSERT_EQUAL_INT(HAL_OK, hal_lora_radio_configure(radio, &modem));

  hal_lora_radio_capabilities_t capabilities = {};
  TEST_ASSERT_EQUAL_INT(HAL_OK,
                        hal_lora_radio_get_capabilities(radio, &capabilities));
  TEST_ASSERT_EQUAL_INT(model, capabilities.model);
  TEST_ASSERT_TRUE(capabilities.supports_channel_activity_detection);
  TEST_ASSERT_TRUE(capabilities.supports_instant_rssi);
  TEST_ASSERT_FALSE(capabilities.supports_explicit_calibration);
  TEST_ASSERT_EQUAL_INT(HAL_EUNSUPPORTED, hal_lora_radio_calibrate(radio));
  hal_lora_radio_state_t state = HAL_LORA_RADIO_STATE_ERROR;
  TEST_ASSERT_EQUAL_INT(HAL_OK, hal_lora_radio_get_state(radio, &state));
  TEST_ASSERT_EQUAL_INT(HAL_LORA_RADIO_STATE_STANDBY, state);

  const uint8_t payload[] = {0x12u, 0x34u};
  TEST_ASSERT_EQUAL_INT(
      HAL_OK, hal_lora_radio_transmit_start(radio, payload, sizeof(payload)));
  TEST_ASSERT_EQUAL_INT(HAL_OK, hal_lora_radio_process(radio));
  hal_lora_operation_status_t tx = {};
  TEST_ASSERT_EQUAL_INT(HAL_OK, hal_lora_radio_get_tx_status(radio, &tx));
  TEST_ASSERT_EQUAL_INT(HAL_LORA_OPERATION_SUCCEEDED, tx.state);

  TEST_ASSERT_EQUAL_INT(HAL_OK, hal_lora_radio_receive_start(radio, 100u));
  hal_lora_packet_info_t packet_info = {};
  packet_info.rssi_dbm = -80;
  packet_info.snr_db = 7;
  packet_info.crc_valid = true;
  TEST_ASSERT_EQUAL_INT(HAL_OK, hal_mock_lora_inject_receive(radio, payload,
                                                             sizeof(payload),
                                                             &packet_info));
  TEST_ASSERT_EQUAL_INT(HAL_OK, hal_lora_radio_process(radio));
  uint8_t received[sizeof(payload)] = {};
  size_t received_length = 0u;
  TEST_ASSERT_EQUAL_INT(
      HAL_OK, hal_lora_radio_receive(radio, received, sizeof(received),
                                     &received_length, &packet_info));
  TEST_ASSERT_EQUAL_UINT(sizeof(payload), received_length);
  TEST_ASSERT_EQUAL_MEMORY(payload, received, sizeof(payload));

  TEST_ASSERT_EQUAL_INT(
      HAL_OK, hal_lora_radio_channel_activity_detect_start(radio, 20u));
  TEST_ASSERT_EQUAL_INT(HAL_OK,
                        hal_mock_lora_inject_channel_activity(radio, true));
  TEST_ASSERT_EQUAL_INT(HAL_OK, hal_lora_radio_process(radio));
  hal_lora_channel_activity_status_t cad = {};
  TEST_ASSERT_EQUAL_INT(
      HAL_OK, hal_lora_radio_get_channel_activity_status(radio, &cad));
  TEST_ASSERT_TRUE(cad.detected);
  TEST_ASSERT_EQUAL_INT(HAL_OK, hal_lora_radio_sleep(radio));
  TEST_ASSERT_EQUAL_INT(HAL_OK, hal_lora_radio_standby(radio));
  TEST_ASSERT_EQUAL_INT(HAL_OK, hal_lora_radio_destroy(radio));
}

void test_sx1276_uses_common_facade_lifecycle(void) {
  exercise_model(HAL_LORA_RADIO_SX1276);
}

void test_sx1278_uses_common_facade_lifecycle(void) {
  exercise_model(HAL_LORA_RADIO_SX1278);
}

void test_sx127x_model_specific_validation_is_strict(void) {
  hal_lora_radio_config_t hardware = radio_config(HAL_LORA_RADIO_SX1278);
  hal_lora_radio_t radio = NULL;
  hardware.hardware.sx127x.max_frequency_hz = UINT32_C(526000000);
  TEST_ASSERT_EQUAL_INT(HAL_EINVAL, hal_lora_radio_create(&hardware, &radio));
  hardware = radio_config(HAL_LORA_RADIO_SX1276);
  hardware.hardware.sx127x.min_tx_power_dbm = 1;
  TEST_ASSERT_EQUAL_INT(HAL_EINVAL, hal_lora_radio_create(&hardware, &radio));
  hardware = radio_config(HAL_LORA_RADIO_SX1276);
  TEST_ASSERT_EQUAL_INT(HAL_OK, hal_lora_radio_create(&hardware, &radio));
  hal_lora_modem_config_t modem = modem_config(HAL_LORA_RADIO_SX1276);
  modem.spreading_factor = 5u;
  TEST_ASSERT_EQUAL_INT(HAL_EINVAL, hal_lora_radio_configure(radio, &modem));
  TEST_ASSERT_EQUAL_INT(HAL_OK, hal_lora_radio_destroy(radio));
}

int main(void) {
  UNITY_BEGIN();
  RUN_TEST(test_sx1276_uses_common_facade_lifecycle);
  RUN_TEST(test_sx1278_uses_common_facade_lifecycle);
  RUN_TEST(test_sx127x_model_specific_validation_is_strict);
  return UNITY_END();
}
