/**
 * @file blink_app.c
 * @brief Portable blink logic - uses only the hal_* surface.
 *
 * Compiles unchanged for RP2040, STM32G474 and the host mock. The only
 * per-board line is the LED pin id; everything else (serial, GPIO, time,
 * device UID, reset reason) is portable.
 */

#include "blink_app.h"

/* Use the `hal/` prefix so the same source resolves under arduino-cli (which
 * puts the library's src/ on the include path) and the G474 build (-I src). */
#include <hal/hal_gpio.h>
#include <hal/hal_serial.h>
#include <hal/hal_system.h>

/* The single per-board line. Pin convention is uniform across backends:
 * a linear id on RP2040, port*16+pin on STM32 (PA5 = 5). Override for other
 * boards via -DBLINK_LED_PIN=... */
#ifndef BLINK_LED_PIN
#  if defined(JH_STM32G474_HW)
#    define BLINK_LED_PIN 5u   /* Nucleo-G474RE LD2 = PA5 */
#  else
#    define BLINK_LED_PIN 25u  /* Raspberry Pi Pico onboard LED = GP25 */
#  endif
#endif

static bool s_led_on;

/* Tiny portable uint -> decimal string (avoids pulling printf into the demo). */
static void u32_to_str(uint32_t v, char *out) {
    char tmp[10];
    int i = 0;
    if (v == 0u) {
        out[0] = '0';
        out[1] = '\0';
        return;
    }
    while (v > 0u) {
        tmp[i++] = (char)('0' + (v % 10u));
        v /= 10u;
    }
    int j = 0;
    while (i > 0) {
        out[j++] = tmp[--i];
    }
    out[j] = '\0';
}

void blink_app_setup(void) {
    hal_serial_begin(115200);
    hal_serial_println("=== JaszczurHAL portable blink ===");

    char uid[HAL_DEVICE_UID_BYTES * 2 + 1];
    if (hal_get_device_uid_hex(uid, sizeof(uid))) {
        hal_serial_print("UID: ");
        hal_serial_println(uid);
    }

    /* Portable crash visibility: works on every backend. */
    hal_serial_print("reset reason: ");
    hal_serial_println(hal_reset_reason_str(hal_get_reset_reason()));

    hal_gpio_set_mode(BLINK_LED_PIN, HAL_GPIO_OUTPUT);
    s_led_on = false;
}

void blink_app_step(void) {
    s_led_on = !s_led_on;
    hal_gpio_write(BLINK_LED_PIN, s_led_on);

    char num[12];
    u32_to_str(hal_millis(), num);
    hal_serial_print(s_led_on ? "LED on  t=" : "LED off t=");
    hal_serial_print(num);
    hal_serial_println(" ms");

    hal_delay_ms(500u);   /* 1 Hz */
}
