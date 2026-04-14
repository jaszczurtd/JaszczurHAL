#include "../../../../hal_config.h"
#if !defined(HAL_DISABLE_THERMOCOUPLE) && !defined(HAL_DISABLE_MAX6675)

// this library is public domain. enjoy!
// https://learn.adafruit.com/thermocouple/

#include "max6675.h"

/**************************************************************************/
/*!
    @brief  Initialize a MAX6675 sensor
    @param   SCLK The Arduino pin connected to Clock
    @param   CS The Arduino pin connected to Chip Select
    @param   MISO The Arduino pin connected to Data Out
*/
/**************************************************************************/
MAX6675::MAX6675(int8_t SCLK, int8_t CS, int8_t MISO) {
  sclk = SCLK;
  cs = CS;
  miso = MISO;
  mutex_ = hal_mutex_create();

  // define pin modes
  pinMode(cs, OUTPUT);
  pinMode(sclk, OUTPUT);
  pinMode(miso, INPUT);

  digitalWrite(cs, HIGH);
}

MAX6675::~MAX6675() {
  if (mutex_) {
    hal_mutex_destroy(mutex_);
    mutex_ = NULL;
  }
}

/**************************************************************************/
/*!
    @brief  Read the Celsius temperature
    @returns Temperature in C or NAN on failure!
*/
/**************************************************************************/
float MAX6675::readCelsius(void) {
  hal_mutex_lock(mutex_);

  uint16_t v;

  digitalWrite(cs, LOW);
  delayMicroseconds(10);

  v = spiread();
  v <<= 8;
  v |= spiread();

  digitalWrite(cs, HIGH);

  float result = NAN;
  if (v & 0x4) {
    // no thermocouple attached
  } else {
    v >>= 3;
    result = v * 0.25f;
  }

  hal_mutex_unlock(mutex_);
  return result;
}

/**************************************************************************/
/*!
    @brief  Read the Fahenheit temperature
    @returns Temperature in F or NAN on failure!
*/
/**************************************************************************/
float MAX6675::readFahrenheit(void) { return readCelsius() * 9.0 / 5.0 + 32; }

byte MAX6675::spiread(void) {
  int i;
  byte d = 0;

  for (i = 7; i >= 0; i--) {
    digitalWrite(sclk, LOW);
    delayMicroseconds(10);
    if (digitalRead(miso)) {
      // set the bit to 0 no matter what
      d |= (1 << i);
    }

    digitalWrite(sclk, HIGH);
    delayMicroseconds(10);
  }

  return d;
}

#endif /* !HAL_DISABLE_THERMOCOUPLE && !HAL_DISABLE_MAX6675 */
