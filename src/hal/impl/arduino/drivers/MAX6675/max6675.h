// this library is public domain. enjoy!
// https://learn.adafruit.com/thermocouple/

#ifndef ADAFRUIT_MAX6675_H
#define ADAFRUIT_MAX6675_H

#include "Arduino.h"
#include "../../../../hal_sync.h"

/**************************************************************************/
/*!
    @brief  Class for communicating with thermocouple sensor
*/
/**************************************************************************/
class MAX6675 {
public:
  MAX6675(int8_t SCLK, int8_t CS, int8_t MISO);
  ~MAX6675();

  float readCelsius(void);
  float readFahrenheit(void);

  /*!    @brief  For compatibility with older versions
         @returns Temperature in F or NAN on failure! */
  float readFarenheit(void) { return readFahrenheit(); }

private:
  int8_t sclk, miso, cs;
  hal_mutex_t mutex_;
  uint8_t spiread(void);
};

#endif
