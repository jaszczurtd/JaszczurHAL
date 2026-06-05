# 06_ds18b20

Portable DS18B20 temperature example for RP2040 and STM32G474.

The example enables `HAL_ENABLE_DS18B20`, which propagates
`HAL_ENABLE_ONEWIRE`. Both hardware targets use the shared Arduino-free
OneWire/DS18B20 implementation from `src/hal/impl/shared/onewire/`.

Default data pin:

- RP2040: GPIO 16.
- STM32G474: pin id 16, i.e. PB0 with the HAL `port * 16 + pin` convention.

A normal external 1-Wire pull-up resistor is expected on the data line.
