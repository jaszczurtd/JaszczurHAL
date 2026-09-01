# Praca z projektem firmware

*Dostępne również [po angielsku](../en/FwProjectWorkflow.md).*

Ten dokument opisuje model projektu firmware oparty na wspólnym mechanizmie
obsługi wielu targetów. Korzystają z niego projekty JaszczurHAL i przykłady
przechowywane w repozytorium. Opis obejmuje śledzony manifest, wybór targetu i
płytki, wykrywanie źródeł projektu, pliki generowane, reguły przypisywania cache
oraz integrację buildu i wgrywania firmware'u.

Akcje CLI i zabezpieczenia urządzenia opisano w dokumencie
[Punkt wejścia JaszczurHAL dla VS Code](../../vscode/README.pl.md). Pola
deskryptora i generowane metadane przedstawiono w
[Profilach targetów i płytek](boards_profiles_howto.md), a ścieżki aktualizacji
sieciowej właściwe dla targetu - w dokumencie
[Natywna aktualizacja OTA](OTAWorkflow.md).

## Układ projektu

Projekt firmware zazwyczaj zawiera:

```text
my-device/
  app.c or app.cpp
  hal_project_config.h
  .vscode/
    jaszczurhal.project.json
    settings.json
    tasks.json
    launch.json
    keybindings.reference.json
    extensions.json
```

Plik `extensions.json` zawiera listę zalecanych rozszerzeń, od których zależą
pliki projektu:
`ms-vscode.cpptools` do IntelliSense, `ms-vscode.cmake-tools` jako
`C_Cpp.default.configurationProvider` ustawiony w `settings.json`,
`marus25.cortex-debug` do konfiguracji debugowania w `launch.json` oraz
`ms-vscode.vscode-serial-monitor` używane razem z akcjami monitora
`jh-vscode`.
VS Code proponuje instalację brakujących pozycji przy otwarciu folderu.

Wygenerowane pliki `launch.json` wskazują konkretne skrypty interfejsu i
targetu OpenOCD. W Windows uruchom
`jh-vscode debug-tools --project <path> --json`, a następnie ustaw wskazany
plik wykonywalny OpenOCD oraz katalog toolchainu Arm w ustawieniach użytkownika
Cortex-Debug. Rozszerzenie odnajdzie GDB w tym katalogu. Te ścieżki, właściwe
dla danego komputera, nie trafiają do śledzonych plików projektu.

Na hostach Linux typu Debian/Ubuntu `runmefirst.sh` instaluje `gdb-multiarch`,
a wygenerowane ustawienia wybierają go przez `cortex-debug.gdbPath.linux`.
Profil STM32G474 używa `board/st_nucleo_g4.cfg` z opcją connect-under-reset.
Dzięki temu wbudowany ST-Link może przejąć kontrolę nad działającym targetem,
zanim połączy się GDB.

Projekty RP i STM32 wybierają `toolchain: "cmake"` i wskazują
`cmake.sourceDir` na `libraries/JaszczurHAL/cmake/jh_firmware_project`.
`JH_PROJECT_DIR` wskazuje katalog aplikacji. Projekty ESP32 i ESP32-S3
wybierają `toolchain: "esp-idf"`; wpis targetu w rejestrze wskazuje produkcyjny
skrypt obsługi oraz ścieżkę manifestu artefaktów. Wspólny punkt wejścia wybiera
właściwy system budowania, dzięki czemu projekt nie potrzebuje własnej
konfiguracji CMake.

Wygeneruj działający samodzielny projekt poleceniem:

```bash
libraries/JaszczurHAL/vscode/tools/create-vscode-example.py \
  --output my-device --target rp2040 --board pico
```

Wygenerowany `tasks.json` umożliwia wybór płytki z GUI lub terminala, wgrywanie
i wykrywanie OTA oraz uruchomienie `Project: Sync board picker`. Zadanie
synchronizacji uruchamia się przy `folderOpen`, odczytuje bieżący rejestr
płytek JaszczurHAL i aktualizuje śledzone opcje GUI tylko wtedy, gdy się
zmieniły. Tworzy też lub naprawia wygenerowane profile debuggera RP2040,
RP2350 ARM oraz STM32G474/ST-Link w `launch.json`. Korzysta przy tym z
artefaktu ELF wskazanego w manifeście i zachowuje konfiguracje dodane przez
użytkownika. VS Code wymaga oznaczenia obszaru roboczego jako zaufanego i
może poprosić o jednorazową zgodę na automatyczne uruchamianie zadań.

Zadanie terminala `Project: Select board` zawsze odczytuje rejestr w chwili
wywołania. Każde wygenerowane zadanie używa `jaszczurhal.vscodeEntry` w
systemach Unix oraz ustawienia `jaszczurhal.vscodeEntryWindows` zastępującego
je w Windows. Oba ustawienia wybierają znajdujące się obok skrypty
`jh-vscode` i `jh-vscode.cmd`, które korzystają z tego samego runtime Pythona.

Sprawdź wszystkie artefakty przechowywane w repozytorium lub wygeneruj je
ponownie. Dotyczy to także wspólnych fragmentów konfiguracji i projektów
przykładowych:

```bash
python3 scripts/sync_generated.py --check
python3 scripts/sync_generated.py --write
```

Zalecane rozszerzenia można sprawdzić bez zmiany profilu VS Code:

```bash
python3 vscode/tools/manage_vscode_extensions.py
```

Przekazanie `--install` wymaga potwierdzenia przed zainstalowaniem
brakujących pozycji. Automatyzacja może użyć `--install --yes` po uzyskaniu
zgody.

## Podstawowe pojęcia

- **Katalog projektu**: ścieżka przekazywana do `--project`, zwykle
  przechowywana jako `JH_PROJECT_DIR`.
- **Manifest**: śledzony `.vscode/jaszczurhal.project.json`.
- **Stan lokalny**: ignorowany przez git `.vscode/jaszczurhal.local.json`,
  zawierający wybrany przez dewelopera target, płytkę i port szeregowy.
- **Target**: stabilny identyfikator buildu: `rp2040`, `rp2350-arm`,
  `rp2350-riscv`, `stm32g474`, `esp32`, `esp32s3` lub `mock`.
- **Płytka (board)**: stabilny identyfikator fizycznego profilu, taki jak
  `pico`, `picow`, `pico2`, `pico2w`, `pico-rm2`, `rp2040-zero`,
  `rp2040-plus-4mb`, `nucleo-g474re`, `esp32-devkitc-v4` lub
  `waveshare-esp32-s3-zero`.
- **Rejestr płytek**: wygenerowany zestaw danych dla narzędzi:
  `boards/targets/*.json`,
  `boards/profiles/*.json` oraz `boards/capabilities.json`.
- **`JH_TARGET` / `JH_BOARD`**: wartości w cache CMake wybierane przez wspólną
  konfigurację przed importem SDK/toolchainu.
- **`HAL_TARGET_*`**: selektor backendu HAL używany podczas kompilacji, generowany
  lub ustalany na podstawie wybranego targetu buildu.

<a id="rozwiązywanie-targetu-i-konfiguracji"></a>

## Wybór targetu i konfiguracji

Aktywna para target/płytka jest wybierana w tej kolejności:

1. opcje przekazane przy wywołaniu, takie jak
   `--target rp2040 --board picow`;
2. `.vscode/jaszczurhal.local.json`;
3. śledzony manifest `target` i `board`;
4. domyślna wartość rejestru `rp2040/pico`.

Konfiguracja wynikowa jest następnie scalana od niższego do wyższego
priorytetu:

1. domyślne wartości rejestru targetu i płytki;
2. bazowy manifest;
3. aktywna nakładka `targetProfiles.<target>`;
4. ustalone wartości `JH_TARGET` i `JH_BOARD`;
5. opcje właściwe dla danej akcji, takie jak `--port`, `--host`, `--verbose` i
   `--allow-unverified-port`.

`.vscode/settings.json` zawiera lokalne ścieżki edytora i ustawienia
wyświetlania. Manifest określa stabilną tożsamość projektu, cache buildu,
układ źródeł, profile targetu, artefakty i ustawienia OTA. Samodzielne projekty
wygenerowane przez `create-vscode-example.py` dodatkowo kopiują początkowy
cache wspólnej konfiguracji do `cmake.configureSettings`. Dzięki temu VS Code
CMake Tools może skonfigurować projekt dla wielu targetów bez pośrednictwa
`jh-vscode`.

Przed diagnozowaniem buildu lub wgrywania sprawdź pełny widok konfiguracji po
rozstrzygnięciu wszystkich wartości:

```bash
../libraries/JaszczurHAL/vscode/entry/jh-vscode \
  config-dump --project "$PWD"
```

Wynik zawiera obiekt `featureResolution` z `registryDigest`,
`requestedFeatures`, `resolvedFeatures`, `resolvedFeaturesDigest` oraz
informację `provenance` o pochodzeniu każdego żądanego wpisu. Ten widok
odzwierciedla aktywny profil targetu i wariant po zastosowaniu wszystkich
nakładek manifestu.

## Macierz targetów

| Target | ISA | Domyślna płytka | Format firmware'u | Wgrywanie |
|---|---|---|---|---|
| `rp2040` | Cortex-M0+ | `pico` | ELF/BIN/HEX/UF2/MAP | weryfikacja tożsamości przez CDC, następnie BOOTSEL; albo bezpośredni BOOTSEL |
| `rp2350-arm` | Cortex-M33 | `pico2` | ELF/BIN/HEX/UF2/MAP | weryfikacja tożsamości przez CDC, następnie BOOTSEL; albo bezpośredni BOOTSEL |
| `rp2350-riscv` | Hazard3 RISC-V | `pico2` | ELF/BIN/HEX/UF2/MAP | weryfikacja tożsamości przez CDC, następnie BOOTSEL; albo bezpośredni BOOTSEL |
| `stm32g474` | Cortex-M4F | `nucleo-g474re` | ELF/BIN/HEX/MAP | OpenOCD |
| `esp32` | dwurdzeniowy Xtensa LX6 | `esp32-devkitc-v4` | ELF/MAP plus obrazy BIN bootloadera, tabeli partycji i aplikacji | flashowanie ESP-IDF przez zweryfikowany mostek USB-UART |
| `esp32s3` | dwurdzeniowy Xtensa LX7 | `waveshare-esp32-s3-zero` | ELF/MAP plus obrazy BIN bootloadera, tabeli partycji i aplikacji | flashowanie ESP-IDF przez zweryfikowany interfejs USB Serial/JTAG |
| `mock` | host | `host-mock` | plik wykonywalny/biblioteka hosta | brak |

Rejestr płytek sprawdza zgodność z targetem i podaje informacje o platformie z
pola `provider`, parametrach fizycznej pamięci flash i PSRAM, domenie GPIO,
komponentach i możliwościach płytki, tożsamości programatora oraz domyślnych
ustawieniach wgrywania. Nieznana para target/płytka powoduje błąd, zanim
uruchomi się kompilator.

## Minimalny manifest

```json
{
  "project": "my-device",
  "module": "tracker",
  "toolchain": "cmake",
  "target": "rp2040",
  "board": "pico",
  "buildDir": "${project}/.build",
  "cmakeBuildDir": "${buildDir}/cmake",
  "cmake": {
    "sourceDir": "${project}/../libraries/JaszczurHAL/cmake/jh_firmware_project",
    "cache": {
      "JH_PROJECT_DIR": "${project}",
      "JH_MODULE_NAME": "tracker"
    }
  },
  "identity": {
    "enabled": true,
    "usbManufacturer": "Jaszczur",
    "usbProduct": "My Device",
    "byIdHint": "My_Device"
  }
}
```

Do buildu firmware'u używany jest Ninja, chyba że `cmake.generator` jawnie
wybiera inny generator CMake. Runtime zawsze generuje bazę poleceń kompilacji i
przekazuje CMake używany interpreter Pythona. W natywnym środowisku Windows drzewo
robocze CMake znajduje się pod krótką ścieżką zapisaną przez
`runmefirst.ps1`; `buildDir` z manifestu pozostaje stałą lokalizacją artefaktów
wynikowych i pliku `compile_commands_patched.json`.

Trzymaj wspólne wartości w bazowym manifeście, a zmiany dotyczące
poszczególnych targetów wprowadzaj w niewielkich nakładkach:

```json
{
  "targetProfiles": {
    "stm32g474": {
      "board": "nucleo-g474re",
      "cmake": {
        "cache": {
          "JH_EXTRA_DEFINES": "APP_STM32_BUILD=1"
        }
      }
    }
  }
}
```

Wartości ustalone na podstawie rejestru są zawsze zapisywane jako ostateczne
`JH_TARGET` i `JH_BOARD`.

Projekt ESP32-S3 używa uproszczonej postaci manifestu właściwej dla tego
systemu budowania:

```json
{
  "project": "my-device",
  "module": "tracker",
  "toolchain": "esp-idf",
  "target": "esp32s3",
  "board": "waveshare-esp32-s3-zero",
  "buildDir": "${project}/.build/esp32s3"
}
```

Rejestr targetu i płytki uzupełnia konfigurację o skrypt obsługi, manifest
artefaktów, strategię wgrywania, wymaganą funkcję FreeRTOS oraz dokładny
identyfikator programatora `303a:1001`. Nie kopiuj tych informacji do manifestu
projektu.

## Dodawanie plików źródłowych projektu

Wspólny projekt CMake automatycznie wykrywa `*.c`, `*.cpp`, `*.h` i
`*.hpp` bezpośrednio w `JH_PROJECT_DIR`.

```text
tracker/
  app.cpp
  hal_project_config.h
  gps_filter.c
  gps_filter.h
```

Projekty z podkatalogami źródłowymi deklarują kompletną listę:

```json
{
  "cmake": {
    "cache": {
      "JH_PROJECT_SOURCES": "app.cpp;hal_project_config.h;filters/gps.c;filters/gps.h"
    }
  }
}
```

`JH_PROJECT_SOURCES` to lista rozdzielona średnikami, względna wobec
`JH_PROJECT_DIR`. Zastępuje ona wykrywanie w katalogu głównym.

Dodatkowe wspólne pliki można dołączyć przez `JH_EXTRA_SOURCES`:

```json
{
  "cmake": {
    "cache": {
      "JH_EXTRA_SOURCES": "../common/product_identity.cpp"
    }
  }
}
```

Mechanizm wspólnego projektu CMake normalizuje wyznaczone ścieżki i usuwa ich
duplikaty.

Skrypt obsługi ESP-IDF wykrywa źródła C, C++ i asemblera bezpośrednio w
katalogu projektu oraz rekurencyjnie w `src/`. Przy jego bezpośrednim wywołaniu
automatyczne wykrywanie można zastąpić wielokrotnie przekazywanym argumentem
`--source <relative-path>`. Wszystkie ścieżki źródłowe muszą pozostać wewnątrz
projektu.

## Konfiguracja funkcji i runtime

Flagi funkcji projektu znajdują się w `hal_project_config.h`:

```c
#pragma once

#define HAL_ENABLE_WIFI
#define HAL_ENABLE_MQTT
#define HAL_ENABLE_APP_TASK1
```

`JH_EXTRA_DEFINES` jest przydatne dla profili targetu, wariantów buildu i
CI:

```json
{
  "cmake": {
    "cache": {
      "JH_EXTRA_DEFINES": "HAL_ENABLE_FREERTOS;APP_DIAGNOSTICS=1"
    }
  }
}
```

Funkcje można włączać za pomocą zapisu `HAL_ENABLE_X` lub
`HAL_ENABLE_X=1`. Po wybraniu aktywnego profilu targetu i wariantu przykładu
wspólny mechanizm konfiguracji oraz `jh-vscode` odrzucają `HAL_ENABLE_X=0` i
inne jawnie podane wartości, zgłaszając `[JH-CFG-VALUE]`. Aby wyłączyć funkcję,
pomiń jej symbol.
Zwykłe parametry, takie jak `APP_DIAGNOSTICS=0`, zachowują standardową
semantykę wartości. W listach definicji każdy wpis `HAL_ENABLE_*` musi być
osobnym, prostym tokenem, a kolejne wpisy trzeba rozdzielać średnikami. Białe
znaki nie rozdzielają definicji funkcji, a wyrażenia generatora CMake są
odrzucane.

Deskryptor `esp32s3` obsługuje wymagane przez target
`HAL_ENABLE_FREERTOS`, zaimplementowane w fazie 2 flagi peryferiów oraz flagi
sieci i usług z fazy 3. Zestaw obejmuje APP_TASK1, UART, tryby I2C controller
i target, SPI, PWM_FREQ, RGB_LED, PCNT, STACK_GUARD, BLE, WiFi, TCP/UDP,
gniazda BSD, TLS, klienta i serwer HTTP, obsługę plików HTTP, serwer WebSocket,
MQTT, czas, OTA oraz WireGuard. Prosty PWM oraz źródła systemu,
synchronizacji, GPIO, ADC, portu szeregowego i timera są zawsze częścią bazowej
konfiguracji targetu. Jeśli żądana funkcja lub którakolwiek z jej zależności
nie znajduje się na liście dozwolonej przez deskryptor, produkcyjne narzędzie
odrzuca konfigurację i zgłasza `[JH-CFG-UNSUPPORTED]`.

Początkowy deskryptor `esp32` jest celowo węższy. Obsługuje wymagany runtime
FreeRTOS i `HAL_ENABLE_BLUETOOTH_GAMEPAD`, które wybiera Bluedroid, BR/EDR oraz
ESP HID Host. Funkcje dostępne tylko na ESP32-S3, w tym publiczne API BLE,
są odrzucane podczas kontroli wstępnej.

Dla pliku `firmware_entry.h` zgodnego z konwencją Fiesta ustawieniu
`FIESTA_ENABLE_CORE1=1` musi towarzyszyć `HAL_ENABLE_APP_TASK1` w
`hal_project_config.h` lub w innym standardowym źródle konfiguracji funkcji.
Dzięki temu wygenerowany adapter punktu wejścia, zestaw funkcji wskazanych
bezpośrednio, zestaw po rozwiązaniu zależności oraz sygnatura linkowania są
identyczne.

Rejestr funkcji wyznacza jedno, niezależne od targetu domknięcie przechodnie dla
wszystkich produkcyjnych mechanizmów korzystających z konfiguracji.
Wygenerowany nagłówek C definiuje makra funkcji wynikające z zależności, a CMake
używa wyznaczonego zestawu do wyboru źródeł i zależności. Generator konfiguracji
płytki używa go do obliczenia `featureHash` i sygnatury linkowania. `jh-vscode` korzysta z tego
samego domknięcia podczas kontroli wstępnej i sprawdzania dostępności OTA,
zachowując bezpośrednie żądania przekazane do CMake. Zdefiniuj
`HAL_CONFIG_VERBOSE`, aby podczas buildu wygenerować raport zawierający
wszystkie aktywne, zarejestrowane funkcje.

Reguły, których wyniki zależą od parametrów strojenia, wyboru systemu budowania,
możliwości płytki lub aktywnego targetu, pozostają w `hal_config.h`. Obejmują
one automatyczne włączenie I2C wynikające z wybranego typu EEPROM, domyślny
transport GPS, sprawdzanie zgodności backendu z systemem budowania i możliwości
płytki oraz ograniczenia właściwe dla targetu.

Build ładuje `hal_project_config.h` przed automatycznym wykrywaniem targetu i
utworzeniem pochodnych selektorów targetu i płytki. W pliku umieszczaj
wyłącznie makra. Może on bezpośrednio definiować `HAL_TARGET_*`,
`HAL_BOARD_PROFILE_*`, `HAL_ENABLE_*` i makra strojenia, ale nie może
dołączać nagłówków JaszczurHAL ani rozgałęziać się na `HAL_TARGET_IS_*` /
`HAL_BOARD_IS_*`. Definicje funkcji używane do wyboru źródeł muszą mieć
bezwarunkową postać `#define HAL_ENABLE_X` lub `#define HAL_ENABLE_X 1`.
Jedynym obsługiwanym wyjątkiem jest warunek `#ifndef HAL_ENABLE_X` odnoszący
się do tego samego symbolu. Nie umieszczaj definicji funkcji pod żadnym innym
`#if`/`#ifdef`, w tym pod bezpośrednimi lub pochodnymi warunkami targetu i
płytki, ponieważ na wczesnym etapie plik jest analizowany jako tekst.

Fizyczny wybór płytki pozostaje w polach `target` i `board`. Projekt określa
natomiast okablowanie aplikacji, tożsamość USB, sekrety, politykę partycji i
wybór funkcji.

<a id="katalogi-budowania-i-pliki-generowane"></a>

## Katalogi buildu i pliki generowane

Zewnętrzne projekty firmware używają `${project}/.build`. Przykłady
przechowywane w repozytorium i sprzętowe stanowiska testowe zapisują artefakty
w stałych lokalizacjach względem katalogu głównego JaszczurHAL:

```text
.build/examples/<example>/
.build/hardware/<fixture>/
```

W systemach Unix cache CMake każdego projektu jest izolowany według targetu i
płytki w katalogu `cmakeBuildDir` wskazanym w manifeście:

```text
<cmakeBuildDir>/<target>/<board>/
```

Zapobiega to współdzieleniu jednego cache przez różne toolchainy, platformy
systemów budowania, wygenerowane nagłówki płytki i układy linkera. Natywne środowisko
Windows używa zamiast tego krótkiej ścieżki przygotowanej przez skrypt
inicjalizacyjny:

```text
<BuildRoot>/<project-name>-<path-hash>/cmake/<target>/<board>/
```

Pierwotny `compile_commands.json` znajduje się w odpowiednim drzewie CMake.
Narzędzie zapisuje dostosowany `compile_commands_patched.json` w stabilnym
`buildDir` z manifestu i po każdym buildzie odświeża artefakty firmware'u
wybranego targetu. Dotyczy to również sytuacji, gdy po przełączeniu między
wcześniej skonfigurowanymi targetami Ninja nie ma nic do przebudowania.

`jh-vscode` zapisuje w `.jh-vscode-cache-keys.json` klucze cache zarządzane
przez manifest. Jeśli klucz zniknie z manifestu, przy następnej konfiguracji
jest usuwany z cache. Gdy zmienia się żądany katalog źródłowy CMake,
nieaktualny cache znajdujący się w zarządzanym katalogu artefaktów jest
tworzony od nowa.

Projekty ESP-IDF używają `buildDir` bezpośrednio jako drzewa buildu IDF;
musi ono znajdować się w katalogu `.build` projektu albo repozytorium
JaszczurHAL lub w jego podkatalogu. Produkcyjne narzędzie zarządza wygenerowaną
konfiguracją projektu i SDK wewnątrz tego drzewa i nigdy nie zapisuje drugiego
rejestru płytek.

Generowane pliki obejmują:

- pliki CMake, nagłówki i JSON wygenerowane dla płytki oraz jednostki
  translacji z sygnaturą linkowania;
- pierwotny `compile_commands.json` w drzewie CMake oraz stabilny
  `compile_commands_patched.json` w `buildDir`;
- `.vscode/c_cpp_properties.json`;
- artefakty targetu ELF/BIN/HEX/UF2/MAP lub ELF/BIN/HEX/MAP;
- dla ESP-IDF: `jh_esp_idf_artifacts.json`, ELF/MAP/BIN aplikacji, obrazy
  bootloadera i tabeli partycji, `sdkconfig`, log buildu, wygenerowane
  metadane płytki i linkowania, informację o źródle toolchainu oraz pierwotną
  bazę poleceń kompilacji;
- kontener OTA i scalony plik UF2 do odzyskiwania, gdy OTA jest włączone.

Konfigurację śledzoną w Git nadal przechowuj w manifeście i
`hal_project_config.h`.

## Akcje buildu i wgrywania

```bash
../libraries/JaszczurHAL/vscode/entry/jh-vscode build --project "$PWD"
../libraries/JaszczurHAL/vscode/entry/jh-vscode upload --project "$PWD"
../libraries/JaszczurHAL/vscode/entry/jh-vscode monitor --project "$PWD"
../libraries/JaszczurHAL/vscode/entry/jh-vscode clean --project "$PWD"
```

`Project: Upload` wybiera strategię wgrywania z rejestru. Gdy na płytce RP
działa firmware, narzędzie najpierw weryfikuje tożsamość urządzenia przez USB
CDC, a następnie przechodzi do BOOTSEL i wgrywa UF2. W przypadku
niezaprogramowanej płytki
użyj `Project: Upload (UF2 / BOOTSEL)`. Dla STM32G474 operacja jest
przekazywana do targetu OpenOCD odpowiedzialnego za wgrywanie. Dla ESP32-S3
wykonywany jest sprawdzony build produkcyjny; narzędzie sprawdza każdą ścieżkę
w manifeście wieloobrazowym i przekazuje zweryfikowany port szeregowy do akcji
flashowania ESP-IDF. Profil tej płytki podaje USB VID/PID `303a:1001`. Brak
urządzenia, nieaktualna ścieżka, niezgodna tożsamość albo kilka pasujących
urządzeń powodują bezpieczne przerwanie operacji. Opcja
`--allow-unverified-port`
jawnie omija tę kontrolę i musi występować razem z `--port`.

Wgrywanie zwalnia port zajęty przez stale działający monitor szeregowy projektu
i pozwala mu połączyć się ponownie, gdy urządzenie znów zgłosi interfejs USB.
Jeśli nie można jednoznacznie wybrać woluminu BOOTSEL lub urządzenia na
podstawie jego tożsamości szeregowej, operacja zostaje przerwana.

Dla ESP32-S3 zadanie `Project: Serial Monitor` wybiera jedyną płytkę pasującą
do identyfikatora programatora z rejestru, jeśli nie wskazano jawnie portu.
`Project: Refresh IntelliSense` korzysta z poleceń kompilacji Xtensa wygenerowanych
przez ESP-IDF i nie zastępuje ich trybem IntelliSense dla Arm. `build-debug`
oraz zarządzane profile Cortex-Debug nie są dostarczane dla ESP32-S3.

## Konfiguracja manifestu OTA

Dla projektów CMake RP manifest publikuje wygenerowany kontener i jego
metadane buildu wraz ze wspólnymi ustawieniami punktu końcowego OTA:

```json
{
  "cmake": {
    "cache": {
      "JH_EXTRA_DEFINES": "HAL_ENABLE_OTA",
      "JH_OTA_GENERATION": 7,
      "JH_OTA_VERSION": "1.4.0"
    }
  },
  "artifacts": {
    "ota": "${buildDir}/firmware.ota"
  },
  "ota": {
    "hostname": "tracker-office",
    "port": 8266,
    "listenPort": 8266,
    "passwordEnv": "TRACKER_OTA_PASSWORD"
  }
}
```

Projekty ESP-IDF pomijają właściwe wyłącznie dla RP wpisy `cmake` i
`artifacts.ota`. Ich manifest buildu produkcyjnego wskazuje binarny obraz
aplikacji. Powyższy obiekt `ota` pozostaje wspólną konfiguracją punktu
końcowego hosta i uwierzytelniania.

`ota.broadcast` określa adres docelowy wyszukiwania przez UDP, a `ota.host`
ustawia stały adres urządzenia. `ota.listenPort` wybiera port, na którym host
nasłuchuje połączenia zwrotnego TCP. Domyślna wartość `8266` odpowiada trwałej
regule zapory ograniczonej do sieci LAN, przygotowanej przez `runmefirst.sh`.
Wartość `0` wybiera port efemeryczny. `ota.passwordEnv` przechowuje sekret
hosta poza śledzonym manifestem.

Nazwa hosta urządzenia, port UDP i hasło muszą odpowiadać konfiguracji
firmware'u. Dokument [Natywna aktualizacja OTA](OTAWorkflow.md) opisuje artefakty
właściwe dla targetu, początkowe przygotowanie urządzenia, zadania,
uwierzytelnianie, reguły zapory hosta, potwierdzanie rozruchu próbnego,
automatyczne przywracanie poprzedniej wersji i odzyskiwanie. Podczas wgrywania
na RP narzędzie podpisuje wygenerowany kontener JaszczurHAL. W przypadku
ESP-IDF sprawdzany jest produkcyjny manifest
artefaktów, po czym wskazany w nim binarny obraz aplikacji zostaje przesłany
bez konwertowania do formatu kontenera RP.

## Przykłady i warianty

Manifesty przykładów mogą deklarować `example.targets` i
`example.variants`. Warianty mogą nadpisywać nazwę modułu, źródła, definicje
funkcji, obsługiwane targety oraz wpisy cache CMake.

```bash
scripts/examples_dispatcher.py list
scripts/examples_dispatcher.py build --target rp2040 --example 01_core_runtime
```

Wygenerowane manifesty przykładów stanowią dane wejściowe buildu, z których
korzysta bramka jakości. Zobacz
[Przykłady JaszczurHAL](../../examples/README.pl.md).
