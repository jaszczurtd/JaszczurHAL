# JaszczurHAL - Dokumentacja API

*Dostępne również [po angielsku](../en/JaszczurHAL_API.md).*

Warstwa abstrakcji sprzętowej (Hardware Abstraction Layer) dla systemów
wbudowanych.
Ten dokument zawiera szczegółową dokumentację API.

**Autor:** Marcin 'Jaszczur' Kielesiński

- **Repozytorium:** `git@github.com:jaszczurtd/JaszczurHAL.git`
- **Główny katalog nagłówków:** `libraries/JaszczurHAL/src/` (zarejestrowany w `otherLibrariesFolders`)

---

## Publiczny nagłówek

Użyj:

```cpp
#include <JaszczurHAL.h>
```

W kodzie wewnętrznym lub wymagającym bezpośredniego dostępu do HAL można użyć:

```cpp
#include <hal/hal.h>
```

Dostępne są także nagłówki zawierające wyłącznie narzędzia:

```cpp
#include <tools.h>    // zbiorczy nagłówek narzędzi C++
```

```c
#include <tools_c.h>  // API narzędzi zgodne z C
```

---

## Struktura biblioteki

```text
CMakeLists.txt              # kompilacja testów hosta i backendu mock
VERSION                     # wersja projektu
.build/                     # ignorowany katalog wszystkich zarządzanych artefaktów kompilacji
boards/                     # deskryptory targetów, płytek i możliwości
config/                     # deklaratywny rejestr funkcji HAL i schemat
rp_native_lib/               # kompilacja biblioteki statycznej RP2040/RP2350 na Pico SDK
  MEMORY_MAP.md              # układ natywnego firmware/pamięci/OTA dla RP
cmake/
  esp-idf/                  # kontrolowana natywna receptura komponentu ESP-IDF
  generated/                # wygenerowany mechanizm wyboru funkcji dla CMake
  jh_rp_native_sdk.cmake    # współdzielona integracja CMake biblioteki/firmware RP
  targets/                  # konfiguracje targetów dla narzędzia VS Code
stm32_lib/                  # CMake biblioteki statycznej STM32G474, toolchain, skrypt linkera
scripts/
  # Pełny opis skryptów obsługi repozytorium: doc/api/pl/00_scripts.md
  build_rp_native_lib.sh    # skrypt kompilacji RP ELF/BIN/UF2
  build_stm32_lib.sh        # pomocnik biblioteki statycznej STM32G474
  build_esp_idf.py          # kompilowanie i flashowanie projektu ESP-IDF, obsługa artefaktów
  check_documentation_links.py # lokalna walidacja linków/kotwic Markdown
  ensure_*.sh               # wyspecjalizowane skrypty pobierania i weryfikacji komponentów
  generate_sbom.py          # generator SBOM CycloneDX
  generate_hal_features.py  # walidacja, generowanie i lint rejestru funkcji
  check_vulnerabilities.sh  # opcjonalny lokalny adapter skanera podatności
runalltests.sh              # pełny zestaw lokalnych kontroli
runmefirst.sh                # jednorazowa lokalna konfiguracja toolchainu
doc/
  HAL_FLAGS.txt             # wspólne podsumowanie flag HAL_ENABLE_*
  table_of_contents.md      # angielski spis treści dokumentacji
  table_of_contents.pl.md   # polski spis treści dokumentacji
  en/                       # angielska dokumentacja referencyjna
    features.md             # macierz funkcji wysokiego poziomu
    JaszczurHAL_API.md      # szczegółowy opis API
    FwProjectWorkflow.md    # praca z projektem firmware
    OTAWorkflow.md          # natywna kompilacja, wgrywanie i odzyskiwanie OTA dla RP/ESP
    lib_compilation.md      # przewodnik po kompilacji biblioteki statycznej
    security_supply_chain.md  # proces SBOM i śledzenia podatności
    boards_profiles_howto.md
    windows_setup.md
  pl/                       # polskie tłumaczenia powyższych plików z doc/en/
    features.md             # macierz funkcji wysokiego poziomu
  api/
    en/                     # podzielone rozdziały API (angielski)
      00_scripts.md          # główna dokumentacja procesów i orkiestracji
    pl/                     # podzielone rozdziały API (polski)
examples/                   # przykłady dla rodziny RP i STM32G474
vscode/                     # wspólny punkt wejścia jh-vscode, schemat, dokumentacja, generator
  entry/                    # skrypty startowe dla Uniksa, Windows i publiczny skrypt Pythona
  tools/create-vscode-example.py # samodzielny generator projektu firmware VS Code
  tools/manage_vscode_extensions.py # konfiguracja rozszerzeń po weryfikacji i uzyskaniu zgody
security/
  third_party.json          # inwentarz komponentów firm trzecich
  esp_idf_tools.json        # zweryfikowany wykaz narzędzi targetu ESP-IDF
  sbom.cdx.json             # wygenerowany SBOM CycloneDX
  vulnerability_log.md      # dziennik oceny CVE/CVSS i poprawek
src/
  JaszczurHAL.h              # główny publiczny include
  hal_app_entry.cpp          # opcjonalny przenośny adapter punktu wejścia aplikacji
  libConfig.h                # nagłówek zgodności wstecznej
  tools.h, tools_c.h         # zbiorcze nagłówki narzędzi (C++ / C)
  arpa/, netinet/, sys/      # nagłówki zgodności gniazd hosta i systemów wbudowanych
  hal/                       # zbiorczy nagłówek HAL i wspólne moduły tematyczne
    hal.h                    # include zbiorczy wyłącznie dla HAL
    commands/                # router poleceń niezależny od transportu i format wiadomości
    core/                    # konfiguracja, status, asercje, kompatybilność
    bluetooth/               # publiczne API BLE, fasada i współdzielone wsparcie BTstack
    i2c/, spi/, serial/      # API magistrali i portu szeregowego ze wspólnymi implementacjami
    time/, rtc/, power/      # czas rzeczywisty, wybudzanie RTC i zarządzanie zasilaniem
    timers/                  # API timerów sprzętowych, rozszerzonych, programowych i SmartTimers
    temperature/             # DHT, DS18B20, MAX6675 i MCP9600
    network/                 # rdzeń TCP/UDP/Wi-Fi i wspólny kod obsługi sieci
      http/, mqtt/, ota/    # publiczne API wraz z implementacjami
      tls/, wireguard/      # bezpieczne transporty i wspólne implementacje protokołów
      websocket/            # publiczne API i implementacja WebSocket
      net_console/          # publiczne API i implementacja zdalnej konsoli
      net_commands/         # adapter poleceń HTTP/WebSocket
      cyw43/, lwip/         # integracja radia i stosu IP
    radio/                  # niskopoziomowe LoRa, niezawodne łącze i adapter poleceń
    storage/                # EEPROM, KV, rejestrator SD, system plików, obsługa pamięci flash
    display/, gpio/         # sterowniki wyświetlaczy/GFX i GPIO
    analog/, audio/, can/   # dodatkowe tematyczne domeny API i sterowników
    generated/              # wygenerowane domknięcie funkcji C i raport produkcyjny
    impl/
      .mock/                # deterministyczna implementacja testowa na hoście
      rp2040/               # implementacja rodziny RP
        drivers/flash/      # koordynacja dostępu do pamięci flash RP i jej partycje
        drivers/rp2040/     # usługi SoC RP2040 (awarie/system)
        drivers/usb/        # natywna konfiguracja/deskryptory TinyUSB CDC
        freertos/           # natywny FreeRTOSConfig i hooki RP
        frameworks/         # integracje bibliotek specyficzne dla RP
      stm32g474/            # implementacja STM32G474
        drivers/
          stm32g474/        # usługi SoC STM32G474 (awarie/system)
        freertos/           # FreeRTOSConfig i hooki STM32
        port/               # uruchamianie, SystemInit i funkcje wymagane przez linker
  utils/                    # narzędzia, PID, watchdog, pomocniki rysowania, integracja Unity
tests/                       # testy jednostkowe hosta (CMake + Unity)
  freertos_posix/           # opcjonalne testy schedulera FreeRTOS POSIX po stronie hosta
  hardware/                 # stanowiska RP: źródła, manifesty i programy weryfikujące
third_party/                # definicje wersji oraz pomijane przez Git instalacje komponentów
  update_components.sh      # synchronizuje komponenty z wersjami zapisanymi w repozytorium
  *_version.conf             # wersje źródeł, narzędzi i toolchainów
  littlefs/                 # ignorowana kopia zewnętrznego systemu plików w ustalonej wersji
```

Kod niezależny od targetu znajduje się razem ze swoim publicznym API w
odpowiednim katalogu `src/hal/<domena>/`. Domena może zawierać publiczne
nagłówki `hal_*.h`, wspólne fasady, prywatne pomocniki `jh_*`, podkatalogi
sterowników urządzeń i wspólne mechanizmy. Dzięki temu deklaracje
i implementacje mają jedną tematyczną hierarchię. `src/hal/impl/` jest
zarezerwowany dla portów i backendów specyficznych dla targetu; przenośny kod
domenowy musi zależeć wyłącznie od API na poziomie HAL.

### Wyznaczanie zestawu funkcji podczas kompilacji

Wersjonowany rejestr w `config/features/` generuje produkcyjny kod, który
wyznacza funkcje dla C i CMake. `hal_config.h` dołącza wygenerowane dla C
domknięcie zależności, natomiast konfiguracje CMake dla RP i STM32G474 używają
go do wyboru źródeł i zależności. Skrypt ESP-IDF przetwarza ten sam zestaw żądań
i zależności, odrzuca funkcje spoza listy dozwolonej przez deskryptor targetu
oraz wybiera obsługiwane źródła podstawowe, peryferyjne i sieciowe. Zapisuje
funkcje żądane i wynikowe wraz z informacjami o pochodzeniu danych płytki i
konfiguracji linkowania.
Generator płytek zapisuje zarówno `requestedFeatures`, jak i
`resolvedFeatures`; skrót zestawu funkcji i sygnatura linkowania są obliczane
ze zbioru po rozwiązaniu zależności. `jh-vscode` wyznacza dla aktywnego profilu
i wariantu to samo domknięcie. Polecenie `config-dump` podaje skrót rejestru,
skrót domknięcia oraz pochodzenie żądań.

Warunkowe wartości domyślne, wybory implementacji, sprawdzenia możliwości płytki
oraz ograniczenia targetu pozostają w `hal_config.h`. `HAL_CONFIG_VERBOSE`
aktywuje wygenerowany raport obejmujący każdą aktywną flagę z rejestru. CI
traktuje niezgodność wygenerowanych plików z rejestrem oraz błędy kontroli
bezpośredniej i wynikowej konfiguracji funkcji jako niepowodzenie.
Zainstalowane pakiety RP i STM32G474 zawierają wygenerowane nagłówki
funkcji i płytki, plik JSON z wynikiem, nagłówek sygnatury linkowania oraz
referencyjny plik źródłowy. Dzięki temu projekty wywołujące kompilator
bezpośrednio mogą zbudować i zlinkować ustalony pakiet bez uruchamiania Pythona.

- `CMakeLists.txt` - kompilacja testów hosta i backendu mock w katalogu głównym
  repozytorium.
- `rp_native_lib/` - oficjalna biblioteka statyczna Pico SDK i testowe obrazy
  firmware.
- `stm32_lib/` - CMake biblioteki statycznej STM32G474, plik toolchainu i skrypt
  linkera.
- `scripts/build_rp_native_lib.sh` - pomocnik biblioteki statycznej RP2040/RP2350
  i opcjonalnych testowych obrazów firmware, w tym tryb `--library-only`
  generujący wyłącznie archiwum oraz opcjonalną macierz testową dla FreeRTOS
  SMP w ustalonej wersji.
- `scripts/build_stm32_lib.sh` - pomocnik biblioteki statycznej STM32G474.
- `scripts/build_esp_idf.py` - produkcyjny skrypt kompilacji projektu
  ESP-IDF, walidacji artefaktów i flashowania z relokowalnym manifestem
  wieloobrazowym.
- `third_party/update_components.sh` - synchronizuje BearSSL, cJSON, LodePNG,
  TJpgDec, FatFs, Unity, lwIP, littlefs, BTstack, sterownik Semtech SX126x,
  FreeRTOS, Pico SDK, picotool, PMD CPD oraz toolchain RISC-V dla RP2350
  z wersjami zapisanymi w `third_party/*_version.conf`. Środowisko ESP-IDF jest
  przygotowywane na żądanie przez skrypt produkcyjny lub wyspecjalizowane
  polecenie `ensure`.
- `scripts/generate_sbom.py` - deterministyczny generator SBOM CycloneDX
  z trybem `--check` tylko do odczytu.
- `scripts/check_sbom.sh` - zgodny wstecznie skrypt do sprawdzania samego SBOM;
  za synchronizację całego repozytorium odpowiada wspólny mechanizm obsługi
  artefaktów generowanych.
- `scripts/check_vulnerabilities.sh` - opcjonalny lokalny adapter skanera
  podatności, który regeneruje SBOM i uruchamia dostępne skanery zależności
  pobieranych ze źródeł oraz dołączonych do repozytorium.
- `doc/api/pl/00_scripts.md` - główny opis działania skryptów obsługi
  repozytorium, punktów wejścia orkiestracji, opcji, artefaktów i relacji
  między nimi.
- `runalltests.sh` - pełny zestaw lokalnych kontroli.
- `runmefirst.sh` - jednorazowa lokalna konfiguracja toolchainu.
- `doc/pl/FwProjectWorkflow.md` - praca z projektem firmware: model
  manifestu, wybór targetu i płytki, wykrywanie źródeł, wgrywanie,
  kompilacja debugowa, katalogi kompilacji i pliki generowane.
- `doc/pl/boards_profiles_howto.md` - deklaratywne deskryptory targetu/płytki,
  generowana konfiguracja, biblioteki statyczne uwzględniające wybraną płytkę
  oraz procedura dodawania fizycznej płytki.
- `doc/pl/OTAWorkflow.md` - wymagania natywnego OTA dla RP i ESP32-S3: integracja
  firmware, artefakty specyficzne dla targetu, wgrywanie z VS Code, zapora
  sieciowa, potwierdzenie próbne, wycofanie aktualizacji i odzyskiwanie.
- `SECURITY.md` - zgłaszanie i wstępna ocena podatności oraz polityka
  utrzymania.
- `security/third_party.json` - ręcznie utrzymywany inwentarz komponentów firm
  trzecich.
- `security/sbom.cdx.json` - wygenerowany SBOM CycloneDX.
- `security/vulnerability_log.md` - dziennik oceny CVE/CVSS i decyzji
  o poprawkach.
- `src/JaszczurHAL.h` - zbiorczy nagłówek modułów HAL i narzędzi.
- `doc/HAL_FLAGS.txt` - zwięzłe podsumowanie flag `HAL_ENABLE_*`.
- `src/libConfig.h` - zgodne wstecznie przekierowanie do
  `hal/core/hal_config.h`.
- `src/tools.h` - zbiorczy nagłówek narzędzi C++.
- `src/tools_c.h` - deklaracje narzędziowe kompatybilne z C.
- `src/hal/hal.h` - zbiorczy nagłówek samego HAL.
- `src/hal/core/hal_config.h` - zgodna wstecznie fasada wyboru funkcji
  podczas kompilacji, propagacji zależności i konfiguracji projektu.
- `src/hal/core/hal_runtime_config.h` oraz `src/hal/core/hal_config.cpp` - API
  i implementacja konfiguracji limitów puli w czasie działania.
- `src/hal/core/hal_assert.h` oraz `src/hal/core/hal_assert.cpp` - przenośne
  API asercji i obsługa błędów dostosowana do targetu.
- `src/hal/core/hal_compat.h` - pomocnicze makra `PROGMEM`, `F()`, `hal_min()`
  i `hal_max()` zapewniające kompatybilność źródłową.
- `src/hal/<domain>/hal_*.h` - publiczne interfejsy modułów HAL pogrupowane
  tematycznie, np. GPIO, magistrale, port szeregowy, bezpieczeństwo, czujniki,
  pamięć masowa, wyświetlacz i sieć.
- `src/hal/can/hal_can_util.cpp`, `src/hal/security/hal_crypto.cpp`,
  `src/hal/security/hal_crc.cpp`, `src/hal/gps/hal_gps.cpp`,
  `src/hal/storage/hal_kv.cpp`, `src/hal/audio/hal_pga2311.cpp`,
  `src/hal/rtc/hal_rtc.cpp`, `src/hal/timers/hal_soft_timer.cpp`,
  `src/hal/control/hal_pid_controller.cpp` - wspólne implementacje adapterów
  i fasad HAL.
- `src/hal/time/hal_time_ntp.cpp` oraz `src/hal/storage/hal_eeprom.cpp` - wspólna,
  bezpieczna wątkowo obsługa czasu systemowego, NTP i RTC oraz fasada EEPROM
  z wyborem implementacji. Przenośny kod AT24C256 i buforowanej pamięci flash
  pozostaje obok API EEPROM, natomiast katalogi targetów dostarczają wyłącznie
  fizyczne mechanizmy obsługi flasha.
- `src/hal/serial/hal_uart_config.h` - stałe konfiguracyjne i pomocniki UART.
- `src/hal/core/hal_status.h` - współdzielone kody wyniku `hal_status_t` dla
  nowych publicznych API.
- `src/hal/system/hal_board.h` oraz `src/hal/system/hal_board.cpp` - identyfikacja
  profilu płytki, dane sprzętowe ustalone podczas kompilacji oraz stan
  dostępności funkcji sprzętowych w czasie działania, przechowywany w sposób
  bezpieczny wątkowo.
- `src/hal/impl/rp2040/` - implementacja rodziny RP.
- `src/hal/impl/stm32g474/` - implementacja STM32G474 (działające, rejestrowe
  implementacje modułów podstawowych; pozostałe moduły są w toku).
- `src/hal/impl/.mock/` - deterministyczna implementacja testowa na hoście.
- `src/hal/<domain>/` - publiczne nagłówki i implementacje niezależne od
  targetu w jednym tematycznym katalogu; przykłady to `bluetooth/`,
  `network/`, `time/`, `timers/`, `temperature/`, `i2c/`, `spi/`, `display/`
  i `storage/`.
- `src/hal/impl/rp2040/drivers/` - dołączone niskopoziomowe sterowniki firm
  trzecich, używane przez opcjonalne moduły HAL.
- `src/hal/impl/rp2040/drivers/rp2040/` - sterowniki specyficzne dla SoC:
  `rp2040_fault.{h,cpp}` (przechwytywanie HardFault, strażnik stosu,
  zapisywanie przyczyny resetu) oraz `rp2040_system.{h,cpp}` (watchdog, wejście
  USB-boot, temperatura układu, wolna pamięć sterty, unikalny identyfikator
  płytki i obsługa stanu bezczynności).
- `src/hal/impl/stm32g474/drivers/stm32g474/` - sterowniki specyficzne dla SoC:
  `stm32g474_fault.{h,cpp}` (klasyfikacja przyczyny resetu, przechowywanie
  informacji o awarii do następnego uruchomienia i strażnik stosu) oraz
  `stm32g474_system.{h,cpp}` (czas, opóźnienia, watchdog, temperatura, UID
  i funkcje bezczynności uwzględniające kontekst ISR).
- `src/hal/network/mqtt/PubSubClient/` oraz
  `src/hal/network/wireguard/core/` - dołączone silniki sieciowe neutralne
  względem targetu.
- `src/utils/` - narzędzia wyższego poziomu: `tools`, `pidController`, `multicoreWatchdog`, `draw7Segment` oraz zarządzana integracja Unity.

---

## Mapy pamięci

Uwagi dotyczące układu pamięci specyficznego dla targetu znajdują się obok
obsługi kompilacji dla każdej implementacji:

- [Mapa pamięci RP](../../rp_native_lib/MEMORY_MAP.md) - układy linkera dla
  aplikacji i OTA, obszary pamięci flash z trwałymi danymi, SRAM, sterta
  i stosy.
- [Mapa pamięci STM32G474](../../stm32_lib/MEMORY_MAP.md) - regiony linkera
  bare-metal, zarezerwowane strony flash EEPROM/KV, sekcje RAM, sterta i stos.

---

## Sugerowana kolejność czytania dokumentacji

- [00_scripts.md](../api/pl/00_scripts.md): główna dokumentacja działania
  JaszczurHAL. Wyjaśnia współdziałanie konfiguracji, zarządzania
  zależnościami, kompilacji, przykładów, walidacji, narzędzi bezpieczeństwa
  i orkiestracji VS Code
- [FwProjectWorkflow.md](FwProjectWorkflow.md): obsługa projektów firmware
  przez wspólny mechanizm sterujący, obejmująca manifest, target, źródła,
  kompilację
  i wgrywanie
- [OTAWorkflow.md](OTAWorkflow.md): natywna konfiguracja OTA dla RP i ESP32-S3,
  przygotowanie urządzenia, wgrywanie, konfigurację sieci i zapory,
  potwierdzenie, wycofanie aktualizacji i odzyskiwanie

Każdy dokument szczegółowo opisuje własny zakres. Pozostałe powinny jedynie
nakreślać kontekst i odsyłać do dokumentu źródłowego, zamiast powtarzać
polecenia, interfejsy lub przykłady konfiguracji.

---

## Publiczne API a moduły pomocnicze

Repozytorium zawiera zarówno sam HAL, jak i zestaw modułów narzędziowych.

### Publiczne API HAL

Te interfejsy oddzielają przenośną logikę aplikacji od wywołań SDK właściwych
dla konkretnej płytki:

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
- `hal_command_router` i `hal_command_wire` do niezależnego od transportu
  kierowania poleceń, egzekwowania reguł i obsługi binarnych wiadomości
  o ograniczonym rozmiarze
- `hal_lora_radio` do niskopoziomowej obsługi LoRa niezależnej od implementacji,
  z obsługą rodzin SX126x i SX127x
- `hal_lora_link` dla adresowanych, potwierdzanych, fragmentowanych
  prywatnych wiadomości przesyłanych przez pojedyncze radio LoRa, z opcjonalnym
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
- opcjonalny hook znacznika czasu dla logowania błędów, ustawiany funkcją
  `hal_debug_set_timestamp_hook(...)`

### Moduły pomocnicze / narzędziowe

Są to wygodne dodatki, ale same w sobie nie stanowią granicy przenośności:

- `tools`
- `SmartTimers`
- `pidController`
- `multicoreWatchdog`
- `draw7Segment`

W nowym kodzie aplikacji korzystaj przede wszystkim z warstwy HAL. Moduły
pomocnicze są przydatnymi elementami składowymi, ale nie powinny zastępować
granicy wyznaczonej przez HAL.

---

## Profile płytek i możliwości dostępne w czasie działania

`HAL_TARGET_*` identyfikuje MCU i architekturę (ISA). `JH_BOARD` wybiera
fizyczny profil z `boards/profiles/`; generator tworzy odpowiedni selektor
`HAL_BOARD_PROFILE_*` oraz konfigurację targetu. Deskryptory płytek są
miarodajnym wykazem profili. Wypisz bieżące identyfikatory poleceniem:

```bash
python3 scripts/generate_board_config.py --boards-root boards --list boards
```

Komponent ESP32-S3 korzysta z wygenerowanych danych targetu i płytki oraz
metadanych linkowania, aby udostępnić publiczne API `hal_board` w czasie
działania.
Moduł odpowiedzialny za daną możliwość aktualizuje jej stan. Możliwość
zadeklarowana przez płytkę pozostaje w stanie `HAL_BOARD_CAP_INACTIVE`, dopóki
moduł nie zgłosi jej dostępności albo błędu.

Podczas kompilacji `HAL_BOARD_DECLARED_CAPABILITIES` opisuje sprzęt zamontowany
na płytce. Na targetach udostępniających tę fasadę użytkownicy
powinni odpytywać `hal_board_get_info()` lub
`hal_board_get_capability_state()`, a następnie używać
`hal_board_require_capabilities()` przed operacjami wymagającymi dowolnej
kombinacji możliwości: `HAL_BOARD_CAP_USB_DEVICE`, `HAL_BOARD_CAP_CYW43`,
`HAL_BOARD_CAP_EXTERNAL_RADIO_FRONTEND`, `HAL_BOARD_CAP_SX1262_RADIO`,
`HAL_BOARD_CAP_BLUETOOTH_CONTROLLER` lub `HAL_BOARD_CAP_NATIVE_WIFI`.
Każda zadeklarowana możliwość ma początkowo stan `HAL_BOARD_CAP_INACTIVE`.
Odpowiedni moduł zmienia go na `AVAILABLE` albo `FAILED`. Implementacja CYW43
na RP
aktualizuje te stany podczas inicjalizacji i deinicjalizacji.

`hal/system/hal_board.h` definiuje stabilny typ wyliczeniowy profili, maskę
bitową możliwości, stany dostępne w czasie działania, strukturę z informacjami
o płytce oraz funkcje odczytujące te dane.
Wygenerowany `src/hal/generated/jh_board_registry.h` mapuje każdy profil
rejestru na tę publiczną tożsamość, bez utrzymywania tu drugiej, ręcznie
pisanej listy profili.

`hal_board_require_capabilities()` zwraca `HAL_OK`, gdy wszystkie żądane
możliwości są dostępne; `HAL_EUNSUPPORTED`, gdy płytka nie deklaruje którejś
z nich; `HAL_EUNINIT`, dopóki choć jedna zadeklarowana możliwość pozostaje
nieaktywna; oraz `HAL_EHW`, gdy odpowiedzialny za nią moduł zgłosił błąd:

```c
if (hal_status_is_ok(hal_board_require_capabilities(HAL_BOARD_CAP_CYW43))) {
    /* Na tej płytce można bezpiecznie używać radia. */
}
```

---

## Sekcje dokumentacji API

Kompletna dokumentacja referencyjna jest podzielona na następujące dokumenty
tematyczne:

| # | Plik | Zawartość |
|---|------|----------|
| 0 | [Skrypty obsługi repozytorium i orkiestracja](../api/pl/00_scripts.md) | Kluczowa architektura operacyjna: konfiguracja stacji roboczej, zarządzane zależności, punkty wejścia kompilacji, przykłady, walidacja, narzędzia bezpieczeństwa, integracja VS Code, zarządzanie artefaktami oraz relacje między tymi procesami |
| 1 | [Przewodnik po kompilacji biblioteki](lib_compilation.md) | Kompilacja dla wszystkich targetów, generowane metadane płytki, skrypty bibliotek statycznych, warianty FreeRTOS oraz integracja firmware |
| P | [Praca z projektem firmware](FwProjectWorkflow.md) | Projekty firmware VS Code obsługiwane przez wspólny mechanizm sterujący: model manifestu, wybór targetu i płytki, wykrywanie źródeł, osobny cache CMake dla każdego targetu, wgrywanie, kompilacja debugowa oraz pliki generowane |
| O | [Natywne aktualizacje OTA](OTAWorkflow.md) | Konfiguracja projektu i firmware OTA właściwa dla RP i ESP32-S3, artefakty, pierwsze flashowanie, integracja VS Code, zapora sieciowa, potwierdzenie, wycofanie aktualizacji, odzyskiwanie oraz granica bezpieczeństwa |
| 2 | [Flagi modułów i konfiguracja](../api/pl/02_module_flags.md) | Jawnie włączane flagi `HAL_ENABLE_*`, propagacja zależności, wybór FreeRTOS, nadpisania rozmiaru stosu oraz moduły rdzeniowe |
| A | [API kodów statusu](../api/pl/01_status_api.md) | Podstawowe zasady wspólne dla modułów: kody wyniku `hal_status_t`, zastępowanie dotychczasowych operacji `void`, które mogą się nie powieść, wersjami zwracającymi status, warianty `_ex` dla zachowanych API zwracających wartość, uchwyt lub `bool`, formy z parametrem wyjściowym oraz alternatywne nazwy na wypadek kolizji. |
| 3 | [Zależności kompilacji, testy i stanowiska sprzętowe](../api/pl/03_build_tests.md) | Architektura testów i miarodajne źródła, zależności, wykonanie na hoście i w CI, pełny wykaz zestawów testów, zasady rozszerzania, sterowanie czasem w implementacji mock oraz scentralizowane procedury i wyniki testów sprzętowych |
| 4 | [Bezpieczeństwo wielordzeniowe i sterowniki](../api/pl/04_multicore_drivers_migration.md) | Zasady inicjalizacji i działania wielordzeniowego, wykaz dołączonych sterowników i licencji, funkcja znacznika czasu logowania, konwersja czasu, przegląd przykładów, pokrycie testami na hoście oraz mapowanie na przenośne API |
| S | [Bezpieczeństwo łańcucha dostaw](security_supply_chain.md) | Wykaz komponentów firm trzecich, generowanie SBOM CycloneDX, skanowanie podatności oraz proces oceny CVE/CVSS |
| 5 | [GPIO, ADC i PWM](../api/pl/05_gpio_adc_pwm.md) | `hal_gpio`, `hal_pwm`, `hal_dac`, `hal_pcnt`, `hal_pwm_freq`, `hal_dacless`, `hal_adc` |
| 6 | [Timery i system](../api/pl/06_timers_system.md) | `hal_timer` (alarmy i zarządzane timery), `hal_system` (czas w milisekundach, watchdog, diagnostyka awarii i UID), `hal_power` (uśpienie, głębokie uśpienie i wyłączenie zasilania), `hal_bits`, `hal_compiler` (przenośne atrybuty i funkcje wbudowane), `hal_math` |
| 7 | [Kryptografia](../api/pl/07_crypto.md) | `hal_crypto` - Base64, MD5, SHA-256, HMAC-SHA256, ChaCha20, ChaCha20-Poly1305 |
| 8 | [Synchronizacja, USB, port szeregowy, ramkowanie i uwierzytelnianie](../api/pl/08_sync_serial.md) | `hal_sync` (muteks i sekcja krytyczna), `hal_usb` (cykl życia USB i CDC oparty na statusie), `hal_serial` (jeden moduł szeregujący TX, transport wybierany podczas linkowania, strumieniowe formatowanie diagnostyki, logowanie odroczone z ISR i ograniczanie częstotliwości), `hal_serial_session` (ramkowany protokół SC), `hal_serial_commands` (adapter routera tekstowego), `hal_serial_frame` (kodek wiadomości), `hal_sc_auth` (wyzwanie i odpowiedź HMAC) |
| 9 | [Magistrale komunikacyjne](../api/pl/09_buses.md) | `hal_spi` (transfer `_ex` zwracający status i obsługa DMA), `hal_i2c` (API kontrolera zwracające status, ograniczony skaner z funkcją zwrotną watchdoga, operacje jednorazowe i czyszczenie magistrali), `hal_i2c_slave` (mapa rejestrów), `hal_uart`, `hal_swserial`, `hal_onewire` |
| 10 | [Magistrala CAN i wyświetlacz](../api/pl/10_can_display.md) | `hal_can` (wybór klasycznego CAN MCP2515, CAN FD MCP251XFD lub natywnego FDCAN STM32G474), `hal_display` (fasada TFT/OLED/LCD/EPD zwracająca status, bezpośredni zapis pikseli, odświeżanie EPD, prymitywy GFX, strumieniowanie, tekst i czcionki) |
| 11 | [Czujniki](../api/pl/11_sensors.md) | `hal_thermocouple` (jedna fasada wybierająca MCP9600, MAX6675 lub implementację mock), `hal_ds18b20` (tryb nieblokujący), `hal_dht` (DHT11/DHT22), `hal_bh1750` (natężenie światła otoczenia), `hal_adp5360` (PMIC: ładowarka, pomiar poziomu baterii i regulatory), `hal_mcp3221` (12-bitowy ADC I2C), `hal_rtc` (PCF8563, DS3231, wewnętrzny AON i wybudzanie względne), `hal_external_adc` (ADS1115), `hal_gps` (NMEA, automatyczne wykrywanie ramkowania) |
| 12 | [Modem komórkowy](../api/pl/12_modem.md) | `hal_modem_at` (silnik AT, URC, współpraca z watchdogiem), `hal_simcom_a76xx` (A7670/A7672 - zasilanie, rozruch, SIM, PDP, LBS, GNSS, subskrypcja MQTT) |
| 13 | [Urządzenia wyjściowe](../api/pl/13_output_devices.md) | `hal_rgb_led` (NeoPixel, transport PIO/GPIO), `hal_digipot` (cyfrowe potencjometry I2C MCP401x/MAX5395), `hal_pga2311` (sterownik głośności stereo), `hal_mcp23017`/`hal_pca9654e`/`hal_pcf8574` (ekspandery GPIO/wyjść I2C), `hal_hc595` (ekspander wyjść SPI z rejestrem przesuwnym), `hal_mcp4725` (12-bitowy DAC I2C), `hal_mfrc522`/`hal_pn532` (czytniki RFID/NFC), `hal_math` (constrain, map, hal_math_round_to_n) |
| 14 | [Pamięć masowa](../api/pl/14_storage.md) | `hal_eeprom` (pamięć flash targetu lub AT24C256), `hal_kv` (magazyn KV tylko do dopisywania z odśmiecaniem), `hal_littlefs` (montowanie i formatowanie LittleFS), `hal_sdlogger` (buforowany rejestrator na karcie SD i zapis awarii) |
| 15 | [Łączność sieciowa](../api/pl/15_connectivity.md) | API `_ex` zwracające status dla `hal_wifi`, resolvera, `hal_udp`, `hal_tcp`, `hal_tls`, `hal_mqtt` i `hal_wireguard`; `hal_http_server`, `hal_http_files`, `hal_websocket`, `hal_net_console`, `hal_net_commands`, `hal_notify`, niezależny adapter gniazd BSD z `getaddrinfo()` i opcjonalnym transportem TLS, `hal_ota`, zawsze dostępne pomocniki kalendarzowe `hal_time` oraz opcjonalny NTP/czas lokalny |
| 16 | [Narzędzia](../api/pl/16_utilities.md) | tematyczne helpery tablic/endian/matematyki/tekstu/ADC/NTC/pikseli/obrazów/sieci/czasu, nagłówki zgodnościowe, `hal_soft_timer`, `hal_pid_controller`, `hal_crc`, `SmartTimers`, `pidController`, `multicoreWatchdog`, `draw7Segment` |
| 17 | [cJSON](../api/pl/17_cJSON.md) | Zarządzane `cJSON` / `cJSON_Utils`, sposoby dołączania nagłówków, reguły zarządzania obiektami i pamięcią, parsowanie, wypisywanie, przykłady JSON Pointer/Patch/Merge Patch |
| 18 | [LodePNG](../api/pl/18_LodePNG.md) | Zarządzany `LodePNG`, sposoby dołączania nagłówków, profil dla systemów wbudowanych, zasady zarządzania pamięcią, skrypt zasobów PNG/Base64 i przykłady RGB565 |
| 19 | [JPEG](../api/pl/19_JPEG.md) | Zarządzany rdzeń `TJpgDec`, profil dla systemów wbudowanych, zasady zarządzania pamięcią, skrypt zasobów JPEG/Base64 i przykłady RGB565 |
| 20 | [Bluetooth](../api/pl/20_bluetooth.md) | BLE Peripheral/Observer, wykrywanie, parowanie i zapisane urządzenia Classic, surowy HID Host, adapter gamepada, A2DP Sink, AVRCP Target, uwierzytelniony Stream, ograniczone kolejki, obsługa płytek, współdzielenie radia i zasady dystrybucji BTstack |
| 21 | [Niskopoziomowe API radia LoRa](../api/pl/21_lora.md) | Zweryfikowane profile SX1262 oraz eksperymentalne, wyłącznie programowe integracje SX1261/SX1276/SX1278, asynchroniczne TX/RX/CAD, bieżące RSSI, obsługiwane funkcje, funkcje zwrotne, diagnostyka i czas transmisji |
| 22 | [Niezawodne łącze LoRa](../api/pl/22_lora_link.md) | 16-bitowe adresowanie, numery sekwencyjne wiadomości, ACK i ponawianie, tłumienie duplikatów, fragmentacja oraz opcjonalne ChaCha20-Poly1305 na `hal_lora_radio` |
| 23 | [Kierowanie poleceń](../api/pl/23_commands.md) | Niezależna od transportu rejestracja procedur obsługi i reguł, binarne komunikaty żądania, odpowiedzi i zdarzenia o ograniczonym rozmiarze, zgodność z warstwą sieciową oraz adaptery ramkowanego portu szeregowego, niezawodnego LoRa i uwierzytelnionego BLE Stream |

---

## Szybkie wyszukiwanie modułów

| Moduł | Sekcja |
|--------|---------|
| `hal_adc` | [GPIO, ADC i PWM](../api/pl/05_gpio_adc_pwm.md) |
| `hal_bh1750` | [Czujniki](../api/pl/11_sensors.md) |
| `hal_ble` | [Bluetooth Low Energy](../api/pl/20_bluetooth.md) |
| `hal_ble_stream` | [JH BLE Stream v1](../api/pl/20_bluetooth.md#jh-ble-stream-v1) |
| `hal_ble_commands` | [Uwierzytelniony adapter BLE Stream](../api/pl/23_commands.md#uwierzytelniony-adapter-ble-stream) |
| `hal_bluetooth_classic` | [Manager Bluetooth Classic](../api/pl/20_bluetooth.md#manager-classic) |
| `hal_bluetooth_hid_host` | [Ogólny Classic HID Host](../api/pl/20_bluetooth.md#ogólny-hid-host) |
| `hal_gamepad` | [Adapter gamepada Bluetooth](../api/pl/20_bluetooth.md#adapter-gamepada) |
| `hal_bluetooth_a2dp_sink` | [A2DP Sink](../api/pl/20_bluetooth.md#a2dp-sink-i-avrcp-target) |
| `hal_bluetooth_avrcp_target` | [AVRCP Target](../api/pl/20_bluetooth.md#a2dp-sink-i-avrcp-target) |
| `hal_adp5360` | [Czujniki](../api/pl/11_sensors.md) |
| `hal_bits` | [Timery i system](../api/pl/06_timers_system.md) |
| `hal_can` | [CAN i wyświetlacz](../api/pl/10_can_display.md) |
| `hal_command_router` / `hal_command_wire` | [Kierowanie poleceń](../api/pl/23_commands.md) |
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
| `hal_lora_radio` | [Niskopoziomowe API radia LoRa](../api/pl/21_lora.md) |
| `hal_lora_link` | [Niezawodne łącze LoRa](../api/pl/22_lora_link.md) |
| `hal_lora_commands` | [Kierowanie poleceń](../api/pl/23_commands.md) |
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
| `hal_serial_commands` | [Kierowanie poleceń](../api/pl/23_commands.md#adapter-ramkowanej-sesji-szeregowej-framed-serial-session) |
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
