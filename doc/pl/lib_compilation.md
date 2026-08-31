# Build biblioteki JaszczurHAL

*Dostępne również [po angielsku](../en/lib_compilation.md).*

## TL;DR

```bash
./scripts/build_rp_native_lib.sh --target rp2040
./scripts/build_rp_native_lib.sh --target rp2350-arm
./scripts/build_rp_native_lib.sh --target rp2350-riscv
./scripts/build_stm32_lib.sh
python3 scripts/build_esp_idf.py build \
  --project tests/fixtures/esp32s3_phase3 --clean
```

> **Część [Dokumentacji API JaszczurHAL](JaszczurHAL_API.md)**

JaszczurHAL używa CMake do buildów hosta, RP i STM32. Ścieżka
ESP32-S3 wywołuje przypięty system buildu ESP-IDF przez kontrolowany
runner Pythona. Buildy embedded wybierają target i fizyczną płytkę z
deklaratywnego rejestru opisanego w
[Profilach targetów i płytek](boards_profiles_howto.md).

| Target | Domyślna płytka | Wejście buildu | Selektor backendu |
|---|---|---|---|
| Mock hosta | - | CMake w katalogu głównym repozytorium | `HAL_TARGET_MOCK` |
| RP2040 | `pico` | `rp_native_lib/` | `HAL_TARGET_RP2040` |
| RP2350 ARM | `pico2` | `rp_native_lib/` | `HAL_TARGET_RP2350_ARM` |
| RP2350 RISC-V | `pico2` | `rp_native_lib/` | `HAL_TARGET_RP2350_RISCV` |
| STM32G474 | `nucleo-g474re` | `stm32_lib/` | `HAL_TARGET_STM32G474` |
| ESP32-S3 | `waveshare-esp32-s3-zero` | kontrolowany build komponentu ESP-IDF | `HAL_TARGET_ESP32_S3` |

Artefakty wytworzone przez repozytorium pozostają poniżej `.build/`.
Skrypty pomocnicze odrzucają ścieżkę wyjściową poza tym katalogiem.

## Zgodność targetu i płytki

Publiczne selektory targetu żyją w `src/hal/core/hal_target.h`. Zdefiniuj
dokładnie jeden selektor, gdy toolchain nie dostarcza wystarczających
informacji do automatycznego wykrycia:

```c
#define HAL_TARGET_RP2040
#define HAL_TARGET_RP2350_ARM
#define HAL_TARGET_RP2350_RISCV
#define HAL_TARGET_STM32G474
#define HAL_TARGET_ESP32_S3
#define HAL_TARGET_MOCK
```

`JH_TARGET` identyfikuje procesor i platformę wykonawczą. `JH_BOARD`
identyfikuje profil fizycznej płytki. Drzewo źródłowe śledzi wygenerowany
globalny rejestr i fallback pod `src/hal/generated/`. Każdy build
generuje:

```text
include/generated/
  jh_board_config.h
  jh_link_contract.h
```

Wygenerowany symbol zgodności ma postać
`jh_board_contract_<target>_<board>_<featureHash>`. Sprawia to, że
niedopasowane biblioteki, nagłówki płytki i zestawy funkcji zawodzą podczas
linkowania. Trzymaj `libJaszczurHAL.a` razem z wygenerowanymi nagłówkami z
tego samego buildu.

Rozwiązywanie funkcji produkcyjnych rozróżnia:

- `requestedFeatures`: bezpośrednie żądania zebrane z wejść definicji CMake
  oraz `hal_project_config.h`;
- `resolvedFeatures`: posortowane przechodnie domknięcie rejestru używane
  do wyboru źródeł, zależności i sygnatury linkowania.

Wpisy funkcji mogą też deklarować dodatkowe `buildEffects`. Wygenerowane
dane CMake wybierają źródła należące do funkcji oraz zarządzane manifesty
źródeł BearSSL, LittleFS lub SX126x dla RP i STM32. ESP-IDF używa
przenośne efekty źródłowe z tego samego rejestru i dodaje lokalnie tylko
swoje specyficzne dla ESP32 pliki backendu. Fakty o płytce, adaptery
targetu, układ flash i specjalne obrazy firmware'u pozostają własnością
swoich odpowiednich receptur buildu.

Konkretny target może dodać wymaganą funkcję. ESP32-S3 zawsze dodaje
`HAL_ENABLE_FREERTOS` z pochodzeniem targetu, ponieważ ESP-IDF uruchamia
swój scheduler przed `app_main()`.

Rozwiązany JSON płytki przechowuje oba zestawy oraz pełny skrót ich
domknięcia. Jego pole `features` pozostaje aliasem `resolvedFeatures`.
12-znakowy `featureHash` to SHA-256 nad `hal.profileId`, po którym
następuje posortowane rozwiązane domknięcie, z nazwami funkcji
serializowanymi jako `=1`. Redundantne bezpośrednie żądania, które nie
zmieniają domknięcia, nie zmieniają więc sygnatury archiwum. Ten sam JSON
zapisuje `boardCompileDefinitions`; wygenerowany CMake udostępnia je jako
`JH_BOARD_COMPILE_DEFINITIONS`, natomiast `jh_board_config.h` zapisuje je dla
projektów korzystających bezpośrednio z kompilatora.

Dwie warunkowe reguły pozostają poza rejestrem v1: EEPROM AT24C256 może
dodać I2C, a GPS może dodać UART, gdy nie zażądano żadnego transportu
szeregowego. Pozostają one w `hal_config.h` i są poza równoważnością
featureHash. Sprawdzenia targetu, płytki, providera, capabilities i
parametrów strojenia również tam pozostają.

Waliduj wejścia funkcji ściśle przed buildem wydania:

```bash
python3 scripts/generate_hal_features.py --lint --input-root .
python3 scripts/generate_hal_features.py \
  --lint --effective --input-root . \
  --resolution-output .build/effective-feature-resolution.json
```

Obie komendy kończą się niepowodzeniem, gdy znajdą nieprawidłową
konfigurację. `--report-only` jest dostępne do tymczasowego audytu
migracji, nie do normalnej bramki jakości.

## Zainstalowany pakiet i bezpośrednie użycie kompilatora

Po skonfigurowaniu i zbudowaniu dowolnego wejścia embedded biblioteki
statycznej, utwórz kompletną, pasującą instalację JaszczurHAL:

```bash
cmake --install .build/static/<target>/<board> \
  --prefix .build/install/<target>/<board>
```

Odpowiednie zainstalowane pliki to:

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

Wszystkie pozostałe publiczne nagłówki HAL są instalowane pod `include/`.
Traktuj to drzewo jako jedną jednostkę. Dla bezpośredniego buildu
kompilatorem dodaj `include/` i `include/generated/` do ścieżki include,
kompiluj z selektorem targetu i bezpośrednimi żądaniami zapisanymi w
`jh_board_resolved.json`, skompiluj
`share/JaszczurHAL/generated/jh_link_contract_reference.c` i zlinkuj ten
obiekt z `lib/libJaszczurHAL.a`. Na przykład, kształt komendy jest
następujący:

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

Gdy zapisane bezpośrednie żądania obejmują
`HAL_ENABLE_STACK_PROTECTOR`, dodaj `-fstack-protector-strong` do każdej
buildu C/C++ aplikacji. Natywne receptury CMake firmware'u propagują to
automatycznie. Zainstalowane archiwum zawiera już pasujące środowisko
runtime `__stack_chk_guard` / `__stack_chk_fail`; nie dostarczaj
drugiego środowiska stack-protector.

`hal_config.h` dołącza zainstalowany wygenerowany nagłówek funkcji, więc
bezpośredni kompilator otrzymuje to samo rozwiązane domknięcie bez
uruchamiania Pythona. Zainstalowany `jh_board_config.h` dostarcza również
każdą definicję płytki/providera wymienioną w
`jh_board_resolved.json.boardCompileDefinitions`, w tym wybory backendu
radiowego, magistrali, stosu i pinów. Przekazuj w wierszu poleceń tylko
selektor targetu i zapisane bezpośrednie żądania funkcji; nie powtarzaj
tych definicji należących do płytki opcjami `-D`. Wygenerowana referencja
używa korzenia `constructor, used` GCC/Clang, więc sygnatura
płytki/funkcji pozostaje żywa pod `--gc-sections`, gdy wspierany skrypt
linkera zachowuje tablice konstruktorów. SDK targetu, obiekty startowe,
skrypt linkera i biblioteki platformy pozostają częścią normalnych wymagań
toolchainu targetu.

## Mock hosta

Projekt w katalogu głównym repozytorium buduje deterministyczny backend
mock oraz jego pliki wykonywalne testów:

```bash
cmake -S . -B .build/host
cmake --build .build/host --parallel
ctest --test-dir .build/host --output-on-failure
```

Build hosta wymaga natywnego toolchainu C/C++ oraz CMake. Nie
wymaga wbudowanego SDK ani kompilatora krzyżowego.

## RP2040 i RP2350

Buildy RP używają przypiętego oficjalnego Pico SDK, wygenerowanego
profilu płytki oraz wejścia aplikacji należącego do HAL.

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

# Tylko linkowalna biblioteka statyczna, bez sond firmware'u
./scripts/build_rp_native_lib.sh --target rp2040 --library-only
```

Główne opcje to:

| Opcja | Znaczenie |
|---|---|
| `--target NAME` | `rp2040`, `rp2350-arm` lub `rp2350-riscv` |
| `--board NAME` | Profil płytki zgodny z wybranym targetem |
| `--example NAME` | Zbuduj `examples/NAME` jako firmware |
| `--example-source FILE` | Wybierz jedno źródło z przykładu wieloprofilowego (powtarzalne) |
| `--freertos` | Włącz przypięte jądro FreeRTOS SMP |
| `--library-only` | Zbuduj wyłącznie linkowalny target `libJaszczurHAL.a`, bez sond firmware'u |
| `-p`, `--project-config DIR` | Katalog zawierający `hal_project_config.h` |
| `-D KEY=VALUE` | Dodatkowa definicja HAL; powtarzalne |
| `--sdk-dir PATH` | Checkout Pico SDK |
| `--toolchain PATH` | Katalog główny toolchainu krzyżowego |
| `--picotool-dir PATH` | Checkout źródeł `picotool` |
| `-o`, `--output DIR` | Katalog buildu poniżej `.build/` |
| `--clean` | Odtwórz wybrany katalog buildu |
| `-j`, `--jobs N` | Liczba równoległych zadań buildu |

Domyślne wyjście to `.build/static/<target>/<board>/`. Domyślny build
weryfikuje bibliotekę statyczną oraz kompletny zestaw sond ELF/BIN/UF2;
build `--library-only` weryfikuje tylko archiwum:

```text
.build/static/<target>/<board>/
  libJaszczurHAL.a
  include/generated/
  jh_rp_native_artifact_probe.{elf,bin,uf2}
  jh_rp_native_core1_probe.{elf,bin,uf2}
  jh_rp_native_firmware.{elf,bin,uf2}  # with --example
```

Sonda core-1 weryfikuje wejście aplikacji oraz symbole multicore. W
buildzie bare-metal `app_task1()` działa na rdzeniu 1 Pico SDK. W buildzie
FreeRTOS HAL tworzy zadania związane z powinowactwem (affinity) i uruchamia
scheduler.

### Bezpośredni build CMake

Skrypt pomocniczy przygotowuje przypięte zależności i dostarcza zmienne
cache. Gdy wybrano `HAL_ENABLE_FREERTOS`, bezpośrednie CMake wywołuje
`scripts/component_manager.py`, aby przygotować lub zweryfikować
FreeRTOS-Kernel. Zewnętrzny `JH_FREERTOS_KERNEL_DIR` jest weryfikowany i
nigdy nie zastępowany. Po obecności pozostałych zależności, równoważna
podstawowa konfiguracja RP2040 wygląda tak:

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
`app_task1()`. `src/hal_app_entry.cpp` zawiera `main()` i mapuje te hooki
na wybrany model wykonania bare-metal lub FreeRTOS.

### Osadzanie wsparcia CMake dla RP

Projekty firmware oparte na dispatcherze używają
`cmake/targets/rp-native.cmake`. Niestandardowy projekt CMake Pico SDK może
użyć tej samej integracji:

```cmake
include(path/to/JaszczurHAL/cmake/jh_rp_native_sdk.cmake)

add_executable(firmware
    app.cpp
)
jh_add_rp_native_firmware(firmware)
```

Pomocnik dołącza HAL, wygenerowane metadane płytki, wybrane biblioteki
Pico SDK, układ linkera, wejście aplikacji oraz przetwarzanie końcowe
ELF/BIN/UF2.

Układ flash, pamięć trwała, sloty OTA i własność RAM są udokumentowane w
[Mapie pamięci RP](../../rp_native_lib/MEMORY_MAP.md).

## STM32G474

Build STM32G474 tworzy bibliotekę statyczną dla wygenerowanego profilu
płytki:

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

Domyślne wyjście to:

```text
.build/static/stm32g474/nucleo-g474re/
  libJaszczurHAL.a
  include/generated/
```

Bezpośrednia konfiguracja CMake używa dostarczonego toolchainu:

```bash
cmake -S stm32_lib -B .build/manual/stm32g474-nucleo \
  -DCMAKE_TOOLCHAIN_FILE=stm32_lib/toolchain_stm32g474.cmake \
  -DJH_TARGET=stm32g474 \
  -DJH_BOARD=nucleo-g474re
cmake --build .build/manual/stm32g474-nucleo --parallel
```

Ten sam backend kompiluje się też kompilatorem hosta dla kontroli
poprawności (sanity check) oraz bazy danych buildu clang-tidy dla
STM32. Ten tryb pozostawia `JH_STM32G474_HW` niezdefiniowane i jest
opt-in, więc brakujący toolchain krzyżowy nie może po cichu wytworzyć
biblioteki hosta zamiast firmware'u:

```bash
cmake -S stm32_lib -B .build/manual/stm32g474-host \
  -DJH_STM32_HOST_SANITY=ON \
  -DCMAKE_EXPORT_COMPILE_COMMANDS=ON
cmake --build .build/manual/stm32g474-host --parallel
```

Bez `CMAKE_TOOLCHAIN_FILE` lub `JH_STM32_HOST_SANITY` konfiguracja
zatrzymuje się i nazywa obie opcje. Wygenerowane pliki płytki pozostają
wewnątrz drzewa buildu CMake, więc trzymaj katalog buildu pod
katalogiem głównym `.build`.

Przekazuj funkcje projektu przez `EXTRA_HAL_DEFINES` lub użyj
`scripts/build_stm32_lib.sh -D ...`. `HAL_ENABLE_FREERTOS` wybiera
przypiętą integrację jądra. Bezpośrednie CMake wywołuje
`scripts/component_manager.py`, aby przygotować lub zweryfikować jądro.
Pomocnik powłoki wywołuje `scripts/ensure_freertos_kernel.sh` dla
`--freertos` lub jawnego `-D HAL_ENABLE_FREERTOS`; funkcja znaleziona
tylko w `hal_project_config.h` jest przygotowywana przez fallback CMake.
Wrapper deleguje do tego samego menedżera. Zewnętrzny
`JH_FREERTOS_KERNEL_DIR` jest weryfikowany i nigdy nie zastępowany.
Firmware bare-metal wywołuje wygenerowane wejście aplikacji HAL w pętli
kooperacyjnej; firmware FreeRTOS używa zadań zarządzanych przez scheduler.

Linkowanie firmware'u musi zawierać wygenerowany obiekt referencyjny
sygnatury linkowania i używać pasującej konfiguracji linkera. Jego korzeń
konstruktora utrzymuje referencję żywą, gdy `--gc-sections` jest włączone;
brakujące lub niedopasowane archiwum nadal więc zawodzi z oczekiwanym
niezdefiniowanym symbolem zgodności. Zobacz
[Mapę pamięci STM32G474](../../stm32_lib/MEMORY_MAP.md) po rezerwacje
flash, SRAM, pamięci trwałej i OTA.

## ESP32-S3 z ESP-IDF

Build ESP32-S3 jest częścią workflow projektu firmware, a nie zainstalowanym
pakietem `libJaszczurHAL.a`. Produkcyjnym punktem wejścia jest
`scripts/build_esp_idf.py`; akceptuje `build`, `artifacts` i `flash`:

```bash
# Czysty build z domyślną płytką targetu.
python3 scripts/build_esp_idf.py build \
  --project tests/fixtures/esp32s3_phase3 \
  --target esp32s3 --board waveshare-esp32-s3-zero --clean

# Ponowna walidacja istniejącego buildu bez jego uruchamiania.
python3 scripts/build_esp_idf.py artifacts \
  --project tests/fixtures/esp32s3_phase3 \
  --target esp32s3 --board waveshare-esp32-s3-zero

# Rewalidacja, a następnie flashowanie projektu aplikacji pod przesunięciami z manifestu.
python3 scripts/build_esp_idf.py flash \
  --project path/to/esp32-project \
  --target esp32s3 --board waveshare-esp32-s3-zero \
  --port /dev/serial/by-id/<Espressif-USB-Serial-JTAG-device>
```

`tests/fixtures/esp32s3_phase3` to fixture buildu/konsolidacji
używany przez CI oraz Bramkę 8. Wybiera każdy backend ESP32-S3 dostarczony
przez Fazę 3 i dowodzi rozwiązywania funkcji, wyboru komponentów,
buildu, linkowania, generowania partycji i publikacji artefaktów. Nie
jest to sprzętowy fixture do testu działania.

Domyślny katalog buildu to
`<project>/.build/esp-idf/esp32s3/waveshare-esp32-s3-zero/`. `--output`
może wybrać inną lokalizację poniżej katalogu głównego `.build` projektu
lub repozytorium. Powtarzalne argumenty `--source` zastępują automatyczne
wykrywanie; bez nich runner obejmuje wspierane pliki źródłowe w katalogu
głównym projektu oraz rekurencyjnie pod `src/`. Powtarzalne argumenty
`--feature` i `--define` rozszerzają konfigurację projektu. `--idf-dir`
lub `JH_ESP_IDF_DIR` wybiera zewnętrznie zarządzany checkout dopiero po
tym, jak jego dokładne przypięcie i narzędzia przejdą weryfikację.

Runner generuje domyślne wartości `sdkconfig` dla flash/PSRAM pochodne od
płytki, buduje źródła projektu z małym komponentem integracyjnym
JaszczurHAL i waliduje wynik przed opublikowaniem
`jh_esp_idf_artifacts.json`. Manifest zawiera względne ścieżki dla ELF,
MAP, BIN aplikacji, bootloadera, tabeli partycji, bazy danych buildu,
wygenerowanych metadanych płytki/linkowania oraz logów. Jego uporządkowane
`flashImages` zachowują każde przesunięcie, rozmiar i SHA-256. Pochodzenie
konfiguracji obejmuje finalny skrót `sdkconfig` oraz wybrany profil
partycji; pochodzenie toolchainu obejmuje przypiętą wersję/commit ESP-IDF,
rzeczywisty kompilator, wersje CMake, Ninja, IDF Python i esptool oraz
skrót `tools.json` ESP-IDF.

Target zawsze rozwiązuje `HAL_ENABLE_FREERTOS` i akceptuje dostarczony
zestaw flag peryferiów Fazy 2 wraz z grafem sieci/usług Fazy 3. Źródła
systemu, synchronizacji, GPIO, ADC, prostego PWM oraz portu
szeregowego/debugowania i timera są częścią komponentu bazowego. Żądane
lub przechodnio rozwiązane funkcje spoza tej dozwolonej listy deskryptora
zawodzą z `[JH-CFG-UNSUPPORTED]`.
Wygenerowany plik CMake projektu niesie rozwiązaną listę funkcji, listę
źródeł komponentu oraz publiczne/prywatne zależności komponentów ESP-IDF;
receptura komponentu używa tych wygenerowanych list zamiast utrzymywać
drugi graf źródeł.
`scripts/build_esp_idf_phase0.py` pozostaje wrapperem zgodności dla
izolowanego fixture Fazy 0.

## Workspace repozytorium i VS Code

Otwórz katalog główny repozytorium JaszczurHAL jako folder VS Code, aby
używać workflow biblioteki statycznej. Jest on oddzielny od workflow projektu
firmware i używa globalnych etykiet zadań współdzielonych przez projekty
JaszczurHAL:

| Skrót | Zadanie repozytorium |
|---|---|
| `Ctrl+Shift+1` | `Project: Build` |
| `Ctrl+Shift+6` | `Project: Refresh IntelliSense` |
| `Ctrl+Shift+7` | `Project: Clean` |
| `Ctrl+Shift+0` | `Project: Install library` |
| `Ctrl+Shift+Alt+1` | `Project: Select board (GUI)` |
| `Ctrl+Shift+Alt+2` | `Project: Select board` |

Początkowy profil to `rp2040:pico`. Wybór odczytuje dane targetu i płytki
z `boards/` i jest przechowywany w ignorowanym przez git
`.vscode/jaszczurhal.library.local.json`. Wspierane profile obejmują mock
hosta, wszystkie trzy natywne rodziny targetów RP oraz STM32G474.
Artefakty buildu pozostają w:

```text
.build/vscode/library/<target>/<board>/
```

`Project: Build` produkuje aktywne linkowalne archiwum i wybiera jego
wygenerowaną bazę danych buildu dla cpptools. `Project: Refresh
IntelliSense` wykonuje jawnie ten sam build przyrostowy przed
ponownym zapisaniem ignorowanego przez git `.vscode/c_cpp_properties.json`.
Każdy build produkcyjny zawiera `libJaszczurHAL.a`; profil mock
zawiera `libhal_mock.a`.

`Project: Install library` buduje aktywny profil produkcyjny i instaluje
jego archiwum, publiczne nagłówki, wygenerowane nagłówki płytki oraz dane
sygnatury linkowania do `.build/install/<target>/<board>/`. Target mock
nie ma interfejsu instalacji. `Project: Clean` usuwa tylko te dwa katalogi
dla aktywnego profilu oraz jego pasujący zarządzany plik IntelliSense.
Zachowuje inne buildy, zarządzane narzędzia i źródła zależności.

Śledzone pliki `.vscode` w katalogu głównym są pochodne rejestru płytek.
Zweryfikuj lub wygeneruj ponownie wszystkie śledzone wygenerowane
artefakty po zmianie rejestru lub zadania workspace'u:

```bash
python3 scripts/sync_generated.py --check
python3 scripts/sync_generated.py --write
```

Skróty wgrywania, monitora szeregowego i sondy debugowania pozostają
wyłącznie dla firmware'u i są celowo niezdefiniowane, gdy otwarty jest
katalog główny repozytorium.

## Projekty firmware i VS Code

Utwórz, sprawdź i zbuduj projekt przez utrzymywany workflow:

```bash
./vscode/tools/create-vscode-example.py --output /path/to/project
./vscode/entry/jh-vscode config-dump --project /path/to/project
./vscode/entry/jh-vscode build --project /path/to/project
```

Wygenerowane projekty udostępniają zadania buildu, wgrywania,
monitorowania, debugowania, OTA i testowania świadome płytki. Szczegóły
znajdują się w [Workflow projektu firmware](FwProjectWorkflow.md)
oraz [Integracji VS Code](../../vscode/README.md).
