#ifndef T_TOOOLS
#define T_TOOOLS

#include <Arduino.h>
#include <SPI.h>
#include <SD.h>
#include <EEPROM.h>
#include <Wire.h>
#include "unity.h"

#define AT24C256
#define EEPROM_I2C_ADDRESS 0x50
#define SD_LOGGER

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
#define NUMSAMPLES 8
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
void updateCrashReport(String data);
void crashReport(const char *format, ...);
void floatToDec(float val, int *hi, int *lo);
float adcToVolt(int adc, float r1, float r2);
float ntcToTemp(int tpin, int thermistor, int r);
int percentToGivenVal(float percent, int maxWidth);
#ifdef I2C_SCANNER
void i2cScanner(void);
#endif
float getAverageValueFrom(int tpin);
float getAverageForTable(int *idx, int *overall, float val, float *table);
unsigned long getSeconds(void);
unsigned short byteArrayToWord(unsigned char* bytes);
byte MSB(unsigned short value);
byte LSB(unsigned short value);
bool isWireBusy(unsigned int dataAddress);
void resetEEPROM(void);
void wordToByteArray(unsigned short word, unsigned char* bytes);
void writeAT24(unsigned int dataAddress, byte dataVal);
byte readAT24(unsigned int dataAddress);
void writeAT24Int(unsigned int dataAddress, int dataVal);
int readAT24Int(unsigned int dataAddress);
float rroundf(float val);
bool isValidString(const char *s, int maxBufSize);
#endif
