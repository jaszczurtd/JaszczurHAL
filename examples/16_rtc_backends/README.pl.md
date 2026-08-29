# 16 - Backendy RTC

Projekt obejmuje przykłady PCF8563 i DS3231. Providery są budowane w jednym
obrazie i wybierane w runtime przez `hal_rtc_config_t::chip`. Brak zewnętrznego
sprzętu jest raportowany bez blokowania drugiego RTC.

Przykład obejmuje też handle daty/czasu i epoki, alarmy i CLKOUT urządzeń
zewnętrznych, timer odliczający PCF8563, czujnik temperatury DS3231 oraz
względne wybudzanie przez native RTC targetu. Na targetach RP używa I2C0 na
GP4/GP5, a na STM32G474 PB9/PB8.

Każdy zewnętrzny RTC jest odczytywany przed rozpoczęciem testu. Nowe urządzenie
z utraconą integralnością zegara lub nieczytelnym kalendarzem fabrycznym
otrzymuje deterministyczną wartość testową `2026-08-20 12:34:50`; istniejący
poprawny zegar pozostaje bez zmian. Ścieżka PB9/PB8 STM32G474 została fizycznie
sprawdzona przy 400 kHz z PCF8563 i DS3231.

Na STM32G474 przykład sprawdza dodatkowo wewnętrzny RTC MCU. Preferuje LSE i
przechodzi na LSI tylko wtedy, gdy domena backup nie ma wybranego źródła zegara.
Ustawia deterministyczną datę wyłącznie przy braku integralności zegara, po czym
raportuje zachowany czas i postęp sekundowy. Sekwencja zasilania budzi MCU z CPU
Sleep po dwóch sekundach, STOP0 po trzech i STOP1 po czterech. Po każdym
przejściu wypisuje sklasyfikowany powód wybudzenia i upływ czasu monotonicznego.
Opróżnianie serial jest włączone, więc każda linia diagnostyczna fizycznie
opuszcza USART2 przed zmianą drzewa zegarów przez STOP. Kalendarz wewnętrzny
obsługuje lata 2000..2099.

Na RP2040 i RP2350 ten sam wewnętrzny handle sprawdza timer AON Pico SDK i
raportuje `HAL_RTC_CLOCK_SOURCE_AON`. RP2040 używa kalendarzowego RTC, a RP2350
Powman. Przykład zachowuje działający zegar po warm reset i ustawia go tylko przy
braku integralności. Obecny backend Pico SDK sprawdza CPU Sleep; capabilities
deep sleep i power-down są zgłaszane jako niewspierane. Żądanie tylko RTC czeka
mimo innych aktywnych przerwań, takich jak ruch USB CDC, i kończy się dopiero po
ustawieniu alarmu AON. Boardy Pico nie mają podtrzymania bateryjnego, więc czas
AON nie przetrwa utraty zasilania.

Zdefiniuj `HAL_EXAMPLE_RTC_POWER_DOWN_TEST=1`, aby ręcznie sprawdzić Standby na
STM32G474. Ostatni krok celowo resetuje MCU po pięciu sekundach. Przy następnym
starcie przykład odczytuje i czyści zachowany rekord wybudzenia, zamiast ponownie
wchodzić w sekwencję zasilania.

## Build i wybór źródła

Uruchom poniższe polecenia z głównego katalogu JaszczurHAL. Metadane projektu
wybierają dokładnie jedno źródło aplikacji dla każdego builda:

| Wybór | Źródło aplikacji | Wspierane targety |
| --- | --- | --- |
| Projekt bazowy | `app.c` | Rodzina RP2040 i STM32G474 |
| Wariant `display-clock` | `display_clock_app.cpp` | STM32G474 |

Build bazowego przykładu STM32G474:

```bash
vscode/entry/jh-vscode build \
  --project examples/16_rtc_backends \
  --target stm32g474 \
  --board nucleo-g474re
```

Ustawia to `JH_PROJECT_SOURCES=app.c`. Obraz trafia do
`.build/examples/16_rtc_backends/firmware.elf`.

Build zegara z wyświetlaczem:

```bash
vscode/entry/jh-vscode build \
  --project examples/16_rtc_backends \
  --target stm32g474 \
  --board nucleo-g474re \
  --variant display-clock
```

Wariant zastępuje bazowy wybór źródeł przez
`JH_PROJECT_SOURCES=display_clock_app.cpp` i włącza funkcje wyświetlacza
ILI9341. Nie buduje `app.c`, więc dwie implementacje `app_start()` i
`app_task0()` nie kolidują. Obraz trafia do
`.build/examples/16_rtc_backends/variants/display-clock/firmware.elf`.

W obu przypadkach `jh-vscode` konfiguruje wspólny projekt firmware CMake. CMake
dodaje wybrane źródło aplikacji, kod startowy STM32, backend STM32G474 oraz
włączone drivery i utilities JaszczurHAL. Dostarczone przez HAL `main()` wywołuje
raz `app_start()`, a następnie stale wywołuje `app_task0()`.

Wygenerowane taski VS Code udostępniają te same ścieżki jako `Project: Build` i
`Project: Build variant: display-clock`. Przed uruchomieniem wariantu
wyświetlacza wybierz `stm32g474:nucleo-g474re`.

## Zegar STM32G474 z podtrzymaniem DS3231

Ręczny wariant `display-clock` używa połączeń ILI9341 z
`examples/07_display_media` oraz DS3231 na PB9/PB8. Wyświetla `HH:MM:SS` przez
`draw7SegString()` pośrodku panelu w orientacji poziomej. Buduj lub wgrywaj go z
`--variant display-clock`.

Build i wgranie przez ST-LINK/OpenOCD:

```bash
vscode/entry/jh-vscode upload \
  --project examples/16_rtc_backends \
  --target stm32g474 \
  --board nucleo-g474re \
  --variant display-clock \
  --port /dev/ttyACM0 \
  --allow-unverified-port
```

Wbudowana wartość początkowa jest stosowana tylko wtedy, gdy DS3231 nadal
zgłasza poprawną integralność zegara i zawiera starszą datę. Utrata integralności
nigdy nie jest automatycznie nadpisywana: wyświetlacz zmienia się na czerwone
`--:--:--`, pokazując nieudany test podtrzymania bateryjnego po przywróceniu
zasilania.
