#include "media_assets.h"

#include <hal/codecs/lodepng/lodepng.h>
#include <hal/core/hal_app.h>
#include <hal/core/hal_target.h>
#include <hal/display/hal_display.h>
#include <hal/system/hal_system.h>
#include <tools.h>

#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#if HAL_TARGET_IS_RP
static const uint8_t kTftCsPin = 17u;
static const uint8_t kTftDcPin = 20u;
static const uint8_t kTftRstPin = 21u;
#elif HAL_TARGET_IS_STM32G474
static const uint8_t kTftCsPin = 22u; /* PB6, CN10 pin 17 / CN5 D10 */
static const uint8_t kTftDcPin = 39u; /* PC7, CN10 pin 19 / CN5 D9 */
static const uint8_t kTftRstPin = 9u; /* PA9, CN10 pin 21 / CN5 D8 */
#else
static const uint8_t kTftCsPin = 17u;
static const uint8_t kTftDcPin = 20u;
static const uint8_t kTftRstPin = 21u;
#endif

static const int kDisplayWidth = 240;
static const int kDisplayHeight = 320;
static const unsigned kMediaMaxDimension = 64u;
static const size_t kMediaMaxPixels =
    (size_t)kMediaMaxDimension * (size_t)kMediaMaxDimension;
static const size_t kMediaMaxEncodedBytes = 4096u;
static const size_t kMediaMaxBase64Bytes = 8192u;

static uint16_t s_rgb565[kMediaMaxPixels];
static bool s_display_ready = false;

static const unsigned char kRoundTripRgba[] = {
    255u, 0u, 0u,   255u, 0u,   255u, 0u,   255u,
    0u,   0u, 255u, 255u, 255u, 255u, 255u, 128u,
};

static bool checked_pixel_count(unsigned width, unsigned height,
                                size_t *out_pixels) {
  if (out_pixels == NULL || width == 0u || height == 0u ||
      width > kMediaMaxDimension || height > kMediaMaxDimension) {
    return false;
  }
  if ((size_t)height > ((size_t)-1) / (size_t)width) {
    return false;
  }

  const size_t pixels = (size_t)width * (size_t)height;
  if (pixels > kMediaMaxPixels) {
    return false;
  }
  *out_pixels = pixels;
  return true;
}

static bool png_memory_round_trip(void) {
  unsigned char *png = NULL;
  size_t png_size = 0u;
  unsigned error = lodepng_encode32(&png, &png_size, kRoundTripRgba, 2u, 2u);
  if (error != 0u || png == NULL) {
    derr("PNG encode failed: %u %s", error, lodepng_error_text(error));
    free(png);
    return false;
  }
  if (png_size == 0u || png_size > kMediaMaxEncodedBytes) {
    derr("PNG encode size rejected: %lu", (unsigned long)png_size);
    free(png);
    return false;
  }

  unsigned char *decoded = NULL;
  unsigned width = 0u;
  unsigned height = 0u;
  error = lodepng_decode32(&decoded, &width, &height, png, png_size);
  const bool direct_ok =
      error == 0u && decoded != NULL && width == 2u && height == 2u &&
      memcmp(decoded, kRoundTripRgba, sizeof(kRoundTripRgba)) == 0;
  free(decoded);
  if (!direct_ok) {
    derr("PNG round-trip decode failed: %u", error);
    free(png);
    return false;
  }

  const size_t base64_capacity = hal_base64_encoded_len(png_size) + 1u;
  if (base64_capacity == 0u || base64_capacity > kMediaMaxBase64Bytes) {
    derr("PNG Base64 size rejected: %lu", (unsigned long)base64_capacity);
    free(png);
    return false;
  }

  char *base64 = (char *)malloc(base64_capacity);
  uint8_t *png_work = (uint8_t *)malloc(png_size);
  if (base64 == NULL || png_work == NULL) {
    derr("PNG round-trip allocation failed");
    free(png_work);
    free(base64);
    free(png);
    return false;
  }

  size_t base64_length = 0u;
  unsigned png_error = 0u;
  unsigned base64_width = 0u;
  unsigned base64_height = 0u;
  uint16_t base64_pixels[4] = {};
  const bool base64_ok =
      hal_base64_encode(png, png_size, base64, base64_capacity,
                        &base64_length) &&
      pngBase64DecodeRgb565(base64, base64_length, png_work, png_size,
                            base64_pixels, 4u, &base64_width, &base64_height,
                            &png_error) &&
      base64_width == 2u && base64_height == 2u;

  if (base64_ok) {
    deb("PNG round-trip: bytes=%lu base64=%lu rgb565=%04X/%04X",
        (unsigned long)png_size, (unsigned long)base64_length,
        (unsigned)base64_pixels[0], (unsigned)base64_pixels[3]);
  } else {
    derr("PNG Base64 round-trip failed: %u", png_error);
  }

  free(png_work);
  free(base64);
  free(png);
  return base64_ok;
}

static bool decode_png_asset(unsigned *out_width, unsigned *out_height) {
  if (out_width == NULL || out_height == NULL) {
    return false;
  }
  *out_width = 0u;
  *out_height = 0u;

  size_t png_size = 0u;
  if (!pngBase64DecodedSize(kMediaPngBase64, sizeof(kMediaPngBase64) - 1u,
                            &png_size) ||
      png_size == 0u || png_size > kMediaMaxEncodedBytes) {
    derr("PNG asset size rejected");
    return false;
  }

  uint8_t *png = (uint8_t *)malloc(png_size);
  if (png == NULL) {
    derr("PNG asset allocation failed: %lu", (unsigned long)png_size);
    return false;
  }

  size_t decoded_size = 0u;
  if (!hal_base64_decode(kMediaPngBase64, sizeof(kMediaPngBase64) - 1u, png,
                         png_size, &decoded_size) ||
      decoded_size != png_size) {
    derr("PNG asset Base64 decode failed");
    free(png);
    return false;
  }

  LodePNGState state;
  lodepng_state_init(&state);
  unsigned inspected_width = 0u;
  unsigned inspected_height = 0u;
  const unsigned inspect_error = lodepng_inspect(
      &inspected_width, &inspected_height, &state, png, png_size);
  lodepng_state_cleanup(&state);

  size_t pixels = 0u;
  if (inspect_error != 0u ||
      !checked_pixel_count(inspected_width, inspected_height, &pixels)) {
    derr("PNG asset dimensions rejected: %ux%u error=%u", inspected_width,
         inspected_height, inspect_error);
    free(png);
    return false;
  }

  unsigned char *rgba = NULL;
  unsigned width = 0u;
  unsigned height = 0u;
  const unsigned decode_error =
      lodepng_decode32(&rgba, &width, &height, png, png_size);
  free(png);
  if (decode_error != 0u || rgba == NULL || width != inspected_width ||
      height != inspected_height || !rgba8888ToRgb565(rgba, s_rgb565, pixels)) {
    derr("PNG asset decode failed: %u", decode_error);
    free(rgba);
    return false;
  }
  free(rgba);

  *out_width = width;
  *out_height = height;
  deb("PNG asset: %ux%u pixels=%lu", width, height, (unsigned long)pixels);
  return true;
}

static bool decode_jpeg_asset(unsigned *out_width, unsigned *out_height) {
  if (out_width == NULL || out_height == NULL) {
    return false;
  }
  *out_width = 0u;
  *out_height = 0u;

  size_t jpeg_size = 0u;
  if (!jpegBase64DecodedSize(kMediaJpegBase64, sizeof(kMediaJpegBase64) - 1u,
                             &jpeg_size) ||
      jpeg_size == 0u || jpeg_size > kMediaMaxEncodedBytes) {
    derr("JPEG asset size rejected");
    return false;
  }

  uint8_t *jpeg = (uint8_t *)malloc(jpeg_size);
  if (jpeg == NULL) {
    derr("JPEG asset allocation failed: %lu", (unsigned long)jpeg_size);
    return false;
  }

  size_t decoded_size = 0u;
  unsigned direct_width = 0u;
  unsigned direct_height = 0u;
  const bool direct_ok =
      hal_base64_decode(kMediaJpegBase64, sizeof(kMediaJpegBase64) - 1u, jpeg,
                        jpeg_size, &decoded_size) &&
      decoded_size == jpeg_size &&
      jpegDecodeRgb565(jpeg, decoded_size, s_rgb565, kMediaMaxPixels,
                       &direct_width, &direct_height);

  size_t direct_pixels = 0u;
  if (!direct_ok ||
      !checked_pixel_count(direct_width, direct_height, &direct_pixels)) {
    derr("JPEG direct decode failed or dimensions rejected");
    free(jpeg);
    return false;
  }
  const uint16_t direct_first = s_rgb565[0];

  memset(s_rgb565, 0, sizeof(s_rgb565));
  unsigned width = 0u;
  unsigned height = 0u;
  const bool helper_ok = jpegBase64DecodeRgb565(
      kMediaJpegBase64, sizeof(kMediaJpegBase64) - 1u, jpeg, jpeg_size,
      s_rgb565, kMediaMaxPixels, &width, &height);
  free(jpeg);

  size_t pixels = 0u;
  if (!helper_ok || !checked_pixel_count(width, height, &pixels) ||
      width != direct_width || height != direct_height) {
    derr("JPEG Base64 helper failed or disagreed with direct decode");
    return false;
  }

  *out_width = width;
  *out_height = height;
  deb("JPEG asset: %ux%u pixels=%lu first=%04X/%04X", width, height,
      (unsigned long)pixels, (unsigned)direct_first, (unsigned)s_rgb565[0]);
  return true;
}

static bool initialize_display(void) {
  const hal_status_t init_status =
      hal_display_init(kTftCsPin, kTftDcPin, kTftRstPin);
  if (init_status != HAL_OK) {
    derr("ILI9341 init failed: %s", hal_status_to_string(init_status));
    return false;
  }
  if (!hal_display_configure(kDisplayWidth, kDisplayHeight,
                             HAL_DISPLAY_ROTATION(0), HAL_DISPLAY_INVERT_OFF,
                             HAL_DISPLAY_COLOR_ORDER_RGB)) {
    derr("ILI9341 configure failed");
    return false;
  }
  return true;
}

static void draw_base_graphics(void) {
  hal_display_fill_screen(HAL_COLOR_BLACK);
  hal_display_fill_rect(0, 0, kDisplayWidth, 34, HAL_COLOR_BLUE);
  hal_display_set_default_font_with_pos_and_color(8, 12, HAL_COLOR_WHITE);
  hal_display_print("JaszczurHAL media");
  hal_display_draw_rect(8, 48, kDisplayWidth - 16, 104, HAL_COLOR_CYAN);
  hal_display_fill_round_rect(18, 58, 72, 34, 6, HAL_COLOR_GREEN);
  hal_display_fill_circle(172, 76, 18, HAL_COLOR_ORANGE);
  hal_display_draw_line(16, 112, kDisplayWidth - 16, 138, HAL_COLOR_PURPLE);
  hal_display_set_default_font_with_pos_and_color(34, 166, HAL_COLOR_WHITE);
  hal_display_print("PNG");
  hal_display_set_default_font_with_pos_and_color(134, 166, HAL_COLOR_WHITE);
  hal_display_print("JPEG");
}

static void draw_media_result(const char *name, bool decoded, int x, int y,
                              unsigned width, unsigned height) {
  if (!s_display_ready) {
    return;
  }
  if (!decoded) {
    hal_display_set_default_font_with_pos_and_color(x, y, HAL_COLOR_RED);
    hal_display_print(name);
    return;
  }
  if (!hal_display_draw_rgb_bitmap(x, y, s_rgb565, (int)width, (int)height)) {
    derr("%s rendering failed", name);
  }
}

void app_start(void) {
  debugInit();
  hal_deb_set_prefix("MEDIA");

  const bool round_trip_ok = png_memory_round_trip();
  s_display_ready = initialize_display();
  if (s_display_ready) {
    draw_base_graphics();
  }

  unsigned width = 0u;
  unsigned height = 0u;
  const bool png_ok = decode_png_asset(&width, &height);
  draw_media_result("PNG failed", png_ok, 42, 196, width, height);

  width = 0u;
  height = 0u;
  const bool jpeg_ok = decode_jpeg_asset(&width, &height);
  draw_media_result("JPEG failed", jpeg_ok, 142, 196, width, height);

  deb("display=%u png_round_trip=%u png=%u jpeg=%u", s_display_ready ? 1u : 0u,
      round_trip_ok ? 1u : 0u, png_ok ? 1u : 0u, jpeg_ok ? 1u : 0u);
}

void app_task0(void) { hal_delay_ms(1000u); }
