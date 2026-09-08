#include "pn532.h"

#include "hal/core/jh_endian.h"

#include "hal/core/hal_mutex_once.h"
#include "hal/system/hal_system.h"

#include <cstring>

static const uint8_t kPn532Ack[] = {0x00, 0x00, 0xFF, 0x00, 0xFF, 0x00};
static constexpr size_t kPn532FrameHeaderSize = 5u;
static constexpr size_t kPn532FrameOverhead = 7u;
static constexpr size_t kPn532MinimumFrameSize = 9u;
static constexpr size_t kPn532DiscardBufferSize = 16u;

static hal_status_t discardResponseBytes(PN532_BUS_DEVICE *device,
                                         size_t count) {
  uint8_t discard[kPn532DiscardBufferSize] = {};
  while (count > 0u) {
    const size_t read_len = count < sizeof(discard) ? count : sizeof(discard);
    const hal_status_t status = device->readData(discard, read_len);
    if (status != HAL_OK) {
      return status;
    }
    count -= read_len;
  }
  return HAL_OK;
}

PN532::PN532(PN532_BUS_DEVICE *dev) : _dev(dev), _mutex(NULL), _packetbuffer{} {
  (void)jh_hal_mutex_try_create_once(&_mutex);
}

PN532::~PN532() {
  if (_mutex != NULL) {
    hal_mutex_destroy(_mutex);
    _mutex = NULL;
  }
}

void PN532::lock() {
  hal_mutex_t mutex = jh_hal_mutex_try_create_once(&_mutex);
  if (mutex != NULL) {
    hal_mutex_lock(mutex);
  }
}

void PN532::unlock() {
  if (_mutex != NULL) {
    hal_mutex_unlock(_mutex);
  }
}

hal_status_t PN532::begin() {
  if (_dev == NULL) {
    return HAL_EINVAL;
  }

  lock();
  hal_status_t status = _dev->begin();
  unlock();
  return status;
}

hal_status_t PN532::wakeup() {
  if (_dev == NULL) {
    return HAL_EINVAL;
  }

  lock();
  hal_status_t status = _dev->wakeup();
  unlock();
  return status;
}

hal_status_t PN532::SAMConfig() {
  const uint8_t command[] = {
      PN532_COMMAND_SAMCONFIGURATION,
      0x01, // Normal mode.
      0x14, // Timeout: 50 ms * 20 = 1 second.
      0x01, // Use IRQ pin.
  };

  lock();
  hal_status_t status = sendCommandCheckAck(command, sizeof(command));
  if (status == HAL_OK) {
    size_t response_len = sizeof(_packetbuffer);
    status = readResponse(PN532_COMMAND_SAMCONFIGURATION, _packetbuffer,
                          &response_len, 9u, PN532_DEFAULT_TIMEOUT_MS);
  }
  unlock();
  return status;
}

hal_status_t PN532::getFirmwareVersion(uint32_t *version) {
  if (version == NULL) {
    return HAL_EINVAL;
  }

  const uint8_t command[] = {PN532_COMMAND_GETFIRMWAREVERSION};

  lock();
  hal_status_t status = sendCommandCheckAck(command, sizeof(command));
  if (status == HAL_OK) {
    size_t response_len = sizeof(_packetbuffer);
    status = readResponse(PN532_COMMAND_GETFIRMWAREVERSION, _packetbuffer,
                          &response_len, 13u, PN532_DEFAULT_TIMEOUT_MS);
    if (status == HAL_OK) {
      *version = jh_load_be32(&_packetbuffer[7]);
    }
  }
  unlock();
  return status;
}

hal_status_t PN532::sendCommandCheckAck(const uint8_t *cmd, size_t cmdlen,
                                        uint16_t timeout_ms) {
  if (_dev == NULL || cmd == NULL || cmdlen == 0 ||
      cmdlen > (PN532_PACKETBUFFER_SIZE - 8)) {
    return HAL_EINVAL;
  }

  _packetbuffer[0] = PN532_PREAMBLE;
  _packetbuffer[1] = PN532_STARTCODE1;
  _packetbuffer[2] = PN532_STARTCODE2;

  const uint8_t len = (uint8_t)(cmdlen + 1);
  _packetbuffer[3] = len;
  _packetbuffer[4] = (uint8_t)(~len + 1);
  _packetbuffer[5] = PN532_HOSTTOPN532;

  uint8_t checksum = PN532_HOSTTOPN532;
  for (size_t i = 0; i < cmdlen; ++i) {
    _packetbuffer[6 + i] = cmd[i];
    checksum = (uint8_t)(checksum + cmd[i]);
  }
  _packetbuffer[6 + cmdlen] = (uint8_t)(~checksum + 1);
  _packetbuffer[7 + cmdlen] = PN532_POSTAMBLE;

  hal_status_t status = _dev->writeCommand(_packetbuffer, cmdlen + 8);
  if (status != HAL_OK) {
    return status;
  }

  status = waitReady(timeout_ms);
  if (status != HAL_OK) {
    return status;
  }
  return readAck();
}

hal_status_t PN532::waitReady(uint16_t timeout_ms) {
  if (_dev == NULL) {
    return HAL_EINVAL;
  }

  const uint32_t start = hal_millis();
  while (!hal_millis_deadline_expired(start, timeout_ms)) {
    bool ready = false;
    hal_status_t status = _dev->isReady(&ready);
    if (status != HAL_OK) {
      return status;
    }
    if (ready) {
      return HAL_OK;
    }
    hal_delay_ms(10);
  }
  return HAL_ETIMEOUT;
}

hal_status_t PN532::readAck() {
  uint8_t ack[sizeof(kPn532Ack)] = {};
  hal_status_t status = _dev->readData(ack, sizeof(ack));
  if (status != HAL_OK) {
    return status;
  }
  return (std::memcmp(ack, kPn532Ack, sizeof(kPn532Ack)) == 0) ? HAL_OK
                                                               : HAL_EPROTO;
}

hal_status_t PN532::readResponse(uint8_t command, uint8_t *response,
                                 size_t *response_len, size_t min_frame_len,
                                 uint16_t timeout_ms) {
  if (_dev == NULL || response == NULL || response_len == NULL) {
    return HAL_EINVAL;
  }

  hal_status_t status = waitReady(timeout_ms);
  if (status != HAL_OK) {
    return status;
  }

  uint8_t header[kPn532FrameHeaderSize] = {};
  status = _dev->readData(header, sizeof(header));
  if (status != HAL_OK) {
    return status;
  }

  if (header[0] != PN532_PREAMBLE || header[1] != PN532_STARTCODE1 ||
      header[2] != PN532_STARTCODE2 || (uint8_t)(header[3] + header[4]) != 0u ||
      header[3] < 2u) {
    /* LEN is bounded to one byte. Even when the header is corrupt, treating
     * it as a recovery hint lets byte-stream transports reach the following
     * frame. SPI/I2C serve the same bytes from their physical-read cache. */
    (void)discardResponseBytes(_dev, (size_t)header[3] + kPn532FrameOverhead -
                                         kPn532FrameHeaderSize);
    return HAL_EPROTO;
  }

  const size_t frame_len = (size_t)header[3] + kPn532FrameOverhead;
  const size_t response_capacity = *response_len;
  const size_t storage_capacity = response_capacity < PN532_PACKETBUFFER_SIZE
                                      ? response_capacity
                                      : PN532_PACKETBUFFER_SIZE;
  const size_t header_copy_len =
      storage_capacity < sizeof(header) ? storage_capacity : sizeof(header);
  std::memcpy(response, header, header_copy_len);

  size_t remaining = frame_len - sizeof(header);
  size_t stored = header_copy_len;
  if (stored < storage_capacity) {
    const size_t writable = storage_capacity - stored;
    const size_t read_len = remaining < writable ? remaining : writable;
    if (read_len > 0u) {
      status = _dev->readData(response + stored, read_len);
      if (status != HAL_OK) {
        return status;
      }
      remaining -= read_len;
    }
  }

  status = discardResponseBytes(_dev, remaining);
  if (status != HAL_OK) {
    return status;
  }

  if (frame_len > response_capacity || frame_len > PN532_PACKETBUFFER_SIZE) {
    return HAL_EOVERFLOW;
  }
  if (frame_len < min_frame_len || frame_len < kPn532MinimumFrameSize) {
    return HAL_EPROTO;
  }

  status = checkResponseFrame(response, frame_len, command);
  if (status != HAL_OK) {
    return status;
  }

  const size_t payload_len = response[3] - 2u;
  *response_len = payload_len;
  return HAL_OK;
}

hal_status_t PN532::checkResponseFrame(const uint8_t *frame, size_t len,
                                       uint8_t command) {
  if (frame == NULL || len < kPn532FrameHeaderSize) {
    return HAL_EINVAL;
  }
  if (frame[0] != PN532_PREAMBLE || frame[1] != PN532_STARTCODE1 ||
      frame[2] != PN532_STARTCODE2) {
    return HAL_EPROTO;
  }
  if ((uint8_t)(frame[3] + frame[4]) != 0) {
    return HAL_EPROTO;
  }
  if (frame[3] < 2u) {
    return HAL_EPROTO;
  }
  const size_t frame_len = (size_t)frame[3] + kPn532FrameOverhead;
  if (len < frame_len) {
    return HAL_EOVERFLOW;
  }
  if (frame[5] != PN532_PN532TOHOST || frame[6] != (uint8_t)(command + 1u)) {
    return HAL_EPROTO;
  }

  uint8_t checksum = 0;
  for (size_t i = 0; i < frame[3]; ++i) {
    checksum = (uint8_t)(checksum + frame[5 + i]);
  }
  checksum = (uint8_t)(checksum + frame[5 + frame[3]]);
  if (checksum != 0) {
    return HAL_EPROTO;
  }
  if (frame[6u + frame[3]] != PN532_POSTAMBLE) {
    return HAL_EPROTO;
  }

  return HAL_OK;
}

hal_status_t PN532::readPassiveTargetID(uint8_t cardbaudrate, uint8_t *uid,
                                        uint8_t *uidLength,
                                        uint16_t timeout_ms) {
  if (uid == NULL || uidLength == NULL) {
    return HAL_EINVAL;
  }

  const uint8_t command[] = {
      PN532_COMMAND_INLISTPASSIVETARGET,
      0x01,
      cardbaudrate,
  };

  lock();
  size_t response_len = sizeof(_packetbuffer);
  hal_status_t status =
      sendCommandCheckAck(command, sizeof(command), timeout_ms);
  if (status == HAL_OK) {
    status = readResponse(PN532_COMMAND_INLISTPASSIVETARGET, _packetbuffer,
                          &response_len, 10u, timeout_ms);
  }
  if (status == HAL_OK) {
    if (_packetbuffer[7] != 1) {
      status = HAL_ENOENT;
    } else if (response_len < 6u) {
      status = HAL_EPROTO;
    } else {
      *uidLength = _packetbuffer[12];
      if (*uidLength == 0u || *uidLength > 7u ||
          *uidLength > response_len - 6u) {
        status = HAL_EPROTO;
      } else {
        std::memcpy(uid, _packetbuffer + 13, *uidLength);
      }
    }
  }
  unlock();
  return status;
}

hal_status_t PN532::inListPassiveTarget(uint8_t *response,
                                        size_t response_len) {
  if (response == NULL || response_len == 0) {
    return HAL_EINVAL;
  }

  const uint8_t command[] = {PN532_COMMAND_INLISTPASSIVETARGET, 1,
                             PN532_MIFARE_ISO14443A};

  lock();
  hal_status_t status = sendCommandCheckAck(command, sizeof(command));
  if (status == HAL_OK) {
    size_t len = sizeof(_packetbuffer);
    status = readResponse(PN532_COMMAND_INLISTPASSIVETARGET, _packetbuffer,
                          &len, 10u, PN532_DEFAULT_TIMEOUT_MS);
    if (status == HAL_OK) {
      const size_t copy_len = (response_len < len) ? response_len : len;
      std::memcpy(response, _packetbuffer + 7, copy_len);
      if (response_len < len) {
        status = HAL_EOVERFLOW;
      }
    }
  }
  unlock();
  return status;
}

hal_status_t PN532::inDataExchange(const uint8_t *send, size_t send_len,
                                   uint8_t *response, size_t *response_len) {
  if (send == NULL || send_len == 0 || response == NULL ||
      response_len == NULL || send_len > (PN532_PACKETBUFFER_SIZE - 2)) {
    return HAL_EINVAL;
  }
  lock();
  _packetbuffer[0] = PN532_COMMAND_INDATAEXCHANGE;
  _packetbuffer[1] = 1;
  std::memcpy(_packetbuffer + 2, send, send_len);

  hal_status_t status = sendCommandCheckAck(_packetbuffer, send_len + 2);
  if (status == HAL_OK) {
    size_t len = sizeof(_packetbuffer);
    status = readResponse(PN532_COMMAND_INDATAEXCHANGE, _packetbuffer, &len,
                          10u, PN532_DEFAULT_TIMEOUT_MS);
    if (status == HAL_OK) {
      if (_packetbuffer[7] != 0x00) {
        status = HAL_EPROTO;
      } else {
        const size_t payload_len = (len > 1) ? (len - 1) : 0;
        if (*response_len < payload_len) {
          status = HAL_EOVERFLOW;
        } else {
          std::memcpy(response, _packetbuffer + 8, payload_len);
          *response_len = payload_len;
        }
      }
    }
  }
  unlock();
  return status;
}

hal_status_t PN532::mifareclassic_AuthenticateBlock(const uint8_t *uid,
                                                    uint8_t uidLen,
                                                    uint32_t blockNumber,
                                                    uint8_t keyNumber,
                                                    const uint8_t *keyData) {
  if (uid == NULL || keyData == NULL || uidLen == 0 || uidLen > 10 ||
      blockNumber > 255) {
    return HAL_EINVAL;
  }

  uint8_t command[1 + 1 + 6 + 10] = {};
  command[0] =
      (keyNumber != 0) ? PN532_MIFARE_CMD_AUTH_B : PN532_MIFARE_CMD_AUTH_A;
  command[1] = (uint8_t)blockNumber;
  std::memcpy(command + 2, keyData, 6);
  std::memcpy(command + 8, uid, uidLen);

  uint8_t response[1] = {};
  size_t response_len = sizeof(response);
  return inDataExchange(command, uidLen + 8, response, &response_len);
}

hal_status_t PN532::mifareclassic_ReadDataBlock(uint8_t blockNumber,
                                                uint8_t *data) {
  if (data == NULL) {
    return HAL_EINVAL;
  }

  const uint8_t command[] = {PN532_MIFARE_CMD_READ, blockNumber};
  uint8_t response[16] = {};
  size_t response_len = sizeof(response);
  const hal_status_t status =
      inDataExchange(command, sizeof(command), response, &response_len);
  if (status != HAL_OK) {
    return status;
  }
  if (response_len != sizeof(response)) {
    return HAL_EPROTO;
  }
  std::memcpy(data, response, sizeof(response));
  return HAL_OK;
}

hal_status_t PN532::mifareclassic_WriteDataBlock(uint8_t blockNumber,
                                                 const uint8_t *data) {
  if (data == NULL) {
    return HAL_EINVAL;
  }

  uint8_t command[18] = {};
  command[0] = PN532_MIFARE_CMD_WRITE;
  command[1] = blockNumber;
  std::memcpy(command + 2, data, 16);

  uint8_t response[1] = {};
  size_t response_len = sizeof(response);
  return inDataExchange(command, sizeof(command), response, &response_len);
}

hal_status_t PN532::mifareultralight_ReadPage(uint8_t page, uint8_t *buffer) {
  if (buffer == NULL) {
    return HAL_EINVAL;
  }

  const uint8_t command[] = {PN532_MIFARE_CMD_READ, page};
  uint8_t pages[16] = {};
  size_t response_len = sizeof(pages);
  const hal_status_t status =
      inDataExchange(command, sizeof(command), pages, &response_len);
  if (status != HAL_OK) {
    return status;
  }
  if (response_len < 4u) {
    return HAL_EPROTO;
  }
  std::memcpy(buffer, pages, 4u);
  return HAL_OK;
}

hal_status_t PN532::mifareultralight_WritePage(uint8_t page,
                                               const uint8_t *buffer) {
  if (buffer == NULL) {
    return HAL_EINVAL;
  }

  uint8_t command[6] = {};
  command[0] = PN532_MIFARE_ULTRALIGHT_CMD_WRITE;
  command[1] = page;
  std::memcpy(command + 2, buffer, 4);

  uint8_t response[1] = {};
  size_t response_len = sizeof(response);
  return inDataExchange(command, sizeof(command), response, &response_len);
}

hal_status_t PN532::ntag2xx_WritePage(uint8_t page, const uint8_t *buffer) {
  return mifareultralight_WritePage(page, buffer);
}
