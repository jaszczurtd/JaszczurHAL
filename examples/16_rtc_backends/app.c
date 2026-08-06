#include <hal/hal_app.h>
#include <hal/hal_i2c.h>
#include <hal/hal_rtc.h>
#include <hal/hal_system.h>
#include <hal/hal_target.h>
#include <tools_c.h>

#include <stdbool.h>
#include <stdint.h>

#if HAL_TARGET_IS_RP
#define EXAMPLE_I2C_SDA 4u
#define EXAMPLE_I2C_SCL 5u
#else
/* STM32 pin id = port * 16 + pin: PB9/PB8. */
#define EXAMPLE_I2C_SDA 25u
#define EXAMPLE_I2C_SCL 24u
#endif

static hal_rtc_t s_pcf8563 = NULL;
static hal_rtc_t s_ds3231 = NULL;
static uint32_t s_last_report_ms = 0u;

static void print_datetime(const char *name, hal_rtc_t rtc) {
  if (rtc == NULL) {
    return;
  }

  hal_rtc_datetime_t value = {0};
  const hal_status_t status = hal_rtc_get_datetime_ex(rtc, &value);
  if (status != HAL_OK) {
    derr("%s datetime read failed: %s", name, hal_status_to_string(status));
    return;
  }

  deb("%s %04u-%02u-%02u %02u:%02u:%02u integrity=%u", name,
      (unsigned)value.year, (unsigned)value.month, (unsigned)value.day,
      (unsigned)value.hour, (unsigned)value.minute, (unsigned)value.second,
      value.clock_integrity ? 1u : 0u);
}

static void exercise_epoch(const char *name, hal_rtc_t rtc) {
  uint64_t epoch = 0u;
  hal_status_t status = hal_rtc_get_epoch_ex(rtc, &epoch);
  if (status != HAL_OK) {
    derr("%s epoch read failed: %s", name, hal_status_to_string(status));
    return;
  }

  status = hal_rtc_set_epoch_ex(rtc, epoch);
  if (status != HAL_OK) {
    derr("%s epoch same-value write failed: %s", name,
         hal_status_to_string(status));
    return;
  }

  uint64_t readback = 0u;
  status = hal_rtc_get_epoch_ex(rtc, &readback);
  if (status == HAL_OK) {
    deb("%s epoch=%lu same-value readback=%lu", name, (unsigned long)epoch,
        (unsigned long)readback);
  } else {
    derr("%s epoch readback failed: %s", name, hal_status_to_string(status));
  }
}

static void exercise_alarm(const char *name, hal_rtc_t rtc) {
  hal_rtc_datetime_t now = {0};
  hal_status_t status = hal_rtc_get_datetime_ex(rtc, &now);
  if (status != HAL_OK) {
    derr("%s alarm time read failed: %s", name, hal_status_to_string(status));
    return;
  }

  const hal_rtc_alarm_t requested = {
      .minute_enabled = true,
      .minute = (uint8_t)((now.minute + 1u) % 60u),
  };
  status = hal_rtc_set_alarm_ex(rtc, &requested);
  if (status != HAL_OK) {
    derr("%s alarm write failed: %s", name, hal_status_to_string(status));
    return;
  }

  hal_rtc_alarm_t readback = {0};
  status = hal_rtc_get_alarm_ex(rtc, &readback);
  if (status == HAL_OK) {
    deb("%s alarm minute_enabled=%u minute=%u", name,
        readback.minute_enabled ? 1u : 0u, (unsigned)readback.minute);
  } else {
    derr("%s alarm readback failed: %s", name, hal_status_to_string(status));
  }
}

static void exercise_clkout(const char *name, hal_rtc_t rtc) {
  hal_status_t status =
      hal_rtc_set_clkout_mode_ex(rtc, HAL_RTC_CLKOUT_DISABLED);
  if (status != HAL_OK) {
    derr("%s CLKOUT write failed: %s", name, hal_status_to_string(status));
    return;
  }

  hal_rtc_clkout_mode_t readback = HAL_RTC_CLKOUT_DISABLED;
  status = hal_rtc_get_clkout_mode_ex(rtc, &readback);
  if (status == HAL_OK) {
    deb("%s CLKOUT mode=%u", name, (unsigned)readback);
  } else {
    derr("%s CLKOUT readback failed: %s", name, hal_status_to_string(status));
  }
}

static void exercise_pcf8563_timer(hal_rtc_t rtc) {
  hal_status_t status = hal_rtc_set_timer_ex(rtc, HAL_RTC_TIMER_1_HZ, 5u);
  if (status != HAL_OK) {
    derr("PCF8563 timer write failed: %s", hal_status_to_string(status));
    return;
  }

  hal_rtc_timer_clock_t clock = HAL_RTC_TIMER_DISABLED;
  uint8_t count = 0u;
  status = hal_rtc_get_timer_ex(rtc, &clock, &count);
  if (status == HAL_OK) {
    deb("PCF8563 timer clock=%u count=%u", (unsigned)clock, (unsigned)count);
  } else {
    derr("PCF8563 timer readback failed: %s", hal_status_to_string(status));
  }
}

static hal_rtc_t init_rtc(hal_rtc_chip_t chip, uint8_t address,
                          const char *name) {
  hal_rtc_config_t config = {.chip = chip,
                             .bus = {.i2c = {.sda_pin = EXAMPLE_I2C_SDA,
                                             .scl_pin = EXAMPLE_I2C_SCL,
                                             .clock_hz = HAL_I2C_CLOCK_FAST_HZ,
                                             .i2c_bus = 0u,
                                             .i2c_addr = address}}};
  hal_rtc_t rtc = NULL;
  const hal_status_t status = hal_rtc_init_ex(&config, &rtc);
  if (status != HAL_OK) {
    derr("%s not detected: %s", name, hal_status_to_string(status));
    return NULL;
  }

  exercise_epoch(name, rtc);
  exercise_alarm(name, rtc);
  exercise_clkout(name, rtc);
  deb("%s ready", name);
  return rtc;
}

void app_start(void) {
  debugInit();
  deb("=== JaszczurHAL RTC backends: PCF8563 + DS3231 ===");
  (void)hal_i2c_init(EXAMPLE_I2C_SDA, EXAMPLE_I2C_SCL, HAL_I2C_CLOCK_FAST_HZ);

  s_pcf8563 = init_rtc(HAL_RTC_CHIP_PCF8563, HAL_RTC_PCF8563_DEFAULT_I2C_ADDR,
                       "PCF8563");
  s_ds3231 =
      init_rtc(HAL_RTC_CHIP_DS3231, HAL_RTC_DS3231_DEFAULT_I2C_ADDR, "DS3231");
  if (s_pcf8563 != NULL) {
    exercise_pcf8563_timer(s_pcf8563);
  }
}

void app_task0(void) {
  const uint32_t now = hal_millis();
  if ((uint32_t)(now - s_last_report_ms) < 1000u) {
    hal_delay_ms(10u);
    return;
  }
  s_last_report_ms = now;

  print_datetime("PCF8563", s_pcf8563);
  print_datetime("DS3231", s_ds3231);
  if (s_ds3231 != NULL) {
    float temperature_c = 0.0f;
    const hal_status_t status =
        hal_rtc_get_temperature_ex(s_ds3231, &temperature_c);
    if (status == HAL_OK) {
      deb("DS3231 temperature=%.2f C", (double)temperature_c);
    } else {
      derr("DS3231 temperature read failed: %s", hal_status_to_string(status));
    }
  }
}
