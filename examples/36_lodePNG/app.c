#include <hal/hal_app.h>
#include <hal/hal_system.h>
#include <tools_c.h>
#include <utils/lodepng.h>

#include <stdlib.h>
#include <string.h>

static const unsigned char kPixelsRgba[] = {
    255, 0, 0, 255, 0, 255, 0, 255, 0, 0, 255, 255, 255, 255, 255, 128,
};

static void demo_lodepng_memory_api(void) {
  unsigned char *png = NULL;
  size_t png_size = 0u;
  unsigned error = lodepng_encode32(&png, &png_size, kPixelsRgba, 2u, 2u);
  if (error != 0u) {
    derr("encode error %u: %s\r\n", error, lodepng_error_text(error));
    return;
  }

  deb("encoded 2x2 RGBA image to %lu PNG bytes\r\n", (unsigned long)png_size);

  unsigned char *decoded = NULL;
  unsigned width = 0u;
  unsigned height = 0u;
  error = lodepng_decode32(&decoded, &width, &height, png, png_size);
  if (error != 0u) {
    derr("decode error %u: %s\r\n", error, lodepng_error_text(error));
    free(png);
    return;
  }

  const int same_pixels =
      width == 2u && height == 2u &&
      memcmp(decoded, kPixelsRgba, sizeof(kPixelsRgba)) == 0;

  deb("decoded width=%u height=%u pixels_match=%u\r\n", width, height,
      same_pixels ? 1u : 0u);

  unsigned short rgb565[4] = {0};
  if (rgba8888ToRgb565(decoded, rgb565, 4u)) {
    deb("rgb565 first=0x%04X last=0x%04X\r\n", (unsigned)rgb565[0],
        (unsigned)rgb565[3]);
  }

  size_t base64_size = hal_base64_encoded_len(png_size) + 1u;
  char *base64_png = (char *)malloc(base64_size);
  unsigned char *png_work = (unsigned char *)malloc(png_size);
  if (base64_png != NULL && png_work != NULL) {
    size_t base64_len = 0u;
    if (hal_base64_encode(png, png_size, base64_png, base64_size,
                          &base64_len)) {
      unsigned char *decoded_from_base64 = NULL;
      unsigned b64_width = 0u;
      unsigned b64_height = 0u;
      unsigned png_error = 0u;

      if (pngBase64Decode32(&decoded_from_base64, &b64_width, &b64_height,
                            base64_png, base64_len, png_work, png_size,
                            &png_error)) {
        deb("base64 decode32 width=%u height=%u\r\n", b64_width, b64_height);
        free(decoded_from_base64);
      } else {
        derr("base64 decode32 failed png_error=%u\r\n", png_error);
      }

      unsigned short base64_rgb565[4] = {0};
      if (pngBase64DecodeRgb565(base64_png, base64_len, png_work, png_size,
                                base64_rgb565, 4u, &b64_width, &b64_height,
                                &png_error)) {
        deb("base64 rgb565 first=0x%04X last=0x%04X\r\n",
            (unsigned)base64_rgb565[0], (unsigned)base64_rgb565[3]);
      } else {
        derr("base64 rgb565 failed png_error=%u\r\n", png_error);
      }
    }
  }

  free(png_work);
  free(base64_png);
  free(decoded);
  free(png);
}

void app_start(void) {
  debugInit();
  hal_deb_set_prefix("PNG");
  demo_lodepng_memory_api();
}

void app_task0(void) { hal_delay_ms(1000u); }
