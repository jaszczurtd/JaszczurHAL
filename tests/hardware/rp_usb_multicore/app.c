#include <hal/core/hal_app.h>
#include <hal/core/hal_status.h>
#include <hal/core/hal_target.h>
#include <hal/system/hal_board.h>
#include <hal/system/hal_system.h>
#include <hal/usb/hal_usb.h>

#include <pico/multicore.h>

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>

#ifndef JH_USB_MULTICORE_RECORDS
#define JH_USB_MULTICORE_RECORDS 4096u
#endif

#define USB_WRITE_TIMEOUT_MS 5000u

#if defined(HAL_ENABLE_FREERTOS)
#define JH_RUNTIME_NAME "freertos"
#else
#define JH_RUNTIME_NAME "baremetal"
#endif

static char s_record[2][64];
static uint8_t s_response[256];
static size_t s_response_length;
static size_t s_response_offset;
static volatile bool s_running;
static volatile bool s_done[2];
static volatile uint32_t s_count[2];
static volatile hal_status_t s_status[2];
static volatile uint8_t s_observed_core[2] = {0xffu, 0xffu};

static uint32_t record_token(uint8_t producer, uint32_t sequence) {
  return (sequence * 2654435761u) ^ ((uint32_t)producer * 0x9e3779b9u) ^
         0x4a485532u;
}

static void prepare_result(void) {
  const int length = snprintf(
      (char *)s_response, sizeof(s_response),
      "JHUSB2-RESULT target=%s board=%s runtime=%s requested=%lu "
      "core0=%lu core1=%lu status0=%d status1=%d observed0=%u observed1=%u\n",
      HAL_TARGET_NAME, HAL_BOARD_PROFILE_NAME, JH_RUNTIME_NAME,
      (unsigned long)JH_USB_MULTICORE_RECORDS,
      (unsigned long)__atomic_load_n(&s_count[0], __ATOMIC_ACQUIRE),
      (unsigned long)__atomic_load_n(&s_count[1], __ATOMIC_ACQUIRE),
      (int)__atomic_load_n(&s_status[0], __ATOMIC_ACQUIRE),
      (int)__atomic_load_n(&s_status[1], __ATOMIC_ACQUIRE),
      (unsigned int)__atomic_load_n(&s_observed_core[0], __ATOMIC_ACQUIRE),
      (unsigned int)__atomic_load_n(&s_observed_core[1], __ATOMIC_ACQUIRE));
  s_response_length =
      length > 0 && (size_t)length < sizeof(s_response) ? (size_t)length : 0u;
  s_response_offset = 0u;
}

static void start_run(void) {
  for (uint8_t producer = 0u; producer < 2u; ++producer) {
    __atomic_store_n(&s_count[producer], 0u, __ATOMIC_RELEASE);
    __atomic_store_n(&s_status[producer], HAL_NONE, __ATOMIC_RELEASE);
    __atomic_store_n(&s_observed_core[producer], 0xffu, __ATOMIC_RELEASE);
    __atomic_store_n(&s_done[producer], false, __ATOMIC_RELEASE);
  }
  __atomic_store_n(&s_running, true, __ATOMIC_RELEASE);
}

static void run_producer(uint8_t producer) {
  if (__atomic_load_n(&s_done[producer], __ATOMIC_ACQUIRE)) {
    hal_delay_ms(1u);
    return;
  }

  const uint32_t sequence =
      __atomic_load_n(&s_count[producer], __ATOMIC_ACQUIRE);
  if (sequence >= JH_USB_MULTICORE_RECORDS) {
    __atomic_store_n(&s_status[producer], HAL_OK, __ATOMIC_RELEASE);
    __atomic_store_n(&s_done[producer], true, __ATOMIC_RELEASE);
    return;
  }

  __atomic_store_n(&s_observed_core[producer], (uint8_t)get_core_num(),
                   __ATOMIC_RELEASE);
  const int length = snprintf(s_record[producer], sizeof(s_record[producer]),
                              "JHUSB2 core=%u seq=%06lu token=%08lx\n",
                              (unsigned int)producer, (unsigned long)sequence,
                              (unsigned long)record_token(producer, sequence));
  if (length <= 0 || (size_t)length >= sizeof(s_record[producer])) {
    __atomic_store_n(&s_status[producer], HAL_EOVERFLOW, __ATOMIC_RELEASE);
    __atomic_store_n(&s_done[producer], true, __ATOMIC_RELEASE);
    return;
  }

  size_t written = 0u;
  hal_status_t status =
      hal_usb_cdc_write((const uint8_t *)s_record[producer], (size_t)length,
                        USB_WRITE_TIMEOUT_MS, &written);
  if (status != HAL_OK || written != (size_t)length) {
    if (status == HAL_OK) {
      status = HAL_EIO;
    }
    __atomic_store_n(&s_status[producer], status, __ATOMIC_RELEASE);
    __atomic_store_n(&s_done[producer], true, __ATOMIC_RELEASE);
    return;
  }

  __atomic_store_n(&s_count[producer], sequence + 1u, __ATOMIC_RELEASE);
  if (sequence + 1u == JH_USB_MULTICORE_RECORDS) {
    __atomic_store_n(&s_status[producer], HAL_OK, __ATOMIC_RELEASE);
    __atomic_store_n(&s_done[producer], true, __ATOMIC_RELEASE);
  }
}

void app_start(void) {}

void app_task0(void) {
  if (s_response_offset < s_response_length) {
    size_t written = 0u;
    (void)hal_usb_cdc_write(s_response + s_response_offset,
                            s_response_length - s_response_offset,
                            USB_WRITE_TIMEOUT_MS, &written);
    s_response_offset += written;
    return;
  }

  if (__atomic_load_n(&s_running, __ATOMIC_ACQUIRE)) {
    run_producer(0u);
    if (__atomic_load_n(&s_done[0], __ATOMIC_ACQUIRE) &&
        __atomic_load_n(&s_done[1], __ATOMIC_ACQUIRE)) {
      __atomic_store_n(&s_running, false, __ATOMIC_RELEASE);
      prepare_result();
    }
    return;
  }

  uint8_t command = 0u;
  size_t received = 0u;
  if (hal_usb_cdc_read(&command, 1u, &received) == HAL_OK && received == 1u &&
      command == (uint8_t)'G') {
    start_run();
  } else {
    hal_delay_ms(1u);
  }
}

void app_task1(void) {
  if (__atomic_load_n(&s_running, __ATOMIC_ACQUIRE)) {
    run_producer(1u);
  } else {
    hal_delay_ms(1u);
  }
}
