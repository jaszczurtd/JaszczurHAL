#include "hal/network/cyw43/jh_cyw43_gspi_transport.h"
#include "utils/unity.h"

#include <cstring>

namespace {

struct fake_platform_t {
  hal_status_t initialize_status = HAL_OK;
  hal_status_t transfer_status = HAL_OK;
  hal_status_t rearm_status = HAL_OK;
  bool initialized = false;
  bool powered = false;
  bool data_released = false;
  bool attached = false;
  bool masked = false;
  bool asserted = false;
  uint32_t delay_total_ms = 0u;
  uint32_t mask_calls = 0u;
  uint32_t rearm_calls = 0u;
  const uint8_t *last_tx = nullptr;
  size_t last_tx_length = 0u;
  size_t last_rx_length = 0u;
  uint8_t tx_copy[96]{};
  jh_cyw43_gspi_host_wake_callback_t callback = nullptr;
  void *callback_context = nullptr;
};

fake_platform_t s_fake;
jh_cyw43_gspi_transport_t s_transport;

hal_status_t fake_initialize(void *context) {
  auto *fake = static_cast<fake_platform_t *>(context);
  fake->initialized = fake->initialize_status == HAL_OK;
  return fake->initialize_status;
}

hal_status_t fake_deinitialize(void *context) {
  auto *fake = static_cast<fake_platform_t *>(context);
  fake->initialized = false;
  return HAL_OK;
}

hal_status_t fake_set_power(void *context, bool enabled) {
  auto *fake = static_cast<fake_platform_t *>(context);
  fake->powered = enabled;
  if (!enabled) {
    fake->data_released = false;
  }
  return HAL_OK;
}

hal_status_t fake_release_data(void *context) {
  static_cast<fake_platform_t *>(context)->data_released = true;
  return HAL_OK;
}

hal_status_t fake_transfer(void *context, const uint8_t *tx, size_t tx_length,
                           uint8_t *rx, size_t rx_length) {
  auto *fake = static_cast<fake_platform_t *>(context);
  fake->last_tx = tx;
  fake->last_tx_length = tx_length;
  fake->last_rx_length = rx_length;
  std::memcpy(fake->tx_copy, tx, tx_length);
  if (fake->transfer_status != HAL_OK) {
    return fake->transfer_status;
  }
  if (rx != nullptr) {
    for (size_t index = 0u; index < rx_length; ++index) {
      rx[index] = (uint8_t)(0x80u + index);
    }
    if (tx_length == 4u && rx_length == 4u) {
      rx[0] = 0xBEu;
      rx[1] = 0xADu;
      rx[2] = 0xFEu;
      rx[3] = 0xEDu;
    }
  }
  return HAL_OK;
}

hal_status_t fake_host_wake_attach(void *context,
                                   jh_cyw43_gspi_host_wake_callback_t callback,
                                   void *callback_context) {
  auto *fake = static_cast<fake_platform_t *>(context);
  fake->attached = true;
  fake->callback = callback;
  fake->callback_context = callback_context;
  return HAL_OK;
}

hal_status_t fake_host_wake_detach(void *context) {
  auto *fake = static_cast<fake_platform_t *>(context);
  fake->attached = false;
  fake->callback = nullptr;
  fake->callback_context = nullptr;
  return HAL_OK;
}

void fake_host_wake_mask(void *context) {
  auto *fake = static_cast<fake_platform_t *>(context);
  fake->masked = true;
  ++fake->mask_calls;
}

hal_status_t fake_host_wake_rearm(void *context, bool *asserted) {
  auto *fake = static_cast<fake_platform_t *>(context);
  ++fake->rearm_calls;
  fake->masked = false;
  *asserted = fake->asserted;
  return fake->rearm_status;
}

void fake_delay_ms(void *context, uint32_t delay_ms) {
  static_cast<fake_platform_t *>(context)->delay_total_ms += delay_ms;
}

const jh_cyw43_gspi_platform_ops_t kFakeOps = {
    fake_initialize,       fake_deinitialize,   fake_set_power,
    fake_release_data,     fake_transfer,       fake_host_wake_attach,
    fake_host_wake_detach, fake_host_wake_mask, fake_host_wake_rearm,
    fake_delay_ms,
};

void initialize_transport(void) {
  TEST_ASSERT_EQUAL_INT(HAL_OK, jh_cyw43_gspi_transport_init(
                                    &s_transport, &kFakeOps, &s_fake, 84u));
}

void power_transport(void) {
  initialize_transport();
  TEST_ASSERT_EQUAL_INT(HAL_OK, jh_cyw43_gspi_power_cycle(&s_transport));
}

void raise_host_wake(void) {
  TEST_ASSERT_NOT_NULL(s_fake.callback);
  fake_host_wake_mask(&s_fake);
  s_fake.callback(s_fake.callback_context);
}

} // namespace

void setUp(void) {
  s_fake = fake_platform_t{};
  std::memset(&s_transport, 0, sizeof(s_transport));
}

void tearDown(void) {}

void test_gspi_rejects_incomplete_configuration(void) {
  jh_cyw43_gspi_platform_ops_t incomplete = kFakeOps;
  incomplete.transfer = nullptr;
  TEST_ASSERT_EQUAL_INT(
      HAL_EINVAL,
      jh_cyw43_gspi_transport_init(&s_transport, &incomplete, &s_fake, 84u));
  TEST_ASSERT_EQUAL_INT(
      HAL_ECONFIG,
      jh_cyw43_gspi_transport_init(&s_transport, &kFakeOps, &s_fake, 82u));
}

void test_gspi_power_cycle_preserves_bounded_sequence(void) {
  initialize_transport();
  TEST_ASSERT_FALSE(s_fake.powered);
  TEST_ASSERT_EQUAL_INT(
      HAL_ESTATE, jh_cyw43_gspi_transfer(&s_transport, (const uint8_t *)"1234",
                                         4u, nullptr, 0u));

  TEST_ASSERT_EQUAL_INT(HAL_OK, jh_cyw43_gspi_power_cycle(&s_transport));
  TEST_ASSERT_TRUE(s_fake.powered);
  TEST_ASSERT_TRUE(s_fake.data_released);
  TEST_ASSERT_EQUAL_UINT32(270u, s_fake.delay_total_ms);
  TEST_ASSERT_EQUAL_UINT32(1u, s_transport.stats.cold_power_cycles);
}

void test_gspi_boot_words_match_verified_wire_order(void) {
  power_transport();
  uint32_t value = 0u;
  uint8_t raw[4]{};

  TEST_ASSERT_EQUAL_INT(HAL_OK, jh_cyw43_gspi_boot_read_u32(
                                    &s_transport, 0u, 0x14u, &value, raw));
  const uint8_t expected_command[] = {0xA0u, 0x04u, 0x40u, 0x00u};
  TEST_ASSERT_EQUAL_UINT8_ARRAY(expected_command, s_fake.tx_copy, 4u);
  TEST_ASSERT_EQUAL_HEX32(0xFEEDBEADu, value);
  TEST_ASSERT_EQUAL_HEX8(0xBEu, raw[0]);

  TEST_ASSERT_EQUAL_INT(
      HAL_OK, jh_cyw43_gspi_boot_write_u32(&s_transport, 0u, 0u, 0x000204B3u));
  const uint8_t expected_write[] = {0x00u, 0x04u, 0xC0u, 0x00u,
                                    0x04u, 0xB3u, 0x00u, 0x02u};
  TEST_ASSERT_EQUAL_UINT8_ARRAY(expected_write, s_fake.tx_copy, 8u);
}

void test_gspi_normal_read_and_write_apply_padding_and_alignment(void) {
  power_transport();
  uint8_t output[5]{};
  TEST_ASSERT_EQUAL_INT(HAL_OK, jh_cyw43_gspi_read(&s_transport, 1u, 0x1234u,
                                                   output, sizeof(output)));
  TEST_ASSERT_EQUAL_UINT32(4u, s_fake.last_tx_length);
  TEST_ASSERT_EQUAL_UINT32(24u, s_fake.last_rx_length);
  TEST_ASSERT_EQUAL_HEX8(0x90u, output[0]);
  TEST_ASSERT_EQUAL_HEX8(0x94u, output[4]);

  const uint8_t input[] = {1u, 2u, 3u, 4u, 5u};
  TEST_ASSERT_EQUAL_INT(HAL_OK, jh_cyw43_gspi_write(&s_transport, 2u, 0x55u,
                                                    input, sizeof(input)));
  TEST_ASSERT_EQUAL_UINT32(12u, s_fake.last_tx_length);
  TEST_ASSERT_EQUAL_UINT8_ARRAY(input, &s_fake.tx_copy[4], sizeof(input));
  TEST_ASSERT_EQUAL_HEX8(0u, s_fake.tx_copy[9]);
  TEST_ASSERT_EQUAL_HEX8(0u, s_fake.tx_copy[10]);
  TEST_ASSERT_EQUAL_HEX8(0u, s_fake.tx_copy[11]);
}

void test_gspi_host_wake_is_one_shot_and_rearms_without_lost_level(void) {
  power_transport();
  TEST_ASSERT_EQUAL_INT(HAL_OK, jh_cyw43_gspi_host_wake_attach(&s_transport));
  TEST_ASSERT_TRUE(s_fake.attached);

  raise_host_wake();
  TEST_ASSERT_TRUE(jh_cyw43_gspi_host_wake_pending(&s_transport));
  TEST_ASSERT_TRUE(s_fake.masked);
  TEST_ASSERT_EQUAL_UINT32(1u, s_transport.stats.host_wake_irqs);

  s_fake.asserted = false;
  TEST_ASSERT_EQUAL_INT(HAL_OK, jh_cyw43_gspi_host_wake_clear(&s_transport));
  TEST_ASSERT_FALSE(jh_cyw43_gspi_host_wake_pending(&s_transport));
  TEST_ASSERT_FALSE(s_fake.masked);

  TEST_ASSERT_EQUAL_INT(HAL_OK, jh_cyw43_gspi_host_wake_suspend(&s_transport));
  s_fake.asserted = true;
  TEST_ASSERT_EQUAL_INT(HAL_OK, jh_cyw43_gspi_host_wake_resume(&s_transport));
  TEST_ASSERT_TRUE(jh_cyw43_gspi_host_wake_pending(&s_transport));
  TEST_ASSERT_EQUAL_UINT32(1u, s_transport.stats.host_wake_levels);
}

void test_gspi_host_wake_refresh_samples_unlatched_level(void) {
  power_transport();
  TEST_ASSERT_EQUAL_INT(HAL_OK, jh_cyw43_gspi_host_wake_attach(&s_transport));
  TEST_ASSERT_FALSE(jh_cyw43_gspi_host_wake_pending(&s_transport));

  const uint32_t rearm_before = s_fake.rearm_calls;
  s_fake.asserted = true;
  TEST_ASSERT_EQUAL_INT(HAL_OK, jh_cyw43_gspi_host_wake_refresh(&s_transport));
  TEST_ASSERT_TRUE(jh_cyw43_gspi_host_wake_pending(&s_transport));
  TEST_ASSERT_EQUAL_UINT32(rearm_before + 1u, s_fake.rearm_calls);
  TEST_ASSERT_EQUAL_UINT32(1u, s_transport.stats.host_wake_levels);

  TEST_ASSERT_EQUAL_INT(HAL_OK, jh_cyw43_gspi_host_wake_refresh(&s_transport));
  TEST_ASSERT_EQUAL_UINT32(rearm_before + 1u, s_fake.rearm_calls);
}

void test_gspi_transfer_errors_rearm_and_update_diagnostics(void) {
  power_transport();
  TEST_ASSERT_EQUAL_INT(HAL_OK, jh_cyw43_gspi_host_wake_attach(&s_transport));
  s_fake.transfer_status = HAL_EBUS;
  const uint8_t command[4]{};
  TEST_ASSERT_EQUAL_INT(HAL_EBUS,
                        jh_cyw43_gspi_transfer(&s_transport, command,
                                               sizeof(command), nullptr, 0u));
  TEST_ASSERT_EQUAL_UINT32(1u, s_transport.stats.transfer_errors);
  TEST_ASSERT_EQUAL_UINT32(0u, s_transport.stats.transactions);
  TEST_ASSERT_FALSE(s_fake.masked);

  uint8_t oversized[84]{};
  TEST_ASSERT_EQUAL_INT(HAL_EINVAL,
                        jh_cyw43_gspi_transfer(&s_transport, oversized,
                                               sizeof(oversized), nullptr, 4u));
}

void test_gspi_deinit_detaches_and_clears_state(void) {
  power_transport();
  TEST_ASSERT_EQUAL_INT(HAL_OK, jh_cyw43_gspi_host_wake_attach(&s_transport));
  TEST_ASSERT_EQUAL_INT(HAL_OK, jh_cyw43_gspi_transport_deinit(&s_transport));
  TEST_ASSERT_FALSE(s_fake.attached);
  TEST_ASSERT_FALSE(s_fake.powered);
  TEST_ASSERT_FALSE(s_fake.initialized);
  TEST_ASSERT_FALSE(s_transport.initialized);
}

int main(void) {
  UNITY_BEGIN();
  RUN_TEST(test_gspi_rejects_incomplete_configuration);
  RUN_TEST(test_gspi_power_cycle_preserves_bounded_sequence);
  RUN_TEST(test_gspi_boot_words_match_verified_wire_order);
  RUN_TEST(test_gspi_normal_read_and_write_apply_padding_and_alignment);
  RUN_TEST(test_gspi_host_wake_is_one_shot_and_rearms_without_lost_level);
  RUN_TEST(test_gspi_host_wake_refresh_samples_unlatched_level);
  RUN_TEST(test_gspi_transfer_errors_rearm_and_update_diagnostics);
  RUN_TEST(test_gspi_deinit_detaches_and_clears_state);
  return UNITY_END();
}
