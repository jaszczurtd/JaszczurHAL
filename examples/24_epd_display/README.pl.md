<a id="24---wyświetlacz-e-paper-ssd1681"></a>

# 24 - Obraz na wyświetlaczu e-paper SSD1681

Przykład wyświetla monochromatyczny obraz na panelu SSD1681 o rozdzielczości
200×200 pikseli. Przesyła dane obrazu przez API `hal_display`; sterownik
korzysta z SPI oraz linii GPIO do sterowania panelem.

Połączenia każdego montażu opisuje `device_tree.json`, a program bierze
magistralę SPI, piny, zegar SPI (4 MHz) i polaryzację sygnału BUSY (aktywny
stan wysoki) z wygenerowanego nagłówka. Poniższa tabela powtarza te
połączenia.

| Sygnał | Pico / Pico 2 | NUCLEO-G474RE | Złącze Nucleo | ESP32-S3-Zero |
| --- | --- | --- | --- | --- |
| MISO | GP16 | PA6 | pin 13 CN10 / D12 | GPIO6 |
| MOSI | GP19 | PA7 | pin 15 CN10 / D11 | GPIO7 |
| SCK | GP18 | PB3 | pin 31 CN10 / D3 | GPIO5 |
| CS | GP17 | PB6 | pin 17 CN10 / D10 | GPIO4 |
| DC | GP20 | PC7 | pin 19 CN10 / D9 | GPIO3 |
| RESET | GP21 | PA9 | pin 21 CN10 / D8 | GPIO2 |
| BUSY | GP22 | PA8 | pin 23 CN10 / D7 | GPIO1 |

Na NUCLEO-G474RE zegar SPI idzie przez PB3 zamiast D13 (PA5), bo PA5 steruje
diodą LD2.

Przykład nie dostarcza własnych tablic LUT określających sposób odświeżania.
Kontroler korzysta z przebiegu zapisanego w pamięci OTP, a sterownik przyjmuje
temperaturę 25°C. Nie jest to pomiar rzeczywistej temperatury panelu.
Gdy wymaga tego moduł lub zakres temperatur pracy, dostarcz tablice LUT
pełnego i częściowego odświeżania zgodne z zaleceniami producenta.
