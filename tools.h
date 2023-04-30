#ifndef T_TOOOLS
#define T_TOOOLS

#include <Arduino.h>
#include <SD.h>
#include <EEPROM.h>

#define SD_LOGGER

#ifndef ADC_BITS
#define ADC_BITS 12
#endif

// how many samples to take and average, more takes longer
// but is more 'smooth'
#ifndef NUMSAMPLES
#define NUMSAMPLES 8
#endif

// The beta coefficient of the thermistor (usually 3000-4000)
#ifndef BCOEFFICIENT
#define BCOEFFICIENT 3600
#endif

// temp. for nominal resistance (almost always 25 C)
#ifndef TEMPERATURENOMINAL
#define TEMPERATURENOMINAL 21   
#endif

bool initSDLogger(int cs);
bool isSDLoggerInitialized(void);
void deb(const char *format, ...);
void derr(const char *format, ...);
void floatToDec(float val, int *hi, int *lo);
float adcToVolt(int adc, float r1, float r2);
float ntcToTemp(int tpin, int thermistor, int r);
int percentToGivenVal(float percent, int maxWidth);
#ifdef I2C_SCANNER
void i2cScanner(void);
#endif
float getAverageValueFrom(int tpin);
unsigned long getSeconds(void);
unsigned short byteArrayToWord(unsigned char* bytes);
byte MSB(unsigned short value);
byte LSB(unsigned short value);
void wordToByteArray(unsigned short word, unsigned char* bytes);

#endif
