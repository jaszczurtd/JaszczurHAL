#include "hal/impl/shared/drivers/cyw43-driver/jh_cyw43_radio_runtime.h"
#include "utils/unity.h"

#include <string.h>

namespace {

struct fake_state_t {
  jh_network_context_owner_t owner;
  hal_status_t start_status;
  hal_status_t stop_status;
  hal_status_t service_status;
  unsigned lock_depth;
  unsigned stack_depth;
  unsigned starts;
  unsigned stops;
  unsigned synchronized_stops;
  unsigned services;
  unsigned invalidations[JH_CYW43_RADIO_CLIENT_COUNT];
  uint32_t invalidated_generation[JH_CYW43_RADIO_CLIENT_COUNT];
  bool ipv4_ready;
};

fake_state_t s_state;
jh_network_service_port_t s_service_port;
jh_cyw43_radio_runtime_port_t s_runtime_port;
jh_cyw43_radio_runtime_t s_runtime;

void state_lock(void *context) {
  ++static_cast<fake_state_t *>(context)->lock_depth;
}

void state_unlock(void *context) {
  auto *state = static_cast<fake_state_t *>(context);
  TEST_ASSERT_GREATER_THAN_UINT(0u, state->lock_depth);
  --state->lock_depth;
}

jh_network_context_owner_t current_owner(void *context) {
  return static_cast<fake_state_t *>(context)->owner;
}

hal_status_t stack_enter(void *context) {
  auto *state = static_cast<fake_state_t *>(context);
  TEST_ASSERT_EQUAL_UINT(0u, state->stack_depth);
  ++state->stack_depth;
  return HAL_OK;
}

void stack_leave(void *context) {
  auto *state = static_cast<fake_state_t *>(context);
  TEST_ASSERT_EQUAL_UINT(1u, state->stack_depth);
  --state->stack_depth;
}

hal_status_t service(void *context) {
  auto *state = static_cast<fake_state_t *>(context);
  ++state->services;
  return state->service_status;
}

bool ipv4_ready(void *context) {
  return static_cast<fake_state_t *>(context)->ipv4_ready;
}

hal_status_t start(void *context) {
  auto *state = static_cast<fake_state_t *>(context);
  ++state->starts;
  return state->start_status;
}

hal_status_t stop(void *context) {
  auto *state = static_cast<fake_state_t *>(context);
  ++state->stops;
  if (state->stack_depth == 1u) {
    ++state->synchronized_stops;
  }
  return state->stop_status;
}

void invalidated(void *context, uint32_t generation) {
  const auto client = *static_cast<const jh_cyw43_radio_client_t *>(context);
  ++s_state.invalidations[client];
  s_state.invalidated_generation[client] = generation;
}

jh_cyw43_radio_client_t s_wifi_client = JH_CYW43_RADIO_CLIENT_WIFI;
jh_cyw43_radio_client_t s_ble_client = JH_CYW43_RADIO_CLIENT_BLE;

jh_cyw43_radio_runtime_snapshot_t snapshot(void) {
  jh_cyw43_radio_runtime_snapshot_t value{};
  TEST_ASSERT_EQUAL_INT(HAL_OK,
                        jh_cyw43_radio_runtime_snapshot(&s_runtime, &value));
  return value;
}

} // namespace

void setUp(void) {
  memset(&s_state, 0, sizeof(s_state));
  memset(&s_runtime, 0, sizeof(s_runtime));
  s_state.owner = 1u;
  s_state.start_status = HAL_OK;
  s_state.stop_status = HAL_OK;
  s_state.service_status = HAL_OK;
  s_state.ipv4_ready = true;
  s_service_port = {
      &s_state,    state_lock,  state_unlock, current_owner,
      stack_enter, stack_leave, service,      ipv4_ready,
  };
  s_runtime_port = {&s_state, &s_service_port, start, stop};
  TEST_ASSERT_EQUAL_INT(
      HAL_OK, jh_cyw43_radio_runtime_init(&s_runtime, &s_runtime_port));
  TEST_ASSERT_EQUAL_INT(HAL_OK, jh_cyw43_radio_runtime_set_invalidation_handler(
                                    &s_runtime, JH_CYW43_RADIO_CLIENT_WIFI,
                                    invalidated, &s_wifi_client));
  TEST_ASSERT_EQUAL_INT(HAL_OK, jh_cyw43_radio_runtime_set_invalidation_handler(
                                    &s_runtime, JH_CYW43_RADIO_CLIENT_BLE,
                                    invalidated, &s_ble_client));
}

void tearDown(void) {
  TEST_ASSERT_EQUAL_UINT(0u, s_state.lock_depth);
  TEST_ASSERT_EQUAL_UINT(0u, s_state.stack_depth);
}

void test_wifi_and_ble_share_one_driver_lifecycle(void) {
  TEST_ASSERT_EQUAL_INT(HAL_OK, jh_cyw43_radio_runtime_acquire(
                                    &s_runtime, JH_CYW43_RADIO_CLIENT_WIFI));
  const uint32_t generation = snapshot().generation;
  TEST_ASSERT_NOT_EQUAL(0u, generation);
  TEST_ASSERT_EQUAL_INT(HAL_OK, jh_cyw43_radio_runtime_acquire(
                                    &s_runtime, JH_CYW43_RADIO_CLIENT_BLE));
  TEST_ASSERT_EQUAL_UINT(1u, s_state.starts);

  TEST_ASSERT_EQUAL_INT(HAL_OK, jh_cyw43_radio_runtime_release(
                                    &s_runtime, JH_CYW43_RADIO_CLIENT_WIFI));
  const auto after_wifi = snapshot();
  TEST_ASSERT_EQUAL_INT(JH_CYW43_RADIO_STATE_READY, after_wifi.state);
  TEST_ASSERT_EQUAL_UINT16(0u, after_wifi.wifi_references);
  TEST_ASSERT_EQUAL_UINT16(1u, after_wifi.ble_references);
  TEST_ASSERT_EQUAL_UINT(0u, s_state.stops);
  TEST_ASSERT_EQUAL_UINT(1u, s_state.invalidations[JH_CYW43_RADIO_CLIENT_WIFI]);
  TEST_ASSERT_EQUAL_UINT32(
      generation, s_state.invalidated_generation[JH_CYW43_RADIO_CLIENT_WIFI]);

  TEST_ASSERT_EQUAL_INT(HAL_OK, jh_cyw43_radio_runtime_service(
                                    &s_runtime, JH_CYW43_RADIO_CLIENT_BLE));
  TEST_ASSERT_EQUAL_UINT(1u, s_state.services);
  TEST_ASSERT_EQUAL_INT(HAL_OK, jh_cyw43_radio_runtime_release(
                                    &s_runtime, JH_CYW43_RADIO_CLIENT_BLE));
  TEST_ASSERT_EQUAL_INT(JH_CYW43_RADIO_STATE_OFF, snapshot().state);
  TEST_ASSERT_EQUAL_UINT(1u, s_state.stops);
  TEST_ASSERT_EQUAL_UINT(1u, s_state.synchronized_stops);
}

void test_restart_invalidates_both_clients_and_pending_operations(void) {
  TEST_ASSERT_EQUAL_INT(HAL_OK, jh_cyw43_radio_runtime_acquire(
                                    &s_runtime, JH_CYW43_RADIO_CLIENT_WIFI));
  TEST_ASSERT_EQUAL_INT(HAL_OK, jh_cyw43_radio_runtime_acquire(
                                    &s_runtime, JH_CYW43_RADIO_CLIENT_BLE));
  const uint32_t old_generation = snapshot().generation;
  jh_network_operation_t operation{};
  TEST_ASSERT_EQUAL_INT(
      HAL_OK, jh_network_operation_begin(&s_runtime.service, &operation));
  TEST_ASSERT_EQUAL_INT(HAL_OK, jh_cyw43_radio_runtime_restart(&s_runtime));
  TEST_ASSERT_FALSE(jh_network_operation_complete(&operation));
  const auto restarted = snapshot();
  TEST_ASSERT_EQUAL_INT(JH_CYW43_RADIO_STATE_READY, restarted.state);
  TEST_ASSERT_NOT_EQUAL(old_generation, restarted.generation);
  TEST_ASSERT_EQUAL_UINT(2u, s_state.starts);
  TEST_ASSERT_EQUAL_UINT(1u, s_state.stops);
  TEST_ASSERT_EQUAL_UINT(1u, s_state.synchronized_stops);
  TEST_ASSERT_EQUAL_UINT(1u, s_state.invalidations[JH_CYW43_RADIO_CLIENT_WIFI]);
  TEST_ASSERT_EQUAL_UINT(1u, s_state.invalidations[JH_CYW43_RADIO_CLIENT_BLE]);
  TEST_ASSERT_FALSE(jh_cyw43_radio_runtime_generation_is_current(
      &s_runtime, JH_CYW43_RADIO_CLIENT_WIFI, old_generation));
  TEST_ASSERT_TRUE(jh_cyw43_radio_runtime_generation_is_current(
      &s_runtime, JH_CYW43_RADIO_CLIENT_WIFI, restarted.generation));
  TEST_ASSERT_TRUE(jh_cyw43_radio_runtime_generation_is_current(
      &s_runtime, JH_CYW43_RADIO_CLIENT_BLE, restarted.generation));
}

void test_duplicate_references_release_only_the_matching_client(void) {
  TEST_ASSERT_EQUAL_INT(HAL_OK, jh_cyw43_radio_runtime_acquire(
                                    &s_runtime, JH_CYW43_RADIO_CLIENT_WIFI));
  TEST_ASSERT_EQUAL_INT(HAL_OK, jh_cyw43_radio_runtime_acquire(
                                    &s_runtime, JH_CYW43_RADIO_CLIENT_WIFI));
  TEST_ASSERT_EQUAL_INT(HAL_OK, jh_cyw43_radio_runtime_acquire(
                                    &s_runtime, JH_CYW43_RADIO_CLIENT_BLE));
  TEST_ASSERT_EQUAL_INT(HAL_OK, jh_cyw43_radio_runtime_release(
                                    &s_runtime, JH_CYW43_RADIO_CLIENT_WIFI));
  TEST_ASSERT_EQUAL_UINT(0u, s_state.invalidations[JH_CYW43_RADIO_CLIENT_WIFI]);
  TEST_ASSERT_EQUAL_UINT16(1u, snapshot().wifi_references);
  TEST_ASSERT_EQUAL_INT(HAL_OK, jh_cyw43_radio_runtime_release(
                                    &s_runtime, JH_CYW43_RADIO_CLIENT_WIFI));
  TEST_ASSERT_EQUAL_UINT(1u, s_state.invalidations[JH_CYW43_RADIO_CLIENT_WIFI]);
  TEST_ASSERT_EQUAL_UINT16(0u, snapshot().wifi_references);
  TEST_ASSERT_EQUAL_UINT(0u, s_state.stops);
}

void test_service_failure_is_propagated_and_context_is_unwound(void) {
  TEST_ASSERT_EQUAL_INT(HAL_OK, jh_cyw43_radio_runtime_acquire(
                                    &s_runtime, JH_CYW43_RADIO_CLIENT_BLE));
  s_state.service_status = HAL_EIO;
  TEST_ASSERT_EQUAL_INT(HAL_EIO, jh_cyw43_radio_runtime_service(
                                     &s_runtime, JH_CYW43_RADIO_CLIENT_BLE));
  TEST_ASSERT_EQUAL_UINT(1u, s_state.services);
  TEST_ASSERT_EQUAL_UINT(0u, s_runtime.service.depth);
}

void test_restart_while_context_is_active_preserves_running_generation(void) {
  TEST_ASSERT_EQUAL_INT(HAL_OK, jh_cyw43_radio_runtime_acquire(
                                    &s_runtime, JH_CYW43_RADIO_CLIENT_WIFI));
  const uint32_t generation = snapshot().generation;
  TEST_ASSERT_EQUAL_INT(
      HAL_OK, jh_cyw43_radio_runtime_enter(&s_runtime,
                                           JH_CYW43_RADIO_CLIENT_WIFI, false));
  TEST_ASSERT_EQUAL_INT(HAL_EBUSY, jh_cyw43_radio_runtime_restart(&s_runtime));
  const auto busy = snapshot();
  TEST_ASSERT_EQUAL_INT(JH_CYW43_RADIO_STATE_READY, busy.state);
  TEST_ASSERT_EQUAL_UINT32(generation, busy.generation);
  TEST_ASSERT_TRUE(jh_cyw43_radio_runtime_generation_is_current(
      &s_runtime, JH_CYW43_RADIO_CLIENT_WIFI, generation));
  TEST_ASSERT_EQUAL_INT(HAL_OK, jh_cyw43_radio_runtime_leave(&s_runtime));
}

void test_generation_rollover_never_exposes_zero(void) {
  TEST_ASSERT_EQUAL_INT(HAL_OK, jh_cyw43_radio_runtime_acquire(
                                    &s_runtime, JH_CYW43_RADIO_CLIENT_BLE));
  s_runtime.service.generation = UINT32_MAX;
  TEST_ASSERT_EQUAL_INT(HAL_OK, jh_cyw43_radio_runtime_restart(&s_runtime));
  TEST_ASSERT_EQUAL_UINT32(2u, snapshot().generation);
}

void test_start_failure_is_sticky_and_does_not_create_references(void) {
  s_state.start_status = HAL_EIO;
  TEST_ASSERT_EQUAL_INT(HAL_EIO, jh_cyw43_radio_runtime_acquire(
                                     &s_runtime, JH_CYW43_RADIO_CLIENT_WIFI));
  const auto failed = snapshot();
  TEST_ASSERT_EQUAL_INT(JH_CYW43_RADIO_STATE_FAILED, failed.state);
  TEST_ASSERT_EQUAL_UINT16(0u, failed.wifi_references);
  TEST_ASSERT_EQUAL_UINT(1u, s_state.stops);
  TEST_ASSERT_EQUAL_UINT(0u, s_state.synchronized_stops);
  TEST_ASSERT_EQUAL_INT(HAL_EHW, jh_cyw43_radio_runtime_acquire(
                                     &s_runtime, JH_CYW43_RADIO_CLIENT_BLE));
  TEST_ASSERT_EQUAL_INT(HAL_EHW, jh_cyw43_radio_runtime_restart(&s_runtime));
  TEST_ASSERT_EQUAL_INT(HAL_EHW, jh_cyw43_radio_runtime_service(
                                     &s_runtime, JH_CYW43_RADIO_CLIENT_WIFI));
}

int main(void) {
  UNITY_BEGIN();
  RUN_TEST(test_wifi_and_ble_share_one_driver_lifecycle);
  RUN_TEST(test_restart_invalidates_both_clients_and_pending_operations);
  RUN_TEST(test_duplicate_references_release_only_the_matching_client);
  RUN_TEST(test_service_failure_is_propagated_and_context_is_unwound);
  RUN_TEST(test_restart_while_context_is_active_preserves_running_generation);
  RUN_TEST(test_generation_rollover_never_exposes_zero);
  RUN_TEST(test_start_failure_is_sticky_and_does_not_create_references);
  return UNITY_END();
}
