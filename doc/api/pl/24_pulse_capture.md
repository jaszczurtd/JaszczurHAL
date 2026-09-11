# Sprzętowy pomiar okresów

Włącz `HAL_ENABLE_PULSE_CAPTURE` i dołącz `<hal/analog/hal_pulse_capture.h>`,
aby mierzyć okresowy sygnał cyfrowy. `hal_pulse_capture_read()` zwraca liczbę
taktów sprzętowego zegara dla 32 pełnych okresów. Pierwsze przechwycenie ustala
początek pomiaru i nie jest liczone jako okres. Faza wejścia oraz opóźnienie
obsługi przez CPU nie wchodzą do wzoru na częstotliwość.

```c
const hal_pulse_capture_config_t config = {0, true, 10000};
hal_status_t status = hal_pulse_capture_init(&config);
/* In the application loop, drain until HAL_EAGAIN. */
hal_pulse_capture_sample_t sample;
while (status == HAL_OK && hal_pulse_capture_read(&sample) == HAL_OK) {
    uint32_t hz = (uint32_t)(((uint64_t)sample.periods * sample.clock_hz +
                              sample.ticks / 2) / sample.ticks);
    /* Use hz and sample.measured_us; handle read errors in real applications. */
    (void)hz;
}
```

Wariant `capture` przykładu [01_core_runtime](../../../examples/01_core_runtime/)
pokazuje obsługę statusów i regularny odbiór danych. Korzysta z GPIO0 (PA0 na STM32).

## Uruchomienie i ważność danych

Dostępne jest jedno aktywne wejście. Wywołujący rezerwuje pin wyłącznie dla
pomiaru. Init/deinit wykonuj na tym samym rdzeniu, bez równoległych wywołań
API. Odczyty są chronione mutexem i nie mogą być wywoływane z ISR. Ponowne
init zwraca `HAL_EBUSY`; deinit można powtarzać, także po błędzie init.
Jeśli zwalnianie zasobów zawiedzie, ponów deinit. Sterownik kopiuje konfigurację.

Opróżniaj strumień co najwyżej co 5 ms. Implementacje sprzętowe zgłaszają
`HAL_EOVERFLOW` po przerwie obsługi 10 ms, zamiast zgadywać liczbę zawinięć
licznika lub bufora. Częstotliwość wejścia nie może przekroczyć 100 kHz, a oba
poziomy muszą trwać przynajmniej 1 us. Dobierz `timeout_us` (1000..100000 us)
tak, by obejmował 32 okresy i opóźnienie obsługi. `clock_hz` podaje rzeczywisty
zegar pomiaru; tolerancja oscylatora nadal ogranicza dokładność bezwzględną.

`measured_us` jest konserwatywnie wyznaczonym czasem zakończenia pomiaru w
jednostkach `hal_micros()`, a nie chwilą odczytu przez aplikację. Wiek licz przez
odejmowanie bez znaku, co zachowuje poprawność przy zawinięciu. `sequence`
zaczyna się od 1; deinit/init rozpoczyna nową numerację. `HAL_EAGAIN` oznacza
brak pełnego bloku, a `HAL_ETIMEOUT` - stare dane lub przerwę sygnału. Timeout
odrzuca niepełny blok. `HAL_EOVERFLOW` i błędy sprzętowe pozostają aktywne do
deinit/init. Błąd nie zmienia struktury wyjściowej. Po każdym błędzie capture
aplikacja musi unieważnić własne częściowo złożone okno.

## Implementacje

| Platforma | Pomiar i zasoby | Ograniczenia |
| --- | --- | --- |
| RP2040 / RP2350 ARM / RP2350 RISC-V | Dynamicznie przydzielona maszyna PIO i siedem instrukcji; dwa kanały DMA; pierścień 1024 słów. PIO zapisuje czas każdego zbocza, a CPU pobiera co 32. znacznik. | GPIO0..29; zegar pomiaru to system clock / 8, co najmniej 2 MHz. Nie zmieniaj zegara podczas pomiaru. Utrata danych RX FIFO i błędy DMA/bufora unieważniają pomiar. |
| STM32G474 | TIM5_CH1 na PA0/AF2, co ósme wybrane zbocze, DMA2 kanał 8 / DMAMUX kanał 15, pierścień 256 słów. Cztery odstępy składają się na próbkę. | Rezerwuje cały TIM5 i kanał DMA. Zajęty timer/DMA lub pin w alternate function powoduje `HAL_EBUSY`. Zgłasza nadpisanie capture i błąd transferu DMA. |
| ESP32-S3 | Timer MCPWM i dwa kanały capture: wejście z preskalerem 32 oraz kanał wyzwalany programowo do określenia wieku próbki. Krótki ISR zapisuje znaczniki sprzętowe do pierścienia RAM o 128 słowach. | Wymaga `CONFIG_MCPWM_ISR_CACHE_SAFE=y`; build HAL dla ESP-IDF włącza tę opcję. ISR i kolejka znajdują się w pamięci wewnętrznej. |
| Mock | Wstrzykiwanie znaczników 16 MHz przez `hal_mock_pulse_capture_edge()`; to samo składanie okresów i obsługa timeoutów. | Deterministyczne testy hostowe, bez symulacji opóźnień peryferiów i IRQ. |

ESP32-S3 odwraca wejście przed preskalerem, gdy wybrano zbocza opadające.
Capture korzysta następnie ze zbocza narastającego podzielonego sygnału.
Początkowa faza jest nieokreślona; kolejne przechwycenia dzielą 32 okresy
sygnału wejściowego. ISR musi odebrać zdarzenie przed następnym: w czasie
krótszym niż 320 us przy 100 kHz, około 914 us przy 35 kHz. Peryferium ma jeden
rejestr przechwycenia, więc spóźniony ISR może zgubić zdarzenie bez osobnej
sygnalizacji przepełnienia. Przepełnienie kolejki RAM jest raportowane, ale nie
wykrywa każdego takiego ubytku. Zapewnij odpowiedni zapas czasu obsługi IRQ.
Testy sprzętowe RP2350, STM32 i ESP32-S3 oczekują na wykonanie.

Program RP liczy takty podczas obu poziomów wejścia. Odstępy między zgodnymi
zboczami mają kwantyzację ośmiu taktów. DMA działa podczas wykonywania kodu
aplikacji. Czas próbki jest powiązany z timerem systemowym przy starcie PIO,
z niewielkim konserwatywnym przesunięciem; nie służy do synchronizacji fazy
między urządzeniami.
