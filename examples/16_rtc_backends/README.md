# 16 - RTC backends

This project covers PCF8563 and DS3231 examples. The providers are compiled
into one image and selected at runtime through `hal_rtc_config_t::chip`.
Missing external hardware is reported without preventing another RTC from
running.

The example also covers date/time and epoch-capable handles, alarms and CLKOUT
for the external devices, the PCF8563 countdown timer, the DS3231 temperature
sensor, and relative wake-up through the target-native RTC. It uses I2C0 on
GP4/GP5 for RP targets and PB9/PB8 for STM32G474.

Each external RTC is read before the exercise begins. A fresh device with lost
clock integrity or an unreadable factory calendar receives the deterministic
`2026-08-20 12:34:50` test value; an already valid clock is retained. The
STM32G474 PB9/PB8 path is physically validated at 400 kHz with both PCF8563 and
DS3231.

On STM32G474 it additionally exercises the MCU's internal RTC. The example
prefers LSE and falls back to LSI only when the backup domain has no clock
source selected. It seeds a deterministic date only when clock integrity is
not yet established, then reports retained time and one-second progression.
The power sequence wakes from CPU Sleep after two seconds, STOP0 after three
seconds, and STOP1 after four seconds. It prints both the classified wake reason
and monotonic elapsed time after every transition. Serial flushing is enabled
so each diagnostic line has physically left USART2 before STOP changes the
clock tree. The internal calendar supports 2000..2099.

On RP2040 and RP2350 the same internal handle exercises the Pico SDK AON timer
and reports `HAL_RTC_CLOCK_SOURCE_AON`. RP2040 uses its calendar RTC and RP2350
uses Powman. The example preserves a running clock across warm resets and seeds
it only when integrity is absent. The current Pico SDK backend exercises CPU
Sleep; deep-sleep and power-down capabilities are reported as unsupported.
An RTC-only request keeps waiting through unrelated enabled interrupts, such as
USB CDC traffic, and completes only after the AON alarm becomes pending. Pico
boards do not provide battery backup, so AON time is not expected to survive
loss of power.

Define `HAL_EXAMPLE_RTC_POWER_DOWN_TEST=1` for a manual STM32G474 Standby test.
That final step intentionally resets the MCU after five seconds. On the next
boot the example reads and clears the retained wake record instead of entering
the power sequence again.

## Build and source selection

Run the following commands from the JaszczurHAL repository root. The project
metadata selects exactly one application source for each build:

| Selection | Application source | Supported targets |
| --- | --- | --- |
| Base project | `app.c` | RP2040 family and STM32G474 |
| `display-clock` variant | `display_clock_app.cpp` | STM32G474 |

Build the base STM32G474 example with:

```bash
vscode/entry/jh-vscode build \
  --project examples/16_rtc_backends \
  --target stm32g474 \
  --board nucleo-g474re
```

This sets `JH_PROJECT_SOURCES=app.c`. The resulting image is stored at
`.build/examples/16_rtc_backends/firmware.elf`.

Build the display clock with:

```bash
vscode/entry/jh-vscode build \
  --project examples/16_rtc_backends \
  --target stm32g474 \
  --board nucleo-g474re \
  --variant display-clock
```

The variant replaces the base source selection with
`JH_PROJECT_SOURCES=display_clock_app.cpp` and enables the ILI9341 display
features. It does not compile `app.c`, so the two implementations of
`app_start()` and `app_task0()` cannot collide. Its image is stored at
`.build/examples/16_rtc_backends/variants/display-clock/firmware.elf`.

In both cases `jh-vscode` configures the shared firmware CMake project. CMake
adds the selected application source, the STM32 startup code, the STM32G474
backend, and the enabled JaszczurHAL drivers and utilities. The HAL-provided
`main()` calls `app_start()` once and then calls `app_task0()` continuously.

The generated VS Code tasks expose the same paths as `Project: Build` and
`Project: Build variant: display-clock`. Select `stm32g474:nucleo-g474re`
before invoking the display variant.

## STM32G474 DS3231 retention clock

The manual `display-clock` variant uses the ILI9341 wiring from
`examples/07_display_media` and the DS3231 on PB9/PB8. It renders `HH:MM:SS`
with `draw7SegString()` in the center of a landscape display. Build or upload
it with `--variant display-clock`.

Build and flash it through ST-LINK/OpenOCD with:

```bash
vscode/entry/jh-vscode upload \
  --project examples/16_rtc_backends \
  --target stm32g474 \
  --board nucleo-g474re \
  --variant display-clock \
  --port /dev/ttyACM0 \
  --allow-unverified-port
```

The embedded initial value is applied only when the DS3231 still reports valid
clock integrity and contains an older date. A lost-integrity condition is never
automatically overwritten: the display changes to a red `--:--:--`, making a
failed battery-retention test visible after power is restored.
