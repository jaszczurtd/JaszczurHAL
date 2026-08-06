# 24 - SSD1681 e-paper display

This example drives a 200 x 200 monochrome SSD1681 panel through the shared
SPI/GPIO EPD backend and the public `hal_display` raw-write API. Adjust the SPI,
CS, DC, reset and BUSY pins for the board in use.

The example intentionally leaves waveform profiles empty, so the controller
uses its OTP waveform with the driver's default 25 C temperature value. Real
products should provide panel-vendor full/partial LUT profiles when required by
their display module and temperature range.
