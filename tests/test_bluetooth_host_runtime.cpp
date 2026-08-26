#include "hal/bluetooth/jh_bluetooth_host_runtime.h"
#include "utils/unity.h"

#include <cstring>

namespace {

struct fake_state_t {
  jh_bluetooth_controller_service_fn host_service;
  void *host_service_context;
  jh_bluetooth_controller_invalidation_fn host_invalidation;
  void *host_invalidation_context;
  hal_status_t controller_start_status;
  hal_status_t prepare_status;
  hal_status_t power_on_status;
  hal_status_t profile_start_status[JH_BLUETOOTH_HOST_PROFILE_COUNT];
  unsigned stack_depth;
  unsigned controller_starts;
  unsigned controller_stops;
  unsigned controller_services;
  unsigned prepares;
  unsigned power_ons;
  unsigned host_stops;
  unsigned host_services;
  unsigned host_invalidations;
  unsigned profile_starts[JH_BLUETOOTH_HOST_PROFILE_COUNT];
  unsigned profile_stops[JH_BLUETOOTH_HOST_PROFILE_COUNT];
  unsigned profile_services[JH_BLUETOOTH_HOST_PROFILE_COUNT];
  unsigned profile_invalidations[JH_BLUETOOTH_HOST_PROFILE_COUNT];
};

struct profile_context_t {
  fake_state_t *state;
  jh_bluetooth_host_profile_t profile;
};

fake_state_t s_state;
profile_context_t s_profile_contexts[JH_BLUETOOTH_HOST_PROFILE_COUNT];
jh_bluetooth_controller_t s_controller;
jh_bluetooth_host_port_t s_port;
jh_bluetooth_host_runtime_t s_runtime;
jh_bluetooth_host_profile_ops_t s_profile_ops[JH_BLUETOOTH_HOST_PROFILE_COUNT];

hal_status_t
controller_start(void *context, jh_bluetooth_controller_service_fn service,
                 void *service_context,
                 jh_bluetooth_controller_invalidation_fn invalidation,
                 void *invalidation_context) {
  auto *state = static_cast<fake_state_t *>(context);
  TEST_ASSERT_EQUAL_UINT(0u, state->stack_depth);
  ++state->controller_starts;
  if (state->controller_start_status == HAL_OK) {
    state->host_service = service;
    state->host_service_context = service_context;
    state->host_invalidation = invalidation;
    state->host_invalidation_context = invalidation_context;
  }
  return state->controller_start_status;
}

hal_status_t controller_stop(void *context) {
  auto *state = static_cast<fake_state_t *>(context);
  TEST_ASSERT_EQUAL_UINT(0u, state->stack_depth);
  ++state->controller_stops;
  if (state->host_invalidation != nullptr) {
    state->host_invalidation(state->host_invalidation_context, 91u);
  }
  state->host_service = nullptr;
  state->host_service_context = nullptr;
  state->host_invalidation = nullptr;
  state->host_invalidation_context = nullptr;
  return HAL_OK;
}

hal_status_t controller_service(void *context) {
  auto *state = static_cast<fake_state_t *>(context);
  TEST_ASSERT_EQUAL_UINT(0u, state->stack_depth);
  if (state->host_service == nullptr) {
    return HAL_EUNINIT;
  }
  ++state->controller_services;
  ++state->stack_depth;
  const hal_status_t status = state->host_service(state->host_service_context);
  --state->stack_depth;
  return status;
}

hal_status_t host_prepare(void *context) {
  auto *state = static_cast<fake_state_t *>(context);
  TEST_ASSERT_EQUAL_UINT(1u, state->stack_depth);
  ++state->prepares;
  return state->prepare_status;
}

hal_status_t host_power_on(void *context) {
  auto *state = static_cast<fake_state_t *>(context);
  TEST_ASSERT_EQUAL_UINT(1u, state->stack_depth);
  ++state->power_ons;
  return state->power_on_status;
}

void host_stop(void *context) {
  auto *state = static_cast<fake_state_t *>(context);
  TEST_ASSERT_EQUAL_UINT(1u, state->stack_depth);
  ++state->host_stops;
}

hal_status_t host_service(void *context) {
  auto *state = static_cast<fake_state_t *>(context);
  TEST_ASSERT_EQUAL_UINT(1u, state->stack_depth);
  ++state->host_services;
  return HAL_OK;
}

void host_invalidated(void *context, uint32_t generation) {
  (void)generation;
  auto *state = static_cast<fake_state_t *>(context);
  TEST_ASSERT_EQUAL_UINT(0u, state->stack_depth);
  ++state->host_invalidations;
}

hal_status_t profile_start(void *context) {
  auto *profile = static_cast<profile_context_t *>(context);
  TEST_ASSERT_EQUAL_UINT(1u, profile->state->stack_depth);
  ++profile->state->profile_starts[profile->profile];
  return profile->state->profile_start_status[profile->profile];
}

void profile_stop(void *context) {
  auto *profile = static_cast<profile_context_t *>(context);
  TEST_ASSERT_EQUAL_UINT(1u, profile->state->stack_depth);
  ++profile->state->profile_stops[profile->profile];
}

hal_status_t profile_service(void *context) {
  auto *profile = static_cast<profile_context_t *>(context);
  TEST_ASSERT_EQUAL_UINT(1u, profile->state->stack_depth);
  ++profile->state->profile_services[profile->profile];
  return HAL_OK;
}

void profile_invalidated(void *context, uint32_t generation) {
  (void)generation;
  auto *profile = static_cast<profile_context_t *>(context);
  TEST_ASSERT_EQUAL_UINT(0u, profile->state->stack_depth);
  ++profile->state->profile_invalidations[profile->profile];
}

jh_bluetooth_host_snapshot_t snapshot(void) {
  jh_bluetooth_host_snapshot_t value{};
  jh_bluetooth_host_runtime_snapshot(&s_runtime, &value);
  return value;
}

void initialize(void) {
  TEST_ASSERT_EQUAL_INT(HAL_OK, jh_bluetooth_host_runtime_init(
                                    &s_runtime, &s_controller, &s_port));
}

void invalidate_host(uint32_t generation) {
  TEST_ASSERT_NOT_NULL(s_state.host_invalidation);
  s_state.host_invalidation(s_state.host_invalidation_context, generation);
}

} // namespace

void setUp(void) {
  std::memset(&s_state, 0, sizeof(s_state));
  std::memset(&s_runtime, 0, sizeof(s_runtime));
  std::memset(&s_controller, 0, sizeof(s_controller));
  std::memset(&s_port, 0, sizeof(s_port));
  std::memset(s_profile_contexts, 0, sizeof(s_profile_contexts));
  std::memset(s_profile_ops, 0, sizeof(s_profile_ops));
  s_state.controller_start_status = HAL_OK;
  s_state.prepare_status = HAL_OK;
  s_state.power_on_status = HAL_OK;
  s_controller.context = &s_state;
  s_controller.start = controller_start;
  s_controller.stop = controller_stop;
  s_controller.service = controller_service;
  s_port.context = &s_state;
  s_port.prepare = host_prepare;
  s_port.power_on = host_power_on;
  s_port.stop = host_stop;
  s_port.service = host_service;
  s_port.invalidated = host_invalidated;
  for (unsigned index = 0u; index < JH_BLUETOOTH_HOST_PROFILE_COUNT; ++index) {
    s_state.profile_start_status[index] = HAL_OK;
    s_profile_contexts[index] = {
        &s_state, static_cast<jh_bluetooth_host_profile_t>(index)};
    s_profile_ops[index] = {&s_profile_contexts[index], profile_start,
                            profile_stop, profile_service, profile_invalidated};
  }
}

void tearDown(void) { TEST_ASSERT_EQUAL_UINT(0u, s_state.stack_depth); }

void test_init_is_idempotent_only_for_the_same_dependencies(void) {
  initialize();
  TEST_ASSERT_EQUAL_INT(HAL_OK, jh_bluetooth_host_runtime_init(
                                    &s_runtime, &s_controller, &s_port));
  auto other_port = s_port;
  TEST_ASSERT_EQUAL_INT(HAL_EBUSY, jh_bluetooth_host_runtime_init(
                                       &s_runtime, &s_controller, &other_port));
}

void test_profiles_and_duplicate_references_share_one_host(void) {
  initialize();
  jh_bluetooth_host_reference_t ble_first{};
  jh_bluetooth_host_reference_t ble_second{};
  jh_bluetooth_host_reference_t classic{};

  TEST_ASSERT_EQUAL_INT(HAL_OK, jh_bluetooth_host_runtime_acquire(
                                    &s_runtime, JH_BLUETOOTH_HOST_PROFILE_BLE,
                                    &s_profile_ops[0], &ble_first));
  TEST_ASSERT_EQUAL_UINT(1u, s_state.controller_starts);
  TEST_ASSERT_EQUAL_UINT(1u, s_state.prepares);
  TEST_ASSERT_EQUAL_UINT(1u, s_state.power_ons);
  TEST_ASSERT_TRUE(jh_bluetooth_host_reference_is_current(&ble_first));

  TEST_ASSERT_EQUAL_INT(HAL_OK, jh_bluetooth_host_runtime_acquire(
                                    &s_runtime, JH_BLUETOOTH_HOST_PROFILE_BLE,
                                    &s_profile_ops[0], &ble_second));
  TEST_ASSERT_EQUAL_UINT(1u, s_state.controller_services);
  TEST_ASSERT_EQUAL_INT(HAL_OK,
                        jh_bluetooth_host_runtime_acquire(
                            &s_runtime, JH_BLUETOOTH_HOST_PROFILE_CLASSIC_HID,
                            &s_profile_ops[1], &classic));
  TEST_ASSERT_EQUAL_UINT(1u, s_state.prepares);
  TEST_ASSERT_EQUAL_UINT(1u, s_state.power_ons);
  TEST_ASSERT_EQUAL_UINT(1u, s_state.profile_starts[1]);
  TEST_ASSERT_EQUAL_UINT32(3u, snapshot().total_references);

  TEST_ASSERT_EQUAL_INT(HAL_OK, jh_bluetooth_host_runtime_service(&classic));
  TEST_ASSERT_EQUAL_UINT(1u, s_state.host_services);
  TEST_ASSERT_EQUAL_UINT(1u, s_state.profile_services[0]);
  TEST_ASSERT_EQUAL_UINT(1u, s_state.profile_services[1]);

  TEST_ASSERT_EQUAL_INT(HAL_OK, jh_bluetooth_host_runtime_release(&ble_first));
  TEST_ASSERT_EQUAL_UINT(0u, s_state.profile_stops[0]);
  TEST_ASSERT_EQUAL_INT(HAL_OK, jh_bluetooth_host_runtime_release(&ble_second));
  TEST_ASSERT_EQUAL_UINT(1u, s_state.profile_stops[0]);
  TEST_ASSERT_EQUAL_UINT(0u, s_state.host_stops);
  TEST_ASSERT_TRUE(jh_bluetooth_host_reference_is_current(&classic));

  TEST_ASSERT_EQUAL_INT(HAL_OK, jh_bluetooth_host_runtime_release(&classic));
  TEST_ASSERT_EQUAL_UINT(1u, s_state.profile_stops[1]);
  TEST_ASSERT_EQUAL_UINT(1u, s_state.host_stops);
  TEST_ASSERT_EQUAL_UINT(1u, s_state.controller_stops);
  TEST_ASSERT_EQUAL_UINT(0u, s_state.host_invalidations);
  TEST_ASSERT_EQUAL_UINT32(0u, snapshot().total_references);
  TEST_ASSERT_FALSE(snapshot().started);
  TEST_ASSERT_EQUAL_INT(HAL_EINVAL,
                        jh_bluetooth_host_runtime_release(&classic));
}

void test_prepare_failure_rolls_back_the_controller(void) {
  initialize();
  s_state.prepare_status = HAL_EIO;
  jh_bluetooth_host_reference_t reference{};
  TEST_ASSERT_EQUAL_INT(HAL_EIO, jh_bluetooth_host_runtime_acquire(
                                     &s_runtime, JH_BLUETOOTH_HOST_PROFILE_BLE,
                                     &s_profile_ops[0], &reference));
  TEST_ASSERT_FALSE(reference.active);
  TEST_ASSERT_EQUAL_UINT(1u, s_state.prepares);
  TEST_ASSERT_EQUAL_UINT(1u, s_state.host_stops);
  TEST_ASSERT_EQUAL_UINT(1u, s_state.controller_stops);
  TEST_ASSERT_EQUAL_UINT32(0u, snapshot().total_references);
}

void test_profile_and_power_failures_roll_back_in_reverse_order(void) {
  initialize();
  s_state.profile_start_status[0] = HAL_ECONFIG;
  jh_bluetooth_host_reference_t reference{};
  TEST_ASSERT_EQUAL_INT(HAL_ECONFIG,
                        jh_bluetooth_host_runtime_acquire(
                            &s_runtime, JH_BLUETOOTH_HOST_PROFILE_BLE,
                            &s_profile_ops[0], &reference));
  TEST_ASSERT_EQUAL_UINT(1u, s_state.profile_starts[0]);
  TEST_ASSERT_EQUAL_UINT(1u, s_state.profile_stops[0]);
  TEST_ASSERT_EQUAL_UINT(1u, s_state.host_stops);
  TEST_ASSERT_EQUAL_UINT(0u, s_state.power_ons);

  setUp();
  initialize();
  s_state.power_on_status = HAL_EIO;
  TEST_ASSERT_EQUAL_INT(HAL_EIO, jh_bluetooth_host_runtime_acquire(
                                     &s_runtime, JH_BLUETOOTH_HOST_PROFILE_BLE,
                                     &s_profile_ops[0], &reference));
  TEST_ASSERT_EQUAL_UINT(1u, s_state.profile_starts[0]);
  TEST_ASSERT_EQUAL_UINT(1u, s_state.profile_stops[0]);
  TEST_ASSERT_EQUAL_UINT(1u, s_state.host_stops);
  TEST_ASSERT_EQUAL_UINT(1u, s_state.power_ons);
}

void test_failed_second_profile_does_not_reset_the_running_host(void) {
  initialize();
  jh_bluetooth_host_reference_t ble{};
  jh_bluetooth_host_reference_t classic{};
  TEST_ASSERT_EQUAL_INT(HAL_OK, jh_bluetooth_host_runtime_acquire(
                                    &s_runtime, JH_BLUETOOTH_HOST_PROFILE_BLE,
                                    &s_profile_ops[0], &ble));
  s_state.profile_start_status[1] = HAL_ECONFIG;
  TEST_ASSERT_EQUAL_INT(HAL_ECONFIG,
                        jh_bluetooth_host_runtime_acquire(
                            &s_runtime, JH_BLUETOOTH_HOST_PROFILE_CLASSIC_HID,
                            &s_profile_ops[1], &classic));
  TEST_ASSERT_FALSE(classic.active);
  TEST_ASSERT_TRUE(jh_bluetooth_host_reference_is_current(&ble));
  TEST_ASSERT_EQUAL_UINT(1u, s_state.prepares);
  TEST_ASSERT_EQUAL_UINT(0u, s_state.host_stops);
  TEST_ASSERT_EQUAL_UINT32(1u, snapshot().total_references);
  TEST_ASSERT_EQUAL_INT(HAL_OK, jh_bluetooth_host_runtime_service(&ble));
  TEST_ASSERT_EQUAL_INT(HAL_OK, jh_bluetooth_host_runtime_release(&ble));
}

void test_invalidation_makes_handles_stale_and_allows_clean_restart(void) {
  initialize();
  jh_bluetooth_host_reference_t stale{};
  TEST_ASSERT_EQUAL_INT(HAL_OK, jh_bluetooth_host_runtime_acquire(
                                    &s_runtime, JH_BLUETOOTH_HOST_PROFILE_BLE,
                                    &s_profile_ops[0], &stale));
  const uint32_t old_generation = stale.generation;
  invalidate_host(41u);
  TEST_ASSERT_EQUAL_UINT(1u, s_state.host_invalidations);
  TEST_ASSERT_EQUAL_UINT(1u, s_state.profile_invalidations[0]);
  TEST_ASSERT_TRUE(snapshot().failed);
  TEST_ASSERT_FALSE(jh_bluetooth_host_reference_is_current(&stale));
  TEST_ASSERT_EQUAL_INT(HAL_ESTATE, jh_bluetooth_host_runtime_service(&stale));
  TEST_ASSERT_EQUAL_INT(HAL_ESTATE, jh_bluetooth_host_runtime_release(&stale));

  jh_bluetooth_host_reference_t restarted{};
  TEST_ASSERT_EQUAL_INT(HAL_OK, jh_bluetooth_host_runtime_acquire(
                                    &s_runtime, JH_BLUETOOTH_HOST_PROFILE_BLE,
                                    &s_profile_ops[0], &restarted));
  TEST_ASSERT_NOT_EQUAL(old_generation, restarted.generation);
  TEST_ASSERT_TRUE(jh_bluetooth_host_reference_is_current(&restarted));
  TEST_ASSERT_EQUAL_INT(HAL_OK, jh_bluetooth_host_runtime_release(&restarted));
}

int main(void) {
  UNITY_BEGIN();
  RUN_TEST(test_init_is_idempotent_only_for_the_same_dependencies);
  RUN_TEST(test_profiles_and_duplicate_references_share_one_host);
  RUN_TEST(test_prepare_failure_rolls_back_the_controller);
  RUN_TEST(test_profile_and_power_failures_roll_back_in_reverse_order);
  RUN_TEST(test_failed_second_profile_does_not_reset_the_running_host);
  RUN_TEST(test_invalidation_makes_handles_stale_and_allows_clean_restart);
  return UNITY_END();
}
