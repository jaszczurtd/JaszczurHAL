<a id="16---rtc-backends"></a>

# 16 - RTC clocks, timed wake-up, and battery backup

This example reads date and time from PCF8563, DS3231, and the
microcontroller's internal clock. It demonstrates alarms, CLKOUT, the
PCF8563 countdown timer, the DS3231 temperature sensor, and timed wake-up.
For devices that support it, it also reads and writes epoch timestamps.

The drivers are compiled together. The `chip` field of `hal_rtc_config_t`
selects the hardware, and an absent external RTC does not stop the other one.
I2C bus 0 uses GP4/GP5 on RP boards and PB9/PB8 on STM32G474.

Before changing an external clock, the application reads its state and keeps
an existing valid time. It writes the fixed test value `2026-08-20 12:34:50`
only when clock integrity is lost or the initial calendar cannot be read.
This does not synchronize the RTC with the current date. The PB9/PB8
connection was previously tested at 400 kHz with both PCF8563 and DS3231.

## Internal RTC and sleep

**STM32G474.** The application reads the internal RTC and reports one-second
progression. It keeps a clock source already selected in the backup domain.
Otherwise, it prefers LSE and falls back to LSI if needed. The fixed initial
date is used only when valid clock state has not been established.
The calendar supports years 2000-2099.

The sleep test wakes from CPU Sleep after two seconds, STOP0 after three,
and STOP1 after four. After each test, the application reports the wake
reason and elapsed monotonic time. It flushes USART2 before entering STOP
so diagnostic output leaves the device before the clocks change.

**RP2040 and RP2350.** The application uses the Pico SDK AON timer, reported
as `HAL_RTC_CLOCK_SOURCE_AON`. This uses the calendar RTC on RP2040 and
Powman on RP2350. A running clock is preserved across warm resets; the
initial value is written only when the clock is not valid.

The Pico SDK implementation used here supports CPU Sleep. Deep sleep and
power-down are reported as unsupported. An RTC-only wake request continues
waiting through unrelated interrupts, such as USB CDC traffic, until the
AON alarm is pending. Pico boards do not provide battery backup for this
clock, so its time is not expected to survive removal of power.

**Manual STM32G474 Standby test.** Set
`HAL_EXAMPLE_RTC_POWER_DOWN_TEST=1`. The final test wakes the MCU through
a reset after five seconds. On the next boot, the application reads and
clears the retained wake record instead of repeating the sleep sequence.

## Build and source selection

Run the commands below from the repository root. Every configuration
compiles both application files, but each one is wrapped in a condition on
`EXAMPLE_RTC_DISPLAY_CLOCK`, which only the `DISPLAY_CLOCK` variant defines.
Exactly one of them therefore provides `app_start()` and `app_task0()`:

| Selection | Application source | Targets |
|---|---|---|
| Base project | `app.c` | RP2040, RP2350 ARM, RP2350 RISC-V, STM32G474 |
| `DISPLAY_CLOCK` variant | `display_clock_app.c` | STM32G474 |

Build the base STM32G474 example:

```bash
vscode/entry/jh-vscode build \
  --project examples/16_rtc_backends \
  --target stm32g474 \
  --board nucleo-g474re
```

The base build runs `app.c` and writes the output to
`.build/examples/16_rtc_backends/firmware.elf`.

Build the display clock:

```bash
vscode/entry/jh-vscode build \
  --project examples/16_rtc_backends \
  --target stm32g474 \
  --board nucleo-g474re \
  --variant DISPLAY_CLOCK
```

The variant defines `EXAMPLE_RTC_DISPLAY_CLOCK=1`. That selects
`display_clock_app.c` instead of `app.c`, and `hal_project_config.h` then
enables ILI9341 support and stops any target other than STM32G474 with
`#error`. The variant builds as module `16_rtc_backends_display_clock`, and
its output is
`.build/examples/16_rtc_backends/variants/display_clock/firmware.elf`.

Both commands use the shared CMake firmware project through `jh-vscode`.
CMake adds the application sources, STM32 startup code, STM32G474
support, and enabled JaszczurHAL drivers and utilities. The HAL-provided
`main()` calls `app_start()` once and then repeatedly calls `app_task0()`.

In VS Code, use `Project: Build` or
`Project: Build variant: display_clock`. Select `stm32g474:nucleo-g474re`
before building the display variant.

## STM32G474 DS3231 retention clock

The `DISPLAY_CLOCK` variant displays DS3231 time as `HH:MM:SS` in the center
of a landscape ILI9341 screen, using `draw7SegString()`. Connect the DS3231
to PB9/PB8 and wire the display as described in `examples/07_display_media`.
Add `--variant DISPLAY_CLOCK` when building or uploading.

Build and upload through ST-LINK/OpenOCD:

```bash
vscode/entry/jh-vscode upload \
  --project examples/16_rtc_backends \
  --target stm32g474 \
  --board nucleo-g474re \
  --variant DISPLAY_CLOCK \
  --port /dev/ttyACM0 \
  --allow-unverified-port
```

Unlike the base example, this variant does not automatically replace invalid
time. It applies its embedded initial date only when the DS3231 still reports
valid clock state but holds an older date. If clock integrity is lost, it
shows a red `--:--:--`. Restarting the application therefore does not hide
a failed battery-backup test.
