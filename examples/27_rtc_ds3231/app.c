/**
 * @file app.c
 * @brief RTC example using the shared DS3231 backend.
 *
 * This example demonstrates:
 * - Initializing the RTC (DS3231 via I2C)
 * - Reading and writing date/time
 * - Converting to/from Unix epoch time
 * - Reading the DS3231 internal temperature sensor
 * - Setting alarms
 * - Outputting time via serial console
 *
 * Compatible targets:
 * - RP2040 (Arduino-Pico via I2C0: SDA=4, SCL=5)
 * - STM32G474 (I2C1 via HAL)
 * - Host/Mock (deterministic testing)
 */

#include <hal/hal_app.h>
#include <hal/hal_i2c.h>
#include <hal/hal_rtc.h>
#include <hal/hal_serial.h>
#include <hal/hal_sync.h>
#include <hal/hal_system.h>

#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>

static hal_rtc_t g_rtc = NULL;
static uint64_t g_last_print_epoch = 0;

static void print_datetime(const hal_rtc_datetime_t *dt) {
  if (!dt) {
    return;
  }

  hal_deb("  [%04u-%02u-%02u %02u:%02u:%02u] DOW:%u CK:%s\r\n",
          (unsigned)dt->year, (unsigned)dt->month, (unsigned)dt->day,
          (unsigned)dt->hour, (unsigned)dt->minute, (unsigned)dt->second,
          (unsigned)dt->weekday, dt->clock_integrity ? "OK" : "FAIL");
}

static void print_temperature(void) {
  if (!g_rtc) {
    return;
  }

  float temperature_c = 0.0f;
  if (hal_rtc_get_temperature(g_rtc, &temperature_c)) {
    hal_deb("DS3231 temperature: %.2f C\r\n", (double)temperature_c);
  } else {
    hal_deb("DS3231 temperature unavailable\r\n");
  }
}

static void test_epoch_conversion(void) {
  if (!g_rtc) {
    return;
  }

  hal_deb("\r\n=== Epoch Conversion Test ===\r\n");

  uint64_t epoch_in = 0;
  if (!hal_rtc_get_epoch(g_rtc, &epoch_in)) {
    hal_derr("hal_rtc_get_epoch failed");
    return;
  }

  hal_deb("Current epoch: %llu\r\n", (unsigned long long)epoch_in);

  uint64_t epoch_out = epoch_in + 3600ull;
  if (!hal_rtc_set_epoch(g_rtc, epoch_out)) {
    hal_derr("hal_rtc_set_epoch failed");
    return;
  }

  uint64_t epoch_verify = 0;
  if (!hal_rtc_get_epoch(g_rtc, &epoch_verify)) {
    hal_derr("hal_rtc_get_epoch failed");
    return;
  }

  hal_deb("After +3600s: %llu (expected ~%llu, diff=%lld)\r\n",
          (unsigned long long)epoch_verify, (unsigned long long)epoch_out,
          (long long)(epoch_verify - epoch_out));

  hal_rtc_set_epoch(g_rtc, epoch_in);
}

static void test_alarm_setup(void) {
  if (!g_rtc) {
    return;
  }

  hal_deb("\r\n=== Alarm Setup Test ===\r\n");

  hal_rtc_datetime_t now = {0};
  if (!hal_rtc_get_datetime(g_rtc, &now)) {
    hal_derr("hal_rtc_get_datetime failed");
    return;
  }

  hal_deb("Current time:\r\n");
  print_datetime(&now);

  uint8_t alarm_minute = (uint8_t)((now.minute + 1u) % 60u);
  hal_rtc_alarm_t alarm = {.minute_enabled = true,
                           .minute = alarm_minute,
                           .hour_enabled = false,
                           .day_enabled = false,
                           .weekday_enabled = false};

  if (!hal_rtc_set_alarm(g_rtc, &alarm)) {
    hal_derr("hal_rtc_set_alarm failed");
    return;
  }

  hal_deb("Alarm set for minute=%u (in ~%us)\r\n", (unsigned)alarm_minute,
          (unsigned)(60u - now.second));
}

static void test_clkout(void) {
  if (!g_rtc) {
    return;
  }

  hal_deb("\r\n=== CLKOUT Test ===\r\n");

  if (!hal_rtc_set_clkout_mode(g_rtc, HAL_RTC_CLKOUT_1_HZ)) {
    hal_derr("hal_rtc_set_clkout_mode failed");
    return;
  }

  hal_deb("CLKOUT set to 1 Hz\r\n");

  hal_rtc_clkout_mode_t mode = 0;
  if (!hal_rtc_get_clkout_mode(g_rtc, &mode)) {
    hal_derr("hal_rtc_get_clkout_mode failed");
    return;
  }

  hal_deb("CLKOUT mode readback: %u\r\n", (unsigned)mode);
  hal_rtc_set_clkout_mode(g_rtc, HAL_RTC_CLKOUT_DISABLED);
}

void app_start(void) {
  hal_debug_init(115200, 0);

  hal_deb("\r\n=== RTC DS3231 Example ===\r\n");
  hal_deb("Platform: %s\r\n", HAL_TARGET_NAME);

  hal_i2c_init(25u, 24u, HAL_I2C_CLOCK_FAST_HZ);

  hal_rtc_config_t cfg = {
      .chip = HAL_RTC_CHIP_DS3231,
      .bus = {.i2c = {
                  .sda_pin = 25,
                  .scl_pin = 24,
                  .clock_hz = HAL_I2C_CLOCK_FAST_HZ,
                  .i2c_bus = 0,
                  .i2c_addr = HAL_RTC_DS3231_DEFAULT_I2C_ADDR,
              }}};

  g_rtc = hal_rtc_init(&cfg);
  if (!g_rtc) {
    hal_derr("hal_rtc_init failed");
    return;
  }

  hal_rtc_datetime_t now = {0};
  if (hal_rtc_get_datetime(g_rtc, &now)) {
    hal_deb("Initial RTC value:\r\n");
    print_datetime(&now);
    test_epoch_conversion();
    print_temperature();
  }

  test_alarm_setup();
  test_clkout();
}

void app_task0(void) {
  if (!g_rtc) {
    return;
  }

  uint64_t now_epoch = 0;
  if (!hal_rtc_get_epoch(g_rtc, &now_epoch)) {
    return;
  }

  if (now_epoch == g_last_print_epoch) {
    return;
  }
  g_last_print_epoch = now_epoch;

  hal_rtc_datetime_t now = {0};
  if (!hal_rtc_get_datetime(g_rtc, &now)) {
    return;
  }

  hal_deb("RTC: %04u-%02u-%02u %02u:%02u:%02u\r\n", (unsigned)now.year,
          (unsigned)now.month, (unsigned)now.day, (unsigned)now.hour,
          (unsigned)now.minute, (unsigned)now.second);
}
