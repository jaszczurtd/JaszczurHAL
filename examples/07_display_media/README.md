# 07 - Display and media

This example combines the ILI9341 graphics and firmware-asset demonstrations
that previously required five separate builds.

| Previous example | Coverage in this project |
|---|---|
| `09_display_tft` | ILI9341 initialization, text, lines, rectangles, rounded rectangles, and circles. |
| `36_lodePNG` | A 2x2 RGBA image is encoded to PNG, decoded, Base64-encoded, decoded again, and converted to RGB565. |
| `37_lodePNG_ili9341_base64` | An embedded Base64 PNG is inspected, decoded, converted to RGB565, and drawn on the TFT. |
| `40_jpeg` | An embedded baseline JPEG is decoded through both the direct and Base64 helper paths. |
| `41_jpeg_ili931_base64` | The decoded JPEG RGB565 pixels are drawn on the TFT. |

The managed JPEG integration uses TJpgDec and is decode-only. PNG encoding is
provided by LodePNG.

Enabled features:

- `HAL_ENABLE_ILI9341` and `HAL_DISPLAY_ILI9341`;
- `HAL_ENABLE_PNG_AS_BASE64`;
- `HAL_ENABLE_JPEG_AS_BASE64`.

## Wiring

The application uses SPI bus 0. RP-family targets use CS 17, DC 20, and reset
21. STM32G474 uses PA4, PB0, and PB1 respectively; PB0/PB1 deliberately avoid
the PA2/PA3 USART2 debug console. Connect the panel's SPI
clock and data pins to the bus-0 pins selected by the target backend.

## Memory limits

Firmware assets are limited to 4096 encoded bytes and 64 x 64 decoded pixels.
All decoded-size calculations are checked before allocation. PNG decode uses a
temporary RGBA8888 allocation and a shared 8 KiB RGB565 buffer; JPEG reuses the
same RGB565 buffer. The limits keep the example inside the STM32G474 RAM budget
and intentionally reject full-screen assets. Larger applications should use
tiling, streaming, or external RAM.
