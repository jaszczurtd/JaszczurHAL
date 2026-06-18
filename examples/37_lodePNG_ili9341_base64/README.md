# Example 37: LodePNG Base64 To ILI9341

Demonstrates the full path for a PNG image stored as Base64 text:

1. `pngBase64DecodedSize()` calculates the exact decoded PNG byte count.
2. Base64 text is decoded to an exactly sized PNG work buffer.
3. `lodepng_inspect()` reads PNG dimensions before full image decode.
4. Images larger than the configured ILI9341 resolution are rejected.
5. `lodepng_decode32()` decodes the PNG to RGBA8888.
6. `rgba8888ToRgb565()` converts RGBA8888 to RGB565.
7. `hal_display_draw_rgb_bitmap()` draws the RGB565 buffer.

Enabled modules:

- `HAL_ENABLE_ILI9341`
- `HAL_DISPLAY_ILI9341`
- `HAL_ENABLE_PNG_AS_BASE64` (propagates `HAL_ENABLE_CRYPTO` and
  `HAL_ENABLE_PNG`)

The embedded Base64 string can be regenerated from any PNG file with:

```bash
./scripts/image_to_base64.py icon.png --name kBase64PngImage
```

## RAM Limits

The example validates image dimensions against the configured display size, but
display-size fit is not the same as RAM fit.

The decode path needs temporary RAM for:

- decoded PNG bytes: exact size reported by `pngBase64DecodedSize()`,
- RGB565 output: `width * height * 2` bytes,
- temporary LodePNG RGBA8888 buffer: `width * height * 4` bytes.

A full 240x320 ILI9341 image would need 153600 bytes for RGB565 and 307200 bytes
for the temporary RGBA8888 decode buffer. That is larger than the STM32G474
96 KB RAM profile, so this workflow is intended for small icons/assets unless
the application adds tiling, streaming, external RAM, or a custom decode path.
