<a id="04---sensor-hub"></a>

# 04 - Temperature, humidity, and light sensors

This example reads light levels from a BH1750, temperature and humidity from
a DHT11, and temperature from a DS18B20. Each sensor is handled independently:
a missing device is reported without stopping the others. DS18B20 conversions
do not block the application loop while the measurement is in progress.

The BH1750 uses I2C address `0x23`. Connect the sensors as follows:

| Signal | RP family | STM32G474 |
|---|---|---|
| BH1750 SDA / SCL | GP4 / GP5 | PB9 / PB8 (I2C1) |
| DHT11 DATA | GP14 | PA8 |
| DS18B20 DATA | GP16 | PB0 |

I2C and OneWire lines require external pull-up resistors.
