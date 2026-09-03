# JPEG

*Also available in [Polish](../pl/19_JPEG.md).*

> **Part of [JaszczurHAL API Reference](../../en/JaszczurHAL_API.md)**

Covers: managed `TJpgDec` enabled by `HAL_ENABLE_JPEG` and Base64 JPEG helpers
enabled by `HAL_ENABLE_JPEG_AS_BASE64`.

The `jaszczurtd/TJpg_Decoder` fork is fetched into
`third_party/TJpg_Decoder` at the commit pinned by
`third_party/jpeg_version.conf`. JaszczurHAL compiles only its target-neutral
Tiny JPEG Decompressor C core. The Arduino wrapper, filesystem adapters and
display facade from that repository are outside the build.

Managed version: `TJpg_Decoder` 1.1.0, including TJpgDec R0.03. The clean
checkout retains the ChaN decoder terms and Bodmer FreeBSD license in
`third_party/TJpg_Decoder/license.txt` and the source headers.

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
`HAL_ENABLE_JPEG`.

The core source and tracked wrapper compile to empty translation units unless
`HAL_ENABLE_JPEG` is defined. Code that uses the raw core or `jpeg*` helper
symbols must be compiled with the same flag.

## Include

For the C or C++ RGB565 helpers, include:

```c
#include <hal/codecs/hal_image.h>
```

The managed TJpgDec C API is available through:

```c
#include <hal/codecs/jpeg/tjpgd.h>
```

The compatibility utility headers retain the historical unprefixed aliases;
new code should use the `hal_image_*` names.

## Embedded Profile

JaszczurHAL feeds compressed bytes from memory and receives decoded rectangles
through the TJpgDec callback API. The configured core:

- emits RGB565 pixels;
- uses a temporary 3500-byte decoder workspace;
- allocates no memory internally;
- supports baseline grayscale and YCbCr JPEG data;
- supports 4:4:4, 4:2:0 and horizontal 4:2:2 sampling;
- rejects progressive JPEG data;
- provides 1:1, 1:2, 1:4 and 1:8 decoding in the raw TJpgDec API.

The high-level JaszczurHAL helpers currently decode at 1:1 scale. File input,
if needed, should be implemented through JaszczurHAL storage APIs.

## API Surface

| Category | Functions |
|---|---|
| RGB565 helper | `hal_image_jpeg_decode_rgb565` |
| Base64 helpers | `hal_image_jpeg_base64_decoded_size`, `hal_image_jpeg_base64_decode_rgb565` |
| Raw decoder | `jd_prepare`, `jd_decomp` |

## Memory Ownership

The high-level helpers use caller-provided input and output buffers:

- `hal_image_jpeg_decode_rgb565()` reads JPEG bytes from memory and writes RGB565 pixels to
  a caller-provided output buffer.
- `hal_image_jpeg_base64_decoded_size()` validates Base64 and reports the exact decoded JPEG
  byte count without writing decoded bytes.
- `hal_image_jpeg_base64_decode_rgb565()` decodes Base64 into a caller-provided JPEG work
  buffer, then decodes the JPEG into a caller-provided RGB565 output buffer.
- The RGB565 output buffer must hold at least `width * height` pixels.
- The decoder adapter allocates and releases its 3500-byte TJpgDec workspace
  for each high-level decode.
- The helpers return `false` for invalid arguments, invalid Base64, unsupported
  JPEG data, allocation failure, decode errors or too-small buffers.

## Example: Decode JPEG Bytes To RGB565

```c
#include <hal/codecs/hal_image.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

static bool decode_jpeg_rgb565(const uint8_t *jpeg,
                               size_t jpeg_size,
                               unsigned short *rgb565,
                               size_t rgb565_pixels,
                               unsigned *width,
                               unsigned *height) {
    return hal_image_jpeg_decode_rgb565(jpeg, jpeg_size,
                            rgb565, rgb565_pixels,
                            width, height);
}
```

## Example: Decode Base64 JPEG

```c
#include <hal/codecs/hal_image.h>
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
    if (!hal_image_jpeg_base64_decoded_size(jpeg_base64, jpeg_base64_len,
                               &jpeg_work_size) ||
        jpeg_work_size == 0) {
        return false;
    }

    uint8_t *jpeg_work = malloc(jpeg_work_size);
    if (jpeg_work == NULL) {
        return false;
    }

    bool ok = hal_image_jpeg_base64_decode_rgb565(jpeg_base64, jpeg_base64_len,
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

`examples/07_display_media` shows the complete display path:

1. `hal_image_jpeg_base64_decoded_size()` calculates the exact decoded JPEG byte count.
2. Base64 text is decoded to an exactly sized JPEG work buffer.
3. `hal_image_jpeg_base64_decode_rgb565()` decodes the baseline JPEG directly to RGB565.
4. Images larger than `hal_display_get_width()` / `hal_display_get_height()`
   are rejected by the example before drawing.
5. `hal_display_draw_rgb_bitmap()` draws the RGB565 image on ILI9341.

The same project also exercises the direct memory-only decode path before
rendering.
