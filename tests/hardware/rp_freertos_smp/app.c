#include <FreeRTOS.h>
#include <task.h>

#include <hal/hal_app.h>
#include <hal/hal_gpio.h>
#include <hal/hal_sync.h>
#include <hal/hal_system.h>
#include <hal/hal_usb.h>

#include <pico/multicore.h>
#if defined(JH_FREERTOS_BOOTSEL_PROBE)
#include <pico/bootrom.h>
#endif

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>

#define STATUS_COMMAND 0xa5u

static hal_mutex_t s_state_mutex;
static uint32_t s_task0_ticks;
static uint32_t s_task1_ticks;
static uint8_t s_task0_core = 0xffu;
static uint8_t s_task1_core = 0xffu;
static uint8_t s_io_buffer[256];
static size_t s_io_length;
static size_t s_io_offset;
static bool s_led_state;

static void prepare_status(void) {
  uint32_t task0_ticks = 0u;
  uint32_t task1_ticks = 0u;
  uint8_t task0_core = 0xffu;
  uint8_t task1_core = 0xffu;

  hal_mutex_lock(s_state_mutex);
  task0_ticks = s_task0_ticks;
  task1_ticks = s_task1_ticks;
  task0_core = s_task0_core;
  task1_core = s_task1_core;
  hal_mutex_unlock(s_state_mutex);

  const int length = snprintf(
      (char *)s_io_buffer, sizeof(s_io_buffer),
      "JHRTOS1 core0=%u core1=%u task0=%lu task1=%lu heap=%lu "
      "scheduler=%ld\n",
      (unsigned int)task0_core, (unsigned int)task1_core,
      (unsigned long)task0_ticks, (unsigned long)task1_ticks,
      (unsigned long)hal_get_free_heap(), (long)xTaskGetSchedulerState());
  if (length <= 0) {
    s_io_length = 0u;
    return;
  }
  s_io_length = (size_t)length < sizeof(s_io_buffer) ? (size_t)length
                                                     : sizeof(s_io_buffer) - 1u;
  s_io_offset = 0u;
}

void app_start(void) {
  hal_gpio_set_mode(HAL_LED_BUILTIN, HAL_GPIO_OUTPUT);
  hal_gpio_write(HAL_LED_BUILTIN, false);
  s_state_mutex = hal_mutex_create();
  configASSERT(s_state_mutex != NULL);
}

void app_task0(void) {
  hal_mutex_lock(s_state_mutex);
  ++s_task0_ticks;
  s_task0_core = (uint8_t)get_core_num();
  hal_mutex_unlock(s_state_mutex);

#if defined(JH_FREERTOS_BOOTSEL_PROBE)
  if (s_task0_ticks >= 1000u) {
    reset_usb_boot(0u, 0u);
  }
#endif

  if (s_io_offset < s_io_length) {
    size_t written = 0u;
    (void)hal_usb_cdc_write(s_io_buffer + s_io_offset,
                            s_io_length - s_io_offset, 50u, &written);
    s_io_offset += written;
    if (s_io_offset == s_io_length) {
      s_io_offset = 0u;
      s_io_length = 0u;
      s_led_state = !s_led_state;
      hal_gpio_write(HAL_LED_BUILTIN, s_led_state);
    }
  } else {
    size_t received = 0u;
    if (hal_usb_cdc_read(s_io_buffer, sizeof(s_io_buffer), &received) ==
            HAL_OK &&
        received > 0u) {
      s_io_length = received;
      s_io_offset = 0u;
      if (received == 1u && s_io_buffer[0] == STATUS_COMMAND) {
        prepare_status();
      }
    }
  }

  hal_delay_ms(1u);
}

void app_task1(void) {
  hal_mutex_lock(s_state_mutex);
  ++s_task1_ticks;
  s_task1_core = (uint8_t)get_core_num();
  hal_mutex_unlock(s_state_mutex);
  hal_delay_ms(1u);
}
