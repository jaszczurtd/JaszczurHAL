#include "../../../../hal_config.h"
#if !defined(HAL_DISABLE_THERMOCOUPLE) && !defined(HAL_DISABLE_MCP9600) && !defined(HAL_DISABLE_I2C)

/**************************************************************************/
/*!
  @file     Adafruit_MCP9600.cpp

  @mainpage Adafruit MCP9600 I2C Thermocouple ADC driver

  @section intro Introduction

  This is a library for the Adafruit MCP9600 breakout board
  ----> https://www.adafruit.com/product/4101

  Adafruit invests time and resources providing this open source code,
  please support Adafruit and open-source hardware by purchasing
  products from Adafruit!

  @section author Author

  K. Townsend (Adafruit Industries)

  @section license License

  BSD (see license.txt)
*/
/**************************************************************************/
#include "Adafruit_MCP9600.h"

#ifndef HAL_DISABLE_I2C
#include "../../../../hal_i2c.h"
#endif

#include <math.h>

/**************************************************************************/
/*!
    @brief  Instantiates a new MCP9600 class
*/
/**************************************************************************/
Adafruit_MCP9600::Adafruit_MCP9600()
    : _wire(&Wire), _i2c_addr(MCP9600_I2CADDR_DEFAULT),
      _mutex(hal_mutex_create()) {
  _device_id = 0x40;
}

Adafruit_MCP9600::~Adafruit_MCP9600() {
  if (_mutex) {
    hal_mutex_destroy(_mutex);
    _mutex = NULL;
  }
}

static bool mcp9600_map_wire_bus(TwoWire *wire, uint8_t *bus_out) {
  if (wire == &Wire) {
    *bus_out = 0;
    return true;
  }
#if defined(WIRE_INTERFACES_COUNT) && (WIRE_INTERFACES_COUNT > 1)
  if (wire == &Wire1) {
    *bus_out = 1;
    return true;
  }
#endif
  return false;
}

void Adafruit_MCP9600::lock_bus_(void) {
  hal_mutex_lock(_mutex);
#ifndef HAL_DISABLE_I2C
  uint8_t bus = 0;
  if (mcp9600_map_wire_bus(_wire, &bus)) {
    hal_i2c_lock_bus(bus);
  }
#endif
}

void Adafruit_MCP9600::unlock_bus_(void) {
#ifndef HAL_DISABLE_I2C
  uint8_t bus = 0;
  if (mcp9600_map_wire_bus(_wire, &bus)) {
    hal_i2c_unlock_bus(bus);
  }
#endif
  hal_mutex_unlock(_mutex);
}

bool Adafruit_MCP9600::write_register_(uint8_t reg, const uint8_t *data,
                                       uint8_t len) {
  if (!_wire) {
    return false;
  }

  _wire->beginTransmission(_i2c_addr);
  _wire->write(reg);
  for (uint8_t i = 0; i < len; i++) {
    _wire->write(data[i]);
  }

  return (_wire->endTransmission() == 0);
}

bool Adafruit_MCP9600::read_register_(uint8_t reg, uint8_t *data, uint8_t len) {
  if (!_wire || !data) {
    return false;
  }

  _wire->beginTransmission(_i2c_addr);
  _wire->write(reg);
  if (_wire->endTransmission(false) != 0) {
    return false;
  }

  uint8_t received = _wire->requestFrom(_i2c_addr, len);
  if (received != len) {
    while (_wire->available()) {
      (void)_wire->read();
    }
    return false;
  }

  for (uint8_t i = 0; i < len; i++) {
    if (!_wire->available()) {
      return false;
    }
    data[i] = (uint8_t)_wire->read();
  }

  return true;
}

bool Adafruit_MCP9600::read_register_u8_(uint8_t reg, uint8_t &value) {
  return read_register_(reg, &value, 1);
}

bool Adafruit_MCP9600::write_register_u8_(uint8_t reg, uint8_t value) {
  return write_register_(reg, &value, 1);
}

/**************************************************************************/
/*!
    @brief  Sets up the I2C connection and tests that the sensor was found.
    @param i2c_addr The I2C address of the target device, default is 0x67
    @param theWire Pointer to an I2C device we'll use to communicate
    default is Wire
    @return true if sensor was found, otherwise false.
*/
/**************************************************************************/
boolean Adafruit_MCP9600::begin(uint8_t i2c_addr, TwoWire *theWire) {
  if (!theWire) {
    return false;
  }

  lock_bus_();
  _wire = theWire;
  _i2c_addr = i2c_addr;

  uint8_t id_raw[2] = {0, 0};
  if (!read_register_(MCP9600_DEVICEID, id_raw, 2)) {
    unlock_bus_();
    return false;
  }

  if ((id_raw[0] != 0x40) && (id_raw[0] != 0x41)) {
    unlock_bus_();
    return false;
  }
  _device_id = id_raw[0];

  if (!write_register_u8_(MCP9600_DEVICECONFIG, 0x80)) {
    unlock_bus_();
    return false;
  }

  unlock_bus_();
  return true;
}

/**************************************************************************/
/*!
    @brief  Read temperature at the end of the thermocouple
    @return Floating point temperature in Centigrade
*/
/**************************************************************************/
float Adafruit_MCP9600::readThermocouple(void) {
  lock_bus_();

  uint8_t config = 0;
  if (!read_register_u8_(MCP9600_DEVICECONFIG, config) || (config & 0x03)) {
    unlock_bus_();
    return NAN;
  }

  uint8_t raw[2] = {0, 0};
  if (!read_register_(MCP9600_HOTJUNCTION, raw, 2)) {
    unlock_bus_();
    return NAN;
  }

  unlock_bus_();

  int16_t therm = (int16_t)((uint16_t(raw[0]) << 8) | raw[1]);
  float temp = therm;
  temp *= 0.0625f; // 0.0625*C per LSB
  return temp;
}

/**************************************************************************/
/*!
    @brief  Read temperature at the MCP9600 chip body
    @return Floating point temperature in Centigrade
*/
/**************************************************************************/
float Adafruit_MCP9600::readAmbient(void) {
  lock_bus_();

  uint8_t config = 0;
  if (!read_register_u8_(MCP9600_DEVICECONFIG, config) || (config & 0x03)) {
    unlock_bus_();
    return NAN;
  }

  uint8_t raw[2] = {0, 0};
  if (!read_register_(MCP9600_COLDJUNCTION, raw, 2)) {
    unlock_bus_();
    return NAN;
  }

  unlock_bus_();

  int16_t cold = (int16_t)((uint16_t(raw[0]) << 8) | raw[1]);
  float temp = cold;
  temp *= 0.0625f; // 0.0625*C per LSB
  return temp;
}

/**************************************************************************/
/*!
    @brief  Whether to have the sensor enabled and working or in sleep mode
    @param  flag True to be in awake mode, False for sleep mode
*/
/**************************************************************************/
void Adafruit_MCP9600::enable(bool flag) {
  lock_bus_();

  uint8_t config = 0;
  if (!read_register_u8_(MCP9600_DEVICECONFIG, config)) {
    unlock_bus_();
    return;
  }

  config &= ~0x03;
  config |= flag ? 0x00 : 0x01;
  (void)write_register_u8_(MCP9600_DEVICECONFIG, config);

  unlock_bus_();
}

/**************************************************************************/
/*!
    @brief  Whether the sensor is enabled and working or in sleep mode
    @returns True if in awake mode, False if in sleep mode
*/
/**************************************************************************/
bool Adafruit_MCP9600::enabled(void) {
  lock_bus_();

  uint8_t config = 0;
  bool ok = read_register_u8_(MCP9600_DEVICECONFIG, config);

  unlock_bus_();

  if (!ok) {
    return false;
  }

  return ((config & 0x03) == 0x00);
}

/**************************************************************************/
/*!
    @brief  The desired ADC resolution setter
    @param  resolution Can be MCP9600_ADCRESOLUTION_18,
    MCP9600_ADCRESOLUTION_16, MCP9600_ADCRESOLUTION_14,
    or MCP9600_ADCRESOLUTION_12.
*/
/**************************************************************************/
void Adafruit_MCP9600::setADCresolution(MCP9600_ADCResolution resolution) {
  lock_bus_();

  uint8_t config = 0;
  if (!read_register_u8_(MCP9600_DEVICECONFIG, config)) {
    unlock_bus_();
    return;
  }

  config &= ~(0x03 << 5);
  config |= (uint8_t(resolution) & 0x03) << 5;
  (void)write_register_u8_(MCP9600_DEVICECONFIG, config);

  unlock_bus_();
}

/**************************************************************************/
/*!
    @brief  The desired ADC resolution getter
    @returns The resolution: MCP9600_ADCRESOLUTION_18,
    MCP9600_ADCRESOLUTION_16, MCP9600_ADCRESOLUTION_14,
    or MCP9600_ADCRESOLUTION_12.
*/
/**************************************************************************/
MCP9600_ADCResolution Adafruit_MCP9600::getADCresolution(void) {
  lock_bus_();

  uint8_t config = 0;
  bool ok = read_register_u8_(MCP9600_DEVICECONFIG, config);

  unlock_bus_();

  if (!ok) {
    return MCP9600_ADCRESOLUTION_18;
  }

  return (MCP9600_ADCResolution)((config >> 5) & 0x03);
}

/**************************************************************************/
/*!
    @brief  Read the raw ADC voltage, say for self calculating a temperature
    @returns The 32-bit signed value from the ADC DATA register
*/
/**************************************************************************/
int32_t Adafruit_MCP9600::readADC(void) {
  lock_bus_();

  uint8_t raw[3] = {0, 0, 0};
  bool ok = read_register_(MCP9600_RAWDATAADC, raw, 3);

  unlock_bus_();

  if (!ok) {
    return 0;
  }

  int32_t reading = (int32_t(raw[0]) << 16) | (int32_t(raw[1]) << 8) | raw[2];
  if (reading & 0x800000) {
    reading |= 0xFF000000;
  }
  return reading;
}

/**************************************************************************/
/*!
    @brief  The desired thermocouple type getter
    @returns The selected type: MCP9600_TYPE_K, MCP9600_TYPE_J,
    MCP9600_TYPE_T, MCP9600_TYPE_N, MCP9600_TYPE_S, MCP9600_TYPE_E,
    MCP9600_TYPE_B or MCP9600_TYPE_R
*/
/**************************************************************************/
MCP9600_ThemocoupleType Adafruit_MCP9600::getThermocoupleType(void) {
  lock_bus_();

  uint8_t sensorconfig = 0;
  bool ok = read_register_u8_(MCP9600_SENSORCONFIG, sensorconfig);

  unlock_bus_();

  if (!ok) {
    return MCP9600_TYPE_K;
  }

  return (MCP9600_ThemocoupleType)((sensorconfig >> 4) & 0x07);
}

/**************************************************************************/
/*!
    @brief  The desired thermocouple type setter
    @param thermotype The desired type: MCP9600_TYPE_K, MCP9600_TYPE_J,
    MCP9600_TYPE_T, MCP9600_TYPE_N, MCP9600_TYPE_S, MCP9600_TYPE_E,
    MCP9600_TYPE_B or MCP9600_TYPE_R
*/
/**************************************************************************/
void Adafruit_MCP9600::setThermocoupleType(MCP9600_ThemocoupleType thermotype) {
  lock_bus_();

  uint8_t sensorconfig = 0;
  if (!read_register_u8_(MCP9600_SENSORCONFIG, sensorconfig)) {
    unlock_bus_();
    return;
  }

  sensorconfig &= ~(0x07 << 4);
  sensorconfig |= (uint8_t(thermotype) & 0x07) << 4;
  (void)write_register_u8_(MCP9600_SENSORCONFIG, sensorconfig);

  unlock_bus_();
}

/**************************************************************************/
/*!
    @brief   The desired filter coefficient getter
    @returns How many readings we will be averaging, can be from 0-7
*/
/**************************************************************************/
uint8_t Adafruit_MCP9600::getFilterCoefficient(void) {
  lock_bus_();

  uint8_t sensorconfig = 0;
  bool ok = read_register_u8_(MCP9600_SENSORCONFIG, sensorconfig);

  unlock_bus_();

  if (!ok) {
    return 0;
  }

  return sensorconfig & 0x07;
}

/**************************************************************************/
/*!
    @brief   The desired filter coefficient setter
    @param filtercount How many readings we will be averaging, can be from 0-7
*/
/**************************************************************************/
void Adafruit_MCP9600::setFilterCoefficient(uint8_t filtercount) {
  lock_bus_();

  uint8_t sensorconfig = 0;
  if (!read_register_u8_(MCP9600_SENSORCONFIG, sensorconfig)) {
    unlock_bus_();
    return;
  }

  sensorconfig &= ~0x07;
  sensorconfig |= (filtercount & 0x07);
  (void)write_register_u8_(MCP9600_SENSORCONFIG, sensorconfig);

  unlock_bus_();
}

/**************************************************************************/
/*!
    @brief  Getter for alert temperature setting
    @param  alert Which alert output we're getting, can be 1 to 4
    @return Floating point temperature in Centigrade
*/
/**************************************************************************/
float Adafruit_MCP9600::getAlertTemperature(uint8_t alert) {
  if ((alert < 1) || (alert > 4)) {
    return NAN;
  }

  lock_bus_();

  uint8_t raw[2] = {0, 0};
  bool ok = read_register_(MCP9600_ALERTLIMIT_1 + alert - 1, raw, 2);

  unlock_bus_();

  if (!ok) {
    return NAN;
  }

  int16_t therm = (int16_t)((uint16_t(raw[0]) << 8) | raw[1]);
  float temp = therm;
  temp *= 0.0625f; // 0.0625*C per LSB
  return temp;
}

/**************************************************************************/
/*!
    @brief  Setter for alert temperature setting
    @param  alert Which alert output we're getting, can be 1 to 4
    @param  temp  Floating point temperature in Centigrade
*/
/**************************************************************************/
void Adafruit_MCP9600::setAlertTemperature(uint8_t alert, float temp) {
  if ((alert < 1) || (alert > 4)) {
    return;
  }

  int16_t therm = (int16_t)(temp / 0.0625f); // 0.0625*C per LSB
  uint8_t raw[2] = {(uint8_t)((therm >> 8) & 0xFF), (uint8_t)(therm & 0xFF)};

  lock_bus_();
  (void)write_register_(MCP9600_ALERTLIMIT_1 + alert - 1, raw, 2);
  unlock_bus_();
}

/**************************************************************************/
/*!
    @brief Configure temperature alert
    @param alert Which alert output we're getting, can be 1 to 4
    @param enabled Whether this alert is on or off
    @param rising True if we want an alert when the temperature rises above.
    False for alert on falling below.
    @param alertColdJunction Whether the temperature we're watching is the
    internal chip temperature (true) or the thermocouple (false). Default is
    false
    @param activeHigh Whether output pin goes high on alert (true) or low
   (false)
    @param interruptMode Whether output pin latches on until we clear it (true)
    or comparator mode (false)
*/
/**************************************************************************/
void Adafruit_MCP9600::configureAlert(uint8_t alert, bool enabled, bool rising,
                                      bool alertColdJunction, bool activeHigh,
                                      bool interruptMode) {
  if ((alert < 1) || (alert > 4)) {
    return;
  }

  uint8_t c = 0;
  if (enabled) {
    c |= 0x1;
  }
  if (interruptMode) {
    c |= 0x2;
  }
  if (activeHigh) {
    c |= 0x4;
  }
  if (rising) {
    c |= 0x8;
  }
  if (alertColdJunction) {
    c |= 0x10;
  }

  lock_bus_();
  (void)write_register_u8_(MCP9600_ALERTCONFIG_1 + alert - 1, c);
  unlock_bus_();
}

/**************************************************************************/
/*!
    @brief  Getter for status register
    @return 8-bit status, see datasheet for bits
*/
/**************************************************************************/
uint8_t Adafruit_MCP9600::getStatus(void) {
  lock_bus_();

  uint8_t status = 0;
  bool ok = read_register_u8_(MCP9600_STATUS, status);

  unlock_bus_();

  return ok ? status : 0;
}

/**************************************************************************/
/*!
    @brief  Sets the resolution for ambient (cold junction) temperature readings
    @param  res_value Ambient_Resolution enum value to set resolution
*/
/**************************************************************************/
void Adafruit_MCP9600::setAmbientResolution(Ambient_Resolution res_value) {
  lock_bus_();

  uint8_t config = 0;
  if (!read_register_u8_(MCP9600_DEVICECONFIG, config)) {
    unlock_bus_();
    return;
  }

  config &= ~0xC0; // Clear existing resolution bits
  config |= ((~uint8_t(res_value) & 0x03) << 6);
  (void)write_register_u8_(MCP9600_DEVICECONFIG, config);

  unlock_bus_();
}

#endif /* !HAL_DISABLE_THERMOCOUPLE && !HAL_DISABLE_MCP9600 && !HAL_DISABLE_I2C */
