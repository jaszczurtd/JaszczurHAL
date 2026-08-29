# JaszczurHAL

*Dostępne również [po angielsku](README.md).*

Autor: Marcin 'Jaszczur' Kielesinski

JaszczurHAL to warstwa abstrakcji sprzętowej (HAL) i biblioteka narzędziowa dla projektów embedded opartych na RP2040/2350/STM32/ESP32.
Zacznij od [polskiego spisu dokumentacji](doc/table_of_contents.pl.md) albo
zobacz [przegląd funkcjonalności](doc/pl/features.md), aby poznać zwięzły spis
obsługiwanych modułów i funkcji.

## Po co to powstało?

Wiele projektów embedded zaczyna się jako szybko napisany kod i z czasem coraz
trudniej je rozwijać, zwłaszcza gdy dostęp do sprzętu jest ściśle powiązany
z logiką aplikacji albo gdy drivery są przywiązane do konkretnego targetu
sprzętowego.

JaszczurHAL wprowadza praktyczną granicę:

- warstwa aplikacji: przenośna logika,
- warstwa HAL: spójne, przenośne API oddzielające szczegóły sprzętowe
  od logiki aplikacji,
- warstwa mock: deterministyczne testowanie po stronie hosta,
- drivery wielokrotnego użytku, thread-safe i współdzielone przez wszystkie
  wspierane targety sprzętowe,
- opcjonalne moduły sterowane flagami buildu `HAL_ENABLE_*` (opt-in),
- opcjonalny stos łączności/bezpieczeństwa/pamięci masowej dla projektów
  firmware z połączeniem sieciowym,
- zestaw narzędzi dla typowych wzorców embedded (timery, PID, watchdog,
  funkcje pomocnicze),
- w pełni funkcjonalne wsparcie FreeRTOS (V11.3.0).

Kod aplikacji pozostaje przenośny pomiędzy wspieranymi targetami i runtime'ami.

Projekt jest już bardzo użyteczny, ale wciąż zostało tu i ówdzie kilka
obszarów w toku (WIP). Niestety, oprócz pracy nad projektem hobbystycznym,
trzeba też jakoś zarabiać na życie - i znaleźć czas na samo życie. :)

## Czy to jest gdzieś faktycznie używane?

Tak - w kilku moich bardziej wymagających projektach.

Najbardziej widocznym przykładem JaszczurHAL w praktyce jest projekt Fiesta: https://github.com/jaszczurtd/Fiesta

To mój prywatny projekt typu retrofit/automotive, złożony z kilku ściśle
zintegrowanych modułów. Moduł ECU jest chyba najbardziej wymagający: używa
JaszczurHAL w trybie dwóch rdzeni, do sterowania pompą wtryskową VP37,
komunikacji CAN z resztą systemu, diagnostyki OBD i innych funkcji
niskopoziomowych.

Są też mniejsze (ale nietrywialne) projekty, na przykład:

* https://github.com/jaszczurtd/doomConsole (port gry Doom z dźwiękiem i wyświetlaczem TFT)
* https://github.com/jaszczurtd/Ford-Mondeo-MK-DPF-Tracker (urządzenie śledzące cykle regeneracji DPF)
* https://github.com/jaszczurtd/lights-timer (zdalne sterowanie oświetleniem akwarium za pomocą aplikacji na Androida)

## Szybki start

Są dwa typowe punkty startowe:

- Aby poznać [API HAL](doc/pl/JaszczurHAL_API.md), wzorce przenośności
  i pokrycie backendów, zacznij od gotowych przykładów w repozytorium:
  [examples/README.md](examples/README.md).
- Aby utworzyć nowy, wybieralny pod kątem targetu projekt firmware do
  codziennej pracy w VS Code, użyj generatora projektów:

```bash
libraries/JaszczurHAL/vscode/tools/create-vscode-example.py \
  --output your-example-project-name
```

Wygenerowany projekt startuje na `rp2040/pico` (zmień to za pomocą
`--target`/`--board` albo zadania `Project: Select board`) i ma gotowe do
użycia zadania VS Code do build/upload/monitor. Opcje generatora,
pierwsze wgranie firmware na czystą płytkę oraz pełny opis zadań są
udokumentowane w [vscode/README.md](vscode/README.md).

## Przykłady

Drzewo `examples/` zawiera skonsolidowane aplikacje, które demonstrują
powiązane ze sobą moduły HAL działające razem. Każdy przykład to przenośny
`app.c`/`app.cpp` wraz z dopasowanym `hal_project_config.h`, zbudowany na
przenośnym interfejsie punktu wejścia: `app_start()`, `app_task0()` oraz
opcjonalnie `app_task1()` (`HAL_ENABLE_APP_TASK1`, mapowane na wykonanie
dwurdzeniowe na RP i na wywołania kooperacyjne na bare-metalowym STM32G474).
ESP-IDF mapuje `app_start()`, `app_task0()` oraz opcjonalnie `app_task1()` do
swojego już działającego schedulera FreeRTOS. ESP32-S3 domyślnie przypisuje
task0/task1 do rdzeni 0/1 i pozwala na jawne nadpisanie tego przypisania
(affinity).

Macierz buildów, wymagania, lista targetów obsługiwanych przez poszczególne
przykładów oraz zasada rozszerzania istniejącego projektu lub wariantu przed
utworzeniem kolejnego katalogu są opisane w [examples/README.md](examples/README.md).

## Wspierane targety i moduły (szybki przegląd)

RP2040 i RP2350 budują firmware bezpośrednio na oficjalnym Pico SDK.
STM32G474 jest wspierany przez bare-metalową implementację repozytorium
i przepływ linkowania. Target ESP32-S3 zbudowany jest na bazie ESP-IDF SDK.
FreeRTOS jest opcjonalny na RP i STM32G474, a wymagany przez runtime ESP-IDF.
Backend mock zapewnia deterministyczną walidację po
stronie hosta.

Zwięzły spis obsługiwanej funkcjonalności i modułów znajduje się w
[przeglądzie funkcjonalności](doc/pl/features.md).

## Wybór modułów (skrótowo)

JaszczurHAL używa modelu flag OPT-IN: domyślnie żaden moduł opcjonalny nie
jest kompilowany. Aby włączyć moduły potrzebne w projekcie, zdefiniuj flagi
`HAL_ENABLE_*` w lokalnym dla projektu pliku
`hal_project_config.h`:

```c
#pragma once
#define HAL_ENABLE_WIFI
#define HAL_ENABLE_TIME
#define HAL_ENABLE_GPS
```

Projekt używa własnych mechanizmów walidacji, aby sprawdzić, czy dana flaga
jest poprawna i wspierana oraz czy jej parametry są prawidłowe.

Pełną macierz flag, zasady propagacji zależności i opcje `HAL_ENABLE_*`
znajdziesz w:

- [JaszczurHAL_API.md](doc/pl/JaszczurHAL_API.md)
- [doc/api/pl/02_module_flags.md](doc/api/pl/02_module_flags.md)
- [doc/HAL_FLAGS.txt](doc/HAL_FLAGS.txt)

## Przykład wyboru targetu (multiplatformowo)

Niezależnie od flag modułów, JaszczurHAL wybiera dokładnie jeden backend
sprzętowy poprzez `src/hal/core/hal_target.h`. Zdefiniuj jedną
z poniższych flag w `hal_project_config.h` (lub przez `-D`):

```c
#define HAL_TARGET_RP2040        // RP2040, Cortex-M0+
#define HAL_TARGET_RP2350_ARM    // RP2350, Cortex-M33
#define HAL_TARGET_RP2350_RISCV  // RP2350, Hazard3 RISC-V
#define HAL_TARGET_STM32G474     // STM32G474
#define HAL_TARGET_ESP32_S3      // ESP32-S3, natywny ESP-IDF
#define HAL_TARGET_MOCK          // deterministyczny backend testowy dla hosta
```

Jeśli żadnej nie zdefiniujesz, target jest **wykrywany automatycznie** na
podstawie toolchainu. Pliki backendu kompilują się tylko dla wybranego
przez siebie targetu, więc nieużywane backendy nie kosztują ani bajtu kodu.

Oficjalne buildy wybierają stabilny target i identyfikator płytki poprzez
generowany rejestr płytek. Zobacz
[Profile targetów i płytek](doc/pl/boards_profiles_howto.md).
Zobacz też [FwProjectWorkflow.md](doc/pl/FwProjectWorkflow.md), aby poznać
pełny model targetu/płytki/konfiguracji.

W praktyce nie musisz znać wewnętrznego działania logiki wyboru targetu.
Wystarczy nacisnąć `Ctrl+Shift+Alt+1` w swoim projekcie i wybrać target z menu.

Oto [pełna lista](vscode/README.md#vs-code-keyboard-shortcuts) dostępnych
skrótów klawiszowych VS Code.

## Opcjonalny FreeRTOS (opt-in)

Wsparcie FreeRTOS wybierane jest jawną flagą buildu:

```c
#define HAL_ENABLE_FREERTOS
```

Aplikacje używają bezpośrednio standardowych, oryginalnych nagłówków i API
FreeRTOS na wszystkich wspieranych targetach.

JaszczurHAL ukrywa szczegóły startu specyficzne dla targetu, takie jak
uruchomienie schedulera i opcjonalne rozmieszczenie zadań aplikacji. Targety
RP używają przypiętego FreeRTOS-Kernel ze wsparciem SMP, natomiast STM32G474
używa tego samego kernela z portem Cortex-M4F. ESP32-S3 używa instancji
FreeRTOS dostarczonej przez przypięty ESP-IDF; jego deskryptor targetu dodaje
`HAL_ENABLE_FREERTOS` jako wymaganą cechę i wspiera opcjonalne drugie zadanie
aplikacji.

Szczegółowe informacje o przypinaniu kernela, portach i wariantach buildu
są dostępne w [lib_compilation.md](doc/pl/lib_compilation.md) i [doc/api/pl/04_multicore_drivers_migration.md](doc/api/pl/04_multicore_drivers_migration.md).

## Thread safety (przegląd)

Thread safety i obsługa wielordzeniowa są jednymi z podstawowych zasad
projektowych na wszystkich targetach. Ścieżki inicjalizacji i zakończenia
(`init` / `create` / `destroy` / `deinit`) są traktowane jako operacje
jednordzeniowe; blokady singletonów i poszczególnych magistral są tworzone atomowo przy
pierwszym użyciu poprzez defensywne, leniwe tworzenie muteksów. Backend mock
celuje w deterministyczne testy jednowątkowe, a opcjonalna flaga
`JH_ENABLE_FREERTOS_POSIX_TESTS` rozszerza ten zestaw o testy schedulera
FreeRTOS po stronie hosta.

Szczegółowe sygnatury, dokładne gwarancje, zachowanie modułów, uwagi
dotyczące backendów i pokrycie testami znajdziesz w [JaszczurHAL_API.md](doc/pl/JaszczurHAL_API.md).

## Build jako biblioteka statyczna (.a)

Kompletny przewodnik po buildzie JaszczurHAL jako linkowalnej biblioteki
statycznej (`libJaszczurHAL.a`), łącznie z buildem przykładowych aplikacji
oraz polityką core/entry: [lib_compilation.md](doc/pl/lib_compilation.md).
Zainstalowane pakiety RP i STM32G474 zawierają wygenerowane nagłówki cech
i płytek, rozwiązane metadane płytki oraz źródło referencyjne zgodności
linkowania wymagane przez projekt korzystający bezpośrednio z kompilatora. Build
i linkowanie zainstalowanego pakietu nie wymaga Pythona.

## Testy i bramki jakości

```bash
./runalltests.sh
```

Obejmuje testy jednostkowe hosta (z pokryciem FreeRTOS POSIX), Valgrind
memcheck, analizę statyczną, kontrole duplikatów i dokumentacji oraz macierze
buildów targetów/firmware. Stanowiska testowe sprzętu są udokumentowane
i wykonywane osobno. Pełna
architektura testów, wymagania, konfiguracja, zasady rozszerzania, procedury
stanowisk testowych i zarejestrowane wyniki znajdują się w
[Zależności buildu, testy i stanowiska testowe sprzętu](doc/api/pl/03_build_tests.md).
Szczegóły implementacji runnera i bramek jakości znajdziesz w
[Skryptach obsługi repozytorium JaszczurHAL](doc/api/pl/00_scripts.md).

## Bezpieczeństwo i SBOM

JaszczurHAL utrzymuje lekki rejestr łańcucha dostaw oprogramowania dla
dołączonych i przypiętych zależności:

- [SECURITY.md](SECURITY.md) - zgłaszanie podatności, triage i polityka
  utrzymania,
- [doc/pl/security_supply_chain.md](doc/pl/security_supply_chain.md) - generowanie
  SBOM, kontrole podatności i polityka `security-scan` w CI,
- [security/third_party.json](security/third_party.json) - utrzymywany ręcznie
  spis zależności zewnętrznych,
- [security/sbom.cdx.json](security/sbom.cdx.json) - generowany SBOM w
  formacie CycloneDX.

## Środowisko programistyczne VS Code

`vscode/` to wspierana warstwa integracji z VS Code dla projektów firmware
korzystających z JaszczurHAL. Projekty wywołują stabilny punkt wejścia:

```text
libraries/JaszczurHAL/vscode/entry/jh-vscode
libraries/JaszczurHAL/vscode/entry/jh-vscode.cmd
```

Punkt wejścia rozwiązuje konfigurację projektu, wybiera aktywny target/płytkę,
buduje firmware oparte na CMake za pośrednictwem dispatchera, wykonuje
wgrywanie po porcie szeregowym z weryfikacją tożsamości, obsługuje wgrywanie
RP2040 przez BOOTSEL/UF2, deleguje flashowanie STM32 do OpenOCD, deleguje
build/flash ESP32-S3 do produkcyjnego runnera ESP-IDF, uruchamia
trwałe monitory portu szeregowego oraz odświeża IntelliSense na podstawie bazy
buildu aktywnego toolchainu.

- Interfejs CLI, etykiety zadań, skróty klawiszowe i generator projektów:
  [vscode/README.md](vscode/README.md)
- Kompletny model projektu i
  [dodawanie plików źródłowych projektu](doc/pl/FwProjectWorkflow.md#dodawanie-plików-źródłowych-projektu):
  [FwProjectWorkflow.md](doc/pl/FwProjectWorkflow.md)
- Aktualizacje sieciowe dla natywnego RP i ESP32-S3, pierwsze wgranie i
  granice bezpieczeństwa:
  [OTAWorkflow.md](doc/pl/OTAWorkflow.md)
- [Pełna lista skrótów klawiszowych](vscode/README.md#vs-code-keyboard-shortcuts)

Gdy sam katalog główny repozytorium JaszczurHAL zostanie otwarty w VS Code,
śledzona konfiguracja `.vscode/` udostępnia osobny workflow dla
biblioteki statycznej. Istniejące globalne skróty budują, instalują,
czyszczą i odświeżają IntelliSense dla jednego profilu target/płytka
wybranego bezpośrednio ze współdzielonego rejestru płytek. Artefakty
pozostają poniżej `.build/vscode/library/`; szczegóły znajdziesz w
[przewodniku po buildu biblioteki](doc/pl/lib_compilation.md#workspace-repozytorium-i-vs-code).

## Debugowanie w VS Code

Generowane profile Cortex-Debug wspierają RP2040 i RP2350 Arm przez SWD za
pomocą Raspberry Pi Debug Probe albo Pico z firmware Debug Probe/Picoprobe.
Projekty STM32G474 używają wbudowanego ST-Link płytki NUCLEO-G474RE. Przepływ
Run and Debug w VS Code buduje i wgrywa ELF w wersji debug za pomocą
zarządzanego OpenOCD i GDB z obsługą Arm na Windows i Linuksie; zobacz
[konfigurację natywnego Windows](doc/pl/windows_setup.md) po szczegóły
okablowania i konfiguracji.

Zarówno Linux, jak i natywny Windows zapewniają workflow programowania
firmware w VS Code dla wydanych ścieżek targetów. Targety RP/STM zapewniają
udokumentowane funkcje build, upload, monitor, OTA i debugowania.
ESP32-S3 zapewnia build, upload przez port szeregowy, monitor,
IntelliSense i surowe OTA aplikacji, ale bez zarządzanego profilu debugowania;
jego ścieżka OTA nadal wymaga walidacji sprzętowej Fazy 3.5 opisanej powyżej.
Linux zapewnia pełną bramkę jakości repozytorium, w tym Valgrind, analizę
statyczną i integracje hosta dostępne tylko na POSIX. Zobacz
[konfigurację natywnego Windows](doc/pl/windows_setup.md) po szczegóły
konfiguracji, weryfikacji i jawne ograniczenia dotyczące wyłącznie Linuksa.

## Zarządzane zależności

Śledzone wersje Pico SDK, ESP-IDF, picotool, PMD CPD, toolchainu RISC-V dla
RP2350, FreeRTOS, BearSSL, cJSON, LodePNG, TJpgDec, FatFs, Unity, lwIP,
littlefs, BTstack i drivera Semtech SX126x znajdują się w
`third_party/*_version.conf`:

```bash
./third_party/update_components.sh
./third_party/update_components.sh --verify-only
```

Kompletna polityka komponentów, wraz z listą kontrolną aktualizacji (spis
bezpieczeństwa, SBOM, dotknięte buildy, pełna bramka), jest udokumentowana w
[third_party/README.md](third_party/README.md).

## Dokumentacja

Główna dokumentacja:

- Pełny spis: [table_of_contents.pl.md](doc/table_of_contents.pl.md)
- Przegląd funkcjonalności: [features.md](doc/pl/features.md)
- Skrypty obsługi repozytorium i orkiestracja: [00_scripts.md](doc/api/pl/00_scripts.md)
- Dokumentacja API: [JaszczurHAL_API.md](doc/pl/JaszczurHAL_API.md)
- Workflow projektu firmware: [FwProjectWorkflow.md](doc/pl/FwProjectWorkflow.md)
- Natywny workflow OTA: [OTAWorkflow.md](doc/pl/OTAWorkflow.md)
- Profile targetów i płytek: [boards_profiles_howto.md](doc/pl/boards_profiles_howto.md)
- Changelog (tylko po angielsku): [CHANGELOG.md](doc/CHANGELOG.md)
- Podsumowanie flag buildu: [HAL_FLAGS](doc/HAL_FLAGS.txt)
- Przewodnik buildu linkowalnej biblioteki statycznej: [lib_compilation.md](doc/pl/lib_compilation.md)
- Workflow firmware w VS Code: [vscode/README.md](vscode/README.md)

## Uwagi i podziękowania

- SmartTimers oparte jest na [Nettigo Timers](https://github.com/nettigo/Timers)
  (forku [garthoff/Timers](https://github.com/garthoff/Timers)).
- Framework testowy Unity jest przypięty do forka projektu:
  [Unity pin](third_party/unity_version.conf)
- Współdzielony stos wyświetlaczy (`src/hal/display/drivers/`) jest przenośną
  reimplementacją opartą na HAL. Silnik GFX (`jh_gfx.*`) adaptuje algorytmy
  renderowania z [Adafruit GFX Library](https://github.com/adafruit/Adafruit-GFX-Library),
  a drivery paneli (`ili9341_driver.*`, `st77xx_driver.*`,
  `ssd1306_driver.*`) adaptują sekwencje poleceń kontrolerów z odpowiadających
  im bibliotek Adafruit ILI9341 / ST7735-ST7789 / SSD1306 autorstwa Limor
  Fried (Ladyada) dla Adafruit Industries (BSD-2-Clause). Maszyny stanów/
  protokoły e-papieru SSD16xx i UC81xx wykorzystują logikę driverów Zephyr
  (Apache-2.0-Clause). Atrybucje dla poszczególnych modułów znajdziesz w
  nagłówkach plików.
- Dołączone, portowane lub lokalnie zaadaptowane komponenty zewnętrzne:
  [cJSON pin](third_party/cjson_version.conf),
  [LodePNG pin](third_party/lodepng_version.conf),
  [TJpgDec pin](third_party/jpeg_version.conf),
  [FatFs pin](third_party/fatfs_version.conf),
  [Unity pin](third_party/unity_version.conf),
  [FreeRTOS-Kernel pin](third_party/freertos_core_version.conf),
  [BearSSL pin](third_party/bearssl_version.conf),
  [lwIP pin](third_party/lwip_version.conf),
  [littlefs pin](third_party/littlefs_version.conf),
  [Semtech SX126x driver pin](third_party/sx126x_driver_version.conf),
  [PubSubClient](src/hal/network/mqtt/PubSubClient/),
  [shared WireGuard/lwIP engine](src/hal/network/wireguard/core/),
  [LiquidCrystal / HD44780](src/hal/display/hd44780/),
  [Brian Varren DACless](src/hal/audio/dacless/),
  [Seeed/Loovee MCP_CAN / MCP2515](src/hal/can/mcp2515/),
  [MCP251XFD](src/hal/can/mcp251xfd/),
  [Adafruit NeoPixel](src/hal/gpio/neopixel/),
  [Adafruit STMPE610](src/hal/input/stmpe610/),
  [Adafruit TSC2007](src/hal/input/tsc2007/),
  [Paul Stoffregen OneWire](src/hal/onewire/),
  [Bonezegei DHT11/DHT22 by Bonezegei (Jofel Batutay)](src/hal/temperature/dht/),
  [Adafruit MAX6675](src/hal/temperature/max6675/),
  [Adafruit MCP9600](src/hal/temperature/mcp9600/),
  [ArtronShop BH1750](src/hal/sensors/bh1750/),
  [Eric Ayars / JeeLabs / RTClib-style DS3231](src/hal/rtc/ds3231/),
  [IRsmallDecoder / RC5 decoder attribution](src/hal/input/irsmall_decoder/).
