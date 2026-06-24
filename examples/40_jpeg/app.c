#include <hal/hal_app.h>
#include <hal/hal_system.h>
#include <tools_c.h>

#include <stdlib.h>
#include <string.h>

static const unsigned kImageMaxPixels = 24u * 24u;

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

static void demo_jpeg_decode(void) {
  size_t jpeg_size = 0u;
  if (!jpegBase64DecodedSize(kBase64JpegImage, sizeof(kBase64JpegImage) - 1u,
                             &jpeg_size)) {
    derr("jpeg base64 size failed");
    return;
  }

  uint8_t *jpeg_work = (uint8_t *)malloc(jpeg_size);
  if (jpeg_work == NULL) {
    derr("jpeg allocation failed size=%lu", (unsigned long)jpeg_size);
    return;
  }

  size_t decoded_size = 0u;
  uint16_t rgb565[kImageMaxPixels] = {};
  unsigned width = 0u;
  unsigned height = 0u;

  if (hal_base64_decode(kBase64JpegImage, sizeof(kBase64JpegImage) - 1u,
                        jpeg_work, jpeg_size, &decoded_size) &&
      jpegDecodeRgb565(jpeg_work, decoded_size, rgb565, kImageMaxPixels, &width,
                       &height)) {
    deb("jpegDecodeRgb565 width=%u height=%u first=0x%04X last=0x%04X\r\n",
        width, height, (unsigned)rgb565[0],
        (unsigned)rgb565[(width * height) - 1u]);
  } else {
    derr("jpegDecodeRgb565 failed\r\n");
  }

  memset(rgb565, 0, sizeof(rgb565));
  if (jpegBase64DecodeRgb565(kBase64JpegImage, sizeof(kBase64JpegImage) - 1u,
                             jpeg_work, jpeg_size, rgb565, kImageMaxPixels,
                             &width, &height)) {
    deb("jpegBase64DecodeRgb565 width=%u height=%u first=0x%04X\r\n", width,
        height, (unsigned)rgb565[0]);
  } else {
    derr("jpegBase64DecodeRgb565 failed\r\n");
  }

  free(jpeg_work);
}

void app_start(void) {
  debugInit();
  hal_deb_set_prefix("JPEG");
  demo_jpeg_decode();
}

void app_task0(void) { hal_delay_ms(1000u); }
