#include "hal/impl/.mock/hal_mock.h"
#include "hal/radio/sx127x/jh_sx127x_adapter.h"
#include "hal/spi/hal_spi.h"
#include "hal/system/hal_system.h"
#include "utils/unity.h"

#include <string.h>

hal_status_t jh_lora_radio_describe_capabilities(
    const jh_lora_radio_context_t *context,
    jh_lora_provider_capabilities_t supported,
    hal_lora_radio_capabilities_t *out_capabilities) {
  if (context == NULL || out_capabilities == NULL ||
      (supported & ~JH_LORA_PROVIDER_CAP_ALL) != 0u) {
    return HAL_EINVAL;
  }
  const hal_lora_sx127x_hardware_config_t *hardware =
      &context->config.hardware.sx127x;
  *out_capabilities = {};
  out_capabilities->model = context->config.model;
  out_capabilities->max_payload_length = HAL_LORA_RADIO_MAX_PAYLOAD;
  out_capabilities->min_frequency_hz = hardware->min_frequency_hz;
  out_capabilities->max_frequency_hz = hardware->max_frequency_hz;
  out_capabilities->min_tx_power_dbm = hardware->min_tx_power_dbm;
  out_capabilities->max_tx_power_dbm = hardware->max_tx_power_dbm;
  out_capabilities->supports_continuous_receive =
      (supported & JH_LORA_PROVIDER_CAP_CONTINUOUS_RX) != 0u;
  out_capabilities->supports_channel_activity_detection =
      (supported & JH_LORA_PROVIDER_CAP_CAD) != 0u;
  out_capabilities->supports_instant_rssi =
      (supported & JH_LORA_PROVIDER_CAP_INSTANT_RSSI) != 0u;
  out_capabilities->supports_explicit_calibration =
      (supported & JH_LORA_PROVIDER_CAP_EXPLICIT_CALIBRATION) != 0u;
  return HAL_OK;
}

static jh_lora_radio_context_t
adapter_context(hal_lora_radio_model_t model = HAL_LORA_RADIO_SX1276) {
  jh_lora_radio_context_t context = {};
  context.config.model = model;
  context.config.spi_bus = 1u;
  context.config.spi_miso_pin = 12u;
  context.config.spi_mosi_pin = 11u;
  context.config.spi_sck_pin = 10u;
  context.config.cs_pin = 9u;
  context.config.spi_clock_hz = UINT32_C(4000000);
  context.config.hardware.sx127x.reset_pin = 20u;
  context.config.hardware.sx127x.dio0_pin = 21u;
  context.config.hardware.sx127x.dio1_pin = 22u;
  context.config.hardware.sx127x.dio2_pin = HAL_LORA_PIN_NONE;
  context.config.hardware.sx127x.rf_switch_rx_pin = 23u;
  context.config.hardware.sx127x.rf_switch_tx_pin = 24u;
  context.config.hardware.sx127x.rf_switch_rx_active_level = true;
  context.config.hardware.sx127x.rf_switch_tx_active_level = true;
  context.config.hardware.sx127x.tcxo_enable_pin = HAL_LORA_PIN_NONE;
  context.config.hardware.sx127x.pa_output = HAL_LORA_SX127X_PA_BOOST;
  context.config.hardware.sx127x.min_frequency_hz = UINT32_C(850000000);
  context.config.hardware.sx127x.max_frequency_hz = UINT32_C(930000000);
  context.config.hardware.sx127x.max_spi_clock_hz = UINT32_C(10000000);
  context.config.hardware.sx127x.min_tx_power_dbm = 2;
  context.config.hardware.sx127x.max_tx_power_dbm = 20;
  context.modem.frequency_hz = UINT32_C(868100000);
  context.modem.bandwidth_hz = UINT32_C(125000);
  context.modem.spreading_factor = 9u;
  context.modem.coding_rate = 5u;
  context.modem.tx_power_dbm = 14;
  context.modem.preamble_symbols = 8u;
  context.modem.sync_word = 0x12u;
  context.modem.explicit_header = true;
  context.modem.crc_enabled = true;
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

static void push_register_read(uint8_t value) {
  const uint8_t response[] = {0u, value};
  hal_mock_spi_push_rx(1u, response, sizeof(response));
}

static hal_status_t initialize(jh_lora_radio_context_t *context) {
  push_register_read(0x12u);
  return jh_sx127x_provider_ops()->initialize(context);
}

void setUp(void) {
  hal_mock_set_millis(0u);
  hal_mock_spi_reset();
}

void tearDown(void) {}

void test_register_transport_uses_mode0_msb_and_releases_bus(void) {
  jh_lora_radio_context_t context = adapter_context();
  TEST_ASSERT_EQUAL_INT(HAL_OK, hal_spi_init(1u, context.config.spi_miso_pin,
                                             context.config.spi_mosi_pin,
                                             context.config.spi_sck_pin));
  TEST_ASSERT_EQUAL_INT(HAL_OK,
                        jh_sx127x_write_register(&context, 0x39u, 0x12u));
  push_register_read(0xA5u);
  uint8_t value = 0u;
  TEST_ASSERT_EQUAL_INT(HAL_OK,
                        jh_sx127x_read_register(&context, 0x42u, &value));
  TEST_ASSERT_EQUAL_HEX8(0xA5u, value);
  uint8_t tx[8] = {};
  TEST_ASSERT_EQUAL_UINT(4u, hal_mock_spi_get_tx(1u, tx, sizeof(tx)));
  const uint8_t expected[] = {0xB9u, 0x12u, 0x42u, 0x00u};
  TEST_ASSERT_EQUAL_MEMORY(expected, tx, sizeof(expected));
  TEST_ASSERT_EQUAL_UINT32(UINT32_C(4000000), hal_mock_spi_get_clock_hz(1u));
  TEST_ASSERT_EQUAL_UINT8(HAL_SPI_MODE0, hal_mock_spi_get_data_mode(1u));
  TEST_ASSERT_EQUAL_INT(0, hal_mock_spi_get_lock_depth(1u));
  TEST_ASSERT_FALSE(hal_mock_spi_transaction_active(1u));
  TEST_ASSERT_TRUE(hal_mock_gpio_get_state(context.config.cs_pin));
}

void test_initialize_probes_version_and_configures_irq_lines(void) {
  jh_lora_radio_context_t context = adapter_context();
  TEST_ASSERT_EQUAL_INT(HAL_OK, initialize(&context));
  TEST_ASSERT_TRUE(context.provider_irq_attached);
  TEST_ASSERT_TRUE(
      hal_mock_gpio_get_state(context.config.hardware.sx127x.reset_pin));
  TEST_ASSERT_GREATER_OR_EQUAL_UINT64(UINT64_C(10200), hal_micros64());
  uint8_t tx[128] = {};
  const size_t length = hal_mock_spi_get_tx(1u, tx, sizeof(tx));
  const uint8_t version[] = {0x42u, 0x00u};
  const uint8_t lora_sleep[] = {0x81u, 0x80u};
  const uint8_t lora_standby[] = {0x81u, 0x81u};
  TEST_ASSERT_TRUE(contains_bytes(tx, length, version, sizeof(version)));
  TEST_ASSERT_TRUE(contains_bytes(tx, length, lora_sleep, sizeof(lora_sleep)));
  TEST_ASSERT_TRUE(
      contains_bytes(tx, length, lora_standby, sizeof(lora_standby)));
  TEST_ASSERT_EQUAL_INT(HAL_OK,
                        jh_sx127x_provider_ops()->deinitialize(&context));
  TEST_ASSERT_FALSE(context.provider_irq_attached);
}

void test_initialize_rejects_unknown_silicon_and_maps_bus_failure(void) {
  jh_lora_radio_context_t context = adapter_context();
  push_register_read(0x00u);
  TEST_ASSERT_EQUAL_INT(HAL_EHW,
                        jh_sx127x_provider_ops()->initialize(&context));

  hal_mock_spi_reset();
  context = adapter_context();
  hal_mock_spi_fail_next_begin(1u, true);
  TEST_ASSERT_EQUAL_INT(HAL_EBUS,
                        jh_sx127x_provider_ops()->initialize(&context));
  TEST_ASSERT_EQUAL_INT(0, hal_mock_spi_get_lock_depth(1u));
}

void test_sx1276_configure_programs_lora_modem_frequency_and_pa(void) {
  jh_lora_radio_context_t context = adapter_context();
  TEST_ASSERT_EQUAL_INT(HAL_OK, initialize(&context));
  hal_mock_spi_reset();
  TEST_ASSERT_EQUAL_INT(HAL_OK, jh_sx127x_provider_ops()->configure(&context));
  uint8_t tx[256] = {};
  const size_t length = hal_mock_spi_get_tx(1u, tx, sizeof(tx));
  const uint8_t frequency[] = {0x86u, 0xD9u, 0x06u, 0x66u};
  const uint8_t modem1[] = {0x9Du, 0x72u};
  const uint8_t modem2[] = {0x9Eu, 0x94u};
  const uint8_t pa[] = {0x89u, 0x8Cu};
  const uint8_t pa_dac[] = {0xCDu, 0x84u};
  TEST_ASSERT_TRUE(contains_bytes(tx, length, frequency, sizeof(frequency)));
  TEST_ASSERT_TRUE(contains_bytes(tx, length, modem1, sizeof(modem1)));
  TEST_ASSERT_TRUE(contains_bytes(tx, length, modem2, sizeof(modem2)));
  TEST_ASSERT_TRUE(contains_bytes(tx, length, pa, sizeof(pa)));
  TEST_ASSERT_TRUE(contains_bytes(tx, length, pa_dac, sizeof(pa_dac)));
}

void test_sx1278_uses_low_frequency_mode_and_rfo_power(void) {
  jh_lora_radio_context_t context = adapter_context(HAL_LORA_RADIO_SX1278);
  context.config.hardware.sx127x.pa_output = HAL_LORA_SX127X_PA_RFO;
  context.config.hardware.sx127x.min_frequency_hz = UINT32_C(410000000);
  context.config.hardware.sx127x.max_frequency_hz = UINT32_C(525000000);
  context.config.hardware.sx127x.min_tx_power_dbm = -4;
  context.config.hardware.sx127x.max_tx_power_dbm = 15;
  context.modem.frequency_hz = UINT32_C(433000000);
  context.modem.tx_power_dbm = 10;
  TEST_ASSERT_EQUAL_INT(HAL_OK, initialize(&context));
  hal_mock_spi_reset();
  TEST_ASSERT_EQUAL_INT(HAL_OK, jh_sx127x_provider_ops()->configure(&context));
  uint8_t tx[256] = {};
  const size_t length = hal_mock_spi_get_tx(1u, tx, sizeof(tx));
  const uint8_t low_frequency_standby[] = {0x81u, 0x89u};
  const uint8_t rfo_pa[] = {0x89u, 0x7Au};
  TEST_ASSERT_TRUE(contains_bytes(tx, length, low_frequency_standby,
                                  sizeof(low_frequency_standby)));
  TEST_ASSERT_TRUE(contains_bytes(tx, length, rfo_pa, sizeof(rfo_pa)));
}

void test_provider_tx_rssi_cad_timeout_cancel_and_power_states(void) {
  jh_lora_radio_context_t context = adapter_context();
  context.config.hardware.sx127x.tcxo_enable_pin = 25u;
  context.config.hardware.sx127x.tcxo_active_level = true;
  context.config.hardware.sx127x.tcxo_startup_us = 3000u;
  TEST_ASSERT_EQUAL_INT(HAL_OK, initialize(&context));
  TEST_ASSERT_TRUE(hal_mock_gpio_get_state(25u));
  context.configured = true;
  context.tx_buffer[0] = 0xA5u;
  context.tx_length = 1u;
  TEST_ASSERT_EQUAL_INT(
      HAL_OK, jh_sx127x_provider_ops()->transmit_start(&context, 100u));
  context.state = HAL_LORA_RADIO_STATE_TX;
  context.transmit_timeout_ms = 100u;
  const uint8_t tx_done[] = {0u, 0x08u, 0u, 0u};
  hal_mock_spi_push_rx(1u, tx_done, sizeof(tx_done));
  jh_lora_provider_events_t events = JH_LORA_PROVIDER_EVENT_NONE;
  TEST_ASSERT_EQUAL_INT(HAL_OK,
                        jh_sx127x_provider_ops()->process(&context, &events));
  TEST_ASSERT_BITS(JH_LORA_PROVIDER_EVENT_TX_DONE | JH_LORA_PROVIDER_EVENT_IRQ,
                   JH_LORA_PROVIDER_EVENT_TX_DONE | JH_LORA_PROVIDER_EVENT_IRQ,
                   events);

  push_register_read(100u);
  int16_t rssi_dbm = 0;
  TEST_ASSERT_EQUAL_INT(
      HAL_OK, jh_sx127x_provider_ops()->get_instant_rssi(&context, &rssi_dbm));
  TEST_ASSERT_EQUAL_INT16(-57, rssi_dbm);

  TEST_ASSERT_EQUAL_INT(
      HAL_OK,
      jh_sx127x_provider_ops()->channel_activity_detect_start(&context, 10u));
  context.state = HAL_LORA_RADIO_STATE_CAD;
  context.channel_activity_started_ms = hal_millis();
  context.channel_activity_timeout_ms = 10u;
  const uint8_t cad_detected[] = {0u, 0x05u, 0u, 0u};
  hal_mock_spi_push_rx(1u, cad_detected, sizeof(cad_detected));
  TEST_ASSERT_EQUAL_INT(HAL_OK,
                        jh_sx127x_provider_ops()->process(&context, &events));
  TEST_ASSERT_BITS(
      JH_LORA_PROVIDER_EVENT_CAD_DONE | JH_LORA_PROVIDER_EVENT_CAD_DETECTED,
      JH_LORA_PROVIDER_EVENT_CAD_DONE | JH_LORA_PROVIDER_EVENT_CAD_DETECTED,
      events);

  context.channel_activity_started_ms = hal_millis();
  context.channel_activity_timeout_ms = 5u;
  hal_mock_set_millis(context.channel_activity_started_ms + 5u);
  TEST_ASSERT_EQUAL_INT(HAL_OK,
                        jh_sx127x_provider_ops()->process(&context, &events));
  TEST_ASSERT_EQUAL_UINT32(JH_LORA_PROVIDER_EVENT_TIMEOUT, events);
  TEST_ASSERT_EQUAL_INT(HAL_OK, jh_sx127x_provider_ops()->cancel(&context));
  TEST_ASSERT_EQUAL_INT(HAL_OK, jh_sx127x_provider_ops()->sleep(&context));
  TEST_ASSERT_TRUE(context.provider_sleeping);
  TEST_ASSERT_FALSE(hal_mock_gpio_get_state(25u));
  TEST_ASSERT_EQUAL_INT(HAL_OK, jh_sx127x_provider_ops()->standby(&context));
  TEST_ASSERT_FALSE(context.provider_sleeping);
  TEST_ASSERT_TRUE(hal_mock_gpio_get_state(25u));

  context.state = HAL_LORA_RADIO_STATE_RX;
  context.receive_continuous = true;
  hal_mock_spi_fail_next_begin(1u, true);
  TEST_ASSERT_EQUAL_INT(HAL_EBUS,
                        jh_sx127x_provider_ops()->process(&context, &events));
  TEST_ASSERT_EQUAL_INT(HAL_EBUS, context.provider_last_status);
  TEST_ASSERT_EQUAL_INT(0, hal_mock_spi_get_lock_depth(1u));
}

void test_provider_receives_fifo_and_maps_packet_metadata(void) {
  jh_lora_radio_context_t context = adapter_context();
  TEST_ASSERT_EQUAL_INT(HAL_OK, initialize(&context));
  context.configured = true;
  TEST_ASSERT_EQUAL_INT(
      HAL_OK, jh_sx127x_provider_ops()->receive_start(&context, 100u, false));
  context.state = HAL_LORA_RADIO_STATE_RX;
  context.receive_timeout_ms = 100u;
  const uint8_t response[] = {
      0u, 0x40u, 0u,    0u,    0u,    3u, 0u,    0u, 0u,
      0u, 0u,    0xA1u, 0xB2u, 0xC3u, 0u, 0x20u, 0u, 100u,
  };
  hal_mock_spi_push_rx(1u, response, sizeof(response));
  jh_lora_provider_events_t events = JH_LORA_PROVIDER_EVENT_NONE;
  TEST_ASSERT_EQUAL_INT(HAL_OK,
                        jh_sx127x_provider_ops()->process(&context, &events));
  TEST_ASSERT_BITS(JH_LORA_PROVIDER_EVENT_RX_DONE | JH_LORA_PROVIDER_EVENT_IRQ,
                   JH_LORA_PROVIDER_EVENT_RX_DONE | JH_LORA_PROVIDER_EVENT_IRQ,
                   events);
  const uint8_t expected[] = {0xA1u, 0xB2u, 0xC3u};
  TEST_ASSERT_EQUAL_UINT(sizeof(expected), context.rx_length);
  TEST_ASSERT_EQUAL_MEMORY(expected, context.rx_buffer, sizeof(expected));
  TEST_ASSERT_EQUAL_INT8(8, context.rx_info.snr_db);
  TEST_ASSERT_EQUAL_INT16(-57, context.rx_info.rssi_dbm);
  TEST_ASSERT_TRUE(context.rx_info.crc_valid);

  const uint8_t crc_error[] = {0u, 0x60u, 0u, 0u};
  hal_mock_spi_push_rx(1u, crc_error, sizeof(crc_error));
  TEST_ASSERT_EQUAL_INT(HAL_OK,
                        jh_sx127x_provider_ops()->process(&context, &events));
  TEST_ASSERT_BITS(
      JH_LORA_PROVIDER_EVENT_CRC_ERROR | JH_LORA_PROVIDER_EVENT_IRQ,
      JH_LORA_PROVIDER_EVENT_CRC_ERROR | JH_LORA_PROVIDER_EVENT_IRQ, events);
}

void test_capabilities_expose_sx127x_optional_operation_boundary(void) {
  jh_lora_radio_context_t context = adapter_context();
  hal_lora_radio_capabilities_t capabilities = {};
  TEST_ASSERT_EQUAL_INT(HAL_OK, jh_sx127x_provider_ops()->get_capabilities(
                                    &context, &capabilities));
  TEST_ASSERT_EQUAL_INT(HAL_LORA_RADIO_SX1276, capabilities.model);
  TEST_ASSERT_TRUE(capabilities.supports_continuous_receive);
  TEST_ASSERT_TRUE(capabilities.supports_channel_activity_detection);
  TEST_ASSERT_TRUE(capabilities.supports_instant_rssi);
  TEST_ASSERT_FALSE(capabilities.supports_explicit_calibration);
  TEST_ASSERT_EQUAL_INT(HAL_EUNSUPPORTED,
                        jh_sx127x_provider_ops()->calibrate(&context));
}

int main(void) {
  UNITY_BEGIN();
  RUN_TEST(test_register_transport_uses_mode0_msb_and_releases_bus);
  RUN_TEST(test_initialize_probes_version_and_configures_irq_lines);
  RUN_TEST(test_initialize_rejects_unknown_silicon_and_maps_bus_failure);
  RUN_TEST(test_sx1276_configure_programs_lora_modem_frequency_and_pa);
  RUN_TEST(test_sx1278_uses_low_frequency_mode_and_rfo_power);
  RUN_TEST(test_provider_tx_rssi_cad_timeout_cancel_and_power_states);
  RUN_TEST(test_provider_receives_fifo_and_maps_packet_metadata);
  RUN_TEST(test_capabilities_expose_sx127x_optional_operation_boundary);
  return UNITY_END();
}
