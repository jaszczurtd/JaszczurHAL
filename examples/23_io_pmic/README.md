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

`device_tree.json` describes the wiring of every assembly, and the program
takes its buses, pins, device addresses and the 74HC595 SPI clock (4 MHz)
from the generated header. I2C runs at 100 kHz, the highest rate PCF8574
accepts. MCP3221 answers at `0x4D`, MCP4725 at `0x60` and ADP5360 at `0x46`.
The table below repeats the wiring.

| Bus or signal | Pico / Pico 2 | NUCLEO-G474RE | ESP32-S3-Zero |
| --- | --- | --- | --- |
| I2C SDA | GP4 | PB9, CN10 pin 5 / D14 | GPIO1 |
| I2C SCL | GP5 | PB8, CN10 pin 3 / D15 | GPIO2 |
| SPI MISO | GP16 | PA6, CN10 pin 13 / D12 | GPIO6 |
| SPI MOSI | GP19 | PA7, CN10 pin 15 / D11 | GPIO7 |
| SPI SCK | GP18 | PB3, CN10 pin 31 / D3 | GPIO5 |
| 74HC595 CS | GP17 | PB6, CN10 pin 17 / D10 | GPIO16 |
| RGB data | GP22 | PA8, CN10 pin 23 / D7 | GPIO8 |

On NUCLEO-G474RE the SPI clock uses PB3 rather than D13 (PA5), because PA5
drives the LD2 LED. On ESP32-S3-Zero I2C uses GPIO1/GPIO2 rather than
GPIO23/GPIO22, because the board does not have those pins. Use external
pull-up resistors on the I2C lines.
