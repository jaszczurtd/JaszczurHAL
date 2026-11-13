<a id="13---adc"></a>

# 13 - Pomiary napięcia: ADC i ADS1115

Przykład odczytuje dwa wejścia wewnętrznego, 12-bitowego ADC oraz cztery
kanały zewnętrznego przetwornika ADS1115 pod adresem I2C `0x48`.
Dla ADS1115 pokazuje odczyt surowej wartości i napięcia po przeliczeniu;
druga funkcja zwraca również status operacji.

Brak ADS1115 nie zatrzymuje pomiarów wewnętrznego ADC. Aplikacja ponawia
inicjalizację zewnętrznego przetwornika co pięć sekund.

Wariant `SCAN` włącza `HAL_ENABLE_ADC_SCAN` i próbkuje te same dwa wejścia
wewnętrzne, `adc0` i `adc1`, oraz wbudowany czujnik temperatury w sposób
ciągły, z taktowaniem sprzętowym na RP i STM32G474. Na RP próbkuje każdy pin
co 12 µs i zbiera jeden blok 400 ramek co 4,8 ms. Na STM32G474 czujnik
temperatury wymaga dłuższej konwersji; rzeczywisty okres ramki odczytaj
z komunikatu aplikacji. Raportuje ona średnią, minimum i maksimum z bloków
zebranych w ostatniej sekundzie oraz liczbę bloków pominiętych. ADS1115 nie
jest w tym wariancie używany.

Obecna konfiguracja `SCAN` nie uruchamia się na ESP32-S3: backend odrzuca
kanał temperatury oraz żądany okres konwersji 4000 ns statusem
`HAL_EUNSUPPORTED`. Aby ją dostosować, pomiń temperaturę, zmień liczbę pinów
i bufor oraz ustaw co najmniej 12000 ns na konwersję. Ograniczenia platform
opisuje [API skanu ADC](../../doc/api/pl/25_adc_scan.md).

Połączenia każdego montażu opisuje `device_tree.json`, a program bierze
wejścia ADC, magistralę I2C i adres ADS1115 z wygenerowanego nagłówka.
Poniższa tabela powtarza te połączenia.

| Sygnał | Pico / Pico 2 | NUCLEO-G474RE | ESP32-S3-Zero |
|---|---|---|---|
| Wejście wewnętrznego ADC `adc0` | GPIO26 | PA0, pin 28 CN7 / A0 | GPIO2 |
| Wejście wewnętrznego ADC `adc1` | GPIO27 | PA1, pin 30 CN7 / A1 | GPIO1 |
| ADS1115 SDA | GPIO4 | PB9, pin 5 CN10 / D14 | GPIO4 |
| ADS1115 SCL | GPIO5 | PB8, pin 3 CN10 / D15 | GPIO5 |

Na ESP32-S3-Zero `adc0` korzysta z GPIO2 zamiast GPIO0, czyli pinu BOOT bez
kanału ADC, a magistrala ADS1115 z GPIO4/GPIO5 zamiast GPIO25/GPIO24, których
ESP32-S3 nie ma.

ADS1115 ma ustawiony zakres ±6,144 V, któremu odpowiada 0,1875 mV/LSB.
To ustawienie przelicznika ADC, nie informacja o dopuszczalnym napięciu na
pinach. Przed podłączeniem sygnału sprawdź ograniczenia wejść i napięcie
zasilania użytego układu.
