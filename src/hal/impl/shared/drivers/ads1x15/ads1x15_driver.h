#pragma once

/**
 * @file ads1x15_driver.h
 * @brief Arduino-free ADS101x/ADS111x driver built on JaszczurHAL I2C.
 *
 * This shared driver mirrors the public behaviour of Rob Tillaart's ADS1X15
 * Arduino library while replacing Arduino.h/Wire calls with JaszczurHAL
 * primitives. It is an internal implementation detail used by
 * hal_external_adc on RP2040 and STM32G474.
 */

#include "hal/hal_config.h"
#include "hal/hal_target.h"

#if (HAL_TARGET_IS_RP || HAL_TARGET_IS_STM32G474 || HAL_TARGET_IS_MOCK) &&     \
    defined(HAL_ENABLE_EXTERNAL_ADC) && defined(HAL_ENABLE_I2C)

#include <stdbool.h>
#include <stdint.h>

#define ADS1X15_LIB_VERSION ("0.6.1")

#ifndef ADS1015_ADDRESS
#define ADS1015_ADDRESS (0x48)
#endif

#ifndef ADS1115_ADDRESS
#define ADS1115_ADDRESS (0x48)
#endif

#define ADS1X15_OK (0)
#define ADS1X15_INVALID_VOLTAGE (-100)
#define ADS1X15_ERROR_TIMEOUT (-101)
#define ADS1X15_ERROR_I2C (-102)
#define ADS1X15_INVALID_GAIN (0xFF)
#define ADS1X15_INVALID_MODE (0xFE)

#define ADS1X15_MODE_CONTINUOUS (0x00)
#define ADS1X15_MODE_SINGLE (0x01)

#define ADS1X15_DATARATE_0 (0x00)
#define ADS1X15_DATARATE_1 (0x01)
#define ADS1X15_DATARATE_2 (0x02)
#define ADS1X15_DATARATE_3 (0x03)
#define ADS1X15_DATARATE_4 (0x04)
#define ADS1X15_DATARATE_5 (0x05)
#define ADS1X15_DATARATE_6 (0x06)
#define ADS1X15_DATARATE_7 (0x07)

#define ADS1X15_GAIN_6144MV (0x00)
#define ADS1X15_GAIN_4096MV (0x01)
#define ADS1X15_GAIN_2048MV (0x02)
#define ADS1X15_GAIN_1024MV (0x04)
#define ADS1X15_GAIN_0512MV (0x08)
#define ADS1X15_GAIN_0256MV (0x10)

#define ADS1x15_COMP_MODE_TRADITIONAL (0x00)
#define ADS1x15_COMP_MODE_WINDOW (0x01)

#define ADS1x15_COMP_POL_FALLING_EDGE (0x00)
#define ADS1x15_COMP_POL_RISING_EDGE (0x01)

#define ADS1x15_COMP_POL_NOLATCH (0x00)
#define ADS1x15_COMP_POL_LATCH (0x01)

#define ADS1x15_COMP_QUE_CONV_TRIGGER_1 (0x00)
#define ADS1x15_COMP_QUE_CONV_TRIGGER_2 (0x01)
#define ADS1x15_COMP_QUE_CONV_TRIGGER_4 (0x02)
#define ADS1x15_COMP_QUE_CONV_DISABLE (0x03)

#define ADS1x15_GAIN_6144MV_FSRANGE_V (6.144)
#define ADS1x15_GAIN_4096MV_FSRANGE_V (4.096)
#define ADS1x15_GAIN_2048MV_FSRANGE_V (2.048)
#define ADS1x15_GAIN_1024MV_FSRANGE_V (1.024)
#define ADS1x15_GAIN_0512MV_FSRANGE_V (0.512)
#define ADS1x15_GAIN_0256MV_FSRANGE_V (0.256)

class ADS1X15 {
public:
  static const char *LibName() { return "ADS1X15"; }
  static const char *LibVersion() { return "0.6.1"; }
  static const char *LibURL() {
    return "https://github.com/RobTillaart/ADS1X15";
  }
  static const char *LibAuthor() { return "Rob Tillaart"; }

  void reset();

  bool begin();
  bool isConnected();

  virtual void setGain(uint8_t gain = 0);
  virtual uint8_t getGain();

  float toVoltage(float value = 1);
  float getMaxVoltage();

  void setMode(uint8_t mode = 1);
  uint8_t getMode();

  void setDataRate(uint8_t dataRate = 4);
  uint8_t getDataRate();

  int16_t readADC(uint8_t pin = 0);
  int16_t readADC_Differential_0_1();
  int16_t getValue();

  void requestADC(uint8_t pin = 0);
  void requestADC_Differential_0_1();
  bool isBusy();
  bool isReady();
  uint8_t lastRequest();

  void setComparatorMode(uint8_t mode);
  uint8_t getComparatorMode();
  bool setComparatorOff();

  void setComparatorPolarity(uint8_t pol);
  uint8_t getComparatorPolarity();

  void setComparatorLatch(uint8_t latch);
  uint8_t getComparatorLatch();

  void setComparatorQueConvert(uint8_t mode);
  uint8_t getComparatorQueConvert();

  void setComparatorThresholdLow(int16_t lo);
  int16_t getComparatorThresholdLow();
  void setComparatorThresholdHigh(int16_t hi);
  int16_t getComparatorThresholdHigh();

  int8_t getError();

  void setWireClock(uint32_t clockSpeed = 100000);
  uint32_t getWireClock();

  inline uint16_t getMaxRegValue() { return (_config & 0x04) ? 32767 : 2047; }

protected:
  ADS1X15();

  uint8_t _config = 0;
  uint8_t _maxPorts = 0;
  uint8_t _address = 0;
  uint8_t _i2cBus = 0;
  uint8_t _conversionDelay = 0;
  uint8_t _bitShift = 0;
  uint16_t _gain = 0;
  uint16_t _mode = 0;
  uint16_t _datarate = 0;

  uint8_t _compMode = 0;
  uint8_t _compPol = 0;
  uint8_t _compLatch = 0;
  uint8_t _compQueConvert = 0;

  uint16_t _lastRequest = 0;

  int16_t _readADC(uint16_t readmode);
  void _requestADC(uint16_t readmode);
  bool _writeRegister(uint8_t address, uint8_t reg, uint16_t value);
  uint16_t _readRegister(uint8_t address, uint8_t reg);
  int8_t _error = ADS1X15_OK;
  uint32_t _clockSpeed = 0;
};

class ADS1013 : public ADS1X15 {
public:
  ADS1013(uint8_t Address = ADS1015_ADDRESS, uint8_t i2cBus = 0);
  void setGain(uint8_t gain) override;
  uint8_t getGain() override;
};

class ADS1014 : public ADS1X15 {
public:
  ADS1014(uint8_t Address = ADS1015_ADDRESS, uint8_t i2cBus = 0);
};

class ADS1015 : public ADS1X15 {
public:
  ADS1015(uint8_t Address = ADS1015_ADDRESS, uint8_t i2cBus = 0);
  int16_t readADC_Differential_0_3();
  int16_t readADC_Differential_1_3();
  int16_t readADC_Differential_2_3();
  int16_t readADC_Differential_0_2();
  int16_t readADC_Differential_1_2();
  void requestADC_Differential_0_3();
  void requestADC_Differential_1_3();
  void requestADC_Differential_2_3();
};

class ADS1113 : public ADS1X15 {
public:
  ADS1113(uint8_t address = ADS1115_ADDRESS, uint8_t i2cBus = 0);
  void setGain(uint8_t gain) override;
  uint8_t getGain() override;
};

class ADS1114 : public ADS1X15 {
public:
  ADS1114(uint8_t address = ADS1115_ADDRESS, uint8_t i2cBus = 0);
};

class ADS1115 : public ADS1X15 {
public:
  ADS1115(uint8_t address = ADS1115_ADDRESS, uint8_t i2cBus = 0);
  int16_t readADC_Differential_0_3();
  int16_t readADC_Differential_1_3();
  int16_t readADC_Differential_2_3();
  int16_t readADC_Differential_0_2();
  int16_t readADC_Differential_1_2();
  void requestADC_Differential_0_3();
  void requestADC_Differential_1_3();
  void requestADC_Differential_2_3();
};

#endif /* supported target && HAL_ENABLE_EXTERNAL_ADC && HAL_ENABLE_I2C */
