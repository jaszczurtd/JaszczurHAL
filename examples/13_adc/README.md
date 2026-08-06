# 13 - ADC

This example reads two internal 12-bit ADC inputs and all four channels of an
ADS1115 at I2C address `0x48`. Internal readings continue when the external ADC
is absent; ADS1115 initialization is retried every five seconds. Each external
channel exercises both the raw and status-returning scaled-read facades.

RP targets use internal ADC GPIO 26/27 and I2C SDA/SCL GPIO 4/5. STM32G474
uses PA0/PA1 for the internal ADC and PB9/PB8 for I2C. The ADS1115 is configured
for a 0.1875 mV LSB (the +/-6.144 V range).
