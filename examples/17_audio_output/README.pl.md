<a id="17---wyjście-audio"></a>

# 17 - Generowanie dźwięku i regulacja głośności

Przykład generuje dźwięk na wyjściu PWM i steruje stereofonicznym regulatorem
wzmocnienia PGA2311 przez SPI. Wartość odczytana z ADC ustala częstotliwość
generowanego sygnału. PGA2311 jest uruchamiany bez wyciszenia, a następnie
aplikacja cyklicznie zmienia jego wzmocnienie. Nie demonstruje przełączania wyciszenia.

Wyjście PWM korzysta z DACless. Pole `use_dma` struktury `hal_dacless_config_t`
wybiera przekazywanie próbek przez DMA albo obsługę przez odpytywanie.
Jest to ustawienie konfiguracji w kodzie, a nie osobny wariant kompilacji.
Domyślnie `use_dma` ma wartość `true`.

Połączenia każdego montażu opisuje `device_tree.json`, a program bierze
magistralę SPI, piny, ustawienia SPI układu PGA2311 (1 MHz) i 12-bitową
rozdzielczość PWM z wygenerowanego nagłówka. Poniższa tabela powtarza te
połączenia.

| Sygnał | Pico / Pico 2 | NUCLEO-G474RE | ESP32-S3-Zero |
|---|---|---|---|
| SPI MISO | GP16 | PA6, pin 13 CN10 / D12 | GPIO13 |
| SPI MOSI | GP19 | PA7, pin 15 CN10 / D11 | GPIO11 |
| SPI SCK | GP18 | PB3, pin 31 CN10 / D3 | GPIO12 |
| PGA2311 CS | GP17 | PB6, pin 17 CN10 / D10 | GPIO14 |
| Wyjście audio PWM | GP6 | PB0, pin 34 CN7 / A3 | GPIO15 |
| Wejście ADC | GP26 | PA0, pin 28 CN7 / A0 | GPIO4 |

Na NUCLEO-G474RE zegar SPI idzie przez PB3 zamiast D13 (PA5), bo PA5 steruje
diodą LD2. Na ESP32-S3 wejściem ADC może być tylko pin przetwornika ADC1,
czyli GPIO1-GPIO10.
