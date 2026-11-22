<a id="01---podstawowe-usługi-runtimeu"></a>

# 01 - Podstawowe funkcje systemowe

Przykład pokazuje miganie diodą, diagnostykę urządzenia, timery programowe
oraz regulację PID na symulowanym obiekcie. Jego kod działa z RAM dzięki
`HAL_RAM_CODE_SOURCES` w `hal_project_config.h`. Nie wymaga dodatkowego sprzętu:
korzysta z diody na płytce i konsoli diagnostycznej.

Callback timera sprzętowego zapisuje czas i liczbę ticków w kolejce zdarzeń.
`app_task0()` formatuje rekordy i opróżnia statyczny bufor TX z budżetem bajtów.
Zatrzymana konsola może stracić rekordy diagnostyczne, co pokazuje licznik
w logu; timer nadal działa bez czekania na transmisję.


Wariant `CAPTURE` mierzy okresowy sygnał, korzystając ze sprzętowych
znaczników czasu. Włącza `HAL_ENABLE_PULSE_CAPTURE` i wymaga zewnętrznego
źródła cyfrowego ze wspólną masą. Odbiór danych działa co 1 ms. Połączenia
każdego montażu opisuje `device_tree.json`, a wariant bierze pin wejścia
z wygenerowanego nagłówka. Poniższa tabela powtarza te połączenia.

| Sygnał | Pico / Pico 2 | NUCLEO-G474RE | ESP32-S3-Zero |
|---|---|---|---|
| Wejście pomiaru, wewnętrzne podciąganie | GPIO0 | PA0, pin 28 CN7 / A0 | GPIO0 |

Na ESP32-S3-Zero GPIO0 jest też przyciskiem BOOT, więc źródło nie może
trzymać go w stanie niskim podczas resetu.
