//
//  utils.c
//  Index
//
//  Created by Marcin Kielesiński on 07/12/2019.
//

#include "tools.h"

static bool loggerInitialized = false;
File loggerFile;

bool initSDLogger(int cs) {
#ifdef SD_LOGGER
  EEPROM.begin(512);

  int logNumber = (EEPROM.read(0) << 24) | (EEPROM.read(1) << 16) | (EEPROM.read(2) << 8) | EEPROM.read(3);
  char fileName[16] = {0};
  snprintf(fileName, sizeof(fileName), "log%d.txt", logNumber);
  logNumber++;

  EEPROM.write(0, (logNumber >> 24) & 0xFF);
  EEPROM.write(1, (logNumber >> 16) & 0xFF);
  EEPROM.write(2, (logNumber >> 8) & 0xFF);
  EEPROM.write(3, (logNumber & 0xFF));
  EEPROM.commit();

  loggerInitialized = SD.begin(cs);
  if(loggerInitialized) {
    loggerFile = SD.open(fileName, FILE_WRITE);
  }
#endif
  return loggerInitialized;
}

bool isSDLoggerInitialized(void) {
  return loggerInitialized;
}

void deb(const char *format, ...) {

  va_list valist;
  va_start(valist, format);

  char buffer[128];
  memset (buffer, 0, sizeof(buffer));
  vsnprintf(buffer, sizeof(buffer) - 1, format, valist);
  Serial.println(buffer);

#ifdef SD_LOGGER
  if(isSDLoggerInitialized()) {
    loggerFile.println(buffer);
    loggerFile.flush();
  }
#endif

  va_end(valist);
}

void derr(const char *format, ...) {

  va_list valist;
  va_start(valist, format);

  char buffer[128];
  memset (buffer, 0, sizeof(buffer));

  const char *error = "ERROR! ";
  int len = strlen(error);

  strcpy(buffer, error);

  vsnprintf(buffer + len, sizeof(buffer) - 1 - len, format, valist);
  Serial.println(buffer);

#ifdef SD_LOGGER
  if(isSDLoggerInitialized()) {
    loggerFile.println(buffer);
    loggerFile.flush();
  }
#endif

  va_end(valist);
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
        delay(1);
    }
    average /= NUMSAMPLES;

    return average;
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