/* Select the periodic-timer branch of the portable example. */
#undef HAL_ENABLE_PULSE_CAPTURE
#include "../examples/01_core_runtime/app.c"
#include "support/serial_mock_c.h"
#include "utils/unity.h"
#include <string.h>

void setUp(void) {}
void tearDown(void) {
  if (s_periodic_timer != NULL) {
    (void)hal_timer_destroy(s_periodic_timer);
    s_periodic_timer = NULL;
  }
  hal_soft_timer_destroy(s_blink_timer);
  hal_soft_timer_destroy(s_pid_timer);
  hal_pid_controller_destroy(s_pid);
  s_blink_timer = NULL;
  s_pid_timer = NULL;
  s_pid = NULL;
  for (unsigned i = 0; i < 32U; ++i) {
    size_t written;
    (void)hal_serial_buffered_poll(1024U, &written);
  }
  (void)hal_serial_buffered_init(NULL, 0U);
}
static void test_disabled_asserts_keep_deferred_diagnostics_initialized(void) {
  app_start();
  size_t pending = 0U;
  uint32_t dropped = 0U;
  TEST_ASSERT_EQUAL(HAL_OK, hal_serial_buffered_status(&pending, &dropped));
  for (unsigned i = 0; i < 16U; ++i)
    app_task0();
  jh_test_serial_reset();
  periodic_tick(s_periodic_timer, NULL);
  app_task0();
  TEST_ASSERT_NOT_NULL(strstr(jh_test_serial_debug(), "timer event"));
}
int main(void) {
  UNITY_BEGIN();
  RUN_TEST(test_disabled_asserts_keep_deferred_diagnostics_initialized);
  return UNITY_END();
}
