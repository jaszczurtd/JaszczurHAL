<a id="22---rfid-and-nfc-readers"></a>

# 22 - Reading RFID and NFC cards

This example uses MFRC522 and PN532 readers on a shared SPI bus. Each reader
has its own `CS` and `RST` pins and is initialized independently. An absent
reader does not stop the other one.

| Signal | RP family | STM32G474 |
| --- | --- | --- |
| SPI MISO / MOSI / SCK | GP16 / GP19 / GP18 | PA6 / PA7 / PA5 |
| MFRC522 CS / RST | GP17 / GP20 | PB6 / PB1 |
| PN532 CS / RST | GP21 / GP22 | PB2 / PB3 |

On NUCLEO-G474RE, SPI MISO/MOSI/SCK and the MFRC522 `CS` are available on
CN10 pins 13/15/11/17, equivalent to D12/D11/D13/D10. PB1, PB2, and PB3
are on CN10 pins 24, 22, and 31; PB3 is also available as D3.

Set the PN532 module to SPI mode. Both readers use 3.3 V logic.
