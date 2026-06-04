/**
 * @file main.c
 * @brief STM32G474 (Nucleo-G474RE) ADC reader - hardware verification of the
 *        real hal_adc backend.
 *
 * Periodically reads a couple of ADC1 inputs and prints their raw codes. This
 * is the simplest way to prove the bare-metal ADC1 polled conversion works on
 * real silicon.
 *
 * Wiring (ADC1): A0 = PA0 (ADC1_IN1), A1 = PA1 (ADC1_IN2) on the Nucleo
 * Arduino header. Console on USART2 (ST-Link VCP) @ 115200. See README.md.
 */

#include <hal/hal_serial.h>
#include <hal/hal_system.h>
#include <hal/hal_adc.h>

/* JaszczurHAL pin ids are port*16 + pin: PA0 = 0, PA1 = 1 (see hal_gpio). */
#define PIN_PA0 0x00u
#define PIN_PA1 0x01u

/* Print an unsigned decimal without relying on printf (hal_serial is char*). */
static void print_uint(unsigned v) {
    char buf[12];
    int i = (int)sizeof(buf) - 1;
    buf[i] = '\0';
    do {
        buf[--i] = (char)('0' + (v % 10u));
        v /= 10u;
    } while (v != 0u && i > 0);
    hal_serial_print(&buf[i]);
}

static void print_channel(const char *label, uint8_t pin) {
    const int raw = hal_adc_read(pin);
    hal_serial_print(label);
    print_uint((unsigned)raw);
    /* 12-bit full scale = 4095 -> ~3300 mV; mV = raw * 3300 / 4095. */
    hal_serial_print("  (~");
    print_uint((unsigned)((raw * 3300) / 4095));
    hal_serial_println(" mV)");
}

int main(void) {
    hal_serial_begin(115200);
    hal_serial_println("");
    hal_serial_println("=== JaszczurHAL G474 ADC reader ===");
    hal_serial_println("ADC1: A0=PA0 (IN1), A1=PA1 (IN2), 12-bit, VREF+=3V3");

    hal_adc_set_resolution(12);

    for (;;) {
        print_channel("  PA0 raw=", PIN_PA0);
        print_channel("  PA1 raw=", PIN_PA1);
        hal_serial_println("");
        hal_delay_ms(1000);
    }
}
