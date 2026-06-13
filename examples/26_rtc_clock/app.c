/**
 * @file app.c
 * @brief RTC (Real-Time Clock) example using PCF8563.
 *
 * This example demonstrates:
 * - Initializing the RTC (PCF8563 via I2C)
 * - Reading and writing date/time
 * - Converting to/from Unix epoch time
 * - Setting alarms
 * - Using countdown timer (PCF8563 feature)
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

/* RTC handle (global for app_task0) */
static hal_rtc_t g_rtc = NULL;

/* Timestamp when last printed to avoid log spam */
static uint64_t g_last_print_epoch = 0;

/**
 * @brief Print a formatted date/time to serial console.
 */
static void print_datetime(const hal_rtc_datetime_t *dt) {
  if (!dt) {
    return;
  }

  hal_deb("  [%04u-%02u-%02u %02u:%02u:%02u] DOW:%u CK:%s\r\n",
          (unsigned)dt->year, (unsigned)dt->month, (unsigned)dt->day,
          (unsigned)dt->hour, (unsigned)dt->minute, (unsigned)dt->second,
          (unsigned)dt->weekday, dt->clock_integrity ? "OK" : "FAIL");
}

/**
 * @brief Test epoch time conversion (read->epoch->set).
 */
static void test_epoch_conversion(void) {
  if (!g_rtc) {
    return;
  }

  hal_deb("\r\n=== Epoch Conversion Test ===\r\n");

  /* Read current time as epoch */
  uint64_t epoch_in = 0;
  if (!hal_rtc_get_epoch(g_rtc, &epoch_in)) {
    hal_derr("hal_rtc_get_epoch failed");
    return;
  }

  hal_deb("Current epoch: %llu\r\n", (unsigned long long)epoch_in);

  /* Add 3600 seconds (1 hour) and write back */
  uint64_t epoch_out = epoch_in + 3600ull;
  if (!hal_rtc_set_epoch(g_rtc, epoch_out)) {
    hal_derr("hal_rtc_set_epoch failed");
    return;
  }

  /* Read back to verify */
  uint64_t epoch_verify = 0;
  if (!hal_rtc_get_epoch(g_rtc, &epoch_verify)) {
    hal_derr("hal_rtc_get_epoch failed");
    return;
  }

  hal_deb("After +3600s: %llu (expected ~%llu, diff=%lld)\r\n",
          (unsigned long long)epoch_verify, (unsigned long long)epoch_out,
          (long long)(epoch_verify - epoch_out));

  /* Restore original time */
  hal_rtc_set_epoch(g_rtc, epoch_in);
}

/**
 * @brief Test alarm functionality (PCF8563-specific).
 */
static void test_alarm_setup(void) {
  if (!g_rtc) {
    return;
  }

  hal_deb("\r\n=== Alarm Setup Test ===\r\n");

  /* Read current time */
  hal_rtc_datetime_t now = {0};
  if (!hal_rtc_get_datetime(g_rtc, &now)) {
    hal_derr("hal_rtc_get_datetime failed");
    return;
  }

  hal_deb("Current time:\r\n");
  print_datetime(&now);

  /* Set alarm for 1 minute from now (minute-match only) */
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

  /* Read back and verify */
  hal_rtc_alarm_t alarm_read = {0};
  if (!hal_rtc_get_alarm(g_rtc, &alarm_read)) {
    hal_derr("hal_rtc_get_alarm failed");
    return;
  }

  hal_deb("Alarm readback: min_enabled=%d min=%u\r\n",
          alarm_read.minute_enabled, alarm_read.minute);
}

/**
 * @brief Test timer functionality (PCF8563-specific).
 */
static void test_timer_setup(void) {
  if (!g_rtc) {
    return;
  }

  hal_deb("\r\n=== Timer Setup Test ===\r\n");

  /* Set timer to 1 Hz, count down from 5 (5 second timeout) */
  if (!hal_rtc_set_timer(g_rtc, HAL_RTC_TIMER_1_HZ, 5u)) {
    hal_derr("hal_rtc_set_timer failed");
    return;
  }

  hal_deb("Timer set: 1 Hz, count=5 (expires in ~5s)\r\n");

  /* Read back */
  hal_rtc_timer_clock_t timer_clock = 0;
  uint8_t timer_count = 0;
  if (!hal_rtc_get_timer(g_rtc, &timer_clock, &timer_count)) {
    hal_derr("hal_rtc_get_timer failed");
    return;
  }

  hal_deb("Timer readback: clock=%u count=%u\r\n", (unsigned)timer_clock,
          (unsigned)timer_count);
}

/**
 * @brief Test CLKOUT functionality.
 */
static void test_clkout(void) {
  if (!g_rtc) {
    return;
  }

  hal_deb("\r\n=== CLKOUT Test ===\r\n");

  /* Enable 1 Hz output on CLKOUT pin */
  if (!hal_rtc_set_clkout_mode(g_rtc, HAL_RTC_CLKOUT_1_HZ)) {
    hal_derr("hal_rtc_set_clkout_mode failed");
    return;
  }

  hal_deb("CLKOUT set to 1 Hz\r\n");

  /* Verify */
  hal_rtc_clkout_mode_t mode = 0;
  if (!hal_rtc_get_clkout_mode(g_rtc, &mode)) {
    hal_derr("hal_rtc_get_clkout_mode failed");
    return;
  }

  hal_deb("CLKOUT mode readback: %u\r\n", (unsigned)mode);

  /* Disable after test */
  hal_rtc_set_clkout_mode(g_rtc, HAL_RTC_CLKOUT_DISABLED);
}

/**
 * @brief Initialize the RTC at startup.
 */
void app_start(void) {
  /* Initialize debug serial */
  hal_debug_init(115200, 0);

  hal_deb("\r\n=== RTC Clock Example ===\r\n");
  hal_deb("Platform: %s\r\n", HAL_TARGET_NAME);

  /* Configure I2C (bus 0, typical pins) */
  /* Note: Pin configuration is platform-specific via hal_i2c_init_bus */
  const hal_rtc_config_t cfg = {
      .chip = HAL_RTC_CHIP_PCF8563,
      .bus.i2c = {
          .i2c_bus = 0,
          .sda_pin = 25,      /* PB9 on STM32G474 (pin id = port*16 + pin) */
          .scl_pin = 24,      /* PB8 on STM32G474 (pin id = port*16 + pin) */
          .clock_hz = 100000, /* 100 kHz I2C */
          .i2c_addr = 0       /* Use default 0x51 for PCF8563 */
      }};

  g_rtc = hal_rtc_init(&cfg);
  if (!g_rtc) {
    hal_derr("Failed to initialize RTC");
    return;
  }

  hal_deb("RTC initialized successfully\r\n");

  /* Read and display current time */
  hal_rtc_datetime_t now = {0};
  if (hal_rtc_get_datetime(g_rtc, &now)) {
    hal_deb("Current time:\r\n");
    print_datetime(&now);

    /* Check clock integrity */
    bool ok = false;
    if (hal_rtc_get_clock_integrity(g_rtc, &ok)) {
      hal_deb("Clock integrity: %s\r\n",
              ok ? "OK" : "FAIL - consider setting time");
    }

    /* If clock not OK, set a default time (2025-01-01 12:00:00) */
    if (!ok) {
      hal_rtc_datetime_t default_time = {.year = 2025,
                                         .month = 1,
                                         .day = 1,
                                         .hour = 12,
                                         .minute = 0,
                                         .second = 0,
                                         .weekday = 3, /* Wednesday */
                                         .clock_integrity = true};
      if (hal_rtc_set_datetime(g_rtc, &default_time)) {
        hal_deb("Set default time to 2025-01-01 12:00:00\r\n");
      }
    }
  } else {
    hal_derr("Failed to read RTC");
  }

  hal_deb("\nStarting main loop - press Ctrl+C to exit\r\n");
}

/**
 * @brief Main loop: display time every 10 seconds and manage tests.
 */
void app_task0(void) {
  if (!g_rtc) {
    hal_delay_ms(1000);
    return;
  }

  /* Get current time */
  hal_rtc_datetime_t now = {0};
  if (!hal_rtc_get_datetime(g_rtc, &now)) {
    hal_delay_ms(1000);
    return;
  }

  /* Convert to epoch for rate limiting */
  uint64_t current_epoch = 0;
  if (hal_rtc_get_epoch(g_rtc, &current_epoch)) {
    /* Print every 10 seconds */
    if ((current_epoch - g_last_print_epoch) >= 10u) {
      hal_deb("\r\nTime update:\r\n");
      print_datetime(&now);

      /* Cycle through tests every ~60s */
      uint64_t cycle = (current_epoch / 10u) % 6u;
      switch (cycle) {
      case 1:
        test_epoch_conversion();
        break;
      case 2:
        test_alarm_setup();
        break;
      case 3:
        test_timer_setup();
        break;
      case 4:
        test_clkout();
        break;
      default:
        break;
      }

      g_last_print_epoch = current_epoch;
    }
  }

  hal_delay_ms(500);
}

/**
 * @brief Optional: Secondary task (if using cooperative multitasking).
 *
 * On RP2040: Called on core 1 if HAL_PROVIDE_APP_ENTRY enables it.
 * On STM32: Called cooperatively with app_task0() in main loop.
 */
void app_task1(void) {
  /* Not used in this example */
  (void)0;
}
