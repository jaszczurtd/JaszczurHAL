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
/** @brief Legacy alias for @ref hal_deb_set_prefix. */
#define setDebugPrefix hal_deb_set_prefix
/** @brief Legacy alias for @ref hal_deb. */
#define deb            hal_deb
/** @brief Legacy alias for @ref hal_derr. */
#define derr           hal_derr
/** @brief Legacy alias for @ref hal_derr_limited. */
#define derr_limited   hal_derr_limited
/** @} */

/** @name SD/Crash logger API */
/** @{ */
/** @brief Return next logger file number. */
int getSDLoggerNumber(void);
/** @brief Return next crash report file number. */
int getSDCrashNumber(void);
/** @brief Initialise SD logger backend. */
bool initSDLogger(int cs);
/** @brief Initialise crash logger backend. */
bool initCrashLogger(const char *addToName, int cs);
/** @brief Return true when SD logger is ready. */
bool isSDLoggerInitialized(void);
/** @brief Return true when crash logger is ready. */
bool isCrashLoggerInitialized(void);
/** @brief Flush and close SD logger output. */
void saveLoggerAndClose(void);
/** @brief Flush and close crash logger output. */
void saveCrashLoggerAndClose(void);
/** @brief Append formatted crash report entry. */
void crashReport(const char *format, ...);
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
float mapfloat(float x, float in_min, float in_max, float out_min, float out_max);
/** @brief Bit-cast float to uint32_t (type punning via memcpy). */
static inline uint32_t float_to_u32(float f) {
  uint32_t u; memcpy(&u, &f, sizeof(u)); return u;
}
/** @brief Bit-cast uint32_t back to float (type punning via memcpy). */
static inline float u32_to_float(uint32_t u) {
  float f; memcpy(&f, &u, sizeof(f)); return f;
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
/** @brief Format MAC address to string buffer. */
const char *macToString(uint8_t mac[6], char *buf, size_t bufSize);
/** @brief Convert WiFi encryption enum to human-readable string. */
const char *encToString(uint8_t enc);
/** @} */

/** @name Network and random helpers */
/** @{ */
/** @brief Scan WiFi networks and check if target SSID exists. */
bool scanNetworks(const char *networkToFind);
/** @brief Return pseudo-random integer refreshed every @p time ms. */
int getRandomEverySomeMillis(uint32_t time, int maxValue);
/** @brief Return pseudo-random float refreshed every @p time ms. */
float getRandomFloatEverySomeMillis(uint32_t time, float maxValue);
/** @} */

#ifdef I2C_SCANNER
/** @brief Scan I2C bus and print discovered addresses. */
void i2cScanner(void);
#endif

#ifdef __cplusplus
}
#endif

#endif
