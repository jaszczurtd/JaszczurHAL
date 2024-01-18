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

static mutex_t debugMutex;
static mutex_t debugErrMutex;
void debugInit(void) {
  mutex_init(&debugMutex);
  mutex_init(&debugErrMutex);
  Serial.begin(9600);
}

NOINIT static char deb_buffer[128];
void deb(const char *format, ...) {

  mutex_enter_blocking(&debugMutex);

  va_list valist;
  va_start(valist, format);

  memset (deb_buffer, 0, sizeof(deb_buffer));
  vsnprintf(deb_buffer, sizeof(deb_buffer) - 1, format, valist);
  Serial.println(deb_buffer);

#ifdef SD_LOGGER
  updateSD(deb_buffer);
#endif

  va_end(valist);

  mutex_exit(&debugMutex);
}

NOINIT static char derr_buffer[128];
void derr(const char *format, ...) {

  mutex_enter_blocking(&debugErrMutex);

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

  mutex_exit(&debugErrMutex);
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
        average += analogRead(tpin);
        delayMicroseconds(10);
    }
    average /= NUMSAMPLES;

    return average;
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
    average = r / average;

    float steinhart;
    steinhart = average / thermistor;     // (R/Ro)
    steinhart = log(steinhart);                  // ln(R/Ro)
    steinhart /= BCOEFFICIENT;                   // 1/B * ln(R/Ro)
    steinhart += 1.0 / (TEMPERATURENOMINAL + 273.15); // + (1/To)
    steinhart = 1.0 / steinhart;                 // Invert
    steinhart -= 273.15;                         // convert absolute temp to C    

    return steinhart;
}

int percentToGivenVal(float percent, int givenVal) {
    return int(((percent / 100.0) * givenVal));
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
  
    delay(500);           // wait 500 mseconds for next scan
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
    delayMicroseconds(100);
    watchdog_update();
  }
  delay(5);
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