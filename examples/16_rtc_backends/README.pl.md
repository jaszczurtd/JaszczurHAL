<a id="16---implementacje-rtc"></a>

# 16 - Zegary RTC, wybudzanie i podtrzymanie czasu

Przykład odczytuje datę i godzinę z PCF8563, DS3231 oraz wewnętrznego zegara
mikrokontrolera. Pokazuje ustawianie alarmów i wyjścia CLKOUT, timer
odliczający PCF8563, pomiar temperatury DS3231 oraz wybudzanie po zadanym
czasie. Dla obsługujących to układów sprawdza też odczyt i zapis czasu
w postaci znacznika epoch.

Sterowniki są kompilowane razem. Układ wybiera pole `chip` struktury
`hal_rtc_config_t`, a brak jednego zewnętrznego RTC nie zatrzymuje obsługi
drugiego. Połączenia każdego montażu opisuje `device_tree.json`, a program
bierze magistralę I2C, piny, częstotliwość zegara i adresy układów RTC
z wygenerowanego nagłówka. Poniższa tabela powtarza te połączenia. Magistrala
pracuje z częstotliwością 400 kHz, PCF8563 odpowiada pod adresem `0x51`,
a DS3231 pod `0x68`.

| Sygnał | Pico / Pico 2 | NUCLEO-G474RE |
|---|---|---|
| I2C SDA | GP4 | PB9, pin 5 CN10 / D14 |
| I2C SCL | GP5 | PB8, pin 3 CN10 / D15 |

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
konfiguracja kompiluje oba pliki aplikacji, ale każdy z nich jest objęty
warunkiem na `EXAMPLE_RTC_DISPLAY_CLOCK`, które definiuje tylko wariant
`DISPLAY_CLOCK`. Dzięki temu `app_start()` i `app_task0()` dostarcza
dokładnie jeden z nich:

| Wybór | Plik aplikacji | Montaże |
|---|---|---|
| Projekt podstawowy | `app.c` | `pico`, `pico2-arm`, `pico2-riscv`, `nucleo` |
| Wariant `DISPLAY_CLOCK` | `display_clock_app.c` | `nucleo` |

Przykład podstawowy dla STM32G474:

```bash
vscode/entry/jh-vscode build \
  --project examples/16_rtc_backends \
  --assembly nucleo
```

Kompilacja podstawowa uruchamia `app.c`, a wynik zapisuje jako
`.build/examples/16_rtc_backends/stm32g474/nucleo/firmware.elf`.

Zegar z wyświetlaczem:

```bash
vscode/entry/jh-vscode build \
  --project examples/16_rtc_backends \
  --assembly nucleo \
  --variant DISPLAY_CLOCK
```

Wariant definiuje `EXAMPLE_RTC_DISPLAY_CLOCK=1`. Wybiera to
`display_clock_app.c` zamiast `app.c`, a `hal_project_config.h` włącza wtedy
obsługę ILI9341 i zatrzymuje przez `#error` kompilację dla platform innych niż
STM32G474. Wariant kompiluje się jako moduł `16_rtc_backends_DISPLAY_CLOCK`,
a wynik znajduje się w
`.build/examples/16_rtc_backends/stm32g474/nucleo/variants/DISPLAY_CLOCK/firmware.elf`.

W obu przypadkach `jh-vscode` korzysta ze wspólnego projektu CMake.
Do plików aplikacji dołączane są kod startowy STM32, obsługa
STM32G474 oraz włączone sterowniki i funkcje JaszczurHAL.
Dostarczane przez HAL `main()` wywołuje `app_start()` raz,
a następnie powtarza `app_task0()`.

W VS Code odpowiadają temu zadania `Project: Build` oraz
`Project: Build variant: DISPLAY_CLOCK`. Przed uruchomieniem wariantu
z wyświetlaczem wybierz montaż `nucleo` zadaniem `Project: Select board`.

## Zegar STM32G474 podtrzymywany przez DS3231

Wariant `DISPLAY_CLOCK` wyświetla czas `HH:MM:SS` z DS3231 na środku ekranu
ILI9341 w orientacji poziomej. Cyfry rysuje `draw7SegString()`.
DS3231 podłącz do PB9/PB8 tak jak w tabeli powyżej. Podłączenie ekranu
opisuje węzeł `display` montażu `nucleo`; zegar SPI ma częstotliwość 24 MHz:

| Sygnał ILI9341 | NUCLEO-G474RE |
|---|---|
| SCK | PB3, pin 31 CN10 / D3 |
| MOSI | PA7, pin 15 CN10 / D11 |
| MISO | PA6, pin 13 CN10 / D12 |
| CS | PB6, pin 17 CN10 / D10 |
| DC | PC7, pin 19 CN10 / D9 |
| RESET | PA9, pin 21 CN10 / D8 |

Zegar SPI idzie przez PB3 zamiast D13 (PA5), bo PA5 steruje diodą LD2.
Przy kompilacji i wgrywaniu dodaj `--variant DISPLAY_CLOCK`.

Kompilacja i wgranie przez ST-LINK/OpenOCD:

```bash
vscode/entry/jh-vscode upload \
  --project examples/16_rtc_backends \
  --assembly nucleo \
  --variant DISPLAY_CLOCK \
  --port /dev/ttyACM0 \
  --allow-unverified-port
```

W przeciwieństwie do przykładu podstawowego ten wariant nie naprawia
niepoprawnego czasu automatycznie. Wpisuje wbudowaną datę początkową tylko
wtedy, gdy DS3231 nadal zgłasza poprawny czas, ale zawiera starszą datę.
Przy utracie poprawności czasu pokazuje czerwone `--:--:--`. Dzięki temu
ponowne uruchomienie nie ukrywa nieudanego testu podtrzymania bateryjnego.
