# Example 41: JPEG Base64 To ILI9341

Demonstrates drawing a Base64-encoded JPEG firmware asset on an ILI9341 TFT:

1. `jpegBase64DecodedSize()` calculates the exact decoded JPEG byte count.
2. Base64 text is decoded into an exactly sized JPEG work buffer.
3. `jpegBase64DecodeRgb565()` decodes the baseline JPEG to RGB565.
4. `hal_display_draw_rgb_bitmap()` draws the RGB565 buffer.

Enabled modules:

- `HAL_ENABLE_ILI9341`
- `HAL_DISPLAY_ILI9341`
- `HAL_ENABLE_JPEG_AS_BASE64` (propagates `HAL_ENABLE_CRYPTO` and
  `HAL_ENABLE_JPEG`)

TJpgDec does not support progressive JPEG files. Use baseline JPEG
assets for this workflow.
