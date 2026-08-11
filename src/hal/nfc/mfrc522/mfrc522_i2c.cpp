#include "mfrc522.h"

#ifdef HAL_ENABLE_I2C

#include "hal/system/hal_system.h"

void MFRC522_I2C::PCD_WriteRegister(MFRC522::PCD_Register reg, byte value) {
  hal_i2c_lock_bus(_bus);
  hal_i2c_begin_transmission_bus(_bus, _chipAddress);
  (void)hal_i2c_write_bus(_bus, (byte)reg);
  (void)hal_i2c_write_bus(_bus, value);
  (void)hal_i2c_end_transmission_bus(_bus);
  hal_i2c_unlock_bus(_bus);
}

void MFRC522_I2C::PCD_WriteRegister(MFRC522::PCD_Register reg, byte count,
                                    byte *values) {
  hal_i2c_lock_bus(_bus);
  hal_i2c_begin_transmission_bus(_bus, _chipAddress);
  (void)hal_i2c_write_bus(_bus, (byte)reg);
  for (byte index = 0; index < count; index++) {
    (void)hal_i2c_write_bus(_bus, values[index]);
  }
  (void)hal_i2c_end_transmission_bus(_bus);
  hal_i2c_unlock_bus(_bus);
}

byte MFRC522_I2C::PCD_ReadRegister(MFRC522::PCD_Register reg) {
  byte value = 0;
  const byte address = (byte)reg;
  if (!hal_i2c_write_read_bus(_bus, _chipAddress, &address, 1, &value, 1)) {
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

  if (!hal_i2c_write_read_bus(_bus, _chipAddress, &address, 1, dst, count)) {
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
