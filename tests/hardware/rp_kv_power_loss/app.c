#include <hal/core/hal_app.h>
#include <hal/core/hal_status.h>
#include <hal/core/hal_target.h>
#include <hal/impl/rp2040/drivers/flash/rp_flash_storage.h>
#include <hal/storage/hal_eeprom.h>
#include <hal/storage/hal_kv.h>
#include <hal/system/hal_system.h>
#include <hal/usb/hal_usb.h>

#include <stddef.h>
#include <stdint.h>
#include <stdio.h>

#define COMMAND_RUN ((uint8_t)'T')
#define TEST_KEY 0x1901u

typedef struct {
  hal_status_t write_status;
  uint32_t recovered_value;
} phase_result_t;

static uint8_t s_response[320];
static size_t s_response_length;
static size_t s_response_offset;

static const char *target_name(void) {
#if HAL_TARGET_IS_RP2040
  return "rp2040";
#elif HAL_TARGET_IS_RP2350_ARM
  return "rp2350-arm";
#else
  return "unsupported";
#endif
}

static hal_status_t reload_store(void) {
  hal_status_t status = hal_eeprom_init(HAL_EEPROM_FLASH, 0u, 0u);
  if (status != HAL_OK) {
    return status;
  }
  return hal_kv_init_ex(0u, hal_eeprom_size());
}

static hal_status_t fresh_store(void) {
  jh_rp_flash_storage_set_replace_fail_phase(JH_RP_FLASH_REPLACE_FAIL_NONE);
  hal_status_t status = hal_eeprom_init(HAL_EEPROM_FLASH, 0u, 0u);
  if (status == HAL_OK) {
    status = hal_eeprom_reset();
  }
  return status == HAL_OK ? reload_store() : status;
}

static phase_result_t run_phase(jh_rp_flash_replace_fail_phase_t phase) {
  phase_result_t result = {HAL_NONE, 0u};
  hal_status_t status = fresh_store();
  if (status == HAL_OK) {
    status = hal_kv_set_u32_ex(TEST_KEY, 100u);
  }
  if (status != HAL_OK) {
    result.write_status = status;
    return result;
  }

  jh_rp_flash_storage_set_replace_fail_phase(phase);
  result.write_status = hal_kv_set_u32_ex(TEST_KEY, 200u);
  jh_rp_flash_storage_set_replace_fail_phase(JH_RP_FLASH_REPLACE_FAIL_NONE);

  status = reload_store();
  if (status == HAL_OK) {
    status = hal_kv_get_u32_ex(TEST_KEY, &result.recovered_value);
  }
  if (status != HAL_OK) {
    result.recovered_value = 0u;
  }
  return result;
}

static hal_status_t run_deferred(uint32_t *first, uint32_t *second) {
  hal_status_t status = fresh_store();
  if (status == HAL_OK) {
    status = hal_kv_set_auto_commit(false);
  }
  if (status == HAL_OK) {
    status = hal_kv_set_u32_ex(0x1902u, 11u);
  }
  if (status == HAL_OK) {
    status = hal_kv_set_u32_ex(0x1903u, 22u);
  }
  if (status == HAL_OK) {
    status = hal_kv_commit_ex();
  }
  (void)hal_kv_set_auto_commit(true);
  if (status == HAL_OK) {
    status = reload_store();
  }
  if (status == HAL_OK) {
    status = hal_kv_get_u32_ex(0x1902u, first);
  }
  if (status == HAL_OK) {
    status = hal_kv_get_u32_ex(0x1903u, second);
  }
  return status;
}

static void run_tests(void) {
  const phase_result_t invalidated =
      run_phase(JH_RP_FLASH_REPLACE_FAIL_AFTER_INVALIDATE);
  const phase_result_t body = run_phase(JH_RP_FLASH_REPLACE_FAIL_AFTER_BODY);
  const phase_result_t verified =
      run_phase(JH_RP_FLASH_REPLACE_FAIL_AFTER_VERIFY);
  const phase_result_t published =
      run_phase(JH_RP_FLASH_REPLACE_FAIL_AFTER_PUBLISH);
  uint32_t first = 0u;
  uint32_t second = 0u;
  const hal_status_t deferred = run_deferred(&first, &second);

  const int length = snprintf(
      (char *)s_response, sizeof(s_response),
      "JHKV2 target=%s invalidate=%d/%lu body=%d/%lu verify=%d/%lu "
      "publish=%d/%lu deferred=%d/%lu/%lu\n",
      target_name(), (int)invalidated.write_status,
      (unsigned long)invalidated.recovered_value, (int)body.write_status,
      (unsigned long)body.recovered_value, (int)verified.write_status,
      (unsigned long)verified.recovered_value, (int)published.write_status,
      (unsigned long)published.recovered_value, (int)deferred,
      (unsigned long)first, (unsigned long)second);
  s_response_length =
      length > 0 && (size_t)length < sizeof(s_response) ? (size_t)length : 0u;
  s_response_offset = 0u;
}

void app_start(void) {}

void app_task0(void) {
  if (s_response_offset < s_response_length) {
    size_t written = 0u;
    (void)hal_usb_cdc_write(s_response + s_response_offset,
                            s_response_length - s_response_offset, 100u,
                            &written);
    s_response_offset += written;
  } else {
    uint8_t command = 0u;
    size_t received = 0u;
    if (hal_usb_cdc_read(&command, 1u, &received) == HAL_OK && received == 1u &&
        command == COMMAND_RUN) {
      run_tests();
    }
  }
  hal_delay_ms(1u);
}

void app_task1(void) { hal_delay_ms(10u); }
