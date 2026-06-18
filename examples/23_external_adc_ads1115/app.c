/**
 * @file app.c
 * @brief Portable ADS1115 external ADC example over JaszczurHAL I2C.
 */

#include <hal/hal_app.h>
#include <hal/hal_external_adc.h>
#include <hal/hal_i2c.h>
#include <hal/hal_system.h>
#include <hal/hal_target.h>
#include <tools_c.h>

#define ADS1115_ADDR 0x48u
#define ADS1115_MV_PER_LSB 0.1875f

#if HAL_TARGET_IS_RP2040
#define EXAMPLE_I2C_SDA_PIN 4u
#define EXAMPLE_I2C_SCL_PIN 5u
#else
#define EXAMPLE_I2C_SDA_PIN 25u
#define EXAMPLE_I2C_SCL_PIN 24u
#endif

static int32_t voltage_to_mv(float volts) {
  return (int32_t)(volts * 1000.0f + (volts >= 0.0f ? 0.5f : -0.5f));
}

void app_start(void) {
  debugInit();
  deb("");
  deb("=== JaszczurHAL ADS1115 external ADC ===");
  deb("ADS1115: I2C address 0x48, gain +/-6.144 V");

  hal_i2c_init(EXAMPLE_I2C_SDA_PIN, EXAMPLE_I2C_SCL_PIN,
               HAL_I2C_CLOCK_STANDARD_HZ);
  hal_ext_adc_init(ADS1115_ADDR, ADS1115_MV_PER_LSB);
}

void app_task0(void) {
  for (uint8_t ch = 0u; ch < 4u; ++ch) {
    const int16_t raw = hal_ext_adc_read(ch);
    const float volts = hal_ext_adc_read_scaled(ch);

    deb("  A%u raw=%d  ~%ld mV", (unsigned)ch, (int)raw,
        (long)voltage_to_mv(volts));
  }
  deb("");
  hal_delay_ms(1000);
}
