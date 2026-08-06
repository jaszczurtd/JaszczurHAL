# 04 - Sensor hub

This portable example services three independent sensors in one loop:

- BH1750 on I2C address `0x23`;
- DHT11 on a GPIO data line;
- DS18B20 through the non-blocking OneWire workflow.

An unavailable sensor is reported without stopping the other two. RP targets use
I2C GP4/GP5, DHT GP14, and DS18B20 GP16. STM32G474 uses I2C1 PB9/PB8, DHT PA8,
and DS18B20 PB0. I2C and OneWire devices require their normal external pull-ups.
