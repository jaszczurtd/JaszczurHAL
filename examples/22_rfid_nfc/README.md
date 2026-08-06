# 22 - RFID and NFC readers

This consolidated project exercises the MFRC522 and PN532 drivers on a shared
SPI bus. Each controller has its own chip-select and reset pin and is
initialized independently, so one reader can be absent without suppressing the
other path.

| Signal | RP family | STM32G474 |
| --- | --- | --- |
| SPI MISO / MOSI / SCK | GP16 / GP19 / GP18 | PA6 / PA7 / PA5 |
| MFRC522 CS / RST | GP17 / GP20 | PB0 / PB1 |
| PN532 CS / RST | GP21 / GP22 | PB2 / PB3 |

Configure the PN532 module for SPI mode. Both readers use 3.3 V logic.
