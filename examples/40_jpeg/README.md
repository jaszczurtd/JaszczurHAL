# Example 40: JPEG

Decodes a small baseline JPEG asset to RGB565 in memory using the managed
TJpgDec integration.

Enabled module:

- `HAL_ENABLE_JPEG_AS_BASE64` (propagates `HAL_ENABLE_CRYPTO` and
  `HAL_ENABLE_JPEG`)

The example keeps the firmware asset as Base64 text, decodes it to JPEG bytes,
then calls `jpegDecodeRgb565()`. It also exercises the direct
`jpegBase64DecodeRgb565()` helper.
