# 22 - Czytniki RFID i NFC

Ten skonsolidowany projekt sprawdza drivery MFRC522 i PN532 na wspólnej
magistrali SPI. Każdy kontroler ma własne piny chip-select i reset oraz jest
inicjalizowany niezależnie, dlatego brak jednego czytnika nie wyłącza drugiej
ścieżki.

| Sygnał | Rodzina RP | STM32G474 |
| --- | --- | --- |
| SPI MISO / MOSI / SCK | GP16 / GP19 / GP18 | PA6 / PA7 / PA5 |
| MFRC522 CS / RST | GP17 / GP20 | PB6 / PB1 |
| PN532 CS / RST | GP21 / GP22 | PB2 / PB3 |

Na NUCLEO-G474RE wspólne sygnały SPI i główny CS MFRC522 znajdują się na pinach
13/15/11/17 CN10, odpowiadających D12/D11/D13/D10. PB1, PB2 i PB3 znajdują się
odpowiednio na pinach 24, 22 i 31 CN10; PB3 jest także D3. Skonfiguruj moduł
PN532 do trybu SPI. Oba czytniki używają logiki 3,3 V.
