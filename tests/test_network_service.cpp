#include "hal/network/jh_network_service.h"
#include "utils/unity.h"

#include <string.h>

typedef struct {
  jh_network_service_t *service;
  jh_network_context_owner_t owner;
  hal_status_t enter_status;
  hal_status_t service_status;
  bool ipv4_ready;
  bool reenter_from_service;
  bool stop_during_enter;
  bool stop_during_service;
  unsigned lock_depth;
  unsigned stack_enter_count;
  unsigned stack_leave_count;
  unsigned service_count;
  unsigned reentry_count;
  hal_status_t stop_status;
} fake_port_state_t;

static fake_port_state_t s_state;
static jh_network_service_port_t s_port;
static jh_network_service_t s_service;

static void fake_lock(void *context) {
  fake_port_state_t *state = static_cast<fake_port_state_t *>(context);
  ++state->lock_depth;
}

static void fake_unlock(void *context) {
  fake_port_state_t *state = static_cast<fake_port_state_t *>(context);
  TEST_ASSERT_GREATER_THAN_UINT(0u, state->lock_depth);
  --state->lock_depth;
}

static jh_network_context_owner_t fake_owner(void *context) {
  return static_cast<fake_port_state_t *>(context)->owner;
}

static hal_status_t fake_stack_enter(void *context) {
  fake_port_state_t *state = static_cast<fake_port_state_t *>(context);
  ++state->stack_enter_count;
  if (state->stop_during_enter) {
    state->stop_status = jh_network_service_stop(state->service);
  }
  return state->enter_status;
}

static void fake_stack_leave(void *context) {
  ++static_cast<fake_port_state_t *>(context)->stack_leave_count;
}

static hal_status_t fake_service(void *context) {
  fake_port_state_t *state = static_cast<fake_port_state_t *>(context);
  ++state->service_count;
  if (state->stop_during_service) {
    state->stop_status = jh_network_service_stop(state->service);
  }
  if (state->reenter_from_service) {
    TEST_ASSERT_EQUAL_INT(HAL_OK,
                          jh_network_service_enter(state->service, false));
    ++state->reentry_count;
    TEST_ASSERT_EQUAL_INT(HAL_OK, jh_network_service_leave(state->service));
  }
  return state->service_status;
}

static bool fake_ipv4_ready(void *context) {
  return static_cast<fake_port_state_t *>(context)->ipv4_ready;
}

void setUp(void) {
  memset(&s_state, 0, sizeof(s_state));
  memset(&s_service, 0, sizeof(s_service));
  s_state.service = &s_service;
  s_state.owner = 1u;
  s_state.enter_status = HAL_OK;
  s_state.service_status = HAL_OK;
  s_state.ipv4_ready = true;
  s_state.stop_status = HAL_ESTATE;
  s_port = {
      &s_state,         fake_lock,        fake_unlock,  fake_owner,
      fake_stack_enter, fake_stack_leave, fake_service, fake_ipv4_ready,
  };
  TEST_ASSERT_EQUAL_INT(HAL_OK, jh_network_service_init(&s_service, &s_port));
  TEST_ASSERT_EQUAL_INT(HAL_OK, jh_network_service_start(&s_service));
}

void tearDown(void) { TEST_ASSERT_EQUAL_UINT(0u, s_state.lock_depth); }

void test_nested_enter_is_balanced_and_enters_platform_once(void) {
  TEST_ASSERT_EQUAL_INT(HAL_OK, jh_network_service_enter(&s_service, false));
  TEST_ASSERT_EQUAL_INT(HAL_OK, jh_network_service_enter(&s_service, true));
  TEST_ASSERT_EQUAL_UINT(2u, s_service.depth);
  TEST_ASSERT_EQUAL_UINT(1u, s_state.stack_enter_count);
  TEST_ASSERT_EQUAL_UINT(1u, s_state.service_count);

  TEST_ASSERT_EQUAL_INT(HAL_OK, jh_network_service_leave(&s_service));
  TEST_ASSERT_EQUAL_UINT(0u, s_state.stack_leave_count);
  TEST_ASSERT_EQUAL_INT(HAL_OK, jh_network_service_leave(&s_service));
  TEST_ASSERT_EQUAL_UINT(1u, s_state.stack_leave_count);
  TEST_ASSERT_TRUE(jh_network_service_is_quiescent(&s_service));
}

void test_service_callback_can_reenter_without_second_platform_lock(void) {
  s_state.reenter_from_service = true;
  TEST_ASSERT_EQUAL_INT(HAL_OK, jh_network_service_enter(&s_service, false));
  TEST_ASSERT_EQUAL_UINT(1u, s_state.reentry_count);
  TEST_ASSERT_EQUAL_UINT(1u, s_state.stack_enter_count);
  TEST_ASSERT_EQUAL_UINT(1u, s_service.depth);
  TEST_ASSERT_EQUAL_INT(HAL_OK, jh_network_service_leave(&s_service));
  TEST_ASSERT_EQUAL_UINT(1u, s_state.stack_leave_count);
}

void test_ipv4_precondition_failure_unwinds_context(void) {
  s_state.ipv4_ready = false;
  TEST_ASSERT_EQUAL_INT(HAL_ESTATE, jh_network_service_enter(&s_service, true));
  TEST_ASSERT_EQUAL_UINT(1u, s_state.stack_enter_count);
  TEST_ASSERT_EQUAL_UINT(1u, s_state.stack_leave_count);
  TEST_ASSERT_EQUAL_UINT(0u, s_service.depth);
}

void test_service_failure_is_propagated_and_unwinds_context(void) {
  s_state.service_status = HAL_EIO;
  TEST_ASSERT_EQUAL_INT(HAL_EIO, jh_network_service_enter(&s_service, false));
  TEST_ASSERT_EQUAL_UINT(1u, s_state.stack_enter_count);
  TEST_ASSERT_EQUAL_UINT(1u, s_state.stack_leave_count);
  TEST_ASSERT_EQUAL_UINT(0u, s_service.depth);
}

void test_stop_racing_with_enter_unwinds_acquired_platform_context(void) {
  s_state.stop_during_enter = true;
  TEST_ASSERT_EQUAL_INT(HAL_ESTATE,
                        jh_network_service_enter(&s_service, false));
  TEST_ASSERT_EQUAL_INT(HAL_OK, s_state.stop_status);
  TEST_ASSERT_EQUAL_UINT(1u, s_state.stack_enter_count);
  TEST_ASSERT_EQUAL_UINT(1u, s_state.stack_leave_count);
  TEST_ASSERT_TRUE(jh_network_service_is_quiescent(&s_service));
  TEST_ASSERT_EQUAL_INT(HAL_OK, jh_network_service_start(&s_service));
  s_state.stop_during_enter = false;
  TEST_ASSERT_EQUAL_INT(HAL_OK, jh_network_service_enter(&s_service, false));
  TEST_ASSERT_EQUAL_INT(HAL_OK, jh_network_service_leave(&s_service));
}

void test_stop_from_serviced_callback_unwinds_pending_context(void) {
  s_state.stop_during_service = true;
  TEST_ASSERT_EQUAL_INT(HAL_ESTATE,
                        jh_network_service_enter(&s_service, false));
  TEST_ASSERT_EQUAL_INT(HAL_EBUSY, s_state.stop_status);
  TEST_ASSERT_EQUAL_UINT(1u, s_state.stack_enter_count);
  TEST_ASSERT_EQUAL_UINT(1u, s_state.stack_leave_count);
  TEST_ASSERT_TRUE(jh_network_service_is_quiescent(&s_service));
  TEST_ASSERT_EQUAL_INT(HAL_OK, jh_network_service_start(&s_service));
}

void test_stop_invalidates_pending_operation_and_late_completion(void) {
  jh_network_operation_t old_operation = {};
  TEST_ASSERT_EQUAL_INT(HAL_OK,
                        jh_network_operation_begin(&s_service, &old_operation));
  TEST_ASSERT_EQUAL_UINT(1u, s_service.pending_operations);
  TEST_ASSERT_FALSE(jh_network_service_is_quiescent(&s_service));

  TEST_ASSERT_EQUAL_INT(HAL_OK, jh_network_service_stop(&s_service));
  TEST_ASSERT_EQUAL_UINT(0u, s_service.pending_operations);
  TEST_ASSERT_FALSE(jh_network_operation_complete(&old_operation));
  TEST_ASSERT_EQUAL_INT(HAL_OK, jh_network_service_start(&s_service));

  jh_network_operation_t current_operation = {};
  TEST_ASSERT_EQUAL_INT(
      HAL_OK, jh_network_operation_begin(&s_service, &current_operation));
  TEST_ASSERT_TRUE(jh_network_operation_complete(&current_operation));
  TEST_ASSERT_TRUE(jh_network_service_is_quiescent(&s_service));
}

void test_try_stop_leaves_service_running_while_context_is_active(void) {
  const uint32_t generation = s_service.generation;
  TEST_ASSERT_EQUAL_INT(HAL_OK, jh_network_service_enter(&s_service, false));
  TEST_ASSERT_EQUAL_INT(HAL_EBUSY, jh_network_service_try_stop(&s_service));
  TEST_ASSERT_TRUE(s_service.running);
  TEST_ASSERT_FALSE(s_service.stopping);
  TEST_ASSERT_EQUAL_UINT32(generation, s_service.generation);
  TEST_ASSERT_EQUAL_INT(HAL_OK, jh_network_service_leave(&s_service));
  TEST_ASSERT_EQUAL_INT(HAL_OK, jh_network_service_try_stop(&s_service));
}

void test_cancel_releases_slot_and_owner_mismatch_is_rejected(void) {
  jh_network_operation_t operation = {};
  TEST_ASSERT_EQUAL_INT(HAL_OK,
                        jh_network_operation_begin(&s_service, &operation));
  TEST_ASSERT_TRUE(jh_network_operation_cancel(&operation));
  TEST_ASSERT_FALSE(jh_network_operation_cancel(&operation));

  TEST_ASSERT_EQUAL_INT(HAL_OK, jh_network_service_enter(&s_service, false));
  s_state.owner = 2u;
  TEST_ASSERT_EQUAL_INT(HAL_ESTATE, jh_network_service_leave(&s_service));
  s_state.owner = 1u;
  TEST_ASSERT_EQUAL_INT(HAL_OK, jh_network_service_leave(&s_service));
}

int main(void) {
  UNITY_BEGIN();
  RUN_TEST(test_nested_enter_is_balanced_and_enters_platform_once);
  RUN_TEST(test_service_callback_can_reenter_without_second_platform_lock);
  RUN_TEST(test_ipv4_precondition_failure_unwinds_context);
  RUN_TEST(test_service_failure_is_propagated_and_unwinds_context);
  RUN_TEST(test_stop_racing_with_enter_unwinds_acquired_platform_context);
  RUN_TEST(test_stop_from_serviced_callback_unwinds_pending_context);
  RUN_TEST(test_stop_invalidates_pending_operation_and_late_completion);
  RUN_TEST(test_try_stop_leaves_service_running_while_context_is_active);
  RUN_TEST(test_cancel_releases_slot_and_owner_mismatch_is_rejected);
  return UNITY_END();
}
