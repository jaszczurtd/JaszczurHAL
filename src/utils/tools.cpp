//
//  utils.c
//  Index
//
//  Created by Marcin Kielesiński on 07/12/2019.
//

#include "tools.h"

#ifdef SD_LOGGER
static bool loggerInitialized = false;
static bool crashLoggerInitialized = false;
static File loggerFile;
static File crashFile;
static SPISettings settingsA(1000000, MSBFIRST, SPI_MODE1);
static bool SDStarted = false;
#else
static bool loggerInitialized = false;
static bool crashLoggerInitialized = false;
#endif

int getSDLoggerNumber(void) {
#ifdef SD_LOGGER
  return hal_eeprom_read_int(HAL_TOOLS_EEPROM_LOGGER_ADDR);
#else
  return -1;
#endif
}

//cs: chip select/SD addres
bool initSDLogger(int cs) {
  (void)cs;  // Only used when SD_LOGGER is defined
#ifdef SD_LOGGER

  char buf[128] = {0};

  int logNumber = getSDLoggerNumber();
  snprintf(buf, sizeof(buf) - 1, "log%d.txt", logNumber);
  logNumber++;
  hal_eeprom_write_int(HAL_TOOLS_EEPROM_LOGGER_ADDR, logNumber);
  hal_eeprom_commit();

  SPI.beginTransaction(settingsA);
  if(!SDStarted) {
    SDStarted = loggerInitialized = SD.begin(cs);
  } else {
    loggerInitialized = SDStarted;
  }
  if(loggerInitialized) {
    loggerFile = SD.open(buf, FILE_WRITE);
    if(!loggerFile) {
      loggerInitialized = false;
    }
  } else {
    hal_serial_println("logger: Card Mount Failed");
  }

  SPI.endTransaction();

#endif
  return loggerInitialized;
}

bool isSDLoggerInitialized(void) {
  return loggerInitialized;
}

#ifdef SD_LOGGER
static unsigned long lastWriteTime = 0; 
#define LOG_BUFFER_SIZE 2048
static char logBuffer[LOG_BUFFER_SIZE];
static int logBufPos = 0;
#endif
void updateSD(String data) {
  (void)data;  // Only used when SD_LOGGER is defined
#ifdef SD_LOGGER
  if(isSDLoggerInitialized()) {  
    const char *s = data.c_str();
    int slen = strlen(s);
    if (logBufPos + slen + 1 < LOG_BUFFER_SIZE) {
      memcpy(logBuffer + logBufPos, s, slen);
      logBufPos += slen;
      logBuffer[logBufPos++] = '\n';
      logBuffer[logBufPos] = '\0';
    }
    unsigned long currentTime = hal_millis();
    if (currentTime - lastWriteTime >= HAL_TOOLS_WRITE_INTERVAL_MS) {
      lastWriteTime = currentTime;

      SPI.beginTransaction(settingsA);
      loggerFile.println(logBuffer);
      loggerFile.flush();
      SPI.endTransaction();
      logBufPos = 0;
      logBuffer[0] = '\0';
    }
  }
  #endif
}

void saveLoggerAndClose(void) {
#ifdef SD_LOGGER
  if(isSDLoggerInitialized()) {  
    loggerInitialized = false;
    SPI.beginTransaction(settingsA);
    loggerFile.println(logBuffer);
    loggerFile.flush();
    loggerFile.close();
    SPI.endTransaction();
    logBufPos = 0;
    logBuffer[0] = '\0';
  }
#endif
}

int getSDCrashNumber(void) {
#ifdef SD_LOGGER
  return hal_eeprom_read_int(HAL_TOOLS_EEPROM_CRASH_ADDR);
#else
  return -1;
#endif
}

//cs: chip select/SD addres
bool initCrashLogger(const char *addToName, int cs) {
  (void)addToName;  // Only used when SD_LOGGER is defined
  (void)cs;         // Only used when SD_LOGGER is defined
#ifdef SD_LOGGER

  char buf[128] = {0};

  int crashNumber = getSDCrashNumber();
  if(addToName != NULL && strlen(addToName) > 0) {
    snprintf(buf, sizeof(buf) - 1, "watchdog%d(%s).txt", crashNumber, addToName);
  } else {
    snprintf(buf, sizeof(buf) - 1, "watchdog%d.txt", crashNumber);
  }
  crashNumber++;

  hal_eeprom_write_int(HAL_TOOLS_EEPROM_CRASH_ADDR, crashNumber);
  hal_eeprom_commit();

  SPI.beginTransaction(settingsA);

  if(!SDStarted) {
    SDStarted = crashLoggerInitialized = SD.begin(cs);
  } else {
    crashLoggerInitialized = SDStarted;
  }

  if(crashLoggerInitialized) {
    crashFile = SD.open(buf, FILE_WRITE);
    if(!crashFile) {
      crashLoggerInitialized = false;
    }
  } else {
    hal_serial_println("crash logger: Card Mount Failed");
  }

  SPI.endTransaction();

  if(crashLoggerInitialized) {
    snprintf(buf, sizeof(buf) - 1, "corresponded log file: log%d.txt", 
      getSDLoggerNumber() - 1);

    updateCrashReport(buf);
  }

#endif
  return crashLoggerInitialized;
}

bool isCrashLoggerInitialized(void) {
  return crashLoggerInitialized;
}

void updateCrashReport(String data) {
  (void)data;  // Only used when SD_LOGGER is defined
#ifdef SD_LOGGER
  if(isCrashLoggerInitialized()) {  
    SPI.beginTransaction(settingsA);
    crashFile.println(data + "\n");
    crashFile.flush();
    SPI.endTransaction();
  }
#endif
}

void saveCrashLoggerAndClose(void) {
#ifdef SD_LOGGER
  if(isCrashLoggerInitialized()) {  
    crashLoggerInitialized = false;
    SPI.beginTransaction(settingsA);
    crashFile.flush();
    crashFile.close();
    SPI.endTransaction();
  }
#endif
}

void crashReport(const char *format, ...) {
  (void)format;  // Only used when SD_LOGGER is defined
#ifdef SD_LOGGER
  if(isCrashLoggerInitialized()) {  
    va_list valist;
    va_start(valist, format);

    char buffer[128];
    memset (buffer, 0, sizeof(buffer));
    vsnprintf(buffer, sizeof(buffer) - 1, format, valist);

    updateCrashReport(buffer);

    va_end(valist);
  }
#endif
}

void debugInit(void) {
  hal_debug_init(HAL_DEBUG_DEFAULT_BAUD);
}

void setDebugPrefixWithColon(const char *moduleName) {
  char prefix[HAL_DEBUG_PREFIX_SIZE] = {0};
  size_t prefixLen = 0;

  if (moduleName == NULL) {
    return;
  }

  while (moduleName[prefixLen] != '\0' && prefixLen < (sizeof(prefix) - 2)) {
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

  if (bits >= bufSize) bits = bufSize - 1;
  memset(buf, 0, bufSize);

  for (int i = bits - 1; i >= 0; i--) {
    buf[bits - 1 - i] = (number & (1 << i)) ? '1' : '0';
  }

  buf[bits] = '\0'; 
  return buf;
}


void floatToDec(float val, int *hi, int *lo) {
	int t1 = (int)val;
	if(t1 > -128) {
		if(hi != NULL) {
			*hi = t1;
		}
		int t2 = (int) (((float)val - t1) * 10);
		if(lo != NULL) {
			if(t2 >= 0) {
				*lo = t2;
			} else {
				*lo = 0;
			}
		}
	}
}

float decToFloat(int hi, int lo) {
  return (float)hi + ((float)lo / 10);
}

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

  if(x > 3584) y = x + 32;
  else if(x == 3583) y = x + 29;
  else if(x == 3582) y = x + 27;

  else if(x > 2560) y = x + 24;
  else if(x == 2559) y = x + 21;
  else if(x == 2558) y = x + 19;

  else if(x > 1536) y = x + 16;
  else if(x == 1535) y = x + 13;
  else if(x == 1534) y = x + 11;

  else if(x > 512) y = x + 8;
  else if(x == 511) y = x + 5;
  else if(x == 510) y = x + 3;
  else y = x;
  return y;
}

float getAverageForTable(int *idx, int *overall, float val, float *table) {

  table[(*idx)++] = val;
  if(*idx > HAL_TOOLS_TEMPERATURE_TABLES_SIZE - 1) {
    *idx = 0;
  }
  (*overall)++;
  if(*overall >= HAL_TOOLS_TEMPERATURE_TABLES_SIZE) {
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
  if(size > 0) {
    for (int i = 0; i < size; i++) {
        average += table[i];
    }
    average /= size;
    if(average < 0) {
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
    // adcCompe() can return values above ADC_MAXVALUE (RP2040 DNL fix adds up to +32).
    // Clamp to ADC_MAXVALUE-1 to prevent (MAXVALUE/average - 1) going to zero or negative,
    // which would produce NaN via log(negative) in steinhart().
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
  float steinhart_val = val / thermistor;     // (R/Ro)
  steinhart_val = log(steinhart_val);                  // ln(R/Ro)
  steinhart_val /= HAL_TOOLS_BCOEFFICIENT;                   // 1/B * ln(R/Ro)
  float invTo = 1.0 / (HAL_TOOLS_TEMPERATURENOMINAL + 273.15);
  if(characteristic) {
    steinhart_val += invTo; // + (1/To)
    steinhart_val = 1.0 / steinhart_val;                 // Invert
    steinhart_val -= 273.15;                         // convert absolute temp to C    
  } else {
    steinhart_val -= invTo; // - (1/To)
    steinhart_val = 1.0 / steinhart_val;                // Invert
    steinhart_val += 273.15;                         // convert absolute temp to C   
    steinhart_val = -steinhart_val; 
  }

  return steinhart_val;  
}

int percentToGivenVal(float percent, int givenVal) {
    return int(((percent / 100.0) * givenVal));
}

int percentFrom(int givenVal, int maxVal) {
  if (maxVal == 0) return 0;
  return (givenVal * 100) / maxVal;
}

#ifdef I2C_SCANNER

unsigned int loopCounter = 0;
static bool t = false;
void i2cScanner(void) {
  uint8_t error, address;
  int nDevices;
 
  char i2c_buf[32];
  while(true) {
    snprintf(i2c_buf, sizeof(i2c_buf), "Scanning %u...", loopCounter++);
    hal_serial_println(i2c_buf);

    nDevices = 0;
    for(address = 1; address < 127; address++ ) {
      hal_watchdog_feed();
      // The i2c_scanner uses the return value of
      // the Write.endTransmisstion to see if
      // a device did acknowledge to the address.
      hal_i2c_begin_transmission(address);
      error = hal_i2c_end_transmission();

      if (error == 0) {
        snprintf(i2c_buf, sizeof(i2c_buf), "I2C found: 0x%02X", address);
        hal_serial_println(i2c_buf);
        nDevices++;
      }
      else if (error == 4) {
        snprintf(i2c_buf, sizeof(i2c_buf), "I2C error at: 0x%02X", address);
        hal_serial_println(i2c_buf);
      }
    }
    if (nDevices == 0)
      hal_serial_println("No I2C devices found");
    else
      hal_serial_println("done");
  
    m_delay(500);           // wait 500 mseconds for next scan
  }

}
#endif

unsigned long getSeconds(void) {
  return ((hal_millis() + 500) / 1000);
}

bool isDaylightSavingTime(int year, int month, int day) {
    if (month < 3 || month > 10) return false;
    if (month > 3 && month < 10) return true;
    // Tomohiko Sakamoto's day-of-week algorithm (0 = Sunday)
    static const int t[] = {0, 3, 2, 5, 0, 3, 5, 1, 4, 6, 2, 4};
    int dow31 = (year + year/4 - year/100 + year/400 + t[month - 1] + 31) % 7;
    int lastSunday = 31 - dow31;
    if (month == 3) return day >= lastSunday;
    return day < lastSunday;  // October
}

void adjustTime(int *year, int *month, int *day, int *hour, int *minute) {
  if (!year || !month || !day || !hour || !minute) {
    return;
  }

  (void)minute;

    if (isDaylightSavingTime(*year, *month, *day)) {
        *hour += 2;  // CEST = UTC+2
    } else {
        *hour += 1;  // CET  = UTC+1
    }

  while (*hour >= 24) {
    *hour -= 24;
    (*day)++;

    int daysInMonth = 31;
    if (*month == 4 || *month == 6 || *month == 9 || *month == 11) {
      daysInMonth = 30;
    } else if (*month == 2) {
      const bool leap = ((*year % 4 == 0) && (*year % 100 != 0)) || (*year % 400 == 0);
      daysInMonth = leap ? 29 : 28;
    }

    if (*day > daysInMonth) {
      *day = 1;
      (*month)++;
      if (*month > 12) {
        *month = 1;
        (*year)++;
      }
    }
    }
}

uint8_t MSB(unsigned short value){
  return (uint8_t)(value >> 8) & 0xFF;
}

uint8_t LSB(unsigned short value){
  return (uint8_t)(value & 0x00FF);
}

int MsbLsbToInt(uint8_t msb, uint8_t lsb) {
  return ((unsigned short)msb << 8) | lsb;
}

unsigned short byteArrayToWord(unsigned char* bytes) {
  unsigned short word = ((unsigned short)bytes[0] << 8) | bytes[1];
  return word;
}

void wordToByteArray(unsigned short word, unsigned char* bytes) {
  bytes[0] = MSB(word);
  bytes[1] = LSB(word);
}

float rroundf(float val) {
  return roundf(val * 10.0f) / 10.0f;
}

float roundfWithPrecisionTo(float value, int precision) {
    float multiplier = 1.0;
    for (int i = 0; i < precision; ++i) {
        multiplier *= 10.0;
    }

    return roundf(value * multiplier) / multiplier;
}

bool concatStrings(char* dest, size_t destSize, const char* src1, const char* src2) {
  size_t len1;
  size_t len2;

  if(dest == NULL || src1 == NULL || src2 == NULL) {
    return false;
  }

  len1 = strlen(src1);
  len2 = strlen(src2);

  if(destSize == 0) {
    return false;
  }

  if((len1 + len2 + 1) > destSize) {
    return false;
  }

  while((*dest++ = *src1++) != '\0') { }

  --dest;

  while((*dest++ = *src2++) != '\0') { }

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
      
      bool p = (
          isdigit(s[a]) || 
          isalpha(s[a]) || 
          isspace(s[a]) || 
          isgraph(s[a]) );
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

const char *macToString(uint8_t mac[6], char *buf, size_t bufSize) {
  snprintf(buf, bufSize, "%02X:%02X:%02X:%02X:%02X:%02X", mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
  return buf;
}

const char *encToString(uint8_t enc) {
  (void)enc;  // Only used when PICO_W is defined
  #ifdef PICO_W
  switch (enc) {
    case ENC_TYPE_NONE: return "NONE";
    case ENC_TYPE_TKIP: return "WPA";
    case ENC_TYPE_CCMP: return "WPA2";
    case ENC_TYPE_AUTO: return "AUTO";
    default: return "UNKN";
  }
  #endif
  return "UNKN";
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
  (void)networkToFind;  // Only used when PICO_W is defined
  bool networkFound = false;
  #ifdef PICO_W
  deb("Beginning scan at %lu\n", hal_millis());
  int cnt = WiFi.scanNetworks();
  if (!cnt) {
    deb("No WiFi networks found");
  } else {
    deb("Found %d networks\n", cnt);
    deb("%32s %5s %17s %2s %4s", "SSID", "ENC", "BSSID        ", "CH", "RSSI");
    for (int i = 0; i < cnt; i++) {
      uint8_t bssid[6];
      WiFi.BSSID(i, bssid);
      char macBuf[20];
      deb("%32s %5s %17s %2d %4ld", WiFi.SSID(i), encToString(WiFi.encryptionType(i)), macToString(bssid, macBuf, sizeof(macBuf)), WiFi.channel(i), WiFi.RSSI(i));
      
      if(networkToFind != NULL && strlen(networkToFind) > 0) {
        if(!strncmp(WiFi.SSID(i), networkToFind, strlen(networkToFind))) {
          deb("network %s is available", networkToFind);
          networkFound = true;
        }
      }
    }
  }
  deb("\n--- END --- at %lu\n", hal_millis());
  #else
  deb("No PicoW configured, WiFi disabled");
  #endif
  return networkFound;
}

float mapfloat(float x, float in_min, float in_max, float out_min, float out_max) {
  if (in_max == in_min) return out_min;
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

bool startsWith(const char *str, const char *prefix) {
  size_t lenPrefix = strlen(prefix);
  return strncmp(str, prefix, lenPrefix) == 0;
}

bool is_time_in_range(long now, long start, long end) {
  return (now >= start && now < end);
}

void extract_time(long timeInMinutes, int* hours, int* minutes) {
  *hours = (int)(timeInMinutes / 60);
  *minutes = (int)(timeInMinutes % 60);
}

int getRandomEverySomeMillis(uint32_t time, int maxValue) {
  static uint32_t lastTime = 0;
  static int lastValue = -1;
  static bool seeded = false;

  if (!seeded) {
    srand((unsigned int)(hal_millis() ^ hal_micros()));
    seeded = true;
  }

  if (maxValue <= 0) return 0;

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
          if (next == 0x93)      output[j++] = 'O'; // U+00D3 (Latin capital O with acute)
          else if (next == 0xB3) output[j++] = 'o'; // U+00F3 (Latin small o with acute)
            i += 2;
        } else if (c == 0xC4 && input[i + 1]) {
            unsigned char next = (unsigned char)input[i + 1];
          if (next == 0x84)      output[j++] = 'A'; // U+0104 (Latin capital A with ogonek)
          else if (next == 0x85) output[j++] = 'a'; // U+0105 (Latin small a with ogonek)
          else if (next == 0x86) output[j++] = 'C'; // U+0106 (Latin capital C with acute)
          else if (next == 0x87) output[j++] = 'c'; // U+0107 (Latin small c with acute)
          else if (next == 0x98) output[j++] = 'E'; // U+0118 (Latin capital E with ogonek)
          else if (next == 0x99) output[j++] = 'e'; // U+0119 (Latin small e with ogonek)
            i += 2;
        } else if (c == 0xC5 && input[i + 1]) {
            unsigned char next = (unsigned char)input[i + 1];
          if (next == 0x81)      output[j++] = 'L'; // U+0141 (Latin capital L with stroke)
          else if (next == 0x82) output[j++] = 'l'; // U+0142 (Latin small l with stroke)
          else if (next == 0x83) output[j++] = 'N'; // U+0143 (Latin capital N with acute)
          else if (next == 0x84) output[j++] = 'n'; // U+0144 (Latin small n with acute)
          else if (next == 0x9A) output[j++] = 'S'; // U+015A (Latin capital S with acute)
          else if (next == 0x9B) output[j++] = 's'; // U+015B (Latin small s with acute)
          else if (next == 0xB9) output[j++] = 'Z'; // U+0179 (Latin capital Z with acute)
          else if (next == 0xBA) output[j++] = 'z'; // U+017A (Latin small z with acute)
          else if (next == 0xBB) output[j++] = 'Z'; // U+017B (Latin capital Z with dot above)
          else if (next == 0xBC) output[j++] = 'z'; // U+017C (Latin small z with dot above)
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
  if(buf == NULL || str == NULL || width <= 0) return;
  int len = (int)strlen(str);
  if(len > width) len = width;
  memcpy(buf, str, (size_t)len);
  for(int i = len; i < width; i++) buf[i] = pad;
}

void hal_pack_field(uint8_t *buf, const char *str, int width) {
  hal_pack_field_pad(buf, str, width, 0x00);
}

