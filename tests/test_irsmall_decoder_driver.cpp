#include "utils/unity.h"

#include "hal/hal_gpio.h"
#include "hal/hal_irsmall_decoder.h"
#include "hal/hal_system.h"
#include "hal/impl/.mock/hal_mock.h"

#include <string.h>

#ifndef HAL_ENABLE_IRSMALL_DECODER
#error                                                                         \
    "HAL_ENABLE_IRSMALL_DECODER must be defined for test_irsmall_decoder_driver"
#endif

static const uint8_t IR_PIN = 7u;
static hal_irsmall_decoder_t dev;

typedef struct {
  uint32_t duration_us;
  bool level;
} rc5_event_t;

static const rc5_event_t RC5_TOGGLE0_ADDR5_CMD18[] = {
    {889u, true},  {889u, false},  {1778u, true}, {889u, false},
    {889u, true},  {889u, false},  {889u, true},  {1778u, false},
    {1778u, true}, {1778u, false}, {1778u, true}, {1778u, false},
    {1778u, true}, {889u, false},  {889u, true},  {1778u, false},
    {1778u, true},
};

static const rc5_event_t RC5_TOGGLE0_ADDR31_CMD69[] = {
    {1778u, true}, {889u, false},  {889u, true},  {1778u, false},
    {889u, true},  {889u, false},  {889u, true},  {889u, false},
    {889u, true},  {889u, false},  {889u, true},  {889u, false},
    {1778u, true}, {889u, false},  {889u, true},  {889u, false},
    {889u, true},  {1778u, false}, {1778u, true}, {1778u, false},
};

static void fire_after(uint32_t us) {
  hal_mock_advance_micros(us);
  hal_mock_gpio_fire_interrupt(IR_PIN);
}

static void fire_rc5_after(uint32_t us, bool level) {
  hal_mock_advance_micros(us);
  hal_mock_gpio_inject_level(IR_PIN, level);
  hal_mock_gpio_fire_interrupt(IR_PIN);
}

static void send_rc5_events(const rc5_event_t *events, size_t count) {
  for (size_t i = 0u; i < count; ++i) {
    fire_rc5_after(events[i].duration_us, events[i].level);
  }
}

static void send_rc5_after_gap(const rc5_event_t *events, size_t count) {
  fire_rc5_after(50000u, !events[0].level);
  send_rc5_events(events, count);
}

static void send_byte_lsb(uint8_t value, uint32_t zero_us, uint32_t one_us) {
  for (uint8_t bit = 0u; bit < 8u; ++bit) {
    fire_after((value & (uint8_t)(1u << bit)) ? one_us : zero_us);
  }
}

static void send_bits_lsb(uint32_t value, uint8_t bits, uint32_t zero_us,
                          uint32_t one_us) {
  for (uint8_t bit = 0u; bit < bits; ++bit) {
    fire_after((value & (1u << bit)) ? one_us : zero_us);
  }
}

static void init_decoder(hal_irsmall_protocol_t protocol) {
  hal_irsmall_decoder_config_t cfg =
      hal_irsmall_decoder_default_config(IR_PIN, protocol);
  cfg.irq_priority = HAL_IRQ_PRIORITY_HIGH;
  TEST_ASSERT_TRUE(hal_irsmall_decoder_init(&dev, &cfg));
  TEST_ASSERT_TRUE(dev.initialized);
  TEST_ASSERT_TRUE(dev.enabled);
  TEST_ASSERT_NOT_NULL(dev.mutex);
}

static void send_nec_frame(uint16_t addr, uint8_t cmd, bool extended) {
  fire_after(extended ? 28000u : 35000u);
  fire_after(5062u);
  send_byte_lsb((uint8_t)addr, 1125u, 2250u);
  send_byte_lsb(extended ? (uint8_t)(addr >> 8u) : (uint8_t)~addr, 1125u,
                2250u);
  send_byte_lsb(cmd, 1125u, 2250u);
  send_byte_lsb((uint8_t)~cmd, 1125u, 2250u);
}

static void send_sirc_code(uint8_t cmd, uint8_t addr, uint8_t ext,
                           uint8_t bits) {
  send_bits_lsb(cmd, 7u, 1200u, 1800u);
  send_bits_lsb(addr, (bits == 15u) ? 8u : 5u, 1200u, 1800u);
  if (bits == 20u) {
    send_bits_lsb(ext, 8u, 1200u, 1800u);
  }
}

static void send_samsung_old_frame(uint16_t addr, uint8_t cmd) {
  fire_after(40000u);
  fire_after(9000u);
  send_bits_lsb(addr, 12u, 1125u, 2550u);
  send_byte_lsb(cmd, 1125u, 2550u);
}

static void send_samsung32_frame(uint8_t addr, uint8_t cmd) {
  fire_after(40000u);
  fire_after(9000u);
  send_byte_lsb(addr, 1125u, 2550u);
  send_byte_lsb(addr, 1125u, 2550u);
  send_byte_lsb(cmd, 1125u, 2550u);
  send_byte_lsb((uint8_t)~cmd, 1125u, 2550u);
}

void setUp(void) {
  memset(&dev, 0, sizeof(dev));
  hal_mock_set_micros(0u);
  hal_mock_gpio_trace_reset();
  hal_mock_mutex_stats_reset();
  hal_mock_critical_section_reset();
}

void tearDown(void) {
  hal_irsmall_decoder_deinit(&dev);
  hal_gpio_detach_interrupt(IR_PIN);
}

void test_default_config_and_protocol_helpers(void) {
  const hal_irsmall_decoder_config_t cfg =
      hal_irsmall_decoder_default_config(12u, HAL_IRSMALL_PROTOCOL_NEC);

  TEST_ASSERT_EQUAL(HAL_IRSMALL_PROTOCOL_NEC, cfg.protocol);
  TEST_ASSERT_EQUAL_UINT8(12u, cfg.input_pin);
  TEST_ASSERT_TRUE(cfg.timeout_enabled);
  TEST_ASSERT_EQUAL(HAL_IRQ_PRIORITY_DEFAULT, cfg.irq_priority);
  TEST_ASSERT_EQUAL_UINT32(
      126226u, hal_irsmall_decoder_timeout_us(HAL_IRSMALL_PROTOCOL_NEC));
  TEST_ASSERT_EQUAL(HAL_GPIO_IRQ_FALLING, hal_irsmall_decoder_irq_mode(
                                              HAL_IRSMALL_PROTOCOL_SAMSUNG32));
  TEST_ASSERT_EQUAL(HAL_GPIO_IRQ_CHANGE,
                    hal_irsmall_decoder_irq_mode(HAL_IRSMALL_PROTOCOL_RC5));
}

void test_init_configures_pullup_interrupt_and_priority(void) {
  init_decoder(HAL_IRSMALL_PROTOCOL_NEC);

  TEST_ASSERT_EQUAL(HAL_GPIO_INPUT_PULLUP, hal_mock_gpio_get_mode(IR_PIN));
  TEST_ASSERT_EQUAL(HAL_IRQ_PRIORITY_HIGH, hal_mock_gpio_get_irq_priority());
}

void test_nec_decodes_address_command_and_repeat_hold(void) {
  hal_irsmall_decoder_data_t data;

  init_decoder(HAL_IRSMALL_PROTOCOL_NEC);
  send_nec_frame(0x12u, 0x34u, false);

  TEST_ASSERT_TRUE(hal_irsmall_decoder_data_available(&dev, &data));
  TEST_ASSERT_EQUAL(HAL_IRSMALL_PROTOCOL_NEC, data.protocol);
  TEST_ASSERT_EQUAL_UINT16(0x12u, data.addr);
  TEST_ASSERT_EQUAL_UINT8(0x34u, data.cmd);
  TEST_ASSERT_FALSE(data.key_held);
  TEST_ASSERT_EQUAL_UINT8(32u, data.bits);
  TEST_ASSERT_FALSE(hal_irsmall_decoder_data_available(&dev, &data));

  for (uint8_t i = 0u; i < 3u; ++i) {
    fire_after(49000u);
    fire_after(2812u);
  }

  TEST_ASSERT_TRUE(hal_irsmall_decoder_data_available(&dev, &data));
  TEST_ASSERT_EQUAL_UINT16(0x12u, data.addr);
  TEST_ASSERT_EQUAL_UINT8(0x34u, data.cmd);
  TEST_ASSERT_TRUE(data.key_held);
}

void test_necx_decodes_16_bit_address_without_aliasing(void) {
  hal_irsmall_decoder_data_t data;

  init_decoder(HAL_IRSMALL_PROTOCOL_NECX);
  send_nec_frame(0xABCDu, 0x21u, true);

  TEST_ASSERT_TRUE(hal_irsmall_decoder_data_available(&dev, &data));
  TEST_ASSERT_EQUAL(HAL_IRSMALL_PROTOCOL_NECX, data.protocol);
  TEST_ASSERT_EQUAL_UINT16(0xABCDu, data.addr);
  TEST_ASSERT_EQUAL_UINT8(0x21u, data.cmd);
}

void test_sirc12_basic_decodes_single_frame(void) {
  hal_irsmall_decoder_data_t data;

  init_decoder(HAL_IRSMALL_PROTOCOL_SIRC12);
  fire_after(17000u);
  send_sirc_code(0x15u, 0x0Au, 0u, 12u);

  TEST_ASSERT_TRUE(hal_irsmall_decoder_data_available(&dev, &data));
  TEST_ASSERT_EQUAL(HAL_IRSMALL_PROTOCOL_SIRC12, data.protocol);
  TEST_ASSERT_EQUAL_UINT16(0x0Au, data.addr);
  TEST_ASSERT_EQUAL_UINT8(0x15u, data.cmd);
  TEST_ASSERT_EQUAL_UINT8(0u, data.ext);
  TEST_ASSERT_EQUAL_UINT8(12u, data.bits);
}

void test_sirc20_basic_decodes_extension_address_and_command(void) {
  hal_irsmall_decoder_data_t data;

  init_decoder(HAL_IRSMALL_PROTOCOL_SIRC20);
  fire_after(6000u);
  send_sirc_code(0x2Au, 0x13u, 0x5Cu, 20u);

  TEST_ASSERT_TRUE(hal_irsmall_decoder_data_available(&dev, &data));
  TEST_ASSERT_EQUAL(HAL_IRSMALL_PROTOCOL_SIRC20, data.protocol);
  TEST_ASSERT_EQUAL_UINT16(0x13u, data.addr);
  TEST_ASSERT_EQUAL_UINT8(0x2Au, data.cmd);
  TEST_ASSERT_EQUAL_UINT8(0x5Cu, data.ext);
  TEST_ASSERT_EQUAL_UINT8(20u, data.bits);
}

void test_sirc_multi_requires_three_matching_frames(void) {
  hal_irsmall_decoder_data_t data;

  init_decoder(HAL_IRSMALL_PROTOCOL_SIRC);
  fire_after(10000u);
  send_sirc_code(0x35u, 0x12u, 0u, 12u);
  fire_after(10000u);
  send_sirc_code(0x35u, 0x12u, 0u, 12u);
  TEST_ASSERT_FALSE(hal_irsmall_decoder_data_available(&dev, &data));
  fire_after(10000u);
  send_sirc_code(0x35u, 0x12u, 0u, 12u);

  TEST_ASSERT_TRUE(hal_irsmall_decoder_data_available(&dev, &data));
  TEST_ASSERT_EQUAL(HAL_IRSMALL_PROTOCOL_SIRC, data.protocol);
  TEST_ASSERT_EQUAL_UINT16(0x12u, data.addr);
  TEST_ASSERT_EQUAL_UINT8(0x35u, data.cmd);
  TEST_ASSERT_FALSE(data.key_held);
  TEST_ASSERT_EQUAL_UINT8(12u, data.bits);
}

void test_rc5_decodes_reference_transition_table_frame(void) {
  hal_irsmall_decoder_data_t data;

  init_decoder(HAL_IRSMALL_PROTOCOL_RC5);
  send_rc5_events(RC5_TOGGLE0_ADDR5_CMD18,
                  sizeof(RC5_TOGGLE0_ADDR5_CMD18) /
                      sizeof(RC5_TOGGLE0_ADDR5_CMD18[0]));

  TEST_ASSERT_TRUE(hal_irsmall_decoder_data_available(&dev, &data));
  TEST_ASSERT_EQUAL(HAL_IRSMALL_PROTOCOL_RC5, data.protocol);
  TEST_ASSERT_EQUAL_UINT16(0x05u, data.addr);
  TEST_ASSERT_EQUAL_UINT8(0x12u, data.cmd);
  TEST_ASSERT_FALSE(data.key_held);
  TEST_ASSERT_EQUAL_UINT8(14u, data.bits);
}

void test_rc5_decodes_extended_command_bit(void) {
  hal_irsmall_decoder_data_t data;

  init_decoder(HAL_IRSMALL_PROTOCOL_RC5);
  send_rc5_events(RC5_TOGGLE0_ADDR31_CMD69,
                  sizeof(RC5_TOGGLE0_ADDR31_CMD69) /
                      sizeof(RC5_TOGGLE0_ADDR31_CMD69[0]));

  TEST_ASSERT_TRUE(hal_irsmall_decoder_data_available(&dev, &data));
  TEST_ASSERT_EQUAL_UINT16(0x1Fu, data.addr);
  TEST_ASSERT_EQUAL_UINT8(0x45u, data.cmd);
  TEST_ASSERT_FALSE(data.key_held);
}

void test_rc5_same_toggle_repeat_reports_key_held(void) {
  hal_irsmall_decoder_data_t data;

  init_decoder(HAL_IRSMALL_PROTOCOL_RC5);
  send_rc5_events(RC5_TOGGLE0_ADDR5_CMD18,
                  sizeof(RC5_TOGGLE0_ADDR5_CMD18) /
                      sizeof(RC5_TOGGLE0_ADDR5_CMD18[0]));

  TEST_ASSERT_TRUE(hal_irsmall_decoder_data_available(&dev, &data));
  TEST_ASSERT_FALSE(data.key_held);

  for (uint8_t i = 0u; i < 3u; ++i) {
    send_rc5_after_gap(RC5_TOGGLE0_ADDR5_CMD18,
                       sizeof(RC5_TOGGLE0_ADDR5_CMD18) /
                           sizeof(RC5_TOGGLE0_ADDR5_CMD18[0]));
  }

  TEST_ASSERT_TRUE(hal_irsmall_decoder_data_available(&dev, &data));
  TEST_ASSERT_EQUAL_UINT16(0x05u, data.addr);
  TEST_ASSERT_EQUAL_UINT8(0x12u, data.cmd);
  TEST_ASSERT_TRUE(data.key_held);
}

void test_samsung_old_decodes_20_bit_frame(void) {
  hal_irsmall_decoder_data_t data;

  init_decoder(HAL_IRSMALL_PROTOCOL_SAMSUNG);
  send_samsung_old_frame(0x0A5Bu, 0x66u);

  TEST_ASSERT_TRUE(hal_irsmall_decoder_data_available(&dev, &data));
  TEST_ASSERT_EQUAL(HAL_IRSMALL_PROTOCOL_SAMSUNG, data.protocol);
  TEST_ASSERT_EQUAL_UINT16(0x0A5Bu, data.addr);
  TEST_ASSERT_EQUAL_UINT8(0x66u, data.cmd);
  TEST_ASSERT_FALSE(data.key_held);
  TEST_ASSERT_EQUAL_UINT8(20u, data.bits);
}

void test_samsung32_decodes_repeated_address_and_command_complement(void) {
  hal_irsmall_decoder_data_t data;

  init_decoder(HAL_IRSMALL_PROTOCOL_SAMSUNG32);
  send_samsung32_frame(0x4Du, 0xB2u);

  TEST_ASSERT_TRUE(hal_irsmall_decoder_data_available(&dev, &data));
  TEST_ASSERT_EQUAL(HAL_IRSMALL_PROTOCOL_SAMSUNG32, data.protocol);
  TEST_ASSERT_EQUAL_UINT16(0x4Du, data.addr);
  TEST_ASSERT_EQUAL_UINT8(0xB2u, data.cmd);
  TEST_ASSERT_FALSE(data.key_held);
  TEST_ASSERT_EQUAL_UINT8(32u, data.bits);
}

void test_disable_detaches_interrupt_and_enable_resets_state(void) {
  hal_irsmall_decoder_data_t data;

  init_decoder(HAL_IRSMALL_PROTOCOL_NEC);
  hal_irsmall_decoder_disable(&dev);
  send_nec_frame(0x12u, 0x34u, false);
  TEST_ASSERT_FALSE(hal_irsmall_decoder_data_available(&dev, &data));

  hal_irsmall_decoder_enable(&dev);
  send_nec_frame(0x22u, 0x44u, false);
  TEST_ASSERT_TRUE(hal_irsmall_decoder_data_available(&dev, &data));
  TEST_ASSERT_EQUAL_UINT16(0x22u, data.addr);
  TEST_ASSERT_EQUAL_UINT8(0x44u, data.cmd);
}

void test_timeout_resets_partial_frame(void) {
  hal_irsmall_decoder_data_t data;

  init_decoder(HAL_IRSMALL_PROTOCOL_NEC);
  fire_after(35000u);
  fire_after(5062u);
  TEST_ASSERT_EQUAL_UINT8(2u, dev.state);

  hal_mock_advance_micros(
      hal_irsmall_decoder_timeout_us(HAL_IRSMALL_PROTOCOL_NEC) + 1u);
  TEST_ASSERT_FALSE(hal_irsmall_decoder_data_available(&dev, &data));
  TEST_ASSERT_EQUAL_UINT8(0u, dev.state);
}

int main(void) {
  UNITY_BEGIN();
  RUN_TEST(test_default_config_and_protocol_helpers);
  RUN_TEST(test_init_configures_pullup_interrupt_and_priority);
  RUN_TEST(test_nec_decodes_address_command_and_repeat_hold);
  RUN_TEST(test_necx_decodes_16_bit_address_without_aliasing);
  RUN_TEST(test_sirc12_basic_decodes_single_frame);
  RUN_TEST(test_sirc20_basic_decodes_extension_address_and_command);
  RUN_TEST(test_sirc_multi_requires_three_matching_frames);
  RUN_TEST(test_rc5_decodes_reference_transition_table_frame);
  RUN_TEST(test_rc5_decodes_extended_command_bit);
  RUN_TEST(test_rc5_same_toggle_repeat_reports_key_held);
  RUN_TEST(test_samsung_old_decodes_20_bit_frame);
  RUN_TEST(test_samsung32_decodes_repeated_address_and_command_complement);
  RUN_TEST(test_disable_detaches_interrupt_and_enable_resets_state);
  RUN_TEST(test_timeout_resets_partial_frame);
  return UNITY_END();
}
