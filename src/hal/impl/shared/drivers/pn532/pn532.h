/**
 * @file pn532.h
 * @brief Shared PN532 NFC/RFID driver for JaszczurHAL.
 *
 * This driver is based on the Adafruit_PN532 driver by Adafruit Industries,
 * which is distributed under the BSD license. The protocol flow, command
 * framing, ACK handling and high-level PN532 commands are intentionally kept
 * close to that proven implementation while using JaszczurHAL transports and
 * status codes.
 */
#pragma once

#include "hal/hal_gpio.h"
#include "hal/hal_i2c.h"
#include "hal/hal_spi.h"
#include "hal/hal_status.h"
#include "hal/hal_sync.h"

#ifdef HAL_ENABLE_UART
#include "hal/hal_uart.h"
#endif

#include <stddef.h>
#include <stdint.h>

#ifndef PN532_UNUSED_PIN
#define PN532_UNUSED_PIN (UINT8_MAX)
#endif

#ifndef PN532_I2C_DEFAULT_ADDRESS
#define PN532_I2C_DEFAULT_ADDRESS (0x24)
#endif

#ifndef PN532_SPI_DEFAULT_CLOCK_HZ
#define PN532_SPI_DEFAULT_CLOCK_HZ (1000000UL)
#endif

static constexpr uint8_t PN532_PREAMBLE = 0x00;
static constexpr uint8_t PN532_STARTCODE1 = 0x00;
static constexpr uint8_t PN532_STARTCODE2 = 0xFF;
static constexpr uint8_t PN532_POSTAMBLE = 0x00;
static constexpr uint8_t PN532_HOSTTOPN532 = 0xD4;
static constexpr uint8_t PN532_PN532TOHOST = 0xD5;

static constexpr uint8_t PN532_COMMAND_GETFIRMWAREVERSION = 0x02;
static constexpr uint8_t PN532_COMMAND_SAMCONFIGURATION = 0x14;
static constexpr uint8_t PN532_COMMAND_INLISTPASSIVETARGET = 0x4A;
static constexpr uint8_t PN532_COMMAND_INDATAEXCHANGE = 0x40;

static constexpr uint8_t PN532_MIFARE_ISO14443A = 0x00;
static constexpr uint8_t PN532_MIFARE_CMD_AUTH_A = 0x60;
static constexpr uint8_t PN532_MIFARE_CMD_AUTH_B = 0x61;
static constexpr uint8_t PN532_MIFARE_CMD_READ = 0x30;
static constexpr uint8_t PN532_MIFARE_CMD_WRITE = 0xA0;
static constexpr uint8_t PN532_MIFARE_ULTRALIGHT_CMD_WRITE = 0xA2;

static constexpr uint32_t PN532_DEFAULT_TIMEOUT_MS = 1000;
static constexpr size_t PN532_PACKETBUFFER_SIZE = 64;

class PN532_BUS_DEVICE {
public:
  virtual ~PN532_BUS_DEVICE() = default;
  virtual hal_status_t begin() { return HAL_OK; }
  virtual hal_status_t wakeup() { return HAL_OK; }
  virtual hal_status_t isReady(bool *ready) = 0;
  virtual hal_status_t writeCommand(const uint8_t *data, size_t len) = 0;
  virtual hal_status_t readData(uint8_t *data, size_t len) = 0;
};

class PN532 {
public:
  explicit PN532(PN532_BUS_DEVICE *dev);
  ~PN532();

  hal_status_t begin();
  hal_status_t wakeup();
  hal_status_t SAMConfig();
  hal_status_t getFirmwareVersion(uint32_t *version);
  hal_status_t
  sendCommandCheckAck(const uint8_t *cmd, size_t cmdlen,
                      uint16_t timeout_ms = PN532_DEFAULT_TIMEOUT_MS);

  hal_status_t
  readPassiveTargetID(uint8_t cardbaudrate, uint8_t *uid, uint8_t *uidLength,
                      uint16_t timeout_ms = PN532_DEFAULT_TIMEOUT_MS);
  hal_status_t inListPassiveTarget(uint8_t *response, size_t response_len);
  hal_status_t inDataExchange(const uint8_t *send, size_t send_len,
                              uint8_t *response, size_t *response_len);

  hal_status_t mifareclassic_AuthenticateBlock(const uint8_t *uid,
                                               uint8_t uidLen,
                                               uint32_t blockNumber,
                                               uint8_t keyNumber,
                                               const uint8_t *keyData);
  hal_status_t mifareclassic_ReadDataBlock(uint8_t blockNumber, uint8_t *data);
  hal_status_t mifareclassic_WriteDataBlock(uint8_t blockNumber,
                                            const uint8_t *data);
  hal_status_t mifareultralight_ReadPage(uint8_t page, uint8_t *buffer);
  hal_status_t mifareultralight_WritePage(uint8_t page, const uint8_t *buffer);
  hal_status_t ntag2xx_WritePage(uint8_t page, const uint8_t *buffer);

  static hal_status_t checkResponseFrame(const uint8_t *frame, size_t len,
                                         uint8_t command);

private:
  void lock();
  void unlock();
  hal_status_t waitReady(uint16_t timeout_ms);
  hal_status_t readAck();
  hal_status_t readResponse(uint8_t command, uint8_t *response,
                            size_t *response_len, size_t min_frame_len,
                            uint16_t timeout_ms);

  PN532_BUS_DEVICE *_dev;
  hal_mutex_t _mutex;
  uint8_t _packetbuffer[PN532_PACKETBUFFER_SIZE];
};

class PN532_SPI : public PN532_BUS_DEVICE {
public:
  PN532_SPI(uint8_t chipSelectPin, uint8_t resetPin = PN532_UNUSED_PIN,
            uint8_t bus = 0);

  hal_status_t begin() override;
  hal_status_t wakeup() override;
  hal_status_t isReady(bool *ready) override;
  hal_status_t writeCommand(const uint8_t *data, size_t len) override;
  hal_status_t readData(uint8_t *data, size_t len) override;

private:
  hal_status_t transferFrame(uint8_t command, const uint8_t *tx, size_t tx_len,
                             uint8_t *rx, size_t rx_len);

  uint8_t _chipSelectPin;
  uint8_t _resetPin;
  uint8_t _bus;
  hal_spi_settings_t _settings;
};

#ifdef HAL_ENABLE_I2C
class PN532_I2C : public PN532_BUS_DEVICE {
public:
  PN532_I2C(uint8_t resetPin = PN532_UNUSED_PIN,
            uint8_t address = PN532_I2C_DEFAULT_ADDRESS, uint8_t bus = 0);

  hal_status_t begin() override;
  hal_status_t wakeup() override;
  hal_status_t isReady(bool *ready) override;
  hal_status_t writeCommand(const uint8_t *data, size_t len) override;
  hal_status_t readData(uint8_t *data, size_t len) override;

private:
  uint8_t _resetPin;
  uint8_t _address;
  uint8_t _bus;
};
#endif

#ifdef HAL_ENABLE_UART
class PN532_UART : public PN532_BUS_DEVICE {
public:
  PN532_UART(hal_uart_port_t port, uint8_t rxPin, uint8_t txPin,
             uint8_t resetPin = PN532_UNUSED_PIN);
  explicit PN532_UART(hal_uart_t uart, uint8_t resetPin = PN532_UNUSED_PIN);
  ~PN532_UART() override;

  hal_status_t begin() override;
  hal_status_t wakeup() override;
  hal_status_t isReady(bool *ready) override;
  hal_status_t writeCommand(const uint8_t *data, size_t len) override;
  hal_status_t readData(uint8_t *data, size_t len) override;

private:
  hal_status_t readByte(uint8_t *value, uint16_t timeout_ms);

  hal_uart_t _uart;
  hal_uart_port_t _port;
  uint8_t _rxPin;
  uint8_t _txPin;
  uint8_t _resetPin;
  bool _ownsUart;
};
#endif
