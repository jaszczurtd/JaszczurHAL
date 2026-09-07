# Kompilacja biblioteki JaszczurHAL

*Dostępne również [po angielsku](../en/lib_compilation.md).*

<a id="tldr"></a>

## Podstawowe polecenia

```bash
./scripts/build_rp_native_lib.sh --target rp2040
./scripts/build_rp_native_lib.sh --target rp2350-arm
./scripts/build_rp_native_lib.sh --target rp2350-riscv
./scripts/build_stm32_lib.sh
python3 scripts/build_esp_idf.py build \
  --project tests/fixtures/esp32s3_phase3 --clean
```

> **Część [Dokumentacji API JaszczurHAL](JaszczurHAL_API.md)**

JaszczurHAL używa CMake do kompilacji dla komputera, RP i STM32. Dla ESP32-S3 skrypt Pythona z repozytorium uruchamia ESP-IDF w ustalonej wersji. Przy kompilacji na urządzenie wybiera się platformę docelową i fizyczną płytkę z rejestru opisanego w [profilach platform i płytek](boards_profiles_howto.md).

| Target | Domyślna płytka | Punkt wejścia kompilacji | Selektor backendu |
|---|---|---|---|
| Mock hosta | - | CMake w katalogu głównym repozytorium | `HAL_TARGET_MOCK` |
| RP2040 | `pico` | `rp_native_lib/` | `HAL_TARGET_RP2040` |
| RP2350 ARM | `pico2` | `rp_native_lib/` | `HAL_TARGET_RP2350_ARM` |
| RP2350 RISC-V | `pico2` | `rp_native_lib/` | `HAL_TARGET_RP2350_RISCV` |
| STM32G474 | `nucleo-g474re` | `stm32_lib/` | `HAL_TARGET_STM32G474` |
| ESP32-S3 | `waveshare-esp32-s3-zero` | kompilacja komponentu ESP-IDF sterowana skryptem | `HAL_TARGET_ESP32_S3` |

Wszystkie artefakty tworzone przez repozytorium są zapisywane w `.build/`.
Skrypty pomocnicze odrzucają ścieżki wyjściowe prowadzące poza ten katalog.

<a id="zgodność-targetu-i-płytki"></a>

## Zgodność platformy i płytki

Makra wybierające platformę są zdefiniowane w `src/hal/core/hal_target.h`. Gdy narzędzia kompilacji nie dostarczają danych wystarczających do automatycznego wykrycia platformy, zdefiniuj dokładnie jedno z tych makr:

```c
#define HAL_TARGET_RP2040
#define HAL_TARGET_RP2350_ARM
#define HAL_TARGET_RP2350_RISCV
#define HAL_TARGET_STM32G474
#define HAL_TARGET_ESP32_S3
#define HAL_TARGET_MOCK
```

`JH_TARGET` określa procesor i platformę wykonawczą, a `JH_BOARD` - profil fizycznej płytki. Wygenerowany rejestr globalny i konfiguracja zastępcza są przechowywane w `src/hal/generated/`. Każda kompilacja tworzy ponadto:

```text
include/generated/
  jh_board_config.h
  jh_link_contract.h
```

Wygenerowany symbol zgodności ma postać
`jh_board_contract_<target>_<board>_<featureHash>`. Dzięki niemu linkowanie
kończy się błędem, jeśli biblioteka, nagłówki płytki i zestaw funkcji nie
pochodzą z tej samej konfiguracji. Przechowuj `libJaszczurHAL.a` razem
z wygenerowanymi nagłówkami z tej samej kompilacji.

Przy ustalaniu konfiguracji funkcji rozróżnia się:

- `requestedFeatures`: funkcje wskazane bezpośrednio w definicjach CMake i `hal_project_config.h`;
- `resolvedFeatures`: posortowany zestaw tych funkcji wraz ze wszystkimi zależnościami przechodnimi z rejestru, używany do doboru źródeł, zależności i sygnatury linkowania.

Wpisy funkcji mogą zawierać dodatkowe `buildEffects`. Wygenerowane dane CMake wskazują źródła danej funkcji i manifesty źródeł BearSSL, LittleFS lub SX126x zarządzane przez JaszczurHAL dla RP i STM32. ESP-IDF korzysta z tych samych wpisów dla źródeł przenośnych i dodaje lokalnie pliki właściwe dla ESP32. Konfigurację płytki, adaptery platform, układ pamięci flash i specjalne obrazy firmware określają odpowiednie skrypty kompilacji.

Platforma może wymagać dodatkowej funkcji. ESP32-S3 zawsze dodaje `HAL_ENABLE_FREERTOS` i zapisuje platformę jako źródło tego wymagania, ponieważ ESP-IDF uruchamia scheduler przed `app_main()`.

Wynikowy JSON płytki zapisuje oba zestawy funkcji i pełny skrót zestawu po rozwiązaniu zależności. Pole `features` pozostaje aliasem `resolvedFeatures`. `featureHash` ma 12 znaków i jest wyznaczany z SHA-256 dla `hal.profileId` oraz posortowanych funkcji zapisanych z wartością `=1`. Dodatkowe żądanie, które nie zmienia wynikowego zestawu, nie zmienia też sygnatury biblioteki.

JSON zawiera również `boardCompileDefinitions`. CMake udostępnia je jako `JH_BOARD_COMPILE_DEFINITIONS`, a `jh_board_config.h` - jako definicje dla projektów kompilowanych bezpośrednio.

Dwie warunkowe reguły pozostają poza rejestrem v1: EEPROM AT24C256 może
dodać I2C, a GPS może dodać UART, gdy nie zażądano żadnego transportu
szeregowego. Pozostają one w `hal_config.h` i nie są uwzględniane przy
porównywaniu konfiguracji za pomocą `featureHash`. Sprawdzenia targetu, płytki,
systemu budowania, cech sprzętowych i regulowanych parametrów konfiguracyjnych
również pozostają w tym pliku.

Przed przygotowaniem wydania sprawdź poprawność żądanych funkcji:

```bash
python3 scripts/generate_hal_features.py --lint --input-root .
python3 scripts/generate_hal_features.py \
  --lint --effective --input-root . \
  --resolution-output .build/effective-feature-resolution.json
```

Oba polecenia zgłaszają błąd przy nieprawidłowej konfiguracji. Opcja `--report-only` służy do tymczasowego audytu podczas migracji; nie zastępuje standardowej kontroli jakości.

## Zainstalowany pakiet i bezpośrednie użycie kompilatora

Po skonfigurowaniu i skompilowaniu biblioteki statycznej dla wybranej platformy utwórz kompletny pakiet instalacyjny z tej samej konfiguracji:

```bash
cmake --install .build/static/<target>/<board> \
  --prefix .build/install/<target>/<board>
```

Instalacja zawiera między innymi:

```text
include/
  JaszczurHAL.h
  hal/generated/
    jh_hal_features.h
    jh_board_registry.h
    jh_board_fallback_config.h
  generated/
    jh_board_config.h
    jh_link_contract.h
lib/
  libJaszczurHAL.a
share/JaszczurHAL/generated/
  jh_link_contract_reference.c
  jh_board_resolved.json
```

Pozostałe nagłówki publiczne HAL są instalowane w `include/`. Traktuj całe drzewo instalacji jako jeden pakiet. Przy bezpośrednim wywołaniu kompilatora dodaj `include/` i `include/generated/` do ścieżek nagłówków, a do definicji - makro platformy i żądane funkcje zapisane w `jh_board_resolved.json`. Skompiluj także `share/JaszczurHAL/generated/jh_link_contract_reference.c` i zlinkuj otrzymany plik obiektowy z `lib/libJaszczurHAL.a`. Przykładowy układ poleceń:

```bash
"${CXX}" <target compile flags> \
  -I<prefix>/include -I<prefix>/include/generated \
  -DHAL_TARGET_<TARGET>=1 -D<REQUESTED_FEATURE>=1 \
  -c app.cpp -o app.o
"${CC}" <target compile flags> \
  -I<prefix>/include -I<prefix>/include/generated \
  -c <prefix>/share/JaszczurHAL/generated/jh_link_contract_reference.c \
  -o jh_link_contract_reference.o
"${CXX}" <target link flags> app.o jh_link_contract_reference.o \
  <prefix>/lib/libJaszczurHAL.a <platform libraries> -o firmware.elf
```

Jeżeli żądane funkcje obejmują `HAL_ENABLE_STACK_PROTECTOR`, dodaj `-fstack-protector-strong` przy kompilowaniu każdego pliku C/C++ aplikacji. Natywna konfiguracja CMake firmware robi to automatycznie. Zainstalowana biblioteka zawiera już implementacje `__stack_chk_guard` / `__stack_chk_fail`; nie dołączaj drugiej implementacji ochrony stosu.

`hal_config.h` dołącza zainstalowany, wygenerowany nagłówek funkcji. Bezpośrednie wywołanie kompilatora otrzymuje dzięki temu ten sam zestaw zależności bez uruchamiania Pythona. `jh_board_config.h` udostępnia też definicje z `jh_board_resolved.json.boardCompileDefinitions`, w tym wybór implementacji radiowej, magistrali, stosu i pinów. W wierszu poleceń podawaj wyłącznie makro platformy i zapisane żądania funkcji. Nie powtarzaj definicji profilu płytki przez `-D`.

Wygenerowane odwołanie do sygnatury korzysta z atrybutów GCC/Clang `constructor, used`. Kontrola zgodności pozostaje aktywna przy `--gc-sections`, o ile obsługiwany skrypt linkera zachowuje tablice konstruktorów. Nadal wymagane są SDK platformy, pliki startowe, skrypt linkera i biblioteki właściwe dla używanych narzędzi.

<a id="mock-hosta"></a>

## Testy na komputerze z implementacją mock

Projekt w katalogu głównym repozytorium kompiluje deterministyczną implementację mock oraz programy testowe:

```bash
cmake -S . -B .build/host
cmake --build .build/host --parallel
ctest --test-dir .build/host --output-on-failure
```

Kompilacja na komputerze wymaga natywnych narzędzi C/C++ i CMake. Nie wymaga SDK urządzenia ani kompilatora krzyżowego.

## RP2040 i RP2350

Dla RP używane są oficjalne Pico SDK w wersji ustalonej w repozytorium, wygenerowany profil płytki i punkt wejścia aplikacji dostarczany przez HAL.

### Skrypt pomocniczy

Z katalogu głównego repozytorium:

```bash
# RP2040 / Pico
./scripts/build_rp_native_lib.sh

# RP2040 / Pico z przykładową aplikacją
./scripts/build_rp_native_lib.sh \
  --target rp2040 \
  --board pico \
  --example 01_core_runtime

# RP2350 ARM
./scripts/build_rp_native_lib.sh --target rp2350-arm

# RP2350 RISC-V
./scripts/build_rp_native_lib.sh --target rp2350-riscv

# Natywny FreeRTOS SMP
./scripts/build_rp_native_lib.sh --target rp2040 --freertos

# Tylko linkowalna biblioteka statyczna, bez kontrolnych obrazów firmware'u
./scripts/build_rp_native_lib.sh --target rp2040 --library-only
```

Główne opcje to:

| Opcja | Znaczenie |
|---|---|
| `--target NAME` | `rp2040`, `rp2350-arm` lub `rp2350-riscv` |
| `--board NAME` | Profil płytki zgodny z wybranym targetem |
| `--example NAME` | Zbuduj `examples/NAME` jako firmware |
| `--example-source FILE` | Wybierz jedno źródło z przykładu wieloprofilowego; opcję można podać wielokrotnie |
| `--freertos` | Włącz jądro FreeRTOS SMP w wersji wskazanej przez repozytorium |
| `--library-only` | Zbuduj wyłącznie linkowalny target `libJaszczurHAL.a`, bez kontrolnych obrazów firmware'u |
| `-p`, `--project-config DIR` | Katalog zawierający `hal_project_config.h` |
| `-D KEY=VALUE` | Dodatkowa definicja HAL; opcję można podać wielokrotnie |
| `--sdk-dir PATH` | Katalog z repozytorium Pico SDK |
| `--toolchain PATH` | Katalog główny toolchainu krzyżowego |
| `--picotool-dir PATH` | Katalog z repozytorium źródeł `picotool` |
| `-o`, `--output DIR` | Katalog kompilacji poniżej `.build/` |
| `--clean` | Utwórz od nowa wybrany katalog kompilacji |
| `-j`, `--jobs N` | Liczba równoległych zadań kompilacji |

Domyślny katalog wynikowy to `.build/static/<target>/<board>/`. Standardowa kompilacja sprawdza bibliotekę statyczną i komplet kontrolnych plików ELF/BIN/UF2. Opcja `--library-only` ogranicza kontrolę do samej biblioteki:

```text
.build/static/<target>/<board>/
  libJaszczurHAL.a
  include/generated/
  jh_rp_native_artifact_probe.{elf,bin,uf2}
  jh_rp_native_core1_probe.{elf,bin,uf2}
  jh_rp_native_firmware.{elf,bin,uf2}  # z opcją --example
```

Program kontrolny rdzenia 1 sprawdza symbole punktu wejścia i obsługi wielu rdzeni. Bez systemu operacyjnego `app_task1()` działa na rdzeniu 1 uruchamianym przez Pico SDK. Przy FreeRTOS HAL tworzy zadania przypisane do rdzeni (CPU affinity) i uruchamia scheduler.

<a id="bezpośredni-build-cmake"></a>

### Bezpośrednie użycie CMake

Skrypt przygotowuje zależności w ustalonych wersjach i przekazuje ustawienia pamięci podręcznej CMake. Przy `HAL_ENABLE_FREERTOS` bezpośrednia konfiguracja CMake uruchamia `scripts/component_manager.py`, aby przygotować lub sprawdzić FreeRTOS-Kernel. Zewnętrzny `JH_FREERTOS_KERNEL_DIR` jest sprawdzany, ale nigdy zastępowany. Po przygotowaniu pozostałych zależności podstawowa konfiguracja RP2040 wygląda tak:

```bash
cmake -S rp_native_lib -B .build/manual/rp2040-pico \
  -DPICO_SDK_PATH="$PWD/third_party/pico-sdk" \
  -DJH_PICOTOOL_EXECUTABLE="$PWD/.build/tools/picotool/picotool" \
  -DJH_TARGET=rp2040 \
  -DJH_BOARD=pico
cmake --build .build/manual/rp2040-pico --parallel
```

Dla katalogu aplikacji dodaj:

```bash
-DJH_RP_NATIVE_APP_DIR="$PWD/examples/01_core_runtime" \
-DHAL_PROJECT_CONFIG_DIR="$PWD/examples/01_core_runtime"
```

Aplikacja dostarcza `app_start()`, `app_task0()` oraz opcjonalnie
`app_task1()`. Plik `src/hal_app_entry.cpp` zawiera `main()` i uruchamia te
funkcje zgodnie z wybranym modelem wykonania: bare-metal albo FreeRTOS.

### Dołączanie obsługi CMake dla RP do własnego projektu

Projekty używające wspólnego wyboru platformy korzystają z `cmake/targets/rp-native.cmake`. Tę samą integrację można dołączyć do własnego projektu CMake opartego na Pico SDK:

```cmake
include(path/to/JaszczurHAL/cmake/jh_rp_native_sdk.cmake)

add_executable(firmware
    app.cpp
)
jh_add_rp_native_firmware(firmware)
```

Funkcja pomocnicza dołącza HAL, wygenerowane metadane płytki, wybrane
biblioteki Pico SDK, układ linkera, punkt wejścia aplikacji oraz końcowe
przetwarzanie plików ELF/BIN/UF2.

Układ pamięci flash, pamięć trwała, sloty OTA i przydział pamięci RAM są
udokumentowane w
[Mapie pamięci RP](../../rp_native_lib/MEMORY_MAP.md).

## STM32G474

Dla STM32G474 powstaje biblioteka statyczna zgodna z wygenerowanym profilem płytki:

```bash
# Bare-metal
./scripts/build_stm32_lib.sh

# FreeRTOS
./scripts/build_stm32_lib.sh --freertos

# Konfiguracja projektu i dodatkowe funkcje
./scripts/build_stm32_lib.sh \
  --board nucleo-g474re \
  -p /path/to/firmware \
  -D HAL_ENABLE_MCP2515 \
  -D HAL_ENABLE_LITTLEFS
```

Domyślny katalog wyjściowy ma następującą zawartość:

```text
.build/static/stm32g474/nucleo-g474re/
  libJaszczurHAL.a
  include/generated/
```

Przy bezpośrednim użyciu CMake wskaż dostarczony zestaw narzędzi:

```bash
cmake -S stm32_lib -B .build/manual/stm32g474-nucleo \
  -DCMAKE_TOOLCHAIN_FILE=stm32_lib/toolchain_stm32g474.cmake \
  -DJH_TARGET=stm32g474 \
  -DJH_BOARD=nucleo-g474re
cmake --build .build/manual/stm32g474-nucleo --parallel
```

Tę samą implementację można skompilować kompilatorem komputera, aby wykonać podstawowe kontrole i utworzyć bazę poleceń dla clang-tidy analizującego STM32. Tryb ten nie definiuje `JH_STM32G474_HW` i wymaga jawnego włączenia. Brak narzędzi krzyżowych nie może więc niepostrzeżenie spowodować utworzenia biblioteki dla komputera zamiast firmware:

```bash
cmake -S stm32_lib -B .build/manual/stm32g474-host \
  -DJH_STM32_HOST_SANITY=ON \
  -DCMAKE_EXPORT_COMPILE_COMMANDS=ON
cmake --build .build/manual/stm32g474-host --parallel
```

Bez `CMAKE_TOOLCHAIN_FILE` lub `JH_STM32_HOST_SANITY` konfiguracja kończy się błędem wskazującym obie możliwości. Wygenerowane pliki płytki pozostają w katalogu kompilacji CMake, który musi znajdować się pod `.build`.

Funkcje projektu przekaż przez `EXTRA_HAL_DEFINES` lub `scripts/build_stm32_lib.sh -D ...`. `HAL_ENABLE_FREERTOS` włącza integrację z jądrem w ustalonej wersji. Bezpośrednie wywołanie CMake uruchamia `scripts/component_manager.py`, aby przygotować lub sprawdzić jądro.

Skrypt powłoki wywołuje `scripts/ensure_freertos_kernel.sh` dla `--freertos` lub jawnego `-D HAL_ENABLE_FREERTOS`. Jeżeli funkcję wskazano tylko w `hal_project_config.h`, zależność przygotowuje CMake. Obie ścieżki korzystają z tego samego menedżera. Zewnętrzny `JH_FREERTOS_KERNEL_DIR` jest weryfikowany, ale nigdy zastępowany.

Bez systemu operacyjnego wygenerowany punkt wejścia HAL pracuje w pętli kooperacyjnej. Przy FreeRTOS zadaniami zarządza scheduler.

Podczas linkowania firmware'u trzeba dołączyć wygenerowany obiekt z odwołaniem
do sygnatury linkowania i użyć pasującej konfiguracji linkera. Wpis w sekcji
konstruktorów zachowuje to odwołanie przy włączonym `--gc-sections`. Brakujące
lub niedopasowane archiwum nadal powoduje oczekiwany błąd niezdefiniowanego
symbolu zgodności. Zobacz
[Mapę pamięci STM32G474](../../stm32_lib/MEMORY_MAP.md), aby sprawdzić rezerwacje
flash, SRAM, pamięci trwałej i OTA.

## ESP32-S3 z ESP-IDF

Na ESP32-S3 JaszczurHAL jest kompilowany jako część projektu firmware, a nie instalowany jako osobny pakiet `libJaszczurHAL.a`. Główny skrypt `scripts/build_esp_idf.py` obsługuje polecenia `build`, `artifacts` i `flash`:

```bash
# Czysty build z domyślną płytką targetu.
python3 scripts/build_esp_idf.py build \
  --project tests/fixtures/esp32s3_phase3 \
  --target esp32s3 --board waveshare-esp32-s3-zero --clean

# Ponowne sprawdzenie istniejącego buildu bez kompilowania.
python3 scripts/build_esp_idf.py artifacts \
  --project tests/fixtures/esp32s3_phase3 \
  --target esp32s3 --board waveshare-esp32-s3-zero

# Ponowne sprawdzenie, a następnie wgranie projektu pod adresy z manifestu.
python3 scripts/build_esp_idf.py flash \
  --project path/to/esp32-project \
  --target esp32s3 --board waveshare-esp32-s3-zero \
  --port /dev/serial/by-id/<Espressif-USB-Serial-JTAG-device>
```

`tests/fixtures/esp32s3_phase3` jest konfiguracją do testów kompilacji i linkowania, używaną przez CI i Gate 8. Włącza wszystkie implementacje ESP32-S3 dostarczone do fazy 3 i sprawdza zależności funkcji, dobór komponentów, kompilację, linkowanie, generowanie partycji i publikowanie plików wynikowych. Nie jest testem działania na sprzęcie.

Domyślny katalog kompilacji to
`<project>/.build/esp-idf/esp32s3/waveshare-esp32-s3-zero/`. `--output`
może wybrać inną lokalizację poniżej katalogu głównego `.build` projektu
lub repozytorium. Argument `--source` można podać wielokrotnie, aby zastąpić
automatyczne wykrywanie; bez niego skrypt dołącza obsługiwane pliki źródłowe
z katalogu głównego projektu oraz, rekurencyjnie, z katalogu `src/`.
Argumenty `--feature` i `--define` również można podawać wielokrotnie, aby
rozszerzyć konfigurację projektu. Opcja `--idf-dir` lub zmienna
`JH_ESP_IDF_DIR` wskazuje zewnętrznie zarządzane repozytorium dopiero po
zweryfikowaniu jego dokładnej wersji i narzędzi.

Skrypt generuje na podstawie profilu płytki domyślne wartości `sdkconfig` dla
pamięci flash i PSRAM, buduje źródła projektu z niewielkim komponentem
integracyjnym JaszczurHAL i waliduje wynik przed opublikowaniem
`jh_esp_idf_artifacts.json`. Manifest zawiera względne ścieżki do ELF,
MAP, BIN aplikacji, bootloadera, tabeli partycji, bazy poleceń kompilacji,
wygenerowanych metadanych płytki i linkowania oraz logów. Pole `flashImages`
zawiera uporządkowaną listę obrazów wraz z ich przesunięciami, rozmiarami i
skrótami SHA-256. Dane o
konfiguracji obejmują końcowy skrót `sdkconfig` oraz wybrany profil partycji;
dane o toolchainie obejmują wersję i commit ESP-IDF wskazane przez
repozytorium, faktycznie użyty kompilator, wersje CMake, Ninja, IDF Python i
esptool oraz skrót pliku `tools.json` ESP-IDF.

ESP32-S3 zawsze włącza `HAL_ENABLE_FREERTOS`. Obsługuje również dostarczone flagi peryferiów fazy 2 oraz funkcje sieciowe i usługi fazy 3. Podstawowy komponent zawiera system, synchronizację, GPIO, ADC, prosty PWM, komunikację szeregową, diagnostykę i timery. Funkcja żądana bezpośrednio lub przez zależność, lecz nieobecna na liście deskryptora, powoduje `[JH-CFG-UNSUPPORTED]`.

Wygenerowany CMake projektu zawiera wynikowy zestaw funkcji, listę źródeł i publiczne oraz prywatne zależności komponentów ESP-IDF. Konfiguracja komponentu korzysta z tych list, zamiast utrzymywać osobny graf źródeł. `scripts/build_esp_idf_phase0.py` pozostaje skryptem zgodności dla odrębnej konfiguracji testowej fazy 0.

<a id="workspace-repozytorium-i-vs-code"></a>

## Repozytorium w VS Code

Aby kompilować bibliotekę statyczną w VS Code, otwórz katalog główny repozytorium JaszczurHAL. Ten tryb pracy jest niezależny od kompilowania projektu firmware, ale używa tych samych nazw zadań:

| Skrót | Zadanie repozytorium |
|---|---|
| `Ctrl+Shift+1` | `Project: Build` |
| `Ctrl+Shift+6` | `Project: Refresh IntelliSense` |
| `Ctrl+Shift+7` | `Project: Clean` |
| `Ctrl+Shift+0` | `Project: Install library` |
| `Ctrl+Shift+Alt+1` | `Project: Select board (GUI)` |
| `Ctrl+Shift+Alt+2` | `Project: Select board` |

Domyślny profil to `rp2040:pico`. Dane platformy i płytki pochodzą z `boards/`, a lokalny wybór jest zapisywany w ignorowanym przez Git `.vscode/jaszczurhal.library.local.json`. Obsługiwane są mock, trzy natywne warianty RP i STM32G474. Pliki wynikowe trafiają do:

```text
.build/vscode/library/<target>/<board>/
```

`Project: Build` tworzy bibliotekę dla aktywnego profilu i wybiera jej bazę poleceń kompilacji dla cpptools. `Project: Refresh IntelliSense` wykonuje tę samą kompilację przyrostową, a następnie odtwarza ignorowany przez Git `.vscode/c_cpp_properties.json`. Konfiguracje sprzętowe tworzą `libJaszczurHAL.a`, a mock - `libhal_mock.a`.

`Project: Install library` kompiluje aktywny profil sprzętowy i instaluje bibliotekę, publiczne i wygenerowane nagłówki oraz dane sygnatury w `.build/install/<target>/<board>/`. Mock nie obsługuje instalacji. `Project: Clean` usuwa tylko katalog kompilacji i instalacji aktywnego profilu oraz odpowiadający mu wygenerowany plik IntelliSense. Nie usuwa innych konfiguracji, zarządzanych narzędzi ani źródeł zależności.

Pliki `.vscode` przechowywane w katalogu głównym repozytorium są generowane na podstawie rejestru płytek. Po zmianie rejestru lub zadań sprawdź albo odtwórz pliki generowane:

```bash
python3 scripts/sync_generated.py --check
python3 scripts/sync_generated.py --write
```

Skróty wgrywania, monitora szeregowego i sondy debugowej dotyczą wyłącznie projektów firmware. Celowo nie są zdefiniowane dla otwartego katalogu głównego repozytorium.

## Projekty firmware i VS Code

Utwórz projekt, sprawdź jego konfigurację i skompiluj go następującymi poleceniami:

```bash
./vscode/tools/create-vscode-example.py --output /path/to/project
./vscode/entry/jh-vscode config-dump --project /path/to/project
./vscode/entry/jh-vscode build --project /path/to/project
```

Wygenerowane projekty udostępniają zadania kompilacji, wgrywania, monitorowania, debugowania, OTA i testów dostosowane do płytki. Szczegóły opisują [Praca z projektem firmware](FwProjectWorkflow.md) i [Integracja VS Code](../../vscode/README.pl.md).
