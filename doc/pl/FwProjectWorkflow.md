# Workflow projektu firmware

*Dostępne również [po angielsku](../en/FwProjectWorkflow.md).*

Ten dokument definiuje model projektu firmware oparty na dispatcherze, używany
przez projekty JaszczurHAL i śledzone przykłady. Obejmuje śledzony manifest,
rozwiązywanie targetu i płytki, wykrywanie źródeł projektu, pliki generowane,
własność cache oraz integrację build/upload.

Akcje CLI i zabezpieczenia urządzenia opisuje
[JaszczurHAL VS Code Entry](../../vscode/README.md), pola deskryptora i
generowane metadane - [Profile targetów i płytek](boards_profiles_howto.md), a
ścieżki aktualizacji sieciowej właściwe dla targetu -
[Natywny workflow OTA](OTAWorkflow.md).

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

`extensions.json` rekomenduje rozszerzenia, od których zależą pliki projektu:
`ms-vscode.cpptools` po IntelliSense, `ms-vscode.cmake-tools` jako
`C_Cpp.default.configurationProvider` ustawiony w `settings.json`,
`marus25.cortex-debug` po konfigurację debugowania w `launch.json` oraz
`ms-vscode.vscode-serial-monitor` obok akcji monitora `jh-vscode`.
VS Code proponuje instalację brakujących pozycji przy otwarciu folderu.

Wygenerowane pliki `launch.json` dostarczają jawne skrypty interfejsu i
targetu OpenOCD. Na Windows uruchom `jh-vscode debug-tools
--project <path> --json` i ustaw zgłoszony plik wykonywalny OpenOCD oraz
katalog toolchainu Arm w ustawieniach użytkownika Cortex-Debug; rozszerzenie
rozwiązuje GDB z tego katalogu. Te lokalne dla maszyny ścieżki pozostają poza
śledzonymi plikami projektu.

Na hostach Linux typu Debian/Ubuntu `runmefirst.sh` instaluje `gdb-multiarch`,
a wygenerowane ustawienia wybierają go przez `cortex-debug.gdbPath.linux`.
Profil STM32G474 używa `board/st_nucleo_g4.cfg` z connect-under-reset, dzięki
czemu wbudowany ST-Link może odzyskać działający target, zanim GDB się
podłączy.

Projekty RP i STM32 wybierają `toolchain: "cmake"` i wskazują
`cmake.sourceDir` na `libraries/JaszczurHAL/cmake/jh_firmware_project`.
`JH_PROJECT_DIR` identyfikuje katalog aplikacji. Projekty ESP32 i ESP32-S3
wybierają `toolchain: "esp-idf"`; ich wpis w rejestrze targetów dostarcza produkcyjny
runner oraz ścieżkę manifestu artefaktów. Współdzielony punkt wejścia wybiera
providera bez konieczności lokalnej receptury CMake dla projektu.

Wygeneruj działający samodzielny projekt poleceniem:

```bash
libraries/JaszczurHAL/vscode/tools/create-vscode-example.py \
  --output my-device --target rp2040 --board pico
```

Wygenerowany `tasks.json` zawiera wybór płytki w GUI i terminalu, wgrywanie i
wykrywanie OTA oraz `Project: Sync board picker`. Zadanie synchronizacji
uruchamia się przy `folderOpen`, odczytuje bieżący rejestr płytek JaszczurHAL
i aktualizuje śledzone opcje GUI tylko wtedy, gdy się zmieniły. To samo
zadanie tworzy lub naprawia wygenerowane profile debuggera RP2040, RP2350 ARM
oraz STM32G474/ST-Link w `launch.json`, używając artefaktu ELF z manifestu i
zachowując konfiguracje należące do projektu użytkownika. VS Code wymaga zaufanego
workspace'u i może poprosić o jednorazową zgodę na zadania automatyczne.
Zadanie terminala `Project: Select board` zawsze odczytuje rejestr w chwili
wywołania.
Każde wygenerowane zadanie używa `jaszczurhal.vscodeEntry` na Unix oraz
nadpisania platformy `jaszczurhal.vscodeEntryWindows` na Windows. Oba
ustawienia wybierają sąsiadujące launchery `jh-vscode` i `jh-vscode.cmd`,
które wykonują jeden współdzielony runtime Pythona.

Sprawdź lub wygeneruj ponownie każdy śledzony artefakt repozytorium, w tym
współdzielone snippety i projekty przykładowe przechowywane w repozytorium:

```bash
python3 scripts/sync_generated.py --check
python3 scripts/sync_generated.py --write
```

Rekomendowane rozszerzenia można sprawdzić bez zmiany profilu VS Code:

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
- **Rejestr płytek**: wygenerowany widok narzędziowy `boards/targets/*.json`,
  `boards/profiles/*.json` oraz `boards/capabilities.json`.
- **`JH_TARGET` / `JH_BOARD`**: wartości cache CMake wybierane przez
  dispatcher przed importem SDK/toolchainu.
- **`HAL_TARGET_*`**: selektor backendu HAL podczas buildu, generowany
  lub wnioskowany z rozwiązanego targetu buildu.

## Rozwiązywanie targetu i konfiguracji

Aktywna para target/płytka jest wybierana w tej kolejności:

1. nadpisania wywołania, takie jak `--target rp2040 --board picow`;
2. `.vscode/jaszczurhal.local.json`;
3. śledzony manifest `target` i `board`;
4. domyślna wartość rejestru `rp2040/pico`.

Efektywna konfiguracja jest następnie scalana od niższego do wyższego
priorytetu:

1. domyślne wartości rejestru targetu i płytki;
2. bazowy manifest;
3. aktywna nakładka `targetProfiles.<target>`;
4. rozwiązane `JH_TARGET` i `JH_BOARD`;
5. opcje specyficzne dla akcji, takie jak `--port`, `--host`, `--verbose` i
   `--allow-unverified-port`.

`.vscode/settings.json` dostarcza ścieżki lokalne dla edytora i preferencje
wyświetlania. Stabilna tożsamość, cache buildu, układ źródeł, profile
targetu, artefakty i ustawienia OTA należą do manifestu. Samodzielne projekty
wygenerowane przez `create-vscode-example.py` dodatkowo kopiują początkowy
cache dispatchera do `cmake.configureSettings`, dzięki czemu VS Code CMake
Tools może skonfigurować współdzielony dispatcher bez przechodzenia przez
`jh-vscode`.

Sprawdź kompletny rozwiązany widok przed diagnozowaniem buildu lub
wgrywania:

```bash
../libraries/JaszczurHAL/vscode/entry/jh-vscode \
  config-dump --project "$PWD"
```

Zrzut zawiera obiekt `featureResolution` z `registryDigest`,
`requestedFeatures`, `resolvedFeatures`, `resolvedFeaturesDigest` oraz
`provenance` dla każdego żądania. Ten widok odzwierciedla aktywny profil
targetu i wariant po zastosowaniu wszystkich nakładek manifestu.

## Macierz targetów

| Target | ISA | Domyślna płytka | Format firmware'u | Wgrywanie |
|---|---|---|---|---|
| `rp2040` | Cortex-M0+ | `pico` | ELF/BIN/HEX/UF2/MAP | zweryfikowane CDC do BOOTSEL lub bezpośredni BOOTSEL |
| `rp2350-arm` | Cortex-M33 | `pico2` | ELF/BIN/HEX/UF2/MAP | zweryfikowane CDC do BOOTSEL lub bezpośredni BOOTSEL |
| `rp2350-riscv` | Hazard3 RISC-V | `pico2` | ELF/BIN/HEX/UF2/MAP | zweryfikowane CDC do BOOTSEL lub bezpośredni BOOTSEL |
| `stm32g474` | Cortex-M4F | `nucleo-g474re` | ELF/BIN/HEX/MAP | OpenOCD |
| `esp32` | dwurdzeniowy Xtensa LX6 | `esp32-devkitc-v4` | ELF/MAP plus obrazy BIN bootloadera, tabeli partycji i aplikacji | flashowanie ESP-IDF przez zweryfikowany mostek USB-UART |
| `esp32s3` | dwurdzeniowy Xtensa LX7 | `waveshare-esp32-s3-zero` | ELF/MAP plus obrazy BIN bootloadera, tabeli partycji i aplikacji | flashowanie ESP-IDF przez zweryfikowany USB Serial/JTAG |
| `mock` | host | `host-mock` | plik wykonywalny/biblioteka hosta | brak |

Rejestr płytek waliduje zgodność targetu, dostarczając informacje o
providera platformy, fizyczne fakty o flash/PSRAM, domenę GPIO, komponenty
płytki, możliwości, tożsamość programatora oraz domyślne ustawienia
wgrywania. Nieznane pary target/płytka kończą się błędem, zanim uruchomi się
kompilator.

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

Build firmware używa Ninja, chyba że `cmake.generator` jawnie wybiera
inny generator CMake. Runtime zawsze włącza bazę danych
buildu i przekazuje swój bieżący interpreter Pythona do CMake. Natywny
Windows utrzymuje drzewo robocze CMake poniżej krótkiego katalogu głównego
zapisanego przez `runmefirst.ps1`; `buildDir` z manifestu pozostaje stabilną
lokalizacją dla finalnych artefaktów i `compile_commands_patched.json`.

Trzymaj wspólne wartości w bazowym manifeście, a zmiany specyficzne dla
targetu wyrażaj jako małe nakładki:

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

Rozwiązane wartości rejestru zawsze przypinają finalne `JH_TARGET` i
`JH_BOARD`.

Projekt ESP32-S3 używa mniejszej, specyficznej dla providera postaci
manifestu:

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

Rejestr targetu/płytki dodaje runner, manifest artefaktów, strategię
wgrywania, wymaganą funkcję FreeRTOS oraz dokładną tożsamość programatora
`303a:1001`. Nie kopiuj tych faktów do manifestu projektu.

## Dodawanie plików źródłowych projektu

Współdzielony projekt CMake automatycznie wykrywa `*.c`, `*.cpp`, `*.h` i
`*.hpp` bezpośrednio pod `JH_PROJECT_DIR`.

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

Dodatkowe pliki współdzielone można dołączyć przez `JH_EXTRA_SOURCES`:

```json
{
  "cmake": {
    "cache": {
      "JH_EXTRA_SOURCES": "../common/product_identity.cpp"
    }
  }
}
```

Dispatcher normalizuje i usuwa duplikaty z rozwiązanych ścieżek.

Runner ESP-IDF wykrywa źródła C, C++ i asemblera bezpośrednio pod katalogiem
projektu oraz rekurencyjnie pod `src/`. Bezpośrednie wywołania runnera mogą
zastąpić wykrywanie powtarzalnymi argumentami `--source <relative-path>`.
Wszystkie ścieżki źródłowe muszą pozostać wewnątrz projektu.

## Konfiguracja funkcji i runtime

Flagi funkcji należące do projektu żyją w `hal_project_config.h`:

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

Żądania funkcji akceptują `HAL_ENABLE_X` i `HAL_ENABLE_X=1`. Dispatcher i
`jh-vscode` odrzucają `HAL_ENABLE_X=0` oraz inne jawne wartości z
`[JH-CFG-VALUE]` po rozwiązaniu aktywnego profilu targetu i wariantu
przykładu. Pomiń symbol funkcji, aby ją wyłączyć. Wartości nie będące
funkcjami, takie jak `APP_DIAGNOSTICS=0`, zachowują swoją normalną semantykę
wartości. W wejściach listy definicji każda pozycja `HAL_ENABLE_*` musi być
samodzielnym, prostym tokenem rozdzielonym średnikami. Białe znaki nie
rozdzielają wielu definicji funkcji, a wyrażenia generatora CMake są
odrzucane.

Deskryptor `esp32s3` wspiera wymagany przez target `HAL_ENABLE_FREERTOS`,
dostarczony zestaw flag peryferiów Fazy 2 oraz flagi sieci/usług Fazy 3. Ten
zestaw obejmuje APP_TASK1, UART, kontroler/target I2C, SPI, PWM_FREQ,
RGB_LED, PCNT, STACK_GUARD, BLE, WiFi, TCP/UDP, gniazda BSD, TLS, klienta/serwer
HTTP/pliki, serwer WebSocket, MQTT, czas, OTA oraz WireGuard. Proste PWM oraz
podstawowe źródła systemu/synchronizacji/GPIO/ADC/portu szeregowego/timera
należą do jego komponentu bazowego. Produkcyjny runner odrzuca żądaną
funkcję lub dowolną zależność, która rozwiązuje się poza dozwoloną listą
deskryptora, z `[JH-CFG-UNSUPPORTED]`.

Początkowy deskryptor `esp32` jest celowo węższy. Obsługuje wymagany runtime
FreeRTOS i `HAL_ENABLE_BLUETOOTH_GAMEPAD`, które wybiera Bluedroid, BR/EDR oraz
ESP HID Host. Funkcje dostarczone tylko na ESP32-S3, w tym publiczne API BLE,
są odrzucane podczas kontroli wstępnej.

Dla `firmware_entry.h` w konwencji Fiesta, `FIESTA_ENABLE_CORE1=1` musi być
sparowane z `HAL_ENABLE_APP_TASK1` w `hal_project_config.h` lub innym
normalnym wejściem funkcji. Zachowuje to identyczny wygenerowany adapter
wejścia, żądane/rozwiązane zestawy funkcji oraz sygnaturę linkowania.

Rejestr funkcji oblicza jedno niezależne od targetu przechodnie domknięcie
dla wszystkich ścieżek produkcyjnych. Wygenerowany nagłówek C definiuje
implikowane makra funkcji, CMake używa rozwiązanego zestawu do wyboru
źródeł i zależności, a generowanie płytki używa go dla `featureHash` i
sygnatury linkowania. `jh-vscode` używa tego samego domknięcia dla
preflightu i kwalifikowalności OTA, zachowując bezpośrednie żądania
przekazane do CMake. Zdefiniuj `HAL_CONFIG_VERBOSE`, aby podczas buildu
wygenerować raport każdej aktywnej zarejestrowanej funkcji.

Reguły, których wyniki zależą od parametrów strojenia, wyboru providera,
możliwości płytki lub aktywnego targetu, pozostają w `hal_config.h`.
Obejmują one implikację I2C przez typ EEPROM, domyślny transport GPS,
walidację backendu/providera, sprawdzenia możliwości płytki oraz ograniczenia
specyficzne dla targetu.

Build ładuje `hal_project_config.h` przed automatycznym wykrywaniem
targetu oraz przed istnieniem pochodnych selektorów targetu/płytki. Trzymaj
ten plik wyłącznie makrowo: może definiować surowe `HAL_TARGET_*`,
`HAL_BOARD_PROFILE_*`, `HAL_ENABLE_*` i makra strojenia, ale nie może
dołączać nagłówków JaszczurHAL ani rozgałęziać się na `HAL_TARGET_IS_*` /
`HAL_BOARD_IS_*`. Definicje funkcji używane do wyboru źródeł muszą być
bezwarunkowym `#define HAL_ENABLE_X` lub `#define HAL_ENABLE_X 1`; jedyną
wspieraną formą warunkową jest strażnik `#ifndef HAL_ENABLE_X` na tym samym
symbolu. Nie umieszczaj definicji funkcji pod żadnym innym `#if`/`#ifdef`,
w tym pod surowymi lub pochodnymi rozgałęzieniami targetu/płytki, ponieważ
wczesny kolektor odczytuje plik tekstowo.

Fizyczny wybór płytki pozostaje w `target` i `board`. Okablowanie aplikacji,
tożsamość USB, sekrety, polityka partycji i wybór funkcji pozostają własnością
projektu.

<a id="katalogi-budowania-i-pliki-generowane"></a>

## Katalogi buildu i pliki generowane

Zewnętrzne projekty firmware używają `${project}/.build`. Przykłady
przechowywane w repozytorium i sprzętowe stanowiska testowe używają katalogu głównego
JaszczurHAL dla stabilnych artefaktów:

```text
.build/examples/<example>/
.build/hardware/<fixture>/
```

Na Unix każdy cache CMake projektu jest izolowany według targetu i płytki
poniżej `cmakeBuildDir` z manifestu:

```text
<cmakeBuildDir>/<target>/<board>/
```

Zapobiega to współdzieleniu jednego cache przez toolchainy, platformy
providera, generowane nagłówki płytki i układy linkera. Natywny Windows
zamiast tego używa krótkiego katalogu głównego bootstrapu:

```text
<BuildRoot>/<project-name>-<path-hash>/cmake/<target>/<board>/
```

Surowy `compile_commands.json` podąża za tym drzewem CMake. Środowisko
runtime zapisuje `compile_commands_patched.json` do stabilnego
`buildDir` z manifestu i odświeża artefakty firmware'u wybranego targetu po
każdym buildzie, w tym no-op Ninja po przełączeniu między wcześniej
skonfigurowanymi targetami.

`jh-vscode` śledzi klucze cache należące do manifestu w
`.jh-vscode-cache-keys.json`. Usunięty klucz jest cofany przy następnej
konfiguracji. Gdy zmienia się żądany katalog źródłowy CMake, nieaktualny
cache znajdujący się wewnątrz zarządzanego katalogu głównego artefaktów jest
odtwarzany.

Projekty ESP-IDF używają `buildDir` bezpośrednio jako drzewa buildu IDF;
musi ono pozostać poniżej katalogu głównego `.build` projektu lub
repozytorium JaszczurHAL. Produkcyjny runner posiada wygenerowaną
konfigurację projektu i konfigurację SDK wewnątrz tego drzewa i nigdy nie
zapisuje drugiego rejestru płytek.

Generowane wyjścia obejmują:

- rozwiązane jednostki translacji CMake/nagłówka/JSON płytki oraz sygnatury
  linkowania;
- surowy `compile_commands.json` w drzewie CMake oraz stabilny
  `compile_commands_patched.json` w `buildDir`;
- `.vscode/c_cpp_properties.json`;
- artefakty targetu ELF/BIN/HEX/UF2/MAP lub ELF/BIN/HEX/MAP;
- dla ESP-IDF: `jh_esp_idf_artifacts.json`, ELF/MAP/BIN aplikacji, obrazy
  bootloadera i tabeli partycji, `sdkconfig`, log buildu, wygenerowane
  metadane płytki/linkowania, pochodzenie toolchainu oraz surową bazę danych
  buildu;
- kontener OTA i połączony (merged) UF2 odzyskiwania, gdy OTA jest włączone.

Śledzona konfiguracja pozostaje w manifeście i `hal_project_config.h`.

## Akcje buildu i wgrywania

```bash
../libraries/JaszczurHAL/vscode/entry/jh-vscode build --project "$PWD"
../libraries/JaszczurHAL/vscode/entry/jh-vscode upload --project "$PWD"
../libraries/JaszczurHAL/vscode/entry/jh-vscode monitor --project "$PWD"
../libraries/JaszczurHAL/vscode/entry/jh-vscode clean --project "$PWD"
```

`Project: Upload` wybiera strategię wgrywania z rejestru. Targety RP używają
zweryfikowanego tożsamością USB CDC, a następnie BOOTSEL/UF2, gdy firmware
działa; pusta płytka używa `Project: Upload (UF2 / BOOTSEL)`. STM32G474
deleguje do targetu wgrywania OpenOCD. ESP32-S3 wykonuje zwalidowany
build produkcyjny, sprawdza każdą ścieżkę w manifeście wieloobrazowym i
przekazuje zweryfikowany port szeregowy do akcji flashowania ESP-IDF. Jego
profil płytki dostarcza USB VID/PID `303a:1001`; zerowa, nieaktualna,
niezgodna lub wiele pasujących urządzeń kończy się niepowodzeniem w trybie
fail-closed. `--allow-unverified-port` to jawna furtka awaryjna i musi być
sparowana z `--port`.

Wgrywanie zwalnia trwały monitor szeregowy projektu i pozwala mu ponownie
połączyć się po enumeracji. Niejednoznaczne wolumeny BOOTSEL lub tożsamości
szeregowe zatrzymują akcję.

Dla ESP32-S3 `Project: Serial Monitor` podąża za jedyną płytką pasującą do
tożsamości programatora z rejestru, gdy żaden jawny port nie jest przypięty.
`Project: Refresh IntelliSense` zużywa polecenia buildu Xtensa emitowane
przez ESP-IDF bez podstawiania trybu IntelliSense dla Arm. `build-debug` oraz
zarządzane profile Cortex-Debug nie są dostarczane dla ESP32-S3.

## Konfiguracja manifestu OTA

Dla projektów CMake RP manifest publikuje wygenerowany kontener i jego
metadane buildu obok współdzielonych ustawień punktu końcowego OTA:

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

Projekty ESP-IDF pomijają specyficzne dla RP wpisy `cmake` i
`artifacts.ota`. Ich manifest buildu produkcyjnego identyfikuje surowy
plik BIN aplikacji; powyższy obiekt `ota` pozostaje współdzieloną
konfiguracją punktu końcowego hosta i uwierzytelniania.

`ota.broadcast` wybiera cel odkrywania UDP. `ota.host` przypina adres
urządzenia. `ota.listenPort` wybiera nasłuchiwacz callbacku TCP hosta;
domyślnie to `8266`, więc odpowiada trwałej regule zapory ograniczonej do
LAN, przygotowanej przez `runmefirst.sh`. Jawne zero prosi o port
efemeryczny. `ota.passwordEnv` trzyma sekret hosta poza śledzonym
manifestem.

Nazwa hosta urządzenia, port UDP i hasło muszą odpowiadać konfiguracji
firmware'u. Zobacz [Natywny workflow OTA](OTAWorkflow.md), aby poznać
specyficzne dla targetu artefakty, provisioning, zadania, uwierzytelnianie,
reguły zapory hosta, potwierdzenie próbne, rollback i odzyskiwanie.
Wgrywanie RP podpisuje wygenerowany kontener JaszczurHAL; wgrywanie ESP-IDF
waliduje produkcyjny manifest artefaktów i przesyła jego surowy plik BIN
aplikacji bez konwertowania go do formatu kontenera RP.

## Przykłady i warianty

Manifesty przykładów mogą deklarować `example.targets` i
`example.variants`. Warianty mogą nadpisywać nazwę modułu, źródła, definicje
funkcji, wspierane targety oraz wpisy cache CMake.

```bash
scripts/examples_dispatcher.py list
scripts/examples_dispatcher.py build --target rp2040 --example 01_core_runtime
```

Wygenerowane manifesty przykładów są wejściami buildu używanymi przez
bramkę jakości. Zobacz [Przykłady JaszczurHAL](../../examples/README.md).
