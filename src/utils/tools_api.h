#ifndef JASZCZURHAL_TOOLS_API_H
#define JASZCZURHAL_TOOLS_API_H

/**
 * @file tools_api.h
 * @brief Shared C-linkage API declarations for tools utilities.
 *
 * This header contains pure C declarations used by both `tools.h` (C++) and
 * `tools_c.h` (C compatibility path).
 */

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

#include "hal/hal_config.h"

#ifdef __cplusplus
extern "C" {
#endif

/** @name Debug API */
/** @{ */
/** @brief Set textual prefix used by debug logs. */
void hal_deb_set_prefix(const char *prefix);
/** @brief Print formatted debug log. */
void hal_deb(const char *format, ...);
/** @brief Print formatted error log. */
void hal_derr(const char *format, ...);
/**
 * @brief Print rate-limited formatted error log.
 * @param source Source tag used by the limiter (for example "can", "gps").
 */
void hal_derr_limited(const char *source, const char *format, ...);
/**
 * @brief Print bounded hex dump.
 * @param prefix Label printed before data.
 * @param buf Data buffer.
 * @param len Number of bytes available in @p buf.
 * @param maxBytes Max number of bytes to log.
 */
void hal_deb_hex(const char *prefix, const uint8_t *buf, int len, int maxBytes);

/** @brief Initialise debug backend. */
void debugInit(void);
/** @brief Set debug prefix to module name followed by a colon. */
void setDebugPrefixWithColon(const char *moduleName);
/** @brief Legacy alias for @ref hal_deb_set_prefix. */
#define setDebugPrefix hal_deb_set_prefix
/** @brief Legacy alias for @ref hal_deb. */
#define deb hal_deb
/** @brief Legacy alias for @ref hal_derr. */
#define derr hal_derr
/** @brief Legacy alias for @ref hal_derr_limited. */
#define derr_limited hal_derr_limited
/** @} */

/** @name Numeric and signal helpers */
/** @{ */
/** @brief Split float to integer and fractional parts. */
void floatToDec(float val, int *hi, int *lo);
/** @brief Merge integer and fractional parts back to float. */
float decToFloat(int hi, int lo);
/** @brief Convert ADC reading to voltage using resistor divider values. */
float adcToVolt(int adc, float r1, float r2);
/** @brief Convert NTC ADC reading to temperature. */
float ntcToTemp(int tpin, int thermistor, int r);
/** @brief Steinhart-Hart helper for thermistor conversion. */
float steinhart(float val, float thermistor, int r, bool characteristic);
/** @brief Convert percentage to integer value in range 0..maxWidth. */
int percentToGivenVal(float percent, int maxWidth);
/** @brief Convert integer value in range 0..maxVal to percentage. */
int percentFrom(int givenVal, int maxVal);
/** @brief Read averaged ADC value from given pin. */
float getAverageValueFrom(int tpin);
/** @brief Exponential smoothing filter. */
float filter(float alpha, float input, float previous_output);
/** @brief Blend current value with new value using alpha weight. */
float filterValue(float currentValue, float newValue, float alpha);
/** @brief ADC compensation helper. */
int adcCompe(int x);
/** @brief Rolling-average update helper. */
float getAverageForTable(int *idx, int *overall, float val, float *table);
/** @brief Return arithmetic mean from integer array. */
int getAverageFrom(int *table, int size);
/** @brief Return minimum value from integer array. */
int getMinimumFrom(int *table, int size);
/** @brief Return midpoint between min and max array values. */
int getHalfwayBetweenMinMax(int *array, int n);
/** @brief Floating-point map helper. */
float mapfloat(float x, float in_min, float in_max, float out_min,
               float out_max);
/** @brief Bit-cast float to uint32_t (type punning via memcpy). */
static inline uint32_t float_to_u32(float f) {
  uint32_t u;
  memcpy(&u, &f, sizeof(u));
  return u;
}
/** @brief Bit-cast uint32_t back to float (type punning via memcpy). */
static inline float u32_to_float(uint32_t u) {
  float f;
  memcpy(&f, &u, sizeof(f));
  return f;
}
/** @} */

/** @name Time/date helpers */
/** @{ */
/** @brief Return current uptime in seconds. */
unsigned long getSeconds(void);
/** @brief Return true when date falls into DST interval. */
bool isDaylightSavingTime(int year, int month, int day);
/** @brief Adjust date/time values by daylight-saving rules. */
void adjustTime(int *year, int *month, int *day, int *hour, int *minute);
/** @brief Return true when @p now is inside [start, end] range in minutes. */
bool is_time_in_range(long now, long start, long end);
/** @brief Split minute-of-day value into hours and minutes. */
void extract_time(long timeInMinutes, int *hours, int *minutes);
/** @} */

/** @name Binary/format/string helpers */
/** @{ */
/** @brief Build 16-bit word from 2-byte array (big-endian). */
unsigned short byteArrayToWord(unsigned char *bytes);
/** @brief Split 16-bit word into 2-byte array (big-endian). */
void wordToByteArray(unsigned short word, unsigned char *bytes);
/** @brief Return MSB byte of 16-bit value. */
uint8_t MSB(unsigned short value);
/** @brief Return LSB byte of 16-bit value. */
uint8_t LSB(unsigned short value);
/** @brief Merge MSB/LSB bytes into signed integer. */
int MsbLsbToInt(uint8_t msb, uint8_t lsb);
/** @brief Round float to nearest integer value represented as float. */
float rroundf(float val);
/** @brief Round float to requested decimal precision. */
float roundfWithPrecisionTo(float value, int precision);
/** @brief Print integer as binary with size metadata to provided buffer. */
char *printBinaryAndSize(int number, char *buf, size_t bufSize);
/** @brief Concatenate two strings into destination buffer with size check. */
bool concatStrings(char *dest, size_t destSize, const char *src1,
                   const char *src2);
/** @brief Validate zero-terminated string length/content. */
bool isValidString(const char *s, int maxBufSize);
/** @brief Convert two hex characters into one byte value. */
char hexToChar(char high, char low);
/** @brief URL-decode string from @p src to @p dst. */
void urlDecode(const char *src, char *dst);
/** @brief Remove whitespace characters from string in-place. */
void removeSpaces(char *str);
/** @brief Parse integer number from current position and advance pointer. */
int parseNumber(const char **str);
/** @brief Convert one hexadecimal digit character to numeric value. */
int from_hex(char a);
/** @brief Parse signed decimal with up to 2 fractional digits as value*100. */
int32_t parse_decimal(const char *t);
/** @brief Parse NMEA DDMM.MMMM coordinate to degrees and billionths. */
void parse_degrees(const char *t, int16_t *deg, uint32_t *billionths);
/** @brief Return true when string starts with provided prefix. */
bool startsWith(const char *str, const char *prefix);
/** @brief Copy string while dropping non-ASCII characters. */
void remove_non_ascii(const char *input, char *output, size_t outputSize);
/** @brief Pack text field to fixed width with custom pad byte. */
void hal_pack_field_pad(uint8_t *buf, const char *str, int width, uint8_t pad);
/** @brief Pack text field to fixed width padded with spaces. */
void hal_pack_field(uint8_t *buf, const char *str, int width);
/** @brief Convert 24-bit RGB to RGB565. */
unsigned short rgbToRgb565(unsigned char r, unsigned char g, unsigned char b);
/**
 * @brief Convert RGB888 pixel buffer to RGB565.
 *
 * @param rgb Input buffer with 3 bytes per pixel: R, G, B.
 * @param rgb565 Output buffer with @p pixelCount RGB565 pixels.
 * @param pixelCount Number of pixels to convert.
 * @return true on success, false when a required pointer is NULL.
 */
bool rgb888ToRgb565(const unsigned char *rgb, unsigned short *rgb565,
                    size_t pixelCount);
/**
 * @brief Convert RGBA8888 pixel buffer to RGB565.
 *
 * @param rgba Input buffer with 4 bytes per pixel: R, G, B, A. Alpha is
 * ignored.
 * @param rgb565 Output buffer with @p pixelCount RGB565 pixels.
 * @param pixelCount Number of pixels to convert.
 * @return true on success, false when a required pointer is NULL.
 */
bool rgba8888ToRgb565(const unsigned char *rgba, unsigned short *rgb565,
                      size_t pixelCount);
#ifdef HAL_ENABLE_PNG_AS_BASE64
/**
 * @brief Return exact decoded PNG byte count for a Base64 PNG string.
 *
 * This validates Base64 syntax and padding using the HAL Base64 decoder without
 * writing decoded bytes. Use the returned size to allocate the @p pngWork
 * buffer passed to the other Base64 PNG helpers.
 *
 * @param base64 Base64 text containing PNG bytes.
 * @param base64Len Number of Base64 characters in @p base64.
 * @param pngSize Output decoded PNG byte count.
 * @return true on success, false on invalid args or invalid Base64.
 */
bool pngBase64DecodedSize(const char *base64, size_t base64Len,
                          size_t *pngSize);
/**
 * @brief Decode a Base64-encoded PNG from memory to RGBA8888.
 *
 * @param rgba Output pointer allocated by LodePNG; free with free().
 * @param width Output image width in pixels.
 * @param height Output image height in pixels.
 * @param base64 Base64 text containing PNG bytes.
 * @param base64Len Number of Base64 characters in @p base64.
 * @param pngWork Caller-provided work buffer for decoded PNG bytes.
 * @param pngWorkSize Size of @p pngWork in bytes.
 * @param pngError Optional: receives LodePNG error code, or 0 when PNG decode
 * was not reached.
 * @return true on success, false on invalid args, invalid Base64, too-small
 * buffers, or PNG error.
 */
bool pngBase64Decode32(unsigned char **rgba, unsigned *width, unsigned *height,
                       const char *base64, size_t base64Len, uint8_t *pngWork,
                       size_t pngWorkSize, unsigned *pngError);
/**
 * @brief Decode a Base64-encoded PNG from memory directly to RGB565.
 *
 * The PNG is decoded through LodePNG's RGBA8888 output path and then converted
 * with @ref rgba8888ToRgb565. Alpha is ignored.
 *
 * @param base64 Base64 text containing PNG bytes.
 * @param base64Len Number of Base64 characters in @p base64.
 * @param pngWork Caller-provided work buffer for decoded PNG bytes.
 * @param pngWorkSize Size of @p pngWork in bytes.
 * @param rgb565 Output RGB565 pixel buffer.
 * @param rgb565Pixels Capacity of @p rgb565 in pixels.
 * @param width Output image width in pixels.
 * @param height Output image height in pixels.
 * @param pngError Optional: receives LodePNG error code, or 0 when PNG decode
 * was not reached.
 * @return true on success, false on invalid args, invalid Base64, too-small
 * buffers, or PNG error.
 */
bool pngBase64DecodeRgb565(const char *base64, size_t base64Len,
                           uint8_t *pngWork, size_t pngWorkSize,
                           unsigned short *rgb565, size_t rgb565Pixels,
                           unsigned *width, unsigned *height,
                           unsigned *pngError);
#endif
#ifdef HAL_ENABLE_JPEG
/**
 * @brief Decode JPEG bytes from memory directly to RGB565.
 *
 * Uses the bundled JPEGDecoder/picojpeg array path. Progressive JPEG files are
 * not supported by picojpeg.
 *
 * @param jpeg Input JPEG byte buffer.
 * @param jpegSize Size of @p jpeg in bytes.
 * @param rgb565 Output RGB565 pixel buffer.
 * @param rgb565Pixels Capacity of @p rgb565 in pixels.
 * @param width Output image width in pixels.
 * @param height Output image height in pixels.
 * @return true on success, false on invalid args, unsupported JPEG, decode
 * error, or too-small output buffer.
 */
bool jpegDecodeRgb565(const uint8_t *jpeg, size_t jpegSize,
                      unsigned short *rgb565, size_t rgb565Pixels,
                      unsigned *width, unsigned *height);
#endif
#ifdef HAL_ENABLE_JPEG_AS_BASE64
/**
 * @brief Return exact decoded JPEG byte count for a Base64 JPEG string.
 *
 * This validates Base64 syntax and padding using the HAL Base64 decoder without
 * writing decoded bytes. Use the returned size to allocate the @p jpegWork
 * buffer passed to @ref jpegBase64DecodeRgb565.
 *
 * @param base64 Base64 text containing JPEG bytes.
 * @param base64Len Number of Base64 characters in @p base64.
 * @param jpegSize Output decoded JPEG byte count.
 * @return true on success, false on invalid args or invalid Base64.
 */
bool jpegBase64DecodedSize(const char *base64, size_t base64Len,
                           size_t *jpegSize);
/**
 * @brief Decode a Base64-encoded JPEG from memory directly to RGB565.
 *
 * @param base64 Base64 text containing JPEG bytes.
 * @param base64Len Number of Base64 characters in @p base64.
 * @param jpegWork Caller-provided work buffer for decoded JPEG bytes.
 * @param jpegWorkSize Size of @p jpegWork in bytes.
 * @param rgb565 Output RGB565 pixel buffer.
 * @param rgb565Pixels Capacity of @p rgb565 in pixels.
 * @param width Output image width in pixels.
 * @param height Output image height in pixels.
 * @return true on success, false on invalid args, invalid Base64, too-small
 * buffers, unsupported JPEG, or decode error.
 */
bool jpegBase64DecodeRgb565(const char *base64, size_t base64Len,
                            uint8_t *jpegWork, size_t jpegWorkSize,
                            unsigned short *rgb565, size_t rgb565Pixels,
                            unsigned *width, unsigned *height);
#endif
/** @brief Format MAC address to string buffer. */
const char *macToString(uint8_t mac[6], char *buf, size_t bufSize);
/** @brief Convert WiFi encryption enum to human-readable string. */
const char *encToString(uint8_t enc);
/** @} */

/** @name Network and random helpers */
/** @{ */
/** @brief Scan WiFi networks through HAL and check if target SSID exists. */
bool scanNetworks(const char *networkToFind);
/** @brief Return pseudo-random integer refreshed every @p time ms. */
int getRandomEverySomeMillis(uint32_t time, int maxValue);
/** @brief Return pseudo-random float refreshed every @p time ms. */
float getRandomFloatEverySomeMillis(uint32_t time, float maxValue);
/** @} */

#ifdef __cplusplus
}
#endif

#endif
