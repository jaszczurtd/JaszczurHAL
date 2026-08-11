#include <hal/core/hal_app.h>
#include <hal/gpio/hal_gpio.h>
#include <hal/usb/hal_usb.h>

#include <stddef.h>
#include <stdint.h>

static uint8_t s_echo_buffer[256];
static size_t s_echo_length;
static size_t s_echo_offset;
static bool s_led_state;

void app_start(void) {
  hal_gpio_set_mode(HAL_LED_BUILTIN, HAL_GPIO_OUTPUT);
  hal_gpio_write(HAL_LED_BUILTIN, false);
}

void app_task0(void) {
  if (s_echo_offset < s_echo_length) {
    size_t written = 0u;
    (void)hal_usb_cdc_write(s_echo_buffer + s_echo_offset,
                            s_echo_length - s_echo_offset, 50u, &written);
    s_echo_offset += written;
    if (s_echo_offset == s_echo_length) {
      s_echo_offset = 0u;
      s_echo_length = 0u;
      s_led_state = !s_led_state;
      hal_gpio_write(HAL_LED_BUILTIN, s_led_state);
    }
    return;
  }

  size_t received = 0u;
  if (hal_usb_cdc_read(s_echo_buffer, sizeof(s_echo_buffer), &received) ==
      HAL_OK) {
    s_echo_length = received;
  }
}
