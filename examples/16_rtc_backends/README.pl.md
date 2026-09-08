<a id="16---implementacje-rtc"></a>

# 16 - Zegary RTC, wybudzanie i podtrzymanie czasu

Przykład odczytuje datę i godzinę z PCF8563, DS3231 oraz wewnętrznego zegara
mikrokontrolera. Pokazuje ustawianie alarmów i wyjścia CLKOUT, timer
odliczający PCF8563, pomiar temperatury DS3231 oraz wybudzanie po zadanym
czasie. Dla obsługujących to układów sprawdza też odczyt i zapis czasu
w postaci znacznika epoch.

Sterowniki są kompilowane razem. Układ wybiera pole `chip` struktury
`hal_rtc_config_t`, a brak jednego zewnętrznego RTC nie zatrzymuje obsługi
drugiego. I2C 0 używa GP4/GP5 na płytkach RP i PB9/PB8 na STM32G474.

Przed zmianą czasu aplikacja odczytuje stan zewnętrznego RTC. Zachowuje
poprawny czas. Wartość testową `2026-08-20 12:34:50` wpisuje tylko wtedy,
gdy układ zgłasza utratę poprawności czasu albo początkowej daty nie można
odczytać. Nie jest to synchronizacja z bieżącą datą. Połączenie PB9/PB8
sprawdzono wcześniej przy 400 kHz z PCF8563 i DS3231.

## Wewnętrzny RTC i uśpienie

**STM32G474.** Aplikacja odczytuje wewnętrzny RTC i pokazuje upływ kolejnych
sekund. Jeśli domena podtrzymania ma już wybrane źródło zegara, zachowuje je.
W przeciwnym razie preferuje LSE, a gdy jest niedostępne, używa LSI.
Datę testową ustawia tylko wtedy, gdy zegar nie zgłasza jeszcze poprawnego
czasu. Kalendarz obsługuje lata 2000-2099.

Test uśpienia obejmuje wybudzenie z CPU Sleep po dwóch sekundach,
STOP0 po trzech i STOP1 po czterech. Po każdej próbie aplikacja podaje
przyczynę wybudzenia oraz czas zmierzony zegarem monotonicznym.
Przed wejściem w STOP czeka na opróżnienie bufora USART2, aby komunikat
został wysłany przed zmianą konfiguracji zegarów.

**RP2040 i RP2350.** Aplikacja korzysta z timera AON Pico SDK, zgłaszanego
jako `HAL_RTC_CLOCK_SOURCE_AON`. Na RP2040 jest on oparty na kalendarzowym
RTC, a na RP2350 - na Powman. Działający zegar jest zachowywany po miękkim
resecie; wartość początkowa jest ustawiana tylko przy braku poprawnego czasu.

Implementacja Pico SDK użyta w tym przykładzie obsługuje CPU Sleep.
Głębokie uśpienie i wyłączenie zasilania są zgłaszane jako nieobsługiwane.
Przy żądaniu wybudzenia wyłącznie przez RTC inne przerwania, np. związane
z USB CDC, nie kończą oczekiwania: aplikacja czeka na alarm AON.
Płytki Pico nie mają podtrzymania bateryjnego zegara, więc nie należy
oczekiwać zachowania czasu po odłączeniu zasilania.

**Ręczny test Standby na STM32G474.** Ustaw
`HAL_EXAMPLE_RTC_POWER_DOWN_TEST=1`. Końcowa próba wybudza układ resetem
po pięciu sekundach. Przy następnym uruchomieniu aplikacja odczytuje i czyści
zachowaną informację o wybudzeniu, zamiast powtarzać całą sekwencję uśpienia.

## Kompilacja i wybór źródła

Uruchamiaj poniższe polecenia z głównego katalogu repozytorium. Każda
konfiguracja wybiera dokładnie jeden plik aplikacji:

| Wybór | Plik aplikacji | Platformy |
|---|---|---|
| Projekt podstawowy | `app.c` | RP2040, RP2350 ARM, RP2350 RISC-V, STM32G474 |
| Wariant `display-clock` | `display_clock_app.c` | STM32G474 |

Przykład podstawowy dla STM32G474:

```bash
vscode/entry/jh-vscode build \
  --project examples/16_rtc_backends \
  --target stm32g474 \
  --board nucleo-g474re
```

Kompilacja używa `JH_PROJECT_SOURCES=app.c`, a wynik zapisuje jako
`.build/examples/16_rtc_backends/firmware.elf`.

Zegar z wyświetlaczem:

```bash
vscode/entry/jh-vscode build \
  --project examples/16_rtc_backends \
  --target stm32g474 \
  --board nucleo-g474re \
  --variant display-clock
```

Wariant ustawia `JH_PROJECT_SOURCES=display_clock_app.c` i włącza obsługę
ILI9341. Nie dołącza `app.c`, więc definicje `app_start()` i `app_task0()`
nie kolidują. Wynik znajduje się w
`.build/examples/16_rtc_backends/variants/display-clock/firmware.elf`.

W obu przypadkach `jh-vscode` korzysta ze wspólnego projektu CMake.
Do wybranego pliku aplikacji dołączane są kod startowy STM32, obsługa
STM32G474 oraz włączone sterowniki i funkcje JaszczurHAL.
Dostarczane przez HAL `main()` wywołuje `app_start()` raz,
a następnie powtarza `app_task0()`.

W VS Code odpowiadają temu zadania `Project: Build` oraz
`Project: Build variant: display-clock`. Przed uruchomieniem wariantu
z wyświetlaczem wybierz `stm32g474:nucleo-g474re`.

## Zegar STM32G474 podtrzymywany przez DS3231

Wariant `display-clock` wyświetla czas `HH:MM:SS` z DS3231 na środku ekranu
ILI9341 w orientacji poziomej. Cyfry rysuje `draw7SegString()`.
DS3231 podłącz do PB9/PB8, a ekran zgodnie z opisem
`examples/07_display_media`. Przy kompilacji i wgrywaniu dodaj
`--variant display-clock`.

Kompilacja i wgranie przez ST-LINK/OpenOCD:

```bash
vscode/entry/jh-vscode upload \
  --project examples/16_rtc_backends \
  --target stm32g474 \
  --board nucleo-g474re \
  --variant display-clock \
  --port /dev/ttyACM0 \
  --allow-unverified-port
```

W przeciwieństwie do przykładu podstawowego ten wariant nie naprawia
niepoprawnego czasu automatycznie. Wpisuje wbudowaną datę początkową tylko
wtedy, gdy DS3231 nadal zgłasza poprawny czas, ale zawiera starszą datę.
Przy utracie poprawności czasu pokazuje czerwone `--:--:--`. Dzięki temu
ponowne uruchomienie nie ukrywa nieudanego testu podtrzymania bateryjnego.
