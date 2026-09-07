<a id="22---czytniki-rfid-i-nfc"></a>

# 22 - Odczyt kart RFID i NFC

Przykład obsługuje czytniki MFRC522 i PN532 na wspólnej magistrali SPI.
Każdy czytnik ma własne piny `CS` i `RST` oraz jest inicjalizowany niezależnie.
Brak jednego z nich nie zatrzymuje obsługi drugiego.

| Sygnał | Rodzina RP | STM32G474 |
| --- | --- | --- |
| SPI MISO / MOSI / SCK | GP16 / GP19 / GP18 | PA6 / PA7 / PA5 |
| MFRC522 CS / RST | GP17 / GP20 | PB6 / PB1 |
| PN532 CS / RST | GP21 / GP22 | PB2 / PB3 |

Na NUCLEO-G474RE sygnały SPI MISO/MOSI/SCK oraz `CS` czytnika MFRC522 są
wyprowadzone odpowiednio na piny 13/15/11/17 złącza CN10, czyli
D12/D11/D13/D10. PB1, PB2 i PB3 znajdują się na pinach 24, 22 i 31 złącza
CN10; PB3 jest również dostępny jako D3.

Ustaw moduł PN532 w tryb SPI. Oba czytniki korzystają z sygnałów logicznych
3,3 V.
