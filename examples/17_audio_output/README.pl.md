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

| Sygnał | Rodzina RP | NUCLEO-G474RE |
|---|---|---|
| SPI MISO / MOSI / SCK | GP16 / GP19 / GP18 | PA6 / PA7 / PA5, piny 13 / 15 / 11 CN10 (D12 / D11 / D13) |
| PGA2311 CS | GP17 | PB6, pin 17 CN10 (D10) |

Na NUCLEO-G474RE wyjście audio PWM jest na PB0 (pin 34 CN7 / A3), a wejście
ADC na PA0 (A0). Numery pinów PWM i ADC dla pozostałych platform są podane
w `app.c`.
