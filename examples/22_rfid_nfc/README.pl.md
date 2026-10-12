<a id="22---czytniki-rfid-i-nfc"></a>

# 22 - Odczyt kart RFID i NFC

Przykład obsługuje czytniki MFRC522 i PN532 na wspólnej magistrali SPI.
Każdy czytnik ma własne piny `CS` i `RST` oraz jest inicjalizowany niezależnie.
Brak jednego z nich nie zatrzymuje obsługi drugiego.

Połączenia każdego montażu opisuje `device_tree.json`, a program bierze
magistralę SPI, piny i ustawienia SPI czytnika MFRC522 (4 MHz)
z wygenerowanego nagłówka. Sterownik PN532 sam ustawia zegar SPI na 1 MHz
i wysyła najpierw najmłodszy bit (LSB first); węzeł `pn532` podaje te same
ustawienia. Poniższa tabela powtarza te połączenia.

| Sygnał | Pico / Pico 2 | NUCLEO-G474RE | ESP32-S3-Zero |
| --- | --- | --- | --- |
| SPI MISO | GP16 | PA6, pin 13 CN10 / D12 | GPIO6 |
| SPI MOSI | GP19 | PA7, pin 15 CN10 / D11 | GPIO7 |
| SPI SCK | GP18 | PB3, pin 31 CN10 / D3 | GPIO5 |
| MFRC522 CS | GP17 | PB6, pin 17 CN10 / D10 | GPIO16 |
| MFRC522 RST | GP20 | PB1, pin 24 CN10 | GPIO17 |
| PN532 CS | GP21 | PB2, pin 22 CN10 | GPIO18 |
| PN532 RST | GP22 | PB10, pin 25 CN10 / D6 | GPIO15 |

Na NUCLEO-G474RE zegar SPI idzie przez PB3 zamiast D13 (PA5), bo PA5 steruje
diodą LD2, dlatego RST czytnika PN532 przeniesiono z PB3 na PB10.
Na ESP32-S3-Zero RST czytnika PN532 korzysta z GPIO15 zamiast GPIO19, bo
GPIO19 to linia USB D-.

Ustaw moduł PN532 w tryb SPI. Oba czytniki korzystają z sygnałów logicznych
3,3 V.
