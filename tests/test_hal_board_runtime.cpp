#include "utils/unity.h"

#include "hal/hal_board.h"
#include "hal/hal_sync.h"
#include "hal/impl/shared/jh_board_runtime.h"

struct hal_mutex_impl_t {
  bool locked;
};

static hal_mutex_impl_t s_mutex{};

hal_mutex_t hal_mutex_create(void) { return &s_mutex; }

void hal_mutex_lock(hal_mutex_t mutex) {
  TEST_ASSERT_NOT_NULL(mutex);
  TEST_ASSERT_FALSE(mutex->locked);
  mutex->locked = true;
}

void hal_mutex_unlock(hal_mutex_t mutex) {
  TEST_ASSERT_NOT_NULL(mutex);
  TEST_ASSERT_TRUE(mutex->locked);
  mutex->locked = false;
}

void hal_mutex_destroy(hal_mutex_t) {}

void hal_critical_section_enter(void) {}

void hal_critical_section_exit(void) {}

void setUp(void) {
  TEST_ASSERT_EQUAL_INT(HAL_OK,
                        jh_board_runtime_set_inactive(
                            HAL_BOARD_CAP_USB_DEVICE | HAL_BOARD_CAP_CYW43 |
                            HAL_BOARD_CAP_BLUETOOTH_CONTROLLER));
}

void tearDown(void) {}

void test_board_info_reports_profile_and_declared_capabilities(void) {
  hal_board_info_t info{};
  TEST_ASSERT_EQUAL_INT(HAL_OK, hal_board_get_info(&info));
  TEST_ASSERT_EQUAL_INT(HAL_BOARD_RP_PICO_W, info.profile);
  TEST_ASSERT_EQUAL_STRING("pico-w", info.name);
  TEST_ASSERT_EQUAL_UINT32(HAL_BOARD_CAP_USB_DEVICE | HAL_BOARD_CAP_CYW43 |
                               HAL_BOARD_CAP_BLUETOOTH_CONTROLLER,
                           info.declared);
  TEST_ASSERT_EQUAL_UINT32(0u, info.available);
  TEST_ASSERT_EQUAL_UINT32(0u, info.failed);
  TEST_ASSERT_EQUAL_INT(HAL_EINVAL, hal_board_get_info(nullptr));
}

void test_capabilities_transition_between_runtime_states(void) {
  hal_board_capability_state_t state = HAL_BOARD_CAP_NOT_PRESENT;

  TEST_ASSERT_EQUAL_INT(
      HAL_OK, hal_board_get_capability_state(HAL_BOARD_CAP_CYW43, &state));
  TEST_ASSERT_EQUAL_INT(HAL_BOARD_CAP_INACTIVE, state);
  TEST_ASSERT_EQUAL_INT(HAL_EUNINIT,
                        hal_board_require_capabilities(HAL_BOARD_CAP_CYW43));

  TEST_ASSERT_EQUAL_INT(HAL_OK,
                        jh_board_runtime_set_available(HAL_BOARD_CAP_CYW43));
  TEST_ASSERT_EQUAL_INT(
      HAL_OK, hal_board_get_capability_state(HAL_BOARD_CAP_CYW43, &state));
  TEST_ASSERT_EQUAL_INT(HAL_BOARD_CAP_AVAILABLE, state);
  TEST_ASSERT_EQUAL_INT(HAL_OK,
                        hal_board_require_capabilities(HAL_BOARD_CAP_CYW43));

  TEST_ASSERT_EQUAL_INT(HAL_OK,
                        jh_board_runtime_set_failed(HAL_BOARD_CAP_CYW43));
  TEST_ASSERT_EQUAL_INT(
      HAL_OK, hal_board_get_capability_state(HAL_BOARD_CAP_CYW43, &state));
  TEST_ASSERT_EQUAL_INT(HAL_BOARD_CAP_FAILED, state);
  TEST_ASSERT_EQUAL_INT(HAL_EHW,
                        hal_board_require_capabilities(HAL_BOARD_CAP_CYW43));
}

void test_composite_capability_requirements_are_consistent(void) {
  const hal_board_capabilities_t required =
      HAL_BOARD_CAP_USB_DEVICE | HAL_BOARD_CAP_CYW43;
  TEST_ASSERT_EQUAL_INT(HAL_OK,
                        jh_board_runtime_set_available(HAL_BOARD_CAP_CYW43));
  TEST_ASSERT_EQUAL_INT(HAL_EUNINIT, hal_board_require_capabilities(required));

  TEST_ASSERT_EQUAL_INT(
      HAL_OK, jh_board_runtime_set_available(HAL_BOARD_CAP_USB_DEVICE));
  TEST_ASSERT_EQUAL_INT(HAL_OK, hal_board_require_capabilities(required));
}

void test_missing_board_hardware_is_reported_as_unsupported(void) {
  hal_board_capability_state_t state = HAL_BOARD_CAP_INACTIVE;
  TEST_ASSERT_EQUAL_INT(HAL_OK,
                        hal_board_get_capability_state(
                            HAL_BOARD_CAP_EXTERNAL_RADIO_FRONTEND, &state));
  TEST_ASSERT_EQUAL_INT(HAL_BOARD_CAP_NOT_PRESENT, state);
  TEST_ASSERT_EQUAL_INT(
      HAL_EUNSUPPORTED,
      hal_board_require_capabilities(HAL_BOARD_CAP_EXTERNAL_RADIO_FRONTEND));
  TEST_ASSERT_EQUAL_INT(
      HAL_EUNSUPPORTED,
      jh_board_runtime_set_available(HAL_BOARD_CAP_EXTERNAL_RADIO_FRONTEND));

  TEST_ASSERT_EQUAL_INT(HAL_OK, hal_board_get_capability_state(
                                    HAL_BOARD_CAP_SX1262_RADIO, &state));
  TEST_ASSERT_EQUAL_INT(HAL_BOARD_CAP_NOT_PRESENT, state);
  TEST_ASSERT_EQUAL_INT(HAL_EUNSUPPORTED, hal_board_require_capabilities(
                                              HAL_BOARD_CAP_SX1262_RADIO));
}

void test_invalid_capability_queries_are_rejected(void) {
  hal_board_capability_state_t state = HAL_BOARD_CAP_NOT_PRESENT;
  TEST_ASSERT_EQUAL_INT(HAL_EINVAL, hal_board_get_capability_state(0u, &state));
  TEST_ASSERT_EQUAL_INT(
      HAL_EINVAL, hal_board_get_capability_state(
                      HAL_BOARD_CAP_USB_DEVICE | HAL_BOARD_CAP_CYW43, &state));
  TEST_ASSERT_EQUAL_INT(
      HAL_EINVAL, hal_board_get_capability_state(HAL_BOARD_CAP_CYW43, nullptr));
  TEST_ASSERT_EQUAL_INT(HAL_EINVAL, hal_board_require_capabilities(0u));
  TEST_ASSERT_EQUAL_INT(HAL_EINVAL,
                        hal_board_require_capabilities(UINT32_C(1) << 31));
}

int main(void) {
  UNITY_BEGIN();
  RUN_TEST(test_board_info_reports_profile_and_declared_capabilities);
  RUN_TEST(test_capabilities_transition_between_runtime_states);
  RUN_TEST(test_composite_capability_requirements_are_consistent);
  RUN_TEST(test_missing_board_hardware_is_reported_as_unsupported);
  RUN_TEST(test_invalid_capability_queries_are_rejected);
  return UNITY_END();
}
