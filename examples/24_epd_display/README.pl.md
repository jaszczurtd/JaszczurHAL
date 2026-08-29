# 24 - Wyświetlacz e-paper SSD1681

Ten przykład obsługuje monochromatyczny panel SSD1681 200 x 200 przez wspólny
backend EPD SPI/GPIO i publiczne API surowego zapisu `hal_display`.

| Sygnał | Rodzina RP | NUCLEO-G474RE | Złącze Nucleo |
| --- | --- | --- | --- |
| MISO | GP16 | PA6 | pin 13 CN10 / D12 |
| MOSI | GP19 | PA7 | pin 15 CN10 / D11 |
| SCK | GP18 | PA5 | pin 11 CN10 / D13 |
| CS | GP17 | PB6 | pin 17 CN10 / D10 |
| DC | GP20 | PC7 | pin 19 CN10 / D9 |
| RESET | GP21 | PA9 | pin 21 CN10 / D8 |
| BUSY | GP22 | PA8 | pin 23 CN10 / D7 |

Przykład celowo pozostawia profile przebiegów puste, więc kontroler używa
przebiegu OTP z domyślną temperaturą drivera 25 C. Produkty powinny dostarczać
profile LUT pełnego i częściowego odświeżania producenta panelu, jeśli wymaga
tego moduł wyświetlacza lub jego zakres temperatur.
