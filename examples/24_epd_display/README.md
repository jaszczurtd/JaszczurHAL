# 24 - SSD1681 e-paper display

This example drives a 200 x 200 monochrome SSD1681 panel through the shared
SPI/GPIO EPD backend and the public `hal_display` raw-write API.

| Signal | RP family | NUCLEO-G474RE | Nucleo connection |
| --- | --- | --- | --- |
| MISO | GP16 | PA6 | CN10 pin 13 / D12 |
| MOSI | GP19 | PA7 | CN10 pin 15 / D11 |
| SCK | GP18 | PA5 | CN10 pin 11 / D13 |
| CS | GP17 | PB6 | CN10 pin 17 / D10 |
| DC | GP20 | PC7 | CN10 pin 19 / D9 |
| RESET | GP21 | PA9 | CN10 pin 21 / D8 |
| BUSY | GP22 | PA8 | CN10 pin 23 / D7 |

The example intentionally leaves waveform profiles empty, so the controller
uses its OTP waveform with the driver's default 25 C temperature value. Real
products should provide panel-vendor full/partial LUT profiles when required by
their display module and temperature range.
