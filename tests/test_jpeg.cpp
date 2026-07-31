#include "tools.h"
#include "utils/unity.h"

#include <stdlib.h>

#ifndef HAL_ENABLE_JPEG
#error "test_jpeg requires HAL_ENABLE_JPEG"
#endif

#ifndef HAL_ENABLE_JPEG_AS_BASE64
#error "test_jpeg requires HAL_ENABLE_JPEG_AS_BASE64"
#endif

void setUp(void) {}
void tearDown(void) {}

static const char kBase64JpegImage[] =
    "/9j/4AAQSkZJRgABAQAAAQABAAD/"
    "2wBDAAUDBAQEAwUEBAQFBQUGBwwIBwcHBw8LCwkMEQ8SEhEP"
    "ERETFhwXExQaFRERGCEYGh0dHx8fExciJCIeJBweHx7/"
    "2wBDAQUFBQcGBw4ICA4eFBEUHh4eHh4e"
    "Hh4eHh4eHh4eHh4eHh4eHh4eHh4eHh4eHh4eHh4eHh4eHh4eHh4eHh4eHh7/"
    "wAARCAAYABgDASIA"
    "AhEBAxEB/8QAHwAAAQUBAQEBAQEAAAAAAAAAAAECAwQFBgcICQoL/"
    "8QAtRAAAgEDAwIEAwUFBAQA"
    "AAF9AQIDAAQRBRIhMUEGE1FhByJxFDKBkaEII0KxwRVS0fAkM2JyggkKFhcYGRolJicoKSo0NT"
    "Y3"
    "ODk6Q0RFRkdISUpTVFVWV1hZWmNkZWZnaGlqc3R1dnd4eXqDhIWGh4iJipKTlJWWl5iZmqKjpK"
    "Wm"
    "p6ipqrKztLW2t7i5usLDxMXGx8jJytLT1NXW19jZ2uHi4+Tl5ufo6erx8vP09fb3+Pn6/"
    "8QAHwEA"
    "AwEBAQEBAQEBAQAAAAAAAAECAwQFBgcICQoL/"
    "8QAtREAAgECBAQDBAcFBAQAAQJ3AAECAxEEBSEx"
    "BhJBUQdhcRMiMoEIFEKRobHBCSMzUvAVYnLRChYkNOEl8RcYGRomJygpKjU2Nzg5OkNERUZHSE"
    "lK"
    "U1RVVldYWVpjZGVmZ2hpanN0dXZ3eHl6goOEhYaHiImKkpOUlZaXmJmaoqOkpaanqKmqsrO0tb"
    "a3"
    "uLm6wsPExcbHyMnK0tPU1dbX2Nna4uPk5ebn6Onq8vP09fb3+Pn6/"
    "9oADAMBAAIRAxEAPwDjPBnh"
    "Twh/wgT+JfEz65u/tQ2KJpzRf88g4JDj/e5z6cVt6To/wzvbuK1soPG0s0hwif6Nz/"
    "gPftVrR9Ln"
    "vPhBFY20LSyyeJMqg7/6N/"
    "nmp7a3ufD14qpGsNzEACQColAP4FlJX8ah1ZzlOTqTvztJJ2SSt5Hd"
    "k8niYShSnGM1DmXN1LnjTwJoOiaBFqGlS6i8rXYgZbl0ZdpVjkbVHPyjv/"
    "SitTbPefDx5bl2lmk1"
    "rezN1P7nFFenldWfJOM5N2k1r8j6HK8bXp4WEcRPmnbVrRf1/"
    "WmweAdb0zSfDz6bew3xka7M4aBV"
    "K4KquDlhz8p7VbMPhK6uHuJW8QySucszmHmiiuN5fHnlKM5K7voz8fyrGyupOEb2SvbWyNCf+"
    "y/7"
    "CXTNMS92/avPJuAv93b/AA/hRRRXRhaEcNFxi27u+uruz7qjiZzgmf/Z";

void test_jpeg_base64_decode_rgb565(void) {
  size_t jpeg_size = 0u;
  TEST_ASSERT_TRUE(jpegBase64DecodedSize(
      kBase64JpegImage, sizeof(kBase64JpegImage) - 1u, &jpeg_size));
  TEST_ASSERT_EQUAL_UINT(954u, jpeg_size);

  uint8_t *jpeg_work = (uint8_t *)malloc(jpeg_size);
  TEST_ASSERT_NOT_NULL(jpeg_work);

  unsigned short rgb565[24u * 24u] = {};
  unsigned width = 0u;
  unsigned height = 0u;
  TEST_ASSERT_TRUE(jpegBase64DecodeRgb565(
      kBase64JpegImage, sizeof(kBase64JpegImage) - 1u, jpeg_work, jpeg_size,
      rgb565, 24u * 24u, &width, &height));
  TEST_ASSERT_EQUAL_UINT(24u, width);
  TEST_ASSERT_EQUAL_UINT(24u, height);
  TEST_ASSERT_NOT_EQUAL_UINT16(0u, rgb565[0]);

  free(jpeg_work);
}

void test_jpeg_decode_rejects_too_small_output(void) {
  size_t jpeg_size = 0u;
  TEST_ASSERT_TRUE(jpegBase64DecodedSize(
      kBase64JpegImage, sizeof(kBase64JpegImage) - 1u, &jpeg_size));

  uint8_t *jpeg_work = (uint8_t *)malloc(jpeg_size);
  TEST_ASSERT_NOT_NULL(jpeg_work);

  size_t decoded_size = 0u;
  TEST_ASSERT_TRUE(hal_base64_decode(kBase64JpegImage,
                                     sizeof(kBase64JpegImage) - 1u, jpeg_work,
                                     jpeg_size, &decoded_size));

  unsigned short rgb565[4] = {};
  unsigned width = 123u;
  unsigned height = 456u;
  TEST_ASSERT_FALSE(
      jpegDecodeRgb565(jpeg_work, decoded_size, rgb565, 4u, &width, &height));
  TEST_ASSERT_EQUAL_UINT(0u, width);
  TEST_ASSERT_EQUAL_UINT(0u, height);

  free(jpeg_work);
}

void test_jpeg_decode_rejects_invalid_input(void) {
  static const uint8_t invalid_jpeg[] = {0xFFu, 0xD8u, 0x00u, 0x01u};
  unsigned short rgb565[4] = {};
  unsigned width = 123u;
  unsigned height = 456u;

  TEST_ASSERT_FALSE(jpegDecodeRgb565(invalid_jpeg, sizeof(invalid_jpeg), rgb565,
                                     4u, &width, &height));
  TEST_ASSERT_EQUAL_UINT(0u, width);
  TEST_ASSERT_EQUAL_UINT(0u, height);
}

int main(void) {
  UNITY_BEGIN();
  RUN_TEST(test_jpeg_base64_decode_rgb565);
  RUN_TEST(test_jpeg_decode_rejects_too_small_output);
  RUN_TEST(test_jpeg_decode_rejects_invalid_input);
  return UNITY_END();
}
