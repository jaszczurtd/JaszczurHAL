# ADS1115 External ADC

Portable ADS1115 external ADC smoke test for RP2040 and STM32G474.

Wiring:

- ADS1115 VDD -> 3V3, GND -> GND.
- ADS1115 ADDR -> GND for I2C address `0x48`.
- RP2040 default example pins: SDA = GP4, SCL = GP5.
- STM32G474/Nucleo-G474RE: I2C1 SCL = PB8, SDA = PB9; external pull-ups to 3V3 are recommended.

The example initialises I2C, starts the shared ADS1X15/ADS1115 driver through
`hal_external_adc`, then prints raw and approximate voltage values for all four
single-ended channels once per second.
