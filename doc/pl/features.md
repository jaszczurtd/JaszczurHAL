<a id="przegląd-funkcjonalności-jaszczurhal"></a>

# Możliwości JaszczurHAL

*Dostępne również [po angielsku](../en/features.md).*

JaszczurHAL udostępnia wspólne API C do tworzenia aplikacji wbudowanych na różnych platformach. Ten przegląd pokazuje dostępne funkcje i najważniejsze ograniczenia. Sygnatury, konfigurację i zasady użycia poszczególnych modułów opisuje [dokumentacja API](JaszczurHAL_API.md).

Sposób tworzenia i kompilowania projektów w VS Code znajdziesz w [FwProjectWorkflow.md](FwProjectWorkflow.md), a przygotowanie i obsługę aktualizacji OTA dla RP i ESP32-S3 - w [OTAWorkflow.md](OTAWorkflow.md).

Aby zachować przenośność, korzystaj z publicznego API JaszczurHAL zarówno w aplikacji, jak i we wspólnych sterownikach. Obsługę Pico SDK, ESP-IDF i rejestrów sprzętowych zapewnia implementacja dla wybranej platformy. Bezpośrednie wywołania Pico SDK lub ESP-IDF są możliwe, gdy potrzebujesz funkcji specyficznych dla danego układu, ale omijają HAL i wiążą tę część kodu z konkretną platformą.

<a id="przenośne-api-platformy-i-możliwości-kompilacji"></a>

## Platformy, konfiguracja i kompilacja

| Obszar | Co oferuje | Źródło |
|---|---|---|
| Wspólne API C | Aplikacje i przenośne sterowniki korzystają z tego samego publicznego API C `hal_*`. Wybór platformy określa implementację, a informacje o możliwościach płytki i zwracane statusy pozwalają uwzględnić różnice sprzętowe. | [publiczny HAL](../../src/hal/), [dokumentacja API](JaszczurHAL_API.md) |
| Profile płytek | Profile RP2040, RP2350, STM32G474, ESP32, ESP32-S3 i hosta są generowane z rejestru płytek. W obsługiwanych konfiguracjach aplikacja może odczytać informacje o płytce podczas działania. | [rejestr płytek](../../boards/README.pl.md), [hal_board.h](../../src/hal/system/hal_board.h) |
| RP2040 / RP2350 | Obsługa RP2040, RP2350 ARM i RP2350 Hazard3 RISC-V, z jawnym wyborem układu i architektury zestawu instrukcji (ISA) oraz opcjonalnym FreeRTOS. Implementacja i natywna kompilacja korzystają z oficjalnego Pico SDK; kod aplikacji pozostaje oparty na API JaszczurHAL. | [implementacja RP](../../src/hal/impl/rp2040/), [kompilacja natywna](../../link_libraries/rp_pico_lib/) |
| STM32G474 | Obsługa STM32G474 bez systemu operacyjnego (bare-metal) lub z FreeRTOS. Implementacja obejmuje kod startowy, skrypt linkera, skoordynowany dostęp do pamięci flash, wbudowane peryferia i opcjonalną łączność przez CYW43 podłączony do gSPI. | [implementacja STM32G474](../../src/hal/impl/stm32g474/) |
| Rodzina ESP32 | Wspólne API JaszczurHAL oparte na ESP-IDF. ESP32-S3 ma zaimplementowane moduły podstawowe, peryferia, usługi sieciowe, podstawową obsługę BLE przez NimBLE oraz OTA. Dla ESP32 i ESP32-S3 narzędzia projektowe generują konfigurację płytki i pamięci oraz integrują kompilację, wgrywanie, monitor i IntelliSense. | [implementacja ESP32](../../src/hal/impl/esp32/), [stanowisko kompilacji ESP32-S3](../../tests/fixtures/esp32s3_phase3/) |
| Testy bez sprzętu (mock) | Deterministyczna implementacja publicznego API JaszczurHAL do testów jednostkowych na komputerze i rozwijania przenośnego kodu bez urządzenia docelowego. | [implementacja mock](../../src/hal/impl/.mock/) |
| Moduły opcjonalne | Flagi `HAL_ENABLE_*` określają, które funkcje i ich zależności mają zostać dołączone podczas kompilacji. | [hal_config.h](../../src/hal/core/hal_config.h) |
| Obsługa kompilatorów | Wspólny nagłówek ujednolica rozszerzenia GNU, Clang i MSVC: operacje atomowe i porządek pamięci, `noreturn`, wymuszone `inline`, `trap`/`unreachable`, pakowanie struktur i zliczanie wiodących zer. | [hal_compiler.h](../../src/hal/core/hal_compiler.h) |
| Uruchamianie aplikacji | Wspólny model `app_start()` / `app_task0()` / opcjonalnego `app_task1()` na obsługiwanych platformach. HAL dostarcza `main()`, a ESP-IDF - `app_main()`. Na RP można dodatkowo włączyć uruchamianie kodu na rdzeniu 1. | [hal_app.h](../../src/hal/core/hal_app.h) |
| Integracja FreeRTOS | Obsługa muteksów, opóźnień i diagnostyki FreeRTOS oraz uruchamianie zadania aplikacji przez HAL. RP2040 i RP2350 korzystają z natywnych portów SMP, a STM32G474 z portu Cortex-M4F ustalonej wersji FreeRTOS-Kernel. Na ESP32-S3 scheduler jest dostarczany przez ustaloną wersję ESP-IDF. | [przenośny punkt wejścia aplikacji](../../src/hal/core/hal_app.h), [flagi modułów](../api/pl/02_module_flags.md) |
| Ochrona stosu | Niezależne opcje włączają synchroniczną ochronę granic stosu przez Pico SDK, MPU lub ESP-IDF oraz wartości kontrolne w ramkach funkcji, dodawane przez `-fstack-protector-strong` w GCC/Clang. Dostępność zależy od platformy. Konfiguracje FreeRTOS mogą dodatkowo sprawdzać granice stosów zadań. | [hal_system.h](../../src/hal/system/hal_system.h), [flagi modułów](../api/pl/02_module_flags.md) |
| Projekty VS Code | Tworzenie nowych projektów i migracja istniejących projektów do wspólnej obsługi VS Code/CMake. Generator i przykłady z repozytorium umożliwiają wybór platformy oraz płytki, z osobną pamięcią podręczną CMake dla każdej platformy. | [FwProjectWorkflow.md](FwProjectWorkflow.md) |
| Biblioteki statyczne | Jeden runner na rodzinę buildów tworzy `libJaszczurHAL.a` dla RP z oficjalnym Pico SDK, dla STM32G474 oraz dla ESP32 z ESP-IDF; jeden skrypt wejściowy wybiera runner z rejestru targetów. Kompilacja RP dodatkowo sprawdza tworzenie plików ELF/BIN/UF2 i symbole punktów wejścia aplikacji. | [runnery bibliotek](../../link_libraries/README.pl.md) |
| Zestaw kontroli projektu | Lokalne uruchamianie testów jednostkowych, Clang ASan/UBSan/libFuzzer, Valgrinda, analizy statycznej oraz kompilacji dla RP, STM32 i przykładów. Zestaw obejmuje także wielomodułową konfigurację ESP32-S3 sprawdzaną wyłącznie przez kompilację, nie na sprzęcie. | [runalltests.sh](../../runalltests.sh) |

<a id="rdzeń-hal"></a>

## Podstawowe funkcje HAL

| Obszar | Co oferuje | Źródło |
|---|---|---|
| GPIO | Odczyt i zapis stanów cyfrowych, konfiguracja podciągania i obsługa przerwań. Na wielordzeniowych RP i ESP32-S3 dostępne są jawne przypisanie obsługi IRQ do rdzenia i diagnostyka. | [hal_gpio.h](../../src/hal/gpio/hal_gpio.h) |
| ADC | Odczyt wejść analogowych przez wspólne API, które szereguje operacje dostępu. Na ESP32-S3 kanały jednorazowych pomiarów ADC wybiera się z generowanych masek pinów płytki. | [hal_adc.h](../../src/hal/analog/hal_adc.h) |
| DAC | Sterowanie sprzętowym wyjściem analogowym na STM32G474 oraz testowanie jego obsługi w implementacji mock, z dodatkową diagnostyką. Platformy bez sprzętowego DAC zwracają `HAL_EUNSUPPORTED`. | [hal_dac.h](../../src/hal/analog/hal_dac.h) |
| PWM | Generowanie sygnału PWM oraz funkcje pomocnicze do sterowania jego częstotliwością. | [hal_pwm.h](../../src/hal/gpio/hal_pwm.h), [hal_pwm_freq.h](../../src/hal/gpio/hal_pwm_freq.h) |
| Zliczanie impulsów | Zliczanie zboczy lub impulsów do pomiaru sygnałów i prostych liczników. | [hal_pcnt.h](../../src/hal/analog/hal_pcnt.h) |
| Sprzętowy pomiar okresów | Sprzętowe znaczniki czasu i okna 32 okresów na RP, STM32G474, ESP32-S3 oraz mocku. | [API](../api/pl/24_pulse_capture.md) |
| Timery i czas systemowy | Timery podstawowe i rozszerzone, obsługa bezczynności i opóźnień, watchdog oraz odczyt unikalnego identyfikatora urządzenia. Diagnostyka awarii korzysta z obsługi błędów właściwej dla platformy. | [hal_timer.h](../../src/hal/timers/hal_timer.h), [hal_system.h](../../src/hal/system/hal_system.h) |
| Tryby niskiego poboru mocy | Uśpienie CPU oraz tryby STOP0, STOP1 i Standby na STM32G474, zależnie od możliwości płytki. API obsługuje rozpoznawanie wybudzenia przez RTC, przywracanie zegara, funkcje zwrotne i kompensację czasu monotonicznego przy przejściach odmierzanych przez RTC. | [hal_power.h](../../src/hal/power/hal_power.h), [API zasilania](../api/pl/06_timers_system.md#halpower-przejścia-niskiego-poboru-mocy-opcjonalny-halenablepowermanagement) |
| Synchronizacja | Muteksy i sekcje krytyczne z implementacjami dostosowanymi do platformy. | [hal_sync.h](../../src/hal/system/hal_sync.h) |
| Timery programowe | Lekkie timery programowe obsługiwane kooperacyjnie przez aplikację. | [hal_soft_timer.h](../../src/hal/timers/hal_soft_timer.h) |
| Funkcje narzędziowe | Operacje na tablicach, liczbach, tekście, pikselach i obrazach, konwersja kolejności bajtów, przeliczenia ADC/NTC oraz funkcje sieciowe i czasowe. Dostępna jest również obsługa regulatora PID i watchdoga. | [Rdzeń HAL](../../src/hal/core/), [Moduły HAL](../../src/hal/), [Warstwa zgodności](../../src/utils/) |

## Komunikacja i łączność

| Obszar | Co oferuje | Źródło |
|---|---|---|
| UART | Sprzętowa komunikacja szeregowa przez wspólne API. Na RP2040 i ESP32-S3 operacje inicjalizacji i zakończenia pracy podlegają przypisaniu do rdzenia, który uruchomił UART. | [hal_uart.h](../../src/hal/serial/hal_uart.h) |
| Urządzenie USB / CDC | Uruchamianie i zatrzymywanie USB/CDC z informacją o wyniku operacji. Na RP HAL obsługuje TinyUSB, deskryptory, pracę w tle, kontrolę przeciążenia i reset 1200 bps do BOOTSEL. Dostępna jest także implementacja testowa na komputerze. | [hal_usb.h](../../src/hal/usb/hal_usb.h) |
| Konsola szeregowa i diagnostyka | Wyprowadzanie danych i logów przez USB CDC na RP, USB Serial/JTAG VFS w ESP-IDF lub USART2/stdout na STM32; transport jest wybierany podczas linkowania. Moduł szereguje TX, formatuje dane w kontekście zadania, odracza logi z ISR, ogranicza raportowanie błędów z poszczególnych źródeł i kopiuje wyjście do konsoli sieciowej. Testy mogą używać transportów przechwytujących dane lub je odrzucających. | [hal_serial.h](../../src/hal/serial/hal_serial.h), [API szeregowe](../api/pl/08_sync_serial.md) |
| Programowy port szeregowy | Komunikacja UART bez sprzętowego kontrolera UART. RP2040 korzysta z natywnego PIO/DMA Pico SDK, a pozostałe platformy ze wspólnej implementacji opartej na HAL GPIO. | [hal_swserial.h](../../src/hal/serial/hal_swserial.h) |
| Kontroler I2C | Obsługa dwóch magistral, operacji atomowych i przywracania magistrali po błędzie. Opcjonalne adresowanie 10-bitowe włącza `HAL_ENABLE_I2C_10BIT`. Skaner adresów 7-bitowych ma ograniczony zakres pracy i przyjmuje funkcję zwrotną do obsługi watchdoga lub zgłaszania postępu. | [hal_i2c.h](../../src/hal/i2c/hal_i2c.h) |
| Urządzenie podrzędne I2C | Praca w roli urządzenia podrzędnego I2C (target/slave), z udostępnianą mapą rejestrów. | [hal_i2c_slave.h](../../src/hal/i2c/hal_i2c_slave.h) |
| SPI | Obsługa kontrolera SPI i konfiguracji poszczególnych urządzeń: magistrali, sygnału CS i parametrów transmisji. Operacje zwracają status. Dostępny jest zapis blokujący i asynchroniczny, z DMA tam, gdzie obsługuje je implementacja. | [hal_spi.h](../../src/hal/spi/hal_spi.h), [hal_spi_device.h](../../src/hal/spi/hal_spi_device.h) |
| Polecenia i format wiadomości | Definiowanie i obsługa poleceń niezależnie od transportu. Router uwzględnia źródło żądania i reguły zabezpieczeń; obsługuje metadane binarne i odpowiedzi o ograniczonym rozmiarze. Wersjonowany format żądań, odpowiedzi i zdarzeń działa z adapterami pakietowymi i ramkowanymi strumieniami. | [API poleceń](../api/pl/23_commands.md), [hal_command_router.h](../../src/hal/commands/hal_command_router.h), [hal_command_wire.h](../../src/hal/commands/hal_command_wire.h) |
| Radio LoRa | Asynchroniczne nadawanie, odbiór i wykrywanie aktywności kanału (TX/RX/CAD), pomiar RSSI, anulowanie operacji, metadane pakietów, diagnostyka, stany zasilania i obliczanie czasu transmisji. API udostępnia informacje o obsługiwanych funkcjach i funkcje zwrotne; SX126x dodaje kalibrację zależną od pasma. SX1262 sprawdzono na sprzęcie. SX1261, SX1276 i SX1278 pozostają eksperymentalne i niezweryfikowane sprzętowo. | [API radia LoRa](../api/pl/21_lora.md), [hal_lora_radio.h](../../src/hal/radio/hal_lora_radio.h) |
| Łącze LoRa z potwierdzeniami | Wymiana adresowanych wiadomości w prywatnym protokole przez jeden uchwyt radia LoRa. Łącze obsługuje 32-bitowe numery sekwencyjne, potwierdzenia ACK, ograniczoną liczbę retransmisji, odrzucanie duplikatów, automatyczną fragmentację i CRC całej wiadomości. Opcjonalną ochronę zapewnia ChaCha20-Poly1305. | [API łącza LoRa](../api/pl/22_lora_link.md), [hal_lora_link.h](../../src/hal/radio/hal_lora_link.h) |
| Polecenia LoRa | Wysyłanie poleceń zdefiniowanych przez aplikację i automatyczne odsyłanie odpowiedzi przez łącze LoRa. Kolejki mają stałe limity i przechowują własne kopie danych. Procedury obsługi otrzymują także metadane łącza. | [API poleceń](../api/pl/23_commands.md), [hal_lora_commands.h](../../src/hal/radio/hal_lora_commands.h) |
| CAN i CAN FD | Wspólne API dla klasycznego CAN i CAN FD. Konfiguracja określa używaną implementację. | [hal_can.h](../../src/hal/can/hal_can.h) |
| MCP2515 CAN | Obsługa kontrolera CAN MCP2515 przez SPI, niezależnie od platformy docelowej. | [sterownik mcp2515](../../src/hal/can/mcp2515/) |
| MCP2517FD/MCP2518FD CAN FD | Obsługa kontrolerów CAN FD MCP2517FD i MCP2518FD przez SPI. | [sterownik mcp251xfd](../../src/hal/can/mcp251xfd/) |
| FDCAN w STM32G474 | Obsługa wbudowanego kontrolera FDCAN w STM32G474. | [implementacja STM32 FDCAN](../../src/hal/impl/stm32g474/hal_can_stm32g474_fdcan.cpp) |
| MFRC522 RFID | Obsługa czytnika RFID MFRC522 przez HAL SPI lub I2C. | [hal_mfrc522.h](../../src/hal/nfc/hal_mfrc522.h), [sterownik mfrc522](../../src/hal/nfc/mfrc522/) |
| PN532 NFC/RFID | Obsługa czytnika NFC/RFID PN532 przez HAL SPI, I2C lub UART. | [hal_pn532.h](../../src/hal/nfc/hal_pn532.h), [sterownik pn532](../../src/hal/nfc/pn532/) |
| WiFi | Łączność przez CYW43/lwIP na Pico W, Pico 2 W, Pico z modułem PIM730 oraz STM32G474 z PIM730. Na ESP32-S3 wykorzystywane są natywne WiFi, `esp_netif` i lwIP z ESP-IDF. | [hal_wifi.h](../../src/hal/network/hal_wifi.h) |
| BLE Peripheral i Observer | Jedno połączenie Peripheral, klasyczne rozgłaszanie BLE i skanowanie pasywne. Moduł kopiuje dane rozgłaszania i raporty skanowania, ogranicza kolejkę raportów, analizuje AD, udostępnia statyczne usługi GAP/GATT i pozwala odczytać ATT MTU. Dostępne są CYW43/BTstack, podstawowe BLE przez NimBLE na ESP32-S3 i deterministyczny mock. | [API Bluetooth](../api/pl/20_bluetooth.md) |
| Zarządzanie Bluetooth Classic | Wyszukiwanie urządzeń (inquiry), kopiowanie wyników, parowanie, SDP i dostęp do zapisanych urządzeń według indeksu, wspólne dla opcjonalnych profili Classic. HID Host udostępnia surowe deskryptory i raporty osobnym adapterom. Obsługiwane są CYW43/BTstack oraz Bluedroid na oryginalnym ESP32. | [flagi modułów](../api/pl/02_module_flags.md), [API Bluetooth](../api/pl/20_bluetooth.md) |
| Bluetooth Classic HID | Ogólna obsługa HID Host bez założeń dotyczących konkretnego urządzenia. API udostępnia kopię jednego deskryptora i surowe raporty Input/Output/Feature z określonymi limitami. | [hal_bluetooth_hid_host.h](../../src/hal/bluetooth/hal_bluetooth_hid_host.h) |
| Gamepad Bluetooth | Opcjonalny adapter Bluetooth Classic zwraca znormalizowany stan przycisków, osi i D-pada. Po rozłączeniu zeruje stan wejść. | [hal_gamepad.h](../../src/hal/bluetooth/hal_gamepad.h) |
| Bluetooth A2DP Sink i AVRCP Target | Odbiór jednego strumienia SBC i wyjście PCM w postaci 16-bitowych próbek ze znakiem, z opcjonalnym miksem mono. Obsługa obejmuje bufory o ograniczonej pojemności, zapamiętane powiązania Classic (bonding), diagnostykę strumienia i regulację głośności przez wartość bezwzględną. | [API Bluetooth](../api/pl/20_bluetooth.md#a2dp-sink-i-avrcp-target), [hal_bluetooth_a2dp_sink.h](../../src/hal/bluetooth/hal_bluetooth_a2dp_sink.h), [hal_bluetooth_avrcp_target.h](../../src/hal/bluetooth/hal_bluetooth_avrcp_target.h) |
| UDP | Przesyłanie datagramów przez wiele gniazd identyfikowanych uchwytami. Dla konfiguracji WiFi dostępny jest również adapter zgodności ze starszym API pojedynczego gniazda. | [hal_udp.h](../../src/hal/network/hal_udp.h) |
| Gniazda TCP | Połączenia klienckie oraz gniazda nasłuchujące i serwerowe, identyfikowane uchwytami. API obejmuje connect, bind/listen/accept, send/recv i shutdown. Dostępne są implementacje mock, CYW43/lwIP i natywna ESP-IDF lwIP. | [hal_tcp.h](../../src/hal/network/hal_tcp.h) |
| JH BLE Stream v1 | Przesyłanie strumienia bajtów przez jedną statyczną usługę GATT, z określonymi limitami zasobów. Protokół obsługuje wersjonowane ramki, negocjację możliwości, wzajemne uwierzytelnianie HMAC-SHA256, osobne klucze ChaCha20-Poly1305 dla kierunków transmisji, ochronę przed powtórzeniem, ograniczanie częstotliwości żądań i ograniczone kolejki RX/TX. | [API Bluetooth](../api/pl/20_bluetooth.md) |
| Polecenia BLE Stream | Wysyłanie poleceń i automatyczne odsyłanie odpowiedzi przez jedną uwierzytelnioną sesję JH BLE Stream, obsługiwaną wyłącznie przez adapter. Moduł dzieli i składa wiadomości zgodnie z MTU, przekazuje żądania do routera poleceń i udostępnia uwierzytelnione metadane drugiej strony oraz sesji. Sesja jest zamykana, gdy nie można bezpiecznie odtworzyć jej stanu. | [API poleceń](../api/pl/23_commands.md#uwierzytelniony-adapter-ble-stream), [hal_ble_commands.h](../../src/hal/bluetooth/hal_ble_commands.h) |
| Serwer HTTP | Serwer HTTP/1.1 obsługiwany przez regularne wywołania `poll()`. Udostępnia dopasowanie tras dokładne i prefiksowe, nagłówki żądań, automatyczny `Content-Length`, wznawianie częściowego zapisu TCP i limity czasu odpowiedzi oraz bezczynności. Połączenia nie są szyfrowane; API nie obsługuje serwera HTTPS. | [hal_http_server.h](../../src/hal/network/http/hal_http_server.h) |
| Pliki przez HTTP | Udostępnianie plików statycznych przez funkcje zwrotne, obsługa ETag/`If-None-Match` oraz odbiór plików w żądaniach PUT z surową treścią i w żądaniach `multipart`. Funkcje korzystają z tras serwera HAL HTTP. | [hal_http_files.h](../../src/hal/network/http/hal_http_files.h) |
| Serwer WebSocket | Serwer WebSocket przez HAL TCP, obsługiwany przez okresowe odpytywanie. Zapewnia negocjację HTTP Upgrade, funkcje zwrotne, wysyłanie i rozgłaszanie danych. Połączenia nie są szyfrowane; API nie obejmuje WSS ani klienta WebSocket. | [hal_websocket.h](../../src/hal/network/websocket/hal_websocket.h) |
| Konsola sieciowa | Konsola TCP chroniona hasłem, z dwukierunkową komunikacją do obsługi poleceń. Udostępnia uwierzytelnionym klientom wyjście `hal_serial` i diagnostykę, nie wyłączając lokalnych logów UART/USB. | [hal_net_console.h](../../src/hal/network/net_console/hal_net_console.h) |
| Polecenia sieciowe | Obsługa poleceń tekstowych i JSON przez HTTP i WebSocket. Adaptery korzystają z cJSON i wspólnego routera poleceń, zachowując istniejące API sieciowych procedur obsługi. | [API poleceń](../api/pl/23_commands.md), [hal_net_commands.h](../../src/hal/network/net_commands/hal_net_commands.h) |
| Zgodność z BSD sockets | Minimalna warstwa IPv4 udostępniająca `sys/socket.h`, `netinet/in.h`, `arpa/inet.h` i `netdb.h` przez uchwyty HAL UDP/TCP. Obejmuje `getaddrinfo()`, `setsockopt()`, `O_NONBLOCK`, `MSG_DONTWAIT` i sprawdzanie gotowości przez `select()`. | [socket.h](../../src/sys/socket.h), [netdb.h](../../src/netdb.h) |
| Klient TLS | Połączenia TLS przez BearSSL i natywne HAL TCP, ze wspólnym API niezależnym od implementacji. Konfiguracja obejmuje kotwice zaufania oraz funkcje zwrotne dostarczające czas i entropię. Dostępne są anulowanie, odpytywanie z limitami i opcjonalny adapter transportu BSD sockets. | [hal_tls.h](../../src/hal/network/tls/hal_tls.h), [transport BearSSL](../../src/hal/network/tls/BearSSL/) |
| Klient HTTP/HTTPS | Pojedyncze żądania HTTP/1.1 z określonymi limitami, przesyłane przez HAL TCP lub BearSSL TLS z weryfikacją. Aplikacja zapewnia bufory nagłówków i treści, a wynik zawiera jawne metadane odpowiedzi. | [hal_http_client.h](../../src/hal/network/http/hal_http_client.h), [API łączności](../api/pl/15_connectivity.md#halhttpclient-httphttps-client-opt-in-halenablehttpclient) |
| Powiadomienia | Wysyłanie powiadomień przez wspólne API. Dostępna implementacja Telegram Bot API korzysta z klienta HTTP/HTTPS. | [hal_notify.h](../../src/hal/network/notify/hal_notify.h), [API łączności](../api/pl/15_connectivity.md#halnotify-notifications-opt-in-halenablenotify) |
| MQTT | Łączność MQTT przez klienta opartego na PubSubClient. | [hal_mqtt.h](../../src/hal/network/mqtt/hal_mqtt.h) |
| Aktualizacje OTA | Aktualizacja firmware przez HAL UDP/TCP, z wykrywaniem urządzeń, rozruchem próbnym, potwierdzeniem obrazu, wycofaniem aktualizacji i wgrywaniem z VS Code. Opcjonalne uwierzytelnianie hasłem AUTH2 odrzuca aktualizację przy błędzie uwierzytelnienia. RP używa podpisanego, wersjonowanego kontenera i wznawialnej zamiany obrazu; ESP32-S3 - surowego obrazu aplikacji sprawdzanego względem manifestu i partycji OTA ESP-IDF. | [hal_ota.h](../../src/hal/network/ota/hal_ota.h), [proces OTA](OTAWorkflow.md) |
| Kalendarz, NTP i czas rzeczywisty | Zawsze dostępne obliczenia kalendarza gregoriańskiego oraz zegar czasu rzeczywistego, który mogą jednocześnie odczytywać różne zadania i rdzenie. Zegar udostępnia stan i źródło czasu, odtwarza czas z RTC, zachowuje dane NTP i integruje się z libc. W razie potrzeby przełącza się z głównego serwera NTP na jeden zapasowy, z określonymi limitami prób. | [hal_time.h](../../src/hal/time/hal_time.h) |
| WireGuard | Integracja WireGuard z host-lwIP, z kierowaniem części lub całości ruchu przez tunel. | [hal_wireguard.h](../../src/hal/network/wireguard/hal_wireguard.h), [silnik WireGuard](../../src/hal/network/wireguard/core/) |
| Modem komórkowy | Obsługa poleceń AT przez wspólny moduł oraz obsługa modemów rodziny SimCom A76xx. | [hal_modem_at.h](../../src/hal/modem/hal_modem_at.h), [hal_simcom_a76xx.h](../../src/hal/modem/hal_simcom_a76xx.h) |

<a id="pamięć-masowa-pliki-i-logowanie"></a>

## Pamięć masowa, pliki i logi

| Obszar | Co oferuje | Źródło |
|---|---|---|
| Dostęp do pamięci flash | Na RP operacje EEPROM/KV, LittleFS i przygotowania OTA są szeregowane przez wspólny koordynator. Na czas operacji zabezpiecza on drugi rdzeń, wstrzymuje TinyUSB, odrzuca funkcje zwrotne umieszczone w XIP i aktywne DMA, stosuje limity czasu, a następnie przywraca wcześniejszy stan. Na STM32G474 wspólny muteks szereguje kasowanie i programowanie flash dla EEPROM/KV i LittleFS. | [sterowniki pamięci flash RP](../../src/hal/impl/rp2040/drivers/flash/), [API pamięci masowej](../api/pl/14_storage.md) |
| Pamięć EEPROM | Wspólne API pamięci trwałej z kontrolą zakresów, blokadami i statusem `hal_status_t`. Obsługuje AT24C256, implementacje oparte na pamięci flash platformy oraz pamięć hosta do testów. | [hal_eeprom.h](../../src/hal/storage/hal_eeprom.h) |
| Pamięć klucz-wartość (K/V) | Trwałe przechowywanie par klucz-wartość w dwóch bankach pamięci o interfejsie typu EEPROM. Mechanizmy ochrony danych uwzględniają awarie. API get/set/commit zwraca `hal_status_t`. | [hal_kv.h](../../src/hal/storage/hal_kv.h) |
| LittleFS | Montowanie, odmontowanie i formatowanie systemu plików oraz odczyt informacji o zajętym miejscu przez wspólne API. Moduł synchronizuje dostęp między wątkami, sprawdza argumenty i stan montowania oraz obsługuje zgłaszanie postępu. RP i STM32G474 korzystają z wewnętrznej pamięci flash zarezerwowanej przez linker; mock pozwala ustawić wyniki operacji w testach. | [fasada i implementacja LittleFS](../../src/hal/storage/), [API pamięci masowej](../api/pl/14_storage.md) |
| FatFs / SD przez SPI | Obsługa kart SD przez SPI i systemu plików FatFs R0.16, pobieranego z dokładnie wskazanej rewizji. | [system plików](../../src/hal/storage/filesystem/) |
| Logi na karcie SD | Zapisywanie logów aplikacji i raportów awarii na karcie SD. | [hal_sdlogger.h](../../src/hal/storage/hal_sdlogger.h), [sdlogger](../../src/hal/storage/filesystem/sdlogger/) |

## Czujniki, urządzenia wejściowe i pomiar czasu

| Obszar | Co oferuje | Źródło |
|---|---|---|
| Zegar czasu rzeczywistego (RTC) | Odczyt i ustawianie czasu kalendarzowego lub czasu epoki oraz obsługa alarmów, timerów i wybudzania po zadanym czasie. Wspólne API zwraca `hal_status_t`, sprawdza poprawność dat gregoriańskich i zapewnia jednolite zasady inicjalizacji oraz blokad. Dostępne są implementacje sprzętowe i mock. | [hal_rtc.h](../../src/hal/rtc/hal_rtc.h), [hal_rtc.cpp](../../src/hal/rtc/hal_rtc.cpp), [implementacje](../../src/hal/rtc/), [rdzeń kalendarza](../../src/hal/time/) |
| RTC PCF8563 | Obsługa zegara PCF8563 przez I2C. | [sterownik pcf8563](../../src/hal/rtc/pcf8563/) |
| RTC DS3231 | Obsługa zegara DS3231 przez I2C. | [sterownik ds3231](../../src/hal/rtc/ds3231/) |
| Wewnętrzny RTC STM32G474 | Obsługa kalendarza w domenie podtrzymywanej, z wyborem LSE/LSI i kontrolą integralności zachowanego czasu. Dostępne są alarm A przez IRQ lub odpytywanie, jednorazowe wybudzenie WUT, diagnostyka źródła i wyjście kalibracyjne 1 Hz. | [implementacja STM32G474](../../src/hal/impl/stm32g474/jh_stm32g474_rtc_provider.cpp) |
| RTC AON RP2040/RP2350 | Obsługa czasu przez Pico SDK AON: kalendarzowy RTC w RP2040 lub timer Powman w RP2350. Stan jest zachowywany po miękkim resecie. Dostępne są względne alarmy wybudzenia i wspólna integracja RTC/NTP. | [implementacja RP](../../src/hal/impl/rp2040/jh_rp_rtc_provider.cpp) |
| GPS / NMEA | Odczyt pozycji, prędkości oraz daty i czasu z odbiornika GPS przesyłającego dane NMEA. Sprzętowy HAL UART lub programowy port szeregowy wybiera się podczas kompilacji. Parser NMEA jest chroniony muteksem, a mock umożliwia ustawienie danych testowych. Na RP obowiązują zasady przypisania UART i jego IRQ do rdzenia. | [hal_gps.h](../../src/hal/gps/hal_gps.h), [hal_gps.cpp](../../src/hal/gps/hal_gps.cpp), [obsługa GPS](../../src/hal/gps/) |
| Obsługa termopar | Odczyt temperatury z MCP9600/MCP9601 i MAX6675 przez wspólne API. Dostępne operacje zależą od układu; MAX6675 obsługuje wyłącznie termopary typu K. Moduł zapewnia jednolite zasady inicjalizacji, blokad i walidacji oraz deterministyczną implementację mock. | [hal_thermocouple.h](../../src/hal/temperature/hal_thermocouple.h), [fasada i implementacje](../../src/hal/temperature/) |
| MCP9600/MCP9601 | Obsługa wzmacniaczy termopar MCP9600/MCP9601 przez I2C. | [sterownik mcp9600](../../src/hal/temperature/mcp9600/) |
| MAX6675 | Obsługa konwertera termopary MAX6675 przez programowe sterowanie GPIO. | [sterownik max6675](../../src/hal/temperature/max6675/) |
| DS18B20 | Odczyt temperatury z cyfrowego czujnika DS18B20 przez 1-Wire. | [hal_ds18b20.h](../../src/hal/temperature/hal_ds18b20.h), [sterownik ds18b20](../../src/hal/temperature/ds18b20/) |
| DHT11/DHT22 | Odczyt temperatury i wilgotności z DHT11/DHT22 przez GPIO. | [hal_dht.h](../../src/hal/temperature/hal_dht.h), [sterownik dht](../../src/hal/temperature/dht/) |
| Magistrala 1-Wire | Wspólny sterownik do komunikacji z urządzeniami 1-Wire. | [hal_onewire.h](../../src/hal/onewire/hal_onewire.h), [sterownik onewire](../../src/hal/onewire/) |
| BH1750 | Pomiar natężenia oświetlenia czujnikiem BH1750 przez I2C. | [hal_bh1750.h](../../src/hal/sensors/hal_bh1750.h), [sterownik bh1750](../../src/hal/sensors/bh1750/) |
| Układ zasilania ADP5360 | Obsługa ładowania, poziomu naładowania baterii, trybu transportowego i resetu oraz konfiguracja przetwornic buck i buck-boost przez I2C. | [hal_adp5360.h](../../src/hal/power/hal_adp5360.h), [sterownik adp5360](../../src/hal/power/adp5360/) |
| MCP3221 | Odczyt z 12-bitowego przetwornika ADC MCP3221 przez I2C. | [hal_mcp3221.h](../../src/hal/analog/hal_mcp3221.h), [proste sterowniki I/O](../../src/hal/gpio/simple_io/) |
| ADS1X15 / ADS1115 | Obsługa zewnętrznych przetworników ADC ADS1X15/ADS1115 przez I2C. | [hal_external_adc.h](../../src/hal/analog/hal_external_adc.h), [sterownik ads1x15](../../src/hal/analog/ads1x15/) |
| Kontroler dotyku TSC2007 | Obsługa rezystancyjnego kontrolera dotyku TSC2007 przez I2C. | [hal_tsc2007.h](../../src/hal/input/hal_tsc2007.h), [sterownik tsc2007](../../src/hal/input/tsc2007/) |
| Kontroler dotyku STMPE610 | Obsługa rezystancyjnego kontrolera dotyku STMPE610 przez I2C lub SPI. | [hal_stmpe610.h](../../src/hal/input/hal_stmpe610.h), [sterownik stmpe610](../../src/hal/input/stmpe610/) |
| Odbiornik podczerwieni | Dekodowanie sygnału odbiornika IR na podstawie GPIO i pomiaru czasu. | [hal_irsmall_decoder.h](../../src/hal/input/hal_irsmall_decoder.h), [moduł IR](../../src/hal/input/irsmall_decoder/) |

## Wyświetlacze, wskaźniki i urządzenia wyjściowe

| Obszar | Co oferuje | Źródło |
|---|---|---|
| Obsługa wyświetlaczy | Rysowanie i wyświetlanie obrazu przez wspólne API dla TFT, RGB OLED i ekranów monochromatycznych. Aplikacja może sprawdzić obsługiwane formaty i operacje. Dostępne są zapis obszaru ze statusem, strumieniowanie, DMA RGB565 i funkcje tekstowe - zależnie od wybranej implementacji. | [hal_display.h](../../src/hal/display/hal_display.h) |
| Grafika GFX i czcionki | Podstawowe operacje rysowania oraz dołączone czcionki bitmapowe. | [sterowniki wyświetlacza](../../src/hal/display/drivers/) |
| TFT ILI9341 | Obsługa wyświetlacza TFT ILI9341 przez SPI. | [sterownik ili9341](../../src/hal/display/drivers/ili9341_driver.h) |
| TFT ST7735/ST7789/ST7796S/GC9A01 | Obsługa rodziny ST77xx przez SPI, w tym inicjalizacja i obracanie obrazu na okrągłym wyświetlaczu GC9A01. | [sterownik st77xx](../../src/hal/display/drivers/st77xx_driver.h) |
| OLED rodziny SSD1306 | Obsługa `SSD1306`, `SSD1309`, `SSD1315`, `SH1106` i `CH1115` przez HAL I2C lub SPI. | [sterownik ssd1306](../../src/hal/display/drivers/ssd1306_driver.h) |
| RGB OLED SSD1331/SSD135x | Wyświetlanie RGB565 przez `hal_display` i HAL SPI/GPIO, z bezpośrednim zapisem pikseli, strumieniowaniem i operacjami GFX. Obsługę przeniesiono ze sterowników wyświetlaczy Zephyr. | [hal_display.h](../../src/hal/display/hal_display.h) |
| LCD ST7567 | Obsługa formatów MONO01/MONO10 przez `hal_display`, z dostępem przez HAL I2C lub SPI/GPIO i uwzględnieniem stronicowej pamięci układu. Obsługę przeniesiono ze sterowników wyświetlaczy Zephyr. | [hal_display.h](../../src/hal/display/hal_display.h) |
| E-papier SSD16xx / UC81xx | Obsługa monochromatycznych ekranów SSD1608/SSD1673/SSD1675A/SSD1680/SSD1681 i UC8175/UC8176/UC8151D/UC8179 przez SPI/GPIO. Dostępne są limity oczekiwania na BUSY, profile LUT dla pełnego i częściowego odświeżania, odroczone odświeżanie ramki i bezpośredni format MONO10. | [hal_display.h](../../src/hal/display/hal_display.h) |
| LCD HD44780 | Obsługa równoległego wyświetlacza znakowego przez HAL GPIO i funkcje odmierzania czasu. | [hal_hd44780.h](../../src/hal/display/hal_hd44780.h), [sterownik hd44780](../../src/hal/display/hd44780/) |
| Dioda RGB / NeoPixel | Sterowanie diodą statusową RGB typu NeoPixel, z transmisją dostosowaną do platformy. | [hal_rgb_led.h](../../src/hal/gpio/hal_rgb_led.h), [sterownik neopixel](../../src/hal/gpio/neopixel/) |
| Potencjometry cyfrowe | Wspólne API sterowania potencjometrami cyfrowymi przez I2C. | [hal_digipot.h](../../src/hal/analog/hal_digipot.h) |
| MCP4017/4018/4019 | Sterowanie potencjometrami cyfrowymi MCP4017/4018/4019 przez I2C. | [sterowniki potencjometrów](../../src/hal/analog/digipot/) |
| MAX5395 | Sterowanie potencjometrem cyfrowym MAX5395 przez I2C. | [sterowniki potencjometrów](../../src/hal/analog/digipot/) |
| Regulacja głośności PGA2311 | Sterowanie stereofonicznym regulatorem głośności przez SPI/GPIO. | [hal_pga2311.h](../../src/hal/audio/hal_pga2311.h), [sterownik pga2311](../../src/hal/audio/pga2311/) |
| MCP23017 / PCA9654E / PCF8574 / 74HC595 / MCP4725 | Obsługa ekspanderów I/O i przetwornika DAC przez wspólne sterowniki oparte na HAL I2C/SPI/GPIO. | [proste sterowniki I/O](../../src/hal/gpio/simple_io/) |
| Audio PWM bez DAC (DACless) | Pełnodupleksowe audio PWM z DMA lub odpytywaniem oraz funkcjami zwrotnymi dla bloków i pojedynczych próbek. Na RP cykliczne próbkowanie ADC przez DMA umożliwia odczyt mikrofonu lub wejść analogowych z częstotliwością audio, w tej samej domenie zegarowej co odtwarzanie. ESP32 odmierza próbki przerwaniem timera zamiast DMA i odświeża wejścia ADC raz na bufor. | [hal_dacless.h](../../src/hal/audio/hal_dacless.h), [hal_dma_pwm_audio.h](../../src/hal/audio/hal_dma_pwm_audio.h), [sterownik dacless](../../src/hal/audio/dacless/) |

## Kryptografia, media i dołączone biblioteki

| Obszar | Co oferuje | Źródło |
|---|---|---|
| Kodowanie i kryptografia | Funkcje Base64, MD5, SHA-256 i HMAC-SHA256 oraz operacje ChaCha20/Poly1305. | [hal_crypto.h](../../src/hal/security/hal_crypto.h), [kryptografia WireGuard](../../src/hal/network/wireguard/core/crypto/) |
| Sumy kontrolne CRC | Obliczanie CRC-8/MAXIM, Maxim 1-Wire CRC-16, CRC-16/CCITT-FALSE i CRC-32/ISO-HDLC do sprawdzania integralności danych. | [hal_crc.h](../../src/hal/security/hal_crc.h) |
| Uwierzytelnianie sesji | Opcjonalne funkcje uwierzytelniania sesji szeregowej. | [hal_sc_auth.h](../../src/hal/security/hal_sc_auth.h) |
| Ramki i sesje szeregowe | Obsługa ramek i sesji w protokołach szeregowych. Opcjonalny adapter synchronicznie przekazuje polecenia TEXT/JSON do procedur obsługi i formatuje odpowiedzi. Zachowuje dotychczasowe funkcje zwrotne odpowiedzi i obsługi zapasowej. | [hal_serial_frame.h](../../src/hal/serial/hal_serial_frame.h), [hal_serial_session.h](../../src/hal/serial/hal_serial_session.h), [API poleceń](../api/pl/23_commands.md#adapter-ramkowanej-sesji-szeregowej-framed-serial-session) |
| cJSON | Biblioteki cJSON i cJSON_Utils do pracy z JSON w środowiskach o ograniczonych zasobach; ich wersją zarządza repozytorium. | [API cJSON](../api/pl/17_cJSON.md) |
| PNG | Biblioteka LodePNG skonfigurowana do pracy przy ograniczonej pamięci, z wersją zarządzaną przez repozytorium. Dostępne są opcjonalne funkcje Base64. | [API LodePNG](../api/pl/18_LodePNG.md) |
| JPEG | Dekodowanie podstawowego profilu JPEG do RGB565 przez TJpgDec, z wersją zarządzaną przez repozytorium i opcjonalnymi funkcjami Base64. | [API JPEG](../api/pl/19_JPEG.md) |
| Unity | Framework Unity 2.5.4 do testów na komputerze i urządzeniu docelowym, z wersją zarządzaną przez repozytorium. | [plik wersji Unity](../../third_party/unity_version.conf) |

## Przykłady i dokumentacja

| Obszar | Co oferuje | Źródło |
|---|---|---|
| Przenośne przykłady | Gotowe do kompilacji aplikacje pokazujące podstawowe funkcje HAL, czujniki, wyświetlacze, łączność, pamięć masową i obsługę mediów. | [examples](../../examples/) |
| Dokumentacja API | Sygnatury funkcji, zasady użycia modułów i różnice między implementacjami. | [doc/api](../api/pl/) |
| Praca z projektem firmware | Instrukcje konfiguracji manifestu, wyboru platformy i płytki, wykrywania źródeł, kompilowania przez CMake lub ESP-IDF, wgrywania i monitorowania. Obejmują również IntelliSense i pliki generowane. | [FwProjectWorkflow.md](FwProjectWorkflow.md) |
| Obsługa aktualizacji OTA | Instrukcje integracji OTA na RP i ESP32-S3: pliki wynikowe, pierwsze wgranie, aktualizacja z VS Code, konfiguracja zapory, potwierdzanie obrazu, wycofanie aktualizacji i odzyskiwanie urządzenia. | [OTAWorkflow.md](OTAWorkflow.md) |
