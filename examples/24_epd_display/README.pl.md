<a id="24---wyświetlacz-e-paper-ssd1681"></a>

# 24 - Obraz na wyświetlaczu e-paper SSD1681

Przykład wyświetla monochromatyczny obraz na panelu SSD1681 o rozdzielczości
200×200 pikseli. Przesyła dane obrazu przez API `hal_display`; sterownik
korzysta z SPI oraz linii GPIO do sterowania panelem.

| Sygnał | Rodzina RP | NUCLEO-G474RE | Złącze Nucleo |
| --- | --- | --- | --- |
| MISO | GP16 | PA6 | pin 13 CN10 / D12 |
| MOSI | GP19 | PA7 | pin 15 CN10 / D11 |
| SCK | GP18 | PA5 | pin 11 CN10 / D13 |
| CS | GP17 | PB6 | pin 17 CN10 / D10 |
| DC | GP20 | PC7 | pin 19 CN10 / D9 |
| RESET | GP21 | PA9 | pin 21 CN10 / D8 |
| BUSY | GP22 | PA8 | pin 23 CN10 / D7 |

Przykład nie dostarcza własnych tablic LUT określających sposób odświeżania.
Kontroler korzysta z przebiegu zapisanego w pamięci OTP, a sterownik przyjmuje
temperaturę 25°C. Nie jest to pomiar rzeczywistej temperatury panelu.
Gdy wymaga tego moduł lub zakres temperatur pracy, dostarcz tablice LUT
pełnego i częściowego odświeżania zgodne z zaleceniami producenta.
