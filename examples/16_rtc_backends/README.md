# 16 - RTC backends

This project replaces the former PCF8563 and DS3231 examples. The providers
are compiled into one image and selected at runtime through
`hal_rtc_config_t::chip`. Missing external hardware is reported without
preventing another RTC from running.

The example covers date/time and epoch-capable handles, alarms and CLKOUT for
the external devices, the PCF8563 countdown timer, the DS3231 temperature
sensor, and relative wake-up through the target-native RTC. It uses I2C0 on
GP4/GP5 for RP targets and PB9/PB8 for STM32G474.

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
