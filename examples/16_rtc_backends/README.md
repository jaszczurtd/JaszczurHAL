# 16 - RTC backends

This project replaces the former PCF8563 and DS3231 examples. Both providers
are compiled into one image and selected at runtime through
`hal_rtc_config_t::chip`. Missing hardware is reported without preventing the
other RTC from running.

The example covers date/time and epoch-capable handles, alarms and CLKOUT for
both devices, the PCF8563 countdown timer, and the DS3231 temperature sensor.
It uses I2C0 on GP4/GP5 for RP targets and PB9/PB8 for STM32G474.
