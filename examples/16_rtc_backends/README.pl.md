# 16 - Implementacje RTC

Projekt obejmuje przykłady PCF8563 i DS3231. Obie implementacje są kompilowane
w jednym obrazie, a `hal_rtc_config_t::chip` wybiera właściwą w czasie
działania. Brak jednego układu nie blokuje obsługi drugiego.

Przykład obejmuje też uchwyty czasu kalendarzowego i czasu epoki, alarmy oraz
wyjścia CLKOUT urządzeń zewnętrznych, timer odliczający PCF8563, czujnik
temperatury DS3231 oraz wybudzanie po zadanym czasie za pomocą RTC właściwego
dla targetu. Na targetach RP używa I2C0 na GP4/GP5, a na STM32G474 PB9/PB8.

Każdy zewnętrzny RTC jest odczytywany przed rozpoczęciem testu. Nowe urządzenie
z utraconą integralnością zegara lub nieczytelnym kalendarzem fabrycznym
otrzymuje deterministyczną wartość testową `2026-08-20 12:34:50`; istniejący
poprawny zegar pozostaje bez zmian. Ścieżka PB9/PB8 STM32G474 została fizycznie
sprawdzona przy 400 kHz z PCF8563 i DS3231.

Na STM32G474 przykład sprawdza dodatkowo wewnętrzny RTC MCU. Preferuje LSE, a z
LSI korzysta tylko wtedy, gdy w domenie podtrzymywanej nie wybrano źródła
zegara. Ustawia deterministyczną datę wyłącznie przy braku integralności zegara,
po czym wyświetla zachowany czas i jego postęp co sekundę. Sekwencja trybów
zasilania budzi MCU kolejno z CPU Sleep po dwóch sekundach, ze STOP0 po trzech
i ze STOP1 po czterech. Po każdym przejściu wypisuje ustalony powód wybudzenia
oraz czas, który upłynął według zegara monotonicznego. Wymuszone opróżnianie
bufora portu szeregowego
zapewnia, że każda linia diagnostyczna opuści USART2, zanim tryb STOP zmieni
konfigurację zegarów. Kalendarz wewnętrzny obsługuje lata 2000..2099.

Na RP2040 i RP2350 ten sam wewnętrzny uchwyt sprawdza timer AON Pico SDK i
zwraca `HAL_RTC_CLOCK_SOURCE_AON`. RP2040 używa kalendarzowego RTC, a RP2350
Powman. Przykład zachowuje działający zegar po miękkim resecie i ustawia go
tylko przy braku integralności. Obecna implementacja oparta na Pico SDK sprawdza
uśpienie CPU; tryby głębokiego uśpienia i wyłączenia zasilania są zgłaszane jako
nieobsługiwane. Oczekiwanie wyłącznie na RTC nie kończy się wskutek innych
aktywnych przerwań, takich jak ruch USB CDC, lecz dopiero po zgłoszeniu alarmu
AON. Płytki Pico nie mają podtrzymania bateryjnego, więc czas AON nie przetrwa
utraty zasilania.

Zdefiniuj `HAL_EXAMPLE_RTC_POWER_DOWN_TEST=1`, aby ręcznie sprawdzić Standby na
STM32G474. Ostatni krok celowo resetuje MCU po pięciu sekundach. Przy następnym
starcie przykład odczytuje i czyści zachowany rekord wybudzenia, zamiast ponownie
uruchamiać sekwencję trybów zasilania.

## Kompilacja i wybór źródła

Uruchom poniższe polecenia z głównego katalogu JaszczurHAL. Metadane projektu
wybierają dokładnie jedno źródło aplikacji dla każdej kompilacji:

| Wybór | Źródło aplikacji | Obsługiwane targety |
| --- | --- | --- |
| Projekt bazowy | `app.c` | Rodzina RP2040 i STM32G474 |
| Wariant `display-clock` | `display_clock_app.cpp` | STM32G474 |

Kompilacja bazowego przykładu STM32G474:

```bash
vscode/entry/jh-vscode build \
  --project examples/16_rtc_backends \
  --target stm32g474 \
  --board nucleo-g474re
```

Ustawia to `JH_PROJECT_SOURCES=app.c`. Obraz trafia do
`.build/examples/16_rtc_backends/firmware.elf`.

Kompilacja zegara z wyświetlaczem:

```bash
vscode/entry/jh-vscode build \
  --project examples/16_rtc_backends \
  --target stm32g474 \
  --board nucleo-g474re \
  --variant display-clock
```

W wariancie zamiast źródła bazowego używane jest
`JH_PROJECT_SOURCES=display_clock_app.cpp`; włączane są także funkcje
wyświetlacza ILI9341. Plik `app.c` nie jest kompilowany, więc dwie implementacje
`app_start()` i `app_task0()` nie kolidują. Obraz trafia do
`.build/examples/16_rtc_backends/variants/display-clock/firmware.elf`.

W obu przypadkach `jh-vscode` konfiguruje wspólny projekt firmware CMake. CMake
dodaje wybrane źródło aplikacji, kod startowy STM32, implementację STM32G474 oraz
włączone sterowniki i narzędzia JaszczurHAL. Funkcja `main()` dostarczana przez
HAL wywołuje raz `app_start()`, a następnie cyklicznie `app_task0()`.

Te same konfiguracje można zbudować za pomocą wygenerowanych zadań VS Code
`Project: Build` i `Project: Build variant: display-clock`. Przed uruchomieniem
wariantu wyświetlacza wybierz `stm32g474:nucleo-g474re`.

## Zegar STM32G474 podtrzymywany przez DS3231

Ręczny wariant `display-clock` używa połączeń ILI9341 z
`examples/07_display_media` oraz DS3231 na PB9/PB8. Wyświetla `HH:MM:SS` przez
`draw7SegString()` pośrodku panelu w orientacji poziomej. Zbuduj lub wgraj go z
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

Wbudowana wartość początkowa jest stosowana tylko wtedy, gdy DS3231 nadal
zgłasza poprawną integralność zegara i zawiera starszą datę. Utrata integralności
nigdy nie jest automatycznie nadpisywana: wyświetlacz pokazuje wtedy na czerwono
`--:--:--`, dzięki czemu po przywróceniu zasilania widać nieudany test
podtrzymania bateryjnego.
