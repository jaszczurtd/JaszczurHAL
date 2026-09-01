# JaszczurHAL - Dokumentacja API

*Dostępne również [po angielsku](../en/JaszczurHAL_API.md).*

Sprzętowa warstwa abstrakcji (Hardware Abstraction Layer) dla projektów embedded.
Backend RP2040/RP2350 kompiluje się względem oficjalnego Pico SDK. STM32G474 jest
dostępny jako backend bare-metal lub FreeRTOS z natywnym wsparciem peryferiów
i współdzielonym stosem driverów. ESP32-S3 zapewnia dokładną tożsamość
targetu/płytki, wejście aplikacji ESP-IDF oraz walidację zgodności buildu,
a także zestaw backendów rdzenia/peryferiów i natywny graf
łączności WiFi/lwIP. Jego API sieciowe obejmuje klientów TLS i HTTPS
oraz serwery HTTP i WebSocket bez szyfrowania; nie zdefiniowano publicznego API
serwera TLS, serwera HTTPS, WSS ani klienta WebSocket. Publiczne API HAL
widziane przez aplikację pozostaje stabilne między targetami.

Ten dokument zawiera szczegółową dokumentację API.
Główny [README.pl.md](../../README.pl.md) celowo pozostaje zwięzły i odsyła
tutaj po pełny opis zachowania i gwarancji.

**Autor:** Marcin 'Jaszczur' Kielesiński

**Repozytorium:** `git@github.com:jaszczurtd/JaszczurHAL.git`
**Główny katalog include:** `libraries/JaszczurHAL/src/` (zarejestrowany w `otherLibrariesFolders`)

---

## Publiczny include

Użyj:

```cpp
#include <JaszczurHAL.h>
```

Nagłówek wewnętrzny może być używany do zaawansowanych/wewnętrznych zastosowań.

```cpp
#include <hal/hal.h>
```

Dostępne są też includes zawierające wyłącznie narzędzia:

```cpp
#include <tools.h>    // C++ utility aggregator
```

```c
#include <tools_c.h>  // C-compatible utility API
```

---

## Struktura biblioteki

```text
CMakeLists.txt              # build testów host/mock
VERSION                     # wersja projektu
.build/                     # ignorowany katalog główny zarządzanych artefaktów buildu
boards/                     # deskryptory targetów, płytek i możliwości
config/                     # deklaratywny rejestr funkcji HAL i schemat
rp_native_lib/               # build biblioteki statycznej RP2040/RP2350 na Pico SDK
  MEMORY_MAP.md              # układ natywnego firmware/pamięci/OTA dla RP
cmake/
  esp-idf/                  # kontrolowana natywna receptura komponentu ESP-IDF
  generated/                # wygenerowany produkcyjny resolver funkcji CMake
  jh_rp_native_sdk.cmake    # współdzielona integracja CMake biblioteki/firmware RP
  targets/                  # receptury targetów dispatchera VS Code
stm32_lib/                  # CMake biblioteki statycznej STM32G474, toolchain, skrypt linkera
scripts/
  # Zobacz doc/api/pl/00_scripts.md, aby uzyskać pełny opis skryptów obsługi repozytorium.
  build_rp_native_lib.sh    # pomocnik buildu RP ELF/BIN/UF2
  build_stm32_lib.sh        # pomocnik biblioteki statycznej STM32G474
  build_esp_idf.py          # runner buildu/artefaktów/flashowania projektu ESP-IDF
  check_documentation_links.py # lokalna walidacja linków/kotwic Markdown
  ensure_*.sh               # ukierunkowane pomocniki pobierania/weryfikacji przypiętych komponentów
  generate_sbom.py          # generator SBOM CycloneDX
  generate_hal_features.py  # walidacja, generowanie i lint rejestru funkcji
  check_vulnerabilities.sh  # opcjonalny lokalny wrapper skanera podatności
runalltests.sh              # pełna lokalna brama walidacyjna
runmefirst.sh                # jednorazowa lokalna konfiguracja toolchainu
doc/
  HAL_FLAGS.txt             # wspólne podsumowanie flag HAL_ENABLE_*
  table_of_contents.md      # angielski spis treści dokumentacji
  table_of_contents.pl.md   # polski spis treści dokumentacji
  en/                       # angielska dokumentacja referencyjna
    features.md             # macierz funkcji wysokiego poziomu
    JaszczurHAL_API.md      # szczegółowe API/referencja
    FwProjectWorkflow.md    # workflow projektu firmware oparty na dispatcherze
    OTAWorkflow.md          # natywny build/wgrywanie/odzyskiwanie OTA dla RP/ESP
    lib_compilation.md      # przewodnik buildu biblioteki statycznej
    security_supply_chain.md  # proces SBOM i śledzenia podatności
    boards_profiles_howto.md
    windows_setup.md
  pl/                       # polskie tłumaczenia powyższych plików z doc/en/
    features.md             # macierz funkcji wysokiego poziomu
  api/
    en/                     # podzielone rozdziały API (angielski)
      00_scripts.md          # kluczowa architektura procesów i orkiestracji
    pl/                     # podzielone rozdziały API (polski)
examples/                   # przykłady dla rodziny RP i STM32G474
vscode/                     # współdzielone wejście jh-vscode, schemat, dokumentacja, generator
  entry/                    # launchery dla Unix, Windows i publiczny launcher Pythona
  tools/create-vscode-example.py # samodzielny generator projektu firmware VS Code
  tools/manage_vscode_extensions.py # konfiguracja rozszerzeń po weryfikacji/zgodzie
security/
  third_party.json          # inwentarz komponentów firm trzecich
  esp_idf_tools.json        # zweryfikowany zrzut narzędzi targetu ESP-IDF
  sbom.cdx.json             # wygenerowany SBOM CycloneDX
  vulnerability_log.md      # dziennik oceny CVE/CVSS i poprawek
src/
  JaszczurHAL.h              # główny publiczny include
  hal_app_entry.cpp          # opcjonalny przenośny wrapper wejścia aplikacji
  libConfig.h                # include dla wstecznej kompatybilności
  tools.h, tools_c.h         # agregatory narzędziowe (C++ / C)
  arpa/, netinet/, sys/      # nagłówki kompatybilności socketów host/embedded
  hal/                       # nagłówek zbiorczy HAL + tematyczne domeny współdzielone
    hal.h                    # include zbiorczy wyłącznie dla HAL
    commands/                # router poleceń neutralny względem transportu i komunikaty przewodowe
    core/                    # konfiguracja, status, asercje, kompatybilność
    bluetooth/               # publiczne API BLE, fasada i współdzielone wsparcie BTstack
    i2c/, spi/, serial/      # API magistrali i portu szeregowego ze wspólnymi implementacjami
    time/, rtc/, power/      # czas zegarowy, wybudzanie RTC i zarządzanie zasilaniem
    timers/                  # API timerów sprzętowych, rozszerzonych, programowych i SmartTimers
    temperature/             # DHT, DS18B20, MAX6675 i MCP9600
    network/                 # rdzeń TCP/UDP/Wi-Fi i współdzielony runtime sieci
      http/, mqtt/, ota/    # współlokowane publiczne API i implementacje
      tls/, wireguard/      # bezpieczne transporty i silniki wielokrotnego użytku
      websocket/            # publiczne API i implementacja WebSocket
      net_console/          # publiczne API i implementacja zdalnej konsoli
      net_commands/         # adapter poleceń HTTP/WebSocket
      cyw43/, lwip/         # integracja radia i stosu IP
    radio/                  # surowe LoRa, niezawodne łącze i adapter poleceń
    storage/                # EEPROM, KV, logger SD, system plików, pomocniki flash
    display/, gpio/         # drivery zorientowane na wyświetlacz/GFX i GPIO
    analog/, audio/, can/   # dodatkowe tematyczne domeny API i driverów
    generated/              # wygenerowane produkcyjne domknięcie/raport funkcji C
    impl/
      .mock/                # deterministyczny backend host/testowy
      rp2040/               # backend rodziny RP
        drivers/flash/      # natywny koordynator flash RP i partycje pamięci
        drivers/rp2040/     # usługi SoC RP2040 (awarie/system)
        drivers/usb/        # natywna konfiguracja/deskryptory TinyUSB CDC
        freertos/           # natywny FreeRTOSConfig i hooki RP
        frameworks/         # integracje frameworków specyficzne dla RP
      stm32g474/            # backend STM32G474
        drivers/
          stm32g474/        # usługi SoC STM32G474 (awarie/system)
        freertos/           # FreeRTOSConfig i hooki STM32
        port/               # startup, SystemInit, niskopoziomowe wsparcie dla linkera
  utils/                    # narzędzia, PID, watchdog, pomocniki rysowania, integracja Unity
tests/                       # testy jednostkowe host (CMake + Unity)
  freertos_posix/           # opcjonalne testy schedulera FreeRTOS POSIX po stronie hosta
  hardware/                 # śledzone źródła/manifesty fixture RP i weryfikatory hosta
third_party/                # śledzone przypięcia + ignorowane zarządzane instalacje komponentów
  update_components.sh      # synchronizuje każdy komponent do jego śledzonego przypięcia
  *_version.conf             # śledzone definicje wersji źródeł/narzędzi/toolchainu
  littlefs/                 # ignorowany przypięty checkout systemu plików upstream
```

Kod niezależny od targetu jest współlokowany ze swoim publicznym API w
odpowiednim katalogu `src/hal/<domena>/`. Domena może zawierać publiczne
nagłówki `hal_*.h`, wspólne fasady, prywatne pomocniki `jh_*`, podkatalogi
driverów urządzeń i silniki wielokrotnego użytku. Dzięki temu deklaracje
i implementacje mają jedną tematyczną hierarchię. `src/hal/impl/` jest
zarezerwowany dla portów i backendów specyficznych dla targetu; przenośny kod
domenowy musi zależeć wyłącznie od API na poziomie HAL.

### Rozwiązywanie funkcji podczas buildu

Wersjonowany rejestr w `config/features/` generuje produkcyjne resolvery C
i CMake. `hal_config.h` dołącza domknięcie C, natomiast buildy CMake dla RP
i STM32G474 używają go do wyboru źródeł i zależności. Runner ESP-IDF rozwiązuje
ten sam graf żądań, odrzuca funkcje spoza listy dozwolonych deskryptora
targetu i wybiera obsługiwany graf źródeł bazowych, peryferyjnych i
sieciowych. Zapisuje żądane i rozwiązane funkcje razem z pochodzeniem płytki
i linkowania.
Generator płytek zapisuje zarówno `requestedFeatures`, jak i
`resolvedFeatures`; jego hash funkcji i sygnatura linkowania używają zbioru
rozwiązanego. `jh-vscode` rozwiązuje aktywny profil i wariant do tego samego
domknięcia i publikuje skrót rejestru, skrót domknięcia oraz pochodzenie
żądań poprzez `config-dump`.

Warunkowe wartości domyślne, wybory providerów, sprawdzenia możliwości płytki
oraz ograniczenia targetu pozostają w `hal_config.h`. `HAL_CONFIG_VERBOSE`
aktywuje wygenerowany raport każdej aktywnej zarejestrowanej flagi. CI
traktuje dryf rejestru oraz lint surowych/efektywnych funkcji jako błędy.
Zainstalowane pakiety RP i STM32G474 zawierają wygenerowane nagłówki
funkcji/płytki, JSON rozwiązania, nagłówek sygnatury linkowania oraz źródło
referencyjne. Dzięki temu projekty wywołujące kompilator bezpośrednio mogą
zbudować i zlinkować ustalony pakiet bez uruchamiania Pythona.

- `CMakeLists.txt` - build testów host/mock w katalogu głównym repozytorium.
- `rp_native_lib/` - oficjalna biblioteka statyczna Pico SDK i sondy firmware.
- `stm32_lib/` - CMake biblioteki statycznej STM32G474, plik toolchainu i skrypt linkera.
- `scripts/build_rp_native_lib.sh` - pomocnik biblioteki statycznej RP2040/RP2350
  i opcjonalnych sond firmware, w tym tryb `--library-only` generujący
  wyłącznie archiwum oraz opcjonalną, przypiętą macierz FreeRTOS SMP.
- `scripts/build_stm32_lib.sh` - pomocnik biblioteki statycznej STM32G474.
- `scripts/build_esp_idf.py` - produkcyjny pomocnik buildu projektu
  ESP-IDF, walidacji artefaktów i flashowania z relokowalnym manifestem
  wieloobrazowym.
- `third_party/update_components.sh` - synchronizuje BearSSL, cJSON, LodePNG,
  TJpgDec, FatFs, Unity, lwIP, littlefs, BTstack, driver Semtech SX126x,
  FreeRTOS, Pico SDK, picotool, PMD CPD oraz toolchain RISC-V dla RP2350
  z ich śledzonymi przypięciami `third_party/*_version.conf`. ESP-IDF jest
  przygotowywane na żądanie przez swój produkcyjny runner lub dedykowaną
  komendę ensure.
- `scripts/generate_sbom.py` - deterministyczny generator SBOM CycloneDX
  z trybem `--check` tylko do odczytu.
- `scripts/check_sbom.sh` - wrapper kompatybilności dla ukierunkowanego
  sprawdzania SBOM; współdzielony runner artefaktów generowanych odpowiada
  za synchronizację w całym repozytorium.
- `scripts/check_vulnerabilities.sh` - opcjonalny lokalny wrapper skanera
  podatności, który regeneruje SBOM i uruchamia dostępne skanery zależności
  źródłowych/dołączonych (vendored).
- `doc/api/pl/00_scripts.md` - kluczowy opis architektury operacyjnej
  skryptów obsługi repozytorium, punktów wejścia orkiestracji, opcji,
  artefaktów i relacji między nimi.
- `runalltests.sh` - pełna lokalna brama walidacyjna.
- `runmefirst.sh` - jednorazowa lokalna konfiguracja toolchainu.
- `doc/pl/FwProjectWorkflow.md` - workflow projektu firmware oparty
  na dispatcherze: model manifestu, wybór targetu/płytki, wykrywanie źródeł,
  wgrywanie, build debug, katalogi buildu i pliki
  generowane.
- `doc/pl/boards_profiles_howto.md` - deklaratywne deskryptory targetu/płytki,
  generowana konfiguracja, biblioteki statyczne świadome płytki oraz
  procedura dodawania fizycznej płytki.
- `doc/pl/OTAWorkflow.md` - wymagania natywnego OTA dla RP i ESP32-S3: integracja
  firmware, artefakty specyficzne dla targetu, wgrywanie z VS Code, zapora
  sieciowa, potwierdzenie próbne, wycofanie (rollback) i odzyskiwanie.
- `SECURITY.md` - zgłaszanie podatności, triaż i polityka utrzymania.
- `security/third_party.json` - ręcznie utrzymywany inwentarz komponentów firm trzecich.
- `security/sbom.cdx.json` - wygenerowany SBOM CycloneDX.
- `security/vulnerability_log.md` - dziennik oceny CVE/CVSS i decyzji o poprawkach.
- `src/JaszczurHAL.h` - zbiorczy include dla modułów HAL i narzędziowych.
- `doc/HAL_FLAGS.txt` - zwięzłe podsumowanie flag `HAL_ENABLE_*`.
- `src/libConfig.h` - przekierowanie dla wstecznej kompatybilności do `hal/core/hal_config.h`.
- `src/tools.h` - agregator narzędziowy C++.
- `src/tools_c.h` - deklaracje narzędziowe kompatybilne z C.
- `src/hal/hal.h` - include zbiorczy wyłącznie dla HAL.
- `src/hal/core/hal_config.h` - fasada kompatybilności dla wyboru funkcji
  podczas buildu, propagacji zależności i konfiguracji projektu.
- `src/hal/core/hal_runtime_config.h` oraz `src/hal/core/hal_config.cpp` - API
  i implementacja konfiguracji limitów puli w runtime.
- `src/hal/core/hal_assert.h` oraz `src/hal/core/hal_assert.cpp` - przenośne
  API asercji i implementacja obsługi błędów świadoma targetu.
- `src/hal/core/hal_compat.h` - pomocnicze makra `PROGMEM`, `F()`, `hal_min()`
  i `hal_max()` zapewniające kompatybilność źródłową.
- `src/hal/<domain>/hal_*.h` - publiczne interfejsy modułów HAL pogrupowane
  tematycznie, np. GPIO, magistrale, port szeregowy, bezpieczeństwo, czujniki,
  pamięć masowa, wyświetlacz i sieć.
- `src/hal/can/hal_can_util.cpp`, `src/hal/security/hal_crypto.cpp`, `src/hal/security/hal_crc.cpp`, `src/hal/gps/hal_gps.cpp`, `src/hal/storage/hal_kv.cpp`, `src/hal/audio/hal_pga2311.cpp`, `src/hal/rtc/hal_rtc.cpp`, `src/hal/timers/hal_soft_timer.cpp`, `src/hal/control/hal_pid_controller.cpp` - współdzielone implementacje wrapperów i fasad HAL.
- `src/hal/time/hal_time_ntp.cpp` oraz `src/hal/storage/hal_eeprom.cpp` -
  współdzielona, thread-safe integracja zegara ściennego/NTP/RTC oraz
  fasady EEPROM z wyborem providera; przenośny kod providera AT24C256 i
  buforowanego flash pozostaje obok API EEPROM, natomiast katalogi targetów
  dostarczają wyłącznie fizyczne mechanizmy flash.
- `src/hal/serial/hal_uart_config.h` - stałe konfiguracyjne i pomocniki UART.
- `src/hal/core/hal_status.h` - współdzielone kody wyniku `hal_status_t` dla
  nowych publicznych API.
- `src/hal/system/hal_board.h` oraz `src/hal/system/hal_board.cpp` - tożsamość
  profilu płytki, fakty fizyczne podczas buildu oraz thread-safe
  stan capabilities w runtime.
- `src/hal/impl/rp2040/` - backend rodziny RP.
- `src/hal/impl/stm32g474/` - backend STM32G474 (rzeczywiste domeny rdzenia na poziomie rejestrów; pozostałe moduły w toku).
- `src/hal/impl/.mock/` - deterministyczny backend testowy na hosta.
- `src/hal/<domain>/` - publiczne nagłówki i implementacje niezależne od
  backendu w jednym tematycznym katalogu; przykłady to `bluetooth/`,
  `network/`, `time/`, `timers/`, `temperature/`, `i2c/`, `spi/`, `display/`
  i `storage/`.
- `src/hal/impl/rp2040/drivers/` - dołączone niskopoziomowe drivery firm trzecich wykorzystywane przez opcjonalne moduły HAL.
- `src/hal/impl/rp2040/drivers/rp2040/` - drivery specyficzne dla SoC: `rp2040_fault.{h,cpp}` (przechwytywanie HardFault, strażnik stosu, zatrzask powodu resetu) oraz `rp2040_system.{h,cpp}` (watchdog, wejście USB-boot, temperatura na chipie, wolna pamięć sterty, unikalny identyfikator płytki, podpowiedź stanu bezczynności).
- `src/hal/impl/stm32g474/drivers/stm32g474/` - drivery specyficzne dla SoC: `stm32g474_fault.{h,cpp}` (klasyfikacja powodu resetu, zachowane przekazanie informacji o awarii, strażnik stosu) oraz `stm32g474_system.{h,cpp}` (czas, opóźnienie, watchdog, temperatura, UID, pomocniki bezczynności / wrażliwe na ISR).
- `src/hal/network/mqtt/PubSubClient/` oraz
  `src/hal/network/wireguard/core/` - dołączone silniki sieciowe neutralne
  względem targetu.
- `src/utils/` - narzędzia wyższego poziomu: `tools`, `pidController`, `multicoreWatchdog`, `draw7Segment` oraz zarządzana integracja Unity.

`JaszczurHAL.h` jest obecnym głównym publicznym include i powinien być
domyślnym include w kodzie projektu. `hal/hal.h` pozostaje dostępny jako
agregator wyłącznie HAL, ale nie jest głównym include eksportowanym przez
obecne metadane biblioteki.

---

## Mapy pamięci

Uwagi dotyczące układu pamięci specyficznego dla targetu znajdują się obok
wsparcia buildu dla każdego backendu:

- [Mapa pamięci RP](../../rp_native_lib/MEMORY_MAP.md) - układy linkera dla
  aplikacji i OTA, trwałe regiony flash, SRAM, sterta i stosy.
- [Mapa pamięci STM32G474](../../stm32_lib/MEMORY_MAP.md) - regiony linkera
  bare-metal, zarezerwowane strony flash EEPROM/KV, sekcje RAM, sterta i stos.

---

## Zakres dokumentacji

Zalecany podział odpowiedzialności:

- [00_scripts.md](../api/pl/00_scripts.md): kluczowa część dokumentacji
  JaszczurHAL, która wyjaśnia, jak konfiguracja, zarządzanie zależnościami,
  buildy, przykłady, walidacja, narzędzia bezpieczeństwa i orkiestracja
  VS Code współdziałają ze sobą; przeczytaj, aby zrozumieć, jak biblioteka
  działa jako kompletny system deweloperski
- [FwProjectWorkflow.md](FwProjectWorkflow.md): workflow projektu firmware
  oparty na dispatcherze, obejmujący manifest, target, źródła, build
  i wgrywanie
- [OTAWorkflow.md](OTAWorkflow.md): natywna konfiguracja OTA dla RP
  i ESP32-S3, provisioning, wgrywanie, sieć/zapora, potwierdzenie, wycofanie
  i odzyskiwanie
- `doc/pl/JaszczurHAL_API.md`: układ modułów, uwagi migracyjne, szczegóły
  publicznego API, odniesienie do flag funkcji

Każdy dokument jest właścicielem szczegółów w swoim przypisanym zakresie.
Pozostałe powinny dostarczać krótki kontekst i odsyłać do tego właściciela
zamiast powtarzać polecenia, interfejsy czy przykłady konfiguracji.

---

## Publiczne API a moduły pomocnicze

Repozytorium zawiera zarówno sam HAL, jak i zestaw modułów narzędziowych.

### Publiczne API HAL

Są to interfejsy zorientowane na przenośność, mające na celu oddzielenie
logiki aplikacji od wywołań SDK specyficznych dla płytki:

- rdzeń i system: `hal_config`, `hal_status`, `hal_bits`, `hal_math`,
  `hal_board`, `hal_system`, `hal_power`, `hal_sync`, `hal_timer`,
  `hal_soft_timer` i `hal_pid_controller`
- analog, GPIO i audio: `hal_gpio`, `hal_adc`, `hal_dac`, `hal_pwm`,
  `hal_pwm_freq`, `hal_pcnt`, `hal_dacless` i `hal_dma_pwm_audio`
- magistrale i port szeregowy: `hal_uart`, `hal_swserial`, `hal_serial`,
  `hal_usb`, `hal_spi`, `hal_spi_device`, `hal_i2c`, `hal_i2c_slave`
  i `hal_onewire`
- bezpieczeństwo i łączność: `hal_crypto`, `hal_crc`, `hal_net`, `hal_wifi`,
  `hal_udp`, `hal_tcp`, `hal_tls`, `hal_http_client`, `hal_http_server`,
  `hal_http_files`, `hal_websocket`, `hal_net_console`, `hal_net_commands`,
  `hal_notify`, `hal_wireguard`, `hal_mqtt`, `hal_ota`, `hal_time`,
  `hal_ble` i `hal_ble_stream`
- `hal_command_router` i `hal_command_wire` dla neutralnej względem
  transportu polityki poleceń, dispatchu i ograniczonych wiadomości
  binarnych
- `hal_lora_radio` dla niezależnej od providera surowej obsługi LoRa
  z providerami z rodzin SX126x i SX127x
- `hal_lora_link` dla adresowanych, potwierdzanych, fragmentowanych
  prywatnych wiadomości przez jedno surowe radio LoRa, z opcjonalnym
  uwierzytelnionym szyfrowaniem
- `hal_lora_commands` dla żądań poleceń, odpowiedzi i zdarzeń przez jedno
  niezawodne łącze LoRa, którego adapter używa na wyłączność
- urządzenia i multimedia: `hal_can`, `hal_display`, `hal_hd44780`,
  `hal_rgb_led`, `hal_thermocouple`, `hal_ds18b20`, `hal_rtc`,
  `hal_external_adc`, `hal_gps`, `hal_tsc2007`, `hal_stmpe610`,
  `hal_irsmall_decoder`, `hal_digipot`, `hal_pga2311`, `hal_mfrc522`
  i `hal_pn532`
- pamięć masowa: `hal_eeprom`, `hal_kv`, `hal_littlefs` i `hal_sdlogger`
- zawsze dostępne pomocniki `hal_time` do deterministycznej konwersji
  daty/czasu gregoriańskiego na epokę, korekty CET/CEST, sprawdzeń
  przedziałów otwartych z jednej strony oraz ekstrakcji minut; opcjonalne
  API NTP/czasu lokalnego pozostają sterowane flagami
- opcjonalny hook znacznika czasu dla logowania błędów poprzez
  `hal_debug_set_timestamp_hook(...)`

### Moduły pomocnicze / narzędziowe

Są to wygodne dodatki, ale same w sobie nie stanowią granicy przenośności:

- `tools`
- `SmartTimers`
- `pidController`
- `multicoreWatchdog`
- `draw7Segment`

Projektując nowy kod aplikacji, preferuj poleganie w pierwszej kolejności na
warstwie HAL. Moduły pomocnicze są użytecznymi elementami składowymi, ale
koncepcyjnie nie powinny zastępować granicy HAL.

---

## Profile płytek i capabilities runtime

`HAL_TARGET_*` identyfikuje MCU i architekturę (ISA). `JH_BOARD` wybiera
fizyczny profil z `boards/profiles/`; generator emituje odpowiedni selektor
`HAL_BOARD_PROFILE_*` oraz konfigurację targetu. Deskryptory płytek są
autorytatywnym inwentarzem profili. Wypisz bieżące identyfikatory poleceniem:

```bash
python3 scripts/generate_board_config.py --boards-root boards --list boards
```

Komponent ESP32-S3 używa wygenerowanych danych targetu/płytki oraz metadanych
linkowania i buduje publiczną fasadę runtime `hal_board`. Stan capability
jest zarządzany przez moduł, który za nią odpowiada: zadeklarowana capability
pozostaje `HAL_BOARD_CAP_INACTIVE`, dopóki moduł nie opublikuje stanu
dostępnego lub błędnego.

`HAL_BOARD_DECLARED_CAPABILITIES` opisuje zamontowany sprzęt podczas buildu.
Na targetach zawierających fasadę runtime użytkownicy
powinni odpytywać `hal_board_get_info()` lub
`hal_board_get_capability_state()`, a następnie używać
`hal_board_require_capabilities()` przed operacjami wymagającymi jednej lub
więcej z: `HAL_BOARD_CAP_USB_DEVICE`, `HAL_BOARD_CAP_CYW43`,
`HAL_BOARD_CAP_EXTERNAL_RADIO_FRONTEND`, `HAL_BOARD_CAP_SX1262_RADIO`
i `HAL_BOARD_CAP_BLUETOOTH_CONTROLLER`, lub `HAL_BOARD_CAP_NATIVE_WIFI`.
Zadeklarowana capability jest początkowo `HAL_BOARD_CAP_INACTIVE`; odpowiedni
moduł przenosi ją do stanu `AVAILABLE` lub `FAILED`. Provider RP CYW43
publikuje te przejścia podczas inicjalizacji i deinicjalizacji.

`hal/system/hal_board.h` definiuje stabilny enum profili, maskę bitową
capabilities, stany runtime, typ migawki oraz funkcje odpytujące.
Wygenerowany `src/hal/generated/jh_board_registry.h` mapuje każdy profil
rejestru na tę publiczną tożsamość, bez utrzymywania tu drugiej, ręcznie
pisanej listy profili.

`hal_board_require_capabilities()` zwraca `HAL_OK`, gdy każda żądana
możliwość jest dostępna, `HAL_EUNSUPPORTED`, gdy płytka jej nie deklaruje,
`HAL_EUNINIT`, dopóki zadeklarowana możliwość jest wciąż nieaktywna, oraz
`HAL_EHW`, gdy jej właściciel zgłosił błąd:

```c
if (hal_status_is_ok(hal_board_require_capabilities(HAL_BOARD_CAP_CYW43))) {
    /* radio paths are safe to use on this board */
}
```

---

## Sekcje dokumentacji API

Kompletna dokumentacja referencyjna jest podzielona na następujące,
ukierunkowane dokumenty:

| # | Plik | Zawartość |
|---|------|----------|
| 0 | [Skrypty obsługi repozytorium i orkiestracja](../api/pl/00_scripts.md) | Kluczowa architektura operacyjna: konfiguracja stacji roboczej, zarządzane zależności, punkty wejścia buildu, przykłady, walidacja, narzędzia bezpieczeństwa, integracja VS Code, własność artefaktów oraz relacje między tymi procesami |
| 1 | [Przewodnik buildu biblioteki](lib_compilation.md) | Build dla wszystkich targetów, generowane metadane płytki, pomocniki bibliotek statycznych, warianty FreeRTOS oraz integracja firmware |
| P | [Workflow projektu firmware](FwProjectWorkflow.md) | Projekty firmware VS Code oparte na dispatcherze: model manifestu, wybór targetu/płytki, wykrywanie źródeł, układ cache CMake per target, wgrywanie, build debug oraz pliki generowane |
| O | [Natywny workflow OTA](OTAWorkflow.md) | Konfiguracja projektu/firmware OTA specyficzna dla targetu RP i ESP32-S3, artefakty, pierwsze flashowanie, integracja VS Code, zapora sieciowa, potwierdzenie, rollback, odzyskiwanie oraz granica bezpieczeństwa |
| 2 | [Flagi modułów i konfiguracja](../api/pl/02_module_flags.md) | Flagi opt-in `HAL_ENABLE_*`, propagacja zależności, wybór FreeRTOS, nadpisania rozmiaru stosu oraz moduły rdzeniowe |
| A | [Status API](../api/pl/01_status_api.md) | Fundamentalne, przekrojowe: kody wyniku `hal_status_t`, bezpośrednia zmiana operacji `void` mogących zakończyć się błędem, warianty `_ex` dla zachowanych API zwracających wartość/uchwyt/`bool`, formy z parametrem wyjściowym oraz fallback przy kolizji. |
| 3 | [Zależności buildu, testy i stanowiska testowe sprzętu](../api/pl/03_build_tests.md) | Architektura testów i źródła prawdy, zależności, wykonanie host/CI, pełny inwentarz zestawów testów, zasady rozszerzania, sterowanie czasem mock oraz scentralizowane procedury i wyniki testów sprzętowych |
| 4 | [Bezpieczeństwo wielordzeniowe i drivery](../api/pl/04_multicore_drivers_migration.md) | Zasady inicjalizacji/działania wielordzeniowego, inwentarz dołączonych driverów i licencji, hook znacznika czasu logowania, pomocnik konwersji czasu, przegląd przykładów, pokrycie testami host oraz mapowanie na przenośne API |
| S | [Bezpieczeństwo łańcucha dostaw](security_supply_chain.md) | Inwentarz komponentów firm trzecich, generowanie SBOM CycloneDX, skanowanie podatności oraz workflow oceny CVE/CVSS |
| 5 | [GPIO, ADC i PWM](../api/pl/05_gpio_adc_pwm.md) | `hal_gpio`, `hal_pwm`, `hal_dac`, `hal_pcnt`, `hal_pwm_freq`, `hal_dacless`, `hal_adc` |
| 6 | [Timery i system](../api/pl/06_timers_system.md) | `hal_timer` (alarmy + zarządzane timery), `hal_system` (millis/watchdog/diagnostyka awarii/UID), `hal_power` (przejścia Sleep/deep-sleep/power-down), `hal_bits`, `hal_compiler` (przenośne atrybuty i wbudowane funkcje), `hal_math` |
| 7 | [Kryptografia](../api/pl/07_crypto.md) | `hal_crypto` - Base64, MD5, SHA-256, HMAC-SHA256, ChaCha20, ChaCha20-Poly1305 |
| 8 | [Synchronizacja, USB, port szeregowy, ramkowanie i uwierzytelnianie](../api/pl/08_sync_serial.md) | `hal_sync` (mutex/sekcja krytyczna), `hal_usb` (cykl życia USB i CDC oparty na statusie), `hal_serial` (jeden rdzeń serializowany po TX, z portami transportu dobieranymi w czasie linkowania, strumieniowym formatowaniem debug, logowaniem odroczonym z ISR i ogranicznikiem szybkości), `hal_serial_session` (ramkowany protokół SC), `hal_serial_commands` (adapter routera tekstowego), `hal_serial_frame` (kodek przewodowy), `hal_sc_auth` (wyzwanie/odpowiedź HMAC) |
| 9 | [Magistrale komunikacyjne](../api/pl/09_buses.md) | `hal_spi` (transfer `_ex` oparty na statusie i pomocniki DMA), `hal_i2c` (API mastera oparte na statusie, ograniczony skaner z callbackiem watchdoga, pomocniki jednorazowe i czyszczenie magistrali), `hal_i2c_slave` (mapa rejestrów), `hal_uart`, `hal_swserial`, `hal_onewire` |
| 10 | [Magistrala CAN i wyświetlacz](../api/pl/10_can_display.md) | `hal_can` (CAN wybierany przez backend: klasyczny CAN MCP2515, CAN FD MCP251XFD oraz natywny FDCAN STM32G474), `hal_display` (fasada TFT/OLED/LCD/EPD oparta na statusie, zapisy surowe, odświeżanie EPD, prymitywy GFX, strumieniowanie, tekst i czcionki) |
| 11 | [Czujniki](../api/pl/11_sensors.md) | `hal_thermocouple` (jedna fasada MCP9600/MAX6675/mock z wyborem providera), `hal_ds18b20` (tryb nieblokujący), `hal_dht` (DHT11/DHT22), `hal_bh1750` (natężenie światła otoczenia), `hal_adp5360` (PMIC - ładowarka/fuel-gauge/regulatory), `hal_mcp3221` (12-bitowy ADC I2C), `hal_rtc` (providerzy PCF8563/DS3231/wewnętrzny AON i wybudzanie względne), `hal_external_adc` (ADS1115), `hal_gps` (NMEA, automatyczne wykrywanie ramkowania) |
| 12 | [Modem komórkowy](../api/pl/12_modem.md) | `hal_modem_at` (silnik AT, URC, współpraca z watchdogiem), `hal_simcom_a76xx` (A7670/A7672 - zasilanie, rozruch, SIM, PDP, LBS, GNSS, subskrypcja MQTT) |
| 13 | [Urządzenia wyjściowe](../api/pl/13_output_devices.md) | `hal_rgb_led` (NeoPixel, transport PIO/GPIO), `hal_digipot` (cyfrowe potencjometry I2C MCP401x/MAX5395), `hal_pga2311` (driver głośności stereo), `hal_mcp23017`/`hal_pca9654e`/`hal_pcf8574` (ekspandery GPIO/wyjść I2C), `hal_hc595` (ekspander wyjść SPI z rejestrem przesuwnym), `hal_mcp4725` (12-bitowy DAC I2C), `hal_mfrc522`/`hal_pn532` (czytniki RFID/NFC), `hal_math` (constrain, map, roundToN) |
| 14 | [Pamięć masowa](../api/pl/14_storage.md) | `hal_eeprom` (flash targetu / AT24C256), `hal_kv` (magazyn KV tylko do dopisywania z GC), `hal_littlefs` (pomocniki montowania/formatowania LittleFS), `hal_sdlogger` (buforowany logger karty SD i reporter awarii) |
| 15 | [Łączność sieciowa](../api/pl/15_connectivity.md) | API `_ex` zwracające status dla `hal_wifi`, resolvera, `hal_udp`, `hal_tcp`, `hal_tls`, `hal_mqtt` i `hal_wireguard`; `hal_http_server`, `hal_http_files`, `hal_websocket`, `hal_net_console`, `hal_net_commands`, `hal_notify`, niezależny adapter gniazd BSD z `getaddrinfo()` i opcjonalnym transportem TLS, `hal_ota`, zawsze dostępne pomocniki kalendarzowe `hal_time` oraz opcjonalny NTP/czas lokalny |
| 16 | [Narzędzia](../api/pl/16_utilities.md) | `hal_soft_timer` (wrapper C nad SmartTimers), `hal_pid_controller` (wrapper C nad pidController), `hal_crc` (uniwersalne sumy kontrolne CRC-8/16/32), funkcje pomocnicze `tools.h/cpp`, `SmartTimers`, `pidController`, `multicoreWatchdog`, `draw7Segment` |
| 17 | [cJSON](../api/pl/17_cJSON.md) | Zarządzane `cJSON` / `cJSON_Utils`, wzorce include, zasady własności, parsowanie, wypisywanie, przykłady JSON Pointer/Patch/Merge Patch |
| 18 | [LodePNG](../api/pl/18_LodePNG.md) | Zarządzany `LodePNG`, wzorce include, profil wbudowany, własność pamięci, skrypt zasobów PNG/Base64 i przykłady RGB565 |
| 19 | [JPEG](../api/pl/19_JPEG.md) | Zarządzany rdzeń `TJpgDec`, profil wbudowany, własność pamięci, skrypt zasobów JPEG/Base64 i przykłady RGB565 |
| 20 | [Bluetooth](../api/pl/20_bluetooth.md) | Cykl życia BLE Peripheral/Observer i gamepada Classic HID, parowanie, reconnect, znormalizowane snapshoty, uwierzytelniony Stream, ograniczone kolejki, wsparcie płytek, koegzystencja oraz granica dystrybucji BTstack |
| 21 | [Surowe radio LoRa](../api/pl/21_lora.md) | Zwalidowane profile SX1262 oraz eksperymentalne, wyłącznie programowe SX1261/SX1276/SX1278, asynchroniczne TX/RX/CAD, bieżące RSSI, capabilities, callbacki, diagnostyka i time-on-air |
| 22 | [Niezawodne łącze LoRa](../api/pl/22_lora_link.md) | 16-bitowe adresowanie, sekwencje wiadomości, ACK/retry, tłumienie duplikatów, fragmentacja oraz opcjonalne ChaCha20-Poly1305 na `hal_lora_radio` |
| 23 | [Routing poleceń](../api/pl/23_commands.md) | Neutralna względem transportu rejestracja handlerów i polityka, ograniczone wiadomości przewodowe żądanie/odpowiedź/zdarzenie, kompatybilność sieciowa, adaptery ramkowanego portu szeregowego, niezawodnego LoRa i uwierzytelnionego BLE Stream |

---

## Szybkie wyszukiwanie modułów

| Moduł | Sekcja |
|--------|---------|
| `hal_adc` | [GPIO, ADC i PWM](../api/pl/05_gpio_adc_pwm.md) |
| `hal_bh1750` | [Czujniki](../api/pl/11_sensors.md) |
| `hal_ble` | [Bluetooth Low Energy](../api/pl/20_bluetooth.md) |
| `hal_gamepad` | [Gamepad Bluetooth Classic HID](../api/pl/20_bluetooth.md#gamepad-bluetooth-classic-hid) |
| `hal_adp5360` | [Czujniki](../api/pl/11_sensors.md) |
| `hal_bits` | [Timery i system](../api/pl/06_timers_system.md) |
| `hal_can` | [CAN i wyświetlacz](../api/pl/10_can_display.md) |
| `hal_command_router` / `hal_command_wire` | [Routing poleceń](../api/pl/23_commands.md) |
| `hal_crc` | [Narzędzia](../api/pl/16_utilities.md) |
| `hal_crypto` | [Kryptografia](../api/pl/07_crypto.md) |
| `cJSON` / `cJSON_Utils` | [cJSON](../api/pl/17_cJSON.md) |
| `LodePNG` | [LodePNG](../api/pl/18_LodePNG.md) |
| `TJpgDec` | [JPEG](../api/pl/19_JPEG.md) |
| `hal_digipot` | [Urządzenia wyjściowe](../api/pl/13_output_devices.md) |
| `hal_display` | [CAN i wyświetlacz](../api/pl/10_can_display.md) |
| `hal_ds18b20` | [Czujniki](../api/pl/11_sensors.md) |
| `hal_dht` | [Czujniki](../api/pl/11_sensors.md) |
| `hal_eeprom` | [Pamięć masowa](../api/pl/14_storage.md) |
| `hal_external_adc` | [Czujniki](../api/pl/11_sensors.md) |
| `hal_gps` | [Czujniki](../api/pl/11_sensors.md) |
| `hal_gpio` | [GPIO, ADC i PWM](../api/pl/05_gpio_adc_pwm.md) |
| `hal_hc595` | [Urządzenia wyjściowe](../api/pl/13_output_devices.md) |
| `hal_i2c` / `hal_i2c_slave` | [Magistrale komunikacyjne](../api/pl/09_buses.md) |
| `hal_kv` | [Pamięć masowa](../api/pl/14_storage.md) |
| `hal_littlefs` | [Pamięć masowa](../api/pl/14_storage.md) |
| `hal_lora_radio` | [Surowe radio LoRa](../api/pl/21_lora.md) |
| `hal_lora_link` | [Niezawodne łącze LoRa](../api/pl/22_lora_link.md) |
| `hal_lora_commands` | [Routing poleceń](../api/pl/23_commands.md) |
| `hal_math` | [Timery i system](../api/pl/06_timers_system.md) / [Urządzenia wyjściowe](../api/pl/13_output_devices.md) |
| `hal_mcp23017` | [Urządzenia wyjściowe](../api/pl/13_output_devices.md) |
| `hal_mcp3221` | [Czujniki](../api/pl/11_sensors.md) |
| `hal_mcp4725` | [Urządzenia wyjściowe](../api/pl/13_output_devices.md) |
| `hal_modem_at` | [Modem komórkowy](../api/pl/12_modem.md) |
| `hal_mqtt` | [Łączność sieciowa](../api/pl/15_connectivity.md) |
| `hal_notify` | [Łączność sieciowa](../api/pl/15_connectivity.md) |
| `hal_onewire` | [Magistrale komunikacyjne](../api/pl/09_buses.md) |
| `hal_ota` | [Łączność sieciowa](../api/pl/15_connectivity.md) |
| `hal_pca9654e` | [Urządzenia wyjściowe](../api/pl/13_output_devices.md) |
| `hal_pcf8574` | [Urządzenia wyjściowe](../api/pl/13_output_devices.md) |
| `hal_pga2311` | [Urządzenia wyjściowe](../api/pl/13_output_devices.md) |
| `hal_pn532` | [Urządzenia wyjściowe](../api/pl/13_output_devices.md) |
| `hal_pid_controller` | [Narzędzia](../api/pl/16_utilities.md) |
| `hal_power` | [Timery i system](../api/pl/06_timers_system.md) |
| `hal_pwm` / `hal_pwm_freq` / `hal_dacless` / `hal_pcnt` | [GPIO, ADC i PWM](../api/pl/05_gpio_adc_pwm.md) |
| `hal_rgb_led` | [Urządzenia wyjściowe](../api/pl/13_output_devices.md) |
| `hal_rtc` | [Czujniki](../api/pl/11_sensors.md) |
| `hal_sc_auth` | [Synchronizacja, port szeregowy, ramkowanie](../api/pl/08_sync_serial.md) |
| `hal_sdlogger` | [Pamięć masowa](../api/pl/14_storage.md) |
| `hal_serial` | [Synchronizacja, port szeregowy, ramkowanie](../api/pl/08_sync_serial.md) |
| `hal_serial_commands` | [Routing poleceń](../api/pl/23_commands.md#adapter-ramkowanej-sesji-szeregowej-framed-serial-session) |
| `hal_serial_frame` | [Synchronizacja, port szeregowy, ramkowanie](../api/pl/08_sync_serial.md) |
| `hal_serial_session` | [Synchronizacja, port szeregowy, ramkowanie](../api/pl/08_sync_serial.md) |
| `hal_simcom_a76xx` | [Modem komórkowy](../api/pl/12_modem.md) |
| `hal_soft_timer` | [Narzędzia](../api/pl/16_utilities.md) |
| `hal_spi` | [Magistrale komunikacyjne](../api/pl/09_buses.md) |
| `hal_swserial` | [Magistrale komunikacyjne](../api/pl/09_buses.md) |
| `hal_sync` | [Synchronizacja, port szeregowy, ramkowanie](../api/pl/08_sync_serial.md) |
| `hal_system` | [Timery i system](../api/pl/06_timers_system.md) |
| `hal_thermocouple` | [Czujniki](../api/pl/11_sensors.md) |
| `hal_time` | [Łączność sieciowa](../api/pl/15_connectivity.md) |
| `hal_usb` | [Synchronizacja, USB, port szeregowy, ramkowanie](../api/pl/08_sync_serial.md) |
| `hal_timer` | [Timery i system](../api/pl/06_timers_system.md) |
| `hal_uart` | [Magistrale komunikacyjne](../api/pl/09_buses.md) |
| `hal_udp` | [Łączność sieciowa](../api/pl/15_connectivity.md) |
| `hal_tcp` | [Łączność sieciowa](../api/pl/15_connectivity.md) |
| `hal_http_server` | [Łączność sieciowa](../api/pl/15_connectivity.md) |
| `hal_http_files` | [Łączność sieciowa](../api/pl/15_connectivity.md) |
| `hal_websocket` | [Łączność sieciowa](../api/pl/15_connectivity.md) |
| `hal_net_console` | [Łączność sieciowa](../api/pl/15_connectivity.md) |
| `hal_net_commands` | [Łączność sieciowa](../api/pl/15_connectivity.md) |
| Adapter gniazd BSD | [Łączność sieciowa](../api/pl/15_connectivity.md) |
| `hal_wifi` | [Łączność sieciowa](../api/pl/15_connectivity.md) |
| `hal_wireguard` | [Łączność sieciowa](../api/pl/15_connectivity.md) |
| `multicoreWatchdog` | [Narzędzia](../api/pl/16_utilities.md) |
| `pidController` | [Narzędzia](../api/pl/16_utilities.md) |
| `SmartTimers` | [Narzędzia](../api/pl/16_utilities.md) |
| `draw7Segment` | [Narzędzia](../api/pl/16_utilities.md) |
