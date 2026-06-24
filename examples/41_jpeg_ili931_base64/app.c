#include <hal/hal_app.h>
#include <hal/hal_display.h>
#include <hal/hal_system.h>
#include <tools_c.h>

#include <stdlib.h>

static const uint8_t TFT_CS_PIN = 17;
static const uint8_t TFT_DC_PIN = 20;
static const uint8_t TFT_RST_PIN = 21;

static const int DISPLAY_WIDTH = 240;
static const int DISPLAY_HEIGHT = 320;
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

static void draw_base64_jpeg(void) {
  size_t jpeg_size = 0u;
  if (!jpegBase64DecodedSize(kBase64JpegImage, sizeof(kBase64JpegImage) - 1u,
                             &jpeg_size)) {
    derr("jpeg base64 size failed");
    return;
  }

  uint8_t *jpeg_work = (uint8_t *)malloc(jpeg_size);
  uint16_t *rgb565 = (uint16_t *)malloc(kImageMaxPixels * sizeof(uint16_t));
  if (jpeg_work == NULL || rgb565 == NULL) {
    derr("image allocation failed");
    free(rgb565);
    free(jpeg_work);
    return;
  }

  unsigned image_width = 0u;
  unsigned image_height = 0u;
  if (!jpegBase64DecodeRgb565(kBase64JpegImage, sizeof(kBase64JpegImage) - 1u,
                              jpeg_work, jpeg_size, rgb565, kImageMaxPixels,
                              &image_width, &image_height)) {
    derr("jpeg decode failed");
    free(rgb565);
    free(jpeg_work);
    return;
  }

  int x = (hal_display_get_width() - (int)image_width) / 2;
  int y = (hal_display_get_height() - (int)image_height) / 2;
  hal_display_fill_screen(HAL_COLOR_BLACK);
  hal_display_draw_rgb_bitmap(x, y, rgb565, (int)image_width,
                              (int)image_height);

  free(rgb565);
  free(jpeg_work);
}

void app_start(void) {
  debugInit();
  hal_deb_set_prefix("JPEG-DSP");

  hal_display_init(TFT_CS_PIN, TFT_DC_PIN, TFT_RST_PIN);
  if (!hal_display_configure(DISPLAY_WIDTH, DISPLAY_HEIGHT,
                             HAL_DISPLAY_ROTATION(0), HAL_DISPLAY_INVERT_OFF,
                             HAL_DISPLAY_COLOR_ORDER_RGB)) {
    derr("display configure failed");
    return;
  }

  draw_base64_jpeg();
}

void app_task0(void) { hal_delay_ms(1000u); }
