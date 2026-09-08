#include "mfrc522.h"

#include "hal/system/hal_system.h"

void MFRC522_SPI::PCD_WriteRegister(MFRC522::PCD_Register reg, byte value) {
  const byte frame[] = {(byte)(reg << 1), value};
  const hal_spi_device_operation_t operation = {HAL_SPI_DEVICE_OP_WRITE, frame,
                                                NULL, sizeof(frame)};
  PCD_RecordTransportStatus(
      hal_spi_device_transaction(&_device, &operation, 1u));
}

void MFRC522_SPI::PCD_WriteRegister(MFRC522::PCD_Register reg, byte count,
                                    byte *values) {
  const byte address = (byte)(reg << 1);
  const hal_spi_device_operation_t operations[] = {
      {HAL_SPI_DEVICE_OP_WRITE, &address, NULL, 1u},
      {HAL_SPI_DEVICE_OP_WRITE, values, NULL, count},
  };
  PCD_RecordTransportStatus(
      hal_spi_device_transaction(&_device, operations, COUNTOF(operations)));
}

byte MFRC522_SPI::PCD_ReadRegister(MFRC522::PCD_Register reg) {
  const byte address = (byte)(0x80 | (reg << 1));
  byte value = 0u;
  const hal_spi_device_operation_t operations[] = {
      {HAL_SPI_DEVICE_OP_WRITE, &address, NULL, 1u},
      {HAL_SPI_DEVICE_OP_TRANSFER_IN_PLACE, NULL, &value, 1u},
  };
  PCD_RecordTransportStatus(
      hal_spi_device_transaction(&_device, operations, COUNTOF(operations)));
  return value;
}

void MFRC522_SPI::PCD_ReadRegister(MFRC522::PCD_Register reg, byte count,
                                   byte *values, byte rxAlign) {
  if (count == 0) {
    return;
  }

  const byte address = (byte)(0x80 | (reg << 1));

  hal_status_t status = hal_spi_device_acquire(&_device);
  if (hal_status_is_error(status)) {
    PCD_RecordTransportStatus(status);
    return;
  }
  status = hal_spi_write(_device.bus, &address, 1u);
  for (byte index = 0; index < count; index++) {
    const byte tx = (index + 1u < count) ? address : 0u;
    byte value = 0u;
    if (hal_status_is_error(status)) {
      break;
    }
    status = hal_spi_transfer_ex(_device.bus, tx, &value);
    if (index == 0 && rxAlign) {
      const byte mask = (byte)((0xFFu << rxAlign) & 0xFFu);
      values[0] = (byte)((values[0] & ~mask) | (value & mask));
    } else {
      values[index] = value;
    }
  }
  PCD_RecordTransportStatus(hal_spi_device_finish(&_device, status));
}

bool MFRC522_SPI::PCD_Init() {
  const hal_spi_settings_t settings = _device.settings;
  const hal_status_t status =
      hal_spi_device_init(&_device, _device.bus, _device.cs_pin, &settings);
  PCD_RecordTransportStatus(status);
  if (hal_status_is_error(status)) {
    return false;
  }

  if (_resetPowerDownPin != UNUSED_PIN) {
    hal_gpio_set_mode(_resetPowerDownPin, HAL_GPIO_OUTPUT_HIGH);
    if (!hal_gpio_read(_resetPowerDownPin)) {
      hal_gpio_write(_resetPowerDownPin, true);
      hal_delay_ms(50);
      return true;
    }
  }
  return false;
}
