# 23 - External I/O, converters, PMIC and RGB LED

One firmware build exercises:

- MCP23017, PCA9654E and PCF8574 I2C GPIO expanders;
- 74HC595 SPI output register;
- MCP3221 ADC and MCP4725 DAC;
- ADP5360 charger, fuel gauge and regulator;
- a one-pixel addressable RGB LED.

Every I2C device is initialized independently, so an incomplete bench setup
still exercises the drivers that are present.

The three otherwise-colliding GPIO expanders use explicitly strapped
addresses: MCP23017 `0x20`, PCA9654E `0x21`, and PCF8574 `0x22`. Configure each
module's address pins accordingly.

| Bus or signal | RP family | STM32G474 |
| --- | --- | --- |
| I2C SDA / SCL | GP4 / GP5 | PB9 / PB8 (D14 / D15) |
| SPI MISO / MOSI / SCK | GP16 / GP19 / GP18 | PA6 / PA7 / PA5 |
| 74HC595 CS | GP17 | PB6 |
| RGB data | GP22 | PA8 |

On NUCLEO-G474RE, SPI MISO/MOSI/SCK and the 74HC595 CS are available on CN10
pins 13/15/11/17, equivalent to D12/D11/D13/D10.

Use appropriate I2C addresses and external pull-ups for the attached modules.
