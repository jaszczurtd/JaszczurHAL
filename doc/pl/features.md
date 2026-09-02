# Przegląd funkcjonalności JaszczurHAL

*Dostępne również [po angielsku](../en/features.md).*

Ten dokument przedstawia ogólny zakres możliwości JaszczurHAL. Jest zwięzłym
przeglądem funkcji, a nie dokumentacją API. Sygnatury funkcji, szczegóły
konfiguracji i zachowanie modułów znajdziesz w
[JaszczurHAL_API.md](JaszczurHAL_API.md).
Model projektu firmware w VS Code z wyborem targetu opisuje
[FwProjectWorkflow.md](FwProjectWorkflow.md), a przygotowanie i obsługę OTA dla
natywnego RP i ESP32-S3 - [OTAWorkflow.md](OTAWorkflow.md).

Kod aplikacji i wspólne sterowniki powinny korzystać z publicznego API
JaszczurHAL. Szczegóły Pico SDK, ESP-IDF i rejestrów targetu pozostają częścią
wybranej implementacji; nie stanowią alternatywnego API aplikacji. Aplikacje
wciąż mogą wywoływać API Pico SDK lub
ESP-IDF bezpośrednio, gdy wymagane jest zachowanie specyficzne dla targetu,
ale takie wywołania omijają abstrakcję HAL, wiążą dany fragment kodu z jedną
platformą i działają wbrew celowi przenośności JaszczurHAL.

## Przenośne API, platformy i możliwości kompilacji

| Obszar | Co oferuje | Źródło |
|---|---|---|
| Jedno przenośne API JaszczurHAL | Kod aplikacji i sterowniki ogólnego przeznaczenia wywołują to samo publiczne API C `hal_*` na wszystkich obsługiwanych targetach. Wybór targetu wskazuje właściwą implementację. Możliwości płytki i zwracane statusy opisują różnice sprzętowe bez zmiany modelu programowania aplikacji. | [publiczny HAL](../../src/hal/), [dokumentacja API](JaszczurHAL_API.md) |
| Profile płytek i informacje dostępne w czasie działania | Profile RP2040, RP2350, STM32G474, ESP32, ESP32-S3 i hosta są generowane z rejestru płytek. Obsługiwane konfiguracje udostępniają wspólne API do odczytu informacji o płytce w czasie działania. | [rejestr płytek](../../boards/README.pl.md), [hal_board.h](../../src/hal/system/hal_board.h) |
| Implementacja RP2040 / RP2350 | Udostępnia wspólne API JaszczurHAL dla RP2040, RP2350 ARM i RP2350 Hazard3 RISC-V, z jednoznacznym wyborem układu i ISA oraz opcjonalnym FreeRTOS. Wewnętrznie wykorzystuje oficjalny Pico SDK do niskopoziomowej obsługi platformy i natywnej kompilacji; przenośny kod aplikacji nadal wywołuje JaszczurHAL. | [implementacja RP](../../src/hal/impl/rp2040/), [kompilacja natywna](../../rp_native_lib/) |
| Implementacja STM32G474 | Udostępnia wspólne API JaszczurHAL dla STM32G474 w wariantach bare-metal i FreeRTOS. Kod właściwy dla targetu obsługuje uruchamianie systemu, skrypt linkera, skoordynowany dostęp do pamięci flash, natywne peryferia oraz opcjonalną łączność CYW43 przez gSPI. | [implementacja STM32G474](../../src/hal/impl/stm32g474/) |
| Implementacja rodziny ESP32 | Udostępnia wspólne API JaszczurHAL oparte na ESP-IDF. ESP32-S3 obejmuje obecnie zaimplementowany zestaw modułów podstawowych, peryferiów, usług sieciowych, bazowej obsługi BLE przez NimBLE oraz OTA. Narzędzia projektowe generują konfigurację płytki i pamięci, a także obsługują kompilację, wgrywanie, monitor i IntelliSense dla obu targetów. | [implementacja ESP32](../../src/hal/impl/esp32/), [stanowisko kompilacji ESP32-S3](../../tests/fixtures/esp32s3_phase3/) |
| Implementacja testowa (mock) | Udostępnia to samo publiczne API JaszczurHAL w deterministycznej postaci przeznaczonej do testów jednostkowych na hoście i rozwijania przenośnego API bez sprzętu. | [implementacja mock](../../src/hal/impl/.mock/) |
| Moduły opcjonalne włączane podczas kompilacji | Funkcje opcjonalne są wybierane flagami `HAL_ENABLE_*`; kompilacja dołącza wyłącznie wymagane przez nie zależności. | [hal_config.h](../../src/hal/core/hal_config.h) |
| Warstwa przenośności kompilatora | Jeden nagłówek ujednolica rozszerzenia kompilatora wymagane przez HAL: `noreturn`, wymuszone `inline`, `trap`/`unreachable`, pakowanie struktur i zliczanie wiodących zer. Obsługuje GNU, Clang i MSVC. | [hal_compiler.h](../../src/hal/core/hal_compiler.h) |
| Główny punkt startu aplikacji | Wspólny model `app_start()` / `app_task0()` / opcjonalnego `app_task1()` na obsługiwanych targetach i w przykładach. HAL dostarcza `main()`, ESP-IDF - `app_main()`. | [hal_app.h](../../src/hal/core/hal_app.h) |
| Integracja FreeRTOS | Na RP2040, RP2350 i STM32G474 używana jest ustalona wersja oryginalnego FreeRTOS-Kernel z natywnymi portami SMP dla RP oraz portem Cortex-M4F dla STM32G474. ESP32-S3 korzysta z kernela dostarczanego przez ESP-IDF w ustalonej wersji. Integracja obejmuje muteksy, opóźnienia i diagnostykę uwzględniającą FreeRTOS, a HAL odpowiada za uruchomienie zadania aplikacji. | [przenośny punkt wejścia aplikacji](../../src/hal/core/hal_app.h), [flagi modułów](../api/pl/02_module_flags.md) |
| Ochrona stosu | Niezależne opcje włączają synchroniczne sprawdzanie granic stosu przez Pico SDK, MPU lub ESP-IDF oraz "kanarki" w ramkach funkcji (ang. canary values), dodawane przez opcję `-fstack-protector-strong` w GCC/Clang. Konfiguracje FreeRTOS mogą dodatkowo wykrywać przepełnienie stosów zadań. | [hal_system.h](../../src/hal/system/hal_system.h), [flagi modułów](../api/pl/02_module_flags.md) |
| Generator szablonów projektów dla VS Code | Generator pustych szablonów dla nowych projektów, lub migrowanych projektów korzystających z biblioteki i przykładów zapisanych w repozytorium, opartych o VS Code i CMake. Obejmuje wybór targetu i płytki oraz osobny cache CMake dla każdego targetu. | [FwProjectWorkflow.md](FwProjectWorkflow.md) |
| Biblioteki statyczne | Dedykowane konfiguracje CMake i skrypty pomocnicze dla oficjalnych kompilacji RP na Pico SDK oraz STM32G474; ścieżka RP dodatkowo weryfikuje generowanie ELF/BIN/UF2 i symbole wejścia (startu) aplikacji. | [kompilacja RP](../../rp_native_lib/), [kompilacja STM32](../../stm32_lib/) |
| Bramka walidacji | Pełny lokalny zestaw kontroli obejmujący testy jednostkowe, Clang ASan/UBSan/libFuzzer, Valgrind, analizę statyczną, kompilacje dla targetów RP/STM, wielomodułową konfigurację ESP32-S3 z ESP-IDF sprawdzaną wyłącznie przez kompilację oraz przykłady. | [runalltests.sh](../../runalltests.sh) |

## Rdzeń HAL

| Obszar | Co oferuje | Źródło |
|---|---|---|
| GPIO | Przenośne cyfrowe I/O, tryby pull i przerwania, wraz z jawnym przypisaniem rdzenia obsługującego IRQ i diagnostyką na wielordzeniowych targetach RP i ESP32-S3. | [hal_gpio.h](../../src/hal/gpio/hal_gpio.h) |
| ADC | Przenośny dostęp do wejść analogowych z serializacją operacji, wraz z kanałami jednorazowych pomiarów ADC ESP32-S3 wybieranymi z generowanych masek pinów płytki. | [hal_adc.h](../../src/hal/analog/hal_adc.h) |
| DAC | Sprzętowe wyjście DAC z dodatkową diagnostyką na STM32G474 i w implementacji mock hosta; targety bez sprzętowego DAC zwracają `HAL_EUNSUPPORTED`. | [hal_dac.h](../../src/hal/analog/hal_dac.h) |
| PWM | Przenośne wyjście PWM oraz pomocnicze funkcje PWM ze sterowaniem częstotliwością. | [hal_pwm.h](../../src/hal/gpio/hal_pwm.h), [hal_pwm_freq.h](../../src/hal/gpio/hal_pwm_freq.h) |
| Zliczanie impulsów | Zliczanie zboczy/impulsów do pomiaru sygnału i prostych zastosowań licznikowych. | [hal_pcnt.h](../../src/hal/analog/hal_pcnt.h) |
| Timery i czas systemowy | Podstawowe timery, ich rozszerzone funkcje, obsługa bezczynności i opóźnień, watchdog, unikalny identyfikator urządzenia oraz diagnostyka awarii właściwa dla targetu. | [hal_timer.h](../../src/hal/timers/hal_timer.h), [hal_system.h](../../src/hal/system/hal_system.h) |
| Zarządzanie niskim poborem mocy | Dostępność poszczególnych trybów zależy od możliwości płytki. API obejmuje uśpienie CPU, tryby STOP0, STOP1 i Standby STM32G474, rozpoznawanie wybudzenia przez RTC, przywracanie zegara, funkcje zwrotne oraz kompensację czasu monotonicznego przy przejściach synchronizowanych z RTC. | [hal_power.h](../../src/hal/power/hal_power.h), [API zasilania](../api/pl/06_timers_system.md#halpower-przejścia-niskiego-poboru-mocy-opcjonalny-halenablepowermanagement) |
| Synchronizacja | Muteksy i sekcje krytyczne z implementacjami specyficznymi dla targetu. | [hal_sync.h](../../src/hal/system/hal_sync.h) |
| Timery programowe | Lekkie kooperacyjne timery programowe. | [hal_soft_timer.h](../../src/hal/timers/hal_soft_timer.h) |
| Prymitywy narzędziowe | Pomocnicze funkcje bitowe i matematyczne, sterowanie PID, obsługa watchdoga oraz ogólne API narzędziowe. | [tools.h](../../src/tools.h), [utils](../../src/utils/) |

## Komunikacja i łączność

| Obszar | Co oferuje | Źródło |
|---|---|---|
| UART | Abstrakcja sprzętowej komunikacji szeregowej. Na RP2040 i ESP32-S3 operacje cyklu życia są przypisane do rdzenia, który uruchomił UART. | [hal_uart.h](../../src/hal/serial/hal_uart.h) |
| Urządzenie USB / CDC | API cyklu życia USB i CDC zwracające status. Na RP HAL zarządza TinyUSB, deskryptorami, obsługą w tle, kontrolą przeciążenia i resetem 1200 bps do BOOTSEL; dostępna jest też implementacja testowa na hoście. | [hal_usb.h](../../src/hal/usb/hal_usb.h) |
| Konsola szeregowa i diagnostyczna | Jeden wspólny moduł szereguje transmisję TX, formatuje dane strumieniowo w kontekście zadania, odracza logi z ISR, ogranicza liczbę błędów dla każdego źródła i kopiuje wyjście do konsoli sieciowej. Podczas linkowania wybierany jest transport: USB CDC na RP, USB Serial/JTAG VFS w ESP-IDF, STM32 USART2/stdout albo testowe porty przechwytujące i odbiorcze. | [hal_serial.h](../../src/hal/serial/hal_serial.h), [API szeregowe](../api/pl/08_sync_serial.md) |
| Programowy port szeregowy | Programowa obsługa UART zoptymalizowana dla targetu: natywne PIO/DMA Pico SDK na RP2040 i wspólna implementacja oparta na HAL GPIO na pozostałych targetach. | [hal_swserial.h](../../src/hal/serial/hal_swserial.h) |
| Kontroler I2C | Przenośne API kontrolera I2C z obsługą dwóch magistral, operacjami atomowymi, odzyskiwaniem magistrali, opcjonalnym adresowaniem 10-bitowym (`HAL_ENABLE_I2C_10BIT`) i ograniczonym skanerem 7-bitowym, który przyjmuje funkcję zwrotną watchdoga lub postępu. | [hal_i2c.h](../../src/hal/i2c/hal_i2c.h) |
| Target I2C | Obsługa I2C w roli targetu z mapą rejestrów. | [hal_i2c_slave.h](../../src/hal/i2c/hal_i2c_slave.h) |
| SPI | Przenośne API mastera/kontrolera SPI plus neutralne względem targetu deskryptory magistrali, CS i ustawień dla poszczególnych urządzeń, wraz z API transferu zwracającym status oraz blokującymi i asynchronicznymi ścieżkami zapisu z obsługą DMA tam, gdzie jest dostępne. | [hal_spi.h](../../src/hal/spi/hal_spi.h), [hal_spi_device.h](../../src/hal/spi/hal_spi_device.h) |
| Router poleceń i binarny przesył wiadomości | Niezależny od transportu przesył poleceń zdefiniowanych przez użytkownika, ze zdefiniowanymi regułami dotyczącymi źródła i zabezpieczeń, metadanymi bezpiecznymi dla danych binarnych, odpowiedziami o ograniczonym rozmiarze oraz wersjonowanymi komunikatami żądania, odpowiedzi i zdarzenia dla adapterów pakietowych i strumieni ramkowanych. | [API poleceń](../api/pl/23_commands.md), [hal_command_router.h](../../src/hal/commands/hal_command_router.h), [hal_command_wire.h](../../src/hal/commands/hal_command_wire.h) |
| Niskopoziomowe API radia LoRa | Jedno API cyklu życia, niezależne od implementacji, obsługuje zweryfikowany sprzęt SX1262 oraz eksperymentalne integracje SX1261, SX1276 i SX1278, których nie sprawdzono jeszcze na fizycznym sprzęcie. Obejmuje asynchroniczne TX/RX/CAD, bieżące RSSI, obsługiwane funkcje, funkcje zwrotne, anulowanie operacji, metadane pakietów, diagnostykę, stany zasilania i czas transmisji. SX126x udostępnia ponadto kalibrację zależną od pasma. | [API radia LoRa](../api/pl/21_lora.md), [hal_lora_radio.h](../../src/hal/radio/hal_lora_radio.h) |
| "Niezawodne" łącze LoRa | Prywatne wiadomości z adresowaniem, przesyłane przez jeden uchwyt niskopoziomowego API LoRa. Łącze używa 32-bitowych numerów sekwencyjnych, ACK i ograniczonej liczby retransmisji, tłumi duplikaty, automatycznie fragmentuje dane, sprawdza CRC całej wiadomości i może chronić ją za pomocą ChaCha20-Poly1305. | [API łącza LoRa](../api/pl/22_lora_link.md), [hal_lora_link.h](../../src/hal/radio/hal_lora_link.h) |
| Polecenia LoRa | Żądania poleceń, automatycznie wysyłane odpowiedzi i nazwane zdarzenia przez niezawodne łącze LoRa pozostające pod wyłączną kontrolą adaptera. Kolejki mają ograniczoną pojemność i przechowują własne kopie danych, a metadane łącza są przekazywane do procedur obsługi. | [API poleceń](../api/pl/23_commands.md), [hal_lora_commands.h](../../src/hal/radio/hal_lora_commands.h) |
| Fasada CAN | Wspólne API wybierające implementację klasycznego CAN lub CAN FD. | [hal_can.h](../../src/hal/can/hal_can.h) |
| MCP2515 CAN | Współdzielona implementacja CAN przez SPI. | [sterownik mcp2515](../../src/hal/can/mcp2515/) |
| MCP2517FD/MCP2518FD CAN FD | Współdzielona implementacja CAN FD przez SPI. | [sterownik mcp251xfd](../../src/hal/can/mcp251xfd/) |
| Natywny FDCAN STM32G474 | Natywna implementacja FDCAN dla STM32G474. | [implementacja STM32 FDCAN](../../src/hal/impl/stm32g474/hal_can_stm32g474_fdcan.cpp) |
| MFRC522 RFID | Wspólny sterownik czytnika RFID przez HAL SPI/I2C. | [hal_mfrc522.h](../../src/hal/nfc/hal_mfrc522.h), [sterownik mfrc522](../../src/hal/nfc/mfrc522/) |
| PN532 NFC/RFID | Wspólny sterownik czytnika NFC/RFID przez HAL SPI/I2C/UART. | [hal_pn532.h](../../src/hal/nfc/hal_pn532.h), [sterownik pn532](../../src/hal/nfc/pn532/) |
| WiFi | Łączność CYW43/lwIP na Pico W, Pico 2 W, Pico+PIM730 (moduł WiFi z CYW43) oraz skonfigurowanym sprzęcie STM32G474+PIM730, plus natywne WiFi/`esp_netif`/lwIP z ESP-IDF na ESP32-S3. | [hal_wifi.h](../../src/hal/network/hal_wifi.h) |
| BLE Peripheral i Observer | Jedno połączenie w roli Peripheral, klasyczne rozgłaszanie BLE z kopiowaniem danych, pasywne skanowanie z ograniczoną kolejką kopii raportów i analizą AD, statyczne usługi GAP/GATT oraz odczyt ATT MTU. Dostępne są implementacje CYW43/BTstack, podstawowa obsługa BLE przez NimBLE na ESP32-S3 i deterministyczna implementacja testowa na hoście. | [API Bluetooth](../api/pl/20_bluetooth.md) |
| Gamepad Bluetooth Classic HID | Jeden nieblokujący profil hosta z parowaniem ograniczonym czasowo i ponownym łączeniem ze znanym urządzeniem w ramach bieżącego uruchomienia. API zwraca znormalizowane struktury stanu przycisków, osi i D-pada, jawnie sygnalizuje przepełnienie oraz zeruje wejścia po rozłączeniu. Dostępne są implementacje CYW43/BTstack, Bluedroid na oryginalnym ESP32 i deterministyczna implementacja testowa na hoście. | [hal_gamepad.h](../../src/hal/bluetooth/hal_gamepad.h), [API Bluetooth](../api/pl/20_bluetooth.md#gamepad-bluetooth-classic-hid) |
| UDP | Transport UDP oparty na uchwytach, obsługujący wiele gniazd, oraz adapter zgodności wstecznej dla pojedynczego gniazda w konfiguracjach WiFi. | [hal_udp.h](../../src/hal/network/hal_udp.h) |
| Gniazda TCP | Klienckie gniazda TCP oparte na uchwytach oraz uchwyty nasłuchujące i serwerowe z operacjami connect, bind/listen/accept, send/recv i shutdown. Dostępne są implementacje testowa, CYW43/lwIP i natywna ESP-IDF lwIP. | [hal_tcp.h](../../src/hal/network/hal_tcp.h) |
| JH BLE Stream v1 | Usługa strumieniowa BLE ogólnego przeznaczenia o określonym/ograniczonym rozmiarze, przenoszona przez jedną statyczną usługę GATT. Zapewnia wersjonowane ramkowanie z negocjacją właściwości, wzajemne potwierdzanie tożsamości przez HMAC-SHA256, kierunkowe klucze ChaCha20-Poly1305, ochronę przed powtórzeniem, ograniczanie częstotliwości żądań oraz kolejki RX/TX o ustalonej pojemności. | [API Bluetooth](../api/pl/20_bluetooth.md) |
| Polecenia BLE Stream | Fragmentacja i ponowne składanie binarnych komunikatów poleceń z uwzględnieniem MTU, automatyczne kierowanie żądań do routera i odsyłanie odpowiedzi, zdefiniowane przez użytkownika zdarzenia, uwierzytelnione metadane drugiej strony i sesji oraz zamknięcie sesji, gdy nie można bezpiecznie odtworzyć jej stanu. Adapter przejmuje wyłączną obsługę jednej uwierzytelnionej sesji JH BLE Stream. | [API poleceń](../api/pl/23_commands.md#uwierzytelniony-adapter-ble-stream), [hal_ble_commands.h](../../src/hal/bluetooth/hal_ble_commands.h) |
| Serwer HTTP | Mały serwer HTTP/1.1 działający bez szyfrowania przez HAL TCP i obsługiwany przez okresowe wywoływanie funkcji głównej (pooling). Udostępnia trasy dokładne i prefiksowe, nagłówki żądania, buforowane odpowiedzi, automatyczny `Content-Length` oraz obsługę żądań możliwą do testowania w implementacji mock. API serwera nie obsługuje HTTPS. | [hal_http_server.h](../../src/hal/network/http/hal_http_server.h) |
| Pliki HTTP | Obsługa plików statycznych przez funkcje zwrotne (callbacki), ETag/`If-None-Match` oraz funkcje pomocnicze do żądań PUT z surową treścią i przesyłania plików `multipart` przez trasy HAL HTTP. | [hal_http_files.h](../../src/hal/network/http/hal_http_files.h) |
| Serwer WebSocket | Mały serwer WebSocket działający bez szyfrowania przez HAL TCP i obsługiwany przez okresowe odpytywanie. Obsługuje negocjację HTTP Upgrade, funkcje zwrotne, pomocnicze funkcje wysyłania i rozgłaszanie. Nie zdefiniowano API WSS ani klienta WebSocket. | [hal_websocket.h](../../src/hal/network/websocket/hal_websocket.h) |
| Konsola sieciowa | Chroniona hasłem konsola TCP, która przekazuje wyjście `hal_serial` i diagnostykę do uwierzytelnionych klientów, zachowując przy tym lokalne logi UART/USB, oraz zapewnia dwukierunkowe wejście poleceń. | [hal_net_console.h](../../src/hal/network/net_console/hal_net_console.h) |
| Polecenia sieciowe | Adaptery tekstowe i JSON oparte na cJSON dla kanałów sterujących HTTP i WebSocket, korzystające ze wspólnego routera poleceń i zachowujące ustalone API sieciowych procedur obsługi. | [API poleceń](../api/pl/23_commands.md), [hal_net_commands.h](../../src/hal/network/net_commands/hal_net_commands.h) |
| Adapter BSD sockets | Minimalna warstwa zgodności IPv4 `sys/socket.h` / `netinet/in.h` / `arpa/inet.h` / `netdb.h` przez uchwyty HAL UDP/TCP, wraz z `getaddrinfo()`, `setsockopt()`, `O_NONBLOCK`, `MSG_DONTWAIT` i gotowością `select()`. | [socket.h](../../src/sys/socket.h), [netdb.h](../../src/netdb.h) |
| TLS | Niezależne od implementacji API klienta TLS oparte na BearSSL i natywnym HAL TCP, z kotwicami zaufania, funkcjami zwrotnymi czasu i entropii, anulowaniem, ograniczonym odpytywaniem oraz opcjonalnym mostkiem do transportu BSD sockets. | [hal_tls.h](../../src/hal/network/tls/hal_tls.h), [transport BearSSL](../../src/hal/network/tls/BearSSL/) |
| Klient HTTP/HTTPS | Jednorazowe żądania HTTP/1.1 ze ścisłymi limitami, przesyłane przez HAL TCP lub zweryfikowany BearSSL TLS. Bufory nagłówków i ciała należą do wywołującego, a odpowiedź zawiera jawne metadane. | [hal_http_client.h](../../src/hal/network/http/hal_http_client.h), [API łączności](../api/pl/15_connectivity.md#halhttpclient-httphttps-client-opt-in-halenablehttpclient) |
| Powiadomienia | Wspólne API przekazuje powiadomienia do wybranej implementacji. Zaimplementowana jest obsługa Telegram Bot API, która korzysta ze wspomnianego wyżej klienta HTTP/HTTPS. | [hal_notify.h](../../src/hal/network/notify/hal_notify.h), [API łączności](../api/pl/15_connectivity.md#halnotify-notifications-opt-in-halenablenotify) |
| MQTT | Klient MQTT oparty na PubSubClient. | [hal_mqtt.h](../../src/hal/network/mqtt/hal_mqtt.h) |
| OTA | Natywne aktualizacje firmware przez HAL UDP/TCP z wykrywaniem urządzeń i opcjonalnym uwierzytelnianiem hasłem AUTH2, które przy błędzie nie dopuszcza aktualizacji firmware. Dostępne są: rozruch próbny, potwierdzenie obrazu, wycofanie aktualizacji i wgrywanie z VS Code. RP używa podpisanego, wersjonowanego kontenera i wznawianego procesu zamiany, a ESP32-S3 - surowego obrazu aplikacji sprawdzonego względem manifestu oraz partycji OTA ESP-IDF. | [hal_ota.h](../../src/hal/network/ota/hal_ota.h), [proces OTA](OTAWorkflow.md) |
| Kalendarz / NTP / czas dnia | Funkcje pomocnicze kalendarza gregoriańskiego są zawsze dostępne. Jeden bezpieczny wątkowo zegar systemowy działający w runtime udostępnia informacje o źródle i stanie, odtwarza czas z RTC, przechowuje stan NTP, integruje się z libc oraz w razie potrzeby przechodzi z podstawowego serwera NTP na jeden zapasowy. | [hal_time.h](../../src/hal/time/hal_time.h) |
| WireGuard | Wspólna integracja WireGuard z host-lwIP, obsługująca kierowanie ruchu przez tunel częściowy lub pełny. | [hal_wireguard.h](../../src/hal/network/wireguard/hal_wireguard.h), [silnik WireGuard](../../src/hal/network/wireguard/core/) |
| Modem komórkowy (LTE) | Ogólny silnik modemu obsługujący polecenia AT oraz rodzinę SimCom A76xx. | [hal_modem_at.h](../../src/hal/modem/hal_modem_at.h), [hal_simcom_a76xx.h](../../src/hal/modem/hal_simcom_a76xx.h) |

## Pamięć masowa, pliki i logowanie

| Obszar | Co oferuje | Źródło |
|---|---|---|
| Koordynacja zmian w pamięci flash | Na RP operacje EEPROM/KV, LittleFS i przygotowywanie obrazu OTA przechodzą przez jeden koordynator. Szereguje on wywołania, doprowadza drugi rdzeń do stanu bezpiecznego dla operacji na pamięci flash, wstrzymuje TinyUSB, odrzuca funkcje zwrotne wykonywane z XIP i aktywne transfery DMA, stosuje ograniczone czasy oczekiwania, a następnie przywraca wcześniejszy stan systemu. Na STM32G474 jeden muteks pamięci flash szereguje sekwencje kasowania i programowania dla EEPROM/KV oraz LittleFS. | [sterowniki pamięci flash RP](../../src/hal/impl/rp2040/drivers/flash/), [API pamięci masowej](../api/pl/14_storage.md) |
| Abstrakcja EEPROM | Jedna fasada trwałej pamięci wybierająca implementację, ze wspólną obsługą blokad i zakresów, przenośnym sterownikiem AT24C256, implementacjami pamięci flash targetu i pamięci hosta oraz API zwracającym status (`hal_status_t`). | [hal_eeprom.h](../../src/hal/storage/hal_eeprom.h) |
| Pamięć klucz-wartość (K/V) | Trwała warstwa dwóch banków oparta na storage typu EEPROM, wraz z mechanizmami ochrony danych w przypadku awarii. Udostępnia API get/set/commit zwracające status (`hal_status_t`). | [hal_kv.h](../../src/hal/storage/hal_kv.h) |
| LittleFS | Jedna bezpieczna wątkowo i niezależna od targetu fasada odpowiada za cykl życia, walidację, stan montowania, zapytania o rozmiar oraz konfigurację funkcji zwrotnej postępu dla wspólnej implementacji littlefs. Natywne implementacje RP i STM32G474 korzystają z partycji wewnętrznej pamięci flash zarezerwowanych przez linker. W testach wynik działania można ustawić przez funkcje implementacji mock. | [fasada i implementacja LittleFS](../../src/hal/storage/), [API pamięci masowej](../api/pl/14_storage.md) |
| FatFs / SD po SPI | Kopia FatFs R0.16 oparta na dokładnie wskazanej rewizji i wspólna obsługa karty SD przez SPI. | [system plików](../../src/hal/storage/filesystem/) |
| SD logger | Zapisywanie zwykłych logów i raportów awarii na karcie SD. | [hal_sdlogger.h](../../src/hal/storage/hal_sdlogger.h), [sdlogger](../../src/hal/storage/filesystem/sdlogger/) |

## Czujniki, urządzenia wejściowe i pomiar czasu

| Obszar | Co oferuje | Źródło |
|---|---|---|
| Fasada RTC | Jedna niezależna od targetu fasada zegara czasu rzeczywistego, wybierająca implementację sprzętową lub mock. Udostępnia API czasu kalendarzowego, epoki, alarmu, timera i względnego wybudzania zwracające `hal_status_t`, wspólną obsługę cyklu życia i blokad oraz walidację kalendarza gregoriańskiego. | [hal_rtc.h](../../src/hal/rtc/hal_rtc.h), [hal_rtc.cpp](../../src/hal/rtc/hal_rtc.cpp), [implementacje](../../src/hal/rtc/), [rdzeń kalendarza](../../src/hal/time/) |
| RTC PCF8563 | Współdzielona implementacja PCF8563 przez I2C. | [sterownik pcf8563](../../src/hal/rtc/pcf8563/) |
| RTC DS3231 | Współdzielona implementacja DS3231 przez I2C. | [sterownik ds3231](../../src/hal/rtc/ds3231/) |
| Wewnętrzny RTC STM32G474 | Natywny kalendarz domeny podtrzymywanej z wyborem LSE/LSI, kontrolą integralności zachowanego czasu, obsługą alarmu A przez przerwanie lub odpytywanie, jednorazowym wybudzeniem WUT, diagnostyką źródła i wyjściem kalibracyjnym 1 Hz. | [implementacja STM32G474](../../src/hal/impl/stm32g474/jh_stm32g474_rtc_provider.cpp) |
| RTC AON RP2040/RP2350 | Natywna implementacja Pico SDK AON wykorzystująca kalendarzowy RTC RP2040 lub timer Powman RP2350, z zachowaniem stanu po miękkim resecie, względnymi alarmami wybudzenia i wspólną integracją RTC/NTP. | [implementacja RP](../../src/hal/impl/rp2040/jh_rp_rtc_provider.cpp) |
| GPS / NMEA | Jedna niezależna od targetu fasada GPS wybiera podczas kompilacji transport HAL UART lub programowy port szeregowy. Wspólny silnik NMEA jest chroniony muteksem, a implementacja mock pozwala deterministycznie ustawiać dane testowe. UART na RP zachowuje zasady dotyczące IRQ i przypisania rdzenia. | [hal_gps.h](../../src/hal/gps/hal_gps.h), [hal_gps.cpp](../../src/hal/gps/hal_gps.cpp), [obsługa GPS](../../src/hal/gps/) |
| Fasada termopary | Jedna niezależna od targetu fasada wybiera implementację MCP9600/MCP9601 lub MAX6675. Zarządza wspólnym cyklem życia, blokadami i walidacją, informuje o funkcjach obsługiwanych przez układ oraz pozwala deterministycznie ustawiać stan implementacji mock. | [hal_thermocouple.h](../../src/hal/temperature/hal_thermocouple.h), [fasada i implementacje](../../src/hal/temperature/) |
| MCP9600/MCP9601 | Współdzielony sterownik wzmacniacza termopary przez I2C. | [sterownik mcp9600](../../src/hal/temperature/mcp9600/) |
| MAX6675 | Współdzielony sterownik konwertera termopary z programową obsługą GPIO. | [sterownik max6675](../../src/hal/temperature/max6675/) |
| DS18B20 | Współdzielona obsługa cyfrowego czujnika temperatury 1-Wire. | [hal_ds18b20.h](../../src/hal/temperature/hal_ds18b20.h), [sterownik ds18b20](../../src/hal/temperature/ds18b20/) |
| DHT11/DHT22 | Współdzielony sterownik czujnika temperatury i wilgotności przez GPIO. | [hal_dht.h](../../src/hal/temperature/hal_dht.h), [sterownik dht](../../src/hal/temperature/dht/) |
| Magistrala 1-Wire | Współdzielony sterownik magistrali 1-Wire. | [hal_onewire.h](../../src/hal/onewire/hal_onewire.h), [sterownik onewire](../../src/hal/onewire/) |
| BH1750 | Współdzielony sterownik czujnika natężenia oświetlenia przez I2C. | [hal_bh1750.h](../../src/hal/sensors/hal_bh1750.h), [sterownik bh1750](../../src/hal/sensors/bh1750/) |
| PMIC ADP5360 | Współdzielony sterownik PMIC przez I2C, obsługujący ładowanie, pomiar poziomu baterii, tryb transportowy, reset oraz konfigurację przetwornic buck i buck-boost. | [hal_adp5360.h](../../src/hal/power/hal_adp5360.h), [sterownik adp5360](../../src/hal/power/adp5360/) |
| MCP3221 | Współdzielony sterownik 12-bitowego ADC przez I2C. | [hal_mcp3221.h](../../src/hal/analog/hal_mcp3221.h), [proste sterowniki I/O](../../src/hal/gpio/simple_io/) |
| ADS1X15 / ADS1115 | Współdzielony sterownik zewnętrznego ADC przez I2C. | [hal_external_adc.h](../../src/hal/analog/hal_external_adc.h), [sterownik ads1x15](../../src/hal/analog/ads1x15/) |
| Dotyk TSC2007 | Współdzielony sterownik rezystancyjnego kontrolera dotyku przez I2C. | [hal_tsc2007.h](../../src/hal/input/hal_tsc2007.h), [sterownik tsc2007](../../src/hal/input/tsc2007/) |
| Dotyk STMPE610 | Współdzielony sterownik rezystancyjnego kontrolera dotyku przez I2C/SPI. | [hal_stmpe610.h](../../src/hal/input/hal_stmpe610.h), [sterownik stmpe610](../../src/hal/input/stmpe610/) |
| Dekodowanie odbiornika podczerwieni | Współdzielony dekoder odbiornika IR oparty na GPIO i pomiarze czasu. | [hal_irsmall_decoder.h](../../src/hal/input/hal_irsmall_decoder.h), [moduł IR](../../src/hal/input/irsmall_decoder/) |

## Wyświetlacze, wskaźniki i urządzenia wyjściowe

| Obszar | Co oferuje | Źródło |
|---|---|---|
| Ogólna fasada wyświetlacza | Wspólne API rysowania i wyświetlania dla implementacji TFT, RGB OLED i monochromatycznych. Informuje o formatach i operacjach obsługiwanych przez aktywną implementację, udostępnia bezpośredni zapis obszaru zwracający status, strumieniowanie i DMA RGB565 oraz API tekstowe tam, gdzie jest obsługiwane przez dany sprzęt. | [hal_display.h](../../src/hal/display/hal_display.h) |
| Silnik GFX i czcionki | Wspólne prymitywy graficzne i dołączone czcionki bitmapowe. | [sterowniki wyświetlacza](../../src/hal/display/drivers/) |
| TFT ILI9341 | Obsługa wyświetlacza TFT przez SPI. | [sterownik ili9341](../../src/hal/display/drivers/ili9341_driver.h) |
| TFT ST7735/ST7789/ST7796S/GC9A01 | Wspólna obsługa rodziny ST77xx przez SPI, wraz z inicjalizacją i obracaniem obrazu na okrągłym TFT GC9A01. | [sterownik st77xx](../../src/hal/display/drivers/st77xx_driver.h) |
| OLED rodziny SSD1306 | Obsługa `SSD1306`, `SSD1309`, `SSD1315`, `SH1106` i `CH1115` przez HAL I2C/SPI. | [sterownik ssd1306](../../src/hal/display/drivers/ssd1306_driver.h) |
| RGB OLED SSD1331/SSD135x | Publiczna implementacja RGB565 `hal_display` przez HAL SPI/GPIO z bezpośrednim zapisem pikseli, strumieniowaniem i prymitywami GFX. Jej zachowanie przeniesiono ze sterowników wyświetlaczy Zephyr. | [hal_display.h](../../src/hal/display/hal_display.h) |
| LCD ST7567 | Publiczna, niskopoziomowa implementacja MONO01/MONO10 `hal_display` przez HAL I2C lub SPI/GPIO, uwzględniająca stronicową organizację pamięci układu. Jej zachowanie przeniesiono ze sterowników wyświetlaczy Zephyr. | [hal_display.h](../../src/hal/display/hal_display.h) |
| EPD SSD16xx / UC81xx | Wspólne monochromatyczne sterowniki wyświetlaczy e-papierowych SPI/GPIO dla SSD1608/SSD1673/SSD1675A/SSD1680/SSD1681 oraz UC8175/UC8176/UC8151D/UC8179. Obsługują ograniczony czas oczekiwania na BUSY, pełne i częściowe profile LUT, odroczone odświeżanie ramki oraz bezpośredni format MONO10 fasady. | [hal_display.h](../../src/hal/display/hal_display.h) |
| LCD HD44780 | Obsługa równoległego znakowego LCD przez HAL GPIO/timing. | [hal_hd44780.h](../../src/hal/display/hal_hd44780.h), [sterownik hd44780](../../src/hal/display/hd44780/) |
| Dioda statusowa RGB / NeoPixel | Wspólna obsługa diody RGB w stylu NeoPixel z transportem specyficznym dla targetu. | [hal_rgb_led.h](../../src/hal/gpio/hal_rgb_led.h), [sterownik neopixel](../../src/hal/gpio/neopixel/) |
| Fasada cyfrowego potencjometru | Wspólne API dla cyfrowych potencjometrów I2C. | [hal_digipot.h](../../src/hal/analog/hal_digipot.h) |
| MCP4017/4018/4019 | Wspólna obsługa cyfrowego potencjometru przez I2C. | [sterowniki potencjometrów](../../src/hal/analog/digipot/) |
| MAX5395 | Wspólna obsługa cyfrowego potencjometru przez I2C. | [sterowniki potencjometrów](../../src/hal/analog/digipot/) |
| Regulacja głośności audio PGA2311 | Wspólny sterownik stereofonicznego kontrolera głośności przez SPI/GPIO. | [hal_pga2311.h](../../src/hal/audio/hal_pga2311.h), [sterownik pga2311](../../src/hal/audio/pga2311/) |
| MCP23017 / PCA9654E / PCF8574 / 74HC595 / MCP4725 | Wspólne proste sterowniki ekspanderów I/O i DAC przez HAL I2C/SPI/GPIO. | [proste sterowniki I/O](../../src/hal/gpio/simple_io/) |
| Audio PWM bez DAC (DACless) | Wspólny pełnodupleksowy silnik audio PWM z obsługą DMA lub poolingu oraz funkcjami zwrotnymi dla bloków danych i próbek. Implementacja RP dodaje ciągłe, cykliczne próbkowanie kanałów ADC przez DMA, dzięki czemu callbacki dla poszczególnych próbek mogą odczytywać wejścia mikrofonowe lub analogowe z częstotliwością audio i w tej samej domenie zegarowej co odtwarzanie. | [hal_dacless.h](../../src/hal/audio/hal_dacless.h), [hal_dma_pwm_audio.h](../../src/hal/audio/hal_dma_pwm_audio.h), [sterownik dacless](../../src/hal/audio/dacless/) |

## Kryptografia, media i dołączone biblioteki

| Obszar | Co oferuje | Źródło |
|---|---|---|
| Funkcje kryptograficzne | Funkcje pomocnicze Base64, MD5, SHA-256 i HMAC-SHA256 oraz operacje ChaCha20/Poly1305. | [hal_crypto.h](../../src/hal/security/hal_crypto.h), [kryptografia WireGuard](../../src/hal/network/wireguard/core/crypto/) |
| Sumy kontrolne CRC | Ogólne funkcje CRC-8/16/32 do sprawdzania integralności danych: CRC-8/MAXIM, Maxim 1-Wire CRC-16, CRC-16/CCITT-FALSE i CRC-32/ISO-HDLC. | [hal_crc.h](../../src/hal/security/hal_crc.h) |
| Funkcje pomocnicze uwierzytelniania sesji | Opcjonalne wsparcie uwierzytelniania sesji szeregowej. | [hal_sc_auth.h](../../src/hal/security/hal_sc_auth.h) |
| Narzędzia ramkowania i sesji szeregowej | Wspólne funkcje do obsługi ramek i sesji szeregowych w protokołach embedded. Opcjonalny adapter synchronicznie przekazuje polecenia TEXT/JSON odpowiednim procedurom obsługi, formatuje odpowiedzi i zachowuje dotychczasowe funkcje zwrotne odpowiedzi oraz obsługi zapasowej. | [hal_serial_frame.h](../../src/hal/serial/hal_serial_frame.h), [hal_serial_session.h](../../src/hal/serial/hal_serial_session.h), [API poleceń](../api/pl/23_commands.md#adapter-ramkowanej-sesji-szeregowej-framed-serial-session) |
| cJSON | Zarządzany cJSON/cJSON_Utils do pracy z JSON w środowiskach o ograniczonych zasobach. | [API cJSON](../api/pl/17_cJSON.md) |
| PNG | Zarządzana wersja LodePNG przeznaczona do pracy przy ograniczonej pamięci oraz opcjonalne funkcje pomocnicze Base64. | [API LodePNG](../api/pl/18_LodePNG.md) |
| JPEG | Zarządzane dekodowanie podstawowego profilu JPEG przez TJpgDec do RGB565 oraz opcjonalne funkcje pomocnicze Base64. | [API JPEG](../api/pl/19_JPEG.md) |
| Unity | Zarządzany framework Unity 2.5.4 do testów po stronie hosta i targetu. | [plik wersji Unity](../../third_party/unity_version.conf) |

## Przykłady i dokumentacja

| Obszar | Co oferuje | Źródło |
|---|---|---|
| Przenośne przykłady | Aplikacje przykładowe obejmujące moduły rdzenia, czujników, wyświetlaczy, łączności, pamięci masowej i mediów, gotowe do kompilacji. | [examples](../../examples/) |
| Dokumentacja API | Szczegółowe gwarancje modułów, sygnatury i uwagi dotyczące poszczególnych implementacji. | [doc/api](../api/pl/) |
| Praca z projektem firmware | Manifest, wybór targetu i płytki, wykrywanie źródeł, wybór mechanizmu kompilacji CMake lub ESP-IDF, wgrywanie, monitorowanie, IntelliSense oraz pliki generowane. | [FwProjectWorkflow.md](FwProjectWorkflow.md) |
| Natywne aktualizacje OTA | Integracja firmware dla RP i ESP32-S3, artefakty specyficzne dla targetu, pierwsze wgranie, wgrywanie z VS Code, zapora sieciowa, potwierdzenie obrazu, wycofanie aktualizacji i odzyskiwanie. | [OTAWorkflow.md](OTAWorkflow.md) |
