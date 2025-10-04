#ifndef T_TOOOLS
#define T_TOOOLS

#include <Arduino.h>
#include "libConfig.h"

#ifdef FREE_RTOS
#include <FreeRTOS.h>
#endif

#include <stdio.h>
#include <string.h>
#include <ctype.h>

#include <SPI.h>
#include <SD.h>
#include <EEPROM.h>
#include <Wire.h>
#include "unity.h"
#include "SmartTimers.h"

#ifndef INC_FREERTOS_H
#define m_mutex_def(mutexname) static mutex_t mutexname
#define m_mutex_init(mutexname) mutex_init(&mutexname)
#define m_mutex_enter_blocking(mutexname) mutex_enter_blocking(&mutexname)
#define m_mutex_exit(mutexname) mutex_exit(&mutexname)
#define m_delay(val) delay(val)
#define m_delay_microseconds(val) delayMicroseconds(val)  

#else

#include "task.h"
#include "semphr.h"
#define m_mutex_def(mutexname) static SemaphoreHandle_t mutexname
#define m_mutex_init(mutexname) mutexname = xSemaphoreCreateMutex()
#define m_mutex_enter_blocking(mutexname) xSemaphoreTake(mutexname, SECS(2))
#define m_mutex_exit(mutexname) xSemaphoreGive(mutexname)
#define m_delay(val) vTaskDelay(val)
#define m_delay_microseconds(val) sleep_us(val)
#endif

#ifdef PICO_W
#include <WiFi.h>
#endif

#define C_INIT_VAL 0xdeadbeef;

#ifndef NOINIT
#define NOINIT __attribute__((section(".noinit"))) 
#endif

#define D_NONE 0
#define D_ADD 1
#define D_SUB 2

#ifdef SD_LOGGER
#define WRITE_INTERVAL 2000
#define EEPROM_LOGGER_ADDR  0
#define EEPROM_CRASH_ADDR   4
#define EEPROM_FIRST_ADDR   8
#else
#define EEPROM_FIRST_ADDR   0
#endif

#ifndef AT24C256
#define RASPBERRY_EEPROM_SIZE 512
#endif

#define AT24C256_EEPROM_SIZE 32768
#define MAX_EEPROM_DELAY_COUNTER 5

//#define I2C_SCANNER

#ifndef ADC_BITS
#define ADC_BITS 12
#endif

// how many samples to take and average, more takes longer
// but is more 'smooth'
#ifndef NUMSAMPLES
#define NUMSAMPLES 4
#endif

//size for tables used for average value calculation
#define TEMPERATURE_TABLES_SIZE 5

// The beta coefficient of the thermistor (usually 3000-4000)
#ifndef BCOEFFICIENT
#define BCOEFFICIENT 3600
#endif

// temp. for nominal resistance (almost always 25 C)
#ifndef TEMPERATURENOMINAL
#define TEMPERATURENOMINAL 21   
#endif

#ifndef PRINTABLE_BUFFER_SIZE
#define PRINTABLE_BUFFER_SIZE 512
#endif
#ifndef PRINTABLE_PREFIX_SIZE
#define PRINTABLE_PREFIX_SIZE 16
#endif

void debugInit(void);
void setDebugPrefix(const char *prf);
int getSDLoggerNumber(void);
int getSDCrashNumber(void);
bool initSDLogger(int cs);
bool initCrashLogger(const char *addToName, int cs);
bool isSDLoggerInitialized(void);
bool isCrashLoggerInitialized(void);
void saveLoggerAndClose(void);
void saveCrashLoggerAndClose(void);
void deb(const char *format, ...);
void derr(const char *format, ...);
char *printBinaryAndSize(int number);
void updateCrashReport(String data);
void crashReport(const char *format, ...);
void floatToDec(float val, int *hi, int *lo);
float decToFloat(int hi, int lo);
float adcToVolt(int adc, float r1, float r2);
float ntcToTemp(int tpin, int thermistor, int r);
float steinhart(float val, float thermistor, int r, bool characteristic);
int percentToGivenVal(float percent, int maxWidth);
int percentFrom(int givenVal, int maxVal);
#ifdef I2C_SCANNER
void i2cScanner(void);
#endif
float getAverageValueFrom(int tpin);
float filter(float alpha, float input, float previous_output);
int adcCompe(int x);
float getAverageForTable(int *idx, int *overall, float val, float *table);
unsigned long getSeconds(void);
bool isDaylightSavingTime(int year, int month, int day);
void adjustTime(int *year, int *month, int *day, int *hour, int *minute);
unsigned short byteArrayToWord(unsigned char* bytes);
byte MSB(unsigned short value);
byte LSB(unsigned short value);
int MsbLsbToInt(byte msb, byte lsb);
bool isWireBusy(unsigned int dataAddress);
void resetEEPROM(void);
void wordToByteArray(unsigned short word, unsigned char* bytes);
void writeAT24(unsigned int dataAddress, byte dataVal);
byte readAT24(unsigned int dataAddress);
void writeAT24Int(unsigned int dataAddress, int dataVal);
int readAT24Int(unsigned int dataAddress);
float rroundf(float val);
float roundfWithPrecisionTo(float value, int precision);
bool isValidString(const char *s, int maxBufSize);
unsigned short rgbToRgb565(unsigned char r, unsigned char g, unsigned char b);
const char *macToString(uint8_t mac[6]);
const char *encToString(uint8_t enc);
bool scanNetworks(const char *networkToFind);
int getAverageFrom(int *table, int size);
int getMinimumFrom(int *table, int size);
int getHalfwayBetweenMinMax(int *array, int n);
float mapfloat(float x, float in_min, float in_max, float out_min, float out_max);
float filterValue(float currentValue, float newValue, float alpha);
char hexToChar(char high, char low);
void urlDecode(const char *src, char *dst);
void removeSpaces(char *str);
bool startsWith(const char *str, const char *prefix);
bool is_time_in_range(long now, long start, long end);
void extract_time(long timeInMinutes, int* hours, int* minutes);
int getRandomEverySomeMillis(uint32_t time, int maxValue);
float getRandomFloatEverySomeMillis(uint32_t time, float maxValue);
char *remove_non_ascii(const char* input);

#endif
