<a id="jpeg"></a>

# JPEG - image decoding

*Also available in [Polish](../pl/19_JPEG.md).*

> **Part of [JaszczurHAL API Reference](../../en/JaszczurHAL_API.md)**

Decode JPEG images to RGB565 with the bundled `TJpgDec` library (`HAL_ENABLE_JPEG`). `HAL_ENABLE_JPEG_AS_BASE64` additionally enables reading Base64-encoded JPEG assets.

JaszczurHAL compiles the portable Tiny JPEG Decompressor C core. It does not include the source repository's Arduino interface, filesystem adapters, or display layer. Sources from `jaszczurtd/TJpg_Decoder` are fetched into `third_party/TJpg_Decoder` at the commit specified by `third_party/jpeg_version.conf`.

Managed version: `TJpg_Decoder` 1.1.0, including TJpgDec R0.03. The clean
checkout retains the ChaN decoder terms and Bodmer FreeBSD license in
`third_party/TJpg_Decoder/license.txt` and the source headers.

<a id="enable"></a>

## Enabling the module

Enable the module in `hal_project_config.h` or with a compiler definition:

```c
#pragma once

#define HAL_ENABLE_JPEG
```

For JPEG assets stored as Base64, enable this flag instead:

```c
#pragma once

#define HAL_ENABLE_JPEG_AS_BASE64
```

`HAL_ENABLE_JPEG_AS_BASE64` automatically enables `HAL_ENABLE_CRYPTO` and `HAL_ENABLE_JPEG`.

The core source and tracked wrapper compile to empty translation units unless
`HAL_ENABLE_JPEG` is defined. Code that uses the raw core or `jpeg*` helper
symbols must be compiled with the same flag.

<a id="include"></a>

## Including the headers

For the C or C++ RGB565 decoding functions, include:

```c
#include <hal/codecs/hal_image.h>
```

For direct access to the TJpgDec C API, use:

```c
#include <hal/codecs/jpeg/tjpgd.h>
```

Compatibility headers retain the historical unprefixed aliases. Use the `hal_image_*` names in new code.

<a id="embedded-profile"></a>

## Embedded-system configuration

The decoder reads compressed image data from memory and returns rectangular output regions through TJpgDec callbacks. JaszczurHAL uses the following configuration:

- emits RGB565 pixels;
- uses a temporary 3500-byte decoder workspace;
- allocates no memory internally;
- supports baseline grayscale and YCbCr JPEG data;
- supports 4:4:4, 4:2:0 and horizontal 4:2:2 sampling;
- rejects progressive JPEG data;
- provides 1:1, 1:2, 1:4 and 1:8 decoding in the raw TJpgDec API.

The high-level JaszczurHAL functions decode only at 1:1 scale. Read files separately through the HAL storage API.

<a id="api-surface"></a>

## Available operations

| Category | Functions |
|---|---|
| RGB565 helper | `hal_image_jpeg_decode_rgb565` |
| Base64 helpers | `hal_image_jpeg_base64_decoded_size`, `hal_image_jpeg_base64_decode_rgb565` |
| Raw decoder | `jd_prepare`, `jd_decomp` |

## Memory ownership

For the high-level functions, the application supplies input and output buffers and, for Base64, an intermediate buffer. The decoder workspace is allocated separately, as described below:

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

<a id="example-decode-jpeg-bytes-to-rgb565"></a>

## Example: decoding JPEG to RGB565

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

<a id="example-decode-base64-jpeg"></a>

## Example: decoding Base64 JPEG

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

<a id="asset-script-jpeg-to-base64"></a>

## Preparing a Base64 JPEG asset

`scripts/image_to_base64.py` converts a JPEG file to a C string declaration. Embed it in firmware and decode it with `HAL_ENABLE_JPEG_AS_BASE64` enabled.

Print the C declaration to the console:

```bash
./scripts/image_to_base64.py icon.jpg
```

Default output:

```c
static const char image[] =
    "...base64...";
```

Write the declaration to a file:

```bash
./scripts/image_to_base64.py icon.jpg --output icon_base64.txt
```

`--otput` is accepted as a compatibility alias for the same option. Use
`--name` to choose the C variable name:

```bash
./scripts/image_to_base64.py icon.jpg --name kBase64JpegImage
```

<a id="example-base64-jpeg-to-ili9341"></a>

## Example: displaying Base64 JPEG on ILI9341

The complete `examples/07_display_media` example shows how to prepare the data and display the image:

1. `hal_image_jpeg_base64_decoded_size()` calculates the exact decoded JPEG byte count.
2. Base64 text is decoded to an exactly sized JPEG work buffer.
3. `hal_image_jpeg_base64_decode_rgb565()` decodes the baseline JPEG directly to RGB565.
4. Images larger than `hal_display_get_width()` / `hal_display_get_height()`
   are rejected by the example before drawing.
5. `hal_display_draw_rgb_bitmap()` draws the RGB565 image on ILI9341.

The same project also tests direct JPEG decoding from memory before displaying the image.
