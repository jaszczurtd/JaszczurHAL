# Zarządzane komponenty zewnętrzne

Ten katalog zawiera dwa rodzaje wpisów:

- przechowywane w repozytorium pliki `*_version.conf`, które określają dokładną
  wersję każdego zarządzanego komponentu;
- ignorowane przez Git instalacje źródeł, narzędzi i toolchainów, odtwarzane na
  podstawie tych plików.

Synchronizacja wszystkich komponentów:

```bash
./third_party/update_components.sh
```

Skrypt pobiera brakujące komponenty i zastępuje każdą instalację, której wersja
lub commit Git różni się od konfiguracji zapisanej w repozytorium. Aby
sprawdzić obecne instalacje bez wprowadzania zmian, uruchom:

```bash
./third_party/update_components.sh --verify-only
```

`runmefirst.sh` uruchamia go po instalacji zależności wymaganych w Linuksie.
`runmefirst.ps1` używa tego samego menedżera komponentów w Pythonie w natywnym
środowisku Windows. Poszczególne skrypty `scripts/ensure_*.sh` pozostają
dostępne jako wyspecjalizowane wrappery zachowujące zgodność w systemach
uniksowych, ale standardowym punktem wejścia jest centralny skrypt
aktualizujący.

## Zależności źródłowe

| Komponent | Plik wersji | Katalog utrzymywany przez projekt | Zastosowanie |
|-----------|-------------|--------------------------------------|---------------|
| BearSSL | `bearssl_version.conf` | `BearSSL/` | Silnik TLS używany w kompilacjach dla hosta, RP i STM32 |
| cJSON | `cjson_version.conf` | `cJSON/` | Parser JSON i pomocnicze API |
| LodePNG | `lodepng_version.conf` | `lodepng/` | Kodek PNG operujący na pamięci |
| TJpg_Decoder | `jpeg_version.conf` | `TJpg_Decoder/` | Mały rdzeń dekompresji JPEG dla wyjścia RGB565 |
| FatFs | `fatfs_version.conf` | `FatFs/` | Rdzeń systemu plików FAT dla wspólnej obsługi kart SD |
| Unity | `unity_version.conf` | `Unity/` | Framework testów uruchamianych na hoście i targetach |
| lwIP | `lwip_version.conf` | `lwip/` | Stos TCP/IP używany przez integrację CYW43 w JaszczurHAL |
| littlefs | `littlefs_version.conf` | `littlefs/` | Rdzeń systemu plików używany przez wspólny provider, test integracyjny na hoście oraz natywną pamięć masową RP i STM32G474 |
| BTstack | `btstack_version.conf` | `BTstack/` | Stos hosta BLE i Classic używany przez integrację Bluetooth CYW43 |
| Sterownik Semtech SX126x | `sx126x_driver_version.conf` | `sx126x_driver/` | Przenośny sterownik poleceń SX1261/SX1262 dla implementacji LoRa |
| FreeRTOS-Kernel | `freertos_core_version.conf` | `FreeRTOS-Kernel/` | Jądro FreeRTOS dla natywnego RP SMP i STM32G474 |
| Pico SDK | `pico_sdk_version.conf` | `pico-sdk/` | Natywne SDK dla RP2040/RP2350 |
| ESP-IDF | `esp_idf_version.conf` | `esp-idf/` | Natywne SDK dla rodziny ESP32 i narzędzia do przygotowania środowiska |
| picotool | `picotool_version.conf` | `picotool/` | Źródła natywnego narzędzia RP do wgrywania i odczytu metadanych |

Wrappery integracyjne BearSSL, cJSON, LodePNG, JPEG i FatFs należące do
JaszczurHAL, a także konfiguracja portu lwIP, pozostają w odpowiednich
katalogach `src/hal/network/`, `src/hal/codecs/` i `src/hal/storage/`. Integracja
Unity należy do infrastruktury testowej. W tym katalogu zarządzane są natomiast
drzewa źródeł projektów zewnętrznych. Skrypty cJSON, LodePNG, TJpg_Decoder i
Unity wymagają czystych katalogów roboczych z dokładnie wskazanym commitem; tryb
`verify-only` odrzuca zmiany lokalne i pliki nieśledzone przez Git. Sprawdzany
jest również adres repozytorium źródłowego, w tym repozytoriów BearSSL, LodePNG,
FatFs i Unity utrzymywanych przez projekt. `jaszczurtd/ff16` jest bezpośrednią
kopią lustrzaną niezmienionego archiwum ChaN R0.16 i zastępuje zawodny mechanizm
pobierania z `elm-chan.org`.

Z jednej kopii littlefs korzystają wspólny provider systemu plików,
konfiguracje CMake natywnych targetów RP i STM32G474 oraz osobny test
integracyjny na hoście, wykorzystujący pamięć flash w RAM-ie. Backend danego
targetu dostarcza jedynie geometrię pamięci flash oraz operacje odczytu,
programowania, kasowania i synchronizacji ze sprawdzaniem błędów.

Kopia źródeł Semtech jest utrzymywana bez lokalnych zmian i dokładnie na
commicie `v2.5.0`. Plik
`LICENSE.SX126X` zawiera przechowywaną w repozytorium kopię licencji Clear BSD.
Integracja LoRa na etapie 1 używa wyłącznie `sx126x.c` i
`sx126x_driver_version.c`; opcjonalne źródła LR-FHSS i BPSK pozostają wyłączone
do czasu osobnego przeglądu.

Submoduły Pico SDK wymagane przez natywne kompilacje wymieniono w
`PICO_SDK_SUBMODULES` w pliku `pico_sdk_version.conf`. JaszczurHAL celowo używa
osobnej kopii `lwip/` w wersji ustalonej przez projekt zamiast submodułu lwIP
z SDK.
BTstack jest kompilowany bezpośrednio z dokładnej rewizji utrzymywanego przez
projekt forka `jaszczurtd/btstack`, zapisanej w `btstack_version.conf`;
JaszczurHAL nie nakłada lokalnych patchy na źródła podczas konfiguracji.

Zewnętrzne kopie FreeRTOS i Pico SDK można wskazać za pomocą udokumentowanych
zmiennych `JH_FREERTOS_KERNEL_DIR`, `JH_PICO_SDK_DIR` oraz opcji ścieżek w
skryptach pomocniczych. Takie katalogi zarządzane przez użytkownika są
sprawdzane, ale nigdy zastępowane.

ESP-IDF korzysta z dokładnie wskazanego commitu wydania i jest pobierany na
żądanie przez `scripts/ensure_esp_idf.sh --enable`. Rekurencyjne submoduły są
częścią sprawdzanego zestawu źródeł. To samo polecenie można bezpiecznie uruchamiać
wielokrotnie: uruchamia oficjalny instalator ESP-IDF dla `ESP_IDF_TARGETS`,
a następnie sprawdza toolchain i środowisko Pythona. `JH_ESP_IDF_DIR` lub
`--dir` pozwala wskazać i zweryfikować zewnętrzną kopię bez jej zastępowania.
W każdym terminalu, w którym narzędzia ESP-IDF są używane bezpośrednio, należy
wykonać `source third_party/esp-idf/export.sh`.

Skrypt buildu produkcyjnego przyjmuje katalog projektu, wyznacza
target `esp32s3` i płytkę `waveshare-esp32-s3-zero`, przygotowuje środowisko SDK
w ustalonej wersji i sprawdza wszystkie wymagania buildu:

```bash
python3 scripts/build_esp_idf.py build \
  --project tests/fixtures/esp32s3_phase3 \
  --target esp32s3 --board waveshare-esp32-s3-zero --clean
```

Jest to projekt testowy służący do sprawdzania kompilacji i linkowania, używany
przez CI oraz lokalną kontrolę. Sprawdza pełny graf funkcji dostarczanych dla
ESP32-S3 i powstające artefakty, ale nie potwierdza ich działania na sprzęcie.

Skrypt generuje domyślne `sdkconfig` na podstawie parametrów pamięci flash i
PSRAM płytki, używa wspólnego punktu wejścia `app_main()` oraz wygenerowanego
grafu komponentów ESP-IDF wyznaczonego po rozwiązaniu zależności aktywnych
funkcji. Odrzuca funkcje spoza listy dozwolonej dla targetu i tworzy przenośny
manifest `jh_esp_idf_artifacts.json`. Manifest zapisuje każdy obraz flash wraz z
offsetem, rozmiarem i SHA-256, profil tabeli partycji, skrót końcowego
`sdkconfig`, dokładny commit ESP-IDF oraz rzeczywiste pochodzenie kompilatora,
CMake, Ninja, Pythona, esptool i rejestru narzędzi ESP-IDF. Domyślny katalog
wyjściowy to `<project>/.build/esp-idf/<target>/<board>/`; `--output` może wybrać
inny katalog poniżej `.build` projektu lub repozytorium.

`scripts/build_esp_idf_phase0.py` pozostaje prostym wrapperem zachowującym
zgodność dla izolowanego projektu testowego etapu 0. Nowe projekty i CI używają
`scripts/build_esp_idf.py`. ESP32-S3 obejmuje bazowe implementacje systemu,
synchronizacji, GPIO, ADC, portu szeregowego, prostego PWM i timera. Graf
komponentów wyznaczony na podstawie aktywnych funkcji dodaje UART, I2C w roli
kontrolera lub targetu, SPI, PWM_FREQ, RMT/RGB, PCNT, konfigurację ochrony stosu,
natywną łączność i usługi oraz opcjonalne kierowanie pracy do APP_TASK1.

`security/esp_idf_tools.json` jest sprawdzonym zapisem środowiska narzędzi
wybranego na podstawie plików wersji. Zawiera sześć narzędzi binarnych lub
zasobów danych oraz jedenaście narzędzi Pythona firmy Espressif, zadeklarowanych
w podstawowych wymaganiach ESP-IDF. Dla każdego podaje dokładną wersję, projekt
źródłowy i znormalizowaną licencję SPDX. Generator SBOM korzysta z tego pliku
bezpośrednio; wpisy nie są powielane w `security/third_party.json`.

## Budowane narzędzia i toolchainy

PMD 7.26.0 jest instalowany w `third_party/pmd/` ze zweryfikowanego archiwum
binarnego ZIP. Menedżer komponentów sprawdza skrót archiwum, pełny wykaz
rozpakowanych plików i wersję podawaną przez PMD. Jedynym wymaganiem po stronie
hosta jest systemowy runtime Java; `runmefirst.sh` instaluje
`default-jre-headless`.
Skrypt `scripts/run_cpd.py` określa zakres źródeł i progi stosowane w taki sam
sposób przez `runalltests.sh` i CI.

Źródła picotool znajdują się w `third_party/picotool`, a wszystkie artefakty
buildu i plik wykonywalny trafiają do:

```text
.build/tools/picotool/
```

Target `rp2350-riscv` używa gotowego toolchainu w wersji określonej przez
`riscv_toolchain_version.conf`, instalowanego jako:

```text
third_party/riscv-toolchain/bin/riscv32-unknown-elf-gcc
```

Identyfikator wydania i pełny wykaz rozpakowanych plików są zapisane w
ignorowanym katalogu instalacji. Dzięki temu skrypt aktualizujący może zastąpić
nieaktualną lub zmienioną zawartość. Plik wersji wskazuje zweryfikowane pliki
dystrybucyjne dla Linuksa x86-64/AArch64 oraz archiwum ZIP dla natywnego
środowiska Windows na AMD64.
Natywne targety ARM nadal używają `arm-none-eabi` i nie korzystają z tego
toolchainu RISC-V.

Wersje archiwów dla natywnego środowiska Windows określa
`windows_tools_version.conf`. `runmefirst.ps1` umieszcza instalacje Pythona,
CMake, Ninja, GNU Arm, OpenOCD, picotool i narzędzi RISC-V utrzymywane przez
projekt w lokalnym katalogu głównym użytkownika o krótkiej ścieżce oraz zapisuje
ustalone ścieżki do plików wykonywalnych w `resolved-tools.json`.

picotool wymaga `libusb-1.0-0-dev` i `pkg-config`, aby uzyskać dostęp do USB.
Jest budowany z Pico SDK w ustalonej wersji i używa zawartego w SDK submodułu
Mbed TLS do obliczania skrótów oraz podpisywania obrazów RP2350. W Linuksie i
Windows weryfikacja wymaga dostępności poleceń `load`, `verify` i `reboot`.
Instalacja w Linuksie jest przebudowywana, jeśli pojawienie się obsługi libusb
lub Mbed TLS z SDK oznacza, że dotychczasowy build nie ma możliwości USB albo
`seal`; sama zgodność wersji nie jest wtedy uznawana za wystarczającą.
