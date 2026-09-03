#include "tools.h"
#include "utils/unity.h"

#include <stdlib.h>
#include <string.h>

#ifndef HAL_ENABLE_PNG
#error "test_lodepng requires HAL_ENABLE_PNG"
#endif

#ifndef HAL_ENABLE_PNG_AS_BASE64
#error "test_lodepng requires HAL_ENABLE_PNG_AS_BASE64"
#endif

#ifdef LODEPNG_COMPILE_DISK
#error "JaszczurHAL default LodePNG profile must keep disk helpers disabled"
#endif

#ifdef LODEPNG_COMPILE_CPP
#error "JaszczurHAL default LodePNG profile must keep the C++ wrapper disabled"
#endif

void setUp(void) {}
void tearDown(void) {}

static const unsigned char kPixelsRgba[] = {
    255, 0, 0, 255, 0, 255, 0, 255, 0, 0, 255, 255, 255, 255, 255, 128,
};

typedef struct {
  unsigned char *png;
  size_t png_size;
  char *base64;
  size_t base64_len;
  uint8_t *png_work;
} png_base64_fixture_t;

static void png_base64_fixture_create(png_base64_fixture_t *fixture) {
  unsigned error =
      lodepng_encode32(&fixture->png, &fixture->png_size, kPixelsRgba, 2u, 2u);
  TEST_ASSERT_EQUAL_UINT(0u, error);
  TEST_ASSERT_NOT_NULL(fixture->png);

  const size_t base64_size = hal_base64_encoded_len(fixture->png_size) + 1u;
  fixture->base64 = (char *)malloc(base64_size);
  fixture->png_work = (uint8_t *)malloc(fixture->png_size);
  TEST_ASSERT_NOT_NULL(fixture->base64);
  TEST_ASSERT_NOT_NULL(fixture->png_work);
  TEST_ASSERT_TRUE(hal_base64_encode(fixture->png, fixture->png_size,
                                     fixture->base64, base64_size,
                                     &fixture->base64_len));
}

static void png_base64_fixture_destroy(png_base64_fixture_t *fixture) {
  free(fixture->png_work);
  free(fixture->base64);
  free(fixture->png);
}

void test_lodepng_encode_decode_rgba_memory(void) {
  unsigned char *png = NULL;
  size_t png_size = 0u;
  unsigned error = lodepng_encode32(&png, &png_size, kPixelsRgba, 2u, 2u);

  TEST_ASSERT_EQUAL_UINT(0u, error);
  TEST_ASSERT_NOT_NULL(png);
  TEST_ASSERT_GREATER_THAN_UINT(0u, png_size);

  unsigned char *decoded = NULL;
  unsigned width = 0u;
  unsigned height = 0u;
  error = lodepng_decode32(&decoded, &width, &height, png, png_size);

  TEST_ASSERT_EQUAL_UINT(0u, error);
  TEST_ASSERT_EQUAL_UINT(2u, width);
  TEST_ASSERT_EQUAL_UINT(2u, height);
  TEST_ASSERT_NOT_NULL(decoded);
  TEST_ASSERT_EQUAL_MEMORY(kPixelsRgba, decoded, sizeof(kPixelsRgba));

  free(decoded);
  free(png);
}

void test_pngBase64Decode32_decodes_rgba_memory(void) {
  png_base64_fixture_t fixture = {};
  png_base64_fixture_create(&fixture);

  unsigned char *decoded = NULL;
  unsigned width = 0u;
  unsigned height = 0u;
  unsigned png_error = 1234u;
  TEST_ASSERT_TRUE(hal_image_png_base64_decode_rgba8888(
      &decoded, &width, &height, fixture.base64, fixture.base64_len,
      fixture.png_work, fixture.png_size, &png_error));
  TEST_ASSERT_EQUAL_UINT(0u, png_error);
  TEST_ASSERT_EQUAL_UINT(2u, width);
  TEST_ASSERT_EQUAL_UINT(2u, height);
  TEST_ASSERT_NOT_NULL(decoded);
  TEST_ASSERT_EQUAL_MEMORY(kPixelsRgba, decoded, sizeof(kPixelsRgba));

  free(decoded);
  png_base64_fixture_destroy(&fixture);
}

void test_pngBase64DecodedSize_reports_exact_png_size(void) {
  png_base64_fixture_t fixture = {};
  png_base64_fixture_create(&fixture);

  size_t decoded_size = 0u;
  TEST_ASSERT_TRUE(hal_image_png_base64_decoded_size(
      fixture.base64, fixture.base64_len, &decoded_size));
  TEST_ASSERT_EQUAL_UINT32((uint32_t)fixture.png_size, (uint32_t)decoded_size);

  TEST_ASSERT_FALSE(
      hal_image_png_base64_decoded_size("AA*A", 4u, &decoded_size));
  TEST_ASSERT_EQUAL_UINT32(0u, (uint32_t)decoded_size);

  png_base64_fixture_destroy(&fixture);
}

void test_pngBase64DecodeRgb565_decodes_to_rgb565(void) {
  png_base64_fixture_t fixture = {};
  png_base64_fixture_create(&fixture);

  unsigned short rgb565[4] = {0};
  unsigned width = 0u;
  unsigned height = 0u;
  unsigned png_error = 1234u;
  TEST_ASSERT_TRUE(hal_image_png_base64_decode_rgb565(
      fixture.base64, fixture.base64_len, fixture.png_work, fixture.png_size,
      rgb565, 4u, &width, &height, &png_error));
  TEST_ASSERT_EQUAL_UINT(0u, png_error);
  TEST_ASSERT_EQUAL_UINT(2u, width);
  TEST_ASSERT_EQUAL_UINT(2u, height);
  TEST_ASSERT_EQUAL_HEX16(0xF800, rgb565[0]);
  TEST_ASSERT_EQUAL_HEX16(0x07E0, rgb565[1]);
  TEST_ASSERT_EQUAL_HEX16(0x001F, rgb565[2]);
  TEST_ASSERT_EQUAL_HEX16(0xFFFF, rgb565[3]);

  png_base64_fixture_destroy(&fixture);
}

void test_pngBase64DecodeRgb565_rejects_small_output_buffer(void) {
  png_base64_fixture_t fixture = {};
  png_base64_fixture_create(&fixture);

  unsigned short rgb565[3] = {0};
  unsigned width = 0u;
  unsigned height = 0u;
  unsigned png_error = 1234u;
  TEST_ASSERT_FALSE(hal_image_png_base64_decode_rgb565(
      fixture.base64, fixture.base64_len, fixture.png_work, fixture.png_size,
      rgb565, 3u, &width, &height, &png_error));
  TEST_ASSERT_EQUAL_UINT(0u, png_error);
  TEST_ASSERT_EQUAL_UINT(2u, width);
  TEST_ASSERT_EQUAL_UINT(2u, height);

  png_base64_fixture_destroy(&fixture);
}

void test_pngBase64Decode32_rejects_invalid_base64_before_png_decode(void) {
  uint8_t png_work[8] = {0};
  unsigned char *decoded = (unsigned char *)1;
  unsigned width = 7u;
  unsigned height = 9u;
  unsigned png_error = 1234u;

  TEST_ASSERT_FALSE(hal_image_png_base64_decode_rgba8888(
      &decoded, &width, &height, "AA*A", 4u, png_work, sizeof(png_work),
      &png_error));
  TEST_ASSERT_NULL(decoded);
  TEST_ASSERT_EQUAL_UINT(0u, width);
  TEST_ASSERT_EQUAL_UINT(0u, height);
  TEST_ASSERT_EQUAL_UINT(0u, png_error);
}

void test_lodepng_error_text_available(void) {
  const char *text = lodepng_error_text(48u);

  TEST_ASSERT_NOT_NULL(text);
  TEST_ASSERT_TRUE(strlen(text) > 0u);
}

int main(void) {
  UNITY_BEGIN();
  RUN_TEST(test_lodepng_encode_decode_rgba_memory);
  RUN_TEST(test_pngBase64Decode32_decodes_rgba_memory);
  RUN_TEST(test_pngBase64DecodedSize_reports_exact_png_size);
  RUN_TEST(test_pngBase64DecodeRgb565_decodes_to_rgb565);
  RUN_TEST(test_pngBase64DecodeRgb565_rejects_small_output_buffer);
  RUN_TEST(test_pngBase64Decode32_rejects_invalid_base64_before_png_decode);
  RUN_TEST(test_lodepng_error_text_available);
  return UNITY_END();
}
