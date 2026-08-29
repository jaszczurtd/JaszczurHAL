# Zarządzane komponenty third-party

Ten katalog zawiera dwa rodzaje wpisów:

- śledzone pliki `*_version.conf`, które przypinają każdy zarządzany komponent;
- ignorowane przez Git instalacje źródeł, narzędzi i toolchainów odtwarzane z
  tych przypięć.

Synchronizacja wszystkich komponentów:

```bash
./third_party/update_components.sh
```

Updater pobiera brakujące komponenty i zastępuje instalację, której wersja lub
commit Git różni się od śledzonej konfiguracji. Sprawdzenie obecnych instalacji
bez zmian:

```bash
./third_party/update_components.sh --verify-only
```

`runmefirst.sh` uruchamia updater po instalacji zależności hosta Linux.
`runmefirst.ps1` używa tego samego managera komponentów w Pythonie dla native
Windows. Poszczególne helpery `scripts/ensure_*.sh` pozostają dostępnymi,
wyspecjalizowanymi launcherami zgodności Unix, natomiast centralny updater jest
normalnym punktem wejścia.

## Zależności źródłowe

| Komponent | Przypięcie | Zarządzany checkout | Przeznaczenie |
|-----------|-------------|---------------------|---------------|
| BearSSL | `bearssl_version.conf` | `BearSSL/` | Silnik TLS dla buildów host, RP i STM32 |
| cJSON | `cjson_version.conf` | `cJSON/` | Parser JSON i API utilities |
| LodePNG | `lodepng_version.conf` | `lodepng/` | Kodek PNG operujący na pamięci |
| TJpg_Decoder | `jpeg_version.conf` | `TJpg_Decoder/` | Mały rdzeń dekompresji JPEG dla wyjścia RGB565 |
| FatFs | `fatfs_version.conf` | `FatFs/` | Rdzeń filesystemu FAT dla wspólnego storage SD |
| Unity | `unity_version.conf` | `Unity/` | Framework testów hostowych i targetowych |
| lwIP | `lwip_version.conf` | `lwip/` | Stos TCP/IP integracji CYW43 JaszczurHAL |
| littlefs | `littlefs_version.conf` | `littlefs/` | Rdzeń filesystemu dla native storage RP i STM32G474 |
| BTstack | `btstack_version.conf` | `BTstack/` | Stos hosta BLE integracji Bluetooth CYW43 |
| Driver Semtech SX126x | `sx126x_driver_version.conf` | `sx126x_driver/` | Przenośny driver poleceń SX1261/SX1262 dla providera LoRa |
| FreeRTOS-Kernel | `freertos_core_version.conf` | `FreeRTOS-Kernel/` | Kernel FreeRTOS native RP SMP i STM32G474 |
| Pico SDK | `pico_sdk_version.conf` | `pico-sdk/` | Native SDK RP2040/RP2350 |
| ESP-IDF | `esp_idf_version.conf` | `esp-idf/` | Native SDK rodziny ESP32 i bootstrap narzędzi |
| picotool | `picotool_version.conf` | `picotool/` | Źródła native narzędzia uploadu/metadanych RP |

Wrappery integracji BearSSL, cJSON, LodePNG, JPEG i FatFs należące do
JaszczurHAL, wraz z konfiguracją portu lwIP, pozostają śledzone w tematycznych
katalogach `src/hal/network/`, `src/hal/codecs/` i `src/hal/storage/`.
Integracja Unity pozostaje w infrastrukturze testowej, a drzewa źródeł upstream
są zarządzane tutaj. Helpery cJSON, LodePNG, TJpg_Decoder i Unity wymagają
czystych checkoutów dokładnego commita; tryb verify-only odrzuca zmiany lokalne
i nieśledzone. Wymuszane jest też pochodzenie repozytorium, w tym należące do
projektu repozytoria BearSSL, LodePNG, FatFs i Unity. Checkout
`jaszczurtd/ff16` jest bezpośrednim mirrorem niezmienionego archiwum ChaN R0.16
i zastępuje zawodny download w runtime z `elm-chan.org`. Checkout littlefs jest
używany bezpośrednio przez przepisy CMake native RP i STM32G474; adaptery flash
właściwe dla targetu pozostają śledzone w katalogach backendów.

Checkout Semtech pozostaje czysty na dokładnym commicie `v2.5.0`. Jego śledzona
kopia licencji Clear BSD to `LICENSE.SX126X`. Integracja LoRa Stage 1 używa tylko
`sx126x.c` i `sx126x_driver_version.c`; opcjonalne źródła LR-FHSS i BPSK są
wyłączone do osobnego review.

Submoduły Pico SDK wymagane przez buildy native wymienia `PICO_SDK_SUBMODULES`
w `pico_sdk_version.conf`. JaszczurHAL celowo używa osobno przypiętego checkoutu
`lwip/` zamiast submodułu lwIP z SDK.

Zewnętrzne checkouty FreeRTOS i Pico SDK można wybrać przez udokumentowane
`JH_FREERTOS_KERNEL_DIR`, `JH_PICO_SDK_DIR` oraz opcje ścieżek helperów. Takie
ścieżki użytkownika są sprawdzane, ale nigdy zastępowane.

ESP-IDF jest przypięty do dokładnego commita wydania i pobierany na żądanie
przez `scripts/ensure_esp_idf.sh --enable`. Rekurencyjne submoduły należą do
sprawdzanego checkoutu. To samo polecenie idempotentnie uruchamia oficjalny
installer ESP-IDF dla `ESP_IDF_TARGETS`, po czym sprawdza toolchain i środowisko
Pythona. `JH_ESP_IDF_DIR` albo `--dir` wybiera zewnętrzny checkout bez jego
zastępowania. W każdym terminalu bezpośrednio używającym narzędzi ESP-IDF należy
wykonać `source third_party/esp-idf/export.sh`.

Runner produkcyjny przyjmuje katalog projektu, rozwiązuje target `esp32s3` i
board `waveshare-esp32-s3-zero`, przygotowuje środowisko przypiętego SDK i
sprawdza kompletne wymagania builda:

```bash
python3 scripts/build_esp_idf.py build \
  --project tests/fixtures/esp32s3_phase3 \
  --target esp32s3 --board waveshare-esp32-s3-zero --clean
```

Jest to fixture build/link używany przez CI i lokalny gate. Sprawdza pełny
dostarczany graf funkcji i artefakty ESP32-S3, ale nie deklaruje akceptacji
sprzętowej runtime.

Runner generuje domyślne `sdkconfig` z faktów flash/PSRAM boardu, używa wspólnego
entry pointu `app_main()` i rozwiązanego grafu komponentów ESP-IDF, odrzuca
funkcje spoza allowlisty targetu i emituje relokowalny manifest
`jh_esp_idf_artifacts.json`. Manifest zapisuje każdy obraz flash wraz z offsetem,
rozmiarem i SHA-256, profil tabeli partycji, digest końcowego `sdkconfig`, dokładny
commit ESP-IDF oraz rzeczywiste provenance kompilatora, CMake, Ninja, Pythona,
esptool i rejestru narzędzi ESP-IDF. Domyślne wyjście to
`<project>/.build/esp-idf/<target>/<board>/`; `--output` może wybrać inny katalog
poniżej `.build` projektu lub repozytorium.

`scripts/build_esp_idf_phase0.py` pozostaje cienkim wrapperem zgodności dla
izolowanego fixture Phase 0. Nowe projekty i CI używają
`scripts/build_esp_idf.py`. ESP32-S3 obejmuje bazowe backendy system/sync/GPIO/
ADC/serial/simple-PWM/timer. Rozwiązywany graf dodaje UART, I2C controller/target,
SPI, PWM_FREQ, RMT/RGB, PCNT, konfigurację stack guard, native connectivity i
usługi oraz opcjonalny dispatch APP_TASK1.

`security/esp_idf_tools.json` jest sprawdzonym snapshotem środowiska narzędzi
wybranego przez przypięcie. Zapisuje sześć narzędzi binarnych/danych i jedenaście
narzędzi Pythona Espressif first-party z dokładnymi wersjami, upstreamami i
znormalizowanymi licencjami SPDX. Generator SBOM używa snapshotu bezpośrednio;
wpisy nie są duplikowane w `security/third_party.json`.

## Budowane narzędzia i toolchainy

PMD 7.26.0 jest instalowany z uwierzytelnionego ZIP-a binarnego w
`third_party/pmd/`. Manager sprawdza digest archiwum, pełny manifest plików po
rozpakowaniu i raportowaną wersję PMD. Jedynym wymaganiem hosta jest systemowy
runtime Java; `runmefirst.sh` instaluje `default-jre-headless`.
`scripts/run_cpd.py` definiuje zakres źródeł i progi używane identycznie przez
`runalltests.sh` i CI.

Źródła picotool znajdują się w `third_party/picotool`, a wszystkie artefakty
builda i program wykonywalny w:

```text
.build/tools/picotool/
```

Target `rp2350-riscv` używa przypiętego prebuilt toolchaina z
`riscv_toolchain_version.conf`, instalowanego jako:

```text
third_party/riscv-toolchain/bin/riscv32-unknown-elf-gcc
```

Tożsamość wydania i pełny manifest plików są zapisane w ignorowanej instalacji,
aby updater zastępował nieaktualną lub zmienioną zawartość. Przypięcie obejmuje
uwierzytelnione zasoby x86-64/AArch64 Linux oraz native AMD64 Windows ZIP.
Targety native ARM nadal używają `arm-none-eabi` i nie korzystają z tego
toolchaina RISC-V.

Archiwa hosta native Windows są przypięte w `windows_tools_version.conf`.
`runmefirst.ps1` umieszcza zarządzane Python, CMake, Ninja, GNU Arm, OpenOCD,
picotool i narzędzia RISC-V pod krótkim lokalnym katalogiem użytkownika oraz
zapisuje rozwiązane programy w `resolved-tools.json`.

picotool wymaga `libusb-1.0-0-dev` i `pkg-config` dla dostępu USB. Jest budowany
z przypiętym Pico SDK i używa submodułu Mbed TLS z SDK do hashy i podpisywania
RP2350. Na Linux i Windows weryfikacja wymaga poleceń `load`, `verify` i
`reboot`. Instalacja Linux jest przebudowywana, gdy nowo dostępna obsługa libusb
lub Mbed TLS ujawnia brak możliwości USB albo `seal`, zamiast akceptować samą
zgodność wersji.
