# 22 - RFID and NFC readers

This consolidated project exercises the MFRC522 and PN532 drivers on a shared
SPI bus. Each controller has its own chip-select and reset pin and is
initialized independently, so one reader can be absent without suppressing the
other path.

| Signal | RP family | STM32G474 |
| --- | --- | --- |
| SPI MISO / MOSI / SCK | GP16 / GP19 / GP18 | PA6 / PA7 / PA5 |
| MFRC522 CS / RST | GP17 / GP20 | PB6 / PB1 |
| PN532 CS / RST | GP21 / GP22 | PB2 / PB3 |

On NUCLEO-G474RE, the common SPI signals and primary MFRC522 CS are grouped on
CN10 pins 13/15/11/17, equivalent to D12/D11/D13/D10. PB1, PB2 and PB3 are on
CN10 pins 24, 22 and 31 respectively; PB3 is also D3. Configure the PN532
module for SPI mode. Both readers use 3.3 V logic.
