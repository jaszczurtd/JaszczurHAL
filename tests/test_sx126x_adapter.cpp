#include "hal/gpio/hal_gpio.h"
#include "hal/impl/.mock/hal_mock.h"
#include "hal/radio/sx126x/jh_sx126x_adapter.h"
#include "hal/spi/hal_spi.h"
#include "hal/system/hal_system.h"
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

static size_t count_bytes(const uint8_t *bytes, size_t length,
                          const uint8_t *expected, size_t expected_length) {
  size_t count = 0u;
  if (expected_length == 0u || expected_length > length) {
    return count;
  }
  for (size_t offset = 0u; offset <= length - expected_length; ++offset) {
    if (memcmp(&bytes[offset], expected, expected_length) == 0) {
      ++count;
    }
  }
  return count;
}

static void configure_context_modem(jh_lora_radio_context_t *context) {
  context->config.hardware.sx126x.min_frequency_hz = UINT32_C(850000000);
  context->config.hardware.sx126x.max_frequency_hz = UINT32_C(930000000);
  context->config.hardware.sx126x.min_tx_power_dbm = -9;
  context->config.hardware.sx126x.max_tx_power_dbm = 22;
  context->modem = hal_lora_default_eu868();
  context->modem.tx_power_dbm = 10;
}

static void reset_mock_transport(void) {
  hal_mock_spi_reset();
  hal_mock_gpio_inject_level(2u, false);
  TEST_ASSERT_EQUAL_INT(HAL_OK, hal_spi_init(1u, 12u, 11u, 10u));
  hal_gpio_set_mode(3u, HAL_GPIO_OUTPUT_HIGH);
}

void setUp(void) {
  hal_mock_set_millis(0u);
  reset_mock_transport();
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
  TEST_ASSERT_TRUE(context.provider_irq_attached);
  TEST_ASSERT_EQUAL_UINT32(1u, context.diagnostics.full_calibrations);
  TEST_ASSERT_EQUAL_INT(HAL_OK, provider->deinitialize(&context));
  TEST_ASSERT_FALSE(context.provider_irq_attached);
}

void test_provider_reports_capabilities_and_instant_rssi(void) {
  jh_lora_radio_context_t context = adapter_context();
  configure_context_modem(&context);
  const jh_lora_radio_provider_ops_t *provider = jh_sx126x_provider_ops();
  hal_lora_radio_capabilities_t capabilities = {};
  TEST_ASSERT_EQUAL_INT(HAL_OK,
                        provider->get_capabilities(&context, &capabilities));
  TEST_ASSERT_EQUAL_INT(HAL_LORA_RADIO_SX1262, capabilities.model);
  TEST_ASSERT_EQUAL_UINT16(HAL_LORA_RADIO_MAX_PAYLOAD,
                           capabilities.max_payload_length);
  TEST_ASSERT_EQUAL_UINT32(UINT32_C(850000000), capabilities.min_frequency_hz);
  TEST_ASSERT_EQUAL_UINT32(UINT32_C(930000000), capabilities.max_frequency_hz);
  TEST_ASSERT_TRUE(capabilities.supports_channel_activity_detection);
  TEST_ASSERT_TRUE(capabilities.supports_instant_rssi);
  TEST_ASSERT_TRUE(capabilities.supports_explicit_calibration);

  const uint8_t scripted_rssi[] = {0u, 0u, 0xA0u};
  hal_mock_spi_push_rx(1u, scripted_rssi, sizeof(scripted_rssi));
  int16_t rssi_dbm = 0;
  TEST_ASSERT_EQUAL_INT(HAL_OK,
                        provider->get_instant_rssi(&context, &rssi_dbm));
  TEST_ASSERT_EQUAL_INT(-80, rssi_dbm);
  uint8_t tx[32] = {};
  const size_t tx_length = hal_mock_spi_get_tx(1u, tx, sizeof(tx));
  const uint8_t get_rssi[] = {0x15u, 0x00u};
  TEST_ASSERT_TRUE(contains_bytes(tx, tx_length, get_rssi, sizeof(get_rssi)));
}

void test_image_calibration_boundaries_cache_and_explicit_calibration(void) {
  typedef struct {
    uint32_t frequency_hz;
    uint8_t lower;
    uint8_t upper;
  } calibration_case_t;
  static const calibration_case_t cases[] = {
      {UINT32_C(430000000), 0x6Bu, 0x6Fu}, {UINT32_C(440000000), 0x6Bu, 0x6Fu},
      {UINT32_C(470000000), 0x75u, 0x81u}, {UINT32_C(510000000), 0x75u, 0x81u},
      {UINT32_C(779000000), 0xC1u, 0xC5u}, {UINT32_C(787000000), 0xC1u, 0xC5u},
      {UINT32_C(863000000), 0xD7u, 0xDBu}, {UINT32_C(870000000), 0xD7u, 0xDBu},
      {UINT32_C(902000000), 0xE1u, 0xE9u}, {UINT32_C(928000000), 0xE1u, 0xE9u},
  };
  const jh_lora_radio_provider_ops_t *provider = jh_sx126x_provider_ops();
  uint8_t tx[4096] = {};
  for (size_t index = 0u; index < sizeof(cases) / sizeof(cases[0]); ++index) {
    reset_mock_transport();
    jh_lora_radio_context_t context = adapter_context();
    configure_context_modem(&context);
    context.config.hardware.sx126x.min_frequency_hz = UINT32_C(150000000);
    context.config.hardware.sx126x.max_frequency_hz = UINT32_C(960000000);
    context.modem.frequency_hz = cases[index].frequency_hz;
    const uint8_t expected[] = {0x98u, cases[index].lower, cases[index].upper};
    TEST_ASSERT_EQUAL_INT(HAL_OK, provider->configure(&context));
    const size_t tx_length = hal_mock_spi_get_tx(1u, tx, sizeof(tx));
    TEST_ASSERT_EQUAL_UINT(
        1u, count_bytes(tx, tx_length, expected, sizeof(expected)));
  }

  reset_mock_transport();
  jh_lora_radio_context_t context = adapter_context();
  configure_context_modem(&context);
  TEST_ASSERT_EQUAL_INT(HAL_OK, provider->configure(&context));
  size_t tx_length = hal_mock_spi_get_tx(1u, tx, sizeof(tx));
  const uint8_t image_calibration_opcode[] = {0x98u};
  const size_t first_calibrations =
      count_bytes(tx, tx_length, image_calibration_opcode,
                  sizeof(image_calibration_opcode));
  context.modem.frequency_hz = UINT32_C(870000000);
  TEST_ASSERT_EQUAL_INT(HAL_OK, provider->configure(&context));
  tx_length = hal_mock_spi_get_tx(1u, tx, sizeof(tx));
  TEST_ASSERT_EQUAL_UINT(first_calibrations,
                         count_bytes(tx, tx_length, image_calibration_opcode,
                                     sizeof(image_calibration_opcode)));

  context.modem.frequency_hz = UINT32_C(870000001);
  TEST_ASSERT_EQUAL_INT(HAL_OK, provider->configure(&context));
  tx_length = hal_mock_spi_get_tx(1u, tx, sizeof(tx));
  TEST_ASSERT_EQUAL_UINT(first_calibrations + 1u,
                         count_bytes(tx, tx_length, image_calibration_opcode,
                                     sizeof(image_calibration_opcode)));
  const uint32_t full_before = context.diagnostics.full_calibrations;
  const uint32_t image_before = context.diagnostics.image_calibrations;
  TEST_ASSERT_EQUAL_INT(HAL_OK, provider->calibrate(&context));
  TEST_ASSERT_EQUAL_UINT32(full_before + 1u,
                           context.diagnostics.full_calibrations);
  TEST_ASSERT_EQUAL_UINT32(image_before + 1u,
                           context.diagnostics.image_calibrations);
  TEST_ASSERT_TRUE(context.modem.frequency_hz >=
                   context.diagnostics.calibrated_frequency_min_hz);
  TEST_ASSERT_TRUE(context.modem.frequency_hz <=
                   context.diagnostics.calibrated_frequency_max_hz);

  const uint32_t full_after_success = context.diagnostics.full_calibrations;
  const uint32_t image_after_success = context.diagnostics.image_calibrations;
  hal_mock_spi_fail_next_write(1u, true);
  TEST_ASSERT_EQUAL_INT(HAL_EBUS, provider->calibrate(&context));
  TEST_ASSERT_EQUAL_UINT32(full_after_success,
                           context.diagnostics.full_calibrations);
  TEST_ASSERT_EQUAL_UINT32(image_after_success,
                           context.diagnostics.image_calibrations);
}

void test_channel_activity_detection_maps_irq_timeout_and_spi_error(void) {
  jh_lora_radio_context_t context = adapter_context();
  configure_context_modem(&context);
  const jh_lora_radio_provider_ops_t *provider = jh_sx126x_provider_ops();
  context.state = HAL_LORA_RADIO_STATE_CAD;
  context.channel_activity_started_ms = hal_millis();
  context.channel_activity_timeout_ms = 20u;
  TEST_ASSERT_EQUAL_INT(HAL_OK,
                        provider->channel_activity_detect_start(&context, 20u));
  uint8_t tx[512] = {};
  size_t tx_length = hal_mock_spi_get_tx(1u, tx, sizeof(tx));
  const uint8_t cad_parameters[] = {0x88u, 0x01u, 0x17u, 0x0Au,
                                    0x00u, 0x00u, 0x00u, 0x00u};
  const uint8_t set_cad[] = {0xC5u};
  TEST_ASSERT_TRUE(
      contains_bytes(tx, tx_length, cad_parameters, sizeof(cad_parameters)));
  TEST_ASSERT_TRUE(contains_bytes(tx, tx_length, set_cad, sizeof(set_cad)));
  TEST_ASSERT_TRUE(hal_mock_gpio_get_state(21u));
  TEST_ASSERT_FALSE(hal_mock_gpio_get_state(22u));

  const uint8_t cad_detected_irq[] = {0u, 0u, 0x01u, 0x80u};
  hal_mock_spi_push_rx(1u, cad_detected_irq, sizeof(cad_detected_irq));
  hal_mock_gpio_inject_level(context.config.hardware.sx126x.dio1_pin, true);
  jh_lora_provider_events_t events = JH_LORA_PROVIDER_EVENT_NONE;
  TEST_ASSERT_EQUAL_INT(HAL_OK, provider->process(&context, &events));
  TEST_ASSERT_BITS(JH_LORA_PROVIDER_EVENT_CAD_DONE,
                   JH_LORA_PROVIDER_EVENT_CAD_DONE, events);
  TEST_ASSERT_BITS(JH_LORA_PROVIDER_EVENT_CAD_DETECTED,
                   JH_LORA_PROVIDER_EVENT_CAD_DETECTED, events);
  TEST_ASSERT_BITS(JH_LORA_PROVIDER_EVENT_IRQ, JH_LORA_PROVIDER_EVENT_IRQ,
                   events);
  TEST_ASSERT_FALSE(hal_mock_gpio_get_state(21u));
  TEST_ASSERT_FALSE(hal_mock_gpio_get_state(22u));

  context = adapter_context();
  configure_context_modem(&context);
  context.state = HAL_LORA_RADIO_STATE_CAD;
  context.channel_activity_started_ms = hal_millis();
  context.channel_activity_timeout_ms = 5u;
  hal_mock_gpio_inject_level(context.config.hardware.sx126x.dio1_pin, false);
  TEST_ASSERT_EQUAL_INT(HAL_OK,
                        provider->channel_activity_detect_start(&context, 5u));
  hal_mock_advance_millis(5u);
  const uint8_t no_irq[] = {0u, 0u, 0u, 0u};
  hal_mock_spi_push_rx(1u, no_irq, sizeof(no_irq));
  events = JH_LORA_PROVIDER_EVENT_NONE;
  TEST_ASSERT_EQUAL_INT(HAL_OK, provider->process(&context, &events));
  TEST_ASSERT_BITS(JH_LORA_PROVIDER_EVENT_TIMEOUT,
                   JH_LORA_PROVIDER_EVENT_TIMEOUT, events);

  context = adapter_context();
  configure_context_modem(&context);
  hal_mock_spi_fail_next_write(1u, true);
  TEST_ASSERT_EQUAL_INT(HAL_EBUS,
                        provider->channel_activity_detect_start(&context, 5u));
  TEST_ASSERT_FALSE(hal_mock_gpio_get_state(21u));
  TEST_ASSERT_FALSE(hal_mock_gpio_get_state(22u));
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
  context.state = HAL_LORA_RADIO_STATE_TX;
  context.transmit_started_ms = hal_millis();
  context.transmit_timeout_ms = 2u;
  const uint8_t no_irq[256] = {};
  hal_mock_spi_push_rx(1u, no_irq, sizeof(no_irq));
  TEST_ASSERT_EQUAL_INT(HAL_OK, provider->transmit_start(&context, 2u));
  hal_mock_advance_millis(2u);
  jh_lora_provider_events_t events = JH_LORA_PROVIDER_EVENT_NONE;
  TEST_ASSERT_EQUAL_INT(HAL_OK, provider->process(&context, &events));
  TEST_ASSERT_BITS(JH_LORA_PROVIDER_EVENT_TIMEOUT,
                   JH_LORA_PROVIDER_EVENT_TIMEOUT, events);
  tx_length = hal_mock_spi_get_tx(1u, tx, sizeof(tx));
  const uint8_t write_buffer[] = {0x0Eu, 0x00u, 0xA5u, 0x5Au};
  const uint8_t set_tx[] = {0x83u};
  TEST_ASSERT_TRUE(
      contains_bytes(tx, tx_length, write_buffer, sizeof(write_buffer)));
  TEST_ASSERT_TRUE(contains_bytes(tx, tx_length, set_tx, sizeof(set_tx)));
  TEST_ASSERT_FALSE(hal_mock_gpio_get_state(21u));
  TEST_ASSERT_FALSE(hal_mock_gpio_get_state(22u));
}

void test_provider_process_maps_crc_irq(void) {
  jh_lora_radio_context_t context = adapter_context();
  configure_context_modem(&context);
  const jh_lora_radio_provider_ops_t *provider = jh_sx126x_provider_ops();
  TEST_ASSERT_EQUAL_INT(HAL_OK, provider->configure(&context));
  context.receive_started_ms = hal_millis();
  context.receive_timeout_ms = 50u;
  context.receive_continuous = false;
  context.state = HAL_LORA_RADIO_STATE_RX;
  TEST_ASSERT_EQUAL_INT(HAL_OK, provider->receive_start(&context, 50u, false));
  const uint8_t crc_irq[] = {0u, 0u, 0x00u, 0x40u};
  hal_mock_spi_push_rx(1u, crc_irq, sizeof(crc_irq));
  hal_mock_gpio_inject_level(context.config.hardware.sx126x.dio1_pin, true);
  jh_lora_provider_events_t events = JH_LORA_PROVIDER_EVENT_NONE;
  TEST_ASSERT_EQUAL_INT(HAL_OK, provider->process(&context, &events));
  TEST_ASSERT_BITS(JH_LORA_PROVIDER_EVENT_CRC_ERROR,
                   JH_LORA_PROVIDER_EVENT_CRC_ERROR, events);
  TEST_ASSERT_BITS(JH_LORA_PROVIDER_EVENT_IRQ, JH_LORA_PROVIDER_EVENT_IRQ,
                   events);
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
  RUN_TEST(test_provider_process_maps_crc_irq);
  RUN_TEST(test_provider_reports_capabilities_and_instant_rssi);
  RUN_TEST(test_image_calibration_boundaries_cache_and_explicit_calibration);
  RUN_TEST(test_channel_activity_detection_maps_irq_timeout_and_spi_error);
  return UNITY_END();
}
