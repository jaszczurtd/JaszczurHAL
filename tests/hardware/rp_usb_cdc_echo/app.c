#include <hal/core/hal_app.h>
#include <hal/gpio/hal_gpio.h>
#include <hal/serial/hal_serial.h>
#include <hal/system/hal_system.h>
#include <hal/usb/hal_usb.h>

#include <stddef.h>
#include <stdint.h>

/* Two probe modes share the firmware. Echo mode validates raw CDC transport.
 * Chatter mode emits debug lines under a 4 s hardware watchdog so the host
 * can prove that a port closed with DTR left high (Linux HUPCL cleared)
 * cannot starve the application loop through blocking debug writes. */
#define JH_CHATTER_WATCHDOG_MS 4000u
#define JH_CHATTER_LINE_INTERVAL_MS 20u
#define JH_CHATTER_LINES_PER_LED_TOGGLE 25u

static const char kChatterEnterMagic[] = "JH:DTRSTUCK\n";
static const char kChatterExitMagic[] = "JH:ECHO\n";

static uint8_t s_echo_buffer[256];
static size_t s_echo_length;
static size_t s_echo_offset;
static bool s_led_state;

static bool s_chatter_mode;
static size_t s_magic_matched;
static bool s_wdt_reboot;
static uint32_t s_chatter_last_ms;
static uint32_t s_chatter_seq;

static void scan_for_magic(const char *magic, const uint8_t *data, size_t len) {
  for (size_t i = 0u; i < len; ++i) {
    const char byte = (char)data[i];
    if (byte == magic[s_magic_matched]) {
      ++s_magic_matched;
      if (magic[s_magic_matched] == '\0') {
        s_chatter_mode = !s_chatter_mode;
        s_magic_matched = 0u;
        s_chatter_last_ms = hal_millis();
        s_echo_length = 0u;
        s_echo_offset = 0u;
        return;
      }
    } else {
      s_magic_matched = byte == magic[0] ? 1u : 0u;
    }
  }
}

void app_start(void) {
  hal_gpio_set_mode(HAL_LED_BUILTIN, HAL_GPIO_OUTPUT);
  hal_gpio_write(HAL_LED_BUILTIN, false);
  s_wdt_reboot = hal_watchdog_caused_reboot();
  (void)hal_watchdog_enable(JH_CHATTER_WATCHDOG_MS, true);
}

static void chatter_task(void) {
  uint8_t received[32];
  size_t read = 0u;
  if (hal_usb_cdc_read(received, sizeof(received), &read) == HAL_OK) {
    scan_for_magic(kChatterExitMagic, received, read);
    if (!s_chatter_mode) {
      return;
    }
  }

  if (hal_millis_interval_elapsed_now(&s_chatter_last_ms,
                                      JH_CHATTER_LINE_INTERVAL_MS)) {
    ++s_chatter_seq;
    deb("JHDTR uptime_ms=%lu wdt_reboot=%d seq=%lu",
        (unsigned long)hal_millis(), s_wdt_reboot ? 1 : 0,
        (unsigned long)s_chatter_seq);
    if ((s_chatter_seq % JH_CHATTER_LINES_PER_LED_TOGGLE) == 0u) {
      s_led_state = !s_led_state;
      hal_gpio_write(HAL_LED_BUILTIN, s_led_state);
    }
  }
}

static void echo_task(void) {
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
    scan_for_magic(kChatterEnterMagic, s_echo_buffer, received);
  }
}

void app_task0(void) {
  hal_watchdog_feed();
  if (s_chatter_mode) {
    chatter_task();
  } else {
    echo_task();
  }
}
