#include "mfrc522.h"

#ifdef HAL_ENABLE_I2C

#include "hal/system/hal_system.h"

void MFRC522_I2C::PCD_WriteRegister(MFRC522::PCD_Register reg, byte value) {
  hal_i2c_lock_bus(_bus);
  hal_i2c_begin_transmission_bus(_bus, _chipAddress);
  if (hal_i2c_write_bus(_bus, (byte)reg) != 1u) {
    PCD_RecordTransportStatus(HAL_EIO);
  }
  if (hal_i2c_write_bus(_bus, value) != 1u) {
    PCD_RecordTransportStatus(HAL_EIO);
  }
  PCD_RecordTransportStatus(hal_i2c_end_transmission_bus_ex(_bus));
  hal_i2c_unlock_bus(_bus);
}

void MFRC522_I2C::PCD_WriteRegister(MFRC522::PCD_Register reg, byte count,
                                    byte *values) {
  hal_i2c_lock_bus(_bus);
  hal_i2c_begin_transmission_bus(_bus, _chipAddress);
  if (hal_i2c_write_bus(_bus, (byte)reg) != 1u) {
    PCD_RecordTransportStatus(HAL_EIO);
  }
  for (byte index = 0; index < count; index++) {
    if (hal_i2c_write_bus(_bus, values[index]) != 1u) {
      PCD_RecordTransportStatus(HAL_EIO);
    }
  }
  PCD_RecordTransportStatus(hal_i2c_end_transmission_bus_ex(_bus));
  hal_i2c_unlock_bus(_bus);
}

byte MFRC522_I2C::PCD_ReadRegister(MFRC522::PCD_Register reg) {
  byte value = 0;
  const byte address = (byte)reg;
  const hal_status_t status =
      hal_i2c_write_read_bus_ex(_bus, _chipAddress, &address, 1u, &value, 1u);
  PCD_RecordTransportStatus(status);
  if (hal_status_is_error(status)) {
    return 0;
  }
  return value;
}

void MFRC522_I2C::PCD_ReadRegister(MFRC522::PCD_Register reg, byte count,
                                   byte *values, byte rxAlign) {
  if (count == 0) {
    return;
  }

  const byte address = (byte)reg;
  byte local[MFRC522::FIFO_SIZE];
  byte *dst = values;
  if (rxAlign) {
    if (count > MFRC522::FIFO_SIZE) {
      return;
    }
    dst = local;
  }

  const hal_status_t status =
      hal_i2c_write_read_bus_ex(_bus, _chipAddress, &address, 1u, dst, count);
  PCD_RecordTransportStatus(status);
  if (hal_status_is_error(status)) {
    return;
  }

  if (rxAlign) {
    byte mask = 0;
    for (byte i = rxAlign; i <= 7; i++) {
      mask |= (byte)(1u << i);
    }
    values[0] = (byte)((values[0] & ~mask) | (local[0] & mask));
    for (byte index = 1; index < count; index++) {
      values[index] = local[index];
    }
  }
}

bool MFRC522_I2C::PCD_Init() {
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

#endif
