<a id="23---external-io-converters-pmic-and-rgb-led"></a>

# 23 - External I/O, converters, and power management

This example uses MCP23017, PCA9654E, and PCF8574 GPIO expanders, a 74HC595
output register, MCP3221 ADC and MCP4725 DAC converters, an ADP5360 power
management device, and one addressable RGB LED. The ADP5360 demonstration
covers charging, battery-state readings, and regulator control.

I2C devices are initialized independently. You do not need every module
connected to test the ones that are available.

Set the expander address pins to match the application: MCP23017 at `0x20`,
PCA9654E at `0x21`, and PCF8574 at `0x22`. The distinct addresses prevent
conflicts on the shared bus.

| Bus or signal | RP family | STM32G474 |
| --- | --- | --- |
| I2C SDA / SCL | GP4 / GP5 | PB9 / PB8 (D14 / D15) |
| SPI MISO / MOSI / SCK | GP16 / GP19 / GP18 | PA6 / PA7 / PA5 |
| 74HC595 CS | GP17 | PB6 |
| RGB data | GP22 | PA8 |

On NUCLEO-G474RE, SPI MISO/MOSI/SCK and the 74HC595 `CS` are on CN10 pins
13/15/11/17, equivalent to D12/D11/D13/D10. Check the addresses of the other
modules and use external pull-up resistors on the I2C lines.
