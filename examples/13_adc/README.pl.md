<a id="13---adc"></a>

# 13 - Pomiary napięcia: ADC i ADS1115

Przykład odczytuje dwa wejścia wewnętrznego, 12-bitowego ADC oraz cztery
kanały zewnętrznego przetwornika ADS1115 pod adresem I2C `0x48`.
Dla ADS1115 pokazuje odczyt surowej wartości i napięcia po przeliczeniu;
druga funkcja zwraca również status operacji.

Brak ADS1115 nie zatrzymuje pomiarów wewnętrznego ADC. Aplikacja ponawia
inicjalizację zewnętrznego przetwornika co pięć sekund.

Wariant `scan` włącza `HAL_ENABLE_ADC_SCAN` i próbkuje oba wejścia wewnętrzne
oraz czujnik temperatury w sposób ciągły, z taktowaniem sprzętowym: każdy pin
co 12 µs, jeden blok 400 ramek co 4,8 ms. Raportuje średnią, minimum i maksimum
z bloków zebranych w ostatniej sekundzie oraz liczbę bloków pominiętych.
ADS1115 nie jest w tym wariancie używany.

| Sygnał | Rodzina RP | STM32G474 |
|---|---|---|
| Wejścia wewnętrznego ADC | GP26 / GP27 | PA0 / PA1 |
| ADS1115 SDA / SCL | GP4 / GP5 | PB9 / PB8 |

ADS1115 ma ustawiony zakres ±6,144 V, któremu odpowiada 0,1875 mV/LSB.
To ustawienie przelicznika ADC, nie informacja o dopuszczalnym napięciu na
pinach. Przed podłączeniem sygnału sprawdź ograniczenia wejść i napięcie
zasilania użytego układu.
