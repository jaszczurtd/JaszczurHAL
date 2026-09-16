# Flagi modułów i konfiguracja

*Dostępne również [po angielsku](../en/02_module_flags.md).*

> **Część [Dokumentacji API JaszczurHAL](../../pl/JaszczurHAL_API.md)**

## Selektywne włączanie modułów (`HAL_ENABLE_*`)

Moduły opcjonalne są domyślnie wyłączone. Aby użyć modułu, zdefiniuj `HAL_ENABLE_<MODULE>` w `hal_project_config.h` albo przekaż flagę przez `-D`. Włączenie modułu udostępnia:

* deklaracje API w odpowiednim nagłówku; przy wyłączonym module jego jednostka translacji pozostaje pusta, a próba użycia niedostępnej funkcji powoduje błąd kompilacji;
* implementację `.cpp` i potrzebne sterowniki zewnętrzne, dołączane warunkowo;
* odpowiedni wpis w nagłówku zbiorczym `hal/hal.h`.

Wyłączony moduł nie zajmuje pamięci kodu ani RAM i nie dołącza swoich zewnętrznych zależności do firmware.

Flagi modułów działają według obecności symbolu. Obsługiwane definicje projektu
to `#define HAL_ENABLE_X` oraz
`#define HAL_ENABLE_X 1`. Nie używaj `#define HAL_ENABLE_X 0`: produkcyjny
preprocesor nadal sprawdza `#ifdef HAL_ENABLE_X`, więc symbol pozostaje
włączony. Generatory płytek, funkcje CMake, skrypty bibliotek statycznych i
`jh-vscode` odrzucają `=0` oraz inne jawne wartości, zgłaszając
`[JH-CFG-VALUE]`. Bezpośrednie wywołanie kompilatora nadal stosuje zwykłe
reguły preprocesora. Linter rejestru zgłasza także symbole nieznane lub
wyprowadzane automatycznie. Gdy definicje są przekazywane jako lista, każdy
wpis `HAL_ENABLE_*` musi być osobnym, prostym tokenem, a wpisy należy
rozdzielać średnikami. Same białe znaki nie rozdzielają definicji. Wyrażenia
generatora CMake są odrzucane.

Rejestr w `config/features/` definiuje zależności modułów. Na jego podstawie powstaje nagłówek C dołączany przez `hal_config.h`. CMake, generator danych płytki i linkowania oraz `jh-vscode` korzystają z tych samych reguł i zapisują zestawy `requestedFeatures` oraz `resolvedFeatures`.

### Dostępne flagi

Ta sekcja zawiera utrzymywany publicznie wykaz flag `HAL_ENABLE_*`, które może
wybrać użytkownik. Test zgodności z rejestrem zapobiega rozbieżnościom względem
`config/features/`; wewnętrzne symbole wyprowadzane automatycznie są celowo
pominięte. `hal_config.h` pozostaje publicznym punktem konfiguracji i zawiera
reguły zależne od kontekstu, których nie opisuje rejestr v1. Krótsze
podsumowanie znajduje się w `doc/HAL_FLAGS.txt`.

W tabeli nagłówek tematyczny `hal_*` oznacza API C zalecane dla nowego kodu.
Jeśli dotychczasowa klasa C++ pozostaje dostępna, jej nagłówek jest wymieniony
osobno po nagłówku C. Włączenie modułu zachowuje oba interfejsy;
ten podział jest zaleceniem dla nowego kodu, a nie usunięciem istniejącej
funkcjonalności.

Moduł przechowujący stan może udostępniać uchwyt do struktury z ukrytymi
polami (ang. opaque handle). W opisach tabeli jest on dalej nazywany krócej
uchwytem do instancji.

Flagi wejścia aplikacji są oddzielone od opcjonalnych modułów HAL:

| Flaga | Efekt |
|---|---|
| `HAL_ENABLE_APP_TASK1` | Uruchamia opcjonalną funkcję `app_task1()` przez punkt wejścia dostarczany przez HAL. RP bare metal uruchamia rdzeń 1, a RP z FreeRTOS tworzy zadanie przypięte do rdzenia 1. FreeRTOS na STM32G474 tworzy drugie zadanie aplikacji. ESP32-S3 domyślnie uruchamia je na rdzeniu 1; `HAL_FREERTOS_TASK1_CORE` może wskazać prawidłowy rdzeń albo `-1`, czyli brak przypisania. W aplikacji z jedną pętlą lub jednym zadaniem pozostaw flagę niezdefiniowaną. |

Integracja FreeRTOS jest również jawnym opt-in, ale nie jest modułem HAL:

| Flaga | Efekt |
|---|---|
| `HAL_ENABLE_FREERTOS` | Włącza FreeRTOS dla wybranego targetu. Kompilacje RP korzystają z kernela i portów SMP w wersjach wskazanych przez repozytorium dla RP2040, RP2350 ARM oraz RP2350 RISC-V; HAL uruchamia kernel i przypisuje zadania aplikacji do odpowiednich rdzeni. STM32G474 używa wskazanej w repozytorium wersji portu Cortex-M4F. Funkcje `hal_mutex_*`, `hal_delay_ms()`, `hal_idle()` oraz diagnostyka runtime dostosowują działanie do FreeRTOS. Flaga nie dodaje publicznego API `hal_rtos_*` i sama nie zapewnia bezpieczeństwa współbieżnych wywołań we wszystkich modułach HAL. |

Ochronę stosu włączają dwie niezależne opcje:

| Flaga | Efekt |
|---|---|
| `HAL_ENABLE_STACK_GUARD` | Włącza synchroniczną ochronę sprzętową natywnych stosów systemowych RP2040/RP2350 i głównego stosu STM32G474, a na ESP32-S3 punkty kontrolne końca stosów zadań udostępniane przez ESP-IDF. Kompilacje FreeRTOS dodatkowo sprawdzają przepełnienie stosów zadań kernela. Niezależne od targetu API `hal_stack_guard_init_ex()` zwraca informację, czy ochrona jest aktywna; nie trzeba go okresowo odpytywać. |
| `HAL_ENABLE_STACK_PROTECTOR` | Włącza opcję GCC/Clang `-fstack-protector-strong` dla źródeł HAL i aplikacji w obsługiwanych konfiguracjach firmware RP oraz STM32G474. Wykrycie nieprawidłowego kanarka uruchamia mechanizm resetu po przepełnieniu stosu właściwy dla danego targetu, a informacja o zdarzeniu jest zachowywana do następnego startu. Flaga działa niezależnie od `HAL_ENABLE_STACK_GUARD`. |

| Flaga | Nagłówek | Implementacja | Dołączane zależności firm trzecich |
|---|---|---|---|
| `HAL_ENABLE_COMMAND_ROUTER` | `hal_command_router.h`, `hal_command_wire.h` | `hal/commands/hal_command_router.cpp` + `hal/commands/hal_command_wire.cpp` | Niezależny od transportu rejestr funkcji obsługi, polityka źródła i bezpieczeństwa, odpowiedzi o ograniczonym rozmiarze oraz wersjonowane binarne komunikaty żądania, odpowiedzi i zdarzenia; zobacz [API komend](23_commands.md) |
| `HAL_ENABLE_SERIAL_COMMANDS` | `hal_serial_commands.h` | `hal/serial/hal_serial_commands.cpp` | Synchroniczne przekazywanie komend TEXT/JSON do routera w aktywnych sesjach szeregowych z ramkowaniem, opcjonalnym formatowaniem odpowiedzi i prefiksem zapasowym (propaguje COMMAND_ROUTER); zobacz [API komend](23_commands.md#adapter-ramkowanej-sesji-szeregowej-framed-serial-session) |
| `HAL_ENABLE_BLE` | `hal_ble.h` | `hal_ble.cpp` + `hal/bluetooth/*` | BLE Peripheral oraz pasywny Observer przez BTstack w wersji wskazanej przez repozytorium i kontroler CYW43; obsługiwane na RP2040 Pico W/Pico+RM2, RP2350 ARM Pico 2 W, STM32G474+PIM730/RM2 oraz mock. RP2350 RISC-V nie jest obsługiwany. Odpowiedni standardowy grant licencyjny BTstack lub grant ograniczony do produktów Raspberry Pi jest opisany w [API Bluetooth](20_bluetooth.md#license-and-distribution-boundary). |
| `HAL_ENABLE_BLUETOOTH_CLASSIC` | `hal_bluetooth_classic.h` | wspólny manager, kodek/provider bondingu + backend targetu | Nieblokujące inquiry Classic, kopiowane wyniki, SDP, ograniczone czasowo parowanie przychodzące i indeksowany zapis peerów (propaguje CRC); zobacz [API Bluetooth](20_bluetooth.md#manager-bluetooth-classic-i-profile) |
| `HAL_ENABLE_BLUETOOTH_A2DP_SINK` | `hal_bluetooth_a2dp_sink.h` | ograniczony runtime SBC/PCM + A2DP Sink BTstack i dekoder Bluedroid | Jeden przychodzący strumień SBC z wyjściem PCM signed, prebufferem, diagnostyką kolejek i wspólnym bondingiem Classic (propaguje BLUETOOTH_CLASSIC) |
| `HAL_ENABLE_BLUETOOTH_AVRCP_TARGET` | `hal_bluetooth_avrcp_target.h` | minimalny AVRCP Target BTstack | Odbiór i raportowanie głośności bezwzględnej ze wspólnym połączeniem oraz bondem A2DP (propaguje BLUETOOTH_A2DP_SINK) |
| `HAL_ENABLE_BLUETOOTH_HID_HOST` | `hal_bluetooth_hid_host.h` | ogólny profil HID + obsługa targetu | Jedno surowe połączenie Classic HID Host ze skopiowanym deskryptorem i ograniczonymi raportami Input/Output/Feature (propaguje BLUETOOTH_CLASSIC) |
| `HAL_ENABLE_BLUETOOTH_GAMEPAD` | `hal_gamepad.h` | adapter i parser gamepada HID | Jeden nieblokujący gamepad Classic HID z jawnym parowaniem i reconnectem, znormalizowanymi kopiami stanu i ograniczoną diagnostyką (propaguje BLUETOOTH_HID_HOST); zobacz [API Bluetooth](20_bluetooth.md#adapter-gamepada) |
| `HAL_ENABLE_BLE_COMMANDS` | `hal_ble_commands.h` | `hal/bluetooth/hal_ble_commands.cpp` | Dwukierunkowe żądania, automatyczne odpowiedzi i zdarzenia przez jedną uwierzytelnioną sesję BLE Stream pozostającą pod wyłączną kontrolą adaptera (propaguje BLE_STREAM + COMMAND_ROUTER); zobacz [API komend](23_commands.md#authenticated-ble-stream-adapter) |
| `HAL_ENABLE_BLE_STREAM` | `hal_ble_stream.h` | `hal_ble_stream.cpp` + `hal/bluetooth/*` | Uwierzytelniony, ograniczony, ramkowany strumień bajtów przez BLE (propaguje BLE + CRYPTO) |
| `HAL_ENABLE_LORA` | `hal_lora_radio.h` | `hal_lora_radio.cpp` | Niezależna od providera obsługa bezpośredniego dostępu do radia LoRa: inicjalizacja i zamykanie, gotowe konfiguracje modemu, blokujący TX, RX przez polling, diagnostyka, stan zasilania i czas transmisji; wymaga dokładnie jednego providera |
| `HAL_ENABLE_LORA_LINK` | `hal_lora_link.h` | `hal_lora_link.cpp` + `jh_lora_link_frame.cpp` | Niezawodne prywatne wiadomości z adresowaniem, sekwencjami, ACK/retry, tłumieniem duplikatów i fragmentacją (propaguje LORA + CRC); opcjonalne AEAD wymaga CRYPTO; zobacz [API łącza LoRa](22_lora_link.md) |
| `HAL_ENABLE_LORA_COMMANDS` | `hal_lora_commands.h` | `hal/radio/hal_lora_commands.cpp` | Żądania, automatyczne odpowiedzi i zdarzenia przez jedno niezawodne łącze LoRa pozostające pod wyłączną kontrolą adaptera (propaguje COMMAND_ROUTER + LORA_LINK); zobacz [API komend](23_commands.md) |
| `HAL_ENABLE_SX126X` | `hal_lora_radio.h` | `hal_lora_radio.cpp` + `hal/radio/sx126x/*` + sterownik Semtech w wersji wskazanej przez repozytorium | SX1262 oraz eksperymentalny, wyłącznie programowy provider SX1261 przez HAL SPI/GPIO (propaguje LORA + SPI); zobacz [API radia LoRa](21_lora.md) |
| `HAL_ENABLE_SX127X` | `hal_lora_radio.h` | `hal_lora_radio.cpp` + `hal/radio/sx127x/*` | Eksperymentalny, wyłącznie programowy provider SX1276/SX1278 przez HAL SPI/GPIO (propaguje LORA + SPI i jest w konflikcie z SX126X); zobacz [API radia LoRa](21_lora.md) |
| `HAL_ENABLE_WIFI` | `hal_wifi.h` | `hal_wifi.cpp` | Backend CYW43/lwIP lub natywny ESP-IDF WiFi/`esp_netif`/lwIP wybierany przez konfigurację targetu/płytki |
| `HAL_ENABLE_TIME` | opcjonalne deklaracje w `hal_time.h` | wspólny zegar runtime + adapter libc targetu | Funkcje ustawiające czas systemowy i odczytujące jego stan oraz WiFi NTP (propaguje UDP + WIFI); proste funkcje kalendarza i przedziałów czasu są zawsze dostępne |
| `HAL_ENABLE_MQTT` | `hal_mqtt.h` | `hal_mqtt.cpp` | PubSubClient przez HAL TCP lub BearSSL TLS (propaguje TCP + WIFI) |
| `HAL_ENABLE_UDP` | `hal_udp.h` | `hal_udp.cpp` | Transport UDP oparty na uchwytach (propaguje WIFI) |
| `HAL_ENABLE_TCP` | `hal_tcp.h` | `hal_tcp.cpp` | Transport klienta i serwera nasłuchującego TCP (propaguje WIFI) |
| `HAL_ENABLE_HTTP_SERVER` | `hal_http_server.h` | `hal/network/http/hal_http_server.cpp` | Mały, sterowany odpytywaniem serwer HTTP/1.1 w formie jawnego tekstu przez HAL TCP (propaguje TCP + WIFI); brak API serwera HTTPS |
| `HAL_ENABLE_HTTP_FILES` | `hal_http_files.h` | `hal/network/http/hal_http_files.cpp` | Serwowanie plików oparte na callbackach i ETag oraz funkcje pomocnicze do wysyłania plików przez trasy HAL HTTP (propaguje HTTP_SERVER + TCP + WIFI) |
| `HAL_ENABLE_WEBSOCKET` | `hal_websocket.h` | `hal/network/websocket/hal_websocket.cpp` | Mały, sterowany odpytywaniem serwer WebSocket w formie jawnego tekstu przez HAL TCP (propaguje TCP + WIFI); brak WSS ani API klienta WebSocket |
| `HAL_ENABLE_NET_CONSOLE` | `hal_net_console.h` | `hal/network/net_console/hal_net_console.cpp` | Chronione hasłem przekazywanie wyjścia serial/debug oraz strumień komend przez HAL TCP (propaguje TCP + WIFI) |
| `HAL_ENABLE_NET_COMMANDS` | `hal_net_commands.h` | `hal/network/net_commands/hal_net_commands.cpp` | Adapter text/JSON HTTP i WebSocket oparty na cJSON, działający na współdzielonym domyślnym routerze (propaguje COMMAND_ROUTER + HTTP_SERVER + WEBSOCKET + CJSON + TCP + WIFI) |
| `HAL_ENABLE_NOTIFY` | `hal_notify.h` | `hal/network/notify/hal_notify.cpp` | Fasada przekazująca powiadomienia do backendu; uchwyty kanałów są sprawdzane przy użyciu numeru generacji |
| `HAL_ENABLE_NOTIFY_TELEGRAM` | `hal_notify.h` | `hal/network/notify/hal_notify_telegram.cpp` | Backend Telegram Bot API przez `hal_http_client`; publiczne dostarczanie Telegram używa HTTPS, natomiast niestandardowe hosty HTTP mogą być użyte do wdrożeń lokalnych/proxy (propaguje NOTIFY + HTTP_CLIENT + TLS + CJSON + TCP + WIFI) |
| `HAL_ENABLE_BSD_SOCKETS` | `sys/socket.h`, `netinet/in.h`, `arpa/inet.h`, `netdb.h`, `fcntl.h`, `sys/select.h`, `unistd.h` | `hal/network/adapters/bsd/hal_bsd_sockets.cpp` | Publiczny adapter BSD/POSIX przez HAL UDP/TCP, w tym `getaddrinfo()` (propaguje UDP + TCP + WIFI); pozostaje użyteczny z TLS i bez niego |
| `HAL_ENABLE_TLS` | `hal_tls.h` | `hal/network/tls/hal_tls.cpp` + `hal/network/tls/BearSSL/*` | Klient BearSSL TLS przez natywny HAL TCP (propaguje TCP + WIFI); nie wymusza gniazd BSD, a opcjonalny transport BearSSL BSD utrzymuje dostępność TLS-over-BSD |
| `HAL_ENABLE_HTTP_CLIENT` | `hal_http_client.h` | `hal/network/http/hal_http_client.cpp` | Ograniczony jednorazowy klient HTTP/1.1 przez HAL TCP z transportem HTTPS, gdy wybrany jest TLS (propaguje TCP + WIFI) |
| `HAL_ENABLE_OTA` | `hal_ota.h` | `hal_ota.cpp` specyficzny dla targetu + współdzielony protokół OTA | Natywna usługa aktualizacji UDP/TCP z wykrywaniem, opcjonalnym wyzwaniem hasłem, rozruchem próbnym, potwierdzeniem i wycofaniem (rollback). RP używa podpisanego silnika kontener/zamiana (swap) JaszczurHAL; ESP32-S3 przygotowuje surowy obraz aplikacji ESP poprzez partycje OTA ESP-IDF (propaguje WIFI + UDP + TCP + CRYPTO + CRC). |
| `HAL_ENABLE_WIREGUARD` | `hal_wireguard.h` | `hal/network/wireguard/hal_wireguard.cpp` + port rozszerzenia lwIP specyficzny dla targetu | Dołączony WireGuard działający przez stos lwIP targetu, który udostępnia wymagane funkcje; obsługiwany również na ESP32-S3 (propaguje UDP + WIFI) |
| `HAL_ENABLE_EEPROM` | `hal_eeprom.h` | `hal_eeprom.cpp` | Emulacja EEPROM we flashu targetu; AT24C256 przez HAL I2C, gdy wybrane |
| `HAL_ENABLE_KV` | `hal_kv.h` | `hal_kv.cpp` | *(propaguje EEPROM)* |
| `HAL_ENABLE_LITTLEFS` | `hal_littlefs.h` | `hal/storage/hal_littlefs.cpp` + wspólny provider littlefs + provider targetu/mocka | Jedna fasada cyklu życia/blokowania/ścieżek/statystyk; natywny RP używa `HAL_RP_FLASH_LITTLEFS_SIZE`, STM32G474 używa `HAL_STM32_FLASH_LITTLEFS_SIZE` |
| `HAL_ENABLE_FAT` | FatFs `ff.h` | zarządzane źródła FatFs oraz we/wy dysku targetu | Współdzielone wsparcie systemu plików FatFs, wykorzystywane przez moduły oparte na SD |
| `HAL_ENABLE_SDLOGGER` | `hal_sdlogger.h` | `hal/storage/filesystem/sdlogger/hal_sdlogger.cpp` | Logger SD przez współdzielony FatFs (propaguje FAT + EEPROM + SPI) |
| `HAL_ENABLE_UART` | `hal_uart.h` | `hal_uart.cpp` | Sprzętowy UART |
| `HAL_ENABLE_SWSERIAL` | `hal_swserial.h` | `hal_swserial.cpp` specyficzny dla targetu | Natywny programowy UART PIO/DMA Pico SDK na RP2040; współdzielony backend HAL GPIO na pozostałych targetach |
| `HAL_ENABLE_I2C` | `hal_i2c.h` | `hal_i2c.cpp` | Magistrala I2C master/kontroler |
| `HAL_ENABLE_I2C_10BIT` | `hal_i2c.h` | `hal_i2c.cpp` + backendy targetów | Opcjonalne 10-bitowe adresowanie I2C master przez `hal_i2c_init_10bit()`/`hal_i2c_init_bus_10bit()` i `hal_i2c_address_t` (propaguje I2C); `hal_i2c_scan()` pozostaje wyłącznie 7-bitowy |
| `HAL_ENABLE_I2C_SLAVE` | `hal_i2c_slave.h` | `hal_i2c_slave.cpp` | Tryb I2C slave/target z mapą rejestrów |
| `HAL_ENABLE_I2C_SLAVE_SNAPSHOT` | `hal_i2c_slave.h` | `hal_i2c_slave.cpp` | Niezmienna odpowiedź rejestrów podczas odczytu; włącza `HAL_ENABLE_I2C_SLAVE`. ESP32 odświeża obraz po ponownym wyborze rejestru. |
| `HAL_ENABLE_SPI` | `hal_spi.h` | `hal_spi.cpp` | SPI master/kontroler |
| `HAL_ENABLE_CAN` | `hal_can.h` | `hal_can.cpp` + `hal_can_util.cpp` | Generyczna fasada API CAN; wymaga co najmniej jednego backendu |
| `HAL_ENABLE_MCP2515` | `hal_can.h` + `hal/can/mcp2515/mcp2515_driver.h` | fasada `hal_can.cpp` specyficzna dla targetu + `hal/can/mcp2515/hal_can_mcp2515.cpp` + `hal/can/mcp2515/hal_can_mcp2515_config.cpp` + `hal/can/mcp2515/mcp2515_driver.cpp` | Współdzielony backend CAN MCP2515 wyłącznie HAL (propaguje CAN + SPI) |
| `HAL_ENABLE_MCP251XFD` | `hal_can.h` + `hal/can/mcp251xfd/mcp251xfd_driver.h` | fasada `hal_can.cpp` specyficzna dla targetu + `hal/can/mcp251xfd/hal_can_mcp251xfd.cpp` + `hal/can/mcp251xfd/hal_can_mcp251xfd_config.cpp` + `hal/can/mcp251xfd/mcp251xfd_driver.cpp` | Współdzielony backend CAN FD MCP2517FD/MCP2518FD (propaguje CAN + SPI) |
| `HAL_ENABLE_STM32G474_FDCAN` | `hal_can.h` | `impl/stm32g474/hal_can.cpp` + `impl/stm32g474/hal_can_stm32g474_fdcan.cpp` + `impl/stm32g474/hal_can_stm32g474_fdcan_config.cpp` | Natywny backend CAN FD FDCAN1 STM32G474 (propaguje CAN; odrzucany podczas kompilacji poza STM32G474) |
| `HAL_ENABLE_RTC` | `hal_rtc.h` | `hal_rtc.cpp` | *(wymaga PCF8563, DS3231 lub backendu wewnętrznego)* |
| `HAL_ENABLE_PCF8563` | `hal_rtc.h` | `hal_rtc.cpp` | Backend PCF8563 (propaguje RTC + I2C) |
| `HAL_ENABLE_DS3231` | `hal_rtc.h` | `hal_rtc.cpp` | Backend DS3231 (propaguje RTC + I2C) |
| `HAL_ENABLE_INTERNAL_RTC` | `hal_rtc.h` | provider RTC specyficzny dla targetu | Natywny dla targetu backend RTC dla STM32G474 oraz RP2040/RP2350 (propaguje RTC; bez I2C) |
| `HAL_ENABLE_POWER_MANAGEMENT` | `hal_power.h` | `hal_power.cpp` specyficzny dla targetu | API stanów sleep/deep-sleep/power-down dostępnych zależnie od funkcji obsługiwanych przez target (propaguje INTERNAL_RTC + RTC); zobacz [Timery i system](06_timers_system.md#halpower-low-power-transitions-optional-halenablepowermanagement) |
| `HAL_ENABLE_THERMOCOUPLE` | `hal_thermocouple.h` | `hal_thermocouple.cpp` | *(wymaga backendu MCP9600 lub MAX6675)* |
| `HAL_ENABLE_MCP9600` | `hal_thermocouple.h` + `hal/temperature/mcp9600/mcp9600_driver.h` | `hal_thermocouple.cpp` + `hal/temperature/mcp9600/mcp9600_driver.cpp` | współdzielony sterownik MCP9600/MCP9601 wyłącznie HAL (propaguje THERMOCOUPLE + I2C) |
| `HAL_ENABLE_MAX6675` | `hal_thermocouple.h` + `hal/temperature/max6675/max6675_driver.h` | `hal_thermocouple.cpp` + `hal/temperature/max6675/max6675_driver.cpp` | współdzielony sterownik bit-bang MAX6675 wyłącznie HAL (propaguje THERMOCOUPLE) |
| `HAL_ENABLE_DS18B20` | `hal_ds18b20.h` + `hal/onewire/onewire_driver.h` | `hal/temperature/ds18b20/hal_ds18b20.cpp` + `hal/onewire/onewire_driver.cpp` | współdzielony backend DS18B20 wyłącznie HAL przez 1-Wire (propaguje ONEWIRE) |
| `HAL_ENABLE_DHT` | `hal_dht.h` | `hal/temperature/dht/hal_dht.cpp` | współdzielony sterownik temperatury/wilgotności DHT11/DHT22 przez HAL GPIO |
| `HAL_ENABLE_BH1750` | `hal_bh1750.h` | `hal/sensors/bh1750/hal_bh1750.cpp` | współdzielony sterownik czujnika natężenia światła otoczenia BH1750 przez HAL I2C (propaguje I2C) |
| `HAL_ENABLE_ADP5360` | `hal_adp5360.h` | `hal/power/adp5360/hal_adp5360.cpp` | współdzielony sterownik PMIC ADP5360 przez HAL I2C: init/reset/shipment MFD, ładowarka, fuel-gauge oraz sterowanie regulatorem buck/buck-boost (propaguje I2C) |
| `HAL_ENABLE_MCP3221` | `hal_mcp3221.h` | `hal/gpio/simple_io/hal_simple_io_drivers.cpp` | 12-bitowy ADC MCP3221 przez HAL I2C (propaguje I2C) |
| `HAL_ENABLE_TSC2007` | `hal_tsc2007.h` | `hal/input/tsc2007/tsc2007.cpp` | współdzielony sterownik kontrolera dotyku rezystancyjnego TSC2007 przez HAL I2C (propaguje I2C) |
| `HAL_ENABLE_STMPE610` | `hal_stmpe610.h` | `hal/input/stmpe610/stmpe610.cpp` | współdzielony sterownik kontrolera dotyku rezystancyjnego STMPE610 przez HAL I2C/SPI (propaguje I2C + SPI) |
| `HAL_ENABLE_IRSMALL_DECODER` | `hal_irsmall_decoder.h` | `hal/input/irsmall_decoder/irsmall_decoder.cpp` | współdzielony dekoder odbiornika podczerwieni oparty na przerwaniu HAL GPIO |
| `HAL_ENABLE_ONEWIRE` | `hal_onewire.h` + `hal/onewire/onewire_driver.h` | `hal/onewire/hal_onewire.cpp` + `hal/onewire/onewire_driver.cpp` | współdzielony sterownik bit-bang 1-Wire wyłącznie HAL (propaguje CRC) |
| `HAL_ENABLE_EXTERNAL_ADC` | `hal_external_adc.h` + `hal/analog/ads1x15/ads1x15_driver.h` | `hal/analog/ads1x15/hal_external_adc_ads1x15.cpp` + `hal/analog/ads1x15/ads1x15_driver.cpp` | współdzielony sterownik ADS1X15/ADS1115 wyłącznie HAL (propaguje I2C) |
| `HAL_ENABLE_GPS` | `hal_gps.h` | `hal_gps.cpp` + `hal/gps/` | przenośna fasada oraz silnik NMEA (RP2040 + STM32G474); wymaga transportu: SWSERIAL lub UART |
| `HAL_ENABLE_DIGIPOT` | `hal_digipot.h` + `hal/analog/digipot/hal_digipot_ops.h` | `hal_digipot.cpp` + `hal/analog/digipot/*.cpp` | fasada, pula i mechanizm wyboru backendu; wymaga backendu MCP401X lub MAX5395 |
| `HAL_ENABLE_MCP401X` | `hal_digipot.h` + `hal/analog/digipot/hal_digipot_ops.h` | `hal_digipot.cpp` + `hal/analog/digipot/digipot_mcp401x.cpp` | współdzielony sterownik HAL I2C MCP4017/4018/4019 (propaguje DIGIPOT + I2C) |
| `HAL_ENABLE_MAX5395` | `hal_digipot.h` + `hal/analog/digipot/hal_digipot_ops.h` | `hal_digipot.cpp` + `hal/analog/digipot/digipot_max5395.cpp` | współdzielony sterownik HAL I2C MAX5395 (propaguje DIGIPOT + I2C) |
| `HAL_ENABLE_PGA2311` | `hal_pga2311.h` + `hal/audio/pga2311/pga2311_driver.h` | `hal_pga2311.cpp` + `hal/audio/pga2311/pga2311_driver.cpp` | współdzielony sterownik regulacji głośności stereo PGA2311 przez HAL SPI/GPIO (propaguje SPI) |
| `HAL_ENABLE_MCP23017` | `hal_mcp23017.h` | `hal/gpio/simple_io/hal_simple_io_drivers.cpp` | Ekspander GPIO MCP23017 przez HAL I2C (propaguje I2C) |
| `HAL_ENABLE_PCA9654E` | `hal_pca9654e.h` | `hal/gpio/simple_io/hal_simple_io_drivers.cpp` | Ekspander wyjść PCA9654E przez HAL I2C (propaguje I2C) |
| `HAL_ENABLE_PCF8574` | `hal_pcf8574.h` | `hal/gpio/simple_io/hal_simple_io_drivers.cpp` | Quasi-dwukierunkowy ekspander GPIO PCF8574 przez HAL I2C (propaguje I2C) |
| `HAL_ENABLE_HC595` | `hal_hc595.h` | `hal/gpio/simple_io/hal_simple_io_drivers.cpp` | Ekspander wyjść z rejestrem przesuwnym 74HC595 przez HAL SPI/GPIO (propaguje SPI) |
| `HAL_ENABLE_MCP4725` | `hal_mcp4725.h` | `hal/gpio/simple_io/hal_simple_io_drivers.cpp` | 12-bitowy DAC MCP4725 przez HAL I2C (propaguje I2C) |
| `HAL_ENABLE_MFRC522` | `hal_mfrc522.h` (C); `hal/nfc/mfrc522/mfrc522.h` (C++) | `hal/nfc/hal_mfrc522.cpp` + `hal/nfc/mfrc522/mfrc522*.cpp` | Uchwyty do instancji transportu i czytnika, typy statusu/UID oraz operacje MFRC522 przez HAL SPI/I2C (propaguje SPI) |
| `HAL_ENABLE_PN532` | `hal_pn532.h` (C); `hal/nfc/pn532/pn532.h` (C++) | `hal/nfc/hal_pn532.cpp` + `hal/nfc/pn532/pn532*.cpp` | Uchwyty do instancji transportu i czytnika, typ UID oraz operacje PN532 przez HAL SPI/I2C/UART (propaguje SPI) |
| `HAL_ENABLE_DACLESS` | `hal_dacless.h` (C); `hal/audio/dacless/dacless.h` (C++) | `hal/audio/hal_dacless.cpp` + `hal/audio/dacless/dacless.cpp` | Uchwyt do instancji DACless, funkcje zwrotne C z kontekstem aplikacji, odczyt stanu oraz funkcje tworzenia i zwalniania instancji (propaguje DMA_PWM_AUDIO + PWM_FREQ) |
| `HAL_ENABLE_DMA_PWM_AUDIO` | `hal_dma_pwm_audio.h` | `hal_dma_pwm_audio.cpp` | Funkcja pomocnicza DMA audio PWM taktowana timerem, wykorzystywana przez DACless |
| `HAL_ENABLE_PWM_FREQ` | `hal_pwm_freq.h` | `hal_pwm_freq.cpp` | RP2040 hardware/pwm, STM32G474 TIM PWM lub ESP32-S3 LEDC |
| `HAL_ENABLE_DAC` | `hal_dac.h` | `hal_dac.cpp` specyficzny dla targetu | Fasada sprzętowego DAC; STM32G474 udostępnia rzeczywiste wyjście, natomiast RP2040 zgłasza brak tej możliwości |
| `HAL_ENABLE_PCNT` | `hal_pcnt.h` | `hal_pcnt.cpp` specyficzny dla targetu | Fasada licznika impulsów dla targetów RP2040, STM32G474, ESP32-S3 PCNT oraz mock |
| `HAL_ENABLE_PULSE_CAPTURE` | `hal_pulse_capture.h` | `hal_pulse_capture.cpp` | [Sprzętowy pomiar okresów](24_pulse_capture.md) |
| `HAL_ENABLE_ADC_SCAN` | `hal_adc_scan.h` | `hal_adc_scan.cpp` + docelowy `hal_adc_scan.cpp` | [Sprzętowo taktowany skan ADC](25_adc_scan.md) |
| `HAL_ENABLE_RGB_LED` | `hal_rgb_led.h` + `hal/gpio/neopixel/jh_neopixel.h` | `hal_rgb_led.cpp` + `hal/gpio/neopixel/jh_neopixel.cpp` | Współdzielony rdzeń NeoPixel + transport targetu (RP2040 PIO / STM32 GPIO taktowane cyklami / ESP32-S3 RMT) |
| `HAL_ENABLE_HD44780` | `hal_hd44780.h` (C); `hal/display/hd44780/hd44780.h` (C++) | `hal/display/hal_hd44780.cpp` + `hal/display/hd44780/hd44780.cpp` | Uchwyt do instancji HD44780, konfiguracja pinów, tekst, kursor i sterowanie wyświetlaczem przez HAL GPIO/taktowanie systemowe |
| `HAL_ENABLE_DISPLAY` | `hal_display.h`; `utils/draw7Segment.h` (C i C++) | `hal/display/drivers/hal_display.cpp` + `utils/draw7Segment.cpp` | API wyświetlaczy graficznych i prosta funkcja rysowania siedmiosegmentowego; wymaga backendu TFT, OLED, LCD lub EPD |
| `HAL_ENABLE_TFT` | `hal_display.h` | `hal/display/drivers/hal_display.cpp` | *(wymaga co najmniej jednego sterownika TFT poniżej; propaguje DISPLAY + SPI)* |
| `HAL_ENABLE_ILI9341` | `hal_display.h` + `hal/display/drivers/ili9341_driver.h` | `hal/display/drivers/hal_display.cpp` + `hal/display/drivers/ili9341_driver.cpp` | współdzielony rdzeń ILI9341 HAL SPI/GPIO + silnik GFX (propaguje TFT + DISPLAY + SPI) |
| `HAL_ENABLE_ST7789` | `hal_display.h` + `hal/display/drivers/st77xx_driver.h` | `hal/display/drivers/hal_display.cpp` + `hal/display/drivers/st77xx_driver.cpp` | współdzielony rdzeń ST77xx HAL SPI/GPIO + silnik GFX (propaguje TFT + DISPLAY + SPI) |
| `HAL_ENABLE_ST7735` | `hal_display.h` + `hal/display/drivers/st77xx_driver.h` | `hal/display/drivers/hal_display.cpp` + `hal/display/drivers/st77xx_driver.cpp` | współdzielony rdzeń ST77xx HAL SPI/GPIO + silnik GFX (propaguje TFT + DISPLAY + SPI) |
| `HAL_ENABLE_ST7796S` | `hal_display.h` + `hal/display/drivers/st77xx_driver.h` | `hal/display/drivers/hal_display.cpp` + `hal/display/drivers/st77xx_driver.cpp` | współdzielony rdzeń ST77xx HAL SPI/GPIO + silnik GFX (propaguje TFT + DISPLAY + SPI) |
| `HAL_ENABLE_GC9A01` | `hal_display.h` + `hal/display/drivers/st77xx_driver.h` | `hal/display/drivers/hal_display.cpp` + `hal/display/drivers/st77xx_driver.cpp` | współdzielony okrągły rdzeń TFT GC9A01 HAL SPI/GPIO + silnik GFX (propaguje TFT + DISPLAY + SPI) |
| `HAL_ENABLE_SSD1306` | `hal_display.h` + `hal/display/drivers/ssd1306_driver.h` | `hal/display/drivers/hal_display.cpp` + `hal/display/drivers/ssd1306_driver.cpp` | współdzielony rdzeń OLED z rodziny SSD1306 (`SSD1306`/`SSD1309`/`SSD1315`/`SH1106`/`CH1115`) + silnik GFX; I2C jest włączane automatycznie, transport SPI jest dostępny, gdy dodatkowo włączone jest `HAL_ENABLE_SPI` (propaguje DISPLAY + I2C) |
| `HAL_ENABLE_SSD1331` | `hal_display.h` + `hal/display/drivers/rgb_oled_driver.h` | `hal/display/drivers/hal_display.cpp` + `hal/display/drivers/rgb_oled_driver.cpp` | Fasada/backend OLED RGB565 SSD1331 przez HAL SPI/GPIO (propaguje DISPLAY + SPI) |
| `HAL_ENABLE_SSD135X` | `hal_display.h` + `hal/display/drivers/rgb_oled_driver.h` | `hal/display/drivers/hal_display.cpp` + `hal/display/drivers/rgb_oled_driver.cpp` | Fasada/backend OLED RGB565 SSD1351/SSD1357 przez HAL SPI/GPIO (propaguje DISPLAY + SPI) |
| `HAL_ENABLE_ST7567` | `hal_display.h` + `hal/display/drivers/st7567_driver.h` | `hal/display/drivers/hal_display.cpp` + `hal/display/drivers/st7567_driver.cpp` | Surowa monochromatyczna fasada/backend ST7567 przez HAL I2C lub SPI/GPIO (propaguje DISPLAY + I2C; transport SPI wymaga dodatkowo SPI) |
| `HAL_ENABLE_SSD16XX` | `hal_display.h` + `hal/display/drivers/ssd16xx_driver.h` | fasada display + współdzielony transport EPD + sterownik SSD16xx | Surowy backend EPD MONO10 SSD1608/SSD1673/SSD1675A/SSD1680/SSD1681 (propaguje DISPLAY + SPI) |
| `HAL_ENABLE_UC81XX` | `hal_display.h` + `hal/display/drivers/uc81xx_driver.h` | fasada display + współdzielony transport EPD + sterownik UC81xx | Surowy backend EPD MONO10 UC8175/UC8176/UC8151D/UC8179 (propaguje DISPLAY + SPI) |
| `HAL_ENABLE_CRYPTO` | `hal_crypto.h` + `hal_sc_auth.h` | `hal_crypto.cpp` + `hal_sc_auth.cpp` | Base64, MD5, SHA-256, HMAC-SHA256, ChaCha20-Poly1305 |
| `HAL_ENABLE_CRC` | `hal_crc.h` | `hal_crc.cpp` | generyczne sumy kontrolne CRC-8/16/32 dla integralności (włączane automatycznie przez ONEWIRE/DS18B20) |
| `HAL_ENABLE_CELLULAR_MODEM` | `hal_modem_at.h` | `hal_modem_at.cpp` | *(fasada - wymaga backendu rodziny modemów, np. `HAL_ENABLE_A7670`)* |
| `HAL_ENABLE_A7670` | `hal_simcom_a76xx.h` | `hal_simcom_a76xx.cpp` | Sterownik rodziny SimCom A76xx (propaguje CELLULAR_MODEM + UART) |
| `HAL_ENABLE_CJSON` | `hal/codecs/cjson/cJSON.h`, `hal/codecs/cjson/cJSON_Utils.h` (`tools.h` z C++) | `hal/codecs/cjson/cJSON.c`, `hal/codecs/cjson/cJSON_Utils.c` | zarządzany checkout cJSON z wersjonowanymi adapterami |
| `HAL_ENABLE_PNG` | `hal/codecs/lodepng/lodepng.h` (`tools.h` z C++) | `hal/codecs/lodepng/lodepng.cpp` | zarządzany checkout LodePNG z wersjonowanym adapterem profilu dla systemów wbudowanych |
| `HAL_ENABLE_PNG_AS_BASE64` | `hal/codecs/hal_image.h` + `hal/codecs/lodepng/lodepng.h` + `hal_crypto.h` | `hal/codecs/hal_image.cpp` + `hal/codecs/lodepng/lodepng.cpp` + `hal_crypto.cpp` | Funkcje pomocnicze dekodowania PNG zakodowanego w Base64 (propaguje CRYPTO + PNG) |
| `HAL_ENABLE_JPEG` | `hal/codecs/hal_image.h` + `hal/codecs/jpeg/tjpgd.h` | `hal/codecs/hal_image.cpp` + `hal/codecs/jpeg/tjpgd.c` | zarządzany rdzeń TJpgDec z wejściem z pamięci i wyjściem RGB565 |
| `HAL_ENABLE_JPEG_AS_BASE64` | `hal/codecs/hal_image.h` + `hal/codecs/jpeg/tjpgd.h` + `hal_crypto.h` | `hal/codecs/hal_image.cpp` + `hal/codecs/jpeg/tjpgd.c` + `hal_crypto.cpp` | Funkcje pomocnicze dekodowania JPEG zakodowanego w Base64 (propaguje CRYPTO + JPEG) |
| `HAL_ENABLE_UNITY` | nagłówki/źródła narzędziowe | `utils/unity.*` | zarządzany framework Unity |

<a id="flaga-opt-out"></a>

### Wyłączanie asercji

| Flaga | Efekt |
|---|---|
| `HAL_DISABLE_ASSERTS` | Zastępuje każdy `HAL_ASSERT()` operacją pustą. Asercje są domyślnie WŁĄCZONE. Odzwierciedla standardową konwencję `NDEBUG`. |

<a id="generowane-rozwiązywanie-zależności-modułów"></a>

### Jak ustalany jest wynikowy zestaw modułów

Konfiguracja przechowuje osobno moduły włączone bezpośrednio przez projekt i pełny zestaw po dodaniu ich zależności:

* `requestedFeatures` zawiera znormalizowane flagi żądane bezpośrednio w ostatecznej konfiguracji projektu i kompilacji.
* `resolvedFeatures` dodaje wszystkie przechodnie zależności `implies` z rejestru. Ten zestaw decyduje o wyborze źródeł i bibliotek oraz o skrócie funkcji używanym w danych płytki i linkowania.

Kompilator otrzymuje bezpośrednio żądane definicje. Wygenerowany nagłówek
`src/hal/generated/jh_hal_features.h` wprowadza ten sam pełny zbiór zależności
dla preprocesora C i C++. Poniższe zestawienie pokazuje publiczne zależności.
Pominięto zależności wewnętrzne prowadzące do automatycznie wyprowadzanego
symbolu `HAL_ENABLE_NETWORK_CORE`:

```
HAL_ENABLE_KV          -> HAL_ENABLE_EEPROM
HAL_ENABLE_SDLOGGER    -> HAL_ENABLE_FAT + HAL_ENABLE_EEPROM + HAL_ENABLE_SPI
HAL_ENABLE_BLE_COMMANDS -> HAL_ENABLE_BLE_STREAM + HAL_ENABLE_COMMAND_ROUTER ->
                           HAL_ENABLE_BLE + HAL_ENABLE_CRYPTO
HAL_ENABLE_BLE_STREAM  -> HAL_ENABLE_BLE + HAL_ENABLE_CRYPTO
HAL_ENABLE_BLUETOOTH_GAMEPAD -> HAL_ENABLE_BLUETOOTH_HID_HOST ->
                                HAL_ENABLE_BLUETOOTH_CLASSIC -> HAL_ENABLE_CRC
HAL_ENABLE_BLUETOOTH_AVRCP_TARGET -> HAL_ENABLE_BLUETOOTH_A2DP_SINK ->
                                     HAL_ENABLE_BLUETOOTH_CLASSIC -> HAL_ENABLE_CRC
HAL_ENABLE_SERIAL_COMMANDS -> HAL_ENABLE_COMMAND_ROUTER
HAL_ENABLE_LORA_COMMANDS -> HAL_ENABLE_COMMAND_ROUTER + HAL_ENABLE_LORA_LINK ->
                            HAL_ENABLE_LORA + HAL_ENABLE_CRC
HAL_ENABLE_TIME        -> HAL_ENABLE_UDP + HAL_ENABLE_WIFI
HAL_ENABLE_MQTT        -> HAL_ENABLE_TCP + HAL_ENABLE_WIFI
HAL_ENABLE_UDP         -> HAL_ENABLE_WIFI
HAL_ENABLE_TCP         -> HAL_ENABLE_WIFI
HAL_ENABLE_HTTP_SERVER -> HAL_ENABLE_TCP -> HAL_ENABLE_WIFI
HAL_ENABLE_HTTP_FILES  -> HAL_ENABLE_HTTP_SERVER -> HAL_ENABLE_TCP -> HAL_ENABLE_WIFI
HAL_ENABLE_WEBSOCKET   -> HAL_ENABLE_TCP -> HAL_ENABLE_WIFI
HAL_ENABLE_NET_CONSOLE -> HAL_ENABLE_TCP -> HAL_ENABLE_WIFI
HAL_ENABLE_NET_COMMANDS -> HAL_ENABLE_COMMAND_ROUTER + HAL_ENABLE_HTTP_SERVER +
                           HAL_ENABLE_WEBSOCKET + HAL_ENABLE_CJSON +
                           HAL_ENABLE_TCP + HAL_ENABLE_WIFI
HAL_ENABLE_NOTIFY_TELEGRAM -> HAL_ENABLE_NOTIFY + HAL_ENABLE_HTTP_CLIENT +
                              HAL_ENABLE_TLS + HAL_ENABLE_CJSON +
                              HAL_ENABLE_TCP + HAL_ENABLE_WIFI
HAL_ENABLE_BSD_SOCKETS -> HAL_ENABLE_UDP + HAL_ENABLE_TCP -> HAL_ENABLE_WIFI
HAL_ENABLE_TLS         -> HAL_ENABLE_TCP -> HAL_ENABLE_WIFI
HAL_ENABLE_HTTP_CLIENT -> HAL_ENABLE_TCP -> HAL_ENABLE_WIFI
HAL_ENABLE_OTA         -> HAL_ENABLE_WIFI + HAL_ENABLE_UDP + HAL_ENABLE_TCP + HAL_ENABLE_CRYPTO + HAL_ENABLE_CRC
HAL_ENABLE_WIREGUARD   -> HAL_ENABLE_UDP + HAL_ENABLE_WIFI
HAL_ENABLE_I2C_10BIT   -> HAL_ENABLE_I2C
HAL_ENABLE_EXTERNAL_ADC-> HAL_ENABLE_I2C
HAL_ENABLE_BH1750      -> HAL_ENABLE_I2C
HAL_ENABLE_ADP5360     -> HAL_ENABLE_I2C
HAL_ENABLE_MCP3221     -> HAL_ENABLE_I2C
HAL_ENABLE_TSC2007     -> HAL_ENABLE_I2C
HAL_ENABLE_STMPE610    -> HAL_ENABLE_I2C + HAL_ENABLE_SPI
HAL_ENABLE_PCF8563     -> HAL_ENABLE_RTC + HAL_ENABLE_I2C
HAL_ENABLE_DS3231      -> HAL_ENABLE_RTC + HAL_ENABLE_I2C
HAL_ENABLE_INTERNAL_RTC-> HAL_ENABLE_RTC
HAL_ENABLE_POWER_MANAGEMENT -> HAL_ENABLE_INTERNAL_RTC -> HAL_ENABLE_RTC
HAL_ENABLE_MCP9600     -> HAL_ENABLE_THERMOCOUPLE + HAL_ENABLE_I2C
HAL_ENABLE_MAX6675     -> HAL_ENABLE_THERMOCOUPLE
HAL_ENABLE_MCP401X     -> HAL_ENABLE_DIGIPOT + HAL_ENABLE_I2C
HAL_ENABLE_MAX5395     -> HAL_ENABLE_DIGIPOT + HAL_ENABLE_I2C
HAL_ENABLE_PGA2311     -> HAL_ENABLE_SPI
HAL_ENABLE_MCP23017    -> HAL_ENABLE_I2C
HAL_ENABLE_PCA9654E    -> HAL_ENABLE_I2C
HAL_ENABLE_PCF8574     -> HAL_ENABLE_I2C
HAL_ENABLE_HC595       -> HAL_ENABLE_SPI
HAL_ENABLE_MCP4725     -> HAL_ENABLE_I2C
HAL_ENABLE_MFRC522     -> HAL_ENABLE_SPI
HAL_ENABLE_PN532       -> HAL_ENABLE_SPI
HAL_ENABLE_DS18B20     -> HAL_ENABLE_ONEWIRE
HAL_ENABLE_ONEWIRE     -> HAL_ENABLE_CRC
HAL_ENABLE_A7670       -> HAL_ENABLE_CELLULAR_MODEM + HAL_ENABLE_UART
HAL_ENABLE_MCP2515     -> HAL_ENABLE_CAN + HAL_ENABLE_SPI
HAL_ENABLE_MCP251XFD   -> HAL_ENABLE_CAN + HAL_ENABLE_SPI
HAL_ENABLE_STM32G474_FDCAN -> HAL_ENABLE_CAN (STM32G474 only)
HAL_ENABLE_{ILI9341,ST7789,ST7735,ST7796S,GC9A01} -> HAL_ENABLE_TFT -> HAL_ENABLE_DISPLAY + HAL_ENABLE_SPI
HAL_ENABLE_SSD1306     -> HAL_ENABLE_DISPLAY + HAL_ENABLE_I2C
                           (SPI OLED transport additionally needs HAL_ENABLE_SPI)
HAL_ENABLE_{SSD1331,SSD135X} -> HAL_ENABLE_DISPLAY + HAL_ENABLE_SPI
HAL_ENABLE_ST7567      -> HAL_ENABLE_DISPLAY + HAL_ENABLE_I2C
HAL_ENABLE_{SSD16XX,UC81XX} -> HAL_ENABLE_DISPLAY + HAL_ENABLE_SPI
HAL_ENABLE_DACLESS     -> HAL_ENABLE_DMA_PWM_AUDIO + HAL_ENABLE_PWM_FREQ
HAL_ENABLE_PNG_AS_BASE64 -> HAL_ENABLE_CRYPTO + HAL_ENABLE_PNG
HAL_ENABLE_JPEG_AS_BASE64 -> HAL_ENABLE_CRYPTO + HAL_ENABLE_JPEG
```

Włącz tylko moduł, którego potrzebuje aplikacja. Jego zależności zostaną dodane automatycznie.

### Reguły zachowane poza rejestrem modułów v1

Reguły zależne od kontekstu, których nie opisuje rejestr v1, pozostają w `hal_config.h`:

| Kategoria | Utrzymane zachowanie |
|---|---|
| Warunkowa propagacja modułów | `HAL_ENABLE_EEPROM` z `HAL_EEPROM_TYPE == EEPROM_TYPE_AT24C256` dodaje `HAL_ENABLE_I2C`. `HAL_ENABLE_GPS` dodaje `HAL_ENABLE_UART` tylko wtedy, gdy nie wybrano ani UART, ani SWSERIAL. |
| Reguły providera i wyboru | Konfiguracja sieci wybiera dokładnie jeden backend i sprawdza, czy udostępnia wymagane funkcje. Fasady sprawdzają wybór providera dla RTC, modemu komórkowego, termopary, CAN, potencjometru cyfrowego, transportu GPS, wyświetlacza i TFT. |
| Reguły targetu i płytki | W `hal_config.h` pozostają zależne od kontekstu reguły obsługi kontrolera, targetu i płytki dla BLE; ograniczenia magistrali, stosu, profilu, pinów, targetu i płytki dla CYW43; ograniczenia targetu, toolchainu i nagłówka FreeRTOS; a także dostępność FDCAN wyłącznie na STM32G474. |
| Wartości domyślne, parametry strojenia, układ i zakresy | Fasada nadal ustala zależne od targetu wartości domyślne EEPROM i układ obszarów pamięci masowej/OTA, domyślne piny, zegar i kraj CYW43, rozmiary pul, limity kolejki oczekujących połączeń i TLS oraz pozostałe parametry i kontrole zakresów. |

Te reguły sprawdzają konfigurację podczas kompilacji, uwzględniając platformę, wybraną implementację, płytkę i parametry projektu.

`resolvedFeatures` i skrót zestawu modułów obejmują tylko zależności zapisane w rejestrze v1. Nie uwzględniają dwóch opisanych wyżej reguł kontekstowych. Dlatego konfiguracja samego GPS może w preprocesorze dodatkowo włączyć `HAL_ENABLE_UART`, a EEPROM AT24 - `HAL_ENABLE_I2C`, mimo że tych flag nie będzie w `resolvedFeatures` ani w jego skrócie.

Przy `HAL_CONFIG_VERBOSE` wygenerowany nagłówek sprawdza każdy zarejestrowany
symbol `HAL_ENABLE_*` i `HAL_DISABLE_*`. Komunikat powstaje po zastosowaniu
zależności warunkowych, dlatego `#pragma message` pokazuje ostateczny stan
preprocesora, łącznie z dodanym kontekstowo I2C lub UART.

<a id="przekazywanie-flag---zalecane-hal_project_configh"></a>

### Zalecany sposób: `hal_project_config.h`

Utwórz `hal_project_config.h` w katalogu swojego projektu firmware i włącz
moduły, których używasz:

```c
#pragma once
#define HAL_ENABLE_WIFI
#define HAL_ENABLE_KV            // -> propagates EEPROM
#define HAL_ENABLE_GPS           // -> auto-enables UART when no transport is selected
#define HAL_ENABLE_MCP9600       // -> propagates THERMOCOUPLE + I2C
#define HAL_ENABLE_BH1750        // -> propagates I2C
#define HAL_ENABLE_TSC2007       // -> propagates I2C
#define HAL_ENABLE_STMPE610      // -> propagates I2C + SPI
#define HAL_ENABLE_IRSMALL_DECODER
#define HAL_ENABLE_HD44780 // HD44780-compatible character LCD over GPIO
#define HAL_ENABLE_DACLESS // DACless PWM-audio engine -> DMA_PWM_AUDIO + PWM_FREQ
#define HAL_ENABLE_UART
#define HAL_ENABLE_PCF8563       // -> propagates RTC + I2C
#define HAL_ENABLE_INTERNAL_RTC  // -> RTC; native on STM32G474 and RP family
#define HAL_ENABLE_POWER_MANAGEMENT // -> INTERNAL_RTC + RTC
#define HAL_ENABLE_PWM_FREQ
```

Plik jest wykrywany przez `__has_include("hal_project_config.h")` przed automatycznym wyborem platformy. Umieszczaj w nim wyłącznie makra - bez dołączania innych nagłówków i bez warunków opartych na wyznaczanych później `HAL_TARGET_IS_*` lub `HAL_BOARD_IS_*`. Flagi wybierające źródła zapisuj bezwarunkowo jako `#define HAL_ENABLE_X` albo `#define HAL_ENABLE_X 1`. Jedynym dopuszczalnym warunkiem jest `#ifndef HAL_ENABLE_X` dla definiowanego symbolu. Nie umieszczaj tych flag w innych gałęziach `#if` lub `#ifdef`, także zależnych od platformy czy płytki: wczesny etap konfiguracji odczytuje plik tekstowo, bez pełnego przetwarzania warunków.

<a id="flaga-dostępności-freertos"></a>

### Włączanie integracji FreeRTOS

`HAL_ENABLE_FREERTOS` wybiera integrację środowiska wykonawczego z FreeRTOS, a nie zwykły moduł opcjonalny. Użyj tej flagi, gdy projekt ma korzystać bezpośrednio z nagłówków FreeRTOS:

```c
#include <FreeRTOS.h>
#include <task.h>
#include <semphr.h>
```

Zasady dla poszczególnych platform:

Mechanizm kompilacji i CMake dla natywnego RP oraz STM32G474 automatycznie wywołują
`scripts/component_manager.py component freertos --enable`, gdy wybrano
FreeRTOS. Jawne tryby skryptów budujących biblioteki statyczne najpierw
uruchamiają `scripts/ensure_freertos_kernel.sh`:
`--freertos` dla RP i STM32G474 oraz jawne `-D HAL_ENABLE_FREERTOS` dla
STM32G474. Adapter korzysta z tego samego menedżera. Jeśli flaga pochodzi
wyłącznie z `hal_project_config.h` albo z `-D` na RP, CMake mimo to
przygotowuje kernel. Zewnętrzny `JH_FREERTOS_KERNEL_DIR` jest
weryfikowany i nigdy nie jest zastępowany.

- Natywny RP2040/RP2350: użyj `./scripts/build_rp_native_lib.sh --freertos`
  lub wybierz `examples/18_freertos_suite` przez zwykły target VS Code.
  CMake wybiera port SMP w wersji wskazanej przez repozytorium dla RP2040,
  RP2350 ARM_NTZ lub RP2350 RISC-V, linkuje `heap_4`, tworzy `app_task0()`
  przypięte do rdzenia 0 oraz
  opcjonalne `app_task1()` przypięte do rdzenia 1, po czym uruchamia
  kernel FreeRTOS. Natywny USB jest obsługiwany przez dedykowane zadanie przypięte
  do rdzenia 0. Test sprzętowy w `tests/hardware/rp_freertos_smp`
  weryfikuje oba przypisania, muteksy międzyrdzeniowe, stan schedulera/sterty
  oraz obsługę przeciążenia CDC.
- STM32G474: użyj zależności `third_party/FreeRTOS-Kernel` w wersji wskazanej
  w `third_party/freertos_core_version.conf` lub przekaż
  `-DJH_FREERTOS_KERNEL_DIR=/path/to/FreeRTOS-Kernel`. Kompilacje CMake STM32
  kompilują jawną listę źródeł kernela Cortex-M4F, dołączają docelowy
  `FreeRTOSConfig.h`, używają `heap_4.c` i powierzają portowi FreeRTOS obsługę
  SVC/PendSV/SysTick. W trybie FreeRTOS `hal_mutex_*` na STM32
  używa muteksów FreeRTOS. `hal_delay_ms()` wywołuje `vTaskDelay()`, a
  `hal_idle()` oddaje sterowanie schedulerze, pod warunkiem że funkcje zostały
  wywołane z właściwego kontekstu zadania. Gdy dodatkowo zdefiniowane jest
  `HAL_PROVIDE_APP_ENTRY`, HAL wywołuje `app_start()`, tworzy zadanie
  FreeRTOS `app_task0()`, tworzy `app_task1()` tylko wtedy, gdy zdefiniowane
  jest `HAL_ENABLE_APP_TASK1`, a następnie wywołuje `vTaskStartScheduler()`.
- ESP32-S3: używa schedulera FreeRTOS uruchomionego już przez ESP-IDF w wersji
  wskazanej przez repozytorium. `app_main()` wywołuje `app_start()` i domyślnie tworzy
  `app_task0()` na rdzeniu 0, opcjonalnie tworzy `app_task1()` na rdzeniu 1 i
  wraca do ESP-IDF. Target wymaga `HAL_ENABLE_FREERTOS`.
  `HAL_FREERTOS_TASK0_CORE` oraz `HAL_FREERTOS_TASK1_CORE` akceptują docelowy
  rdzeń lub `-1` oznaczające brak przypisania.
- Host/mock: `HAL_ENABLE_FREERTOS` nie jest obsługiwane przez zwykły backend
  mock. CI używa opcjonalnej kompilacji hosta `JH_ENABLE_FREERTOS_POSIX_TESTS`
  do skompilowania portu GCC/POSIX kernela FreeRTOS, uruchomienia rzeczywistego
  schedulera na wątkach pthread i sprawdzenia w `ctest` kodu STM32G474
  kompilowanego na hoście z `HAL_ENABLE_FREERTOS`.

Domyślne parametry zadań tworzonych przez HAL w integracji FreeRTOS:

| Makro | Domyślnie | Jednostka / znaczenie |
|---|---|---|
| `HAL_FREERTOS_CORE_COUNT` | `2` | Liczba rdzeni schedulera natywnego RP; dozwolone wartości to `1` i `2`. Konfiguracja jednordzeniowa nie może włączać `HAL_ENABLE_APP_TASK1`. |
| `HAL_FREERTOS_TASK0_STACK` | `512` | Słowa stosu FreeRTOS dla `app_task0()` |
| `HAL_FREERTOS_TASK1_STACK` | `512` | Słowa stosu FreeRTOS dla `app_task1()` |
| `HAL_FREERTOS_TASK0_PRIORITY` | `tskIDLE_PRIORITY + 1` | Priorytet FreeRTOS dla `app_task0()` |
| `HAL_FREERTOS_TASK1_PRIORITY` | `tskIDLE_PRIORITY + 1` | Priorytet FreeRTOS dla `app_task1()` |
| `HAL_FREERTOS_HEAP_SIZE` | `164 * 1024` | Rozmiar puli `heap_4` RP w bajtach |
| `HAL_USB_FREERTOS_TASK_STACK` | `512` | Stos zadania USB rdzenia 0 RP w słowach FreeRTOS |
| `HAL_USB_FREERTOS_TASK_PRIORITY` | `tskIDLE_PRIORITY + 2` | Priorytet zadania USB rdzenia 0 RP |

ESP32-S3 zmienia oba domyślne rozmiary stosu aplikacji na `3072` bajty, zgodnie
z jednostką używaną przez ESP-IDF, i ustawia domyślne wartości rdzenia
`HAL_FREERTOS_TASK0_CORE=0` oraz `HAL_FREERTOS_TASK1_CORE=1`. Zadania aplikacji
nadal używają wspólnych domyślnych priorytetów
`tskIDLE_PRIORITY + 1`.

STM32G474 używa puli `heap_4` o rozmiarze 24 KiB z pliku
`FreeRTOSConfig.h` właściwego dla tego targetu. Stosy zadań aplikacji używają tych samych
domyślnych 512 słów; `_Min_Stack_Size` linkera pozostaje rezerwacją stosu
rozruchu/wyjątków.

Nadpisania rozmiaru stosu platformy:

| Makro | Domyślnie | Jednostka / znaczenie |
|---|---|---|
| `HAL_STM32_MAIN_STACK_SIZE` | `0x800` | Bajty zarezerwowane jako `_Min_Stack_Size` STM32 (rezerwa linkera między stertą a stosem na szczycie RAM-u) |
| `HAL_RP_CORE0_STACK_SIZE` | `0x800` | Bajty mapowane na `PICO_STACK_SIZE` dla dowolnego natywnego targetu RP |
| `HAL_RP_CORE1_STACK_SIZE` | `HAL_RP_CORE0_STACK_SIZE` / `0x800` | Bajty mapowane na `PICO_CORE1_STACK_SIZE` dla dowolnego natywnego targetu RP |

**Wielowątkowość:** tryby FreeRTOS na RP2040, STM32G474 i ESP32-S3 udostępniają
mechanizmy muteksów, opóźnień i bezczynności. Dla kodu wrażliwego na czas
`hal_critical_section_*` korzysta bezpośrednio z mechanizmu sekcji krytycznej
przerwań danego targetu. ESP32-S3 dodatkowo synchronizuje oba rdzenie za pomocą
wspólnego `portMUX_TYPE`. Implementacja ma zapasowy, atomowy mechanizm
jednokrotnego tworzenia muteksów singletonów i magistral oraz zabezpiecza callback
I2C slave na RP2040. Kontekst callbacku timera i wyjątki właściwe dla
poszczególnych modułów wymagają osobnej analizy, zanim będzie można zadeklarować
silniejsze gwarancje bezpieczeństwa współbieżnych wywołań.

Narzędzia projektu automatycznie dodają katalog nagłówków aplikacji. W projektach generowanych przez `jh-vscode` korzystaj z zadań `Project: Build` i `Project: Select board`.

<a id="alternatywa-flagi--d-w-linii-poleceń"></a>

### Alternatywa: definicje `-D` w poleceniu kompilatora

```bash
./scripts/build_rp_native_lib.sh \
  --target rp2040 \
  --board picow \
  -D HAL_ENABLE_WIFI \
  -D HAL_ENABLE_EEPROM \
  -D HAL_ENABLE_GPS \
  -D HAL_ENABLE_I2C
```

<a id="moduły-podstawowe-bez-flagi-wyłączającej"></a>

### Moduły podstawowe, których nie wyłącza się flagą

| Moduł | Przeznaczenie |
|---|---|
| `hal_gpio` | Odczyt / zapis / przerwania GPIO |
| `hal_adc` | ADC wbudowany w układ |
| `hal_pwm` | Podstawowy PWM w stylu analogWrite |
| `hal_timer` | Niskopoziomowe alarmy jednorazowe plus zarządzane timery (okresowe/jednorazowe tworzenie/start/stop/pauza/wznowienie/zapytanie) |
| `hal_system` | millis / delay / watchdog / idle + niezależne od typu `hal_constrain` / `hal_map` + `COUNTOF(arr)` |
| `hal_bits` | funkcje pomocnicze dla bitów (`is_set`, `set_bit`, `bitSet`, operacje na rejestrach volatile) |
| `hal_sync` | Muteksy, sekcje krytyczne |
| `hal_serial` | Wyjście szeregowe debug |
| `hal_spi` | Inicjalizacja magistrali SPI |
| `hal_math` | niezależne od typu makra `hal_constrain` / `hal_map` |

`hal_crypto` jest włączany opcjonalnie przez `HAL_ENABLE_CRYPTO` i nie należy
do zestawu dostępnego bezwarunkowo.

### Zarządzanie zależnościami

Opcjonalne integracje firm trzecich wykorzystywane przez moduły HAL są
wybierane przez CMake. Funkcje pomocnicze specyficzne dla targetu RP
znajdują się pod `src/hal/impl/rp2040/drivers/rp2040/`. Przenośne nagłówki,
fasady, sterowniki urządzeń i kod wielokrotnego użytku są umieszczone
tematycznie pod `src/hal/<domain>/`. `src/hal/impl/` jest zarezerwowane dla
backendów `.mock`, `rp2040` i `stm32g474`.

Zestaw włączonych modułów decyduje, które zależności zostaną skompilowane:

- włączone moduły (`HAL_ENABLE_*`) dołączają swoje backendy firm trzecich;
- moduły pozostawione wyłączone (ustawienie domyślne) nie udostępniają
  deklaracji ani nie dołączają implementacji do kompilacji.

\* `HAL_ENABLE_TIME` włącza współdzielony zegar runtime, status, NTP
oraz API czasu lokalnego. Z `HAL_ENABLE_RTC` może przywracać stan z RTC i
zapisywać zwalidowane wyniki NTP. Czyste funkcje pomocnicze
`hal_get_seconds()`, `hal_time_from_components(...)`,
`hal_time_is_daylight_saving_time(...)`,
`hal_time_adjust_cet_cest(...)`, `hal_time_is_in_range(...)` oraz
`hal_time_extract_minutes(...)` pozostają dostępne bezwarunkowo, bez
zależności sieciowej.

---


---

*Dalej: [Przewodnik po bezpieczeństwie wielordzeniowym, sterownikach i migracji](03_build_tests.md)*
