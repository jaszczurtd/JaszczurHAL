<a id="22---rfid-and-nfc-readers"></a>

# 22 - Reading RFID and NFC cards

This example uses MFRC522 and PN532 readers on a shared SPI bus. Each reader
has its own `CS` and `RST` pins and is initialized independently. An absent
reader does not stop the other one.

`device_tree.json` describes the wiring of every assembly, and the program
takes its SPI bus, pins and MFRC522 SPI settings (4 MHz) from the generated
header. The PN532 driver sets its own SPI clock of 1 MHz, LSB first, and the
`pn532` node states the same settings. The table below repeats the wiring.

| Signal | Pico / Pico 2 | NUCLEO-G474RE | ESP32-S3-Zero |
| --- | --- | --- | --- |
| SPI MISO | GP16 | PA6, CN10 pin 13 / D12 | GPIO6 |
| SPI MOSI | GP19 | PA7, CN10 pin 15 / D11 | GPIO7 |
| SPI SCK | GP18 | PB3, CN10 pin 31 / D3 | GPIO5 |
| MFRC522 CS | GP17 | PB6, CN10 pin 17 / D10 | GPIO16 |
| MFRC522 RST | GP20 | PB1, CN10 pin 24 | GPIO17 |
| PN532 CS | GP21 | PB2, CN10 pin 22 | GPIO18 |
| PN532 RST | GP22 | PB10, CN10 pin 25 / D6 | GPIO15 |

On NUCLEO-G474RE the SPI clock uses PB3 rather than D13 (PA5), because PA5
drives the LD2 LED, so PN532 RST moved from PB3 to PB10. On ESP32-S3-Zero
PN532 RST uses GPIO15 rather than GPIO19, because GPIO19 is the USB D- line.

Set the PN532 module to SPI mode. Both readers use 3.3 V logic.
