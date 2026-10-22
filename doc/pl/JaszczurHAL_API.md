# JaszczurHAL - Dokumentacja API

*Dostępne również [po angielsku](../en/JaszczurHAL_API.md).*

JaszczurHAL udostępnia wspólne API do obsługi sprzętu w aplikacjach wbudowanych. Ten przewodnik opisuje organizację biblioteki, konfigurację funkcji i sposób korzystania z dokumentacji poszczególnych modułów.

**Autor:** Marcin 'Jaszczur' Kielesiński

- **Repozytorium:** `git@github.com:jaszczurtd/JaszczurHAL.git`
- **Katalogi nagłówków:** `src/` w checkoucie HAL oraz `third_party/jhbase/include` (kody statusów i `COUNTOF`; buildy CMake HAL dodają go same)

---

## Publiczny nagłówek

W aplikacji dołącz zbiorczy nagłówek:

```c
#include <JaszczurHAL.h>
```

W kodzie wewnętrznym lub wymagającym bezpośredniego dostępu do warstwy HAL można dołączyć:

```c
#include <hal/hal.h>
```

Aby korzystać wyłącznie z funkcji narzędziowych, dołącz odpowiedni nagłówek:

```cpp
#include <tools.h>    // zbiorczy nagłówek narzędzi C++
```

```c
#include <tools_c.h>  // API narzędzi zgodne z C
```

### API C

W nowym kodzie aplikacji używaj deklaracji C z `JaszczurHAL.h` albo z
odpowiedniego tematycznego nagłówka `hal_*`. Funkcje, których działanie
może zakończyć się błędem, zwracają `hal_status_t`.
W razie potrzeby moduł przechowujący stan udostępnia uchwyt do struktury z
ukrytymi polami (ang. opaque handle).
Takie uchwyty mają między innymi moduły `hal_hd44780`, `hal_dacless`,
`hal_mfrc522` i `hal_pn532`. Prosta funkcja pomocnicza z nagłówka
`utils/draw7Segment.h` jest również dostępna bezpośrednio z kodu C.

Plik `.c` może bezpośrednio dołączać te nagłówki. Część implementacji
JaszczurHAL nadal jest kompilowana jako C++, dlatego do końcowego linkowania
użyj dostarczonej integracji CMake albo linkera C++, jeśli ręcznie integrujesz
archiwum.

### API C++

Wybrane publiczne nagłówki `hal_*` podczas kompilacji jako C++ nadal
udostępniają dotychczasowe klasy. Nagłówki zgodnościowe narzędzi zachowują też
wcześniejsze nazwy funkcji. Te elementy API pozostają utrzymywane, więc
istniejące aplikacje C++ nie wymagają natychmiastowego przepisania. W tej
dokumentacji najpierw opisano API C zalecane dla nowego kodu, a odpowiadające
mu dotychczasowe API C++ znajduje się w osobnej sekcji.

`tools.h` pozostaje zbiorczym nagłówkiem narzędzi C++. Kod C powinien dołączać
`tools_c.h` albo właściwy nagłówek tematyczny.

---

## Struktura biblioteki

Przenośne moduły mają publiczne nagłówki, współdzielone implementacje i sterowniki
urządzeń w katalogach `src/hal/<domain>/`. Kod zależny od platformy znajduje się
w `src/hal/impl/`. Kod współdzielony korzysta z API HAL, zamiast wywoływać
bezpośrednio implementację konkretnej platformy.

| Miejsce | Przeznaczenie |
|---|---|
| `src/hal/core/` | Konfiguracja, kody statusów, asercje, funkcje kompilatora i limity pul ustalane podczas działania. |
| `src/hal/<domain>/` | API i przenośne implementacje magistral, czujników, pamięci, wyświetlaczy, sieci i pozostałych modułów. |
| `src/hal/impl/rp2040/` | Implementacje RP2040 i RP2350, w tym koordynacja operacji flash i obsługa USB. |
| `src/hal/impl/stm32g474/` | Implementacje peryferiów STM32G474, kod startowy i integracja FreeRTOS. |
| `src/hal/impl/esp32/` | Implementacje rodziny ESP32 korzystające z ESP-IDF. |
| `src/hal/impl/.mock/` | Implementacje hostowe do testów, z opcjonalną obsługą sieci przez system operacyjny. |
| `src/utils/` | Narzędzia zgodnościowe C++, PID, watchdog, funkcje rysowania i integracja Unity. |
| `boards/`, `config/` | Dane platform i płytek, zależności funkcji oraz schematy i typy sprzętu. |
| `cmake/`, `link_libraries/` | Integracja firmware i konfiguracje kompilacji bibliotek statycznych. |
| `tools/jh/` | Hostowe CLI: odczyt JSON, walidacja schematów i generator domyślnych wartości urządzeń na katalogu typów. |
| `scripts/`, `vscode/` | Narzędzia konfiguracji, generowania, kompilacji, wgrywania, debugowania i kontroli. |
| `tests/` | Testy hostowe i niezależne dane testowe; testy urządzeń wybiera się osobno z `tests/hardware/`. |
| `third_party/` | Pliki wersji i ignorowane instalacje zarządzanych zależności. |
| `security/` | Wykaz zależności, generowany SBOM i oceny podatności. |
| `doc/` | Poradniki i rozdziały API po angielsku i polsku. |

[Przegląd funkcji](features.md) i [poradnik kompilacji i testów](../api/pl/03_build_tests.md)
opisują obsługę platform oraz zakres jej weryfikacji. Sama obecność katalogu
nie potwierdza działania wszystkich modułów na sprzęcie.

Punkty wejścia narzędzi opisuje [poradnik skryptów](../api/pl/00_scripts.md),
archiwa i generowane nagłówki [poradnik kompilacji biblioteki](lib_compilation.md),
a przygotowanie zależności [poradnik komponentów](../../third_party/README.pl.md).
Osobny projekt hostowy opisuje [instrukcja jh CLI](../../tools/jh/README.pl.md).
Deklaracje rdzenia znajdują się w `hal/core/hal_runtime_config.h`, `hal_assert.h`
i `hal_compat.h`; ich parametry i ograniczenia opisują te nagłówki.

### Wyznaczanie zestawu funkcji podczas kompilacji

Rejestr w `config/features/` jest podstawą generowanej konfiguracji funkcji dla C i CMake. `hal_config.h` dołącza nagłówek uwzględniający zależności modułów, a konfiguracje CMake RP i STM32G474 wybierają na tej podstawie źródła i biblioteki. Narzędzie ESP-IDF stosuje te same reguły, odrzuca funkcje spoza listy dozwolonej dla platformy i wybiera obsługiwane źródła modułów podstawowych, peryferyjnych i sieciowych. Zapisuje też żądane i wynikowe funkcje wraz z pochodzeniem danych płytki oraz konfiguracji linkowania. Generator płytek przechowuje oba zbiory jako `requestedFeatures` i `resolvedFeatures`; skrót funkcji i sygnatura linkowania są obliczane ze zbioru wynikowego. `jh-vscode` wyznacza ten sam zestaw dla aktywnego profilu i wariantu. Polecenie `config-dump` pokazuje skróty rejestru i wynikowego zestawu oraz źródła żądań funkcji.

Warunkowe wartości domyślne, wybory implementacji, sprawdzenia możliwości płytki
oraz ograniczenia targetu pozostają w `hal_config.h`. `HAL_CONFIG_VERBOSE`
aktywuje wygenerowany raport obejmujący każdą aktywną flagę z rejestru. CI
traktuje niezgodność wygenerowanych plików z rejestrem oraz błędy kontroli
bezpośredniej i wynikowej konfiguracji funkcji jako niepowodzenie.
Zainstalowane pakiety RP i STM32G474 zawierają wygenerowane nagłówki
funkcji i płytki, plik JSON z wynikiem, nagłówek sygnatury linkowania oraz
referencyjny plik źródłowy. Dzięki temu projekty wywołujące kompilator
bezpośrednio mogą zbudować i zlinkować ustalony pakiet bez uruchamiania Pythona.

---

## Mapy pamięci

Układ pamięci każdej platformy opisano obok jej konfiguracji kompilacji:

- [Mapa pamięci RP](../../link_libraries/rp_pico_lib/MEMORY_MAP.md) - układy linkera dla
  aplikacji i OTA, obszary pamięci flash z trwałymi danymi, SRAM, sterta
  i stosy.
- [Mapa pamięci STM32G474](../../link_libraries/stm32_lib/MEMORY_MAP.md) - regiony linkera
  bare-metal, zarezerwowane strony flash EEPROM/KV, sekcje RAM, sterta i stos.

---

<a id="sugerowana-kolejność-czytania-dokumentacji"></a>

## Od czego zacząć

- [00_scripts.md](../api/pl/00_scripts.md) wyjaśnia, które skrypty przygotowują środowisko, zarządzają zależnościami, kompilują projekty i uruchamiają kontrole oraz jak współpracują z VS Code.
- [FwProjectWorkflow.md](FwProjectWorkflow.md) prowadzi przez konfigurację manifestu, wybór platformy i źródeł, kompilację oraz wgrywanie firmware.
- [OTAWorkflow.md](OTAWorkflow.md) opisuje przygotowanie urządzeń RP i ESP32-S3 do OTA, konfigurację sieci i zapory, aktualizację, potwierdzenie rozruchu oraz odzyskiwanie.

Szczegółowe polecenia, interfejsy i przykłady konfiguracji znajdują się w rozdziale poświęconym danej czynności. Pozostałe rozdziały podają kontekst i odsyłają do właściwego opisu, zamiast go powielać.

---

## Publiczne API a moduły pomocnicze

Repozytorium zawiera zarówno sam HAL, jak i zestaw modułów narzędziowych.

### Publiczne API HAL

Publiczne interfejsy HAL oddzielają logikę aplikacji od wywołań SDK konkretnej platformy:

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
- zawsze dostępne funkcje `hal_time` do deterministycznej konwersji
  daty/czasu gregoriańskiego na epokę, korekty CET/CEST, sprawdzeń
  przedziałów otwartych z jednej strony oraz ekstrakcji minut; opcjonalne
  API NTP/czasu lokalnego pozostają sterowane flagami
- opcjonalny hook znacznika czasu dla logowania błędów, ustawiany funkcją
  `hal_debug_set_timestamp_hook(...)`

### Moduły pomocnicze / narzędziowe

Moduły narzędziowe uzupełniają HAL, ale same nie zapewniają warstwy abstrakcji sprzętu:

- `tools`
- `SmartTimers`
- `pidController`
- `multicoreWatchdog`
- `draw7Segment`

W nowej aplikacji opieraj obsługę sprzętu na API HAL. Korzystaj z modułów narzędziowych tam, gdzie ułatwiają zadanie, ale nie traktuj ich jako zamiennika warstwy zapewniającej przenośność.

---

## Profile płytek i możliwości dostępne w czasie działania

Makra `HAL_TARGET_*` identyfikują mikrokontroler i architekturę zestawu instrukcji (ISA). `JH_BOARD` wybiera profil fizycznej płytki z `boards/profiles/`; generator tworzy odpowiadający mu selektor `HAL_BOARD_PROFILE_*` i konfigurację platformy. Dostępne profile są zdefiniowane w deskryptorach płytek. Aby wyświetlić ich identyfikatory, uruchom:

```bash
python3 scripts/generate_board_config.py --boards-root boards --list boards
```

Komponent ESP32-S3 udostępnia API `hal_board` na podstawie wygenerowanych danych platformy, płytki i konfiguracji linkowania. Zadeklarowana funkcja sprzętowa pozostaje w stanie `HAL_BOARD_CAP_INACTIVE`, dopóki obsługujący ją moduł nie zgłosi gotowości albo błędu.

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

Nagłówek `hal/system/hal_board.h` definiuje stabilne identyfikatory profili, maskę funkcji sprzętowych, stany ich dostępności, strukturę informacji o płytce i funkcje odczytu. Wygenerowany `src/hal/generated/jh_board_registry.h` przypisuje profilom rejestru te identyfikatory; nie trzeba utrzymywać drugiej, ręcznej listy.

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
| 0 | [Skrypty obsługi repozytorium i orkiestracja](../api/pl/00_scripts.md) | Przygotowanie środowiska, kompilacja i kontrole oraz dane wejściowe i wyniki narzędzi. |
| 1 | [Przewodnik po kompilacji biblioteki](lib_compilation.md) | Kompilacja dla wszystkich targetów, generowane metadane płytki, skrypty bibliotek statycznych, warianty FreeRTOS oraz integracja firmware |
| P | [Praca z projektem firmware](FwProjectWorkflow.md) | Konfiguracja, kompilacja, wgrywanie i debugowanie projektu firmware przez VS Code lub CLI. |
| O | [Natywne aktualizacje OTA](OTAWorkflow.md) | Przygotowanie RP i ESP32-S3 do OTA, aktualizacja, potwierdzanie próbnego rozruchu i odzyskiwanie urządzenia. Obejmuje ograniczenia zabezpieczeń. |
| 2 | [Flagi modułów i konfiguracja](../api/pl/02_module_flags.md) | Jawnie włączane flagi `HAL_ENABLE_*`, propagacja zależności, wybór FreeRTOS, nadpisania rozmiaru stosu oraz moduły rdzeniowe |
| A | [API kodów statusu](../api/pl/01_status_api.md) | Interpretacja wyników operacji i przechodzenie na API zwracające status. |
| 3 | [Zależności kompilacji, testy i stanowiska sprzętowe](../api/pl/03_build_tests.md) | Kompilacja i uruchamianie testów hostowych, zakres CI i wybór procedur testowania urządzeń. |
| 4 | [Bezpieczeństwo wielordzeniowe i sterowniki](../api/pl/04_multicore_drivers_migration.md) | Współdzielenie modułów między rdzeniami i zadaniami, inicjalizacja zasobów i konfiguracja logowania. |
| S | [Bezpieczeństwo zależności i narzędzi](security_supply_chain.md) | Wykaz komponentów firm trzecich, generowanie SBOM CycloneDX, skanowanie podatności oraz proces oceny CVE/CVSS |
| 5 | [GPIO, ADC i PWM](../api/pl/05_gpio_adc_pwm.md) | `hal_gpio`, `hal_pwm`, `hal_dac`, `hal_pcnt`, `hal_pwm_freq`, `hal_dacless`, `hal_adc` |
| 6 | [Timery i system](../api/pl/06_timers_system.md) | Timery, odczyt czasu, tryby zasilania, diagnostyka systemu i przenośne funkcje kompilatora. |
| 7 | [Kryptografia](../api/pl/07_crypto.md) | `hal_crypto` - Base64, MD5, SHA-256, HMAC-SHA256, ChaCha20, ChaCha20-Poly1305 |
| 8 | [Synchronizacja, USB, port szeregowy, ramkowanie i uwierzytelnianie](../api/pl/08_sync_serial.md) | Synchronizacja, USB, diagnostyka szeregowa oraz wymiana poleceń z ramkowaniem i uwierzytelnianiem. |
| 9 | [Magistrale komunikacyjne](../api/pl/09_buses.md) | Transmisja przez SPI, I2C, UART i 1-Wire, z zasadami blokowania magistrali i limitami operacji. |
| 10 | [Magistrala CAN i wyświetlacz](../api/pl/10_can_display.md) | Obsługa Classical CAN i CAN FD oraz wyświetlanie tekstu i obrazów przez API wyświetlaczy. |
| 11 | [Czujniki](../api/pl/11_sensors.md) | Odczyt czujników i GPS, obsługa RTC oraz zdekodowanych sygnałów wejściowych. |
| 12 | [Modem komórkowy](../api/pl/12_modem.md) | `hal_modem_at` (silnik AT, URC, współpraca z watchdogiem), `hal_simcom_a76xx` (A7670/A7672 - zasilanie, rozruch, SIM, PDP, LBS, GNSS, subskrypcja MQTT) |
| 13 | [Urządzenia wyjściowe](../api/pl/13_output_devices.md) | Sterowanie wyjściem audio, diodami i urządzeniami wyjściowymi. |
| 14 | [Pamięć masowa](../api/pl/14_storage.md) | Zapis ustawień i plików, obsługa flash i logów, z zasadami trwałości danych i współbieżnego dostępu. |
| 15 | [Łączność sieciowa](../api/pl/15_connectivity.md) | WiFi, TCP/UDP i usługi sieciowe, w tym TLS, HTTP, MQTT, OTA i NTP. |
| 16 | [Narzędzia](../api/pl/16_utilities.md) | Funkcje pomocnicze modułów, timery programowe, regulacja PID i narzędzia zgodnościowe C++. |
| 17 | [cJSON](../api/pl/17_cJSON.md) | Parsowanie i zapis JSON, zarządzanie obiektami oraz operacje Pointer i Patch. |
| 18 | [LodePNG](../api/pl/18_LodePNG.md) | Dekodowanie PNG i przygotowanie zasobów RGB565 z uwzględnieniem pamięci embedded. |
| 19 | [JPEG](../api/pl/19_JPEG.md) | Dekodowanie JPEG i przygotowanie zasobów RGB565 z uwzględnieniem pamięci embedded. |
| 20 | [Bluetooth](../api/pl/20_bluetooth.md) | BLE, profile Classic, gamepady, audio i uwierzytelnione strumienie. Obejmuje obsługę płytek i zasady współdzielenia radia. |
| 21 | [Niskopoziomowe API radia LoRa](../api/pl/21_lora.md) | Konfiguracja radia LoRa i transmisja pakietów. SX1262 sprawdzono sprzętowo; SX1261/SX1276/SX1278 pozostają eksperymentalne. |
| 22 | [Niezawodne łącze LoRa](../api/pl/22_lora_link.md) | Adresowane wiadomości LoRa z potwierdzeniami, ponawianiem, fragmentacją i opcjonalnym szyfrowaniem. |
| 23 | [Kierowanie poleceń](../api/pl/23_commands.md) | Rejestrowanie i obsługa poleceń, kodowanie wiadomości oraz adaptery szeregowe, sieciowe, LoRa i BLE. |
| 24 | [Sprzętowy pomiar okresów](../api/pl/24_pulse_capture.md) | Sprzętowe znaczniki czasu i okna 32 okresów na RP, STM32G474, ESP32-S3 oraz mocku |
| 25 | [Sprzętowo taktowany skan ADC](../api/pl/25_adc_scan.md) | Ciągły skan DMA zestawu pinów do bloków aplikacji na RP, STM32G474, ESP32-S3 i mocku |

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
| `hal_pulse_capture` | [Sprzętowy pomiar okresów](../api/pl/24_pulse_capture.md) |
| `hal_adc_scan` | [Sprzętowo taktowany skan ADC](../api/pl/25_adc_scan.md) |
