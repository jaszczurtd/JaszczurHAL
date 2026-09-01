# Kompilacja biblioteki JaszczurHAL

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

JaszczurHAL używa CMake do buildów dla hosta, RP i STM32. W przypadku ESP32-S3
skrypt Pythona zarządzany w repozytorium uruchamia system buildu ESP-IDF w
określonej tam wersji. Podczas buildów dla systemów wbudowanych wybiera się
target oraz fizyczną płytkę z deklaratywnego rejestru opisanego w
[Profilach targetów i płytek](boards_profiles_howto.md).

| Target | Domyślna płytka | Punkt wejścia buildu | Selektor backendu |
|---|---|---|---|
| Mock hosta | - | CMake w katalogu głównym repozytorium | `HAL_TARGET_MOCK` |
| RP2040 | `pico` | `rp_native_lib/` | `HAL_TARGET_RP2040` |
| RP2350 ARM | `pico2` | `rp_native_lib/` | `HAL_TARGET_RP2350_ARM` |
| RP2350 RISC-V | `pico2` | `rp_native_lib/` | `HAL_TARGET_RP2350_RISCV` |
| STM32G474 | `nucleo-g474re` | `stm32_lib/` | `HAL_TARGET_STM32G474` |
| ESP32-S3 | `waveshare-esp32-s3-zero` | build komponentu ESP-IDF zarządzany przez skrypt | `HAL_TARGET_ESP32_S3` |

Wszystkie artefakty tworzone przez repozytorium są zapisywane w `.build/`.
Skrypty pomocnicze odrzucają ścieżki wyjściowe prowadzące poza ten katalog.

## Zgodność targetu i płytki

Publiczne selektory targetu znajdują się w `src/hal/core/hal_target.h`. Zdefiniuj
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
identyfikuje profil fizycznej płytki. W katalogu `src/hal/generated/` drzewo
źródłowe zawiera wygenerowany globalny rejestr i konfigurację zastępczą. Każdy
build generuje:

```text
include/generated/
  jh_board_config.h
  jh_link_contract.h
```

Wygenerowany symbol zgodności ma postać
`jh_board_contract_<target>_<board>_<featureHash>`. Dzięki niemu linkowanie
kończy się błędem, jeśli biblioteka, nagłówki płytki i zestaw funkcji nie
pochodzą z tej samej konfiguracji. Przechowuj `libJaszczurHAL.a` razem
z wygenerowanymi nagłówkami z tego samego buildu.

Proces rozwiązywania zależności funkcji dla buildu produkcyjnego rozróżnia:

- `requestedFeatures`: bezpośrednie żądania zebrane z definicji przekazanych do
  CMake oraz z `hal_project_config.h`;
- `resolvedFeatures`: posortowane domknięcie przechodnie wyznaczone na
  podstawie rejestru, używane do wyboru źródeł, zależności i sygnatury
  linkowania.

Definicje funkcji mogą też deklarować dodatkowe `buildEffects`. Wygenerowane
dane CMake wybierają źródła przypisane do funkcji oraz zarządzane przez
JaszczurHAL manifesty źródeł BearSSL, LittleFS lub SX126x dla RP i STM32.
ESP-IDF korzysta z tych samych wpisów `buildEffects` dotyczących przenośnych
źródeł i dodaje lokalnie tylko pliki backendu właściwe dla ESP32. Dane płytki,
adaptery targetu, układ pamięci flash i specjalne obrazy firmware'u są definiowane
przez odpowiednie receptury buildu.

Konkretny target może wymusić dodatkową funkcję. ESP32-S3 zawsze dodaje
`HAL_ENABLE_FREERTOS` i zapisuje, że żądanie pochodzi od targetu, ponieważ
ESP-IDF uruchamia scheduler przed `app_main()`.

Wygenerowany plik JSON z konfiguracją płytki przechowuje oba zestawy oraz pełny
skrót ich domknięcia. Pole `features` pozostaje aliasem `resolvedFeatures`.
12-znakowy `featureHash` jest skrótem SHA-256 obliczanym dla `hal.profileId`
i posortowanego wynikowego domknięcia; nazwy funkcji są zapisywane z wartością
`=1`. Nadmiarowe bezpośrednie żądania, które nie zmieniają domknięcia, nie
zmieniają więc sygnatury archiwum. Ten sam JSON
zapisuje `boardCompileDefinitions`; wygenerowany CMake udostępnia je jako
`JH_BOARD_COMPILE_DEFINITIONS`, natomiast `jh_board_config.h` udostępnia je
projektom korzystającym bezpośrednio z kompilatora.

Dwie warunkowe reguły pozostają poza rejestrem v1: EEPROM AT24C256 może
dodać I2C, a GPS może dodać UART, gdy nie zażądano żadnego transportu
szeregowego. Pozostają one w `hal_config.h` i nie są uwzględniane przy
porównywaniu konfiguracji za pomocą `featureHash`. Sprawdzenia targetu, płytki,
systemu budowania, cech sprzętowych i regulowanych parametrów konfiguracyjnych
również pozostają w tym pliku.

Przed buildem wydania przeprowadź ścisłą walidację żądanych funkcji:

```bash
python3 scripts/generate_hal_features.py --lint --input-root .
python3 scripts/generate_hal_features.py \
  --lint --effective --input-root . \
  --resolution-output .build/effective-feature-resolution.json
```

Oba polecenia kończą się niepowodzeniem, gdy znajdą nieprawidłową
konfigurację. Opcja `--report-only` służy do tymczasowego audytu migracji i nie
powinna zastępować standardowej kontroli jakości.

## Zainstalowany pakiet i bezpośrednie użycie kompilatora

Po skonfigurowaniu i zbudowaniu jednego z punktów wejścia biblioteki statycznej
dla systemów wbudowanych utwórz odpowiadającą mu, kompletną instalację
JaszczurHAL:

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

Wszystkie pozostałe publiczne nagłówki HAL są instalowane w `include/`.
Traktuj całe drzewo instalacji jako jedną całość. Jeśli wywołujesz kompilator
bezpośrednio, dodaj `include/` i `include/generated/` do ścieżki wyszukiwania
nagłówków. Kompiluj z selektorem targetu i bezpośrednimi żądaniami zapisanymi
w `jh_board_resolved.json`. Skompiluj również
`share/JaszczurHAL/generated/jh_link_contract_reference.c`, a powstały plik
obiektowy zlinkuj z `lib/libJaszczurHAL.a`. Przykładowy układ poleceń wygląda
następująco:

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
`HAL_ENABLE_STACK_PROTECTOR`, dodaj `-fstack-protector-strong` podczas
kompilowania każdego pliku C/C++ aplikacji. Natywne receptury CMake firmware'u
dodają tę flagę automatycznie. Zainstalowane archiwum zawiera już odpowiednią
implementację runtime'u `__stack_chk_guard` / `__stack_chk_fail`; nie dodawaj
drugiej implementacji mechanizmu stack protector.

`hal_config.h` dołącza zainstalowany, wygenerowany nagłówek z listą funkcji.
Dzięki temu bezpośrednio wywołany kompilator otrzymuje ten sam wynik rozwiązania
zależności bez uruchamiania Pythona. Zainstalowany `jh_board_config.h`
udostępnia również wszystkie definicje związane z płytką i backendem wymienione
w `jh_board_resolved.json.boardCompileDefinitions`, w tym ustawienia backendu
radiowego, magistrali, stosu i pinów. W wierszu poleceń podawaj tylko selektor
targetu i zapisane bezpośrednie żądania funkcji. Nie powtarzaj za pomocą opcji
`-D` definicji pochodzących z profilu płytki. Wygenerowany kod z odwołaniem do
sygnatury wykorzystuje atrybuty GCC/Clang `constructor, used`, dlatego
`--gc-sections` nie usuwa sygnatury płytki i funkcji, jeśli obsługiwany skrypt
linkera zachowuje tablice konstruktorów. SDK targetu, obiekty startowe, skrypt
linkera i biblioteki platformy są nadal standardowymi wymaganiami toolchainu
targetu.

## Mock hosta

Projekt w katalogu głównym repozytorium buduje deterministyczny backend mocka
oraz pliki wykonywalne testów:

```bash
cmake -S . -B .build/host
cmake --build .build/host --parallel
ctest --test-dir .build/host --output-on-failure
```

Build hosta wymaga natywnego toolchainu C/C++ oraz CMake. Nie wymaga
wbudowanego SDK ani kompilatora krzyżowego.

## RP2040 i RP2350

Buildy RP używają oficjalnego Pico SDK w wersji wskazanej przez repozytorium,
wygenerowanego profilu płytki oraz punktu wejścia aplikacji dostarczanego przez
HAL.

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
| `-o`, `--output DIR` | Katalog buildu poniżej `.build/` |
| `--clean` | Utwórz od nowa wybrany katalog buildu |
| `-j`, `--jobs N` | Liczba równoległych zadań buildu |

Domyślny katalog wyjściowy to `.build/static/<target>/<board>/`. Domyślny build
weryfikuje bibliotekę statyczną oraz kompletny zestaw kontrolnych plików
ELF/BIN/UF2;
build `--library-only` weryfikuje tylko archiwum:

```text
.build/static/<target>/<board>/
  libJaszczurHAL.a
  include/generated/
  jh_rp_native_artifact_probe.{elf,bin,uf2}
  jh_rp_native_core1_probe.{elf,bin,uf2}
  jh_rp_native_firmware.{elf,bin,uf2}  # z opcją --example
```

Program kontrolny dla rdzenia 1 weryfikuje punkt wejścia aplikacji oraz
symbole obsługi wielu rdzeni. W buildzie bare-metal `app_task1()` działa na
rdzeniu 1 udostępnianym przez Pico SDK. W buildzie FreeRTOS HAL tworzy zadania
przypisane do konkretnych rdzeni (CPU affinity) i uruchamia scheduler.

### Bezpośredni build CMake

Skrypt pomocniczy przygotowuje zależności w wersjach wskazanych przez
repozytorium i przekazuje zmienne cache'u CMake. Gdy wybrano
`HAL_ENABLE_FREERTOS`, bezpośrednie wywołanie CMake uruchamia
`scripts/component_manager.py`, aby przygotować lub zweryfikować
FreeRTOS-Kernel. Zewnętrzny `JH_FREERTOS_KERNEL_DIR` jest weryfikowany i
nigdy nie jest zastępowany. Gdy pozostałe zależności są już dostępne,
równoważna podstawowa konfiguracja RP2040 wygląda tak:

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

Projekty firmware budowane przez centralny mechanizm wyboru targetu używają
pliku `cmake/targets/rp-native.cmake`. Niestandardowy projekt Pico SDK oparty
na CMake może użyć tej samej integracji:

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

Domyślny katalog wyjściowy ma następującą zawartość:

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

Ten sam backend można również skompilować kompilatorem hosta w celu
podstawowej kontroli poprawności oraz utworzenia bazy poleceń kompilacji dla
clang-tidy na STM32. Ten tryb pozostawia `JH_STM32G474_HW` niezdefiniowane i
wymaga jawnego włączenia, więc brakujący toolchain krzyżowy nie może po cichu
wytworzyć biblioteki hosta zamiast firmware'u:

```bash
cmake -S stm32_lib -B .build/manual/stm32g474-host \
  -DJH_STM32_HOST_SANITY=ON \
  -DCMAKE_EXPORT_COMPILE_COMMANDS=ON
cmake --build .build/manual/stm32g474-host --parallel
```

Bez `CMAKE_TOOLCHAIN_FILE` ani `JH_STM32_HOST_SANITY` konfiguracja kończy się
błędem wskazującym obie możliwości. Wygenerowane pliki płytki pozostają
wewnątrz drzewa buildu CMake, więc katalog buildu należy umieścić pod
katalogiem głównym `.build`.

Przekazuj funkcje projektu przez `EXTRA_HAL_DEFINES` lub użyj
`scripts/build_stm32_lib.sh -D ...`. `HAL_ENABLE_FREERTOS` wybiera integrację
z jądrem w wersji wskazanej przez repozytorium. Bezpośrednie wywołanie CMake
uruchamia
`scripts/component_manager.py`, aby przygotować lub zweryfikować jądro.
Pomocnik powłoki wywołuje `scripts/ensure_freertos_kernel.sh` dla
`--freertos` lub jawnego `-D HAL_ENABLE_FREERTOS`; jeśli ta funkcja HAL została
zażądana wyłącznie w `hal_project_config.h`, zależność przygotowuje mechanizm
rezerwowy w CMake. Skrypt zgodności korzysta z tego samego menedżera.
Zewnętrzny `JH_FREERTOS_KERNEL_DIR` jest weryfikowany i nigdy nie jest
zastępowany.
Firmware bare-metal wywołuje wygenerowany punkt wejścia aplikacji HAL w pętli
kooperacyjnej; w firmware FreeRTOS zadania uruchamia scheduler.

Podczas linkowania firmware'u trzeba dołączyć wygenerowany obiekt z odwołaniem
do sygnatury linkowania i użyć pasującej konfiguracji linkera. Wpis w sekcji
konstruktorów zachowuje to odwołanie przy włączonym `--gc-sections`. Brakujące
lub niedopasowane archiwum nadal powoduje oczekiwany błąd niezdefiniowanego
symbolu zgodności. Zobacz
[Mapę pamięci STM32G474](../../stm32_lib/MEMORY_MAP.md), aby sprawdzić rezerwacje
flash, SRAM, pamięci trwałej i OTA.

## ESP32-S3 z ESP-IDF

Build ESP32-S3 jest częścią procesu budowania projektu firmware, a nie
zainstalowanym pakietem `libJaszczurHAL.a`. Produkcyjnym punktem wejścia jest
`scripts/build_esp_idf.py`; obsługuje polecenia `build`, `artifacts` i `flash`:

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

`tests/fixtures/esp32s3_phase3` to konfiguracja testowa kompilacji i
linkowania używana przez CI oraz test Gate 8. Włącza wszystkie backendy
ESP32-S3 dostarczone w fazie 3 i sprawdza rozwiązywanie zależności funkcji,
dobór komponentów, build, linkowanie, generowanie partycji oraz publikację
artefaktów. Nie służy do testów działania na sprzęcie.

Domyślny katalog buildu to
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

Dla targetu zawsze włączane jest `HAL_ENABLE_FREERTOS`; akceptowany jest też
dostarczony zestaw flag peryferiów fazy 2 wraz z grafem zależności funkcji
sieciowych i usług fazy 3. Źródła obsługi systemu, synchronizacji, GPIO, ADC,
prostego PWM, portu szeregowego i debugowania oraz timera są częścią komponentu
bazowego. Żądane lub pośrednio włączone funkcje spoza listy dozwolonej w
deskryptorze powodują błąd `[JH-CFG-UNSUPPORTED]`.
Wygenerowany plik CMake projektu zawiera wynikową listę włączonych funkcji,
listę źródeł komponentu oraz publiczne/prywatne zależności komponentów ESP-IDF;
receptura komponentu korzysta z tych wygenerowanych list zamiast utrzymywać
drugi graf źródeł.
`scripts/build_esp_idf_phase0.py` pozostaje skryptem zgodności dla izolowanej
konfiguracji testowej fazy 0.

<a id="workspace-repozytorium-i-vs-code"></a>

## Repozytorium w VS Code

Otwórz katalog główny repozytorium JaszczurHAL jako folder VS Code, aby
korzystać z procesu budowania biblioteki statycznej. Jest on niezależny od
procesu budowania projektu firmware i używa globalnych etykiet zadań
stosowanych już w projektach korzystających z JaszczurHAL:

| Skrót | Zadanie repozytorium |
|---|---|
| `Ctrl+Shift+1` | `Project: Build` |
| `Ctrl+Shift+6` | `Project: Refresh IntelliSense` |
| `Ctrl+Shift+7` | `Project: Clean` |
| `Ctrl+Shift+0` | `Project: Install library` |
| `Ctrl+Shift+Alt+1` | `Project: Select board (GUI)` |
| `Ctrl+Shift+Alt+2` | `Project: Select board` |

Początkowy profil to `rp2040:pico`. Dane wybranego targetu i płytki są
odczytywane z `boards/`, a sam wybór jest zapisywany w ignorowanym przez Git
pliku `.vscode/jaszczurhal.library.local.json`. Obsługiwane profile obejmują
mock hosta, wszystkie trzy rodziny targetów RP obsługiwane natywnie oraz
STM32G474.
Artefakty buildu pozostają w:

```text
.build/vscode/library/<target>/<board>/
```

`Project: Build` tworzy linkowalne archiwum dla aktywnego profilu i wybiera jego
wygenerowaną bazę poleceń kompilacji do użycia przez cpptools.
`Project: Refresh IntelliSense` jawnie uruchamia ten sam build przyrostowy
przed ponownym zapisaniem ignorowanego przez Git
`.vscode/c_cpp_properties.json`.
Każdy build produkcyjny zawiera `libJaszczurHAL.a`; profil mock
zawiera `libhal_mock.a`.

`Project: Install library` buduje aktywny profil produkcyjny i instaluje
jego archiwum, publiczne nagłówki, wygenerowane nagłówki płytki oraz dane
sygnatury linkowania do `.build/install/<target>/<board>/`. Target mock
nie ma interfejsu instalacji. `Project: Clean` usuwa tylko te dwa katalogi
dla aktywnego profilu oraz pasujący plik IntelliSense utworzony dla tego
profilu. Pozostałe buildy, zarządzane narzędzia i źródła zależności pozostają
bez zmian.

Pliki `.vscode` z katalogu głównego, które są przechowywane w repozytorium,
generuje się na podstawie rejestru płytek. Po zmianie rejestru lub zadań
środowiska VS Code zweryfikuj albo wygeneruj ponownie wszystkie wygenerowane
pliki śledzone w repozytorium:

```bash
python3 scripts/sync_generated.py --check
python3 scripts/sync_generated.py --write
```

Skróty wgrywania, monitora szeregowego i sondy debugowej pozostają
wyłącznie dla firmware'u i są celowo niezdefiniowane, gdy otwarty jest
katalog główny repozytorium.

## Projekty firmware i VS Code

Utwórz, sprawdź i zbuduj projekt za pomocą utrzymywanego zestawu narzędzi:

```bash
./vscode/tools/create-vscode-example.py --output /path/to/project
./vscode/entry/jh-vscode config-dump --project /path/to/project
./vscode/entry/jh-vscode build --project /path/to/project
```

Wygenerowane projekty udostępniają zadania buildu, wgrywania, monitorowania,
debugowania, OTA i testowania skonfigurowane dla wybranej płytki. Szczegóły
znajdują się w dokumencie [Praca z projektem firmware](FwProjectWorkflow.md)
oraz [Integracji VS Code](../../vscode/README.pl.md).
