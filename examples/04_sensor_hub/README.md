<a id="04---sensor-hub"></a>

# 04 - Temperature, humidity, and light sensors

This example reads light levels from a BH1750, temperature and humidity from
a DHT11, and temperature from a DS18B20. Each sensor is handled independently:
a missing device is reported without stopping the others. DS18B20 conversions
do not block the application loop while the measurement is in progress.

The BH1750 uses I2C address `0x23`. `device_tree.json` describes the wiring
of every assembly, and the program takes the bus, pins and sensor address
from the generated header. The table below repeats that wiring.

| Signal | Pico / Pico 2 | NUCLEO-G474RE | ESP32-S3-Zero |
|---|---|---|---|
| BH1750 SDA | GP4 | PB9 (I2C1), CN10 pin 5 / D14 | GPIO4 |
| BH1750 SCL | GP5 | PB8 (I2C1), CN10 pin 3 / D15 | GPIO5 |
| DHT11 DATA | GP14 | PA8, CN10 pin 23 / D7 | GPIO8 |
| DS18B20 DATA | GP16 | PB0, CN7 pin 34 / A3 | GPIO16 |

On the ESP32-S3-Zero the I2C bus uses GPIO4 / GPIO5, because the ESP32-S3
has no GPIO24 / GPIO25.

I2C and OneWire lines require external pull-up resistors.
