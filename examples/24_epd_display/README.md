<a id="24---ssd1681-e-paper-display"></a>

# 24 - Displaying an image on SSD1681 e-paper

This example displays a monochrome image on a 200×200-pixel SSD1681 panel.
It sends image data through the `hal_display` API; the driver uses SPI and
GPIO control signals to operate the panel.

`device_tree.json` describes the wiring of every assembly, and the program
takes its SPI bus, pins, SPI clock (4 MHz) and the BUSY polarity (active
high) from the generated header. The table below repeats that wiring.

| Signal | Pico / Pico 2 | NUCLEO-G474RE | Nucleo connection | ESP32-S3-Zero |
| --- | --- | --- | --- | --- |
| MISO | GP16 | PA6 | CN10 pin 13 / D12 | GPIO6 |
| MOSI | GP19 | PA7 | CN10 pin 15 / D11 | GPIO7 |
| SCK | GP18 | PB3 | CN10 pin 31 / D3 | GPIO5 |
| CS | GP17 | PB6 | CN10 pin 17 / D10 | GPIO4 |
| DC | GP20 | PC7 | CN10 pin 19 / D9 | GPIO3 |
| RESET | GP21 | PA9 | CN10 pin 21 / D8 | GPIO2 |
| BUSY | GP22 | PA8 | CN10 pin 23 / D7 | GPIO1 |

On NUCLEO-G474RE the SPI clock uses PB3 rather than D13 (PA5), because PA5
drives the LD2 LED.

The example supplies no custom refresh LUTs. The controller uses its OTP
waveform, and the driver assumes a temperature of 25°C; this is not a measured
panel temperature. Provide the panel manufacturer's full and partial refresh
LUTs when required by the display module or operating temperature range.
