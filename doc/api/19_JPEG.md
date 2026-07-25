# JPEG

> **Part of [JaszczurHAL API Reference](../JaszczurHAL_API.md)**

Covers: bundled `JPEGDecoder` / `picojpeg` enabled by `HAL_ENABLE_JPEG` and
Base64 JPEG helpers enabled by `HAL_ENABLE_JPEG_AS_BASE64`.

`JPEGDecoder` is a standalone baseline JPEG decoder bundled in
`src/hal/impl/shared/frameworks/jpeg/`. It is not a HAL wrapper and does not
abstract hardware. JaszczurHAL gates the upstream header/source behind
`HAL_ENABLE_JPEG`, keeps the default profile memory-oriented, and adds small
helpers for decoding JPEG bytes or Base64 JPEG assets directly to RGB565.

Author/license: upstream `JPEGDecoder` is based on the implementation by
Makoto Kurauchi and Bodmer. The bundled source carries its original license in
`src/hal/impl/shared/frameworks/jpeg/LICENSE`.

## Enable

Enable the module in `hal_project_config.h` or with a compiler definition:

```c
#pragma once

#define HAL_ENABLE_JPEG
```

For Base64-encoded JPEG assets, enable the helper flag instead:

```c
#pragma once

#define HAL_ENABLE_JPEG_AS_BASE64
```

`HAL_ENABLE_JPEG_AS_BASE64` propagates both `HAL_ENABLE_CRYPTO` and
`HAL_ENABLE_JPEG`, so the Base64 decoder and JPEG decoder are compiled
together.

The JPEG source files are part of the shared framework source list, but their
contents compile to nothing unless `HAL_ENABLE_JPEG` is defined. The public
header is also guarded, so code that uses `JPEGDecoder`, `JpegDec` or
`jpeg*` helper symbols must be compiled with the same flag.

## Include

Direct include for the bundled C++ decoder:

```cpp
#include <hal/impl/shared/frameworks/jpeg/JPEGDecoder.h>
```

For C++ files that already use the utility aggregator, `tools.h` also exposes
`JPEGDecoder` when `HAL_ENABLE_JPEG` is defined:

```cpp
#include <tools.h>
```

For C or C++ helper functions such as `jpegDecodeRgb565()` and
`jpegBase64DecodeRgb565()`, include:

```c
#include <tools_c.h>
```

## Embedded Profile

JaszczurHAL integrates the decoder as a target-neutral, memory-only utility.
Decode JPEG bytes with `JpegDec.decodeArray()` (or the `jpeg*` helpers below);
file-based decoding, if ever needed, must go through the JaszczurHAL filesystem.

The bundled decoder is based on `picojpeg` and does not support progressive JPEG
files. Use baseline JPEG assets.

## API Surface

Common APIs:

| Category | Functions / objects |
|---|---|
| Array decode | `JpegDec.decodeArray`, `JpegDec.available`, `JpegDec.read`, `JpegDec.abort` |
| MCU output | `JpegDec.pImage`, `JpegDec.width`, `JpegDec.height`, `JpegDec.MCUWidth`, `JpegDec.MCUHeight`, `JpegDec.MCUx`, `JpegDec.MCUy` |
| RGB565 helper | `jpegDecodeRgb565` |
| Base64 helpers | `jpegBase64DecodedSize`, `jpegBase64DecodeRgb565` |

## Memory Ownership

The high-level JaszczurHAL helpers use caller-provided buffers:

- `jpegDecodeRgb565()` reads JPEG bytes from memory and writes RGB565 pixels to
  a caller-provided output buffer.
- `jpegBase64DecodedSize()` validates Base64 and reports the exact decoded JPEG
  byte count without writing decoded bytes.
- `jpegBase64DecodeRgb565()` decodes Base64 into a caller-provided JPEG work
  buffer, then decodes the JPEG into a caller-provided RGB565 output buffer.
- The RGB565 output buffer must hold at least `width * height` pixels.
- The helpers return `false` for invalid arguments, invalid Base64, unsupported
  JPEG data, decode errors or too-small buffers.
- If using `JpegDec` directly, call `JpegDec.abort()` when stopping early or
  after an error so internal decoder state is released/reset.

## Example: Decode JPEG Bytes To RGB565

```c
#include <tools_c.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

static bool decode_jpeg_rgb565(const uint8_t *jpeg,
                               size_t jpeg_size,
                               unsigned short *rgb565,
                               size_t rgb565_pixels,
                               unsigned *width,
                               unsigned *height) {
    return jpegDecodeRgb565(jpeg, jpeg_size,
                            rgb565, rgb565_pixels,
                            width, height);
}
```

## Example: Decode Base64 JPEG

```c
#include <tools_c.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>

static bool decode_base64_jpeg_rgb565(const char *jpeg_base64,
                                      size_t jpeg_base64_len,
                                      unsigned short *rgb565,
                                      size_t rgb565_pixels,
                                      unsigned *width,
                                      unsigned *height) {
    size_t jpeg_work_size = 0;
    if (!jpegBase64DecodedSize(jpeg_base64, jpeg_base64_len,
                               &jpeg_work_size) ||
        jpeg_work_size == 0) {
        return false;
    }

    uint8_t *jpeg_work = malloc(jpeg_work_size);
    if (jpeg_work == NULL) {
        return false;
    }

    bool ok = jpegBase64DecodeRgb565(jpeg_base64, jpeg_base64_len,
                                     jpeg_work, jpeg_work_size,
                                     rgb565, rgb565_pixels,
                                     width, height);
    free(jpeg_work);
    return ok;
}
```

## Asset Script: JPEG To Base64

Use `scripts/image_to_base64.py` to turn a JPEG file into a C string that can be
embedded in firmware and decoded with `HAL_ENABLE_JPEG_AS_BASE64`.

Print generated C declaration to the console:

```bash
./scripts/image_to_base64.py icon.jpg
```

Default output:

```c
static const char image[] =
    "...base64...";
```

Write generated text to a file:

```bash
./scripts/image_to_base64.py icon.jpg --output icon_base64.txt
```

`--otput` is accepted as a compatibility alias for the same option. Use
`--name` to choose the C variable name:

```bash
./scripts/image_to_base64.py icon.jpg --name kBase64JpegImage
```

## Example: Base64 JPEG To ILI9341

`examples/41_jpeg_ili931_base64` shows the complete display path:

1. `jpegBase64DecodedSize()` calculates the exact decoded JPEG byte count.
2. Base64 text is decoded to an exactly sized JPEG work buffer.
3. `jpegBase64DecodeRgb565()` decodes the baseline JPEG directly to RGB565.
4. Images larger than `hal_display_get_width()` / `hal_display_get_height()`
   are rejected by the example before drawing.
5. `hal_display_draw_rgb_bitmap()` draws the RGB565 image on ILI9341.

`examples/40_jpeg` shows the smaller memory-only decode path without a display.
