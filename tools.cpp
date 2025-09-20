//
//  utils.c
//  Index
//
//  Created by Marcin Kielesiński on 07/12/2019.
//

#include "tools.h"

static bool loggerInitialized = false;
static bool crashLoggerInitialized = false;
static File loggerFile;
static File crashFile;
static SPISettings settingsA(1000000, MSBFIRST, SPI_MODE1);
static String logBuffer = "";

#ifdef SD_LOGGER
static bool SDStarted = false;
#endif

int getSDLoggerNumber(void) {
#ifdef SD_LOGGER
  return readAT24Int(EEPROM_LOGGER_ADDR);
#else
  return -1;
#endif
}

#ifndef AT24C256
static bool eepromStarted = false;
#endif
void startRaspberryEEPROM(int size) {
  #ifndef AT24C256
  if(!eepromStarted) {
    EEPROM.begin(size);
    eepromStarted = true;
  }
  #endif
}

//cs: chip select/SD addres
bool initSDLogger(int cs) {
#ifdef SD_LOGGER

  char buf[128] = {0};

  #ifndef AT24C256
  startRaspberryEEPROM(RASPBERRY_EEPROM_SIZE);
  #endif

  int logNumber = getSDLoggerNumber();
  snprintf(buf, sizeof(buf) - 1, "log%d.txt", logNumber);
  logNumber++;
  writeAT24Int(EEPROM_LOGGER_ADDR, logNumber);

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
    Serial.println("logger: Card Mount Failed");
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
#endif
void updateSD(String data) {
#ifdef SD_LOGGER
  if(isSDLoggerInitialized()) {  
    logBuffer += data;
    logBuffer += "\n";
    unsigned long currentTime = millis();
    if (currentTime - lastWriteTime >= WRITE_INTERVAL) {
      lastWriteTime = currentTime;

      SPI.beginTransaction(settingsA);
      loggerFile.println(logBuffer);
      loggerFile.flush();
      SPI.endTransaction();
      logBuffer = "";
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
    logBuffer = "";
  }
#endif
}

int getSDCrashNumber(void) {
#ifdef SD_LOGGER
  return readAT24Int(EEPROM_CRASH_ADDR);
#else
  return -1;
#endif
}

//cs: chip select/SD addres
bool initCrashLogger(const char *addToName, int cs) {
#ifdef SD_LOGGER

  char buf[128] = {0};

  #ifndef AT24C256
  startRaspberryEEPROM(RASPBERRY_EEPROM_SIZE);
  #endif

  int crashNumber = getSDCrashNumber();
  if(addToName != NULL && strlen(addToName) > 0) {
    snprintf(buf, sizeof(buf) - 1, "watchdog%d(%s).txt", crashNumber, addToName);
  } else {
    snprintf(buf, sizeof(buf) - 1, "watchdog%d.txt", crashNumber);
  }
  crashNumber++;

  writeAT24Int(EEPROM_CRASH_ADDR, crashNumber);

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
    Serial.println("crash logger: Card Mount Failed");
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

m_mutex_def(debugMutex);
m_mutex_def(debugErrMutex);
void debugInit(void) {
  m_mutex_init(debugMutex);
  m_mutex_init(debugErrMutex);
  Serial.begin(9600);
}

NOINIT static char deb_buffer[PRINTABLE_BUFFER_SIZE];
void deb(const char *format, ...) {

  m_mutex_enter_blocking(debugMutex);

  va_list valist;
  va_start(valist, format);

  memset (deb_buffer, 0, sizeof(deb_buffer));
  vsnprintf(deb_buffer, sizeof(deb_buffer) - 1, format, valist);
  Serial.println(deb_buffer);

#ifdef SD_LOGGER
  updateSD(deb_buffer);
#endif

  va_end(valist);

  m_mutex_exit(debugMutex);
}

NOINIT static char binary_buffer[PRINTABLE_BUFFER_SIZE];
char *printBinaryAndSize(int number) {
  m_mutex_enter_blocking(debugMutex);

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

  memset(binary_buffer, 0, sizeof(binary_buffer));

  for (int i = bits - 1; i >= 0; i--) {
    binary_buffer[bits - 1 - i] = (number & (1 << i)) ? '1' : '0';
  }

  binary_buffer[bits] = '\0'; 

  m_mutex_exit(debugMutex);
  return binary_buffer;
}

NOINIT static char derr_buffer[PRINTABLE_BUFFER_SIZE];
void derr(const char *format, ...) {

  m_mutex_enter_blocking(debugErrMutex);

  va_list valist;
  va_start(valist, format);

  memset (derr_buffer, 0, sizeof(derr_buffer));

  const char *error = "ERROR! ";
  int len = strlen(error);

  strcpy(derr_buffer, error);

  vsnprintf(derr_buffer + len, sizeof(derr_buffer) - 1 - len, format, valist);
  Serial.println(derr_buffer);

#ifdef SD_LOGGER
  updateSD(derr_buffer);
#endif

  va_end(valist);

  m_mutex_exit(debugErrMutex);
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

  return adc * (V_REF / pow(2, ADC_BITS)) * V_DIVIDER_SCALE;
}

float getAverageValueFrom(int tpin) {

    uint8_t i;
    float average = 0;

    // take N samples in a row, with a slight delay
    for (i = 0; i < NUMSAMPLES; i++) {
        average += adcCompe(analogRead(tpin));
        m_delay_microseconds(10);
    }
    average /= NUMSAMPLES;

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
  if(*idx > TEMPERATURE_TABLES_SIZE - 1) {
    *idx = 0;
  }
  (*overall)++;
  if(*overall >= TEMPERATURE_TABLES_SIZE - 1) {
    *overall = TEMPERATURE_TABLES_SIZE - 1;
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
// convert the value to resistance
    average = 4095.0 / average - 1;
    return steinhart(average, thermistor, r, true);
}

float steinhart(float val, float thermistor, int r, bool characteristic) {
  val = r / val;
  float steinhart = val / thermistor;     // (R/Ro)
  steinhart = log(steinhart);                  // ln(R/Ro)
  steinhart /= BCOEFFICIENT;                   // 1/B * ln(R/Ro)
  float invTo = 1.0 / (TEMPERATURENOMINAL + 273.15);
  if(characteristic) {
    steinhart += invTo; // + (1/To)
    steinhart = 1.0 / steinhart;                 // Invert
    steinhart -= 273.15;                         // convert absolute temp to C    
  } else {
    steinhart -= invTo; // - (1/To)
    steinhart = 1.0 / steinhart;                // Invert
    steinhart += 273.15;                         // convert absolute temp to C   
    steinhart = -steinhart; 
  }

  return steinhart;  
}

int percentToGivenVal(float percent, int givenVal) {
    return int(((percent / 100.0) * givenVal));
}

int percentFrom(int givenVal, int maxVal) {
  return (givenVal * 100) / maxVal;
}

#ifdef I2C_SCANNER

unsigned int loopCounter = 0;
static bool t = false;
void i2cScanner(void) {
  byte error, address;
  int nDevices;
 
  while(true) {
    Serial.print("Scanning ");
    Serial.print(loopCounter++);
    Serial.print("...\n");
  
    nDevices = 0;
    for(address = 1; address < 127; address++ ) {
      watchdog_update();
      // The i2c_scanner uses the return value of
      // the Write.endTransmisstion to see if
      // a device did acknowledge to the address.
      Wire.beginTransmission(address);
      error = Wire.endTransmission();
  
      if (error == 0) {
        Serial.print("I2C device found at address 0x");
        if (address < 16)
          Serial.print("0");
        Serial.print(address, HEX);
        Serial.println("  !");
  
        nDevices++;
      }
      else if (error == 4) {
        Serial.print("Unknown error at address 0x");
        if (address < 16)
          Serial.print("0");
        Serial.println(address, HEX);
      }    
    }
    if (nDevices == 0)
      Serial.println("No I2C devices found\n");
    else
      Serial.println("done\n");
  
    m_delay(500);           // wait 500 mseconds for next scan
  }

}
#endif

unsigned long getSeconds(void) {
  return ((millis() + 500) / 1000);
}

bool isDaylightSavingTime(int year, int month, int day) {
     if ((month > 3 && month < 10) || 
        (month == 3 && day >= 25) || 
        (month == 10 && day < 29)) {
        return true;
    }
    return false;
}

void adjustTime(int *year, int *month, int *day, int *hour, int *minute) {
    (*hour)++;
    if (isDaylightSavingTime(*year, *month, *day)) {
        (*hour)++;
        if (*hour == 24) {
            *hour = 0;
            (*day)++;
        }
    } else {
      (*hour)--;
        if (*hour == -1) {
            *hour = 23;
            (*day)--;
        }
    }
}

byte MSB(unsigned short value){
  return (byte)(value >> 8) & 0xFF;
}

byte LSB(unsigned short value){
  return (byte)(value & 0x00FF);
}

int MsbLsbToInt(byte msb, byte lsb) {
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

bool isWireBusy(unsigned int dataAddress) {
  Wire.beginTransmission(dataAddress);
  return Wire.endTransmission();
}

void resetEEPROM(void) {
#ifndef AT24C256
  if(!eepromStarted) {
    startRaspberryEEPROM(RASPBERRY_EEPROM_SIZE);
    for(int a = 0; a < RASPBERRY_EEPROM_SIZE; a++) {
      EEPROM.write(a, 0);
    }
    EEPROM.commit();
  }
#else
  for(int a = 0; a < AT24C256_EEPROM_SIZE; a++) {
    writeAT24(a, 0);
    watchdog_update();
  }
#endif
}

void writeAT24(unsigned int dataAddress, byte dataVal) {
  #ifdef AT24C256
  Wire.beginTransmission(EEPROM_I2C_ADDRESS);
  Wire.write(dataAddress >> 8);
  Wire.write(dataAddress & 0xff);
  Wire.write(dataVal);
  Wire.endTransmission();
  while(isWireBusy(EEPROM_I2C_ADDRESS)){   
    m_delay_microseconds(100);
    watchdog_update();
  }
  m_delay(5);
  #else
  EEPROM.write(dataAddress, dataVal);
  EEPROM.commit();
  #endif
}

// Function to read from EEPROM
byte readAT24(unsigned int dataAddress) {
  byte readData = 0;
  #ifdef AT24C256
  Wire.beginTransmission(EEPROM_I2C_ADDRESS);
  Wire.write(dataAddress >> 8);
  Wire.write(dataAddress & 0xff);
  Wire.endTransmission();
  Wire.requestFrom(EEPROM_I2C_ADDRESS, 1);
  if (Wire.available()) {
    readData = Wire.read();
  }
  #else
  readData = EEPROM.read(dataAddress);
  #endif
  return readData;
}

void writeAT24Int(unsigned int dataAddress, int dataVal) {
  writeAT24(dataAddress + 0, (dataVal >> 24) & 0xFF);
  writeAT24(dataAddress + 1, (dataVal >> 16) & 0xFF);
  writeAT24(dataAddress + 2, (dataVal >> 8) & 0xFF);
  writeAT24(dataAddress + 3, (dataVal & 0xFF));
}

int readAT24Int(unsigned int dataAddress) {
  return  (readAT24(dataAddress + 0) << 24) |
          (readAT24(dataAddress + 1) << 16) |
          (readAT24(dataAddress + 2) << 8) |
          (readAT24(dataAddress + 3));
}

float rroundf(float val) {
  return (int(val * 10) / 10.0);
}

float roundfWithPrecisionTo(float value, int precision) {
    float multiplier = 1.0;
    for (int i = 0; i < precision; ++i) {
        multiplier *= 10.0;
    }

    return (float)((int)(value * multiplier)) / multiplier;
}

bool isValidString(const char *s, int maxBufSize) {
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
  return true; 
}

unsigned short rgbToRgb565(unsigned char r, unsigned char g, unsigned char b) {
    unsigned short r5 = (r >> 3) & 0x1F;
    unsigned short g6 = (g >> 2) & 0x3F;
    unsigned short b5 = (b >> 3) & 0x1F;
    
    return (r5 << 11) | (g6 << 5) | b5;
}

const char *macToString(uint8_t mac[6]) {
  static char s[20];
  snprintf(s, sizeof(s) - 1, "%02X:%02X:%02X:%02X:%02X:%02X", mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
  return s;
}

const char *encToString(uint8_t enc) {
  #ifdef PICO_W
  switch (enc) {
    case ENC_TYPE_NONE: return "NONE";
    case ENC_TYPE_TKIP: return "WPA";
    case ENC_TYPE_CCMP: return "WPA2";
    case ENC_TYPE_AUTO: return "AUTO";
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
              *dst++ = *src++; // kopiuj jak jest błędny format
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
  #ifdef PICO_W
  deb("Beginning scan at %lu\n", millis());
  auto cnt = WiFi.scanNetworks();
  if (!cnt) {
    deb("No WiFi networks found");
  } else {
    deb("Found %d networks\n", cnt);
    deb("%32s %5s %17s %2s %4s", "SSID", "ENC", "BSSID        ", "CH", "RSSI");
    for (auto i = 0; i < cnt; i++) {
      uint8_t bssid[6];
      WiFi.BSSID(i, bssid);
      deb("%32s %5s %17s %2d %4ld", WiFi.SSID(i), encToString(WiFi.encryptionType(i)), macToString(bssid), WiFi.channel(i), WiFi.RSSI(i));
      
      if(networkToFind != NULL && strlen(networkToFind) > 0) {
        if(!strncmp(WiFi.SSID(i), networkToFind, strlen(networkToFind))) {
          deb("network %s is available", networkToFind);
          networkFound = true;
        }
      }
    }
  }
  deb("\n--- END --- at %lu\n", millis());
  #else
  deb("No PicoW configured, WiFi disabled");
  #endif
  return networkFound;
}

float mapfloat(float x, float in_min, float in_max, float out_min, float out_max) {
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

  uint32_t now = millis();
  if (now - lastTime >= time) {
    lastTime = now;
    lastValue = rand() % maxValue;
  }

  return lastValue;
}

float getRandomFloatEverySomeMillis(uint32_t time, float maxValue) {
    static uint32_t lastTime = 0;
    static float lastValue = -1.0f;

    uint32_t now = millis();
    if (now - lastTime >= time) {
        lastTime = now;
        uint32_t r = ((uint32_t)rand() << 16) | (uint32_t)rand(); // 32-bit los
        lastValue =  (r / (float)UINT32_MAX) * maxValue;        
    }

    return lastValue;
}

char *remove_non_ascii(const char* input) {
    static char output[256];
    int i = 0, j = 0;

    while (input[i]) {
        if ((unsigned char)input[i] == 0xC4 || (unsigned char)input[i] == 0xC5) {
            unsigned char next = input[i + 1];

            if (next == 0x85) output[j++] = 'a';     // ą
            else if (next == 0x87) output[j++] = 'c'; // ć
            else if (next == 0x99) output[j++] = 'e'; // ę
            else if (next == 0x82) output[j++] = 'l'; // ł
            else if (next == 0x84) output[j++] = 'n'; // ń
            else if (next == 0xB3) output[j++] = 'o'; // ó
            else if (next == 0x9B) output[j++] = 's'; // ś
            else if (next == 0xBA) output[j++] = 'z'; // ź
            else if (next == 0xBC) output[j++] = 'z'; // ż
            else if (next == 0x84) output[j++] = 'n'; // ń (C5 84)
            else if (next == 0x81) output[j++] = 'L'; // Ł
            else j++; 

            i += 2; 
        } else {
            output[j++] = input[i++];
        }
    }

    output[j] = '\0';
    return output;
}


