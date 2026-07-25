/**
 * @file app.c
 * @brief STM32G474 (Nucleo-G474RE) ADC reader - hardware verification of the
 *        real hal_adc backend.
 *
 * Periodically reads a couple of ADC1 inputs and prints their raw codes. This
 * is the simplest way to prove the bare-metal ADC1 polled conversion works on
 * real silicon.
 *
 * Wiring (ADC1): A0 = PA0 (ADC1_IN1), A1 = PA1 (ADC1_IN2) on the Nucleo
 * NUCLEO expansion header. See README.md.
 */

#include <hal/hal_adc.h>
#include <hal/hal_app.h>
#include <hal/hal_system.h>
#include <tools_c.h>

/* JaszczurHAL pin ids are port*16 + pin: PA0 = 0, PA1 = 1 (see hal_gpio). */
#define PIN_PA0 0x00u
#define PIN_PA1 0x01u

static void print_channel(const char *label, uint8_t pin) {
  const int raw = hal_adc_read(pin);
  /* 12-bit full scale = 4095 -> ~3300 mV; mV = raw * 3300 / 4095. */
  deb("%s%d  (~%u mV)", label, raw, (unsigned)((raw * 3300) / 4095));
}

void app_start(void) {
  debugInit();
  deb("");
  deb("=== JaszczurHAL G474 ADC reader ===");
  deb("ADC1: A0=PA0 (IN1), A1=PA1 (IN2), 12-bit, VREF+=3V3");

  hal_adc_set_resolution(12);
}

void app_task0(void) {
  print_channel("  PA0 raw=", PIN_PA0);
  print_channel("  PA1 raw=", PIN_PA1);
  deb("");
  hal_delay_ms(1000);
}
