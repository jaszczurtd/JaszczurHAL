<a id="13---adc"></a>

# 13 - Voltage measurements with the internal ADC and ADS1115

This example reads two internal 12-bit ADC inputs and all four channels of an
ADS1115 at I2C address `0x48`. For each ADS1115 channel, it demonstrates a raw
reading and a scaled voltage reading; the latter also returns operation status.

Internal ADC measurements continue when the ADS1115 is absent. The application
retries external ADC initialization every five seconds.

The `SCAN` variant enables `HAL_ENABLE_ADC_SCAN` and samples the same two
internal inputs, `adc0` and `adc1`, and the chip's temperature sensor
continuously with hardware pacing: each pin every 12 µs, one block of 400
frames every 4.8 ms. It reports the mean, minimum and maximum of every block
collected during the last second, and how many blocks it missed. The ADS1115
is not used in this variant.

`device_tree.json` describes the wiring of every assembly, and the program
takes the ADC inputs, the I2C bus and the ADS1115 address from the generated
header. The table below repeats that wiring.

| Signal | Pico / Pico 2 | NUCLEO-G474RE | ESP32-S3-Zero |
|---|---|---|---|
| Internal ADC input `adc0` | GPIO26 | PA0, CN7 pin 28 / A0 | GPIO2 |
| Internal ADC input `adc1` | GPIO27 | PA1, CN7 pin 30 / A1 | GPIO1 |
| ADS1115 SDA | GPIO4 | PB9, CN10 pin 5 / D14 | GPIO4 |
| ADS1115 SCL | GPIO5 | PB8, CN10 pin 3 / D15 | GPIO5 |

On ESP32-S3-Zero `adc0` uses GPIO2 rather than GPIO0, the BOOT pin, which has
no ADC channel, and the ADS1115 bus uses GPIO4/GPIO5 rather than
GPIO25/GPIO24, which do not exist on the ESP32-S3.

The ADS1115 is set to the ±6.144 V range, equivalent to 0.1875 mV/LSB.
This is the ADC scaling setting, not the permitted voltage at its pins.
Check the device's input limits and supply voltage before connecting a signal.
