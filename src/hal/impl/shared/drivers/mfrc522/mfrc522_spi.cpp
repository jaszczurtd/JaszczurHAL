#include "mfrc522.h"

#include "hal/hal_system.h"

void MFRC522_SPI::PCD_WriteRegister(MFRC522::PCD_Register reg, byte value) {
  hal_spi_lock(_bus);
  hal_spi_begin_transaction(_bus, &_spiSettings);
  hal_gpio_write(_chipSelectPin, false);
  (void)hal_spi_transfer(_bus, (byte)(reg << 1));
  (void)hal_spi_transfer(_bus, value);
  hal_gpio_write(_chipSelectPin, true);
  hal_spi_end_transaction(_bus);
  hal_spi_unlock(_bus);
}

void MFRC522_SPI::PCD_WriteRegister(MFRC522::PCD_Register reg, byte count,
                                    byte *values) {
  hal_spi_lock(_bus);
  hal_spi_begin_transaction(_bus, &_spiSettings);
  hal_gpio_write(_chipSelectPin, false);
  (void)hal_spi_transfer(_bus, (byte)(reg << 1));
  for (byte index = 0; index < count; index++) {
    (void)hal_spi_transfer(_bus, values[index]);
  }
  hal_gpio_write(_chipSelectPin, true);
  hal_spi_end_transaction(_bus);
  hal_spi_unlock(_bus);
}

byte MFRC522_SPI::PCD_ReadRegister(MFRC522::PCD_Register reg) {
  hal_spi_lock(_bus);
  hal_spi_begin_transaction(_bus, &_spiSettings);
  hal_gpio_write(_chipSelectPin, false);
  (void)hal_spi_transfer(_bus, (byte)(0x80 | (reg << 1)));
  byte value = hal_spi_transfer(_bus, 0);
  hal_gpio_write(_chipSelectPin, true);
  hal_spi_end_transaction(_bus);
  hal_spi_unlock(_bus);
  return value;
}

void MFRC522_SPI::PCD_ReadRegister(MFRC522::PCD_Register reg, byte count,
                                   byte *values, byte rxAlign) {
  if (count == 0) {
    return;
  }

  const byte address = (byte)(0x80 | (reg << 1));
  byte index = 0;

  hal_spi_lock(_bus);
  hal_spi_begin_transaction(_bus, &_spiSettings);
  hal_gpio_write(_chipSelectPin, false);
  count--;
  (void)hal_spi_transfer(_bus, address);
  if (rxAlign) {
    byte mask = (byte)((0xFFu << rxAlign) & 0xFFu);
    byte value = hal_spi_transfer(_bus, address);
    values[0] = (byte)((values[0] & ~mask) | (value & mask));
    index++;
  }
  while (index < count) {
    values[index] = hal_spi_transfer(_bus, address);
    index++;
  }
  values[index] = hal_spi_transfer(_bus, 0);
  hal_gpio_write(_chipSelectPin, true);
  hal_spi_end_transaction(_bus);
  hal_spi_unlock(_bus);
}

bool MFRC522_SPI::PCD_Init() {
  hal_gpio_set_mode(_chipSelectPin, HAL_GPIO_OUTPUT_HIGH);
  hal_gpio_write(_chipSelectPin, true);

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
