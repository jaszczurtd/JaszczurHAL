<a id="13---adc"></a>

# 13 - Voltage measurements with the internal ADC and ADS1115

This example reads two internal 12-bit ADC inputs and all four channels of an
ADS1115 at I2C address `0x48`. For each ADS1115 channel, it demonstrates a raw
reading and a scaled voltage reading; the latter also returns operation status.

Internal ADC measurements continue when the ADS1115 is absent. The application
retries external ADC initialization every five seconds.

The `scan` variant enables `HAL_ENABLE_ADC_SCAN` and samples both internal
inputs and the temperature sensor continuously with hardware pacing: each pin
every 12 µs, one block of 400 frames every 4.8 ms. It reports the mean,
minimum and maximum of every block collected during the last second, and how
many blocks it missed. The ADS1115 is not used in this variant.

| Signal | RP family | STM32G474 |
|---|---|---|
| Internal ADC inputs | GP26 / GP27 | PA0 / PA1 |
| ADS1115 SDA / SCL | GP4 / GP5 | PB9 / PB8 |

The ADS1115 is set to the ±6.144 V range, equivalent to 0.1875 mV/LSB.
This is the ADC scaling setting, not the permitted voltage at its pins.
Check the device's input limits and supply voltage before connecting a signal.
