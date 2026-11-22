#include "adc_scan_notification.h"
#include "hal/analog/jh_adc_scan_backend.h"
#include "utils/unity.h"

typedef struct {
  unsigned calls;
  bool from_isr;
} notification_t;

static void notified(void *user, bool from_isr) {
  notification_t *record = user;
  ++record->calls;
  record->from_isr = from_isr;
}

void adc_scan_notification_case(hal_adc_scan_config_t *config,
                                void (*feed)(void), bool from_isr) {
  notification_t first = {0U, false};
  notification_t second = {0U, false};
  uint8_t positions[HAL_ADC_SCAN_MAX_PINS] = {0U};
  uint32_t period = 0U;
  hal_adc_scan_block_t block = {0};
  config->notify = notified;
  config->notify_user = &first;
  TEST_ASSERT_EQUAL_INT(HAL_OK, jh_adc_scan_start(config, positions, &period));
  TEST_ASSERT_EQUAL_UINT(0U, first.calls);
  feed();
  TEST_ASSERT_EQUAL_UINT(1U, first.calls);
  TEST_ASSERT_EQUAL_INT(from_isr, first.from_isr);
  TEST_ASSERT_TRUE(jh_adc_scan_completed(&block));
  TEST_ASSERT_EQUAL_UINT32(1U, block.sequence);
  feed();
  TEST_ASSERT_EQUAL_UINT(2U, first.calls);
  TEST_ASSERT_TRUE(jh_adc_scan_completed(&block));
  TEST_ASSERT_EQUAL_UINT32(2U, block.sequence);
  TEST_ASSERT_EQUAL_INT(HAL_OK, jh_adc_scan_stop());
  TEST_ASSERT_EQUAL_UINT(2U, first.calls);

  config->notify_user = &second;
  TEST_ASSERT_EQUAL_INT(HAL_OK, jh_adc_scan_start(config, positions, &period));
  feed();
  TEST_ASSERT_EQUAL_UINT(2U, first.calls);
  TEST_ASSERT_EQUAL_UINT(1U, second.calls);
  TEST_ASSERT_EQUAL_INT(from_isr, second.from_isr);
  TEST_ASSERT_TRUE(jh_adc_scan_completed(&block));
  TEST_ASSERT_EQUAL_UINT32(1U, block.sequence);
  TEST_ASSERT_EQUAL_INT(HAL_OK, jh_adc_scan_stop());
}
