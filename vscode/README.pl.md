# Punkt wejścia VS Code JaszczurHAL

Ten katalog zawiera wspólny workflow firmware VS Code dla projektów używających
JaszczurHAL: build zwykły i debug, upload, upload UF2, flash ESP-IDF, monitor
serial, odświeżanie IntelliSense, helpery boardów i portów oraz czyszczenie
tożsamości USB.

Śledzona konfiguracja używana po otwarciu głównego katalogu repozytorium
JaszczurHAL w VS Code jest osobnym workflow biblioteki statycznej opisanym w
[instrukcji builda biblioteki](../doc/pl/lib_compilation.md#workspace-repozytorium-i-vs-code).
Nie traktuje ona głównego katalogu repozytorium jako projektu firmware.

Stabilny interfejs publiczny znajduje się w `entry/`. Pliki
`.vscode/tasks.json` projektu powinny wywoływać `entry/jh-vscode` na Unix i
`entry/jh-vscode.cmd` na Windows, pozostawiając zachowanie właściwe dla projektu
w konfiguracji. Przenośna logika CLI, konfiguracji, CMake, artefaktów, OTA i
trwałego monitora znajduje się w `runtime/`. Operacje hosta korzystają z leniwie
ładowanego adaptera platformy. Implementacja Linux i entrypointy zgodności są w
`linux/runtime/`, a adapter native Windows zapewnia tożsamość COM, własność
procesów, wykrywanie woluminów BOOTSEL, trwały upload UF2 i locki builda.
Katalogi runtime są szczegółami implementacji i zwykłymi pakietami Pythona z
`__init__.py` na każdym poziomie.

Pełny model projektu firmware opisuje
[`doc/pl/FwProjectWorkflow.md`](../doc/pl/FwProjectWorkflow.md), a wymagania OTA
native RP i ESP32-S3 wraz z firewallem i odzyskiwaniem opisuje
[`doc/pl/OTAWorkflow.md`](../doc/pl/OTAWorkflow.md).

## Launchery hosta

Oba launchery wykonują `entry/jh_vscode.py`, który importuje wspólny runtime.
Launcher Unix używa `python3`. Launcher Windows wybiera pierwszy interpreter
Python 3 z pyserial w kolejności:

1. jawnie ustawiony `JH_VSCODE_PYTHON`;
2. `.build/windows/venv/Scripts/python.exe` pod głównym katalogiem JaszczurHAL;
3. `py -3`;
4. `python`.

`JH_VSCODE_PYTHON` musi wskazywać program interpretera bez dodatkowych
argumentów. Brak właściwego interpretera zwraca kod 8 z diagnostyką konfiguracji
hosta. Zarządzane środowisko tworzy `runmefirst.ps1`.

Generowane `tasks.json` zachowują polecenie Unix w `command` i dodają nadpisanie
Windows korzystające z `jaszczurhal.vscodeEntryWindows`. Generowane
`settings.json` wskazują sąsiedni `jh-vscode.cmd`. Etykiety tasków i argumenty
są identyczne na obu hostach. Operacje urządzeń Windows używają native API COM i
woluminów; nie wymagają WSL, Git Bash ani warstwy POSIX.

## Interfejs CLI

```text
jh-vscode <action> [options]
```

Dostępne akcje:

```text
build
build-debug
upload
upload-uf2
upload-ota
ota-discover
monitor
monitor-probe
monitor-any
refresh-intellisense
clean
select-board
sync-board-picker
list-ports
change-port
clear-identity
config-dump
debug-tools
```

`debug` jest akceptowany jako tymczasowy alias `build-debug`; nowe taski powinny
używać `build-debug`. `change-port` wybiera port interaktywnie albo przez
`--port` i zapisuje go jako lokalne `uploadPort` w
`.vscode/jaszczurhal.local.json`.

`debug-tools --project <path> --json` rozwiązuje sprawdzone GDB dla Arm, OpenOCD,
katalog skryptów oraz skrypty interfejsu i targetu właściwe dla rodziny boardu.
Na Linux generowane ustawienia wybierają `gdb-multiarch` instalowane przez
`runmefirst.sh`. Na native Windows ścieżki pochodzą z rekordu środowiska hosta
zapisanego przez bootstrap; `runmefirst.ps1` zapisuje `openocd` i
`armToolchainPath` w ustawieniach użytkownika Cortex-Debug.

`list-ports --json` raportuje listę ścieżek zgodności `bootsel` i strukturalne
`bootselRecords` z mountem, ścieżką urządzenia, GUID-em woluminu Windows,
etykietą i filesystemem, jeśli platforma je udostępnia.

`sync-board-picker` odświeża input `boardSelection` i automatyczny task otwarcia
katalogu na podstawie rejestru `boards/`. Tworzy lub naprawia zarządzane profile
Cortex-Debug w `launch.json` dla RP2040, RP2350 ARM i STM32G474/ST-Link.
Zachowuje konfiguracje nazwane przez konsumenta. VS Code może jednorazowo
wymagać zgody przez `Tasks: Manage Automatic Tasks`; dynamicznym fallbackiem
terminalowym pozostaje `Project: Select board`.

## Generowane taski VS Code

Generator zapisuje te same etykiety i argumenty na obu hostach:

| Task | Akcja CLI | Zachowanie |
|---|---|---|
| `Project: Build` | `build` | Buduje aktywny target i board w Release oraz publikuje stabilne artefakty; domyślny task builda. |
| `Project: Build (Debug)` | `build-debug` | Używa osobnego cache CMake Debug, publikuje ELF Debug i poprzedza profile Cortex-Debug. |
| `Project: Upload` | `upload` | Buduje i wgrywa przez backend targetu: CDC-to-BOOTSEL UF2 dla RP, OpenOCD dla STM32G474 lub sprawdzony USB Serial/JTAG dla ESP32-S3. |
| `Project: Upload (UF2 / BOOTSEL)` | `upload-uf2` | Buduje obraz RP, sprawdza UF2 i kopiuje na jeden jednoznaczny wolumin BOOTSEL. |
| `Project: Upload (OTA)` | `upload-ota --interactive` | Buduje obraz OTA, wykrywa urządzenia native RP lub ESP-IDF, uwierzytelnia i pyta przy niejednoznacznym wyborze. |
| `Project: Discover OTA devices` | `ota-discover` | Wyświetla zgodne urządzenia OTA, adres, target, generację, slot i stan boot. |
| `Project: List ports` | `list-ports` | Pokazuje porty serial, dopasowania tożsamości i kandydatów BOOTSEL bez otwierania urządzenia. |
| `Project: Change port` | `change-port` | Wybiera port i zapisuje go w ignorowanej lokalnej konfiguracji. |
| `Project: Serial Monitor` | `monitor --lock-policy replace-own` | Uruchamia trwały monitor projektu i może zastąpić tylko sprawdzony monitor JaszczurHAL tego samego portu. |
| `Project: Debug Probe Monitor` | `monitor-probe --lock-policy replace-own` | Uruchamia monitor dla skonfigurowanej tożsamości debug probe. |
| `Project: Serial Monitor (Any)` | `monitor-any --lock-policy wait` | Czeka na dowolny właściwy port i nie wypiera innego właściciela. |
| `Project: Refresh IntelliSense` | `refresh-intellisense` | Buduje target compile database i zapisuje poprawioną bazę pod stabilną ścieżką cpptools. |
| `Project: Clean` | `clean` | Po walidacji ścieżek usuwa artefakty projektu i odpowiadające drzewa CMake. |
| `Project: Clear USB Identity` | `clear-identity` | Buduje neutralny firmware RP i wgrywa go po zwykłych kontrolach bezpieczeństwa. |
| `Project: Config Dump` | `config-dump` | Wypisuje rozwiązany manifest, lokalne nadpisania, target, board, ścieżki, upload i rozwiązanie funkcji HAL. |
| `Project: Select board` | `select-board --interactive` | Wybiera target i board w terminalu oraz zapisuje wybór lokalnie. |
| `Project: Select board (GUI)` | `select-board --selection ...` | Używa generowanego pickera VS Code i zapisuje parę target/board. |
| `Project: Sync board picker` | `sync-board-picker` | Odświeża picker i profile debug RP2040, RP2350 Arm i STM32G474. |
| `Project: Build variant: <id>` | `build --variant <id>` | Buduje zadeklarowany wariant przykładu zwykłą ścieżką artefaktów. |

Panel Run and Debug udostępnia profile `Project: Debug Firmware` dla RP2040,
RP2350 ARM i STM32G474/ST-Link. Każdy najpierw wykonuje
`Project: Build (Debug)`, a następnie ładuje ELF z ustawieniami probe, OpenOCD i
resetu właściwymi dla profilu.

Najważniejsze opcje:

```text
--project <path>       Katalog modułu firmware.
--target <id>          Target tylko dla tego wywołania.
--board <id>           Board tylko dla tego wywołania.
--variant <id>         Wariant zadeklarowany w manifeście.
--selection <t:b>      Trwale wybierz target/board.
--interactive          Interaktywny wybór targetu/boardu.
--port <port>          Nadpisz port uploadu/monitora.
--bootsel-volume <id>  Wybierz katalog dysku BOOTSEL lub GUID woluminu Windows.
--host <address>       Pomiń wykrywanie OTA i użyj podanego adresu.
--baud <baud>          Baud monitora serial, domyślnie 115200.
--lock-policy <mode>   Polityka locka monitora: wait, replace-own, replace-any.
--allow-unverified-port
                       Ekspercki upload na jawny port bez zgodnej tożsamości USB.
--verbose              Szczegółowe wyjście.
--json                 Wyjście maszynowe, jeśli wspierane.
--help                 Pomoc.
--version              Wersja narzędzia.
```

`--project` oznacza katalog modułu firmware, nie główny katalog repozytorium:

```text
jh-vscode build --project /home/user/projects/router-reset/reseter
jh-vscode build --project /home/user/projects/Fiesta/src/Clocks
jh-vscode clear-identity --project /home/user/projects/Fiesta/src/ECU
```

Akcje dotyczące modułu wymagają `--project` i muszą zgłosić niejednoznaczny
moduł przed dostępem do serial, dysków BOOTSEL lub artefaktów builda.

Projekty RP i STM32 używają `toolchain: "cmake"` i wspólnego dispatchera. Runtime
rozwiązuje target/board, konfiguruje CMake i uruchamia targety `firmware`,
`firmware_debug`, `firmware_upload` oraz `firmware_compile_db`. Domyślnie używa
Ninja, eksportuje compile commands i przekazuje bieżący interpreter Python.
Manifest wskazuje `cmake.sourceDir` na `cmake/jh_firmware_project` oraz przekazuje
katalog modułu jako `JH_PROJECT_DIR`.

Native Windows używa sprawdzonego stanu narzędzi i krótkiego katalogu builda z
`runmefirst.ps1`. Cache CMake pozostaje pod krótkim katalogiem, a końcowe
artefakty w `buildDir` manifestu. Przed próbą builda stabilne obrazy są usuwane i
publikowane ponownie dopiero po sukcesie targetu.

Targety RP budują bezpośrednio przez oficjalny Pico SDK i udostępniają USB CDC
należące do HAL. Upload zwalnia monitor, wykonuje touch DTR 1200 bps, czeka na
jeden dysk BOOTSEL i kopiuje UF2. STM32G474 obsługuje bare-metal i FreeRTOS oraz
wgrywanie OpenOCD.

Projekty ESP32-S3 używają `toolchain: "esp-idf"`. Build uruchamia produkcyjny
runner i ponownie sprawdza `jh_esp_idf_artifacts.json`, zachowując uporządkowany
zestaw bootloadera, tabeli partycji i obrazu aplikacji. Upload zwalnia monitor i
przekazuje sprawdzony port do akcji `flash`. `refresh-intellisense` zachowuje
kompilator Xtensa i flagi. ESP32-S3 nie udostępnia `build-debug` ani zarządzanych
profili Cortex-Debug.

## Dodawanie plików źródłowych projektu

Wykrywanie źródeł i wszystkie reguły `JH_PROJECT_SOURCES` opisuje wyłącznie
[Dodawanie plików źródłowych projektu](../doc/pl/FwProjectWorkflow.md#dodawanie-plików-źródłowych-projektu).

## Generator nowego projektu

`tools/create-vscode-example.py` tworzy samodzielny projekt firmware VS Code
oparty na CMake obok innych repozytoriów:

```bash
libraries/JaszczurHAL/vscode/tools/create-vscode-example.py \
  --output /home/user/projects/jaszczurhal-vscode-example
```

Projekt zawiera małą aplikację blink, lokalne `.vscode/` i
`hal_project_config.h` z `HAL_PROVIDE_APP_ENTRY`. Nie zawiera lokalnego
`CMakeLists.txt`; manifest wskazuje wspólny dispatcher i zapisuje początkowy
target/board. Inny board początkowy wybierają `--target` i `--board`:

```bash
libraries/JaszczurHAL/vscode/tools/create-vscode-example.py \
  --output /home/user/projects/jaszczurhal-stm32-example \
  --target stm32g474 \
  --board nucleo-g474re
```

Flagi przyjmują `HAL_ENABLE_X` albo `HAL_ENABLE_X=1`. `jh-vscode` odrzuca `=0`,
wyrażenia generatora CMake i nieznane lub pochodne symbole, po czym rozwiązuje
przechodnie zależności, wymagania i konflikty. `config-dump` udostępnia wynik w
`featureResolution`. Nagłówek projektu jest wejściem zawierającym wyłącznie
makra, ładowanym przed autodetekcją; definicje funkcji muszą być bezwarunkowe
albo chronione przez `#ifndef` tego samego symbolu.

Wygenerowany projekt używa zwykłych akcji `jh-vscode`:

```bash
../libraries/JaszczurHAL/vscode/entry/jh-vscode build --project "$PWD"
../libraries/JaszczurHAL/vscode/entry/jh-vscode build-debug --project "$PWD"
../libraries/JaszczurHAL/vscode/entry/jh-vscode refresh-intellisense --project "$PWD"
../libraries/JaszczurHAL/vscode/entry/jh-vscode select-board --project "$PWD" --interactive
```

`build-debug` używa osobnego cache Debug na target i board, ale publikuje te
same stabilne ścieżki artefaktów. Profile Cortex-Debug obsługują RP2040, RP2350
Arm i STM32G474. RP wymaga osobnego CMSIS-DAP/Picoprobe na SWD; STM32G474 używa
wbudowanego ST-Link NUCLEO-G474RE. Sprawdzone prędkości adaptera to 5 MHz dla
RP2040 i 2 MHz dla RP2350.

Projekt powinien znajdować się poza `libraries/JaszczurHAL/vscode/`. Artefakty
generowane odświeża wspólny runner:

```bash
python3 scripts/sync_generated.py --check
python3 scripts/sync_generated.py --write
```

## Rozszerzenia VS Code

Sprawdzenie aktywnego profilu względem wspólnej listy:

```bash
python3 vscode/tools/manage_vscode_extensions.py
```

Interaktywna instalacja braków:

```bash
python3 vscode/tools/manage_vscode_extensions.py --install
```

`--install --yes` wyraża jawną zgodę nieinteraktywną. Każda instalacja jest
sprawdzana przez `code --list-extensions`. Użyj `--code <path>` lub
`JH_VSCODE_CODE`, gdy `code` nie znajduje się w `PATH`.

## Skróty klawiszowe VS Code

Pliki `.vscode/keybindings.reference.json` są tylko referencją. Skróty muszą
znajdować się w rzeczywistym pliku użytkownika VS Code:

```text
~/.config/Code/User/keybindings.json
```

Utrzymywane przypisania:

```text
Ctrl+Shift+1  Project: Build
Ctrl+Shift+2  Project: Upload
Ctrl+Shift+3  Project: Serial Monitor
Ctrl+Shift+4  Project: Upload (UF2 / BOOTSEL)
Ctrl+Shift+5  Project: Debug Probe Monitor
Ctrl+Shift+6  Project: Refresh IntelliSense
Ctrl+Shift+7  Project: Clean
Ctrl+Shift+8  Project: Upload (OTA)
Ctrl+Shift+9  Project: Config Dump
Ctrl+Shift+Alt+1  Project: Select board (GUI)
Ctrl+Shift+Alt+2  Project: Select board
Ctrl+Shift+Alt+3  Project: Discover OTA devices
```

Prawidłowe przypisanie monitora wygląda tak:

```json
{
    "key": "ctrl+shift+3",
    "command": "workbench.action.tasks.runTask",
    "args": "Project: Serial Monitor"
}
```

Po udanym buildzie lub uploadzie narzędzie pokazuje skrót mapy pamięci ELF.
`JH_VSCODE_MEMORY_OVERVIEW=0` wyłącza ten raport.

Monitor domyślnie używa `--lock-policy wait`. `replace-own` może zatrzymać tylko
inny monitor JaszczurHAL tego samego projektu po sprawdzeniu znacznika własności,
PID i tożsamości startu procesu. `replace-any` zachowuje to samo ograniczenie i
nigdy nie zatrzymuje obcego procesu.

Port monitora Pico może podążać za jedynym urządzeniem CDC o sprawdzonej
tożsamości USB projektu po zmianie numeru `ttyACM` lub COM. Przy zeru albo wielu
dopasowaniach monitor czeka; jawny `--port` pozostaje przypięty. Upload również
sprawdza wszystkie skonfigurowane stabilne pola tożsamości. Pierwsze wgranie na
czysty board serial wymaga jawnego
`--allow-unverified-port --port <port>`; domyślne taski nie przekazują tej flagi.

Tożsamość programatora ESP32-S3 pochodzi z deskryptora boardu. Dla
`waveshare-esp32-s3-zero` native USB Serial/JTAG ma VID/PID `303a:1001`.
Niejednoznaczne lub niezgodne porty są odrzucane.

## Pierwszeństwo konfiguracji

Wybór targetu/boardu, overlaye manifestu, fallback ustawień i lokalny stan
opisuje wyłącznie [Rozwiązywanie targetu i konfiguracji](../doc/pl/FwProjectWorkflow.md#rozwiązywanie-targetu-i-konfiguracji).

## Minimalny manifest projektu

Utrzymywany przykład manifestu i reguły overlay znajdują się w
[Minimalnym manifeście](../doc/pl/FwProjectWorkflow.md#minimalny-manifest).
Walidacja maszynowa używa `schema/jh_vscode_project.schema.json`.

## Tożsamość USB

Tożsamość USB oznacza deskryptory producenta i produktu widoczne dla hosta.
Manifest może dodatkowo przypiąć `usbVid`, `usbPid`, `usbSerialNumber`,
`usbInterface` lub `usbLocation`. Numer COM jest wyłącznie lokalnym wyborem w
`.vscode/jaszczurhal.local.json`, nigdy stabilną tożsamością.

Dla ESP-IDF stały VID/PID programatora pochodzi z
`boards/profiles/<board>.json.programming.usb` i nie może być duplikowany w
manifeście. Native RP wstrzykuje tożsamość przez cache CMake:

```text
-DJH_USB_MANUFACTURER="Jaszczur"
-DJH_USB_PRODUCT="Router Reset"
```

`clear-identity` buduje neutralny firmware z `neutral_fw/rp_native/` bez
własnych wpisów tożsamości i wgrywa go na sprawdzony target serial lub jeden
jednoznaczny BOOTSEL.

`upload-uf2` sprawdza każdy blok UF2 i wymaga jednego woluminu FAT oznaczonego
`RPI-RP2`, `RP2350` lub `RPI-RP2350`. Przy wielu urządzeniach użyj
`--bootsel-volume`; wybór można zapisać jako lokalne `bootselVolume`. Upload RP
przez CDC wykonuje reset 1200 bps i czeka wyłącznie na nowy dozwolony wolumin.
Pierwszy flash nadal wymaga ręcznego BOOTSEL.

Trwały monitor jest zwalniany kooperacyjnie przed uploadem i wznawia połączenie
po powrocie boardu. Awaryjne zatrzymanie dotyczy tylko procesu zgodnego z PID,
czasem startu i znacznikiem własności. Windows zapisuje UF2 jako czysty strumień,
opróżnia i zamyka handle; błędy nośnika, krótkie zapisy oraz niespójny artefakt
zwracają kod uploadu 6.

## Native OTA

`upload-ota` obsługuje sieciową aktualizację boardów WiFi native `rp2040` i
`rp2350-arm` z `HAL_ENABLE_OTA`. Buduje projekt, znajduje artefakt `.ota`,
podpisuje nagłówek skonfigurowanym hasłem, przesyła obraz do staging slotu i
czeka na akceptację. Hasło nie trafia do niepodpisanego artefaktu builda.

Dla ESP-IDF akcja sprawdza `jh_esp_idf_artifacts.json`, wymaga `HAL_ENABLE_OTA`
w rozwiązanym zbiorze funkcji i porównuje BIN aplikacji z rozmiarem i SHA-256.
Wysyła surowe bajty aplikacji do nieaktywnej partycji `two-ota-large`; nie tworzy
kontenera `.ota` RP. Ścieżka wymaga jeszcze pełnej weryfikacji sprzętowej,
przerwań, rollbacku i negatywnych przypadków bezpieczeństwa.

`ota-discover` rozgłasza żądanie i wypisuje hostname, adres, target, port,
rozmiar slotu, generację obrazu i boot mode. Przy wielu urządzeniach użyj
`--interactive`, `ota.host` albo `--host <address>`.

Sekrety powinny pozostawać poza manifestem przez `ota.passwordEnv`. Niepuste
hasło wymaga wymiany HMAC-SHA256 AUTH2. `allowEmptyPassword` jawnie dopuszcza
nieuwierzytelniony transfer deweloperski. Callback hosta domyślnie używa TCP/8266,
a `runmefirst.sh` proponuje ograniczoną do LAN regułę firewalla.

Pełną integrację, pierwszą instalację, potwierdzanie trial, rollback i
odzyskiwanie opisuje [Workflow native OTA](../doc/pl/OTAWorkflow.md). Na RP
pierwsza instalacja używa scalonego UF2 przez BOOTSEL, a aplikacja potwierdza
trial przez `hal_ota_confirm_boot_ex()` dopiero po pomyślnych self-testach.
ESP32-S3 odzyskuje się przez ponowne wgranie pełnego zestawu obrazów ESP-IDF.

Aplikacja referencyjna to
[`examples/25_ota`](../examples/25_ota/README.pl.md).

## Kody wyjścia

```text
0   Sukces.
1   Błąd ogólny.
2   Nieprawidłowe użycie CLI.
3   Brak lub błędna konfiguracja projektu.
4   Niejednoznaczny albo niebezpieczny wybór urządzenia.
5   Błąd builda.
6   Błąd uploadu.
7   Błąd monitora, w tym brak pyserial na hoście.
8   Niewspierana akcja, ścieżka platformy lub niepełne zależności launchera.
```

## Generowane pliki i cache builda

Katalogi artefaktów, compile databases, generowane adaptery, izolację cache
target/board, reset nieaktualnego cache i własność kluczy opisuje wyłącznie
[Katalogi builda i generowane pliki](../doc/pl/FwProjectWorkflow.md#katalogi-buildu-i-pliki-generowane).
