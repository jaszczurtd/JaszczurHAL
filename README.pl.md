# JaszczurHAL

*Dostępne również [po angielsku](README.md).*

Autor: Marcin 'Jaszczur' Kielesinski

JaszczurHAL to warstwa abstrakcji sprzętowej (HAL) i biblioteka narzędziowa dla
systemów wbudowanych opartych na RP2040, RP2350, STM32 i ESP32.
Zacznij od [polskiego spisu dokumentacji](doc/table_of_contents.pl.md) albo
zobacz [przegląd funkcjonalności](doc/pl/features.md), aby poznać zwięzły spis
obsługiwanych modułów i funkcji.

## Po co to powstało?

Wiele projektów systemów wbudowanych zaczyna się od kodu pisanego w pośpiechu.
Z czasem coraz trudniej je rozwijać, zwłaszcza gdy dostęp do sprzętu jest
ściśle powiązany z logiką aplikacji albo sterowniki są przywiązane do
konkretnego targetu sprzętowego.

JaszczurHAL wprowadza wyraźny podział odpowiedzialności:

- warstwa aplikacji: przenośna logika,
- warstwa HAL: spójne, przenośne API oddzielające szczegóły sprzętowe
  od logiki aplikacji,
- warstwa mock: deterministyczne testowanie po stronie hosta,
- sterowniki wielokrotnego użytku, bezpieczne w środowisku wielowątkowym
  i współdzielone przez wszystkie obsługiwane targety sprzętowe,
- opcjonalne moduły włączane jawnie flagami kompilacji `HAL_ENABLE_*`,
- opcjonalny stos łączności, bezpieczeństwa i pamięci masowej dla projektów
  firmware z połączeniem sieciowym,
- zestaw narzędzi do typowych zadań w systemach wbudowanych (timery, PID,
  watchdog, funkcje pomocnicze),
- w pełni funkcjonalna obsługa FreeRTOS (V11.3.0).

Kod aplikacji pozostaje przenośny pomiędzy obsługiwanymi targetami
i środowiskami wykonawczymi.

Projekt jest już bardzo użyteczny, ale wciąż zostało tu i ówdzie kilka
obszarów wymagających dalszej pracy (WIP). Niestety, oprócz pracy nad projektem
hobbystycznym,
trzeba też jakoś zarabiać na życie - i znaleźć czas na samo życie. :)

## Czy to jest gdzieś faktycznie używane?

Tak - w kilku moich bardziej wymagających projektach.

Najbardziej widocznym przykładem JaszczurHAL w praktyce jest projekt Fiesta: https://github.com/jaszczurtd/Fiesta

To mój prywatny projekt modernizacji samochodu, złożony z kilku ściśle
zintegrowanych modułów. Moduł ECU jest chyba najbardziej wymagający: używa
JaszczurHAL w trybie dwurdzeniowym do sterowania pompą wtryskową VP37,
komunikacji CAN z resztą systemu, diagnostyki OBD i innych funkcji
niskopoziomowych.

Są też mniejsze (ale nietrywialne) projekty, na przykład:

* https://github.com/jaszczurtd/doomConsole (port gry Doom z dźwiękiem, wyświetlaczem TFT i obsługą Gamepada Bluetooth 8BitDo)
* https://github.com/jaszczurtd/Ford-Mondeo-MK-DPF-Tracker (urządzenie śledzące cykle regeneracji DPF)
* https://github.com/jaszczurtd/lights-timer (zdalne sterowanie oświetleniem akwarium za pomocą aplikacji na Androida)

## Szybki start

Są dwa typowe punkty startowe:

- Aby poznać [API HAL](doc/pl/JaszczurHAL_API.md), sposób przenoszenia kodu
  i zakres obsługi poszczególnych platform, zacznij od gotowych przykładów:
  [examples/README.pl.md](examples/README.pl.md).
- Aby utworzyć nowy projekt firmware z możliwością wyboru targetu,
  przeznaczony do codziennej pracy w VS Code, użyj generatora projektów:

```bash
libraries/JaszczurHAL/vscode/tools/create-vscode-example.py \
  --output your-example-project-name
```

Wygenerowany projekt startuje na `rp2040/pico` (zmień to za pomocą
`--target`/`--board` albo zadania `Project: Select board`) i ma gotowe do
użycia zadania VS Code do kompilowania, wgrywania i monitorowania. Opcje
generatora, pierwsze wgranie firmware na czystą płytkę oraz pełny opis zadań są
udokumentowane w [vscode/README.pl.md](vscode/README.pl.md).

## Przykłady

Drzewo `examples/` zawiera przykładowe aplikacje, które pokazują współdziałanie
powiązanych modułów HAL. Każdy przykład składa się z przenośnego pliku
`app.c` lub `app.cpp` oraz pasującego pliku `hal_project_config.h`. Korzysta ze
wspólnego interfejsu punktu wejścia: `app_start()`, `app_task0()` i opcjonalnie
`app_task1()`. Na RP opcjonalne zadanie działa na drugim rdzeniu, a w trybie
bare metal na STM32G474 jest wywoływane kooperacyjnie
(`HAL_ENABLE_APP_TASK1`).
ESP-IDF uruchamia `app_start()`, `app_task0()` oraz opcjonalnie `app_task1()` w
ramach działającego już planisty FreeRTOS. ESP32-S3 domyślnie przypisuje zadania
0 i 1 do rdzeni 0 i 1, ale to przypisanie można jawnie zmienić.

Macierz kompilacji, wymagania, lista targetów obsługiwanych przez poszczególne
przykłady oraz zasada rozszerzania istniejącego projektu lub wariantu przed
utworzeniem kolejnego katalogu są opisane w
[examples/README.pl.md](examples/README.pl.md).

## Obsługiwane targety i moduły (szybki przegląd)

Firmware dla RP2040 i RP2350 jest kompilowane bezpośrednio z oficjalnym Pico
SDK. STM32G474 korzysta z implementacji bare metal i procesu linkowania
dostarczonych przez repozytorium. Target ESP32-S3 jest oparty na ESP-IDF.
FreeRTOS jest opcjonalny na RP i STM32G474, a wymagany przez runtime ESP-IDF.
Backend mock służy do deterministycznej weryfikacji na hoście.

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
jest poprawna i obsługiwana oraz czy jej parametry są prawidłowe.

Pełną macierz flag, zasady propagacji zależności i opcje `HAL_ENABLE_*`
znajdziesz w:

- [JaszczurHAL_API.md](doc/pl/JaszczurHAL_API.md)
- [doc/api/pl/02_module_flags.md](doc/api/pl/02_module_flags.md)
- [doc/HAL_FLAGS.txt](doc/HAL_FLAGS.txt)

## Przykład wyboru targetu dla wielu platform

Niezależnie od flag modułów JaszczurHAL wybiera dokładnie jeden backend
sprzętowy poprzez `src/hal/core/hal_target.h`. Zdefiniuj jedną
z poniższych flag w `hal_project_config.h` (lub przez `-D`):

```c
#define HAL_TARGET_RP2040        // RP2040, Cortex-M0+
#define HAL_TARGET_RP2350_ARM    // RP2350, Cortex-M33
#define HAL_TARGET_RP2350_RISCV  // RP2350, Hazard3 RISC-V
#define HAL_TARGET_STM32G474     // STM32G474
#define HAL_TARGET_ESP32_S3      // ESP32-S3, natywny ESP-IDF
#define HAL_TARGET_MOCK          // deterministyczna implementacja testowa dla hosta
```

Jeśli żadnej nie zdefiniujesz, target jest **wykrywany automatycznie** na
podstawie toolchainu. Pliki backendów są kompilowane tylko dla wybranego
targetu, więc nieużywane warianty nie zajmują pamięci programu.

Oficjalne kompilacje wybierają stabilny target i identyfikator płytki poprzez
generowany rejestr płytek. Zobacz
[Profile targetów i płytek](doc/pl/boards_profiles_howto.md).
Zobacz też [FwProjectWorkflow.md](doc/pl/FwProjectWorkflow.md), aby poznać
pełny model targetu/płytki/konfiguracji.

W praktyce nie musisz znać wewnętrznego działania logiki wyboru targetu.
Wystarczy nacisnąć `Ctrl+Shift+Alt+1` w swoim projekcie i wybrać target z menu.

Oto [pełna lista](vscode/README.pl.md#skróty-klawiszowe-vs-code) dostępnych
skrótów klawiszowych VS Code.

## Opcjonalny FreeRTOS (opt-in)

Obsługę FreeRTOS włącza się jawną flagą kompilacji:

```c
#define HAL_ENABLE_FREERTOS
```

Aplikacje używają bezpośrednio standardowych nagłówków i API projektu
FreeRTOS na wszystkich obsługiwanych targetach.

JaszczurHAL ukrywa szczegóły startu specyficzne dla targetu, takie jak
uruchomienie planisty i opcjonalne rozmieszczenie zadań aplikacji. Targety
RP używają FreeRTOS-Kernel w ustalonej wersji, z obsługą SMP, natomiast
STM32G474 używa tego samego jądra z portem Cortex-M4F. ESP32-S3 używa instancji
FreeRTOS dostarczonej przez ESP-IDF w ustalonej wersji; jego deskryptor targetu
dodaje `HAL_ENABLE_FREERTOS` jako wymaganą flagę i obsługuje opcjonalne drugie
zadanie aplikacji.

Szczegółowe informacje o zarządzaniu wersją kernela, portach i wariantach
kompilacji są dostępne w [lib_compilation.md](doc/pl/lib_compilation.md) oraz
[doc/api/pl/04_multicore_drivers_migration.md](doc/api/pl/04_multicore_drivers_migration.md).

## Bezpieczeństwo wielowątkowe (przegląd)

Bezpieczeństwo wielowątkowe i obsługa wielu rdzeni należą do podstawowych
założeń projektowych na wszystkich targetach. Zakłada się, że inicjalizacja
i zamykanie (`init` / `create` / `destroy` / `deinit`) odbywają się na jednym
rdzeniu. Muteksy współdzielonych instancji i poszczególnych magistral są
tworzone atomowo dopiero przy pierwszym użyciu, z zabezpieczeniem przed
wyścigiem. Backend mock służy do deterministycznych testów jednowątkowych,
a opcjonalna flaga
`JH_ENABLE_FREERTOS_POSIX_TESTS` rozszerza ten zestaw o testy schedulera
FreeRTOS po stronie hosta.

Szczegółowe sygnatury, dokładne gwarancje, zachowanie modułów, uwagi
dotyczące poszczególnych implementacji i pokrycie testami znajdziesz w
[JaszczurHAL_API.md](doc/pl/JaszczurHAL_API.md).

## Kompilacja jako biblioteka statyczna (.a)

Kompletny przewodnik po kompilacji JaszczurHAL jako biblioteki statycznej
(`libJaszczurHAL.a`), wraz z kompilacją przykładowych aplikacji oraz zasadami
dotyczącymi rdzenia i punktu wejścia: [lib_compilation.md](doc/pl/lib_compilation.md).
Zainstalowane pakiety RP i STM32G474 zawierają wygenerowane nagłówki cech
i płytek, metadane wybranej płytki oraz referencyjny plik źródłowy do kontroli
zgodności podczas linkowania, wymagany przez projekt korzystający bezpośrednio
z kompilatora. Kompilacja i linkowanie zainstalowanego pakietu nie wymagają
Pythona.

## Testy i bramki jakości

```bash
./runalltests.sh
```

Obejmuje testy jednostkowe hosta (z pokryciem FreeRTOS POSIX), kontrole
Clang ASan/UBSan/libFuzzer, Valgrind memcheck, analizę statyczną, kontrole
duplikatów i dokumentacji oraz macierze
kompilacji dla targetów i firmware. Stanowiska testowe sprzętu są udokumentowane
i wykonywane osobno. Pełna
architektura testów, wymagania, konfiguracja, zasady rozszerzania, procedury
stanowisk testowych i zarejestrowane wyniki znajdują się w
[Zależności kompilacji, testy i stanowiska testowe sprzętu](doc/api/pl/03_build_tests.md).
Szczegóły implementacji skryptu uruchamiającego i bramek jakości znajdziesz w
[Skryptach obsługi repozytorium JaszczurHAL](doc/api/pl/00_scripts.md).

## Bezpieczeństwo i SBOM

JaszczurHAL utrzymuje lekki rejestr łańcucha dostaw oprogramowania dla
dołączonych zależności o ściśle określonych wersjach:

- [SECURITY.md](SECURITY.md) - zgłaszanie i wstępna ocena podatności oraz polityka
  utrzymania,
- [doc/pl/security_supply_chain.md](doc/pl/security_supply_chain.md) - generowanie
  SBOM, kontrole podatności i polityka `security-scan` w CI,
- [security/third_party.json](security/third_party.json) - utrzymywany ręcznie
  spis zależności zewnętrznych,
- [security/sbom.cdx.json](security/sbom.cdx.json) - generowany SBOM w
  formacie CycloneDX.

## Środowisko programistyczne VS Code

`vscode/` to obsługiwana warstwa integracji z VS Code dla projektów firmware
korzystających z JaszczurHAL. Projekty wywołują stabilny punkt wejścia:

```text
libraries/JaszczurHAL/vscode/entry/jh-vscode
libraries/JaszczurHAL/vscode/entry/jh-vscode.cmd
```

Punkt wejścia ustala konfigurację projektu oraz wybiera aktywny target i płytkę.
Korzystając ze wspólnego mechanizmu sterującego, kompiluje projekty firmware
oparte na CMake,
wgrywa wynik przez port szeregowy po zweryfikowaniu tożsamości urządzenia
i obsługuje wgrywanie RP2040 przez BOOTSEL/UF2. Operację programowania STM32
przekazuje do OpenOCD, a kompilację i programowanie ESP32-S3 - do produkcyjnego
skryptu ESP-IDF.
Uruchamia też monitory portu szeregowego pozostające aktywne do zatrzymania oraz
odświeża IntelliSense na
podstawie bazy poleceń kompilacji wygenerowanej przez aktywny toolchain.

- Interfejs CLI, etykiety zadań, skróty klawiszowe i generator projektów:
  [vscode/README.pl.md](vscode/README.pl.md)
- Kompletny model projektu i
  [dodawanie plików źródłowych projektu](doc/pl/FwProjectWorkflow.md#dodawanie-plików-źródłowych-projektu):
  [FwProjectWorkflow.md](doc/pl/FwProjectWorkflow.md)
- Aktualizacje sieciowe dla natywnego RP i ESP32-S3, pierwsze wgranie i
  granice bezpieczeństwa:
  [OTAWorkflow.md](doc/pl/OTAWorkflow.md)
- [Pełna lista skrótów klawiszowych](vscode/README.pl.md#skróty-klawiszowe-vs-code)

Gdy sam katalog główny repozytorium JaszczurHAL zostanie otwarty w VS Code,
konfiguracja `.vscode/` przechowywana w repozytorium udostępnia osobny zestaw
zadań do pracy
z biblioteką statyczną. Istniejące globalne skróty budują, instalują,
czyszczą i odświeżają IntelliSense dla jednego profilu targetu i płytki,
wybranego bezpośrednio ze wspólnego rejestru płytek. Artefakty
pozostają poniżej `.build/vscode/library/`; szczegóły znajdziesz w
[przewodniku po kompilacji biblioteki](doc/pl/lib_compilation.md#workspace-repozytorium-i-vs-code).

## Debugowanie w VS Code

Generowane profile Cortex-Debug obsługują RP2040 i RP2350 Arm przez SWD za
pomocą Raspberry Pi Debug Probe albo Pico z firmware Debug Probe/Picoprobe.
Projekty STM32G474 używają interfejsu ST-Link wbudowanego w płytkę
NUCLEO-G474RE. Uruchomienie profilu Run and Debug w VS Code kompiluje i ładuje
plik ELF w wersji debug za pomocą zarządzanego OpenOCD i GDB z obsługą Arm w
Windows i Linuksie. Szczegóły okablowania i konfiguracji zawiera dokument
[Natywna konfiguracja dla Windows](doc/pl/windows_setup.md).

Zarówno w Linuksie, jak i w natywnym środowisku Windows VS Code obsługuje spójny
proces tworzenia firmware dla targetów uwzględnionych w wydaniu. Dla RP i STM
dostępne są
opisane funkcje kompilacji, wgrywania, monitorowania, OTA i debugowania.
Dla ESP32-S3 dostępne są kompilacja, wgrywanie przez port szeregowy, monitor,
IntelliSense i OTA surowego obrazu aplikacji, ale bez zarządzanego profilu
debugowania. Obsługa OTA dla tego targetu nadal wymaga opisanej wyżej walidacji
sprzętowej
fazy 3.5.

Pełna bramka jakości repozytorium działa w Linuksie i obejmuje Valgrind, analizę
statyczną oraz integracje hosta dostępne tylko na POSIX. Szczegóły konfiguracji,
weryfikacji i jawne ograniczenia dotyczące wyłącznie Linuksa zawiera dokument
[Natywna konfiguracja dla Windows](doc/pl/windows_setup.md).

## Zarządzane zależności

Dokładne wersje Pico SDK, ESP-IDF, picotool, PMD CPD, toolchainu RISC-V dla
RP2350, FreeRTOS, BearSSL, cJSON, LodePNG, TJpgDec, FatFs, Unity, lwIP,
littlefs, BTstack i sterownika Semtech SX126x są zapisane w
`third_party/*_version.conf`:

```bash
./third_party/update_components.sh
./third_party/update_components.sh --verify-only
```

Kompletna polityka komponentów, wraz z listą kontrolną aktualizacji (spis
bezpieczeństwa, SBOM, kompilacje objęte zmianą i pełna bramka), jest
udokumentowana w [third_party/README.pl.md](third_party/README.pl.md).

## Dokumentacja

Główna dokumentacja:

- Pełny spis: [table_of_contents.pl.md](doc/table_of_contents.pl.md)
- Przegląd funkcjonalności: [features.md](doc/pl/features.md)
- Skrypty obsługi repozytorium i orkiestracja: [00_scripts.md](doc/api/pl/00_scripts.md)
- Dokumentacja API: [JaszczurHAL_API.md](doc/pl/JaszczurHAL_API.md)
- Praca z projektem firmware: [FwProjectWorkflow.md](doc/pl/FwProjectWorkflow.md)
- Natywne aktualizacje OTA: [OTAWorkflow.md](doc/pl/OTAWorkflow.md)
- Profile targetów i płytek: [boards_profiles_howto.md](doc/pl/boards_profiles_howto.md)
- Podsumowanie flag kompilacji: [HAL_FLAGS](doc/HAL_FLAGS.txt)
- Przewodnik po kompilacji biblioteki statycznej: [lib_compilation.md](doc/pl/lib_compilation.md)
- Obsługa firmware w VS Code: [vscode/README.pl.md](vscode/README.pl.md)

## Uwagi i podziękowania

- SmartTimers oparte jest na [Nettigo Timers](https://github.com/nettigo/Timers)
  (forku [garthoff/Timers](https://github.com/garthoff/Timers)).
- Framework testowy Unity korzysta z forka projektu w wersji wskazanej przez
  [plik wersji Unity](third_party/unity_version.conf).
- Wspólny stos wyświetlaczy (`src/hal/display/drivers/`) jest przenośną
  reimplementacją opartą na HAL. Silnik GFX (`jh_gfx.*`) korzysta
  z dostosowanych algorytmów renderowania zaczerpniętych z
  [Adafruit GFX Library](https://github.com/adafruit/Adafruit-GFX-Library),
  a sterowniki paneli (`ili9341_driver.*`, `st77xx_driver.*`,
  `ssd1306_driver.*`) korzystają z dostosowanych sekwencji poleceń kontrolerów
  zaczerpniętych z odpowiadających
  im bibliotek Adafruit ILI9341 / ST7735-ST7789 / SSD1306 autorstwa Limor
  Fried (Ladyada) dla Adafruit Industries (BSD-2-Clause). Maszyny stanów
  i protokoły e-papieru SSD16xx oraz UC81xx oparto na logice sterowników Zephyr
  (Apache-2.0-Clause). Informacje o pochodzeniu kodu i autorstwie poszczególnych
  modułów znajdziesz w nagłówkach plików.
- Dołączone, portowane lub lokalnie zaadaptowane komponenty zewnętrzne:
  [plik wersji cJSON](third_party/cjson_version.conf),
  [plik wersji LodePNG](third_party/lodepng_version.conf),
  [plik wersji TJpgDec](third_party/jpeg_version.conf),
  [plik wersji FatFs](third_party/fatfs_version.conf),
  [plik wersji Unity](third_party/unity_version.conf),
  [plik wersji FreeRTOS-Kernel](third_party/freertos_core_version.conf),
  [plik wersji BearSSL](third_party/bearssl_version.conf),
  [plik wersji lwIP](third_party/lwip_version.conf),
  [plik wersji littlefs](third_party/littlefs_version.conf),
  [wersja sterownika Semtech SX126x](third_party/sx126x_driver_version.conf),
  [PubSubClient](src/hal/network/mqtt/PubSubClient/),
  [wspólny silnik WireGuard/lwIP](src/hal/network/wireguard/core/),
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
