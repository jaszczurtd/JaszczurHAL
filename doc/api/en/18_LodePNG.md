<a id="lodepng"></a>

# PNG - image encoding and decoding

*Also available in [Polish](../pl/18_LodePNG.md).*

> **Part of [JaszczurHAL API Reference](../../en/JaszczurHAL_API.md)**

Encode and decode PNG images with the bundled `LodePNG` library (`HAL_ENABLE_PNG`). `HAL_ENABLE_PNG_AS_BASE64` additionally enables reading Base64-encoded PNG assets.

By default, the API processes images in memory without file operations. LodePNG sources are fetched into `third_party/lodepng` at the commit specified by `third_party/lodepng_version.conf`. Integration code in `src/hal/codecs/lodepng/` compiles them when `HAL_ENABLE_PNG` is enabled and preserves the public header path.

Managed version: `LodePNG` 20260119 from the `jaszczurtd/lodepng` fork.

The tracked wrapper preserves the C ABI when the source is compiled as C++.
GCC 15 for RP2350 RISC-V compiles this source with `-fno-inline` to avoid an
interprocedural optimizer false positive while retaining the complete warning
policy. The managed checkout remains unchanged.

Author/license: upstream `LodePNG` is authored by Lode Vandevenne and
distributed under the zlib license.

<a id="enable"></a>

## Enabling the module

Enable the module in `hal_project_config.h` or with a compiler definition:

```c
#pragma once

#define HAL_ENABLE_PNG
```

For PNG assets stored as Base64, enable this flag instead:

```c
#pragma once

#define HAL_ENABLE_PNG_AS_BASE64
```

`HAL_ENABLE_PNG_AS_BASE64` automatically enables `HAL_ENABLE_CRYPTO` and `HAL_ENABLE_PNG`, compiling the Base64 decoder and PNG support together.

The source file is part of the shared framework source list, but its contents
compile to nothing unless `HAL_ENABLE_PNG` is defined. The public header is also
guarded, so code that uses `lodepng_*` symbols must be compiled with the same
flag.

<a id="include"></a>

## Including the headers

Include the headers directly from C or C++:

```c
#include <hal/codecs/hal_image.h>          // HAL memory/Base64 adapters
#include <hal/codecs/lodepng/lodepng.h>
```

Compatibility headers retain the historical unprefixed aliases. Use the `hal_image_*` names in new code.

<a id="embedded-profile"></a>

## Embedded-system configuration

The default configuration retains the original memory-based C API but disables:

- `LODEPNG_COMPILE_DISK` - no `FILE` / disk helpers.
- `LODEPNG_COMPILE_CPP` - no `std::vector` / `std::string` wrapper.

To enable optional file support or the C++ interface, define `HAL_LODEPNG_ENABLE_DISK` or `HAL_LODEPNG_ENABLE_CPP`, respectively, before including `hal/codecs/lodepng/lodepng.h`.

LodePNG `LODEPNG_NO_COMPILE_*` flags can reduce the code size further, for example by disabling an unused encoder or decoder.

<a id="api-surface"></a>

## Available operations

Common memory APIs:

| Category | Functions |
|---|---|
| Decode | `lodepng_decode_memory`, `lodepng_decode32`, `lodepng_decode24` |
| Encode | `lodepng_encode_memory`, `lodepng_encode32`, `lodepng_encode24` |
| Advanced state | `lodepng_state_init`, `lodepng_state_cleanup`, `lodepng_decode`, `lodepng_encode` |
| Color helpers | `lodepng_color_mode_init`, `lodepng_color_mode_cleanup`, `lodepng_get_raw_size` |
| Errors | `lodepng_error_text` |
| Base64 helpers | `hal_image_png_base64_decoded_size`, `hal_image_png_base64_decode_rgba8888`, `hal_image_png_base64_decode_rgb565` |

## Memory ownership

The simple encode and decode functions allocate their output buffers through the LodePNG allocator. With the default configuration, free these buffers with `free(ptr)`.

Buffer usage rules:

- `lodepng_decode32()` and `lodepng_decode24()` allocate a raw pixel buffer.
- `lodepng_encode32()` and `lodepng_encode24()` allocate a PNG byte buffer.
- `hal_image_png_base64_decoded_size()` validates Base64 and reports the exact decoded PNG
  byte count without writing decoded bytes.
- `hal_image_png_base64_decode_rgba8888()` decodes Base64 into a caller-provided PNG work buffer,
  then allocates the RGBA8888 output with LodePNG.
- `hal_image_png_base64_decode_rgb565()` uses the same caller-provided PNG work buffer,
  allocates a temporary RGBA8888 buffer with LodePNG, converts it to caller
  output RGB565, then frees the temporary RGBA8888 buffer.
- `lodepng_state_init()` must be paired with `lodepng_state_cleanup()`.
- Custom allocation can be supplied with upstream `LODEPNG_NO_COMPILE_ALLOCATORS`
  and external `lodepng_malloc`, `lodepng_realloc`, `lodepng_free` definitions.

<a id="example-decode-to-rgb565"></a>

## Example: decoding to RGB565

```c
#include <hal/codecs/hal_image.h>
#include <hal/codecs/lodepng/lodepng.h>
#include <hal/display/hal_pixel.h>
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
              hal_pixel_rgba8888_buffer_to_rgb565_ex(
                  rgba, rgb565, pixels) == HAL_OK;

    free(rgba);
    return ok;
}
```

<a id="example-decode-base64-png"></a>

## Example: decoding Base64 PNG

```c
#include <hal/codecs/hal_image.h>
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
    if (!hal_image_png_base64_decoded_size(png_base64, png_base64_len, &png_work_size) ||
        png_work_size == 0) {
        return false;
    }

    uint8_t *png_work = malloc(png_work_size);
    if (png_work == NULL) {
        return false;
    }

    bool ok = hal_image_png_base64_decode_rgb565(png_base64, png_base64_len,
                                    png_work, png_work_size,
                                    rgb565, rgb565_pixels,
                                    width, height, &png_error);
    free(png_work);
    return ok;
}
```

<a id="asset-script-png-to-base64"></a>

## Preparing a Base64 PNG asset

`scripts/image_to_base64.py` converts a PNG file to a C string declaration. Embed it in firmware and decode it with `HAL_ENABLE_PNG_AS_BASE64` enabled.

Print the C declaration to the console:

```bash
./scripts/image_to_base64.py icon.png
```

Default output:

```c
static const char image[] =
    "...base64...";
```

Write the declaration to a file:

```bash
./scripts/image_to_base64.py icon.png --output icon_base64.txt
```

`--otput` is accepted as a compatibility alias for the same option. Use
`--name` to choose the C variable name:

```bash
./scripts/image_to_base64.py icon.png --name kBase64PngImage
```

<a id="example-base64-png-to-ili9341"></a>

## Example: displaying Base64 PNG on ILI9341

The complete `examples/07_display_media` example shows how to prepare the data and display the image:

1. `hal_image_png_base64_decoded_size()` calculates the exact decoded PNG byte count.
2. Base64 text is decoded to an exactly sized PNG work buffer.
3. `lodepng_inspect()` validates image dimensions before full RGBA decode.
4. Images larger than `hal_display_get_width()` / `hal_display_get_height()`
   are rejected.
5. `lodepng_decode32()` produces RGBA8888.
6. `hal_pixel_rgba8888_buffer_to_rgb565_ex()` converts the image to RGB565.
7. `hal_display_draw_rgb_bitmap()` draws the image on ILI9341.
