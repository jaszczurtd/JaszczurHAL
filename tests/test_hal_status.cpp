#include "utils/unity.h"

#include "hal/core/hal_status.h"
#include "hal/hal.h"

static hal_status_t return_status(hal_status_t status) { return status; }

void setUp(void) {}

void tearDown(void) {}

void test_hal_status_values_are_stable(void) {
  TEST_ASSERT_EQUAL_INT(0, (int)HAL_NONE);
  TEST_ASSERT_EQUAL_INT(1, (int)HAL_OK);
  TEST_ASSERT_EQUAL_INT(-1, (int)HAL_EINVAL);
  TEST_ASSERT_EQUAL_INT(-2, (int)HAL_EBUSY);
  TEST_ASSERT_EQUAL_INT(-3, (int)HAL_ETIMEOUT);
  TEST_ASSERT_EQUAL_INT(-4, (int)HAL_EIO);
  TEST_ASSERT_EQUAL_INT(-5, (int)HAL_EUNSUPPORTED);
  TEST_ASSERT_EQUAL_INT(-6, (int)HAL_ENOENT);
  TEST_ASSERT_EQUAL_INT(-7, (int)HAL_EAGAIN);
  TEST_ASSERT_EQUAL_INT(-8, (int)HAL_EOVERFLOW);
  TEST_ASSERT_EQUAL_INT(-9, (int)HAL_ENOMEM);
  TEST_ASSERT_EQUAL_INT(-10, (int)HAL_IGNORED);
  TEST_ASSERT_EQUAL_INT(-11, (int)HAL_EEXIST);
  TEST_ASSERT_EQUAL_INT(-12, (int)HAL_EPERM);
  TEST_ASSERT_EQUAL_INT(-13, (int)HAL_EINTERNAL);
  TEST_ASSERT_EQUAL_INT(-14, (int)HAL_ECANCELED);
  TEST_ASSERT_EQUAL_INT(-15, (int)HAL_EPROTO);
  TEST_ASSERT_EQUAL_INT(-16, (int)HAL_EAUTH);
  TEST_ASSERT_EQUAL_INT(-17, (int)HAL_EBUS);
  TEST_ASSERT_EQUAL_INT(-18, (int)HAL_EHW);
  TEST_ASSERT_EQUAL_INT(-19, (int)HAL_ECONFIG);
  TEST_ASSERT_EQUAL_INT(-20, (int)HAL_ESTATE);
  TEST_ASSERT_EQUAL_INT(-21, (int)HAL_EUNINIT);
  TEST_ASSERT_EQUAL_INT(-22, (int)HAL_EDEPRECATED);
  TEST_ASSERT_EQUAL_INT(-23, (int)HAL_EUNKNOWN);
}

void test_hal_status_failure_values_are_negative(void) {
  TEST_ASSERT_TRUE(return_status(HAL_EINVAL) < HAL_OK);
  TEST_ASSERT_TRUE(return_status(HAL_EBUSY) < HAL_OK);
  TEST_ASSERT_TRUE(return_status(HAL_ETIMEOUT) < HAL_OK);
  TEST_ASSERT_TRUE(return_status(HAL_EIO) < HAL_OK);
  TEST_ASSERT_TRUE(return_status(HAL_EUNSUPPORTED) < HAL_OK);
  TEST_ASSERT_TRUE(return_status(HAL_ENOENT) < HAL_OK);
  TEST_ASSERT_TRUE(return_status(HAL_EAGAIN) < HAL_OK);
  TEST_ASSERT_TRUE(return_status(HAL_EOVERFLOW) < HAL_OK);
  TEST_ASSERT_TRUE(return_status(HAL_ENOMEM) < HAL_OK);
  TEST_ASSERT_TRUE(return_status(HAL_IGNORED) < HAL_OK);
  TEST_ASSERT_TRUE(return_status(HAL_EEXIST) < HAL_OK);
  TEST_ASSERT_TRUE(return_status(HAL_EPERM) < HAL_OK);
  TEST_ASSERT_TRUE(return_status(HAL_EINTERNAL) < HAL_OK);
  TEST_ASSERT_TRUE(return_status(HAL_ECANCELED) < HAL_OK);
  TEST_ASSERT_TRUE(return_status(HAL_EPROTO) < HAL_OK);
  TEST_ASSERT_TRUE(return_status(HAL_EAUTH) < HAL_OK);
  TEST_ASSERT_TRUE(return_status(HAL_EBUS) < HAL_OK);
  TEST_ASSERT_TRUE(return_status(HAL_EHW) < HAL_OK);
  TEST_ASSERT_TRUE(return_status(HAL_ECONFIG) < HAL_OK);
  TEST_ASSERT_TRUE(return_status(HAL_ESTATE) < HAL_OK);
  TEST_ASSERT_TRUE(return_status(HAL_EUNINIT) < HAL_OK);
  TEST_ASSERT_TRUE(return_status(HAL_EDEPRECATED) < HAL_OK);
  TEST_ASSERT_TRUE(return_status(HAL_EUNKNOWN) < HAL_OK);
}

void test_hal_status_to_string_returns_symbolic_names(void) {
  TEST_ASSERT_EQUAL_STRING("HAL_NONE", hal_status_to_string(HAL_NONE));
  TEST_ASSERT_EQUAL_STRING("HAL_OK", hal_status_to_string(HAL_OK));
  TEST_ASSERT_EQUAL_STRING("HAL_EINVAL", hal_status_to_string(HAL_EINVAL));
  TEST_ASSERT_EQUAL_STRING("HAL_EBUSY", hal_status_to_string(HAL_EBUSY));
  TEST_ASSERT_EQUAL_STRING("HAL_ETIMEOUT", hal_status_to_string(HAL_ETIMEOUT));
  TEST_ASSERT_EQUAL_STRING("HAL_EIO", hal_status_to_string(HAL_EIO));
  TEST_ASSERT_EQUAL_STRING("HAL_EUNSUPPORTED",
                           hal_status_to_string(HAL_EUNSUPPORTED));
  TEST_ASSERT_EQUAL_STRING("HAL_ENOENT", hal_status_to_string(HAL_ENOENT));
  TEST_ASSERT_EQUAL_STRING("HAL_EAGAIN", hal_status_to_string(HAL_EAGAIN));
  TEST_ASSERT_EQUAL_STRING("HAL_EOVERFLOW",
                           hal_status_to_string(HAL_EOVERFLOW));
  TEST_ASSERT_EQUAL_STRING("HAL_ENOMEM", hal_status_to_string(HAL_ENOMEM));
  TEST_ASSERT_EQUAL_STRING("HAL_IGNORED", hal_status_to_string(HAL_IGNORED));
  TEST_ASSERT_EQUAL_STRING("HAL_EEXIST", hal_status_to_string(HAL_EEXIST));
  TEST_ASSERT_EQUAL_STRING("HAL_EPERM", hal_status_to_string(HAL_EPERM));
  TEST_ASSERT_EQUAL_STRING("HAL_EINTERNAL",
                           hal_status_to_string(HAL_EINTERNAL));
  TEST_ASSERT_EQUAL_STRING("HAL_ECANCELED",
                           hal_status_to_string(HAL_ECANCELED));
  TEST_ASSERT_EQUAL_STRING("HAL_EPROTO", hal_status_to_string(HAL_EPROTO));
  TEST_ASSERT_EQUAL_STRING("HAL_EAUTH", hal_status_to_string(HAL_EAUTH));
  TEST_ASSERT_EQUAL_STRING("HAL_EBUS", hal_status_to_string(HAL_EBUS));
  TEST_ASSERT_EQUAL_STRING("HAL_EHW", hal_status_to_string(HAL_EHW));
  TEST_ASSERT_EQUAL_STRING("HAL_ECONFIG", hal_status_to_string(HAL_ECONFIG));
  TEST_ASSERT_EQUAL_STRING("HAL_ESTATE", hal_status_to_string(HAL_ESTATE));
  TEST_ASSERT_EQUAL_STRING("HAL_EUNINIT", hal_status_to_string(HAL_EUNINIT));
  TEST_ASSERT_EQUAL_STRING("HAL_EDEPRECATED",
                           hal_status_to_string(HAL_EDEPRECATED));
  TEST_ASSERT_EQUAL_STRING("HAL_EUNKNOWN", hal_status_to_string(HAL_EUNKNOWN));
  TEST_ASSERT_EQUAL_STRING("HAL_STATUS_UNKNOWN",
                           hal_status_to_string((hal_status_t)-1000));
}

void test_hal_status_helpers_convert_legacy_results(void) {
  TEST_ASSERT_TRUE(hal_status_is_ok(HAL_OK));
  TEST_ASSERT_FALSE(hal_status_is_ok(HAL_NONE));
  TEST_ASSERT_FALSE(hal_status_is_ok(HAL_EINVAL));

  TEST_ASSERT_FALSE(hal_status_is_error(HAL_OK));
  TEST_ASSERT_FALSE(hal_status_is_error(HAL_NONE));
  TEST_ASSERT_TRUE(hal_status_is_error(HAL_EBUS));

  TEST_ASSERT_EQUAL_INT(HAL_OK, hal_status_from_bool(true, HAL_EBUS));
  TEST_ASSERT_EQUAL_INT(HAL_EBUS, hal_status_from_bool(false, HAL_EBUS));
  TEST_ASSERT_TRUE(hal_status_to_bool(HAL_OK));
  TEST_ASSERT_FALSE(hal_status_to_bool(HAL_EINVAL));
}

int main(void) {
  UNITY_BEGIN();
  RUN_TEST(test_hal_status_values_are_stable);
  RUN_TEST(test_hal_status_failure_values_are_negative);
  RUN_TEST(test_hal_status_to_string_returns_symbolic_names);
  RUN_TEST(test_hal_status_helpers_convert_legacy_results);
  return UNITY_END();
}
