<a id="24---ssd1681-e-paper-display"></a>

# 24 - Displaying an image on SSD1681 e-paper

This example displays a monochrome image on a 200×200-pixel SSD1681 panel.
It sends image data through the `hal_display` API; the driver uses SPI and
GPIO control signals to operate the panel.

| Signal | RP family | NUCLEO-G474RE | Nucleo connection |
| --- | --- | --- | --- |
| MISO | GP16 | PA6 | CN10 pin 13 / D12 |
| MOSI | GP19 | PA7 | CN10 pin 15 / D11 |
| SCK | GP18 | PA5 | CN10 pin 11 / D13 |
| CS | GP17 | PB6 | CN10 pin 17 / D10 |
| DC | GP20 | PC7 | CN10 pin 19 / D9 |
| RESET | GP21 | PA9 | CN10 pin 21 / D8 |
| BUSY | GP22 | PA8 | CN10 pin 23 / D7 |

The example supplies no custom refresh LUTs. The controller uses its OTP
waveform, and the driver assumes a temperature of 25°C; this is not a measured
panel temperature. Provide the panel manufacturer's full and partial refresh
LUTs when required by the display module or operating temperature range.
