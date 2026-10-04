#include <hal/core/hal_app.h>
#include <hal/gpio/hal_gpio.h>
#include <hal/serial/hal_serial.h>
#include <hal/system/hal_system.h>
#include <hal/usb/hal_usb.h>

#include <stddef.h>
#include <stdint.h>
#include <stdio.h>

/* Two probe modes share the firmware. Echo mode validates raw CDC transport.
 * Chatter mode emits debug lines under a 4 s hardware watchdog so the host
 * can prove that a port closed with DTR left high (Linux HUPCL cleared)
 * cannot starve the application loop through blocking debug writes. In echo
 * mode JH:IDENTITY answers with the serial number, UID and reset reason, and
 * JH:RESET restarts the board through hal_system_reset(). */
#define JH_CHATTER_WATCHDOG_MS 4000u
#define JH_CHATTER_LINE_INTERVAL_MS 20u
#define JH_CHATTER_LINES_PER_LED_TOGGLE 25u

typedef struct {
  const char *text;
  size_t matched;
} magic_t;

static magic_t s_chatter_enter = {"JH:DTRSTUCK\n", 0u};
static magic_t s_chatter_exit = {"JH:ECHO\n", 0u};
static magic_t s_identity = {"JH:IDENTITY\n", 0u};
static magic_t s_reset = {"JH:RESET\n", 0u};
static bool s_identity_pending;
static bool s_reset_pending;
static char s_identity_line[96];

static uint8_t s_echo_buffer[256];
static size_t s_echo_length;
static size_t s_echo_offset;
static bool s_led_state;

static bool s_chatter_mode;
static bool s_wdt_reboot;
static uint32_t s_chatter_last_ms;
static uint32_t s_chatter_seq;

/* True when @p byte completes the magic text. */
static bool magic_seen(magic_t *magic, char byte) {
  if (byte == magic->text[magic->matched]) {
    ++magic->matched;
    if (magic->text[magic->matched] == '\0') {
      magic->matched = 0u;
      return true;
    }
    return false;
  }
  magic->matched = byte == magic->text[0] ? 1u : 0u;
  return false;
}

static void toggle_chatter(void) {
  s_chatter_mode = !s_chatter_mode;
  s_chatter_last_ms = hal_millis();
  s_echo_length = 0u;
  s_echo_offset = 0u;
}

static void send_identity(void) {
  char serial[HAL_DEVICE_SERIAL_HEX_BUF_SIZE] = "none";
  char uid[HAL_DEVICE_UID_HEX_BUF_SIZE] = "none";
  uint8_t raw[HAL_DEVICE_SERIAL_MAX_BYTES];
  size_t serial_len = 0u;
  (void)hal_get_device_serial_ex(raw, sizeof(raw), &serial_len);
  (void)hal_get_device_serial_hex_ex(serial, sizeof(serial));
  (void)hal_get_device_uid_hex_ex(uid, sizeof(uid));
  const int len = snprintf(s_identity_line, sizeof(s_identity_line),
                           "\nJHID serial=%s len=%u uid=%s reset=%s wdt=%d\n",
                           serial, (unsigned)serial_len, uid,
                           hal_reset_reason_str(hal_get_reset_reason()),
                           s_wdt_reboot ? 1 : 0);
  size_t written = 0u;
  if (len > 0) {
    (void)hal_usb_cdc_write((const uint8_t *)s_identity_line, (size_t)len, 200u,
                            &written);
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
    for (size_t i = 0u; i < read; ++i) {
      if (magic_seen(&s_chatter_exit, (char)received[i])) {
        toggle_chatter();
        return;
      }
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
  if (s_identity_pending) {
    s_identity_pending = false;
    send_identity();
  }
  if (s_reset_pending) {
    hal_delay_ms(50u);
    (void)hal_system_reset();
  }

  size_t received = 0u;
  if (hal_usb_cdc_read(s_echo_buffer, sizeof(s_echo_buffer), &received) ==
      HAL_OK) {
    s_echo_length = received;
    for (size_t i = 0u; i < received; ++i) {
      const char byte = (char)s_echo_buffer[i];
      s_identity_pending |= magic_seen(&s_identity, byte);
      s_reset_pending |= magic_seen(&s_reset, byte);
      if (magic_seen(&s_chatter_enter, byte)) {
        toggle_chatter();
        return;
      }
    }
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
