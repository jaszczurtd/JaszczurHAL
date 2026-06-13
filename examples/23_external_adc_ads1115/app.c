/**
 * @file app.c
 * @brief Portable ADS1115 external ADC example over JaszczurHAL I2C.
 */

#include <hal/hal_app.h>
#include <hal/hal_external_adc.h>
#include <hal/hal_i2c.h>
#include <hal/hal_serial.h>
#include <hal/hal_system.h>
#include <hal/hal_target.h>

#define ADS1115_ADDR 0x48u
#define ADS1115_MV_PER_LSB 0.1875f

#if HAL_TARGET_IS_RP2040
#define EXAMPLE_I2C_SDA_PIN 4u
#define EXAMPLE_I2C_SCL_PIN 5u
#else
#define EXAMPLE_I2C_SDA_PIN 25u
#define EXAMPLE_I2C_SCL_PIN 24u
#endif

static void print_uint32(uint32_t v) {
  char buf[11];
  int i = (int)sizeof(buf) - 1;
  buf[i] = '\0';
  do {
    buf[--i] = (char)('0' + (v % 10u));
    v /= 10u;
  } while (v != 0u && i > 0);
  hal_serial_print(&buf[i]);
}

static void print_int32(int32_t v) {
  if (v < 0) {
    hal_serial_print("-");
    v = -v;
  }
  print_uint32((uint32_t)v);
}

static void print_voltage(float volts) {
  int32_t mv = (int32_t)(volts * 1000.0f + (volts >= 0.0f ? 0.5f : -0.5f));
  hal_serial_print("~");
  print_int32(mv);
  hal_serial_print(" mV");
}

void app_start(void) {
  hal_serial_begin(115200);
  hal_serial_println("");
  hal_serial_println("=== JaszczurHAL ADS1115 external ADC ===");
  hal_serial_println("ADS1115: I2C address 0x48, gain +/-6.144 V");

  hal_i2c_init(EXAMPLE_I2C_SDA_PIN, EXAMPLE_I2C_SCL_PIN,
               HAL_I2C_CLOCK_STANDARD_HZ);
  hal_ext_adc_init(ADS1115_ADDR, ADS1115_MV_PER_LSB);
}

void app_task0(void) {
  for (uint8_t ch = 0u; ch < 4u; ++ch) {
    const int16_t raw = hal_ext_adc_read(ch);
    const float volts = hal_ext_adc_read_scaled(ch);

    hal_serial_print("  A");
    print_uint32(ch);
    hal_serial_print(" raw=");
    print_int32(raw);
    hal_serial_print("  ");
    print_voltage(volts);
    hal_serial_println("");
  }
  hal_serial_println("");
  hal_delay_ms(1000);
}
