# 25 - SSD1306 OLED (shared display backend)

Buffered monochrome OLED demo driven through the portable `hal_display` API.
Rendering uses the shared GFX engine (`jh_gfx`) and the shared SSD1306 I2C
driver, so the same `app.cpp` runs on both RP2040 and STM32G474.

## Wiring (I2C bus 0)

| Signal | RP2040 (Pico) | STM32G474 (Nucleo-G474RE) |
|--------|---------------|---------------------------|
| SDA    | GP4           | PB9                       |
| SCL    | GP5           | PB8                       |
| VCC    | 3V3           | 3V3                       |
| GND    | GND           | GND                       |

External pull-ups to 3V3 are required on SDA/SCL. Most 128x64 modules respond
at I2C address `0x3C`. Console on the board default debug UART @ 115200.

## Build

```bash
# RP2040
cmake --build build_examples_rp2040 --target 25_display_oled_rp2040

# STM32G474
cmake --build build_examples_stm32 --target 25_display_oled_stm32g474
```

## Notes

The SSD1306 is a buffered display: drawing operations compose a frame in RAM
and only become visible after `hal_display_flush()`. This contrasts with the
immediate-mode SPI TFT panels in example `09_display_tft`.
