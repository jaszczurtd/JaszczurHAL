/**
 * @file main.c
 * @brief Portable JaszczurHAL blink + boot/fault report.
 *
 * PORTABILITY GOAL
 * ----------------
 * The body of this demo is written against the portable `hal_*` surface ONLY,
 * so the exact same source compiles for every backend (RP2040/Arduino, the
 * host mock, and STM32G474). The only per-target line is the LED pin id.
 *
 * The single capability that is NOT yet unified across backends — the detailed
 * fault register dump (PC/CFSR/HFSR/BFAR) — is isolated behind one #ifdef and
 * clearly marked. Everything else (GPIO, time, serial, device UID and even the
 * reset reason) is already portable, so "was the last reset a crash?" is
 * reported through the common API on any backend.
 *
 * On RP2040 build via arduino-cli (see examples/01_blink); on STM32G474 build
 * via this folder's build.sh / CMake (defines JH_STM32G474_HW).
 */

#include <hal_gpio.h>
#include <hal_system.h>
#include <hal_serial.h>

#ifdef JH_STM32G474_HW
/* Platform-specific extra: rich Cortex-M fault record (the one piece not yet
 * behind a portable hal_* facade). Reported in addition to the portable
 * reset-reason below. */
#include "port/exception_info.h"
#endif

/* The only per-board line. Pin convention is uniform: linear id on RP2040,
 * port*16+pin on STM32 (PA5 = 5). Override for other boards. */
#if defined(JH_STM32G474_HW)
#define LED_PIN 5u    /* Nucleo-G474RE LD2 = PA5 */
#else
#define LED_PIN 25u   /* Raspberry Pi Pico onboard LED = GP25 */
#endif

/* Tiny portable uint -> decimal string (avoids pulling printf into the demo). */
static void u32_to_str(uint32_t v, char *out)
{
    char tmp[10];
    int i = 0;
    if (v == 0u) { out[0] = '0'; out[1] = '\0'; return; }
    while (v > 0u) { tmp[i++] = (char)('0' + (v % 10u)); v /= 10u; }
    int j = 0;
    while (i > 0) { out[j++] = tmp[--i]; }
    out[j] = '\0';
}

int main(void)
{
    hal_serial_begin(115200);
    hal_serial_println("=== JaszczurHAL blink (portable) ===");

    char uid[HAL_DEVICE_UID_BYTES * 2 + 1];
    if (hal_get_device_uid_hex(uid, sizeof(uid))) {
        hal_serial_print("UID: ");
        hal_serial_println(uid);
    }

    /* Portable crash visibility: works on every backend. */
    hal_serial_print("reset reason: ");
    hal_serial_println(hal_reset_reason_str(hal_get_reset_reason()));

#ifdef JH_STM32G474_HW
    /* Platform extra: detailed register dump if the last reset was a fault. */
    exception_info_report_last();
#endif

    hal_gpio_set_mode(LED_PIN, HAL_GPIO_OUTPUT);

    bool led_on = false;
    for (;;) {
        led_on = !led_on;
        hal_gpio_write(LED_PIN, led_on);

        char num[12];
        u32_to_str(hal_millis(), num);
        hal_serial_print(led_on ? "LED on  t=" : "LED off t=");
        hal_serial_print(num);
        hal_serial_println(" ms");

        hal_delay_ms(500u);   /* 1 Hz */
    }
}
