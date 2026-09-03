/*
 * ADS101x/ADS111x register logic is modeled after Rob Tillaart's ADS1X15
 * Arduino library. This implementation was rewritten as an Arduino-free
 * JaszczurHAL shared driver: it keeps the proven register map, gain/mode/data
 * rate mapping, blocking/async conversion flow, comparator configuration,
 * threshold access and ADS101x/ADS111x class variants, but uses only HAL I2C,
 * timing and idle primitives.
 *
 * MIT License
 *
 * Copyright (c) 2013-2026 Rob Tillaart
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 * SOFTWARE.
 */

#include "hal/core/hal_target.h"
#if (HAL_TARGET_IS_RP || HAL_TARGET_IS_STM32G474 || HAL_TARGET_IS_MOCK)

#include "hal/core/hal_config.h"
#if defined(HAL_ENABLE_EXTERNAL_ADC) && defined(HAL_ENABLE_I2C)

#include "ads1x15_driver.h"

#include "hal/core/jh_endian.h"

#include "hal/i2c/hal_i2c.h"
#include "hal/system/hal_system.h"

#define ADS1015_CONVERSION_DELAY (1)
#define ADS1115_CONVERSION_DELAY (8)

#define ADS1X15_REG_CONVERT (0x00)
#define ADS1X15_REG_CONFIG (0x01)
#define ADS1X15_REG_LOW_THRESHOLD (0x02)
#define ADS1X15_REG_HIGH_THRESHOLD (0x03)

#define ADS1X15_OS_NOT_BUSY (0x8000)
#define ADS1X15_OS_START_SINGLE (0x8000)

#define ADS1X15_MUX_DIFF_0_1 (0x0000)
#define ADS1X15_MUX_DIFF_0_3 (0x1000)
#define ADS1X15_MUX_DIFF_1_3 (0x2000)
#define ADS1X15_MUX_DIFF_2_3 (0x3000)
#define ADS1X15_READ_0 (0x4000)
#define ADS1X15_READ_1 (0x5000)
#define ADS1X15_READ_2 (0x6000)
#define ADS1X15_READ_3 (0x7000)

#define ADS1X15_PGA_6_144V (0x0000)
#define ADS1X15_PGA_4_096V (0x0200)
#define ADS1X15_PGA_2_048V (0x0400)
#define ADS1X15_PGA_1_024V (0x0600)
#define ADS1X15_PGA_0_512V (0x0800)
#define ADS1X15_PGA_0_256V (0x0A00)

#define ADS1X15_MODE_CONTINUE (0x0000)
#define ADS1X15_MODE_ONCE (0x0100)

#define ADS1X15_COMP_MODE_TRADITIONAL (0x0000)
#define ADS1X15_COMP_MODE_WINDOW (0x0010)
#define ADS1X15_COMP_POL_ACTIV_LOW (0x0000)
#define ADS1X15_COMP_POL_ACTIV_HIGH (0x0008)
#define ADS1X15_COMP_NON_LATCH (0x0000)
#define ADS1X15_COMP_LATCH (0x0004)
#define ADS1X15_COMPARATOR_OFF (0xFFF7)

#define ADS_CONF_CHAN_1 (0x00)
#define ADS_CONF_CHAN_4 (0x01)
#define ADS_CONF_RES_12 (0x00)
#define ADS_CONF_RES_16 (0x04)
#define ADS_CONF_NOGAIN (0x00)
#define ADS_CONF_GAIN (0x10)
#define ADS_CONF_NOCOMP (0x00)
#define ADS_CONF_COMP (0x20)

ADS1X15::ADS1X15() {}

void ADS1X15::reset() {
  setGain(0);
  setMode(1);
  setDataRate(4);

  _compMode = 0;
  _compPol = 1;
  _compLatch = 0;
  _compQueConvert = 3;
  _lastRequest = 0xFFFF;
}

bool ADS1X15::begin() {
  if ((_address < 0x48) || (_address > 0x4B))
    return false;
  if (!isConnected())
    return false;
  return true;
}

bool ADS1X15::isConnected() { return !hal_i2c_is_busy_bus(_i2cBus, _address); }

void ADS1X15::setGain(uint8_t gain) {
  if (!(_config & ADS_CONF_GAIN))
    gain = 0;
  switch (gain) {
  default:
  case 0:
    _gain = ADS1X15_PGA_6_144V;
    break;
  case 1:
    _gain = ADS1X15_PGA_4_096V;
    break;
  case 2:
    _gain = ADS1X15_PGA_2_048V;
    break;
  case 4:
    _gain = ADS1X15_PGA_1_024V;
    break;
  case 8:
    _gain = ADS1X15_PGA_0_512V;
    break;
  case 16:
    _gain = ADS1X15_PGA_0_256V;
    break;
  }
}

uint8_t ADS1X15::getGain() {
  if (!(_config & ADS_CONF_GAIN))
    return 0;
  switch (_gain) {
  case ADS1X15_PGA_6_144V:
    return 0;
  case ADS1X15_PGA_4_096V:
    return 1;
  case ADS1X15_PGA_2_048V:
    return 2;
  case ADS1X15_PGA_1_024V:
    return 4;
  case ADS1X15_PGA_0_512V:
    return 8;
  case ADS1X15_PGA_0_256V:
    return 16;
  default:
    break;
  }
  _error = ADS1X15_INVALID_GAIN;
  return _error;
}

float ADS1X15::toVoltage(float value) {
  if (value == 0)
    return 0;

  float volts = getMaxVoltage();
  if (volts < 0)
    return volts;

  volts *= value;
  if (_config & ADS_CONF_RES_16) {
    volts *= 3.0518509475997E-5f;
  } else {
    volts *= 4.8851978505129E-4f;
  }
  return volts;
}

float ADS1X15::getMaxVoltage() {
  switch (_gain) {
  case ADS1X15_PGA_6_144V:
    return ADS1x15_GAIN_6144MV_FSRANGE_V;
  case ADS1X15_PGA_4_096V:
    return ADS1x15_GAIN_4096MV_FSRANGE_V;
  case ADS1X15_PGA_2_048V:
    return ADS1x15_GAIN_2048MV_FSRANGE_V;
  case ADS1X15_PGA_1_024V:
    return ADS1x15_GAIN_1024MV_FSRANGE_V;
  case ADS1X15_PGA_0_512V:
    return ADS1x15_GAIN_0512MV_FSRANGE_V;
  case ADS1X15_PGA_0_256V:
    return ADS1x15_GAIN_0256MV_FSRANGE_V;
  default:
    break;
  }
  _error = ADS1X15_INVALID_VOLTAGE;
  return _error;
}

void ADS1X15::setMode(uint8_t mode) {
  switch (mode) {
  case 0:
    _mode = ADS1X15_MODE_CONTINUE;
    break;
  default:
  case 1:
    _mode = ADS1X15_MODE_ONCE;
    break;
  }
}

uint8_t ADS1X15::getMode(void) {
  switch (_mode) {
  case ADS1X15_MODE_CONTINUE:
    return 0;
  case ADS1X15_MODE_ONCE:
    return 1;
  default:
    break;
  }
  _error = ADS1X15_INVALID_MODE;
  return _error;
}

void ADS1X15::setDataRate(uint8_t dataRate) {
  _datarate = dataRate;
  if (_datarate > 7)
    _datarate = 4;
  _datarate <<= 5;
}

uint8_t ADS1X15::getDataRate(void) { return (_datarate >> 5) & 0x07; }

int16_t ADS1X15::readADC(uint8_t pin) {
  if (pin >= _maxPorts)
    return 0;
  uint16_t mode = ((4 + pin) << 12);
  return _readADC(mode);
}

int16_t ADS1X15::readADC_Differential_0_1() {
  return _readADC(ADS1X15_MUX_DIFF_0_1);
}

int16_t ADS1X15::getValue() {
  int16_t raw = (int16_t)_readRegister(_address, ADS1X15_REG_CONVERT);
  if (_bitShift)
    raw >>= _bitShift;
  return raw;
}

void ADS1X15::requestADC(uint8_t pin) {
  if (pin >= _maxPorts)
    return;
  uint16_t mode = ((4 + pin) << 12);
  _requestADC(mode);
}

void ADS1X15::requestADC_Differential_0_1() {
  _requestADC(ADS1X15_MUX_DIFF_0_1);
}

bool ADS1X15::isBusy() { return isReady() == false; }

bool ADS1X15::isReady() {
  uint16_t val = _readRegister(_address, ADS1X15_REG_CONFIG);
  return ((val & ADS1X15_OS_NOT_BUSY) > 0);
}

uint8_t ADS1X15::lastRequest() {
  switch (_lastRequest) {
  case ADS1X15_READ_0:
    return 0x00;
  case ADS1X15_READ_1:
    return 0x01;
  case ADS1X15_READ_2:
    return 0x02;
  case ADS1X15_READ_3:
    return 0x03;
  case ADS1X15_MUX_DIFF_0_1:
    return 0x10;
  case ADS1X15_MUX_DIFF_0_3:
    return 0x30;
  case ADS1X15_MUX_DIFF_1_3:
    return 0x31;
  case ADS1X15_MUX_DIFF_2_3:
    return 0x32;
  default:
    break;
  }
  return 0xFF;
}

void ADS1X15::setComparatorMode(uint8_t mode) { _compMode = mode == 0 ? 0 : 1; }

uint8_t ADS1X15::getComparatorMode() { return _compMode; }

bool ADS1X15::setComparatorOff() {
  uint16_t config = _readRegister(_address, ADS1X15_REG_CONFIG);
  config &= ADS1X15_COMPARATOR_OFF;
  return _writeRegister(_address, ADS1X15_REG_CONFIG, config);
}

void ADS1X15::setComparatorPolarity(uint8_t pol) {
  _compPol = pol == 0 ? 0 : 1;
}

uint8_t ADS1X15::getComparatorPolarity() { return _compPol; }

void ADS1X15::setComparatorLatch(uint8_t latch) {
  _compLatch = latch == 0 ? 0 : 1;
}

uint8_t ADS1X15::getComparatorLatch() { return _compLatch; }

void ADS1X15::setComparatorQueConvert(uint8_t mode) {
  _compQueConvert = (mode < 3) ? mode : 3;
}

uint8_t ADS1X15::getComparatorQueConvert() { return _compQueConvert; }

void ADS1X15::setComparatorThresholdLow(int16_t lo) {
  _writeRegister(_address, ADS1X15_REG_LOW_THRESHOLD, (uint16_t)lo);
}

int16_t ADS1X15::getComparatorThresholdLow() {
  return (int16_t)_readRegister(_address, ADS1X15_REG_LOW_THRESHOLD);
}

void ADS1X15::setComparatorThresholdHigh(int16_t hi) {
  _writeRegister(_address, ADS1X15_REG_HIGH_THRESHOLD, (uint16_t)hi);
}

int16_t ADS1X15::getComparatorThresholdHigh() {
  return (int16_t)_readRegister(_address, ADS1X15_REG_HIGH_THRESHOLD);
}

int8_t ADS1X15::getError() {
  int8_t rv = _error;
  _error = ADS1X15_OK;
  return rv;
}

void ADS1X15::setWireClock(uint32_t clockSpeed) {
  _clockSpeed = clockSpeed;
  hal_i2c_set_clock_bus(_i2cBus, _clockSpeed);
}

uint32_t ADS1X15::getWireClock() { return _clockSpeed; }

int16_t ADS1X15::_readADC(uint16_t readmode) {
  _requestADC(readmode);

  if (_mode == ADS1X15_MODE_ONCE) {
    uint32_t start = hal_millis();
    uint8_t timeOut = (128 >> (_datarate >> 5)) + 10;
    while (isBusy()) {
      if ((hal_millis() - start) > timeOut) {
        _error = ADS1X15_ERROR_TIMEOUT;
        return ADS1X15_ERROR_TIMEOUT;
      }
      hal_idle();
    }
  } else {
    hal_delay_ms(_conversionDelay);
  }
  return getValue();
}

void ADS1X15::_requestADC(uint16_t readmode) {
  uint16_t config = ADS1X15_OS_START_SINGLE;
  config |= readmode;
  config |= _gain;
  config |= _mode;
  config |= _datarate;
  if (_compMode)
    config |= ADS1X15_COMP_MODE_WINDOW;
  else
    config |= ADS1X15_COMP_MODE_TRADITIONAL;
  if (_compPol)
    config |= ADS1X15_COMP_POL_ACTIV_HIGH;
  else
    config |= ADS1X15_COMP_POL_ACTIV_LOW;
  if (_compLatch)
    config |= ADS1X15_COMP_LATCH;
  else
    config |= ADS1X15_COMP_NON_LATCH;
  config |= _compQueConvert;
  _writeRegister(_address, ADS1X15_REG_CONFIG, config);

  _lastRequest = readmode;
}

bool ADS1X15::_writeRegister(uint8_t address, uint8_t reg, uint16_t value) {
  uint8_t data[3] = {reg, 0u, 0u};
  jh_store_be16(&data[1], value);

  hal_i2c_begin_transmission_bus(_i2cBus, address);
  bool ok = true;
  for (uint8_t i = 0; i < 3; ++i) {
    if (hal_i2c_write_bus(_i2cBus, data[i]) != 1u) {
      ok = false;
      break;
    }
  }
  uint8_t rv = hal_i2c_end_transmission_bus(_i2cBus);
  if (!ok || rv != 0) {
    _error = ADS1X15_ERROR_I2C;
    return false;
  }
  return true;
}

uint16_t ADS1X15::_readRegister(uint8_t address, uint8_t reg) {
  uint8_t raw[2] = {0, 0};
  if (hal_i2c_write_read_bus(_i2cBus, address, &reg, 1u, raw, 2u)) {
    return jh_load_be16(raw);
  }
  _error = ADS1X15_ERROR_I2C;
  return 0x0000;
}

ADS1013::ADS1013(uint8_t address, uint8_t i2cBus) {
  _address = address;
  _i2cBus = i2cBus;
  _config = ADS_CONF_NOCOMP;
  _conversionDelay = ADS1015_CONVERSION_DELAY;
  _bitShift = 4;
  _maxPorts = 1;
  _gain = ADS1X15_PGA_2_048V;
  reset();
}

void ADS1013::setGain(uint8_t gain) {
  (void)gain;
  _gain = ADS1X15_PGA_2_048V;
}

uint8_t ADS1013::getGain() { return 2; }

ADS1014::ADS1014(uint8_t address, uint8_t i2cBus) {
  _address = address;
  _i2cBus = i2cBus;
  _config = ADS_CONF_COMP | ADS_CONF_GAIN;
  _conversionDelay = ADS1015_CONVERSION_DELAY;
  _bitShift = 4;
  _maxPorts = 1;
  reset();
}

ADS1015::ADS1015(uint8_t address, uint8_t i2cBus) {
  _address = address;
  _i2cBus = i2cBus;
  _config = ADS_CONF_COMP | ADS_CONF_GAIN | ADS_CONF_RES_12 | ADS_CONF_CHAN_4;
  _conversionDelay = ADS1015_CONVERSION_DELAY;
  _bitShift = 4;
  _maxPorts = 4;
  reset();
}

int16_t ADS1015::readADC_Differential_0_3() {
  return _readADC(ADS1X15_MUX_DIFF_0_3);
}

int16_t ADS1015::readADC_Differential_1_3() {
  return _readADC(ADS1X15_MUX_DIFF_1_3);
}

int16_t ADS1015::readADC_Differential_2_3() {
  return _readADC(ADS1X15_MUX_DIFF_2_3);
}

int16_t ADS1015::readADC_Differential_0_2() {
  /* Upstream-compatible pseudo differential path: two single-ended reads. */
  return readADC(0) - readADC(2);
}

int16_t ADS1015::readADC_Differential_1_2() {
  /* Upstream-compatible pseudo differential path: two single-ended reads. */
  return readADC(1) - readADC(2);
}

void ADS1015::requestADC_Differential_0_3() {
  _requestADC(ADS1X15_MUX_DIFF_0_3);
}

void ADS1015::requestADC_Differential_1_3() {
  _requestADC(ADS1X15_MUX_DIFF_1_3);
}

void ADS1015::requestADC_Differential_2_3() {
  _requestADC(ADS1X15_MUX_DIFF_2_3);
}

ADS1113::ADS1113(uint8_t address, uint8_t i2cBus) {
  _address = address;
  _i2cBus = i2cBus;
  _config = ADS_CONF_RES_16;
  _conversionDelay = ADS1115_CONVERSION_DELAY;
  _bitShift = 0;
  _maxPorts = 1;
  _gain = ADS1X15_PGA_2_048V;
  reset();
}

void ADS1113::setGain(uint8_t gain) {
  (void)gain;
  _gain = ADS1X15_PGA_2_048V;
}

uint8_t ADS1113::getGain() { return 2; }

ADS1114::ADS1114(uint8_t address, uint8_t i2cBus) {
  _address = address;
  _i2cBus = i2cBus;
  _config = ADS_CONF_COMP | ADS_CONF_GAIN | ADS_CONF_RES_16;
  _conversionDelay = ADS1115_CONVERSION_DELAY;
  _bitShift = 0;
  _maxPorts = 1;
  reset();
}

ADS1115::ADS1115(uint8_t address, uint8_t i2cBus) {
  _address = address;
  _i2cBus = i2cBus;
  _config = ADS_CONF_COMP | ADS_CONF_GAIN | ADS_CONF_RES_16 | ADS_CONF_CHAN_4;
  _conversionDelay = ADS1115_CONVERSION_DELAY;
  _bitShift = 0;
  _maxPorts = 4;
  reset();
}

int16_t ADS1115::readADC_Differential_0_3() {
  return _readADC(ADS1X15_MUX_DIFF_0_3);
}

int16_t ADS1115::readADC_Differential_1_3() {
  return _readADC(ADS1X15_MUX_DIFF_1_3);
}

int16_t ADS1115::readADC_Differential_2_3() {
  return _readADC(ADS1X15_MUX_DIFF_2_3);
}

int16_t ADS1115::readADC_Differential_0_2() {
  /* Upstream-compatible pseudo differential path: two single-ended reads. */
  return readADC(0) - readADC(2);
}

int16_t ADS1115::readADC_Differential_1_2() {
  /* Upstream-compatible pseudo differential path: two single-ended reads. */
  return readADC(1) - readADC(2);
}

void ADS1115::requestADC_Differential_0_3() {
  _requestADC(ADS1X15_MUX_DIFF_0_3);
}

void ADS1115::requestADC_Differential_1_3() {
  _requestADC(ADS1X15_MUX_DIFF_1_3);
}

void ADS1115::requestADC_Differential_2_3() {
  _requestADC(ADS1X15_MUX_DIFF_2_3);
}

#endif /* HAL_ENABLE_EXTERNAL_ADC && HAL_ENABLE_I2C */
#endif /* supported target */
