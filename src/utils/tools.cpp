//
//  utils.c
//  Index
//
//  Created by Marcin Kielesiński on 07/12/2019.
//

#include "tools.h"

#include "hal/hal_time.h"

#include <math.h>
#include <stdlib.h>

#ifdef HAL_ENABLE_PNG_AS_BASE64
#include "hal/impl/shared/frameworks/lodepng/lodepng.h"
#endif

#ifdef HAL_ENABLE_JPEG
#include "hal/impl/shared/frameworks/jpeg/tjpgd.h"
#endif

void debugInit(void) { hal_debug_init(HAL_DEBUG_DEFAULT_BAUD); }

void setDebugPrefixWithColon(const char *moduleName) {
  char prefix[HAL_DEBUG_PREFIX_SIZE] = {};
  size_t prefixLen = 0;

  if (moduleName == NULL) {
    return;
  }

  while (prefixLen < (sizeof(prefix) - 2) && moduleName[prefixLen] != '\0') {
    prefix[prefixLen] = moduleName[prefixLen];
    prefixLen++;
  }

  prefix[prefixLen++] = ':';
  prefix[prefixLen] = '\0';

  hal_deb_set_prefix(prefix);
}

char *printBinaryAndSize(int number, char *buf, size_t bufSize) {
  if (buf == NULL || bufSize == 0) {
    return buf;
  }

  unsigned int bits = 0;

  if (number < 0) {
    bits = sizeof(int) * 8;
  } else if (number <= 0xFF) {
    bits = 8;
  } else if (number <= 0xFFFF) {
    bits = 16;
  } else {
    bits = 32;
  }

  if (bits >= bufSize)
    bits = bufSize - 1;
  memset(buf, 0, bufSize);

  for (int i = bits - 1; i >= 0; i--) {
    buf[bits - 1 - i] = (number & (1 << i)) ? '1' : '0';
  }

  buf[bits] = '\0';
  return buf;
}

void floatToDec(float val, int *hi, int *lo) {
  int t1 = (int)val;
  if (t1 > -128) {
    if (hi != NULL) {
      *hi = t1;
    }
    int t2 = (int)(((float)val - t1) * 10);
    if (lo != NULL) {
      if (t2 >= 0) {
        *lo = t2;
      } else {
        *lo = 0;
      }
    }
  }
}

float decToFloat(int hi, int lo) { return (float)hi + ((float)lo / 10); }

float adcToVolt(int adc, float r1, float r2) {
  const float V_REF = 3.3;
  const float V_DIVIDER_SCALE = (r1 + r2) / r2;

  return adc * (V_REF / pow(2, HAL_TOOLS_ADC_BITS)) * V_DIVIDER_SCALE;
}

float getAverageValueFrom(int tpin) {

  uint8_t i;
  float average = 0;

  // Dummy read - RP2040 ADC mux cross-talk fix: first read after channel
  // switch carries residual charge from the previous channel.
  (void)hal_adc_read(tpin);

  // take N samples in a row, with a slight delay
  for (i = 0; i < HAL_TOOLS_NUMSAMPLES; i++) {
    average += adcCompe(hal_adc_read(tpin));
    m_delay_microseconds(10);
  }
  average /= HAL_TOOLS_NUMSAMPLES;

  return average;
}

float filter(float alpha, float input, float previous_output) {
  return alpha * input + (1.0f - alpha) * previous_output;
}

int adcCompe(int x) {
  int y = 0;

  if (x > 3584)
    y = x + 32;
  else if (x == 3583)
    y = x + 29;
  else if (x == 3582)
    y = x + 27;

  else if (x > 2560)
    y = x + 24;
  else if (x == 2559)
    y = x + 21;
  else if (x == 2558)
    y = x + 19;

  else if (x > 1536)
    y = x + 16;
  else if (x == 1535)
    y = x + 13;
  else if (x == 1534)
    y = x + 11;

  else if (x > 512)
    y = x + 8;
  else if (x == 511)
    y = x + 5;
  else if (x == 510)
    y = x + 3;
  else
    y = x;
  return y;
}

float getAverageForTable(int *idx, int *overall, float val, float *table) {

  table[(*idx)++] = val;
  if (*idx > HAL_TOOLS_TEMPERATURE_TABLES_SIZE - 1) {
    *idx = 0;
  }
  (*overall)++;
  if (*overall >= HAL_TOOLS_TEMPERATURE_TABLES_SIZE) {
    *overall = HAL_TOOLS_TEMPERATURE_TABLES_SIZE;
  }

  float average = 0;
  for (int i = 0; i < *overall; i++) {
    average += table[i];
  }
  average /= *overall;
  return average;
}

int getAverageFrom(int *table, int size) {
  int average = 0;
  if (size > 0) {
    for (int i = 0; i < size; i++) {
      average += table[i];
    }
    average /= size;
    if (average < 0) {
      average = 0;
    }
  }
  return average;
}

int getMinimumFrom(int *table, int size) {
  if (size <= 0) {
    return -1;
  }

  int min = table[0];
  for (int i = 1; i < size; ++i) {
    if (table[i] < min) {
      min = table[i];
    }
  }
  return min;
}

int getHalfwayBetweenMinMax(int *array, int n) {
  if (n <= 0) {
    return -1;
  }

  int min = array[0];
  int max = array[0];

  for (int i = 1; i < n; ++i) {
    if (array[i] < min) {
      min = array[i];
    } else if (array[i] > max) {
      max = array[i];
    }
  }

  return (max + min) / 2;
}

float ntcToTemp(int tpin, int thermistor, int r) {
  float average = getAverageValueFrom(tpin);
  // adcCompe() can return values above ADC_MAXVALUE (RP2040 DNL fix adds up to
  // +32). Clamp to ADC_MAXVALUE-1 to prevent (MAXVALUE/average - 1) going to
  // zero or negative, which would produce NaN via log(negative) in steinhart().
  if (average >= (float)HAL_TOOLS_ADC_MAXVALUE) {
    average = (float)(HAL_TOOLS_ADC_MAXVALUE - 1);
  }
  if (average <= 0.0f) {
    average = 1.0f;
  }
  // convert the value to resistance
  average = HAL_TOOLS_ADC_MAXVALUE / average - 1;
  return steinhart(average, thermistor, r, true);
}

float steinhart(float val, float thermistor, int r, bool characteristic) {
  val = r / val;
  float steinhart_val = val / thermistor;  // (R/Ro)
  steinhart_val = log(steinhart_val);      // ln(R/Ro)
  steinhart_val /= HAL_TOOLS_BCOEFFICIENT; // 1/B * ln(R/Ro)
  float invTo = 1.0 / (HAL_TOOLS_TEMPERATURENOMINAL + 273.15);
  if (characteristic) {
    steinhart_val += invTo;              // + (1/To)
    steinhart_val = 1.0 / steinhart_val; // Invert
    steinhart_val -= 273.15;             // convert absolute temp to C
  } else {
    steinhart_val -= invTo;              // - (1/To)
    steinhart_val = 1.0 / steinhart_val; // Invert
    steinhart_val += 273.15;             // convert absolute temp to C
    steinhart_val = -steinhart_val;
  }

  return steinhart_val;
}

int percentToGivenVal(float percent, int givenVal) {
  return int(((percent / 100.0) * givenVal));
}

int percentFrom(int givenVal, int maxVal) {
  if (maxVal == 0)
    return 0;
  return (givenVal * 100) / maxVal;
}

unsigned long getSeconds(void) { return ((hal_millis() + 500) / 1000); }

bool isDaylightSavingTime(int year, int month, int day) {
  return hal_time_is_daylight_saving_time(year, month, day);
}

void adjustTime(int *year, int *month, int *day, int *hour, int *minute) {
  hal_time_adjust_cet_cest(year, month, day, hour, minute);
}

uint8_t MSB(unsigned short value) { return (uint8_t)(value >> 8) & 0xFF; }

uint8_t LSB(unsigned short value) { return (uint8_t)(value & 0x00FF); }

int MsbLsbToInt(uint8_t msb, uint8_t lsb) {
  return ((unsigned short)msb << 8) | lsb;
}

unsigned short byteArrayToWord(unsigned char *bytes) {
  unsigned short word = ((unsigned short)bytes[0] << 8) | bytes[1];
  return word;
}

void wordToByteArray(unsigned short word, unsigned char *bytes) {
  bytes[0] = MSB(word);
  bytes[1] = LSB(word);
}

float rroundf(float val) { return roundf(val * 10.0f) / 10.0f; }

float roundfWithPrecisionTo(float value, int precision) {
  float multiplier = 1.0;
  for (int i = 0; i < precision; ++i) {
    multiplier *= 10.0;
  }

  return roundf(value * multiplier) / multiplier;
}

bool concatStrings(char *dest, size_t destSize, const char *src1,
                   const char *src2) {
  size_t len1;
  size_t len2;

  if (dest == NULL || src1 == NULL || src2 == NULL) {
    return false;
  }

  len1 = strlen(src1);
  len2 = strlen(src2);

  if (destSize == 0) {
    return false;
  }

  if ((len1 + len2 + 1) > destSize) {
    return false;
  }

  while ((*dest++ = *src1++) != '\0') {
  }

  --dest;

  while ((*dest++ = *src2++) != '\0') {
  }

  return true;
}

bool isValidString(const char *s, int maxBufSize) {
  if (s == NULL || maxBufSize <= 0) {
    return false;
  }

  if (*s == '\0') {
    return false;
  }

  for (int a = 0; a < maxBufSize; a++) {
    if (s[a] == '\0') {
      return true;
    }

    bool p = (isdigit(s[a]) || isalpha(s[a]) || isspace(s[a]) || isgraph(s[a]));
    if (!p) {
      return false;
    }
  }
  return false;
}

unsigned short rgbToRgb565(unsigned char r, unsigned char g, unsigned char b) {
  unsigned short r5 = (r >> 3) & 0x1F;
  unsigned short g6 = (g >> 2) & 0x3F;
  unsigned short b5 = (b >> 3) & 0x1F;

  return (r5 << 11) | (g6 << 5) | b5;
}

bool rgb888ToRgb565(const unsigned char *rgb, unsigned short *rgb565,
                    size_t pixelCount) {
  if (rgb == NULL || rgb565 == NULL) {
    return false;
  }

  for (size_t i = 0; i < pixelCount; ++i) {
    const size_t src = i * 3u;
    rgb565[i] = rgbToRgb565(rgb[src], rgb[src + 1u], rgb[src + 2u]);
  }

  return true;
}

bool rgba8888ToRgb565(const unsigned char *rgba, unsigned short *rgb565,
                      size_t pixelCount) {
  if (rgba == NULL || rgb565 == NULL) {
    return false;
  }

  for (size_t i = 0; i < pixelCount; ++i) {
    const size_t src = i * 4u;
    rgb565[i] = rgbToRgb565(rgba[src], rgba[src + 1u], rgba[src + 2u]);
  }

  return true;
}

#if defined(HAL_ENABLE_PNG_AS_BASE64) || defined(HAL_ENABLE_JPEG)
static bool hal_tools_mul_size(size_t a, size_t b, size_t *out) {
  if (out == NULL) {
    return false;
  }

  if (a != 0u && b > (((size_t)-1) / a)) {
    *out = 0u;
    return false;
  }

  *out = a * b;
  return true;
}
#endif

#ifdef HAL_ENABLE_PNG_AS_BASE64
bool pngBase64DecodedSize(const char *base64, size_t base64Len,
                          size_t *pngSize) {
  if (pngSize != NULL) {
    *pngSize = 0u;
  }

  if (pngSize == NULL || (base64 == NULL && base64Len != 0u)) {
    return false;
  }

  return hal_base64_decode(base64, base64Len, NULL, 0u, pngSize);
}

bool pngBase64Decode32(unsigned char **rgba, unsigned *width, unsigned *height,
                       const char *base64, size_t base64Len, uint8_t *pngWork,
                       size_t pngWorkSize, unsigned *pngError) {
  if (pngError != NULL) {
    *pngError = 0u;
  }
  if (rgba != NULL) {
    *rgba = NULL;
  }
  if (width != NULL) {
    *width = 0u;
  }
  if (height != NULL) {
    *height = 0u;
  }

  if (rgba == NULL || width == NULL || height == NULL || base64 == NULL ||
      pngWork == NULL) {
    return false;
  }

  size_t pngSize = 0u;
  if (!hal_base64_decode(base64, base64Len, pngWork, pngWorkSize, &pngSize)) {
    return false;
  }

  unsigned error = lodepng_decode32(rgba, width, height, pngWork, pngSize);
  if (pngError != NULL) {
    *pngError = error;
  }

  if (error != 0u) {
    if (*rgba != NULL) {
      free(*rgba);
      *rgba = NULL;
    }
    *width = 0u;
    *height = 0u;
    return false;
  }

  return true;
}

bool pngBase64DecodeRgb565(const char *base64, size_t base64Len,
                           uint8_t *pngWork, size_t pngWorkSize,
                           unsigned short *rgb565, size_t rgb565Pixels,
                           unsigned *width, unsigned *height,
                           unsigned *pngError) {
  if (pngError != NULL) {
    *pngError = 0u;
  }
  if (width != NULL) {
    *width = 0u;
  }
  if (height != NULL) {
    *height = 0u;
  }

  if (rgb565 == NULL || width == NULL || height == NULL) {
    return false;
  }

  unsigned char *rgba = NULL;
  unsigned decodedWidth = 0u;
  unsigned decodedHeight = 0u;
  if (!pngBase64Decode32(&rgba, &decodedWidth, &decodedHeight, base64,
                         base64Len, pngWork, pngWorkSize, pngError)) {
    return false;
  }

  *width = decodedWidth;
  *height = decodedHeight;

  size_t pixels = 0u;
  bool ok = hal_tools_mul_size((size_t)decodedWidth, (size_t)decodedHeight,
                               &pixels) &&
            pixels <= rgb565Pixels && rgba8888ToRgb565(rgba, rgb565, pixels);

  free(rgba);
  return ok;
}
#endif

#ifdef HAL_ENABLE_JPEG
typedef struct {
  const uint8_t *input;
  size_t input_size;
  size_t input_offset;
  uint16_t *output;
  size_t output_pixels;
  size_t output_width;
  bool output_valid;
} hal_tools_jpeg_context_t;

static size_t hal_tools_jpeg_input(JDEC *decoder, uint8_t *buffer,
                                   size_t length) {
  hal_tools_jpeg_context_t *context =
      (hal_tools_jpeg_context_t *)decoder->device;
  if (context == NULL || context->input_offset > context->input_size) {
    return 0u;
  }

  const size_t remaining = context->input_size - context->input_offset;
  if (length > remaining) {
    length = remaining;
  }
  if (buffer != NULL && length != 0u) {
    memcpy(buffer, context->input + context->input_offset, length);
  }
  context->input_offset += length;
  return length;
}

static int hal_tools_jpeg_output(JDEC *decoder, void *bitmap,
                                 JRECT *rectangle) {
  hal_tools_jpeg_context_t *context =
      (hal_tools_jpeg_context_t *)decoder->device;
  if (context == NULL || bitmap == NULL || rectangle == NULL ||
      rectangle->right < rectangle->left ||
      rectangle->bottom < rectangle->top) {
    return 0;
  }

  const size_t block_width =
      (size_t)rectangle->right - (size_t)rectangle->left + 1u;
  const size_t block_height =
      (size_t)rectangle->bottom - (size_t)rectangle->top + 1u;
  const uint16_t *source = (const uint16_t *)bitmap;

  for (size_t row = 0u; row < block_height; ++row) {
    const size_t output_row = (size_t)rectangle->top + row;
    size_t output_offset = 0u;
    if (!hal_tools_mul_size(output_row, context->output_width,
                            &output_offset) ||
        output_offset > context->output_pixels ||
        (size_t)rectangle->left > context->output_pixels - output_offset ||
        block_width >
            context->output_pixels - output_offset - (size_t)rectangle->left) {
      context->output_valid = false;
      return 0;
    }
    output_offset += (size_t)rectangle->left;
    memcpy(context->output + output_offset, source + (row * block_width),
           block_width * sizeof(*source));
  }

  return 1;
}

bool jpegDecodeRgb565(const uint8_t *jpeg, size_t jpegSize,
                      unsigned short *rgb565, size_t rgb565Pixels,
                      unsigned *width, unsigned *height) {
  if (width != NULL) {
    *width = 0u;
  }
  if (height != NULL) {
    *height = 0u;
  }

  if (jpeg == NULL || jpegSize == 0u || rgb565 == NULL || width == NULL ||
      height == NULL) {
    return false;
  }

  void *workspace = malloc(TJPGD_WORKSPACE_SIZE);
  if (workspace == NULL) {
    return false;
  }

  hal_tools_jpeg_context_t context = {};
  context.input = jpeg;
  context.input_size = jpegSize;
  context.output = rgb565;
  context.output_pixels = rgb565Pixels;
  context.output_valid = true;

  JDEC decoder = {};
  decoder.swap = 0u;
  JRESULT result = jd_prepare(&decoder, hal_tools_jpeg_input, workspace,
                              TJPGD_WORKSPACE_SIZE, &context);
  if (result != JDR_OK) {
    free(workspace);
    return false;
  }

  size_t pixels = 0u;
  if (!hal_tools_mul_size((size_t)decoder.width, (size_t)decoder.height,
                          &pixels) ||
      pixels > rgb565Pixels) {
    free(workspace);
    return false;
  }

  context.output_width = (size_t)decoder.width;
  result = jd_decomp(&decoder, hal_tools_jpeg_output, 0u);
  free(workspace);
  if (result != JDR_OK || !context.output_valid) {
    return false;
  }

  *width = (unsigned)decoder.width;
  *height = (unsigned)decoder.height;
  return true;
}
#endif

#ifdef HAL_ENABLE_JPEG_AS_BASE64
bool jpegBase64DecodedSize(const char *base64, size_t base64Len,
                           size_t *jpegSize) {
  if (jpegSize != NULL) {
    *jpegSize = 0u;
  }

  if (jpegSize == NULL || (base64 == NULL && base64Len != 0u)) {
    return false;
  }

  return hal_base64_decode(base64, base64Len, NULL, 0u, jpegSize);
}

bool jpegBase64DecodeRgb565(const char *base64, size_t base64Len,
                            uint8_t *jpegWork, size_t jpegWorkSize,
                            unsigned short *rgb565, size_t rgb565Pixels,
                            unsigned *width, unsigned *height) {
  if (width != NULL) {
    *width = 0u;
  }
  if (height != NULL) {
    *height = 0u;
  }

  if (base64 == NULL || jpegWork == NULL || rgb565 == NULL || width == NULL ||
      height == NULL) {
    return false;
  }

  size_t jpegSize = 0u;
  if (!hal_base64_decode(base64, base64Len, jpegWork, jpegWorkSize,
                         &jpegSize)) {
    return false;
  }

  return jpegDecodeRgb565(jpegWork, jpegSize, rgb565, rgb565Pixels, width,
                          height);
}
#endif

const char *macToString(uint8_t mac[6], char *buf, size_t bufSize) {
  snprintf(buf, bufSize, "%02X:%02X:%02X:%02X:%02X:%02X", mac[0], mac[1],
           mac[2], mac[3], mac[4], mac[5]);
  return buf;
}

const char *encToString(uint8_t enc) {
#ifdef HAL_ENABLE_WIFI
  return hal_wifi_encryption_to_string((hal_wifi_encryption_t)enc);
#else
  (void)enc;
  return "UNKN";
#endif
}

char hexToChar(char high, char low) {
  int hi = isdigit(high) ? high - '0' : toupper(high) - 'A' + 10;
  int lo = isdigit(low) ? low - '0' : toupper(low) - 'A' + 10;
  return (char)((hi << 4) | lo);
}

void urlDecode(const char *src, char *dst) {
  while (*src) {
    if (*src == '%') {
      if (isxdigit(src[1]) && isxdigit(src[2])) {
        *dst++ = hexToChar(src[1], src[2]);
        src += 3;
      } else {
        *dst++ = *src++; // copy as-is for malformed percent-encoding
      }
    } else if (*src == '+') {
      *dst++ = ' ';
      src++;
    } else {
      *dst++ = *src++;
    }
  }
  *dst = '\0';
}

bool scanNetworks(const char *networkToFind) {
  bool networkFound = false;
#ifdef HAL_ENABLE_WIFI
  deb("Beginning scan at %lu\n", hal_millis());
  int cnt = hal_wifi_scan_networks();
  if (cnt < 0) {
    deb("WiFi scan failed");
    return false;
  }
  if (cnt == 0) {
    deb("No WiFi networks found");
  } else {
    deb("Found %d networks\n", cnt);
    deb("%32s %5s %17s %2s %4s", "SSID", "ENC", "BSSID        ", "CH", "RSSI");
    for (int i = 0; i < cnt; i++) {
      hal_wifi_scan_result_t network;
      if (!hal_wifi_get_scan_result((size_t)i, &network)) {
        continue;
      }
      char macBuf[20];
      deb("%32s %5s %17s %2d %4ld", network.ssid,
          hal_wifi_encryption_to_string(network.encryption),
          macToString(network.bssid, macBuf, sizeof(macBuf)),
          (int)network.channel, (long)network.rssi);

      if (networkToFind != NULL && strlen(networkToFind) > 0) {
        if (!strncmp(network.ssid, networkToFind, strlen(networkToFind))) {
          deb("network %s is available", networkToFind);
          networkFound = true;
        }
      }
    }
  }
  deb("\n--- END --- at %lu\n", hal_millis());
#else
  (void)networkToFind;
  deb("HAL WiFi disabled");
#endif
  return networkFound;
}

float mapfloat(float x, float in_min, float in_max, float out_min,
               float out_max) {
  if (in_max == in_min)
    return out_min;
  return (x - in_min) * (out_max - out_min) / (in_max - in_min) + out_min;
}

float filterValue(float currentValue, float newValue, float alpha) {
  return (alpha * newValue) + ((1.0 - alpha) * currentValue);
}

void removeSpaces(char *str) {
  char *src = str, *dst = str;
  while (*src) {
    if (!isspace((unsigned char)*src)) {
      *dst++ = *src;
    }
    src++;
  }
  *dst = '\0';
}

int parseNumber(const char **str) {
  int value = 0;
  while (isdigit(**str)) {
    value = value * 10 + (**str - '0');
    (*str)++;
  }
  return value;
}

int from_hex(char a) {
  if (a >= 'A' && a <= 'F') {
    return a - 'A' + 10;
  }
  if (a >= 'a' && a <= 'f') {
    return a - 'a' + 10;
  }
  return a - '0';
}

int32_t parse_decimal(const char *t) {
  bool neg = (*t == '-');
  if (neg) {
    ++t;
  }
  int32_t ret = 100 * (int32_t)atol(t);
  while (isdigit((unsigned char)*t)) {
    ++t;
  }
  if (*t == '.' && isdigit((unsigned char)t[1])) {
    ret += 10 * (t[1] - '0');
    if (isdigit((unsigned char)t[2])) {
      ret += t[2] - '0';
    }
  }
  return neg ? -ret : ret;
}

void parse_degrees(const char *t, int16_t *deg, uint32_t *billionths) {
  if (deg == NULL || billionths == NULL) {
    return;
  }

  uint32_t left = (uint32_t)atol(t);
  uint16_t minutes = (uint16_t)(left % 100);
  uint32_t mult = 10000000UL;
  uint32_t tenmillionths = minutes * mult;
  *deg = (int16_t)(left / 100);
  while (isdigit((unsigned char)*t)) {
    ++t;
  }
  if (*t == '.') {
    while (isdigit((unsigned char)*++t)) {
      mult /= 10;
      tenmillionths += (uint32_t)(*t - '0') * mult;
    }
  }
  *billionths = (5 * tenmillionths + 1) / 3;
}

bool startsWith(const char *str, const char *prefix) {
  size_t lenPrefix = strlen(prefix);
  return strncmp(str, prefix, lenPrefix) == 0;
}

bool is_time_in_range(long now, long start, long end) {
  return hal_time_is_in_range(now, start, end);
}

void extract_time(long timeInMinutes, int *hours, int *minutes) {
  hal_time_extract_minutes(timeInMinutes, hours, minutes);
}

int getRandomEverySomeMillis(uint32_t time, int maxValue) {
  static uint32_t lastTime = 0;
  static int lastValue = -1;
  static bool seeded = false;

  if (!seeded) {
    srand((unsigned int)(hal_millis() ^ hal_micros()));
    seeded = true;
  }

  if (maxValue <= 0)
    return 0;

  uint32_t now = hal_millis();
  if (now - lastTime >= time) {
    lastTime = now;
    lastValue = rand() % maxValue;
  }

  return lastValue;
}

float getRandomFloatEverySomeMillis(uint32_t time, float maxValue) {
  static uint32_t lastTime = 0;
  static float lastValue = -1.0f;
  static bool seeded = false;

  if (!seeded) {
    srand((unsigned int)(hal_millis() ^ hal_micros()));
    seeded = true;
  }

  uint32_t now = hal_millis();
  if (now - lastTime >= time) {
    lastTime = now;
    uint32_t r = ((uint32_t)rand() << 16) | (uint32_t)rand();
    lastValue = (r / (float)UINT32_MAX) * maxValue;
  }

  return lastValue;
}

void remove_non_ascii(const char *input, char *output, size_t outputSize) {
  if (input == NULL || output == NULL || outputSize == 0) {
    return;
  }

  int i = 0, j = 0;
  const int maxOut = (int)outputSize - 1;

  while (input[i] && j < maxOut) {
    unsigned char c = (unsigned char)input[i];

    if (c == 0xC3 && input[i + 1]) {
      unsigned char next = (unsigned char)input[i + 1];
      if (next == 0x93)
        output[j++] = 'O'; // U+00D3 (Latin capital O with acute)
      else if (next == 0xB3)
        output[j++] = 'o'; // U+00F3 (Latin small o with acute)
      i += 2;
    } else if (c == 0xC4 && input[i + 1]) {
      unsigned char next = (unsigned char)input[i + 1];
      if (next == 0x84)
        output[j++] = 'A'; // U+0104 (Latin capital A with ogonek)
      else if (next == 0x85)
        output[j++] = 'a'; // U+0105 (Latin small a with ogonek)
      else if (next == 0x86)
        output[j++] = 'C'; // U+0106 (Latin capital C with acute)
      else if (next == 0x87)
        output[j++] = 'c'; // U+0107 (Latin small c with acute)
      else if (next == 0x98)
        output[j++] = 'E'; // U+0118 (Latin capital E with ogonek)
      else if (next == 0x99)
        output[j++] = 'e'; // U+0119 (Latin small e with ogonek)
      i += 2;
    } else if (c == 0xC5 && input[i + 1]) {
      unsigned char next = (unsigned char)input[i + 1];
      if (next == 0x81)
        output[j++] = 'L'; // U+0141 (Latin capital L with stroke)
      else if (next == 0x82)
        output[j++] = 'l'; // U+0142 (Latin small l with stroke)
      else if (next == 0x83)
        output[j++] = 'N'; // U+0143 (Latin capital N with acute)
      else if (next == 0x84)
        output[j++] = 'n'; // U+0144 (Latin small n with acute)
      else if (next == 0x9A)
        output[j++] = 'S'; // U+015A (Latin capital S with acute)
      else if (next == 0x9B)
        output[j++] = 's'; // U+015B (Latin small s with acute)
      else if (next == 0xB9)
        output[j++] = 'Z'; // U+0179 (Latin capital Z with acute)
      else if (next == 0xBA)
        output[j++] = 'z'; // U+017A (Latin small z with acute)
      else if (next == 0xBB)
        output[j++] = 'Z'; // U+017B (Latin capital Z with dot above)
      else if (next == 0xBC)
        output[j++] = 'z'; // U+017C (Latin small z with dot above)
      i += 2;
    } else if (c < 0x80) {
      output[j++] = input[i++];
    } else {
      i++;
    }
  }

  output[j] = '\0';
}

void hal_pack_field_pad(uint8_t *buf, const char *str, int width, uint8_t pad) {
  if (buf == NULL || str == NULL || width <= 0)
    return;
  int len = (int)strlen(str);
  if (len > width)
    len = width;
  memcpy(buf, str, (size_t)len);
  for (int i = len; i < width; i++)
    buf[i] = pad;
}

void hal_pack_field(uint8_t *buf, const char *str, int width) {
  hal_pack_field_pad(buf, str, width, 0x00);
}
