# Praca z projektem firmware

*Dostępne również [po angielsku](../en/FwProjectWorkflow.md).*

Ten rozdział opisuje tworzenie, konfigurowanie, kompilowanie i wgrywanie projektów firmware korzystających z JaszczurHAL. Te same zasady obowiązują w projektach użytkownika i przykładach z repozytorium. Omówiono manifest przechowywany w Git, wybór platformy i płytki, dodawanie źródeł, pliki generowane oraz rozdzielenie pamięci podręcznej CMake między konfiguracjami.

Opis poleceń CLI i kontroli urządzenia przed wgrywaniem znajdziesz w [instrukcji narzędzia JaszczurHAL dla VS Code](../../vscode/README.pl.md). Pola deskryptorów i generowane metadane opisują [profile platform i płytek](boards_profiles_howto.md), a aktualizacje przez sieć - [instrukcja OTA](OTAWorkflow.md).

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

Wygenerowane pliki `launch.json` wskazują skrypty interfejsu i układu docelowego OpenOCD. W Windows uruchom `jh-vscode debug-tools --project <path> --json`, a następnie w ustawieniach użytkownika Cortex-Debug podaj wskazany plik wykonywalny OpenOCD i katalog narzędzi Arm. Rozszerzenie znajdzie w nim GDB. Ścieżki właściwe dla danego komputera nie są zapisywane w plikach projektu przechowywanych w Git.

W systemach Linux opartych na Debianie lub Ubuntu skrypt `runmefirst.sh` instaluje `gdb-multiarch`, a wygenerowana konfiguracja wybiera go przez `cortex-debug.gdbPath.linux`. Profil STM32G474 używa `board/st_nucleo_g4.cfg` z opcją connect-under-reset. Pozwala to wbudowanemu ST-Linkowi przejąć kontrolę nad działającym układem przed połączeniem GDB.

Dla RP i STM32 ustaw `toolchain: "cmake"` oraz `cmake.sourceDir` na `libraries/JaszczurHAL/cmake/jh_firmware_project`. Katalog aplikacji wskazuje `JH_PROJECT_DIR`. Dla ESP32 i ESP32-S3 wybierz `toolchain: "esp-idf"`; skrypt kompilacji i ścieżka manifestu plików wynikowych pochodzą wtedy z rejestru platform. Wspólne narzędzie uruchamia właściwy system kompilacji, więc projekt nie potrzebuje własnej konfiguracji CMake.

Wygeneruj działający samodzielny projekt poleceniem:

```bash
libraries/JaszczurHAL/vscode/tools/create-vscode-example.py \
  --output my-device --target rp2040 --board pico
```

Wygenerowany `tasks.json` udostępnia wybór płytki w GUI lub terminalu, wykrywanie urządzeń OTA, wgrywanie oraz zadanie `Project: Sync board picker`. Zadanie synchronizacji uruchamia się przy `folderOpen`: odczytuje rejestr płytek JaszczurHAL i zmienia zapisane opcje GUI tylko wtedy, gdy wymagają aktualizacji. Tworzy lub naprawia także profile debugowania RP2040, RP2350 ARM i STM32G474/ST-Link w `launch.json`, korzystając z pliku ELF wskazanego w manifeście. Konfiguracje dodane przez użytkownika pozostają bez zmian. VS Code wymaga zaufanego obszaru roboczego i może poprosić o jednorazową zgodę na automatyczne zadania.

Zadanie terminala `Project: Select board` odczytuje rejestr przy każdym uruchomieniu. Wygenerowane zadania korzystają z ustawienia `jaszczurhal.vscodeEntry` w systemach Unix i zastępującego je `jaszczurhal.vscodeEntryWindows` w Windows. Ustawienia wskazują umieszczone obok siebie skrypty `jh-vscode` i `jh-vscode.cmd`, które uruchamiają ten sam kod Pythona.

Aby sprawdzić lub odtworzyć pliki generowane przechowywane w repozytorium, w tym wspólne fragmenty konfiguracji i projekty przykładowe, uruchom:

```bash
python3 scripts/sync_generated.py --check
python3 scripts/sync_generated.py --write
```

Zalecane rozszerzenia można sprawdzić bez zmiany profilu VS Code:

```bash
python3 vscode/tools/manage_vscode_extensions.py
```

Opcja `--install` wymaga potwierdzenia przed instalacją brakujących rozszerzeń. W automatyzacji można użyć `--install --yes`, o ile wcześniej uzyskano zgodę na instalację.

## Podstawowe pojęcia

- **Katalog projektu**: ścieżka przekazywana do `--project`, zwykle zapisana jako `JH_PROJECT_DIR`.
- **Manifest**: plik `.vscode/jaszczurhal.project.json` przechowywany w Git.
- **Stan lokalny**: ignorowany przez Git plik `.vscode/jaszczurhal.local.json`, zawierający wybrane lokalnie platformę, płytkę i port szeregowy.
- **Platforma docelowa (target)**: stały identyfikator konfiguracji kompilacji: `rp2040`, `rp2350-arm`, `rp2350-riscv`, `stm32g474`, `esp32`, `esp32s3` lub `mock`.
- **Płytka (board)**: stały identyfikator profilu sprzętu, np. `pico`, `picow`, `pico2`, `pico2w`, `pico-rm2`, `rp2040-zero`, `rp2040-plus-4mb`, `nucleo-g474re`, `esp32-devkitc-v4` lub `waveshare-esp32-s3-zero`.
- **Rejestr płytek**: generowane dane dla narzędzi w `boards/targets/*.json`, `boards/profiles/*.json` i `boards/capabilities.json`.
- **`JH_TARGET` / `JH_BOARD`**: wartości w pamięci podręcznej CMake ustalane przed importem SDK i narzędzi kompilacji.
- **`HAL_TARGET_*`**: makro wybierające implementację HAL podczas kompilacji, generowane lub ustalane na podstawie wybranej platformy.

<a id="rozwiązywanie-targetu-i-konfiguracji"></a>

<a id="wybór-targetu-i-konfiguracji"></a>

## Wybór platformy i konfiguracji

Platforma i płytka są wybierane według następujących priorytetów, od najwyższego:

1. opcje przekazane przy wywołaniu, takie jak
   `--target rp2040 --board picow`;
2. `.vscode/jaszczurhal.local.json`;
3. śledzony manifest `target` i `board`;
4. domyślna wartość rejestru `rp2040/pico`.

Następnie ustawienia są łączone w poniższej kolejności. Wartości z późniejszych pozycji zastępują wcześniejsze:

1. domyślne wartości rejestru targetu i płytki;
2. bazowy manifest;
3. aktywna nakładka `targetProfiles.<target>`;
4. ustalone wartości `JH_TARGET` i `JH_BOARD`;
5. opcje właściwe dla danej akcji, takie jak `--port`, `--host`, `--verbose` i
   `--allow-unverified-port`.

Plik `.vscode/settings.json` zawiera ścieżki i ustawienia edytora. Tożsamość projektu, katalogi kompilacji, układ źródeł, profile platform, pliki wynikowe i ustawienia OTA zapisuj w manifeście. W samodzielnych projektach tworzonych przez `create-vscode-example.py` początkowe ustawienia wspólnej konfiguracji są dodatkowo kopiowane do `cmake.configureSettings`. Dzięki temu CMake Tools może skonfigurować projekt bez wywoływania `jh-vscode`.

Przed diagnozowaniem problemów z kompilacją lub wgrywaniem sprawdź pełną konfigurację po połączeniu wszystkich źródeł ustawień:

```bash
../libraries/JaszczurHAL/vscode/entry/jh-vscode \
  config-dump --project "$PWD"
```

Wynik zawiera obiekt `featureResolution` z polami `registryDigest`, `requestedFeatures`, `resolvedFeatures`, `resolvedFeaturesDigest` i `provenance`. Ostatnie pole wskazuje pochodzenie każdego żądanego ustawienia. Wynik uwzględnia aktywny profil platformy, wariant i wszystkie nakładki manifestu.

<a id="macierz-targetów"></a>

## Obsługiwane platformy

| Target | ISA | Domyślna płytka | Format firmware'u | Wgrywanie |
|---|---|---|---|---|
| `rp2040` | Cortex-M0+ | `pico` | ELF/BIN/HEX/UF2/MAP | weryfikacja tożsamości przez CDC, następnie BOOTSEL; albo bezpośredni BOOTSEL |
| `rp2350-arm` | Cortex-M33 | `pico2` | ELF/BIN/HEX/UF2/MAP | weryfikacja tożsamości przez CDC, następnie BOOTSEL; albo bezpośredni BOOTSEL |
| `rp2350-riscv` | Hazard3 RISC-V | `pico2` | ELF/BIN/HEX/UF2/MAP | weryfikacja tożsamości przez CDC, następnie BOOTSEL; albo bezpośredni BOOTSEL |
| `stm32g474` | Cortex-M4F | `nucleo-g474re` | ELF/BIN/HEX/MAP | OpenOCD |
| `esp32` | dwurdzeniowy Xtensa LX6 | `esp32-devkitc-v4` | ELF/MAP plus obrazy BIN bootloadera, tabeli partycji i aplikacji | flashowanie ESP-IDF przez zweryfikowany mostek USB-UART |
| `esp32s3` | dwurdzeniowy Xtensa LX7 | `waveshare-esp32-s3-zero` | ELF/MAP plus obrazy BIN bootloadera, tabeli partycji i aplikacji | flashowanie ESP-IDF przez zweryfikowany interfejs USB Serial/JTAG |
| `mock` | host | `host-mock` | plik wykonywalny/biblioteka hosta | brak |

Rejestr płytek sprawdza zgodność płytki z platformą. Dostarcza również informacje z pola `provider`, parametry fizycznej pamięci flash i PSRAM, zakres GPIO, komponenty i możliwości płytki, identyfikator programatora oraz domyślne ustawienia wgrywania. Nieznana para platforma-płytka powoduje błąd jeszcze przed uruchomieniem kompilatora.

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

Domyślnym generatorem kompilacji jest Ninja; inny generator CMake można wskazać w `cmake.generator`. Narzędzie zawsze włącza generowanie bazy poleceń kompilacji i przekazuje CMake używany interpreter Pythona. W natywnym środowisku Windows katalog roboczy CMake znajduje się pod krótką ścieżką zapisaną przez `runmefirst.ps1`. Pole `buildDir` w manifeście nadal wskazuje stałą lokalizację plików wynikowych i `compile_commands_patched.json`.

Wspólne ustawienia zapisuj w bazowym manifeście, a różnice między platformami - w niewielkich nakładkach:

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

Wybór ustalony na podstawie rejestru jest zawsze zapisywany w końcowych wartościach `JH_TARGET` i `JH_BOARD`.

Dla ESP32-S3 użyj uproszczonego manifestu dostosowanego do ESP-IDF:

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

Rejestr platform i płytek dostarcza skrypt kompilacji, manifest plików wynikowych, sposób wgrywania, wymagane włączenie FreeRTOS i identyfikator programatora `303a:1001`. Nie powielaj tych informacji w manifeście projektu.

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

Jeżeli źródła znajdują się w podkatalogach, podaj ich pełną listę:

```json
{
  "cmake": {
    "cache": {
      "JH_PROJECT_SOURCES": "app.cpp;hal_project_config.h;filters/gps.c;filters/gps.h"
    }
  }
}
```

`JH_PROJECT_SOURCES` jest listą ścieżek względem `JH_PROJECT_DIR`, rozdzielonych średnikami. Jej podanie zastępuje automatyczne wykrywanie plików w katalogu głównym.

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

Wspólna konfiguracja CMake normalizuje ścieżki i usuwa powtórzone wpisy.

Skrypt ESP-IDF wykrywa pliki C, C++ i asemblera w katalogu projektu oraz rekurencyjnie w `src/`. Przy bezpośrednim wywołaniu skryptu można zastąpić wykrywanie listą powtarzanych argumentów `--source <relative-path>`. Wszystkie wskazane pliki muszą znajdować się wewnątrz projektu.

<a id="konfiguracja-funkcji-i-runtime"></a>

## Wybór funkcji i konfiguracji wykonawczej

Flagi funkcji projektu zapisuj w `hal_project_config.h`:

```c
#pragma once

#define HAL_ENABLE_WIFI
#define HAL_ENABLE_MQTT
#define HAL_ENABLE_APP_TASK1
```

Dla profili platform, wariantów kompilacji i CI można użyć `JH_EXTRA_DEFINES`:

```json
{
  "cmake": {
    "cache": {
      "JH_EXTRA_DEFINES": "HAL_ENABLE_FREERTOS;APP_DIAGNOSTICS=1"
    }
  }
}
```

Funkcję włącza zapis `HAL_ENABLE_X` albo `HAL_ENABLE_X=1`. Po ustaleniu aktywnego profilu platformy i wariantu przykładu wspólna konfiguracja oraz `jh-vscode` odrzucają `HAL_ENABLE_X=0` i inne jawne wartości, zgłaszając `[JH-CFG-VALUE]`. Aby wyłączyć funkcję, pomiń jej symbol.

Ta reguła nie dotyczy zwykłych parametrów, takich jak `APP_DIAGNOSTICS=0`. Na listach definicji każdy wpis `HAL_ENABLE_*` musi być osobnym, prostym tokenem, a wpisy muszą być rozdzielone średnikami. Białe znaki nie są separatorami definicji. Wyrażenia generatora CMake nie są obsługiwane.

Deskryptor `esp32s3` wymaga `HAL_ENABLE_FREERTOS` i obsługuje peryferia oraz usługi sieciowe opisane w dokumentacji jako zakres faz 2 i 3. Zestaw obejmuje APP_TASK1, UART, tryby kontrolera i urządzenia podrzędnego I2C, SPI, PWM_FREQ, RGB_LED, PCNT, STACK_GUARD, BLE, WiFi, TCP/UDP, gniazda BSD, TLS, klienta i serwer HTTP, pliki HTTP, serwer WebSocket, MQTT, czas, OTA oraz WireGuard.

Podstawowa konfiguracja zawsze zawiera prosty PWM oraz moduły systemowe, synchronizacji, GPIO, ADC, komunikacji szeregowej i timerów. Żądanie funkcji lub zależności spoza listy dopuszczonej przez deskryptor powoduje błąd `[JH-CFG-UNSUPPORTED]`.

Początkowy deskryptor `esp32` celowo obejmuje mniej funkcji. Obsługuje wymagany FreeRTOS oraz `HAL_ENABLE_BLUETOOTH_GAMEPAD`, które wybiera Bluedroid, BR/EDR i ESP HID Host. Funkcje dostępne tylko na ESP32-S3, w tym publiczne API BLE, są odrzucane podczas kontroli wstępnej.

Przy użyciu `firmware_entry.h` zgodnego z konwencją Fiesta ustawienie `FIESTA_ENABLE_CORE1=1` wymaga również `HAL_ENABLE_APP_TASK1` w `hal_project_config.h` lub innym standardowym źródle konfiguracji funkcji. Zapewnia to zgodność wygenerowanego adaptera punktu wejścia, funkcji żądanych i wynikających z zależności oraz sygnatury linkowania.

Rejestr funkcji wyznacza pełny zestaw zależności przechodnich jednakowo dla wszystkich platform i narzędzi. Wygenerowany nagłówek C definiuje wynikające z nich makra, CMake dobiera źródła i zależności, a generator płytki oblicza `featureHash` i sygnaturę linkowania. `jh-vscode` używa tego samego zestawu podczas kontroli wstępnej i sprawdzania dostępności OTA; do CMake przekazuje przy tym pierwotnie żądane funkcje. Aby otrzymać podczas kompilacji raport wszystkich aktywnych funkcji z rejestru, zdefiniuj `HAL_CONFIG_VERBOSE`.

Reguły zależne od parametrów konfiguracji, systemu kompilacji, możliwości płytki lub platformy pozostają w `hal_config.h`. Obejmują automatyczne włączenie I2C dla wybranego typu EEPROM, domyślny transport GPS, kontrolę zgodności implementacji z systemem kompilacji, sprawdzanie możliwości płytki i ograniczenia poszczególnych platform.

Plik `hal_project_config.h` jest odczytywany przed automatycznym wykryciem platformy i utworzeniem pochodnych makr platformy oraz płytki. Umieszczaj w nim wyłącznie makra: bezpośrednie definicje `HAL_TARGET_*`, `HAL_BOARD_PROFILE_*`, `HAL_ENABLE_*` i parametrów konfiguracji. Nie dołączaj nagłówków JaszczurHAL ani nie uzależniaj zawartości od `HAL_TARGET_IS_*` / `HAL_BOARD_IS_*`.

Definicje używane do wyboru źródeł muszą mieć bezwarunkową postać `#define HAL_ENABLE_X` lub `#define HAL_ENABLE_X 1`. Jedyny dozwolony wyjątek to osłona `#ifndef HAL_ENABLE_X` dotycząca tego samego symbolu. Żadne inne `#if`/`#ifdef`, również oparte na bezpośrednich lub pochodnych makrach platformy i płytki, nie są obsługiwane: na tym etapie narzędzie analizuje treść pliku, a nie wynik działania preprocesora.

Platformę i fizyczną płytkę wybieraj w polach `target` i `board`. Projekt określa połączenia aplikacji, tożsamość USB, sekrety, zasady podziału pamięci na partycje i włączone funkcje.

<a id="katalogi-budowania-i-pliki-generowane"></a>

<a id="katalogi-buildu-i-pliki-generowane"></a>

## Katalogi kompilacji i pliki generowane

Zewnętrzny projekt firmware zapisuje wyniki w `${project}/.build`. Przykłady z repozytorium i konfiguracje testów sprzętowych używają stałych lokalizacji względem katalogu głównego JaszczurHAL:

```text
.build/examples/<example>/
.build/hardware/<fixture>/
```

W systemach Unix pamięć podręczna CMake jest oddzielna dla każdej pary platforma-płytka i znajduje się pod `cmakeBuildDir` wskazanym w manifeście:

```text
<cmakeBuildDir>/<target>/<board>/
```

Dzięki temu różne zestawy narzędzi, platformy systemów kompilacji, wygenerowane nagłówki i układy pamięci linkera nie współdzielą jednej pamięci podręcznej. Natywne środowisko Windows używa zamiast tego krótkiej ścieżki przygotowanej przez skrypt inicjalizacyjny:

```text
<BuildRoot>/<project-name>-<path-hash>/cmake/<target>/<board>/
```

Oryginalny `compile_commands.json` znajduje się w odpowiednim katalogu CMake. Narzędzie zapisuje dostosowany `compile_commands_patched.json` w stałym `buildDir` i po każdej kompilacji odświeża pliki firmware wybranej platformy. Robi to również po powrocie do wcześniej skonfigurowanej platformy, gdy Ninja nie musi ponownie kompilować źródeł.

`jh-vscode` zapisuje klucze pamięci podręcznej zarządzane przez manifest w `.jh-vscode-cache-keys.json`. Usunięcie klucza z manifestu powoduje jego usunięcie z pamięci podręcznej przy następnej konfiguracji. Zmiana katalogu źródłowego CMake powoduje odtworzenie nieaktualnej pamięci podręcznej, jeśli znajduje się ona w zarządzanym katalogu wynikowym.

W projektach ESP-IDF `buildDir` jest bezpośrednio katalogiem kompilacji IDF. Musi znajdować się w `.build` projektu lub repozytorium JaszczurHAL, ewentualnie w podkatalogu jednej z tych lokalizacji. Skrypt zarządza tam wygenerowaną konfiguracją projektu i SDK; nie tworzy drugiego rejestru płytek.

Generowane pliki obejmują:

- pliki CMake, nagłówki i JSON wygenerowane dla płytki oraz jednostki
  translacji z sygnaturą linkowania;
- pierwotny `compile_commands.json` w drzewie CMake oraz stabilny
  `compile_commands_patched.json` w `buildDir`;
- `.vscode/c_cpp_properties.json`;
- artefakty targetu ELF/BIN/HEX/UF2/MAP lub ELF/BIN/HEX/MAP;
- dla ESP-IDF: `jh_esp_idf_artifacts.json`, ELF/MAP/BIN aplikacji, obrazy
  bootloadera i tabeli partycji, `sdkconfig`, log kompilacji, wygenerowane
  metadane płytki i linkowania, informację o źródle toolchainu oraz pierwotną
  bazę poleceń kompilacji;
- kontener OTA i scalony plik UF2 do odzyskiwania, gdy OTA jest włączone.

Ustawienia przechowywane w Git nadal zapisuj w manifeście i `hal_project_config.h`.

<a id="akcje-buildu-i-wgrywania"></a>

## Kompilowanie i wgrywanie

```bash
../libraries/JaszczurHAL/vscode/entry/jh-vscode build --project "$PWD"
../libraries/JaszczurHAL/vscode/entry/jh-vscode upload --project "$PWD"
../libraries/JaszczurHAL/vscode/entry/jh-vscode monitor --project "$PWD"
../libraries/JaszczurHAL/vscode/entry/jh-vscode clean --project "$PWD"
```

Zadanie `Project: Upload` wybiera sposób wgrywania na podstawie rejestru. Na RP z działającym firmware najpierw weryfikuje tożsamość przez USB CDC, a następnie przełącza urządzenie do BOOTSEL i wgrywa UF2. Dla niezaprogramowanej płytki użyj `Project: Upload (UF2 / BOOTSEL)`. Na STM32G474 zadanie uruchamia odpowiedni cel OpenOCD.

Na ESP32-S3 narzędzie wykonuje kompilację z kontrolą konfiguracji, sprawdza wszystkie ścieżki w manifeście obrazów i przekazuje zweryfikowany port do wgrywania przez ESP-IDF. Profil płytki podaje VID/PID `303a:1001`. Brak urządzenia, nieaktualna ścieżka, niezgodna tożsamość lub kilka pasujących urządzeń przerywają operację. Opcja `--allow-unverified-port` jawnie omija kontrolę portu i wymaga jednoczesnego podania `--port`.

Na czas wgrywania narzędzie zwalnia port zajęty przez monitor szeregowy projektu. Monitor może ponownie się połączyć, gdy urządzenie zgłosi interfejs USB. Niejednoznaczny wybór woluminu BOOTSEL lub urządzenia na podstawie tożsamości szeregowej przerywa operację.

Na ESP32-S3 zadanie `Project: Serial Monitor` wybiera jedno urządzenie zgodne z identyfikatorem programatora z rejestru, chyba że jawnie podano port. `Project: Refresh IntelliSense` korzysta z poleceń kompilacji Xtensa wygenerowanych przez ESP-IDF; nie zastępuje ich trybem Arm. Dla ESP32-S3 nie dostarczono `build-debug` ani zarządzanych profili Cortex-Debug.

## Konfiguracja manifestu OTA

W projektach RP korzystających z CMake manifest wskazuje wygenerowany kontener OTA i jego metadane kompilacji oraz wspólne ustawienia połączenia OTA:

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

Projekty ESP-IDF nie używają wpisów `cmake` i `artifacts.ota` przeznaczonych dla RP. Ich manifest kompilacji wskazuje surowy obraz binarny aplikacji. Obiekt `ota` z powyższego przykładu nadal określa adresy, porty i uwierzytelnianie po stronie komputera.

`ota.broadcast` wskazuje adres wyszukiwania urządzeń przez UDP, a `ota.host` - stały adres konkretnego urządzenia. `ota.listenPort` określa port, na którym komputer oczekuje na zwrotne połączenie TCP. Domyślne `8266` odpowiada trwałej regule zapory ograniczonej do LAN, tworzonej przez `runmefirst.sh`. Wartość `0` wybiera port efemeryczny. `ota.passwordEnv` pozwala przechowywać sekret w zmiennej środowiskowej zamiast w manifeście śledzonym przez Git.

Nazwa hosta urządzenia, port UDP i hasło muszą być zgodne z konfiguracją firmware. [Instrukcja OTA](OTAWorkflow.md) opisuje pliki wynikowe poszczególnych platform, pierwsze programowanie, zadania, uwierzytelnianie, zaporę komputera, potwierdzanie rozruchu próbnego, wycofanie aktualizacji i odzyskiwanie. Przy aktualizacji RP narzędzie podpisuje kontener JaszczurHAL. Przy aktualizacji ESP-IDF sprawdza manifest kompilacji i przesyła wskazany surowy obraz aplikacji bez konwersji do kontenera RP.

## Przykłady i warianty

Manifesty przykładów mogą zawierać `example.targets` i `example.variants`. Wariant może zastąpić nazwę modułu, źródła, definicje funkcji, obsługiwane platformy i wpisy pamięci podręcznej CMake.

```bash
scripts/examples_dispatcher.py list
scripts/examples_dispatcher.py build --target rp2040 --example 01_core_runtime
```

Wygenerowane manifesty przykładów są używane przez zestaw kontroli jakości jako dane wejściowe kompilacji. Zobacz [przykłady JaszczurHAL](../../examples/README.pl.md).
