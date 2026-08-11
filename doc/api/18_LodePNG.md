# LodePNG

> **Part of [JaszczurHAL API Reference](../JaszczurHAL_API.md)**

Covers: managed `LodePNG` enabled by `HAL_ENABLE_PNG` and Base64 PNG helpers
enabled by `HAL_ENABLE_PNG_AS_BASE64`.

`LodePNG` is a standalone PNG encoder/decoder fetched into
`third_party/lodepng` at the commit pinned by
`third_party/lodepng_version.conf`. Thin integration wrappers in
`src/hal/codecs/lodepng/` gate the upstream source behind
`HAL_ENABLE_PNG` and expose the memory-based API through the existing include
path.

Managed version: `LodePNG` 20260119 from the `jaszczurtd/lodepng` fork.

The tracked wrapper preserves the C ABI when the source is compiled as C++.
GCC 15 for RP2350 RISC-V compiles this source with `-fno-inline` to avoid an
interprocedural optimizer false positive while retaining the complete warning
policy. The managed checkout remains unchanged.

Author/license: upstream `LodePNG` is authored by Lode Vandevenne and
distributed under the zlib license.

## Enable

Enable the module in `hal_project_config.h` or with a compiler definition:

```c
#pragma once

#define HAL_ENABLE_PNG
```

For Base64-encoded PNG assets, enable the helper flag instead:

```c
#pragma once

#define HAL_ENABLE_PNG_AS_BASE64
```

`HAL_ENABLE_PNG_AS_BASE64` propagates both `HAL_ENABLE_CRYPTO` and
`HAL_ENABLE_PNG`, so the Base64 decoder and LodePNG are compiled together.

The source file is part of the shared framework source list, but its contents
compile to nothing unless `HAL_ENABLE_PNG` is defined. The public header is also
guarded, so code that uses `lodepng_*` symbols must be compiled with the same
flag.

## Include

Direct include, safe from both C and C++:

```c
#include <hal/codecs/lodepng/lodepng.h>
```

For C++ files that already use the utility aggregator, `tools.h` also exposes
LodePNG when `HAL_ENABLE_PNG` is defined:

```c
#include <tools.h>
```

`tools_c.h` does not re-export LodePNG itself, but it exposes the JaszczurHAL
Base64 PNG helpers from `tools_api.h` when `HAL_ENABLE_PNG_AS_BASE64` is
defined.

## Embedded Profile

By default JaszczurHAL keeps the upstream memory-based C API and disables:

- `LODEPNG_COMPILE_DISK` - no `FILE` / disk helpers.
- `LODEPNG_COMPILE_CPP` - no `std::vector` / `std::string` wrapper.

If an application really needs those upstream optional sections, define
`HAL_LODEPNG_ENABLE_DISK` or `HAL_LODEPNG_ENABLE_CPP` before including
`hal/codecs/lodepng/lodepng.h`.

The usual upstream `LODEPNG_NO_COMPILE_*` flags still work for further trimming,
for example disabling the encoder or decoder in a tightly constrained build.

## API Surface

Common memory APIs:

| Category | Functions |
|---|---|
| Decode | `lodepng_decode_memory`, `lodepng_decode32`, `lodepng_decode24` |
| Encode | `lodepng_encode_memory`, `lodepng_encode32`, `lodepng_encode24` |
| Advanced state | `lodepng_state_init`, `lodepng_state_cleanup`, `lodepng_decode`, `lodepng_encode` |
| Color helpers | `lodepng_color_mode_init`, `lodepng_color_mode_cleanup`, `lodepng_get_raw_size` |
| Errors | `lodepng_error_text` |
| Base64 helpers | `pngBase64DecodedSize`, `pngBase64Decode32`, `pngBase64DecodeRgb565` |

## Memory Ownership

The simple encode/decode functions allocate output buffers with LodePNG's
allocator. With the default allocator profile, free returned buffers with
`free(ptr)`.

Rules that matter most:

- `lodepng_decode32()` and `lodepng_decode24()` allocate a raw pixel buffer.
- `lodepng_encode32()` and `lodepng_encode24()` allocate a PNG byte buffer.
- `pngBase64DecodedSize()` validates Base64 and reports the exact decoded PNG
  byte count without writing decoded bytes.
- `pngBase64Decode32()` decodes Base64 into a caller-provided PNG work buffer,
  then allocates the RGBA8888 output with LodePNG.
- `pngBase64DecodeRgb565()` uses the same caller-provided PNG work buffer,
  allocates a temporary RGBA8888 buffer with LodePNG, converts it to caller
  output RGB565, then frees the temporary RGBA8888 buffer.
- `lodepng_state_init()` must be paired with `lodepng_state_cleanup()`.
- Custom allocation can be supplied with upstream `LODEPNG_NO_COMPILE_ALLOCATORS`
  and external `lodepng_malloc`, `lodepng_realloc`, `lodepng_free` definitions.

## Example: Decode To RGB565

```c
#include <tools_c.h>
#include <hal/codecs/lodepng/lodepng.h>
#include <stdbool.h>
#include <stdlib.h>

static bool decode_icon_rgb565(const unsigned char *png,
                               size_t png_size,
                               unsigned short *rgb565,
                               size_t rgb565_pixels,
                               unsigned *width,
                               unsigned *height) {
    unsigned char *rgba = NULL;
    unsigned error = lodepng_decode32(&rgba, width, height, png, png_size);
    if (error != 0) {
        return false;
    }

    size_t pixels = (size_t)(*width) * (size_t)(*height);
    bool ok = pixels <= rgb565_pixels &&
              rgba8888ToRgb565(rgba, rgb565, pixels);

    free(rgba);
    return ok;
}
```

## Example: Decode Base64 PNG

```c
#include <tools_c.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>

static bool decode_base64_icon_rgb565(const char *png_base64,
                                      size_t png_base64_len,
                                      unsigned short *rgb565,
                                      size_t rgb565_pixels,
                                      unsigned *width,
                                      unsigned *height) {
    unsigned png_error = 0;
    size_t png_work_size = 0;
    if (!pngBase64DecodedSize(png_base64, png_base64_len, &png_work_size) ||
        png_work_size == 0) {
        return false;
    }

    uint8_t *png_work = malloc(png_work_size);
    if (png_work == NULL) {
        return false;
    }

    bool ok = pngBase64DecodeRgb565(png_base64, png_base64_len,
                                    png_work, png_work_size,
                                    rgb565, rgb565_pixels,
                                    width, height, &png_error);
    free(png_work);
    return ok;
}
```

## Asset Script: PNG To Base64

Use `scripts/image_to_base64.py` to turn a PNG file into a C string that can be
embedded in firmware and decoded with `HAL_ENABLE_PNG_AS_BASE64`.

Print generated C declaration to the console:

```bash
./scripts/image_to_base64.py icon.png
```

Default output:

```c
static const char image[] =
    "...base64...";
```

Write generated text to a file:

```bash
./scripts/image_to_base64.py icon.png --output icon_base64.txt
```

`--otput` is accepted as a compatibility alias for the same option. Use
`--name` to choose the C variable name:

```bash
./scripts/image_to_base64.py icon.png --name kBase64PngImage
```

## Example: Base64 PNG To ILI9341

`examples/07_display_media` shows the complete display path:

1. `pngBase64DecodedSize()` calculates the exact decoded PNG byte count.
2. Base64 text is decoded to an exactly sized PNG work buffer.
3. `lodepng_inspect()` validates image dimensions before full RGBA decode.
4. Images larger than `hal_display_get_width()` / `hal_display_get_height()`
   are rejected.
5. `lodepng_decode32()` produces RGBA8888.
6. `rgba8888ToRgb565()` converts the image to RGB565.
7. `hal_display_draw_rgb_bitmap()` draws the image on ILI9341.
