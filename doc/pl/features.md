# Przegląd funkcjonalności JaszczurHAL

*Dostępne również [po angielsku](../en/features.md).*

Ten dokument to wysokopoziomowy spis tego, co oferuje JaszczurHAL. Jest
pomyślany jako zwięzła mapa funkcjonalności, a nie dokumentacja API. Sygnatury
funkcji, szczegóły konfiguracji i zachowanie modułów znajdziesz w
[JaszczurHAL_API.md](JaszczurHAL_API.md).
Model projektu firmware w VS Code z wyborem targetu opisuje
[FwProjectWorkflow.md](FwProjectWorkflow.md), a instalację i działanie OTA dla
natywnego RP i ESP32-S3 - [OTAWorkflow.md](OTAWorkflow.md).

Kod aplikacji i drivery wielokrotnego użytku powinny korzystać z publicznego API
JaszczurHAL. Szczegóły Pico SDK, ESP-IDF i rejestrów targetu należą do
implementacji wybranego backendu; nie są alternatywnym API aplikacji. Aplikacje
wciąż mogą wywoływać API Pico SDK lub
ESP-IDF bezpośrednio, gdy wymagane jest zachowanie specyficzne dla targetu,
ale takie wywołania omijają abstrakcję HAL, wiążą dotknięty nimi kod z daną
platformą i działają wbrew celowi przenośności JaszczurHAL.

## Przenośne API, platformy i możliwości buildu

| Obszar | Co oferuje | Źródło |
|---|---|---|
| Jedno przenośne API JaszczurHAL | Kod aplikacji oraz driverów wielokrotnego użytku wywołuje to samo publiczne API C `hal_*` na wszystkich wspieranych targetach. Wybór targetu dobiera właściwy backend, a capabilities płytki i statusy ujawniają zamierzone różnice sprzętowe bez zmiany modelu programowania widocznego dla aplikacji. | [publiczny HAL](../../src/hal/), [dokumentacja API](JaszczurHAL_API.md) |
| Profile płytek i możliwości runtime | Generowane profile RP2040, RP2350, STM32G474, ESP32-S3 i hosta pochodzące z rejestru płytek. Wspierane buildy udostępniają współdzieloną fasadę płytki w runtime. | [rejestr płytek](../../boards/README.md), [hal_board.h](../../src/hal/system/hal_board.h) |
| Backend RP2040 / RP2350 | Implementuje współdzielone API JaszczurHAL dla RP2040, RP2350 ARM i RP2350 Hazard3 RISC-V, z dokładnym wyborem chipu i ISA oraz opcjonalnym FreeRTOS. Wewnętrznie wykorzystuje oficjalny Pico SDK do niskopoziomowej obsługi platformy i natywnego buildu; przenośny kod aplikacji nadal wywołuje JaszczurHAL. | [backend RP](../../src/hal/impl/rp2040/), [build natywny](../../rp_native_lib/) |
| Backend STM32G474 | Implementuje współdzielone API JaszczurHAL dla STM32G474 w wariantach bare-metal i FreeRTOS. Wewnętrzne elementy specyficzne dla targetu zapewniają start, obsługę linkera, skoordynowane usługi flash, natywne peryferia oraz opcjonalną łączność CYW43 przez gSPI. | [backend STM32G474](../../src/hal/impl/stm32g474/) |
| Backend ESP32-S3 | Implementuje współdzielone API JaszczurHAL na bazie ESP-IDF, które dostarcza bazowy runtime platformy, system buildu i natywne usługi. Backend oraz narzędzia projektowe udostępniają generowaną konfigurację płytki/pamięci, uruchamianie task0/task1 FreeRTOS, implementacje rdzenia i peryferiów, natywne WiFi/lwIP, bezpieczny wybór USB Serial/JTAG, build/upload/monitor/IntelliSense oraz surowe OTA aplikacji. | [implementacja ESP32](../../src/hal/impl/esp32/), [stanowisko Fazy 2](../api/pl/03_build_tests.md#sprzętowy-test-esp32-s3-faza-2), [stanowisko buildu Fazy 3](../../tests/fixtures/esp32s3_phase3/) |
| Backend mock | Implementuje to samo publiczne API JaszczurHAL jako deterministyczny backend hosta do testów jednostkowych i rozwoju przenośnego API bez sprzętu. | [backend mock](../../src/hal/impl/.mock/) |
| Moduły opcjonalne włączane podczas buildu | Funkcje opcjonalne są wybierane flagami `HAL_ENABLE_*`; build dołącza wyłącznie wymagane przez nie zależności. | [hal_config.h](../../src/hal/core/hal_config.h) |
| Warstwa przenośności kompilatora | Jeden nagłówek rozwiązuje rozszerzenia kompilatora, od których zależy HAL - noreturn, wymuszony inline, trap/unreachable, pakowanie struktur i zliczanie wiodących zer - na GNU, Clang i MSVC. | [hal_compiler.h](../../src/hal/core/hal_compiler.h) |
| Przenośny punkt wejścia aplikacji | Wspólny model `app_start()` / `app_task0()` / opcjonalnego `app_task1()` na wspieranych targetach i w przykładach, wraz z `main()` należącym do HAL, `app_main()` ESP-IDF i opcjonalnym startem core-1 na RP. | [hal_app.h](../../src/hal/core/hal_app.h) |
| Integracja FreeRTOS | Przypięty oryginalny kernel z natywnymi portami SMP dla RP2040/RP2350 oraz portem Cortex-M4F dla STM32G474, plus scheduler dostarczany przez przypięty ESP-IDF. Obejmuje mutex/opóźnienia/raportowanie runtime świadome FreeRTOS oraz start zadania aplikacji zarządzany przez HAL. | [przenośny punkt wejścia aplikacji](../../src/hal/core/hal_app.h), [flagi modułów](../api/pl/02_module_flags.md) |
| Ochrona stosu | Niezależne opt-iny zapewniają synchroniczne strażniki granic stosu Pico SDK/MPU/ESP-IDF oraz kanarki ramki `-fstack-protector-strong` GCC/Clang tam, gdzie są wspierane; buildy FreeRTOS mogą dodatkowo sprawdzać granice stosu zadań. | [hal_system.h](../../src/hal/system/hal_system.h), [flagi modułów](../api/pl/02_module_flags.md) |
| Projekty firmware oparte na dispatcherze | Wspólny workflow VS Code/CMake dla generowanych projektów, przeniesionych modułów downstream i przykładów w repozytorium, z wyborem targetu/płytki i osobnym cache CMake dla każdego targetu. | [FwProjectWorkflow.md](FwProjectWorkflow.md) |
| Buildy bibliotek statycznych | Dedykowane przepływy CMake/pomocnicze dla oficjalnych buildów RP na Pico SDK i STM32G474; przepływ RP dodatkowo weryfikuje generowanie ELF/BIN/UF2 i symbole wejścia aplikacji. | [build RP](../../rp_native_lib/), [build STM32](../../stm32_lib/) |
| Bramka walidacji | Pełna lokalna bramka dla testów jednostkowych, Valgrind, analizy statycznej, buildów targetów RP/STM, fixture'a wieloobrazowego buildu ESP-IDF `esp32s3_phase3` oraz przykładów. | [runalltests.sh](../../runalltests.sh) |

## Rdzeń HAL

| Obszar | Co oferuje | Źródło |
|---|---|---|
| GPIO | Przenośne cyfrowe I/O, tryby pull i przerwania, wraz z jawnym przypisaniem rdzenia obsługującego IRQ i diagnostyką na wielordzeniowych targetach RP i ESP32-S3. | [hal_gpio.h](../../src/hal/gpio/hal_gpio.h) |
| ADC | Przenośna, serializowana abstrakcja wejścia analogowego, wraz z kanałami ADC oneshot ESP32-S3 wybieranymi z generowanych masek pinów płytki. | [hal_adc.h](../../src/hal/analog/hal_adc.h) |
| DAC | Prawdziwe wyjście DAC z dodatkową diagnostyką na STM32G474 i mocku hosta; targety bez sprzętowego DAC zwracają `HAL_EUNSUPPORTED`. | [hal_dac.h](../../src/hal/analog/hal_dac.h) |
| PWM | Przenośne wyjście PWM oraz pomocnicze funkcje PWM ze sterowaniem częstotliwością. | [hal_pwm.h](../../src/hal/gpio/hal_pwm.h), [hal_pwm_freq.h](../../src/hal/gpio/hal_pwm_freq.h) |
| Zliczanie impulsów | Zliczanie zboczy/impulsów do pomiaru sygnału i prostych zastosowań licznikowych. | [hal_pcnt.h](../../src/hal/analog/hal_pcnt.h) |
| Timery i czas systemowy | Podstawowe timery, rozszerzone funkcje timerów, idle/delay, watchdog, unikalny identyfikator urządzenia oraz diagnostyka awarii/faultów z obsługą faultów specyficzną dla targetu. | [hal_timer.h](../../src/hal/timers/hal_timer.h), [hal_system.h](../../src/hal/system/hal_system.h) |
| Zarządzanie niskim poborem mocy | Sterowany możliwościami CPU Sleep, STM32G474 STOP0/STOP1/Standby, klasyfikacja wybudzenia z RTC, przywracanie zegara, callbacki oraz kompensacja czasu monotonicznego dla przejść zsynchronizowanych z RTC. | [hal_power.h](../../src/hal/power/hal_power.h), [API zasilania](../api/pl/06_timers_system.md#halpower-przejścia-niskiego-poboru-mocy-opcjonalny-halenablepowermanagement) |
| Synchronizacja | Muteksy i sekcje krytyczne z implementacjami specyficznymi dla targetu. | [hal_sync.h](../../src/hal/system/hal_sync.h) |
| Timery programowe | Lekkie kooperacyjne timery programowe. | [hal_soft_timer.h](../../src/hal/timers/hal_soft_timer.h) |
| Prymitywy narzędziowe | Pomocnicze funkcje bitowe, matematyczne, sterowanie PID, wsparcie watchdoga i popularne API narzędziowe. | [tools.h](../../src/tools.h), [utils](../../src/utils/) |

## Komunikacja i łączność

| Obszar | Co oferuje | Źródło |
|---|---|---|
| UART | Abstrakcja sprzętowej komunikacji szeregowej; przypisanie cyklu życia do rdzenia na RP2040 i ESP32-S3 podąża za rdzeniem, który uruchamia UART. | [hal_uart.h](../../src/hal/serial/hal_uart.h) |
| USB device / CDC | API cyklu życia USB i CDC zwracające status. Na RP HAL zarządza TinyUSB, deskryptorami, obsługą w tle, ograniczonym backpressure i resetem 1200 bps do BOOTSEL; dostępny jest też mock hosta. | [hal_usb.h](../../src/hal/usb/hal_usb.h) |
| Konsola szeregowa/debug | Jeden współdzielony rdzeń serializujący TX ze strumieniowym formatowaniem w kontekście zadania, logami odroczonymi z ISR, limitowaniem błędów per źródło, mirroringiem do konsoli sieciowej oraz linkowanym podczas buildu RP USB CDC, ESP-IDF USB Serial/JTAG VFS, STM32 USART2/stdout lub portami przechwytywania/RX mocka. | [hal_serial.h](../../src/hal/serial/hal_serial.h), [API szeregowe](../api/pl/08_sync_serial.md) |
| Software serial | Zoptymalizowany pod target programowy UART: natywny PIO/DMA Pico SDK na RP2040 i współdzielony backend HAL GPIO na pozostałych targetach. | [hal_swserial.h](../../src/hal/serial/hal_swserial.h) |
| I2C master | Przenośne API kontrolera i2c z obsługą dwóch magistral, funkcjami atomowymi, odzyskiwaniem magistrali, opcjonalnym adresowaniem 10-bitowym (`HAL_ENABLE_I2C_10BIT`) i ograniczonym 7-bitowym skanerem przyjmującym callback watchdog/postępu. | [hal_i2c.h](../../src/hal/i2c/hal_i2c.h) |
| I2C slave | Wsparcie I2C w trybie target/mapy rejestrów. | [hal_i2c_slave.h](../../src/hal/i2c/hal_i2c_slave.h) |
| SPI | Przenośne API mastera/kontrolera SPI plus neutralne względem targetu deskryptory magistrali/CS/ustawień per urządzenie, wraz z API transferu zwracającym status oraz blokującymi/asynchronicznymi ścieżkami zapisu z obsługą DMA tam, gdzie są wspierane. | [hal_spi.h](../../src/hal/spi/hal_spi.h), [hal_spi_device.h](../../src/hal/spi/hal_spi_device.h) |
| Router poleceń i wiadomości sieciowe | Neutralne względem transportu nazwane handlery z polityką źródła/bezpieczeństwa, binarnie bezpiecznymi metadanymi żądania, ograniczonymi odpowiedziami oraz wersjonowanymi wiadomościami żądanie/odpowiedź/zdarzenie dla adapterów pakietowych lub strumieni ramkowanych. | [API poleceń](../api/pl/23_commands.md), [hal_command_router.h](../../src/hal/commands/hal_command_router.h), [hal_command_wire.h](../../src/hal/commands/hal_command_wire.h) |
| Surowe radio LoRa | Niezależny od providera cykl życia dla zwalidowanego sprzętu SX1262 oraz eksperymentalnych, niesprawdzonych fizycznie integracji SX1261, SX1276 i SX1278. Obejmuje asynchroniczne TX/RX/CAD, bieżące RSSI, capabilities, callbacki, anulowanie, metadane pakietu, diagnostykę, stany zasilania i airtime; SX126x dodatkowo udostępnia jawną kalibrację zależną od pasma. | [API radia LoRa](../api/pl/21_lora.md), [hal_lora_radio.h](../../src/hal/radio/hal_lora_radio.h) |
| Niezawodne łącze LoRa | Prywatne adresowane wiadomości przez jeden uchwyt surowego LoRa z 32-bitowymi sekwencjami, ACK i ograniczoną retransmisją, tłumieniem duplikatów, przezroczystą fragmentacją, CRC całej wiadomości oraz opcjonalną ochroną ChaCha20-Poly1305. | [API łącza LoRa](../api/pl/22_lora_link.md), [hal_lora_link.h](../../src/hal/radio/hal_lora_link.h) |
| Polecenia LoRa | Żądania poleceń, automatycznie wysyłane odpowiedzi i nazwane zdarzenia przez niezawodne łącze LoRa pozostające pod wyłączną kontrolą adaptera, z ograniczonymi kolejkami kopiującymi dane i metadanymi łącza przekazywanymi do handlerów. | [API poleceń](../api/pl/23_commands.md), [hal_lora_commands.h](../../src/hal/radio/hal_lora_commands.h) |
| API statusu sieci | Operacje `hal_status_t` dla WiFi/DNS, TCP/UDP, MQTT i WireGuard rozszerzają dotychczasowe API z zachowaniem wrapperów zgodności wstecznej i dokładnym mapowaniem stanów absent/inactive/failed na sprzęt płytki. | [API łączności](../api/pl/15_connectivity.md) |
| Fasada CAN | Wspólne API wybierające backend klasycznego CAN lub CAN FD. | [hal_can.h](../../src/hal/can/hal_can.h) |
| MCP2515 CAN | Współdzielony backend CAN po SPI. | [driver mcp2515](../../src/hal/can/mcp2515/) |
| MCP2517FD/MCP2518FD CAN FD | Współdzielony backend CAN FD po SPI. | [driver mcp251xfd](../../src/hal/can/mcp251xfd/) |
| Natywny FDCAN STM32G474 | Natywny backend FDCAN STM32G474. | [backend STM32 FDCAN](../../src/hal/impl/stm32g474/hal_can_stm32g474_fdcan.cpp) |
| MFRC522 RFID | Współdzielony driver czytnika RFID przez HAL SPI/I2C. | [hal_mfrc522.h](../../src/hal/nfc/hal_mfrc522.h), [driver mfrc522](../../src/hal/nfc/mfrc522/) |
| PN532 NFC/RFID | Współdzielony driver czytnika NFC/RFID przez HAL SPI/I2C/UART. | [hal_pn532.h](../../src/hal/nfc/hal_pn532.h), [driver pn532](../../src/hal/nfc/pn532/) |
| WiFi | Łączność CYW43/lwIP na Pico W, Pico 2 W, Pico+PIM730 oraz skonfigurowanym sprzęcie STM32G474+PIM730, plus natywne WiFi/`esp_netif`/lwIP z ESP-IDF na ESP32-S3. | [hal_wifi.h](../../src/hal/network/hal_wifi.h) |
| BLE Peripheral i Observer | Jedno połączenie Peripheral, kopiowane klasyczne rozgłaszanie (advertising), pasywne skanowanie z kopiowanymi raportami o ograniczonym rozmiarze i parsowaniem AD, statyczne usługi GAP/GATT, raportowanie ATT MTU, backendy sprzętowe dla Pico W, Pico 2 W, Pico+PIM730/RM2 i STM32G474+PIM730/RM2 oraz deterministyczny mock hosta. | [API Bluetooth](../api/pl/20_bluetooth.md) |
| UDP | Transport UDP oparty na uchwytach z wieloma gniazdami plus wrapper zgodności wstecznej dla pojedynczego gniazda w buildach WiFi. | [hal_udp.h](../../src/hal/network/hal_udp.h) |
| Gniazda TCP | Klienckie gniazda TCP oparte na uchwytach oraz uchwyty listenera/serwera z connect, bind/listen/accept, send/recv, shutdown oraz backendami mock, CYW43/lwIP i natywnym ESP-IDF lwIP. | [hal_tcp.h](../../src/hal/network/hal_tcp.h) |
| JH BLE Stream v1 | Strumień bajtów ogólnego przeznaczenia o ograniczonym rozmiarze, przenoszony przez jedną statyczną usługę GATT. Zapewnia wersjonowane ramkowanie z negocjacją możliwości, wzajemne potwierdzanie tożsamości przez HMAC-SHA256, kierunkowe klucze ChaCha20-Poly1305, ochronę przed powtórzeniem, ograniczanie częstotliwości żądań oraz kolejki RX/TX o ustalonej pojemności. | [API Bluetooth](../api/pl/20_bluetooth.md) |
| Polecenia BLE Stream | Fragmentacja i rekonstrukcja command-wire z uwzględnieniem MTU, automatyczny dispatch i odpowiedzi routera, nazwane zdarzenia, uwierzytelnione metadane peera/sesji oraz odzyskiwanie fail-closed. Adapter przejmuje wyłączną obsługę jednej uwierzytelnionej sesji JH BLE Stream. | [API poleceń](../api/pl/23_commands.md#uwierzytelniony-adapter-ble-stream), [hal_ble_commands.h](../../src/hal/bluetooth/hal_ble_commands.h) |
| Serwer HTTP | Mały, sterowany pollingiem serwer HTTP/1.1 w czystym tekście przez HAL TCP z trasami dokładnymi/prefiksowymi, nagłówkami żądania, buforowanymi odpowiedziami, automatycznym `Content-Length` i obsługą żądań testowalną mockiem. Nie zdefiniowano API serwera HTTPS. | [hal_http_server.h](../../src/hal/network/http/hal_http_server.h) |
| Pliki HTTP | Serwowanie plików statycznych oparte na callbackach, ETag/`If-None-Match`, surowe helpery PUT i uploadu multipart przez trasy HAL HTTP. | [hal_http_files.h](../../src/hal/network/http/hal_http_files.h) |
| Serwer WebSocket | Mały, sterowany pollingiem serwer WebSocket w czystym tekście przez HAL TCP z handshakiem HTTP Upgrade, callbackami, helperami wysyłki i rozgłaszaniem. Nie zdefiniowano API WSS ani klienta WebSocket. | [hal_websocket.h](../../src/hal/network/websocket/hal_websocket.h) |
| Konsola sieciowa | Chroniona hasłem konsola TCP, która mirroruje wyjście `hal_serial`/debug do uwierzytelnionych klientów, zachowując przy tym lokalne logi UART/USB, plus dwukierunkowe wejście poleceń. | [hal_net_console.h](../../src/hal/network/net_console/hal_net_console.h) |
| Polecenia sieciowe | Adaptery tekst/JSON oparte na cJSON dla kanałów sterujących HTTP i WebSocket przez współdzielony router poleceń, zachowujące ustalone API handlerów specyficzne dla sieci. | [API poleceń](../api/pl/23_commands.md), [hal_net_commands.h](../../src/hal/network/net_commands/hal_net_commands.h) |
| Adapter gniazd BSD | Minimalna warstwa zgodności IPv4 `sys/socket.h` / `netinet/in.h` / `arpa/inet.h` / `netdb.h` przez uchwyty HAL UDP/TCP, wraz z `getaddrinfo()`, `setsockopt()`, `O_NONBLOCK`, `MSG_DONTWAIT` i gotowością `select()`. | [socket.h](../../src/sys/socket.h), [netdb.h](../../src/netdb.h) |
| TLS | Niezależne od providera API klienta TLS oparte na BearSSL i natywnym HAL TCP, z kotwicami zaufania, callbackami czasu/entropii, anulowaniem, ograniczonym pollingiem i opcjonalnym mostkiem do transportu gniazd BSD. | [hal_tls.h](../../src/hal/network/tls/hal_tls.h), [transport BearSSL](../../src/hal/network/tls/BearSSL/) |
| Klient HTTP/HTTPS | Ograniczone jednorazowe żądania HTTP/1.1 przez HAL TCP lub zweryfikowany BearSSL TLS. Bufory nagłówków i ciała należą do wywołującego, a odpowiedź zawiera jawne metadane. | [hal_http_client.h](../../src/hal/network/http/hal_http_client.h), [API łączności](../api/pl/15_connectivity.md#halhttpclient-httphttps-client-opt-in-halenablehttpclient) |
| Powiadomienia | Fasada delegująca powiadomienia do wybranego backendu; dostępny backend Telegram Bot API korzysta z istniejącego klienta HTTP/HTTPS. | [hal_notify.h](../../src/hal/network/notify/hal_notify.h), [API łączności](../api/pl/15_connectivity.md#halnotify-notifications-opt-in-halenablenotify) |
| MQTT | Wrapper łączności MQTT oparty na PubSubClient. | [hal_mqtt.h](../../src/hal/network/mqtt/hal_mqtt.h) |
| OTA | Natywne aktualizacje firmware przez HAL UDP/TCP z wykrywaniem, opcjonalnym fail-closed uwierzytelnianiem hasłem AUTH2, potwierdzeniem próbnym, rollbackiem i wgrywaniem z VS Code. RP używa podpisanego, wersjonowanego kontenera i wznawialnej podmiany; ESP32-S3 używa zwalidowanego manifestem surowego obrazu aplikacji i partycji OTA ESP-IDF. | [hal_ota.h](../../src/hal/network/ota/hal_ota.h), [workflow OTA](OTAWorkflow.md) |
| Kalendarz / NTP / czas dnia | Zawsze dostępne helpery kalendarza gregoriańskiego oraz jeden thread-safe zegar ścienny runtime ze snapshotami źródła/statusu, odtwarzaniem z RTC, trwałym stanem NTP, adapterami libc i ograniczonym wyborem źródła podstawowego lub zapasowego. | [hal_time.h](../../src/hal/time/hal_time.h) |
| WireGuard | Współdzielona integracja WireGuard z host-lwIP z routingiem split/full tunnel na backendach reklamujących tę możliwość. | [hal_wireguard.h](../../src/hal/network/wireguard/hal_wireguard.h), [silnik WireGuard](../../src/hal/network/wireguard/core/) |
| Modem komórkowy | Generyczny silnik modemu na komendach AT plus wsparcie rodziny SimCom A76xx. | [hal_modem_at.h](../../src/hal/modem/hal_modem_at.h), [hal_simcom_a76xx.h](../../src/hal/modem/hal_simcom_a76xx.h) |

## Pamięć masowa, pliki i logowanie

| Obszar | Co oferuje | Źródło |
|---|---|---|
| Koordynator transakcji flash | Jeden wewnętrzny koordynator dla wszystkich natywnych mutacji flash: serializuje wywołujących, zabezpiecza drugi rdzeń, wstrzymuje TinyUSB, odrzuca callbacki rezydujące w XIP i aktywne DMA, stosuje ograniczone timeouty i przywraca stan runtime. EEPROM/KV, LittleFS i etapowanie OTA przechodzą przez niego na natywnym RP; STM32G474 używa swoich skoordynowanych usług flash. | [drivery flash rp](../../src/hal/impl/rp2040/drivers/flash/), [API pamięci masowej](../api/pl/14_storage.md) |
| Abstrakcja EEPROM | Jedna fasada trwałej pamięci wybierająca provider, ze wspólną obsługą blokad i zakresów, przenośnym driverem AT24C256, providerami flash targetu i pamięci hosta oraz API zwracającym status (`hal_status_t`). | [hal_eeprom.h](../../src/hal/storage/hal_eeprom.h) |
| Pamięć klucz-wartość | Mała trwała warstwa klucz-wartość na bazie pamięci stylu EEPROM, wraz z API get/set/commit zwracającym status (`hal_status_t`). | [hal_kv.h](../../src/hal/storage/hal_kv.h) |
| LittleFS | Lekki cykl życia/funkcje pomocnicze systemu plików, wraz z API mount/format/path zwracającym status (`hal_status_t`); natywny RP i STM32G474 używają zarezerwowanych przez linker partycji flash wewnętrznej. | [hal_littlefs.h](../../src/hal/storage/hal_littlefs.h) |
| FatFs / SD po SPI | Dokładnie ustalony checkout FatFs R0.16 i współdzielone I/O dysku SD po SPI. | [framework systemu plików](../../src/hal/storage/filesystem/) |
| SD logger | Wsparcie logowania na karcie SD i logowania raportów awarii. | [hal_sdlogger.h](../../src/hal/storage/hal_sdlogger.h), [sdlogger](../../src/hal/storage/filesystem/sdlogger/) |

## Sensory, urządzenia wejściowe i pomiar czasu

| Obszar | Co oferuje | Źródło |
|---|---|---|
| Fasada RTC | Jedna niezależna od targetu fasada zegara czasu rzeczywistego z dynamicznie wybieranymi providerami układu i mocka, API datetime/epoch/alarm/timer/relative-wake zwracającym status (`hal_status_t`), wspólnym cyklem życia i blokadami oraz walidacją gregoriańską. | [hal_rtc.h](../../src/hal/rtc/hal_rtc.h), [hal_rtc.cpp](../../src/hal/rtc/hal_rtc.cpp), [providerzy](../../src/hal/rtc/), [rdzeń kalendarza](../../src/hal/time/) |
| RTC PCF8563 | Współdzielony backend I2C PCF8563. | [driver pcf8563](../../src/hal/rtc/pcf8563/) |
| RTC DS3231 | Współdzielony backend I2C DS3231. | [driver ds3231](../../src/hal/rtc/ds3231/) |
| Wewnętrzny RTC STM32G474 | Natywny kalendarz domeny backup z wyborem LSE/LSI, integralnością zachowanego czasu, IRQ/pollingiem Alarm A, jednorazowym wybudzeniem WUT, diagnostyką źródła i wyjściem kalibracyjnym 1 Hz. | [provider STM32G474](../../src/hal/impl/stm32g474/jh_stm32g474_rtc_provider.cpp) |
| RTC AON RP2040/RP2350 | Natywny provider Pico SDK AON wykorzystujący kalendarzowy RTC RP2040 lub timer Powman RP2350, z zachowaniem stanu po ciepłym resecie, względnymi alarmami wybudzenia i współdzieloną integracją RTC/NTP. | [provider RP](../../src/hal/impl/rp2040/jh_rp_rtc_provider.cpp) |
| GPS / NMEA | Jedna niezależna od targetu fasada GPS z wyborem HAL UART/SoftwareSerial podczas buildu, współdzielonym, chronionym muteksem silnikiem NMEA i deterministycznym wstrzykiwaniem mocka. RP UART zachowuje wytyczne dotyczące IRQ/przypisania rdzenia. | [hal_gps.h](../../src/hal/gps/hal_gps.h), [hal_gps.cpp](../../src/hal/gps/hal_gps.cpp), [framework GPS](../../src/hal/gps/) |
| Fasada termopary | Jedna niezależna od targetu fasada wybierająca provider, ze wspólnym cyklem życia, blokadami, walidacją, capabilities układu i deterministycznym wstrzykiwaniem mocka dla MCP9600/MCP9601 i MAX6675. | [hal_thermocouple.h](../../src/hal/temperature/hal_thermocouple.h), [fasada i providerzy](../../src/hal/temperature/) |
| MCP9600/MCP9601 | Współdzielony driver wzmacniacza termopary I2C. | [driver mcp9600](../../src/hal/temperature/mcp9600/) |
| MAX6675 | Współdzielony driver konwertera termopary bit-banged na GPIO. | [driver max6675](../../src/hal/temperature/max6675/) |
| DS18B20 | Współdzielone wsparcie cyfrowego czujnika temperatury 1-Wire. | [hal_ds18b20.h](../../src/hal/temperature/hal_ds18b20.h), [driver ds18b20](../../src/hal/temperature/ds18b20/) |
| DHT11/DHT22 | Współdzielony driver czujnika temperatury i wilgotności na GPIO. | [hal_dht.h](../../src/hal/temperature/hal_dht.h), [driver dht](../../src/hal/temperature/dht/) |
| Magistrala 1-Wire | Generyczny współdzielony wrapper/driver magistrali 1-Wire. | [hal_onewire.h](../../src/hal/onewire/hal_onewire.h), [driver onewire](../../src/hal/onewire/) |
| BH1750 | Współdzielony driver czujnika oświetlenia otoczenia I2C. | [hal_bh1750.h](../../src/hal/sensors/hal_bh1750.h), [driver bh1750](../../src/hal/sensors/bh1750/) |
| PMIC ADP5360 | Współdzielony driver PMIC I2C ze sterowaniem ładowaniem, odczytami fuel-gauge, funkcjami shipment/reset oraz konfiguracją regulatorów buck/buck-boost. | [hal_adp5360.h](../../src/hal/power/hal_adp5360.h), [driver adp5360](../../src/hal/power/adp5360/) |
| MCP3221 | Współdzielony driver 12-bitowego ADC I2C. | [hal_mcp3221.h](../../src/hal/analog/hal_mcp3221.h), [proste drivery I/O](../../src/hal/gpio/simple_io/) |
| ADS1X15 / ADS1115 | Współdzielony driver zewnętrznego ADC po I2C. | [hal_external_adc.h](../../src/hal/analog/hal_external_adc.h), [driver ads1x15](../../src/hal/analog/ads1x15/) |
| Dotyk TSC2007 | Współdzielony driver rezystancyjnego kontrolera dotyku I2C. | [hal_tsc2007.h](../../src/hal/input/hal_tsc2007.h), [driver tsc2007](../../src/hal/input/tsc2007/) |
| Dotyk STMPE610 | Współdzielony driver rezystancyjnego kontrolera dotyku I2C/SPI. | [hal_stmpe610.h](../../src/hal/input/hal_stmpe610.h), [driver stmpe610](../../src/hal/input/stmpe610/) |
| Dekodowanie odbiornika podczerwieni | Współdzielony dekoder odbiornika IR oparty na GPIO/pomiarze czasu. | [hal_irsmall_decoder.h](../../src/hal/input/hal_irsmall_decoder.h), [framework IR](../../src/hal/input/irsmall_decoder/) |

## Wyświetlacze, wskaźniki i urządzenia wyjściowe

| Obszar | Co oferuje | Źródło |
|---|---|---|
| Generyczna fasada wyświetlacza | Wspólne API rysowania i wyświetlania dla backendów TFT, RGB OLED i monochromatycznych, wraz z capabilities runtime, surowymi zapisami obszaru zwracającymi status, ścieżkami strumieniowania/DMA RGB565 i API tekstu tam, gdzie są zadeklarowane. | [hal_display.h](../../src/hal/display/hal_display.h) |
| Silnik GFX i fonty | Współdzielone prymitywy graficzne i dołączone fonty bitmapowe. | [drivery wyświetlacza](../../src/hal/display/drivers/) |
| TFT ILI9341 | Backend wyświetlacza TFT SPI. | [driver ili9341](../../src/hal/display/drivers/ili9341_driver.h) |
| TFT ST7735/ST7789/ST7796S/GC9A01 | Współdzielony backend TFT SPI rodziny ST77xx, wraz ze wsparciem inicjalizacji/rotacji okrągłego TFT GC9A01. | [driver st77xx](../../src/hal/display/drivers/st77xx_driver.h) |
| OLED rodziny SSD1306 | Backend OLED dla `SSD1306`, `SSD1309`, `SSD1315`, `SH1106` i `CH1115` przez HAL I2C/SPI. | [driver ssd1306](../../src/hal/display/drivers/ssd1306_driver.h) |
| RGB OLED SSD1331/SSD135x | Publiczny backend RGB565 `hal_display` przez HAL SPI/GPIO z surowymi zapisami, strumieniowaniem i prymitywami GFX; portowany z zachowania driverów wyświetlaczy Zephyr. | [hal_display.h](../../src/hal/display/hal_display.h) |
| LCD ST7567 | Publiczny surowy backend MONO01/MONO10 `hal_display` przez HAL I2C lub SPI/GPIO z możliwościami układu stron; portowany z zachowania driverów wyświetlaczy Zephyr. | [hal_display.h](../../src/hal/display/hal_display.h) |
| EPD SSD16xx / UC81xx | Współdzielone monochromatyczne drivery e-papieru SPI/GPIO dla SSD1608/SSD1673/SSD1675A/SSD1680/SSD1681 oraz UC8175/UC8176/UC8151D/UC8179, z timeoutami BUSY, profilami LUT pełnymi/częściowymi, odroczonym odświeżaniem klatki i surowymi możliwościami fasady MONO10. | [hal_display.h](../../src/hal/display/hal_display.h) |
| LCD HD44780 | Wsparcie równoległego znakowego LCD przez HAL GPIO/pomiar czasu. | [hal_hd44780.h](../../src/hal/display/hal_hd44780.h), [driver hd44780](../../src/hal/display/hd44780/) |
| Dioda statusowa RGB / NeoPixel | Współdzielone wsparcie diody RGB w stylu NeoPixel z transportem specyficznym dla targetu. | [hal_rgb_led.h](../../src/hal/gpio/hal_rgb_led.h), [driver neopixel](../../src/hal/gpio/neopixel/) |
| Fasada cyfrowego potencjometru | Wspólne API dla cyfrowych potencjometrów I2C. | [hal_digipot.h](../../src/hal/analog/hal_digipot.h) |
| MCP4017/4018/4019 | Współdzielony backend cyfrowego potencjometru I2C. | [drivery digipot](../../src/hal/analog/digipot/) |
| MAX5395 | Współdzielony backend cyfrowego potencjometru I2C. | [drivery digipot](../../src/hal/analog/digipot/) |
| Regulacja głośności audio PGA2311 | Współdzielony driver stereo kontrolera głośności SPI/GPIO. | [hal_pga2311.h](../../src/hal/audio/hal_pga2311.h), [driver pga2311](../../src/hal/audio/pga2311/) |
| MCP23017 / PCA9654E / PCF8574 / 74HC595 / MCP4725 | Współdzielone proste drivery ekspandera I/O i DAC przez HAL I2C/SPI/GPIO. | [proste drivery I/O](../../src/hal/gpio/simple_io/) |
| Audio PWM bez DAC (DACless) | Współdzielony pełnodupleksowy silnik audio PWM ze ścieżkami DMA i pollingu oraz callbackami blokowymi/próbkowymi. Backend RP dodaje swobodnie działający, karuzelowy przechwyt ADC przez DMA, więc callbacki per próbka odczytują wejścia mikrofonu/analogowe z częstotliwością audio w tej samej domenie zegarowej co odtwarzanie. | [hal_dacless.h](../../src/hal/audio/hal_dacless.h), [hal_dma_pwm_audio.h](../../src/hal/audio/hal_dma_pwm_audio.h), [driver dacless](../../src/hal/audio/dacless/) |

## Kryptografia, media i dołączone biblioteki

| Obszar | Co oferuje | Źródło |
|---|---|---|
| Funkcje kryptograficzne | Funkcje pomocnicze Base64, MD5, SHA-256, HMAC-SHA256 i zorientowane na ChaCha20/Poly1305. | [hal_crypto.h](../../src/hal/security/hal_crypto.h), [kryptografia wireguard](../../src/hal/network/wireguard/core/crypto/) |
| Sumy kontrolne CRC | Generyczne CRC-8/16/32 do integralności danych: CRC-8/MAXIM, Maxim 1-Wire CRC-16, CRC-16/CCITT-FALSE i CRC-32/ISO-HDLC. | [hal_crc.h](../../src/hal/security/hal_crc.h) |
| Funkcje pomocnicze uwierzytelniania sesji | Opcjonalne wsparcie uwierzytelniania sesji szeregowej. | [hal_sc_auth.h](../../src/hal/security/hal_sc_auth.h) |
| Narzędzia ramkowania/sesji szeregowej | Helpery wielokrotnego użytku dla ramek i sesji szeregowych w protokołach embedded oraz opcjonalny synchroniczny dispatch poleceń TEXT/JSON z formatterem odpowiedzi i hookami fallback. | [hal_serial_frame.h](../../src/hal/serial/hal_serial_frame.h), [hal_serial_session.h](../../src/hal/serial/hal_serial_session.h), [API poleceń](../api/pl/23_commands.md#adapter-ramkowanej-sesji-szeregowej-framed-serial-session) |
| cJSON | Zarządzany cJSON/cJSON_Utils do pracy z JSON w ograniczonym środowisku. | [API cJSON](../api/pl/17_cJSON.md) |
| PNG | Zarządzane wsparcie LodePNG zorientowane na pamięć plus opcjonalne funkcje pomocnicze Base64. | [API LodePNG](../api/pl/18_LodePNG.md) |
| JPEG | Zarządzane bazowe dekodowanie JPEG TJpgDec do RGB565 plus opcjonalne funkcje pomocnicze Base64. | [API JPEG](../api/pl/19_JPEG.md) |
| Unity | Zarządzany framework Unity 2.5.4 do testów po stronie hosta i targetu. | [Unity pin](../../third_party/unity_version.conf) |

## Przykłady i dokumentacja

| Obszar | Co oferuje | Źródło |
|---|---|---|
| Przenośne przykłady | Buildowalne aplikacje przykładowe obejmujące moduły rdzenia, sensorów, wyświetlaczy, łączności, pamięci masowej i mediów. | [examples](../../examples/) |
| Dokumentacja API | Szczegółowe gwarancje modułów, sygnatury i uwagi dotyczące backendów. | [doc/api](../api/pl/) |
| Workflow projektu firmware | Manifest, wybór targetu i płytki, wykrywanie źródeł, wybór providera CMake lub ESP-IDF, upload, monitor, IntelliSense oraz pliki generowane. | [FwProjectWorkflow.md](FwProjectWorkflow.md) |
| Natywny workflow OTA | Integracja firmware dla RP i ESP32-S3, artefakty specyficzne dla targetu, pierwsze wgranie, wgrywanie z VS Code, firewall, potwierdzenie, rollback i odzyskiwanie. | [OTAWorkflow.md](OTAWorkflow.md) |
