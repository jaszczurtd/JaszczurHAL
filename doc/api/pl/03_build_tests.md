# Zależności buildu, testy i stanowiska testowe sprzętu

*Dostępne również [po angielsku](../en/03_build_tests.md).*

> **Część [Dokumentacji API JaszczurHAL](../../pl/JaszczurHAL_API.md)**

## Zależności (build sprzętowy)

| Moduł HAL | Zależność zewnętrzna |
|---|---|
| Model komponentowy ESP32-S3 | Wersja ESP-IDF wskazana przez repozytorium, z jednym generowanym grafem źródeł i zależności: podstawowy kod systemu i prostego PWM, opcjonalnie wybierane peryferia fazy 2 oraz natywna łączność i usługi fazy 3. |
| `hal_gpio`, `hal_pwm`, `hal_adc`, `hal_system` | API `hardware_*` / `pico_*` Pico SDK w rodzinie RP; backend rejestrowy STM32G474; usługi GPIO, LEDC PWM, ADC i systemowe ESP-IDF dla ESP32-S3. `hal_system` używa też API zadań FreeRTOS w obsługiwanych buildach `HAL_ENABLE_FREERTOS`. |
| `hal_usb` | Urządzenie TinyUSB zarządzane przez HAL na RP: deskryptory CDC, obsługa IRQ i timera w buildach bare-metal, zadanie robocze na rdzeniu 0 w buildach FreeRTOS oraz reset do trybu BOOTSEL. STM32G474 nie jest obecnie obsługiwany. Mock udostępnia deterministyczne bufory CDC i pozwala wykryć reset. |
| `hal_serial` | Jedna niezależna od targetu implementacja portu szeregowego i debugowania oraz porty dołączane podczas linkowania: RP CDC `hal_usb`, VFS USB Serial/JTAG uruchamiany podczas startu ESP32-S3, debugowy USART2 lub stdout hosta na STM32G474 oraz przechwytywanie stdout i dane RX ustawiane przez testy w mocku. |
| `hal_sync` | RP: `pico/mutex.h` z Pico SDK w buildach bare i semafory FreeRTOS w buildach RTOS. STM32G474: atomowa spinlock w buildach bare i muteksy FreeRTOS w buildach RTOS. ESP32-S3: muteksy FreeRTOS z ESP-IDF oraz sekcje krytyczne `portMUX_TYPE`. |
| `hal_timer` | RP2040: API alarmów/czasu Pico SDK (`pico/time.h`); STM32G474: backend rejestrowy TIM6 + NVIC; ESP32-S3: domyślny GPTimer ESP-IDF oraz dedykowane pule. |
| `hal_soft_timer` | wewnętrzne narzędzie `SmartTimers` |
| `hal_pid_controller` | wewnętrzne narzędzie `pidController` |
| `hal_can` | ogólna fasada CAN plus drivery CAN wybierane przez backend: MCP2515 (`hal/can/mcp2515/*`), MCP251XFD (`hal/can/mcp251xfd/*`) oraz natywny FDCAN STM32G474 (`impl/stm32g474/hal_can_stm32g474_fdcan.*`) |
| `hal_display` | Współdzielony stos wyświetlacza (`hal/display/drivers/hal_display.cpp`, `jh_gfx.*`, `ili9341_driver.*`, `st77xx_driver.*`, `ssd1306_driver.*`) używany przez RP2040 i STM32G474; backendy targetowe dostarczają transport SPI/I2C/GPIO |
| `hal_hd44780` | współdzielony driver znakowego LCD kompatybilnego z HD44780 (`hal/display/hd44780/hd44780.*`) nad HAL GPIO/timingiem systemowym |
| `hal_dma_pwm_audio` | pomocnik DMA audio-PWM taktowany timerem, używany przez DACless na RP2040, STM32G474 i mocku |
| `hal_dacless` | współdzielony silnik audio-PWM DACless (`hal/audio/dacless/dacless.*`) nad HAL DMA/PWM-freq, ADC, timingiem i synchronizacją |
| `hal_tsc2007` | współdzielony driver rezystancyjnego kontrolera dotyku TSC2007 (`hal/input/tsc2007/tsc2007.cpp`) nad HAL I2C/timingiem systemowym |
| `hal_stmpe610` | współdzielony driver rezystancyjnego kontrolera dotyku STMPE610 (`hal/input/stmpe610/stmpe610.cpp`) nad HAL I2C lub HAL SPI/GPIO |
| `hal_irsmall_decoder` | współdzielony dekoder odbiornika IR (`hal/input/irsmall_decoder/irsmall_decoder.cpp`) nad przerwaniami HAL GPIO i timingiem systemowym |
| `hal_spi` | natywny `hardware/spi.h` Pico SDK dla RP2040; backend rejestrowy STM32G474; ESP-IDF SPI master na SPI2/SPI3 dla ESP32-S3. |
| `hal_lora_radio` | Wzajemnie wykluczające się backendy rodzin układów: oficjalny driver Semtech SX126x w wersji wskazanej przez repozytorium, z adapterem HAL dla zwalidowanego SX1262 i eksperymentalnego SX1261, sprawdzonego wyłącznie programowo, albo własny backend rejestrowy HAL dla eksperymentalnych SX1276/SX1278, również sprawdzonych wyłącznie programowo. Oba kompilują się dla RP i STM32G474 i są deterministycznie testowane z użyciem mocka. |
| `hal_lora_link` | Własny protokół HAL nad jednym skonfigurowanym `hal_lora_radio`; CRC-32 jest wewnętrzne, ChaCha20-Poly1305 używa opcjonalnego modułu `hal_crypto`, i nie wprowadza się żadnej dodatkowej zależności zewnętrznej |
| `hal_i2c` | natywny `hardware/i2c.h` Pico SDK dla RP2040; backend rejestrowy STM32G474; ESP-IDF I2C master na I2C0/I2C1 dla ESP32-S3. |
| `hal_swserial` | natywny backend PIO/DMA Pico SDK na RP2040; współdzielony backend HAL GPIO/timing/sync na pozostałych targetach |
| `hal_gps` | jedna przenośna fasada wybierająca `hal_uart` / `hal_swserial` podczas buildu, plus współdzielony wbudowany silnik NMEA |
| `hal_rgb_led` | współdzielony rdzeń NeoPixel (`hal/gpio/neopixel/jh_neopixel.*`) + implementacja transportu targetowego, w tym RMT na ESP32-S3 |
| `hal_thermocouple` (MCP9600/MCP9601) | współdzielony driver (`hal/temperature/mcp9600/mcp9600_driver.*`) |
| `hal_thermocouple` (MAX6675) | współdzielony driver (`hal/temperature/max6675/max6675_driver.*`) |
| `hal_onewire` | współdzielony driver bit-bang (`hal/onewire/onewire_driver.*`) nad HAL GPIO/czasem |
| `hal_ds18b20` | współdzielony backend DS18B20 (`hal/temperature/ds18b20/hal_ds18b20.cpp`) nad współdzielonym OneWire |
| `hal_external_adc` | współdzielony driver ADS1X15/ADS1115 (`hal/analog/ads1x15/ads1x15_driver.*`) |
| `hal_pga2311` | współdzielony driver stereo-głośności PGA2311 (`hal/audio/pga2311/pga2311_driver.*`) nad HAL SPI/GPIO |
| `hal_wifi` | driver CYW43/lwIP w wersji wskazanej przez repozytorium na RP i STM32G474 lub natywne WiFi ESP-IDF/`esp_netif`/lwIP na ESP32-S3 |
| `hal_littlefs` | jedna niezależna od targetu fasada i wspólny provider oparty na `third_party/littlefs` w wersji wskazanej przez repozytorium; RP i STM32G474 określają geometrię pamięci i udostępniają skoordynowane operacje bezpośredniego dostępu do wewnętrznego flash, a osobny test integracyjny hosta używa modelu flash w RAM |
| `hal_udp` | wspólna implementacja bezpośredniego API UDP lwIP działająca na wybranym backendzie sieciowym CYW43 |
| `hal_tls` | wbudowany BearSSL nad natywnym `hal_tcp`; opcjonalny adapter transportu BSD jest kompilowany tylko, gdy dodatkowo włączono `HAL_ENABLE_BSD_SOCKETS` |
| Adapter BSD sockets | współdzielony `hal/network/adapters/bsd/hal_bsd_sockets.cpp` nad HAL UDP/TCP; pozostaje niezależnie wybieralny bez TLS |
| `hal_wireguard` | wspólna implementacja WireGuard/lwIP oraz backend udostępniany przez target korzystający z lwIP |
| `hal_mqtt` | wbudowany `PubSubClient` nad HAL TCP, z opcjonalnym transportem MQTTS przez BearSSL |
| `hal_notify` | fasada wybierająca backend oraz Telegram korzystający ze współdzielonego klienta HTTP/HTTPS |
| `hal_ota` | staging/aplikator RP z uwierzytelnionym transportem VS Code nad HAL UDP/TCP |
| `hal_time` | Współdzielone pomocniki gregoriańskie/CET/CEST oraz interwałów, plus klient HAL UDP/NTP i integracja z zegarem targetu |
| `hal_kv` | wewnętrzne `hal_eeprom` + `hal_sync` |
| `hal_sdlogger` | rdzeń FatFs R0.16 w wersji wskazanej przez repozytorium oraz współdzielona warstwa plikowa w `hal/storage/filesystem/` |
| `tools` | API HAL |
| `multicoreWatchdog` | wewnętrzne `SmartTimers` + mutex `hal_sync` |

## Zależności (mock / build PC)

Wszystkie pliki `impl/.mock/` zależą tylko od standardowych nagłówków hosta,
takich jak `<cstdio>`, `<cstring>`, `<mutex>`, `<queue>` i `<stdarg.h>`. Żadne
SDK wbudowane nie jest wymagane.

---

## Mapa systemu testów i miarodajne źródła

| Warstwa testów | Źródło konfiguracji | Wykonanie | Punkt rozszerzenia |
|---|---|---|---|
| Testy jednostkowe hosta/mock | `tests/CMakeLists.txt`, `tests/test_*.cpp`, główny `CMakeLists.txt` | CMake plus CTest | Dodaj zestaw Unity i zarejestruj go przez `add_hal_test(...)`, lub zadeklaruj dedykowany plik wykonywalny dla dodatkowych źródeł. |
| Testy hosta FreeRTOS POSIX | `tests/freertos_posix/`, `JH_ENABLE_FREERTOS_POSIX_TESTS` | CTest przez build hosta lub pełną kontrolę jakości | Dodaj target przez `add_hal_freertos_posix_test(...)`. |
| Kontrola jakości repozytorium | `runalltests.sh`, `.github/workflows/ci.yml` oraz dane narzędziowe opisane w `00_scripts.md` | `./runalltests.sh` | Rozszerz odpowiedni etap i jego ukierunkowane testy regresyjne; zapisuj generowane artefakty wyłącznie w `.build/`. |
| Projekty sprawdzające build firmware | `tests/fixtures/<fixture>/.vscode/jaszczurhal.project.json` | `jh-vscode` lub właściwy skrypt produkcyjny | Rozszerz macierz targetów, płytek i wariantów w manifeście oraz test układu artefaktów. |
| Fizyczne stanowiska sprzętowe | źródło, manifest i weryfikator w `tests/hardware/<fixture>/` | Build i wgranie przez `jh-vscode` lub właściwy skrypt produkcyjny, a następnie uruchomienie opisanego niżej weryfikatora | Dodaj firmware, jawną macierz sprzętową, mechanizm sprawdzający wynik na hoście, kryteria akceptacji i podsekcję w tym dokumencie. |

Jeśli opis różni się od zachowania, rozstrzygające są powyższe pliki
wykonywalne. README każdego stanowiska sprzętowego zawiera jedynie krótkie
odnośniki ułatwiające odnalezienie dokumentacji. Pełne instrukcje operatora,
okablowanie, wymagania i zapisane wyniki akceptacji znajdują się w sekcji
[Stanowiska sprzętowe](#stanowiska-sprzętowe) poniżej.

---

## Testy jednostkowe

### Wymagania

- CMake ≥ 3.16
- GCC / Clang z C++17

### Kompilowanie i uruchamianie

```bash
cmake -B .build/host -DCMAKE_BUILD_TYPE=Debug
cmake --build .build/host
ctest --test-dir .build/host --output-on-failure
```

## Bramki jakości repozytorium

### Skrypty szybkiego startu

Dwa skrypty ułatwiające pracę znajdują się w korzeniu repozytorium:

**`runmefirst.sh`** - jednorazowa konfiguracja toolchainu
```bash
./runmefirst.sh
```
Przygotowuje lokalne środowisko:
- instaluje hooki Git (`pre-commit` i `commit-msg` z `.githooks/`);
- synchronizuje przez `third_party/update_components.sh` wszystkie komponenty
  w wersjach wskazanych przez repozytorium;
- instaluje trwałe reguły dostępu USB i `/dev/ttyACM*` dla RP2040/RP2350,
  umożliwiające wgrywanie bez sudo i automatyczny reset BOOTSEL przez 1200 bps
- proponuje trwałą regułę zapory TCP/8266, ograniczoną do sieci LAN, dla
  połączeń zwrotnych OTA;
- przygotowuje katalogi buildu i początkową konfigurację CMake.

Uruchom go po sklonowaniu repozytorium oraz po zmianie środowiska.

**`runalltests.sh`** - pełna kontrola jakości
```bash
./runalltests.sh
./runalltests.sh --check-generated
```
Domyślnie przed rozpoczęciem kontroli skrypt odświeża wersjonowane pliki
generowane deterministycznie, a w końcowym podsumowaniu wymienia zmiany
wprowadzone przez synchronizację. Opcja `--check-generated` tylko sprawdza te
same pliki i nie poprawia rozbieżności. CI używa tego bardziej rygorystycznego trybu przez
`scripts/sync_generated.py --check`.

Uruchamia dziewięć etapów kontroli jakości w następującej kolejności:
1. Sprawdzenie obecności narzędzi
2. Testy jednostkowe hosta/mock (`.build/gate/host/` + ctest, w tym FreeRTOS POSIX)
3. Testy Clang ASan/UBSan i krótkie testy libFuzzer przez ten sam skrypt,
   którego używa CI
4. Bezpieczeństwo pamięci (Valgrind memcheck na wszystkich natywnych plikach wykonywalnych testów C/C++)
5. Analiza statyczna: cppcheck
6. Analiza statyczna: clang-tidy (bazy danych buildu hosta + STM32 poniżej
   `.build/gate/`)
7. Wykrywanie duplikatów PMD CPD w obrębie własnych implementacji C/C++ oraz
   skryptów Python
8. Buildy targetów (STM32G474 oraz testy startu i rdzenia Pico SDK
   RP2040/RP2350 ARM/RP2350 RISC-V, profile funkcjonalne RP, sześć
   reprezentatywnych buildów ELF/BIN/UF2 `01_core_runtime`/`18_freertos_suite`
   oraz jeden czysty build
   `tests/fixtures/esp32s3_phase3` z ESP-IDF w wersji wskazanej przez
   repozytorium i zwalidowanym
   manifestem zawierającym wiele obrazów)
9. Buildy przykładów (macierz `gateTargets` wyprowadzona ze wspólnego mechanizmu buildu
   plus dedykowane stanowiska target/runtime)

Kończy działanie z niezerowym kodem przy pierwszym błędzie; logi rejestrują
wszelkie ostrzeżenia/błędy. Bramka Valgrind wybiera każdy bezpośrednio
zarejestrowany natywny plik wykonywalny testu C/C++ przez etykietę CTest
`memcheck`. `MEMCHECK_REQUIRED_TESTS` w `runalltests.sh` to krytyczny podzbiór
sprawdzany przed wykonaniem, ale nie jest pełną listą. Testy Python, CMake i
sterowane powłoką nie są uruchamiane przez memcheck. Sprawiedliwe planowanie
wątków Valgrind pozwala uruchamiać natywne testy planisty FreeRTOS POSIX bez
ich zawieszania. Postęp CTest/Valgrind jest na bieżąco zapisywany w terminalu i w
`.build/gate/logs/jh_memcheck.log`.

Wszystkie dane wyjściowe buildu tworzone przez repozytorium trafiają do jednego
ignorowanego katalogu `.build/`. Testy kompilatora CMake uruchamiane w trybie
skryptowym używają `.build/tests/` i nie zapisują plików `.o` w
katalogu głównym repozytorium.

Etap clang-tidy tworzy osobne bazy danych analizy dla każdego profilu, z jedną
komendą buildu na plik źródłowy. Zapobiega to sytuacji, w której testy
fasad, które kompilują ten sam wspólny driver z kilkoma zestawami modułów,
uruchamiały analizator wielokrotnie. Zwykłe buildy targetów nadal kompilują
każdy skonfigurowany wariant.

Etap CPD używa uwierzytelnionej dystrybucji PMD 7.26.0 zarządzanej w
`third_party/pmd`. Skanuje pliki implementacji C/C++, a nie nagłówki, oraz
pliki Pythona w `scripts/`, wykluczając źródła generowane i dostarczone przez
firmy trzecie.
Każda grupa duplikatów C/C++ od 100 tokenów blokuje w produkcji, testach i
przykładach; każda grupa skryptów Python od 50 tokenów również blokuje. Żadna
lista bazowa ani lista wyjątków nie może ukryć istniejącej grupy. Raport liczy
łączny zakres zduplikowanych tokenów i podaje wynik globalnie oraz dla
mocka, RP2040, STM32G474, kodu współdzielonego, pozostałego kodu przenośnego
i skryptów Python. Raporty XML i deterministyczne listy plików są zapisywane
poniżej `.build/gate/cpd/`. CPD `PASS` oznacza zero grup przy skonfigurowanych
progach specyficznych dla danego języka.

Jest to **zalecana kontrola przed commitem** oraz **bramka testowa CI/CD**.
Uruchamiaj przed wypchnięciem zmian, aby wcześnie wychwycić problemy między
platformami.

### Natywna bramka CI dla Windows

`.github/workflows/ci.yml` uruchamia dwie natywne bramki `windows-2025`, oprócz
kompletnej bramki jakości dla Linuksa:

- `windows-tooling` przygotowuje uwierzytelnione zarządzane środowisko,
  powtarza `runmefirst.ps1 -VerifyOnly`, uruchamia współdzielone testy
  runtime/platform/bootstrap i generatora, weryfikuje wybór źródeł zależności
  CMake dla FreeRTOS na RP i STM32, wykonuje czysty produkcyjny build
  ESP32-S3/ESP-IDF i publikuje artefakty zawierające wiele obrazów, a następnie kompiluje
  i uruchamia przenośne testy hosta z MSVC `/W4 /permissive- /WX`;
- `Windows firmware (<target>)` buduje wygenerowany projekt użytkownika ze
  ścieżki zawierającej spacje przez Ninja dla `rp2040`, `rp2350-arm`,
  `rp2350-riscv` i `stm32g474`, sprawdza artefakty targetu oraz bazę danych
  buildu ze ścieżkami dostosowanymi do Windows, a następnie publikuje
  reprezentatywne artefakty buildu.

Inwentarz CTest dla Windows utrzymuje adapter BSD POSIX, integrację
Bash/POSIX BearSSL oraz runtime FreeRTOS GCC/POSIX jako
widoczne, wyłączone testy. Ich aktywne pokrycie, wraz z Valgrind, cppcheck,
clang-tidy i PMD CPD, pozostaje w bramce dla Linuksa. Fiesta, DoomConsole i
Ford DPF Tracker mają osobne natywne procesy CI firmware dla Windows. Każdy z
nich uruchamia właściwe dla danego projektu testy integracyjne, niezależnie od
projektu testowego generowanego przez JaszczurHAL.

## Stanowiska sprzętowe

Powtarzalne testy fizycznych urządzeń używają tego samego mechanizmu VS Code
co aplikacje i zapisują artefakty w `.build/hardware/`:

| Stanowisko | Pokrycie |
|---|---|
| `tests/hardware/bluetooth_stage1` | Wewnętrzny test kontrolera CYW43/BTstack opracowany przed publicznym API: rozgłaszanie, statyczny GATT oraz wariant odniesienia `wifi-only` na Pico W i STM32G474/PIM730. |
| `tests/hardware/bluetooth_gamepad` | Zanonimizowany deskryptor i raporty 8BitDo Zero 2 Android D-input oraz prywatna sonda parsera gamepada Classic HID Host dla Pico 2 W. |
| `tests/hardware/bluetooth_observer` | Publiczne pasywne skanowanie Observer, ograniczona kolejka raportów oraz parsowanie BLE Teltonika/iBeacon/Eddystone na Pico W, Pico 2 W i STM32G474/PIM730. |
| `tests/hardware/bluetooth_stream` | Publiczny cykl życia BLE i uwierzytelniona bramka Stream w różnych krotkach target/board/runtime, w tym ponowne łączenie, watchdog, ciągły ruch, nasycenie i negatywne przypadki bezpieczeństwa. |
| `tests/hardware/rp_usb_cdc_echo` | Natywne zgłaszanie interfejsu CDC TinyUSB, mechanizm ograniczania nadawcy (`backpressure`), ponowne łączenie i przepustowość |
| `tests/hardware/rp_usb_multicore` | Równoległe zadania generujące dane CDC na obu rdzeniach RP, integralność i kompletność danych oraz przypisanie rekordów do właściwego producenta w trybach bare metal i FreeRTOS |
| `tests/hardware/rp_freertos_smp` | Scheduler, oba rdzenie, mutex/opóźnienie, sterta i USB pod FreeRTOS SMP |
| `tests/hardware/rp_flash_transaction` | Sekwencjonowanie koordynatora flash, ścieżki odrzucenia, kasowanie/programowanie i odzyskiwanie |
| `tests/hardware/rp_storage` | Trwały zapis w EEPROM, formatowanie i ponowne montowanie LittleFS oraz montowanie po resecie |
| `tests/hardware/rp_sdlogger` | Fizyczne montowanie karty SD przez SPI, deterministyczne dopisywanie, opróżnianie bufora i zamykanie pliku, reset, ponowne montowanie, zawartość pliku i trwałość licznika logów w EEPROM |
| `tests/hardware/rp_ota` | Odkrywanie, uwierzytelnianie, transfer, próba/potwierdzenie, wycofanie (rollback) i odzyskiwanie USB/sieci |
| `tests/hardware/lora_sx1262` | Inicjalizacja dwóch urządzeń SX1262, dwukierunkowe surowe pakiety, cykl życia niezawodnego łącza oraz fragmentowane transakcje żądanie/odpowiedź routera poleceń na parach zintegrowanych LF lub zewnętrznych HF |
| `tests/hardware/esp32s3_phase1` | Tożsamość targetu/płytki ESP32-S3 Fazy 1, generowana sygnatura linkowania, model układu/liczba rdzeni, fizyczny flash, zainicjalizowany Quad PSRAM oraz powtarzane bicie serca `app_task0()` FreeRTOS nad natywnym USB Serial/JTAG. |
| `tests/hardware/esp32s3_phase2` | Sonda runtime Fazy 2 ESP32-S3 dla obu zadań aplikacji, system/sync, GPIO/IRQ, ADC, USB Serial/JTAG TX/RX, sprzętowy UART, skanowanie mastera I2C, ścieżka transferu mastera SPI, callbacki timera z dedykowanej puli oraz włączona konfiguracja stack-guard FreeRTOS. |

Poniższe podsekcje zawierają pełne instrukcje obsługi każdego stanowiska
fizycznego. Udany build firmware potwierdza wyłącznie poprawność programową,
chyba że opis danego stanowiska wyraźnie stanowi inaczej. Do akceptacji
sprzętowej potrzebny jest wynik weryfikatora hostowego albo kontroli wzrokowej
oraz spełnienie zapisanych kryteriów PASS.

### Sprzętowy test USB CDC na RP

`tests/hardware/rp_usb_cdc_echo` sprawdza natywną obsługę TinyUSB przez HAL na RP
na fizycznym Pico lub Pico 2, w tym targety RP2350 ARM i RISC-V. Firmware
odbija dowolne bajty CDC i przełącza diodę LED płytki po każdym w pełni
odbitym bloku odbioru USB.

Skompiluj i wykonaj pierwsze wgranie BOOTSEL:

```sh
vscode/entry/jh-vscode build \
  --project tests/hardware/rp_usb_cdc_echo \
  --target rp2040 --board pico
vscode/entry/jh-vscode upload-uf2 \
  --project tests/hardware/rp_usb_cdc_echo \
  --target rp2040 --board pico
```

Dla Pico W i Pico 2 W użyj odpowiednio `--board picow` i `--board pico2w`.
Gdy inna płytka jest już w trybie BOOTSEL, niezależna od targetu akcja
`upload` najpierw zapamiętuje listę istniejących dysków, następnie wyzwala
reset przez 1200 bps i zapisuje plik wyłącznie na nowo wykrytym dysku.

Zweryfikuj integralność danych, opóźnione odczyty hosta, przepustowość oraz
zamknięcie/ponowne otwarcie:

```sh
python3 -m pip install pyserial
python3 tests/hardware/rp_usb_cdc_echo/verify_cdc_echo.py \
  --port /dev/serial/by-id/<device>
```

Po pierwszym wgraniu, neutralna względem targetu akcja `upload` musi wejść
do BOOTSEL przez dotknięcie DTR 1200 bps i wrócić z tą samą tożsamością CDC:

```sh
vscode/entry/jh-vscode upload \
  --project tests/hardware/rp_usb_cdc_echo \
  --target rp2040 --board pico \
  --port /dev/serial/by-id/<device>
```

Użyj jawnego, stabilnego portu by-id, gdy podłączonych jest wiele
kompatybilnych płytek. Skrypt celowo nie wybiera samodzielnie między dwoma
zweryfikowanymi portami.

#### Wstrzymanie i wznowienie w runtime na Linuksie

Zamknij każdy proces trzymający port CDC. Ustaw `USB_DEVICE_SYSFS` na węzeł
urządzenia USB, a nie węzeł jego interfejsu (na przykład
`/sys/bus/usb/devices/3-4.1.4`):

```sh
printf '0\n' |
  sudo tee "$USB_DEVICE_SYSFS/power/autosuspend_delay_ms" >/dev/null
printf 'auto\n' |
  sudo tee "$USB_DEVICE_SYSFS/power/control" >/dev/null
sleep 3
cat "$USB_DEVICE_SYSFS/power/runtime_status"

printf 'on\n' |
  sudo tee "$USB_DEVICE_SYSFS/power/control" >/dev/null
sleep 1
cat "$USB_DEVICE_SYSFS/power/runtime_status"
```

Oczekiwane stany to `suspended`, a następnie `active`. Uruchom ponownie
`verify_cdc_echo.py` po wznowieniu, a następnie przywróć oryginalne wartości
`autosuspend_delay_ms` i `control`.

### Sprzętowy test USB wielordzeniowy na RP

`tests/hardware/rp_usb_multicore` uruchamia jednego producenta CDC na każdym
rdzeniu RP. Obaj producenci zapisują 4096 niezależnie numerowanych rekordów z
sumą kontrolną przez `hal_usb`, podczas gdy host weryfikuje granice rekordów,
integralność, kompletność, przypisanie producenta do rdzenia i końcowy status
HAL. Uszkodzona linia wskazuje przeplatanie bajtów między równoległymi
zapisami.

Skompiluj i wgraj wariant bare-metal RP2040:

```sh
vscode/entry/jh-vscode build \
  --project tests/hardware/rp_usb_multicore \
  --target rp2040 --board pico
vscode/entry/jh-vscode upload \
  --project tests/hardware/rp_usb_multicore \
  --target rp2040 --board pico \
  --port /dev/serial/by-id/<device>
python3 tests/hardware/rp_usb_multicore/verify_usb_multicore.py \
  --port /dev/serial/by-id/<device> \
  --target rp2040 --board pico --runtime baremetal
```

Dla Pico 2 wybierz `rp2350-arm` lub `rp2350-riscv`, użyj płytki buildu
`pico2` i przekaż `--board pico2` do weryfikatora. Dodaj `--variant freertos`
do komend buildu i wgrywania oraz użyj `--runtime freertos` dla przebiegu
FreeRTOS SMP.

Domyślne `--records 4096` weryfikatora musi zgadzać się z
`JH_USB_MULTICORE_RECORDS` w buildu firmware.

### Sprzętowy test FreeRTOS SMP na RP

`tests/hardware/rp_freertos_smp` sprawdza natywny kernel FreeRTOS w wersji
wskazanej przez repozytorium na fizycznym Pico lub Pico 2. Obejmuje start
planisty, przypisanie zadań
aplikacji na obu rdzeniach, działanie muteksu HAL między rdzeniami,
raportowanie sterty FreeRTOS oraz natywny ruch USB CDC z opóźnionymi
odczytami hosta.

Zbuduj i wgraj przez standardowy proces VS Code dla targetów natywnych:

```sh
vscode/entry/jh-vscode build \
  --project tests/hardware/rp_freertos_smp \
  --target rp2040 --board pico
vscode/entry/jh-vscode upload \
  --project tests/hardware/rp_freertos_smp \
  --target rp2040 --board pico \
  --port /dev/serial/by-id/<device>
```

Uruchom weryfikator hosta:

```sh
python3 tests/hardware/rp_freertos_smp/verify_freertos_smp.py \
  --port /dev/serial/by-id/<device>
```

Użyj `rp2350-arm` lub `rp2350-riscv` z płytką `pico2` dla Pico 2. Gdy
urządzenie nie ma jeszcze działającego firmware CDC, użyj `upload-uf2`, gdy
jest ono w BOOTSEL.

### Sprzętowy test transakcji flash na RP

`tests/hardware/rp_flash_transaction` weryfikuje natywnego koordynatora
flash RP na fizycznym Pico lub Pico 2. Wykonuje operacje rezydujące w RAM z
obu rdzeni, weryfikuje odrzucanie aktywnego DMA i callbacków XIP, sprawdza
obsługę wejścia rekurencyjnego, mutuje ostatni sektor flash oraz weryfikuje
czyszczenie i odzyskiwanie po zatrzymaniu operacji między kasowaniem a
programowaniem.

Sonda celowo zawłaszcza ostatni sektor flash płytki. Nie uruchamiaj jej na
firmware, które przechowuje tam niepowiązane dane.

Zbuduj i wgraj wariant bare-metal zgodnie ze zwykłym procesem:

```sh
vscode/entry/jh-vscode build \
  --project tests/hardware/rp_flash_transaction \
  --target rp2040 --board pico
vscode/entry/jh-vscode upload \
  --project tests/hardware/rp_flash_transaction \
  --target rp2040 --board pico \
  --port /dev/serial/by-id/<device>
```

Dla wariantu FreeRTOS SMP dodaj poniższy tymczasowy wpis cache do manifestu
i uruchom te same komendy buildu/wgrywania:

```json
"JH_EXTRA_DEFINES": "HAL_ENABLE_FREERTOS=1"
```

Usuń wpis cache przed ponownym buildem wariantu bare-metal.

Uruchom weryfikator:

```sh
python3 tests/hardware/rp_flash_transaction/verify_flash_transaction.py \
  --port /dev/serial/by-id/<device>
```

### Sprzętowy test natywnego magazynu danych na RP

`tests/hardware/rp_storage` sprawdza natywne EEPROM i LittleFS na fizycznym
sprzęcie RP2040/RP2350. Zapisuje trwale licznik uruchomień w EEPROM, formatuje
i ponownie montuje partycję LittleFS, wywołuje reset przez watchdog, a następnie
potwierdza trwałość EEPROM i możliwość zamontowania LittleFS bez ponownego
formatowania.

Zbuduj i wgraj zgodnie ze zwykłym procesem dla targetów natywnych:

```sh
vscode/entry/jh-vscode build \
  --project tests/hardware/rp_storage \
  --target rp2040 --board pico
vscode/entry/jh-vscode upload \
  --project tests/hardware/rp_storage \
  --target rp2040 --board pico \
  --port /dev/serial/by-id/<device>
```

Uruchom weryfikator:

```sh
python3 tests/hardware/rp_storage/verify_storage.py \
  --port /dev/serial/by-id/<device>
```

Użyj `rp2350-arm` lub `rp2350-riscv` z płytką `pico2` dla Pico 2.

### Sprzętowy test RP SDLogger

`tests/hardware/rp_sdlogger` sprawdza wspólną implementację SDLogger z fizyczną
kartą SD podłączoną przez SPI. Montuje kartę, otwiera log numerowany na
podstawie EEPROM, dopisuje deterministyczną zawartość, opróżnia bufor i zamyka
plik, wywołuje reset przez watchdog, ponownie montuje kartę, sprawdza dokładny
dopisany fragment na końcu pliku oraz potwierdza, że licznik logów w EEPROM
został zachowany.

Podłącz moduł SD SPI 3,3 V do Pico lub Pico 2:

| Sygnał SD | GPIO RP | Fizyczny pin Pico |
|---|---:|---:|
| MISO | GP16 | 21 |
| CS | GP17 | 22 |
| SCK | GP18 | 24 |
| MOSI | GP19 | 25 |
| 3V3 | 3V3(OUT) | 36 |
| GND | GND | 23 |

Skompiluj, wgraj i zweryfikuj wariant bare-metal RP2040:

```sh
vscode/entry/jh-vscode build \
  --project tests/hardware/rp_sdlogger \
  --target rp2040 --board pico
vscode/entry/jh-vscode upload \
  --project tests/hardware/rp_sdlogger \
  --target rp2040 --board pico \
  --port /dev/serial/by-id/<device>
python3 tests/hardware/rp_sdlogger/verify_sdlogger.py \
  --port /dev/serial/by-id/<device> \
  --target rp2040 --board pico --runtime baremetal
```

Dla Pico 2 wybierz `rp2350-arm` lub `rp2350-riscv`, użyj płytki buildu
`pico2` i przekaż `--board pico2` do weryfikatora. Dodaj `--variant freertos`
do komend buildu i wgrywania oraz użyj `--runtime freertos` dla przebiegu
FreeRTOS.

Weryfikator jest powtarzalny bez formatowania karty. Jeśli istnieje stary
plik logu o tej samej nazwie, weryfikuje on nowo dopisany deterministyczny
fragment końcowy.

### Sprzętowy test natywnego OTA na RP

`tests/hardware/rp_ota` weryfikuje odkrywanie i uwierzytelnianie OTA,
potwierdzany transfer fragment po fragmencie, próbny rozruch (trial boot),
jawne potwierdzenie, drugą niepotwierdzoną próbę, automatyczne wycofanie
(rollback) oraz odzyskiwanie sieci/USB po każdym restarcie. Obsługuje
Pico W/RP2040 oraz Pico 2 W/RP2350 ARM w buildach bare-metal i FreeRTOS,
a także zwykłego Pico/RP2040 podłączonego do bezprzewodowego modułu
PIM730/RM2.

Skopiuj lokalny szablon sekretu i zastąp wszystkie wartości. Wynikowy
nagłówek jest ignorowany przez Git:

```sh
cp tests/hardware/rp_ota/ota_test_secrets.example.h \
  tests/hardware/rp_ota/ota_test_secrets.h
```

Weryfikator odczytuje hasło OTA z tego ignorowanego nagłówka. Zmienna
środowiskowa może je nadpisać w razie potrzeby:

```sh
export JH_OTA_TEST_PASSWORD='the-value-from-ota_test_secrets.h'
```

Skompiluj, wgraj i zweryfikuj wariant bare-metal RP2040:

```sh
vscode/entry/jh-vscode build \
  --project tests/hardware/rp_ota \
  --target rp2040 --board picow
vscode/entry/jh-vscode upload \
  --project tests/hardware/rp_ota \
  --target rp2040 --board picow \
  --port /dev/serial/by-id/<device>
python3 tests/hardware/rp_ota/verify_ota.py \
  --port /dev/serial/by-id/<device> \
  --target rp2040 --board picow --runtime baremetal
```

Użyj `--target rp2350-arm --board pico2w` dla Pico 2 W. Dodaj
`--variant freertos` zarówno do buildu, jak i wgrywania, a następnie
przekaż `--runtime freertos` do weryfikatora dla wariantu FreeRTOS. Stanowisko
przydziela zadaniu aplikacji stos 8 KiB w buildach FreeRTOS, ponieważ
inicjalizacja CYW43 i obsługa OTA przekraczają ogólny domyślny rozmiar 2 KiB.

Dla Pico+PIM730 podłącz moduł do zwykłego Pico w następujący sposób:

| PIM730 | Sygnał Pico | Fizyczny pin |
|---|---|---|
| `WL_ON` | GP2 | 4 |
| `CS` | GP3 | 5 |
| `DAT` | GP4 | 6 |
| `CLK` | GP5 | 7 |
| `GND` | GND | 8 |
| `3V3` | 3V3(OUT) | 36 |

`DAT` to połączony dwukierunkowy sygnał danych/wybudzania hosta. Pozostaw
`BL_ON`, `GPIO0..2` oraz `N/C` niepodłączone. Skompiluj i zweryfikuj ten
profil jawnie:

```sh
vscode/entry/jh-vscode build \
  --project tests/hardware/rp_ota \
  --target rp2040 --board pico-rm2
vscode/entry/jh-vscode upload \
  --project tests/hardware/rp_ota \
  --target rp2040 --board pico-rm2 \
  --port /dev/serial/by-id/<device>
python3 tests/hardware/rp_ota/verify_ota.py \
  --port /dev/serial/by-id/<device> \
  --target rp2040 --board pico-rm2 --runtime baremetal \
  --artifact-dir .build/hardware/rp_ota/cmake/rp2040/pico-rm2
```

Dodaj `--status-only`, aby zweryfikować tożsamość płytki, gotowość sieci
oraz automatyczną telemetrię zegara gSPI bez generowania ani przesyłania
obrazów OTA. Ten tryb diagnostyczny nie wymaga hasła OTA ani artefaktów
buildu.

Użyj `--variant freertos`, `--runtime freertos` oraz katalogu artefaktów
`.build/hardware/rp_ota/cmake/variants/freertos/rp2040/pico-rm2` dla
przebiegu FreeRTOS.

Gdy istnieje jednocześnie kilka buildów dla różnych kombinacji target/runtime, wskaż
weryfikatorowi pasujące dane wyjściowe CMake zamiast ostatnio opublikowanego
katalogu artefaktów:

```sh
python3 tests/hardware/rp_ota/verify_ota.py \
  --port /dev/serial/by-id/<device> \
  --target rp2350-arm --board pico2w --runtime freertos \
  --artifact-dir \
    .build/hardware/rp_ota/cmake/variants/freertos/rp2350-arm/pico2w
```

Pico W i Pico 2 W mogą pozostać podłączone razem. Ich nazwy hostów OTA to
`jh-ota-rp2040` i `jh-ota-rp2350-arm`, więc odkrywanie nigdy nie zgaduje
między nimi. Pico W i Pico+PIM730 współdzielą nazwę hosta RP2040; gdy oba są
zasilone, przekaż adres IP wybranego urządzenia do `--broadcast`. Odpowiedź
statusu CDC zawiera i weryfikator sprawdza profil płytki, aktualny adres
IPv4, target, runtime i stan rozruchu OTA, plus aktywne `clk_sys`, żądaną i
efektywną szybkość gSPI, dzielnik 16,8 oraz wybrany program timingu PIO.
Weryfikator tworzy podpisane kontenery A/B poniżej `.build/hardware/rp_ota`
i pozostawia urządzenie w stabilnym obrazie A po udowodnieniu wycofania z
obrazu B.

`hal_ota_begin()` publikuje też skonfigurowaną nazwę hosta przez mDNS.
Podczas gdy sonda jest podłączona, zweryfikuj responder niezależnie, na
przykład przez `getent hosts jh-ota-rp2040.local` (lub nazwę hosta RP2350).
Nazwa hosta WiFi jest wysyłana w opcji DHCP 12, a jej zmiana przy aktywnej
dzierżawie wyzwala jej odnowienie.

Callback wgrywania OTA to połączenie TCP od płytki do hosta na TCP/8266.
Uruchom `./runmefirst.sh` i zatwierdź jego trwałą regułę OTA ograniczoną do
sieci LAN przed testem sprzętowym. Zweryfikuj regułę bez jej zmiany za pomocą:

```sh
python3 scripts/configure_ota_firewall.py --check
```

Na natywnym Windowsie najpierw zweryfikuj, że wybrana zaufana sieć LAN ma
profil połączenia `Private`, a następnie sprawdź dokładny plan reguły bez
zmieniania hosta:

```powershell
.\.build\windows\venv\Scripts\python.exe `
  .\scripts\configure_ota_firewall.py --dry-run `
  --interface 'Wi-Fi' --network '192.168.2.0/24'
```

Zastosuj ten sam zakres z już podniesionego (elevated) PowerShell po usunięciu
`--dry-run`. Pomocnik prosi o potwierdzenie, tworzy idempotentną regułę
Windows Defender Firewall ograniczoną do profilu `Private`, interfejsu,
podsieci źródłowej i TCP/8266, i nigdy nie zmienia samego profilu sieci.
Weryfikacja sprzętowa na Windows używa zarządzanego pliku wykonywalnego
Pythona i akceptuje port COM, na przykład `--port COM3`.

Jeśli sieć testowa różni się od sieci wybranej podczas wstępnej konfiguracji,
uruchom ponownie pomocnika z jawnymi `--interface` i `--network`. Jeśli
odkrywanie przez broadcast ograniczony (limited-broadcast) jest zablokowane,
ale adres IP urządzenia jest znany, skieruj wykrywanie bezpośrednio pod ten
adres:

```sh
python3 tests/hardware/rp_ota/verify_ota.py \
  <other-options> --broadcast <device-ip>
```

Gdy fabryczny UF2 zastępuje aktywny program obrazem dla innej kombinacji
target/runtime lub płytka ponownie użyta przez inne stanowisko flash zgłasza
nieprawidłowy stan początkowy OTA, wejdź w BOOTSEL i wykasuj tylko cztery
sektory kontrolne OTA przed wgraniem nowego UF2. Stare metadane zawierają
skrót (digest) poprzedniego aktywnego programu i nie mogą być sparowane z
zastępczym obrazem. Bezwzględne zakresy to `0x101fc000..0x10200000` dla
Pico W lub Pico+PIM730 oraz `0x103fc000..0x10400000` dla Pico 2 W:

```sh
.build/tools/picotool/picotool erase -r <range-start> <range-end> \
  --bus <usb-bus> --address <usb-address>
```

Ta zautomatyzowana sonda nie symuluje utraty zasilania podczas trwającej
podmiany obrazu flash. Walidacja utraty zasilania wymaga sterowanego
przełącznika zasilania i jest osobnym, destrukcyjnym przebiegiem
odzyskiwania.

### Sprzętowy test Bluetooth - etap 1

`tests/hardware/bluetooth_stage1` to wewnętrzny test integracji CYW43/BTstack,
opracowany jeszcze przed publicznym API. Macierz buildu obejmuje STM32G474 Nucleo + PIM730,
Raspberry Pi Pico W oraz RP2350 ARM Pico 2 W. Celowo nie włącza żadnego
publicznego makra włączającego Bluetooth i nie może służyć jako
przykład publicznego API aplikacji.

Opisane niżej wcześniejsze testy sprzętowe etapu 1 obejmują Nucleo+PIM730 oraz
Pico W. Akceptacja sprzętowa Pico 2 W używa zamiast tego publicznych stanowisk
[Bluetooth Observer](#sprzętowy-test-bluetooth-observer) oraz
[BLE Stream](#bramka-sprzętowa-jh-ble-stream-v1). RP2350 RISC-V jest
nieobsługiwane, ponieważ jego transport Bluetooth CYW43 nie jest włączony.

Projekt kompiluje źródła BTstack bezpośrednio i nie linkuje
`pico_cyw43_arch`, `pico_btstack_cyw43` ani integracji pamięci masowej
Bluetooth z Pico SDK. Uruchamia wspólną instancję JH zarządzającą radiem CYW43
przez interfejs BLE i za jej pośrednictwem pobiera firmware Bluetooth.
Następnie rozpoczyna rozgłaszanie z możliwością nawiązania połączenia pod nazwą
`JH BLE Stage 1` oraz udostępnia niewielką, statyczną charakterystykę GATT do
odczytu i zapisu.

Pomyślny build potwierdza tylko poprawność oprogramowania. Wyniki testu
sprzętowego muszą
rejestrować wyjście `JHBT1`, zachowanie połączenia/zapisu, użycie pamięci
ELF/map oraz dokładną płytkę/okablowanie testowane. Przebieg STM32
dodatkowo sprawdza, czy na zmontowanym stanowisku sygnał `BT_ON` modułu PIM730
nadal jest połączony z `WL_ON`.

Wariant `bluetooth` służy do właściwego testu, a `wifi-only` jest równoważnym
punktem odniesienia dla pomiaru pamięci. Oba warianty należy mierzyć na
podstawie plików ELF/map, przy tym samym układzie docelowym, tej samej płytce,
wersji kompilatora i konfiguracji buildu.

Buildy programowe Etapu 1 zmierzone 2026-08-04 to:

| Target i wariant | Obciążenie FLASH | SRAM statyczne | Zarezerwowana sterta/stos |
|---|---:|---:|---:|
| STM32G474 + PIM730, `bluetooth` | 332,3 KiB | 50,0 KiB | 3,0 KiB |
| STM32G474 + PIM730, `wifi-only` | 276,9 KiB | 43,2 KiB | 3,0 KiB |
| RP2040 Pico W, `bluetooth` | 403,2 KiB | 60,4 KiB | 6,0 KiB |
| RP2040 Pico W, `wifi-only` | 326,0 KiB | 53,6 KiB | 6,0 KiB |

Te pomiary nie wymagają zmniejszonego ATT MTU ani mniejszych kolejek Etapu 1.

Po wprowadzeniu wspólnej instancji zarządzającej radiem w etapie 2 uzyskano
następujące pomiary obrazów zbudowanych w tych samych warunkach:

| Target i wariant | Obciążenie FLASH | SRAM statyczne | Zarezerwowana sterta/stos |
|---|---:|---:|---:|
| STM32G474 + PIM730, `bluetooth` | 326,0 KiB | 48,4 KiB | 3,0 KiB |
| STM32G474 + PIM730, `wifi-only` | 278,1 KiB | 43,2 KiB | 3,0 KiB |
| RP2040 Pico W, `bluetooth` | 393,8 KiB | 57,3 KiB | 6,0 KiB |
| RP2040 Pico W, `wifi-only` | 327,8 KiB | 53,6 KiB | 6,0 KiB |

Obrazy tylko-WiFi nadal wykluczają BTstack, firmware Bluetooth oraz
współdzielone pule Bluetooth na magistrali. Zmiana sposobu zarządzania radiem nie dodaje
żadnego statycznego SRAM do żadnej z baz tylko-WiFi.

Po przejściu w etapie 3 na interfejs kontrolera, HCI o ograniczonym czasie
pracy i pętlę zdarzeń zarządzaną przez JH uzyskano następujące pomiary obrazów
zbudowanych w tych samych warunkach:

| Target i wariant | Obciążenie FLASH | SRAM statyczne | Zarezerwowana sterta/stos |
|---|---:|---:|---:|
| STM32G474 + PIM730, `bluetooth` | 327,1 KiB | 48,5 KiB | 3,0 KiB |
| STM32G474 + PIM730, `wifi-only` | 278,1 KiB | 43,3 KiB | 3,0 KiB |
| RP2040 Pico W, `bluetooth` | 390,1 KiB | 57,3 KiB | 6,0 KiB |
| RP2040 Pico W, `wifi-only` | 322,5 KiB | 53,6 KiB | 6,0 KiB |

Test sprzętowy etapu 3 ponownie sprawdził na obu płytkach uruchomienie
kontrolera, rozgłaszanie, połączenie przez BlueZ i wykrycie usługi GATT.
STM32G474 + PIM730 zarejestrował symetryczny ruch ACL na poziomie `11/11`.
Limit iteracji przeznaczonych na opróżnianie kolejki został osiągnięty
dwukrotnie, wyłącznie podczas inicjalizacji. Pico W również zarejestrował
symetryczny ruch ACL na poziomie `11/11`, lecz ani razu nie osiągnął tego
limitu. Oba transporty zachowały status `HAL_OK`, a na obu płytkach pozostał
uruchomiony wariant `bluetooth`.

Podetap sprzętowy 1.a został ukończony na obu profilach 2026-08-04. Sonda
STM32G474 + PIM730 użyła okablowania opisanego poniżej. Sonda Pico W korzystała
z wbudowanego układu CYW43439 i zgłosiła się przez USB jako `JaszczurHAL RP`.
Na obu płytkach kontroler osiągnął stan gotowości i rozpoczął rozgłaszanie z
możliwością nawiązania połączenia. BlueZ wykrył statyczną usługę GATT, a testy
odczytu i zapisu charakterystyki zakończyły się powodzeniem. Urządzenie
zaakceptowało też rozłączenie, ponowne połączenie i kolejny odczyt GATT.
Odpowiadające im obrazy `wifi-only` również zwróciły `HAL_OK`.

Wstępne wykrywanie ATT na STM32 ujawniło brakującą
inicjalizację Security Managera; sonda inicjalizuje teraz `sm_init()` przed
`att_server_init()`. Cykl życia połączenia jest obserwowany przez jedną
rejestrację zdarzenia HCI, dzięki czemu każde fizyczne łącze jest liczone
raz.

Ostateczny obraz przywrócony na każdej płytce to wariant `bluetooth`.
Podczas testu połączenia Pico W ani razu nie osiągnięto limitu iteracji
opróżniania kolejki. Na STM32 limit ten osiągnięto dwukrotnie podczas
inicjalizacji kontrolera; później transport działał stabilnie ze statusem
`HAL_OK`.

Podstawowy test sprzętowy etapu 2 ponownie sprawdził na obu płytkach
uruchomienie kontrolera, rozgłaszanie, połączenie przez BlueZ i wykrycie usługi
GATT z użyciem wspólnej instancji zarządzającej radiem. Pico W zarejestrował
również symetryczny ruch ACL i ani razu nie osiągnął limitu iteracji
opróżniania kolejki. Na obu płytkach pozostał uruchomiony wariant `bluetooth`.

#### Podetap sprzętowy 1.a - okablowanie i procedura

Zacznij od Nucleo odłączonego od USB i wszelkiego innego zasilania. Podłącz
PIM730 bezpośrednio krótkimi przewodami:

| PIM730 | STM32G474 | Złącze Nucleo |
|---|---|---|
| `CS` | `PB12` | CN10 pin 16 |
| `DAT` | `PB15` | CN10 pin 26 |
| `WL_ON` | `PB14` | CN10 pin 28 |
| `CLK` | `PB13` | CN10 pin 30 |
| `GND` | GND | CN10 pin 20 |
| `3V3` | 3,3 V | CN7 pin 16 |

Nie używaj napięcia 5 V. Sprawdź wzrokowo, czy przeznaczona do ewentualnego
przecięcia ścieżka łącząca `BT_ON` z `WL_ON` na PIM730 jest nienaruszona.
Wyprowadzenia `BT_ON` i `BL_ON` pozostaw niepodłączone. Dopiero po sprawdzeniu
okablowania i ścieżki wgraj obraz Bluetooth STM32 przez ST-Link płytki Nucleo.
Przed sprawdzeniem wykrywania urządzenia, połączenia, odczytu i zapisu
charakterystyki, ponownego połączenia oraz regresji wariantu `wifi-only`
zarejestruj cykliczne komunikaty `JHBT1`. Drugim profilem sprzętowym jest test
radia wbudowanego w Pico W.

### Sprzętowy test gamepada Bluetooth Classic HID

`tests/hardware/bluetooth_gamepad` zawiera wewnętrzny test hosta Classic HID,
opracowany przed publicznym API, oraz zanonimizowany zapis
`zero2_android_dinput.json`. Zapis obejmuje
137-bajtowy deskryptor raportu, tożsamość PnP, metadane SDP, wszystkie dwanaście
stanów wejściowych, niedeklarowany końcowy bajt wejścia i powtórzony surowy
raport. Nie zawiera adresów Bluetooth, kluczy połączeń, tożsamości hosta ani
numerów seryjnych USB.

Firmware inicjalizuje wspólne środowisko wykonawcze HCI/L2CAP, ulotną bazę na
jeden klucz połączenia, klienta SDP, jedno połączenie HID Host i jedną funkcję
obsługi zdarzeń. Wyszukiwanie rozpoczyna się wyłącznie po odebraniu przez port
szeregowy polecenia `DISCOVER`. Kończy się po 120 sekundach albo po
zaakceptowaniu pierwszego zgodnego urządzenia. Urządzenie musi mieć klasę
peryferyjną, zapisaną nazwę, usługę Classic HID i zapisaną tożsamość PnP.
Wewnętrznego mechanizmu wyboru nie wolno łączyć z publicznym BLE ani z
wcześniejszym testem etapu 1.

Wewnętrzny parser korzysta z deskryptora raportów HID, a nie ze stałych
offsetów bajtów kontrolera Zero 2. Obsługuje kolekcje aplikacyjne Generic
Desktop Game Pad i Joystick. Normalizuje maksymalnie 32 przyciski, dziewięć osi
pulpitu oraz jeden przełącznik kierunkowy do rekordów stanu przechowywanych w
pamięci o stałym rozmiarze. Limity C6 wynoszą 256 bajtów deskryptora, 32 bajty
raportu wejściowego i 16 rekordów w kolejce. Nieznane zastosowania HID są
ignorowane. Ograniczona diagnostyka sygnalizuje nieprawidłowe lub zbyt duże
deskryptory, skrócone lub zbyt duże raporty, nieznane identyfikatory raportów,
powtórzone mapowania zastosowań oraz przepełnienie kolejki. Ponowny raport,
który nie zmienia stanu, nie dodaje do kolejki kolejnego rekordu.

`test_bluetooth_gamepad_parser` używa tego samego zanonimizowanego zapisu co
test sprzętowy. Obejmuje zapisane raporty, układy pól wynikające z deskryptora,
wielokrotne podanie tego samego stanu wejścia, czyszczenie stanu po ponownym
połączeniu, nieprawidłowe i skrócone dane, nieznane identyfikatory raportów i
zastosowania HID, powtórzone zastosowania,
przepełnienie kolejki oraz brak alokacji dynamicznej podczas działania
parsera.

Zbuduj wymagany obraz Pico 2 W:

```sh
vscode/entry/jh-vscode build \
  --project tests/hardware/bluetooth_gamepad \
  --target rp2350-arm --board pico2w --variant classic-hid
```

Wgraj go i uruchom weryfikator sprzętowy na powstałym porcie CDC:

```sh
vscode/entry/jh-vscode upload \
  --project tests/hardware/bluetooth_gamepad \
  --target rp2350-arm --board pico2w --variant classic-hid \
  --port /dev/ttyACM0

python3 tests/hardware/bluetooth_gamepad/verify_zero2.py \
  --port /dev/ttyACM0
```

Po pojawieniu się komunikatu uruchom Zero 2 w trybie Android D-input przez
`B+Start`, a następnie przytrzymaj `Select`, aż dioda parowania zacznie migać.
Weryfikator zatwierdza metodę parowania zgłoszoną przez kontroler, sprawdza
zapisany deskryptor i wszystkie wejścia, wykonuje rozłączenia/ponowne
połączenia oraz pełne wyłączenie i ponowne włączenie zasilania, a następnie
utrzymuje ciągłe połączenie przez 30 minut. Podczas pierwszego ponownego
łączenia prosi o przytrzymanie jednego elementu sterującego, aby procedura
rozłączenia mogła zwolnić aktywny stan.

Weryfikator zapisuje `zero2_pico2w_c6_result.json`; wcześniejszy wynik C5
pozostaje punktem odniesienia sprzed integracji parsera. Raport C6 zawiera
wersje targetu i bibliotek, czasy, liczniki transportu, diagnostykę parsera
oraz maksymalne zajęcie pul. Nie może zawierać adresów Bluetooth, materiału
link key, tożsamości hosta, nazwy portu szeregowego ani numeru seryjnego USB.
Pliki ELF/map i lista symboli muszą również potwierdzać obecność HID Host
`ENABLE_CLASSIC`, klienta SDP, parsera HID oraz przechowywanej w pamięci bazy
kluczy połączeń, a jednocześnie
wykluczając ATT, GATT, SM, RFCOMM, serwer SDP, HID Device i profile audio.

### Sprzętowy test Bluetooth Observer

`tests/hardware/bluetooth_observer` sprawdza pasywne API BLE Observer na
Raspberry Pi Pico W, Pico 2 W oraz STM32G474 Nucleo z PIM730/RM2. Uruchamia
pasywne skanowanie w starszym trybie, opróżnia kolejkę raportów o ograniczonej
pojemności, analizuje struktury AD i zapisuje dane producenta Teltonika oraz
sygnatury iBeacon i Eddystone bez inicjowania połączenia BLE.

Skompiluj i wgraj każdą płytkę osobno:

```bash
vscode/entry/jh-vscode build \
  --project tests/hardware/bluetooth_observer \
  --target rp2040 --board picow

vscode/entry/jh-vscode build \
  --project tests/hardware/bluetooth_observer \
  --target rp2350-arm --board pico2w

vscode/entry/jh-vscode build \
  --project tests/hardware/bluetooth_observer \
  --target stm32g474 --board nucleo-g474re-pim730
```

Udane wyjście używa prefiksu `JHBL4A`. Zarejestruj co najmniej jeden raport
Teltonika EYE Beacon na każdej płytce, całkowite i odrzucone liczniki
raportów oraz podsumowanie pamięci ELF/map. Test pozostaje pasywny:
odpowiedzi skanowania, klient GATT, połączenia, parowanie i bonding są poza
zakresem tego testu.

RP2350 RISC-V jest nieobsługiwane, ponieważ jego transport Bluetooth CYW43
nie jest włączony.

### Bramka sprzętowa JH BLE Stream v1

`tests/hardware/bluetooth_stream` sprawdza publiczny cykl życia BLE oraz
uwierzytelniony strumień aplikacji na Raspberry Pi Pico W, Pico 2 W, RP2040
Pico z RM2/PIM730 oraz STM32G474 Nucleo z PIM730/RM2. Firmware reklamuje się
przez BLE jako `JH Stream HW`, wymaga stałego, testowego sekretu o długości
256 bitów i odsyła uwierzytelnione ładunki. Skrypt `verify.py` pełni na
Linuksie rolę urządzenia centralnego za pośrednictwem BlueZ.

#### Macierz buildu

Skompiluj i wgraj osobno każdą z ośmiu kombinacji układu docelowego, płytki i
środowiska wykonawczego:

| Target | Board | Runtime |
|---|---|---|
| `rp2040` | `picow` | bare-metal, FreeRTOS |
| `rp2040` | `pico-rm2` | bare-metal, FreeRTOS |
| `rp2350-arm` | `pico2w` | bare-metal, FreeRTOS |
| `stm32g474` | `nucleo-g474re-pim730` | bare-metal, FreeRTOS |

Te same osiem krotek jest zadeklarowanych jako `example.hardwareMatrix` w
manifeście stanowiska i są sprawdzane przez test układu artefaktów
repozytorium.

Warianty bare-metal zbudujesz następująco:

```bash
vscode/entry/jh-vscode build \
  --project tests/hardware/bluetooth_stream \
  --target rp2040 --board picow
vscode/entry/jh-vscode build \
  --project tests/hardware/bluetooth_stream \
  --target rp2040 --board pico-rm2
vscode/entry/jh-vscode build \
  --project tests/hardware/bluetooth_stream \
  --target rp2350-arm --board pico2w
vscode/entry/jh-vscode build \
  --project tests/hardware/bluetooth_stream \
  --target stm32g474 --board nucleo-g474re-pim730
```

Dołącz `--variant freertos` do każdej komendy buildu i wgrywania dla
obrazu FreeRTOS. Firmware testowy inicjalizuje BLE przy pierwszym wywołaniu
`app_task0()`, już po uruchomieniu planisty FreeRTOS. Stos zadania 0 ma
1024 słowa, ponieważ uwierzytelnione uzgadnianie połączenia i używane przez nie
kryptograficzne
zmienne tymczasowe przekraczają ogólny domyślny rozmiar stanowiska. Użyj tego
samego jawnie wskazanego układu docelowego, płytki i wariantu podczas
wgrywania. Pomyślny build potwierdza wyłącznie poprawność oprogramowania
i nie liczy się jako test sprzętowy.

#### Warianty obciążeniowe STM32G474 PIM730 + ILI9341

Opcjonalne warianty `display` i `display-freertos` zachowują ten sam
protokół BLE Stream i weryfikator hosta, jednocześnie ciągle aktualizując
ILI9341 podłączony do złącza SPI Arduino NUCLEO-G474RE. Wykorzystują SPI1
równolegle z dedykowanym transportem gSPI PIM730 na PB12-PB15. Te warianty
stanowią dodatkowy dowód współistnienia/obciążenia i nie zastępują żadnego z
dwóch bazowych obrazów bramki STM32 zadeklarowanych w
`example.hardwareMatrix`.

Skompiluj osobne artefakty za pomocą:

```bash
vscode/entry/jh-vscode build \
  --project tests/hardware/bluetooth_stream \
  --target stm32g474 --board nucleo-g474re-pim730 \
  --variant display

vscode/entry/jh-vscode build \
  --project tests/hardware/bluetooth_stream \
  --target stm32g474 --board nucleo-g474re-pim730 \
  --variant display-freertos
```

ILI9341 używa okablowania już zwalidowanego przez `examples/07_display_media`:

| ILI9341 | STM32G474 | Złącze Nucleo |
|---|---|---|
| `SCK` | `PA5` | CN5 pin 6 (`D13`) |
| `MISO` | `PA6` | CN5 pin 5 (`D12`) |
| `MOSI` | `PA7` | CN5 pin 4 (`D11`) |
| `CS` | `PB6` | CN5 pin 3 (`D10`) |
| `DC` | `PC7` | CN5 pin 2 (`D9`) |
| `RESET` | `PA9` | CN5 pin 1 (`D8`) |

Ekran wyświetla adres kontrolera, stan BLE/Stream, MTU, RX/TX, liczniki
odrzuceń/przepełnień/bezpieczeństwa, restarty cyklu życia, status i czas
działania. Statyczne i niezmienione pola są przerysowywane tylko wtedy, gdy
zmienia się ich wartość; RX/TX oraz status/czas działania nadal aktualizują
się raz na sekundę. Utrzymuje to trwałe obciążenie SPI1 bez wielokrotnego
przesyłania identycznych pełnych wierszy tekstu. Błąd inicjalizacji lub
aktualizacji wyświetlacza zatrzymuje normalny przebieg testu i jest zgłaszany
jako błąd cyklu życia. LCD obsługuje wyłącznie zapis, dlatego poprawność obrazu
na panelu należy potwierdzić wzrokowo.

RP2350 RISC-V jest celowo pominięte: transport Bluetooth CYW43 nie jest
włączony dla tego targetu.

#### Weryfikator sprzętowy

Uruchom weryfikator po wgraniu każdego obrazu, używając adresu wypisanego
przez stanowisko. `--target`, `--board` i `--runtime` są wymagane i muszą
opisywać wgrany obraz:

```bash
python3 tests/hardware/bluetooth_stream/verify.py \
  --address XX:XX:XX:XX:XX:XX \
  --target rp2040 \
  --board picow \
  --runtime baremetal
```

Domyślny przebieg sprawdza:

- publiczne metadane, ATT MTU, inicjalizację, rozgłaszanie, połączenie i
  uwierzytelniony strumień, w tym dokładnie jedną instancję usługi, flagi GATT
  oraz obie odmiany zapisu DATA: write-request i write-command;
- żądanie szyfrowane pełnej deinicjalizacji Stream i BLE, po którym
  następuje inicjalizacja bez resetu MCU, ze sprawdzeniem stabilnego adresu
  i generacji;
- 50 kolejnych cykli rozłączenia, ponownego połączenia, uwierzytelnienia i
  echa;
- uwierzytelniony strumień przez co najmniej 300 sekund przy docelowej
  szybkości co najmniej 10 wiadomości na sekundę, z co najmniej 90% tej
  docelowej szybkości plus sprawdzeniami sekwencji, duplikacji i
  integralności;
- nasycenie kolejki RX 12 zaszyfrowanymi ramkami, weryfikację zachowanych i
  odrzuconych ramek, jawne rozliczenie przepełnienia oraz echo po
  nasyceniu;
- przypadki błędnego dowodu, sfałszowanego tagu, ponownego użycia komunikatu
  (replay), przeskoku licznika do przodu i stopniowego wydłużania czasu między
  próbami uwierzytelnienia.

Główne parametry obciążenia są jawne i wymuszają minima akceptacji:

```bash
python3 tests/hardware/bluetooth_stream/verify.py \
  --address XX:XX:XX:XX:XX:XX \
  --target rp2040 \
  --board picow \
  --runtime baremetal \
  --reconnects 50 \
  --stream-seconds 300 \
  --stream-rate 10 \
  --saturation-frames 12 \
  --saturation-hold 5
```

`--reconnects` musi wynosić co najmniej 50, `--stream-seconds` co najmniej 300,
a `--stream-rate` co najmniej 10. Test kończy się niepowodzeniem również wtedy,
gdy rzeczywista szybkość przesyłania uwierzytelnionych wiadomości spadnie
poniżej 90% wartości `--stream-rate`. Na potrzeby wielogodzinnego testu
stabilności zwiększ czas trwania strumienia. Dla obrazu podstawowego wybierz
runtime `baremetal`, a dla wariantu manifestu `freertos` - runtime `freertos`.
Weryfikator jawnie wybiera wykrywanie LE, dlatego nieaktualny alias BlueZ nie
wpływa na wybór według adresu. Wymaga systemowych pakietów Pythona dla D-Bus i
GLib oraz pakietu `cryptography`.

#### Zarejestrowane wyniki sprzętowe - 2026-08-25

| Target i runtime | Ponowne połączenia | Ciągły uwierzytelniony strumień | Wynik |
|---|---:|---:|---|
| RP2040 Pico W, bare-metal | 50/50 | 2773 wiadomości / 300,1 s (9,24 Hz) | PASS |
| RP2040 Pico W, FreeRTOS | 50/50 | 3000 wiadomości / 300,0 s (10,00 Hz) | PASS po zażądaniu interwału połączenia 15 ms |
| RP2350 ARM Pico 2 W, bare-metal | 50/50 | 3000 wiadomości / 300,0 s (10,00 Hz) | PASS |
| RP2350 ARM Pico 2 W, FreeRTOS | 50/50 | 3000 wiadomości / 300,0 s (10,00 Hz) | PASS |
| RP2040 Pico + RM2/PIM730, oba runtime | - | - | test sprzętowy oczekuje na wykonanie |
| STM32G474 + RM2/PIM730, oba runtime | - | - | rozszerzony test sprzętowy oczekuje na wykonanie |
| STM32G474 + RM2/PIM730 + ILI9341, bare-metal | 50/50 | 2910 wiadomości / 300,1 s (9,70 Hz) | HOST PASS, w tym reset IWDG |
| STM32G474 + RM2/PIM730 + ILI9341, FreeRTOS | 50/50 | 2930 wiadomości / 300,0 s (9,77 Hz) | HOST PASS, w tym reset IWDG |

Przebiegi obciążeniowe wyświetlacza STM32G474 z 2026-08-25 użyły wariantów
`display` i `display-freertos` z PIM730 na PB12-PB15 oraz ILI9341 na SPI1.
Oba przeszły restart cyklu życia, niepilnowany reset IWDG, 50 uwierzytelnionych
ponownych połączeń, pięciominutowy strumień, nasycenie z czterema
zachowanymi i ośmioma odrzuconymi ramkami plus jednym zgłoszonym
przepełnieniem, oraz wszystkie negatywne przypadki bezpieczeństwa/odzyskiwania
z 62 unikatowymi próbami uzgodnienia sesji. Krok IWDG zmienił powód resetu `2 -> 4` oraz
identyfikator rozruchu
`7cb8c0cf4bc622a8 -> 5e78009fc6d2fb2b` w bare-metal oraz
`927f820a16299332 -> 5f2cd64b89ba00fc` w FreeRTOS, zachowując adres
`28:CD:C1:19:18:19`. Średnie/maksymalne opóźnienie ciągłego strumienia
wyniosło 103,1/331,1 ms dla bare-metal i 102,4/313,1 ms dla FreeRTOS.

Wcześniejsze przebiegi sprzed watchdoga osiągnęły dokładnie 3000 wiadomości
w 300,0 sekundy w obu runtime. Wstępny przebieg bare-metal, który
przerysowywał wszystkie dziewięć wierszy LCD co sekundę, osiągnął tylko 2660
wiadomości (8,87 Hz) i poprawnie nie przeszedł progu akceptacji 9,00 Hz.
Zachowanie niezmienionych wierszy usunęło to unikalne obciążenie, podczas
gdy RX/TX i czas działania nadal odświeżały się raz na sekundę.

Po dodaniu kontroli identyfikatora każdego rozruchu pełny test bare-metal na
Pico 2 W ponownie zaliczył 50/50 ponownych połączeń, przesłał 3000 wiadomości
w 300,0 s (10,00 Hz), a także przeszedł próbę nasycenia i wszystkie przypadki
bezpieczeństwa. Krok z watchdogiem zmienił powód resetu `3 -> 4` oraz
identyfikator rozruchu
`cb3ef2a0b00439b4 -> bc75beed8bfd5cf1`, zachowując adres
`2C:CF:67:BB:40:2E`.

Zarejestrowane udane testy wykonały publiczną procedurę restartu cyklu życia,
zachowały lokalny adres podczas resetu, zmieniły powód resetu z `3` na kod
watchdoga `4` i uwierzytelniły nową sesję. Weryfikator akceptuje dowolny powód
przed resetem, lecz po resecie wymaga kodu watchdoga `4` oraz zmiany
niezerowego, losowego identyfikatora rozruchu. Dzięki temu wcześniejszy kod
resetu watchdoga nie może spowodować uznania zwykłego restartu BLE za reset
MCU. Każdy przebieg zachował też cztery z 12 ramek
nasycenia, zgłaszając osiem odrzuceń i jedno potwierdzenie przepełnienia,
oraz przeszedł sprawdzenia sfałszowanego tagu, powtórki, luki licznika,
błędnego dowodu, backoffu i odzyskiwania świeżej sesji. Komenda watchdoga
pozwala sprawdzić samoczynne przerwanie działania przez reset MCU; nie odcina
jednak fizycznie zasilania VBUS.

Wcześniejszy obraz Pico W FreeRTOS użył stosu zadania 512 słów i resetował
się podczas pierwszego uwierzytelnionego uzgadniania sesji. Zwiększenie stosu
stanowiska do 1024 słów usunęło ten reset.

Jeden z kolejnych testów utracił jednak łącze BLE podczas ponownego łączenia, a
inny osiągnął zaledwie 8,06 Hz. Backend Stream pozostawiał wybór interwału
połączenia całkowicie urządzeniu centralnemu. W efekcie wykonywana kolejno
uwierzytelniona wymiana żądanie-powiadomienie trwała zbyt długo na RP2040 z
FreeRTOS. Gdy urządzenie peryferyjne zaczęło żądać interwału 15 ms przy zerowym
opóźnieniu peryferium (peripheral latency), pełny powtórzony test zakończył się
powodzeniem: odzyskiwanie po watchdogu, 50/50 ponownych
połączeń, 3000 wiadomości w 300,0 sekundy (10,00 Hz, 60,2/153,5 ms
średnie/maksymalne opóźnienie), nasycenie oraz wszystkie negatywne przypadki
bezpieczeństwa i odzyskiwania z 62 unikatowymi próbami uzgodnienia sesji.

Miarodajne wyniki po stronie hosta pochodzą obecnie z Linuksa i BlueZ.
Natywne wykonanie w Windows oraz integrację z projektem `lights-timer`
odłożono na później; żadna z tych funkcji nie jest wymagana do uzyskania
wyników opisanych powyżej.

#### Polecenia stanowiska testowego i zasady identyfikacji

Sterowanie tożsamością, restartem, nasyceniem i statystykami to polecenia
wyłącznie dla stanowiska testowego, przenoszone wewnątrz wzajemnie
uwierzytelnionych i zaszyfrowanych ładunków Stream DATA. Każde polecenie to
jedna kompletna ramka DATA zapisywana żądaniem `WriteValue` BlueZ do
istniejącej charakterystyki RX
`b7ce0002-3c13-4fe2-801f-d71bdab1369b`. Jej odpowiedź to jedno zaszyfrowane
powiadomienie DATA z istniejącej charakterystyki TX
`b7ce0003-3c13-4fe2-801f-d71bdab1369b`. Polecenia nie są dzielone między
zapisy GATT. Nie dodają żadnej charakterystyki i nie zmieniają protokołu
przewodowego JH BLE Stream v1.

Weryfikator dodatkowo wysyła zwykłe uwierzytelnione echo przez BlueZ
`type=command`, aby przetestować ścieżkę `write-without-response` na RX;
polecenia sterujące stanowiska używają `type=request`, dzięki czemu ich
zakończenie żądania jest obserwowalne.

| Uwierzytelniony ładunek polecenia | Odpowiedź lub efekt stanowiska |
|---|---|
| `JHBL5/IDENTITY` | `J5I1\|<target>\|<board>\|<runtime>` |
| `JHBL5/RESTART` | `JHBL5/RESTARTING`, następnie pełny restart Stream i BLE |
| `JHBL5/BOOT` | `J5B1` z następującym po nim jednym bajtem powodu resetu i 64-bitowym losowym identyfikatorem rozruchu w formacie little-endian |
| `JHBL5/POWER-LOSS` (RP i STM32G474) | `JHBL5/POWER-LOSS-ARMED`, następnie reset watchdoga bez interwencji hosta lub użytkownika |
| `JHBL5/SATURATE` + czas trwania little-endian | `JHBL5/SATURATE-READY`, następnie ograniczona pauza RX |
| `JHBL5/STATS` | zwarty binarny rekord `J5S1` potwierdzający odzyskanie |

Odpowiedź tożsamości jest kompilowana bezpośrednio z `HAL_TARGET_NAME`,
`HAL_BOARD_PROFILE_NAME` i `HAL_ENABLE_FREERTOS`; runtime to dokładnie
`baremetal` lub `freertos`. Weryfikator porównuje ją z wszystkimi trzema
wymaganymi wartościami CLI przed akceptacją wyników obciążenia. Na przykład
odpowiedź Pico W bare-metal to `J5I1|rp2040|picow|baremetal`.

Udany przebieg fizyczny kończy się `JHBL5 HOST PASS`. Log urządzenia używa
prefiksu `JHBL5` i rejestruje wynegocjowane MTU, liczniki, błędy
uwierzytelniania, odrzucenia powtórek, błędy cyklu życia, restarty oraz
straty ograniczonej kolejki.

Końcowa faza bezpieczeństwa celowo uruchamia mechanizm opóźniania kolejnych
prób uwierzytelnienia. Przez całe skonfigurowane okno 30 sekund wysyła co
najmniej raz na sekundę odrzucane komunikaty HELLO. Dopiero po upływie tego
czasu potwierdza odzyskanie nowej, uwierzytelnionej sesji i wypisuje
`JHBL5 HOST PASS`. Udany
przebieg pozostawia więc stanowisko gotowe do następnego testu bez ponownego
wgrywania firmware'u.

Wbudowany sekret i jego kopia w `verify.py` to publiczny materiał testowy.
Nigdy nie mogą być ponownie użyte przez produkt. Produkt potrzebuje unikalnego,
losowego sekretu przypisanego do urządzenia, dostarczonego niezależnym kanałem
(`out of band`) i zapisanego podczas konfiguracji urządzenia (provisioningu).

<a id="sx1262-raw-lora-hardware-gate"></a>

### Bramka sprzętowa surowego LoRa SX1262

`tests/hardware/lora_sx1262` używa możliwego do zbudowania firmware'u
[`27_lora_point_to_point`](../../../examples/27_lora_point_to_point/) oraz
dwóch modułów radiowych pracujących w tym samym paśmie. Sprawdza inicjalizację,
dwukierunkowe przesyłanie pakietów drogą radiową, ciągłość numerów sekwencji,
asynchroniczne callbacki wyzwalane przez DIO1, diagnostykę IRQ i anulowania,
metadane RSSI/SNR, usypianie i wybudzanie oraz ponowną inicjalizację po
zniszczeniu i utworzeniu obiektu radia.

W profilach ze sprzętową diodą stanu jej ciągłe świecenie oznacza
aktywność nadawania, a impuls 120 ms potwierdza odebrany pakiet.

Nie paruj urządzenia LF z urządzeniem HF. Potwierdź etykiety na obu
radiostacjach i antenach, podłącz właściwą antenę przed włączeniem zasilania,
użyj I/O 3,3 V i przestrzegaj lokalnych przepisów dotyczących widma, mocy i
cyklu pracy.

#### Para LF: dwie płytki RP2040-LoRa-LF

Skompiluj i wgraj inicjatora na pierwszą płytkę, następnie skompiluj i wgraj
respondera na drugą płytkę:

```bash
vscode/entry/jh-vscode build \
  --project examples/27_lora_point_to_point \
  --target rp2040 --board rp2040-lora-lf
vscode/entry/jh-vscode upload \
  --project examples/27_lora_point_to_point \
  --target rp2040 --board rp2040-lora-lf \
  --port /dev/serial/by-id/<lf-initiator>

vscode/entry/jh-vscode build \
  --project examples/27_lora_point_to_point \
  --target rp2040 --board rp2040-lora-lf --variant responder
vscode/entry/jh-vscode upload \
  --project examples/27_lora_point_to_point \
  --target rp2040 --board rp2040-lora-lf --variant responder \
  --port /dev/serial/by-id/<lf-responder>
```

Firmware celowo używa w tym teście częstotliwości 434,0 MHz. Jest to
konfiguracja testowa, a nie uniwersalne ustawienie zgodne z przepisami każdego
regionu.

#### Para HF: zewnętrzny Core1262-HF na RP2040 i STM32G474

Użyj stałego okablowania udokumentowanego przez profile złożone. Skompiluj
RP2040/Pico jako inicjatora oraz NUCLEO-G474RE jako respondera (lub odwróć
obie role):

```bash
vscode/entry/jh-vscode build \
  --project examples/27_lora_point_to_point \
  --target rp2040 --board pico-core1262-hf
vscode/entry/jh-vscode build \
  --project examples/27_lora_point_to_point \
  --target stm32g474 --board nucleo-g474re-core1262-hf --variant responder
```

Oba urządzenia Core1262-HF używają tego samego profilu elektrycznego modułu i
konfiguracji technicznej EU868. Mapowania pinów hosta pochodzą z danych
wygenerowanych dla płytek; przykład nie zawiera okablowania zależnego od
układu docelowego. Profil Nucleo używa SPI2 na PB13/PB14/PB15 i pozostawia
LD2/`HAL_LED_BUILTIN` na PA5.

Przed próbą transmisji radiowej można zbudować i wgrać na dowolnym hoście
wariant bez nadawania, wybierając `--variant probe`. Pomyślny wynik potwierdza
możliwości backendu, jawną kalibrację, bieżący poziom RSSI, CAD i tryb czuwania,
bez uruchamiania toru nadawczego RF.

#### Weryfikacja

Uruchom wystarczająco długo, aby zaobserwować automatyczne sondy cyklu życia
przy sekwencji 10 i 20:

```bash
python3 tests/hardware/lora_sx1262/verify_pair.py \
  --initiator-port /dev/serial/by-id/<initiator> \
  --responder-port /dev/serial/by-id/<responder> \
  --duration 75
```

Do zaliczenia potrzeba co najmniej pięciu zgodnych sekwencji ping/pong, metadanych
pakietów, znacznika asynchronicznej pętli zdarzeń na obu radiostacjach,
niezerowych liczników IRQ/callback/anulowania, `HAL_OK` snu/wybudzenia oraz
`HAL_OK` reinicjalizacji. Następnie zamień, które fizyczne urządzenie
otrzymuje wariant `responder`, i powtórz test.

Powtórz test dla dwóch deterministycznych kombinacji. Warianty
bazowy i `responder` używają SF9/10 dBm; `sf7` i `responder-sf7` używają
SF7/6 dBm. Nie zakładaj, że SF12/14 dBm jest dozwolone. Oba końce przebiegu
muszą używać pasującej rodziny wariantów. Zarejestruj etykiety
modułu/anteny, dokładne okablowanie, wersję firmware, odległość, liczby
pakietów, straty, zakres RSSI/SNR oraz JSON weryfikatora w prywatnym
raporcie sprzętowym.

### Bramka sprzętowa routera poleceń SX1262 przez LoRa

Warianty `link` i `link-responder` przykładu
[`27_lora_point_to_point`](../../../examples/27_lora_point_to_point/) dołączają
`hal_lora_commands` do jednego niezawodnego łącza. Inicjator wysyła
500-bajtowe binarne żądanie `echo` z identyfikatorem korelacji. Responder
przekazuje je przez wspólny router i zwraca identyczny ładunek. Przy domyślnych
limitach transmisja w każdym kierunku wymaga trzech niezaszyfrowanych
fragmentów łącza.

Zasady obsługi zezwalają zarówno na źródło `LORA_LINK`, jak i `BLE_STREAM`.
Ten test dostarcza tylko adapter LoRa. Wpis dotyczący źródła BLE pokazuje, że
trasę i protokół można w przyszłości wykorzystać również w adapterze BLE; nie
oznacza to, że taki adapter już zaimplementowano.

Dla dwóch zintegrowanych płytek LF, skompiluj i wgraj przeciwne role. Gdy obie
płytki są już w BOOTSEL i dlatego nie mają portu szeregowego, wybierz każdy
dysk jawnie:

```bash
vscode/entry/jh-vscode upload \
  --project examples/27_lora_point_to_point \
  --target rp2040 --board rp2040-lora-lf --variant link \
  --bootsel-volume /dev/<initiator-partition>

vscode/entry/jh-vscode upload \
  --project examples/27_lora_point_to_point \
  --target rp2040 --board rp2040-lora-lf --variant link-responder \
  --bootsel-volume /dev/<responder-partition>
```

Gdy oba urządzenia zgłoszą przez USB interfejs CDC, użyj stabilnych ścieżek
`/dev/serial/by-id/` i przechwyć co najmniej trzy kompletne transakcje:

```bash
python3 tests/hardware/lora_sx1262/verify_commands.py \
  --initiator-port /dev/serial/by-id/<command-initiator> \
  --responder-port /dev/serial/by-id/<command-responder> \
  --duration 75 --minimum-transactions 3
```

Do zaliczenia potrzebny jest oczekiwany znacznik roli z obu urządzeń oraz brak
błędu lub przekroczenia czasu `JHCMD1`. W przypadku co najmniej trzech
niezerowych identyfikatorów żądania dane inicjatora, funkcji obsługi respondera
i odpowiedzi odbieranej przez inicjator muszą być zgodne pod względem długości
500 bajtów, CRC-32 oraz liczby trzech fragmentów. Identyfikator peera funkcji
obsługi i źródło odpowiedzi muszą wynosić odpowiednio `0x1001` i `0x1002`.
Oba identyfikatory sesji muszą odpowiadać niezerowej wartości `READY` właściwej
dla swojej roli. Flagi bezpieczeństwa w niezaszyfrowanych danych muszą wynosić
zero, RSSI musi mieścić się w ujemnym zakresie LoRa, a SNR - w ustalonych
granicach. Źródłem wywołania funkcji obsługi musi być `LORA_LINK`, natomiast
licznik jej wywołań musi ściśle rosnąć. Końcowy status i porównanie bajtów muszą
zakończyć się powodzeniem. Zapisane logi można sprawdzić za pomocą
`--initiator-log` i `--responder-log` zamiast portów szeregowych na żywo.

Zamień role dwóch fizycznych urządzeń i powtórz. Zarejestruj ich etykiety
modułu i anteny, wersję firmware, odległość, dopasowane identyfikatory
żądań, zakres RSSI/SNR oraz JSON weryfikatora tylko w prywatnym raporcie
sprzętowym. Częstotliwość 434,0 MHz jest techniczną wartością testową;
podłącz anteny LF i przestrzegaj lokalnych wymogów dotyczących widma, mocy i
cyklu pracy.

### Sprzętowy test ESP32-S3 - faza 1

`tests/hardware/esp32s3_phase1` sprawdza cały podstawowy proces wyboru układu
docelowego i płytki, kompilowania, wgrywania oraz monitorowania Waveshare
ESP32-S3-Zero SKU 25081. Celowo nie obejmuje API HAL dla GPIO, portu
szeregowego, magistral, sieci ani pamięci masowej; przewidziano je w późniejszych
fazach.

Firmware podaje dokładną, wygenerowaną tożsamość układu docelowego i płytki, a
następnie sprawdza wykryty model chipu, liczbę rdzeni, fizyczny rozmiar
flash, inicjalizację PSRAM oraz fizyczny rozmiar PSRAM z danymi zapisanymi w
rejestrze płytek. Skrypt `verify_phase1.py` wyznacza oczekiwane wartości na
podstawie tych samych deskryptorów układu i płytki, po czym czeka na cykliczny raport na natywnym
porcie USB Serial/JTAG.

Użyj stabilnej ścieżki `/dev/serial/by-id/`, gdy jest dostępna. Skompiluj i
zwaliduj artefakty za pomocą produkcyjnego skryptu ESP-IDF:

```bash
python3 scripts/build_esp_idf.py build \
  --project tests/hardware/esp32s3_phase1 \
  --target esp32s3 --board waveshare-esp32-s3-zero \
  --output .build/hardware/esp32s3_phase1 --clean
python3 scripts/build_esp_idf.py artifacts \
  --project tests/hardware/esp32s3_phase1 \
  --target esp32s3 --board waveshare-esp32-s3-zero \
  --output .build/hardware/esp32s3_phase1
```

Ten sam projekt jest zarejestrowany jako projekt ESP-IDF `jh-vscode`. Ustaw `PORT`
na stabilny alias podłączonej płytki, następnie skompiluj projekt, odśwież
IntelliSense, wgraj firmware i uruchom monitorowanie przez publiczny proces:

```bash
PORT="/dev/serial/by-id/<Espressif-USB-Serial-JTAG-device>"
vscode/entry/jh-vscode config-dump \
  --project tests/hardware/esp32s3_phase1
vscode/entry/jh-vscode build \
  --project tests/hardware/esp32s3_phase1
vscode/entry/jh-vscode refresh-intellisense \
  --project tests/hardware/esp32s3_phase1
vscode/entry/jh-vscode upload \
  --project tests/hardware/esp32s3_phase1 --port "$PORT"
vscode/entry/jh-vscode monitor \
  --project tests/hardware/esp32s3_phase1 --port "$PORT" \
  --lock-policy replace-own
```

Wybrane urządzenie musi mieć VID/PID USB Serial/JTAG zgodne z profilem płytki:
`303a:1001`. Aby przetestować przekazanie wgrywania, pozostaw monitor
uruchomiony i wywołaj tę samą komendę `upload` z drugiego terminala. Wgranie
musi zatrzymać monitor wyłącznie tego projektu, wgrać wszystkie trzy obrazy z
manifestu, zresetować płytkę i umożliwić monitorowi ponowne połączenie.

Zatrzymaj monitor przed uruchomieniem samodzielnego weryfikatora, ponieważ
obie komendy wymagają wyłącznego dostępu do portu szeregowego:

```bash
python3 tests/hardware/esp32s3_phase1/verify_phase1.py \
  --port "$PORT"
```

Udany przebieg wypisuje jeden obiekt JSON z `"phase": "task0"`, sekwencją co
najmniej jeden oraz `"status": "PASS"`.

#### Zweryfikowany punkt odniesienia fazy 1

Pełny test sprzętowy zakończył się czystym buildem ESP-IDF obejmującym 555
kroków. Obraz aplikacji miał 150544 bajtów, a 86% partycji pozostało wolne.
Podczas każdego z trzech pełnych wgrań zapisano bootloader, tabelę partycji i
obraz aplikacji. Dane zgłoszone podczas działania odpowiadały ESP32-S3 z dwoma
rdzeniami, 4194304 bajtami fizycznej pamięci flash oraz zainicjalizowaną pamięcią
Quad PSRAM o pojemności 2097152 bajtów. Trwały monitor ESP również zwolnił port
do wgrywania,
ponownie połączył się po resecie i wznowił powtarzane bicie serca
`app_task0()`.

Ten wynik potwierdza działanie procesu wyboru układu docelowego i płytki,
kompilowania, wgrywania oraz monitorowania
fazy 1 dla Waveshare ESP32-S3-Zero SKU 25081. Nie rozszerza wsparcia na
API GPIO, portu szeregowego, magistrali, sieci, pamięci masowej ani
opcjonalnego drugiego zadania, przypisane do fazy 2.

### Sprzętowy test ESP32-S3 - faza 2

`tests/hardware/esp32s3_phase2` sprawdza HAL peryferiów fazy 2 w profilu
`waveshare-esp32-s3-zero`. Wymaga jedynie natywnego kabla USB płytki; żaden
zewnętrzny czujnik, zworka ani urządzenie SPI/I2C nie jest wymagane.

Firmware sprawdza:

- czas systemowy, architekturę, UID, stertę, temperaturę układu, watchdog,
  zapis i odtworzenie informacji o błędzie zachowanej po restarcie oraz
  działanie włączonej ochrony stosu FreeRTOS;
- muteksy FreeRTOS, sekcje krytyczne oraz przypisanie `app_task0` i `app_task1`
  do rdzeni 0 i 1;
- wejście GPIO z podciągnięciem (pull-up), wyjście/odczyt zwrotny oraz
  przekonfigurowane przerwanie GPIO przypisane do tego samego rdzenia;
- odczyty 12-bitowego ADC rozstawione przez wewnętrzne podciągnięcie/
  podwieszenie (pull-down/pull-up) GPIO;
- sprzętowy UART1 TX/RX przez jeden pin pętli zwrotnej macierzy GPIO;
- czyszczenie magistrali mastera I2C, inicjalizację oraz kompletne
  skanowanie adresów (zero wykrytych urządzeń jest prawidłowe dla
  nieokablowanej płytki);
- transakcje mastera SPI2, blokujące DMA oraz synchroniczną ścieżkę zapasową
  dla asynchronicznego DMA, bez zakładania odebranych danych od nieobecnego
  slave'a;
- kontrolowane wstrzymywanie i wznawianie GPTimer z dedykowanej puli,
  powtarzane callbacki ISR oraz poprawne zwalnianie zasobów;
- dwukierunkowy ruch debugowania przez natywny VFS USB Serial/JTAG konsoli
  startowej.

Skompiluj projekt i wygeneruj przenośny manifest artefaktów:

```bash
python3 scripts/build_esp_idf.py build \
  --project tests/hardware/esp32s3_phase2 \
  --target esp32s3 \
  --board waveshare-esp32-s3-zero \
  --name jh_esp32_phase2_hardware \
  --clean

python3 scripts/build_esp_idf.py artifacts \
  --project tests/hardware/esp32s3_phase2 \
  --target esp32s3 \
  --board waveshare-esp32-s3-zero \
  --name jh_esp32_phase2_hardware
```

Użyj stabilnego aliasu `/dev/serial/by-id/...` płytki na Linuksie (lub jej
portu COM na Windows) zarówno dla flashowania, jak i weryfikacji:

```bash
python3 scripts/build_esp_idf.py flash \
  --project tests/hardware/esp32s3_phase2 \
  --target esp32s3 \
  --board waveshare-esp32-s3-zero \
  --name jh_esp32_phase2_hardware \
  --port /dev/serial/by-id/usb-Espressif_USB_JTAG_serial_debug_unit_SERIAL-if00

python3 tests/hardware/esp32s3_phase2/verify_phase2.py \
  --port /dev/serial/by-id/usb-Espressif_USB_JTAG_serial_debug_unit_SERIAL-if00
```

Weryfikator wysyła `PING` i akceptuje wyłącznie kompletny raport
`status=PASS`. Brakujący callback, zły rdzeń, zablokowana ścieżka RX,
wynik ADC poza zakresem lub błąd peryferium nie mogą więc zostać omyłkowo
zgłoszone jako pomyślny wynik podstawowego testu.

Firmware testowy celowo nie wywołuje `hal_enter_bootloader()`: udane przejście
do trybu pobierania resetuje MCU i wymaga osobnego testu ponownego połączenia
oraz odzyskiwania. Projekt fazy 3 sprawdza obecność symbolu podczas buildu i
linkowania, natomiast sam reset należy do kampanii sprzętowej fazy 3.5.

#### Zarejestrowany stan fazy 2

Wcześniejsza wersja przeszła pełny test sprzętowy na Waveshare ESP32-S3-Zero.
Zadania `task0` i `task1` były przypisane do rdzeni 0 i 1, dwa callbacki GPIO
działały w kontekście ISR, a odczyty pull-down/pull-up ADC wyniosły 37/4095.
Pętla zwrotna UART GPIO i SPI przeszły, nieokablowane skanowanie I2C zwróciło
`HAL_OK` z zerem urządzeń, 20 callbacków GPTimer z domyślnej puli działało w
kontekście ISR, a dwukierunkowe USB Serial/JTAG oraz sprawdzenia
systemowe/synchronizacji przeszły.

Ten wcześniejszy wynik obejmuje wyłącznie pierwotny podzbiór testów. Obecna
wersja wymaga dedykowanej puli timerów i zaimplementowanej ochrony stosu, lecz
tych przypadków nie sprawdzono jeszcze ponownie na sprzęcie. Obsługa układu
docelowego przez I2C, PWM/PWM_FREQ, RMT/RGB, PCNT, wejście w tryb pobierania,
sprzętowe testy wymuszające błędy stosu i inne usterki oraz odtwarzanie
informacji o błędzie zachowanej po restarcie także pozostają częścią kampanii
sprzętowej fazy 3.5.

## Projekty do sprawdzania buildu i linkowania firmware'u

### Projekt sprawdzający build i linkowanie ESP32-S3

| Projekt testowy | Zakres |
|---|---|
| `tests/fixtures/esp32s3_phase3` | Projekt ESP-IDF przeznaczony wyłącznie do sprawdzania buildu, wybierający każdy backend ESP32-S3 dostarczony w fazie 3. Sprawdza dobór funkcji, źródeł i zależności, build, linkowanie, generowanie partycji `two-ota-large` oraz publikowanie artefaktów. |

Ten projekt jest kompilowany przez CI i lokalny etap kontroli jakości nr 8.
Pomyślny build nie potwierdza działania WiFi, gniazd, TLS, usług, OTA ani
WireGuard w czasie wykonywania, podobnie jak nowych peryferiów fazy 2. Te
obszary wymagają osobnych testów sprzętowych, cyklu życia i negatywnych
przypadków bezpieczeństwa.

---

## Architektura testów hosta

### Jak to działa

Konfiguracja CMake w katalogu głównym projektu kompiluje bibliotekę statyczną
`hal_mock` z:

- wszystkich zaślepek `src/hal/impl/.mock/*.cpp`,
- niezależnych od backendu źródeł HAL w `UTIL_SOURCES` (patrz
  `CMakeLists.txt`), w tym pozostałych współdzielonych adapterów statusu
  MQTT/WireGuard w `hal_network_status.cpp`, fasad HAL, warstw
  kompatybilności, przenośnych driverów urządzeń i dołączonych bibliotek,
- `src/utils/unity.c` (warstwa integracyjna Unity).

Dokładną listę zawiera zbiór `UTIL_SOURCES` w `CMakeLists.txt`; jest on
miarodajnym źródłem danych.

Każdy program testowy w `tests/` jest linkowany wyłącznie z `hal_mock`, bez
nagłówków Pico SDK i bez dostępu do sprzętu.

Zarządzany framework Unity 2.5.4 znajduje się w `third_party/Unity/src`.
Śledzona integracja JaszczurHAL składa się z:

- `src/utils/unity.c`
- `src/utils/unity.h`
- `src/utils/unity_internals.h`
- `src/utils/unity_config.h`

Konfiguracja CMake dla hosta kompiluje warstwę `src/utils/unity.c` jako część
`hal_mock` oraz włącza `HAL_ENABLE_UNITY` i `UNITY_INCLUDE_CONFIG_H`. Źródła testów
dołączają `"utils/unity.h"` i używają lokalnego dla repozytorium
`unity_config.h`. Uruchom `scripts/ensure_unity.sh` lub główny skrypt
aktualizujący komponenty, aby odtworzyć wersję źródeł wskazaną przez
repozytorium. Poza buildem
testowym Unity pozostaje nieaktywne, chyba że jawnie włączono
`HAL_ENABLE_UNITY`.

Plik `tools.cpp` sprawdza `test_tools` z użyciem mocków HAL.
Plik `multicoreWatchdog.cpp` sprawdza `test_multicoreWatchdog` z użyciem
lokalnej zaślepki zamknięcia loggera i mocków HAL.
`utils/draw7Segment.cpp` nie ma zależności platformowych
(czysty `const char*` + `hal_display`).

### Przykłady Unity

Minimalny plik testowy:

```cpp
#include "utils/unity.h"

void setUp(void) {}
void tearDown(void) {}

void test_adds_numbers(void) {
    TEST_ASSERT_EQUAL_INT(4, 2 + 2);
}

int main(void) {
    UNITY_BEGIN();
    RUN_TEST(test_adds_numbers);
    return UNITY_END();
}
```

Test używający mocków HAL:

```cpp
#include "utils/unity.h"
#include "hal/system/hal_system.h"
#include "hal/impl/.mock/hal_mock.h"

void setUp(void) {
    hal_mock_set_millis(0);
}

void tearDown(void) {}

void test_delay_ms_updates_mock_time(void) {
    hal_delay_ms(10);

    TEST_ASSERT_EQUAL_UINT32(10, hal_millis());
    TEST_ASSERT_EQUAL_UINT32(10000, hal_micros());
}

int main(void) {
    UNITY_BEGIN();
    RUN_TEST(test_delay_ms_updates_mock_time);
    return UNITY_END();
}
```

Prosta rejestracja CMake:

```cmake
add_hal_test(test_my_module)
```

Oczekuje to `tests/test_my_module.cpp` i linkuje go z `hal_mock`.

Gdy test wymaga dodatkowych plików implementacji, utwórz dedykowany target:

```cmake
add_executable(test_my_driver
    test_my_driver.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../src/hal/sensors/my_driver/my_driver.cpp
)
target_link_libraries(test_my_driver PRIVATE hal_mock)
add_test(NAME test_my_driver COMMAND test_my_driver)
```

Uruchom tylko nowy zestaw:

```bash
cmake --build .build/host --target test_my_module
ctest --test-dir .build/host -R test_my_module --output-on-failure
```

### Przewodnik po zestawach testów

`tests/CMakeLists.txt` jest autorytatywnym spisem testów. Sprawdź zestaw
zarejestrowany w bieżącym checkout za pomocą:

```bash
ctest --test-dir .build/host -N
```

Poniższa tabela jest przewodnikiem pokrycia dla reprezentatywnych i
zgrupowanych zestawów; celowo nie jest drugim wyczerpującym rejestrem
testów.

| Zestaw | Co obejmuje |
|---|---|
| `test_hal_gpio` | tryby pinów, odczyt/zapis, wstrzykiwanie poziomu, podłączanie/odłączanie przerwań |
| `test_hal_adc` | konfiguracja rozdzielczości, wstrzyknięcie + odczyt |
| `test_hal_pwm` | konfiguracja rozdzielczości, zapis |
| `test_hal_timer` | dodawanie i anulowanie alarmów niskiego poziomu, diagnostyka `_ex` oraz uruchamianie, zatrzymywanie, wstrzymywanie i wznawianie zarządzanego timera, a także ustawianie okresu i odczyt pozostałego czasu |
| `test_stm32_hal_timer` | rzeczywisty backend timera STM32G474 w symulacji hosta: alarmy jednorazowe, ponowne planowanie z poziomu callbacku, anulowanie, limity i niszczenie puli, dzielenie długiego opóźnienia na części oraz kontrolowane zatrzymanie, wstrzymanie i wznowienie |
| `test_hal_ds18b20` | nieblokujący cykl żądania, odpytywania i pobierania ostatniego wyniku, zachowanie stanu zajętości, obsługa CRC i wykrywania obecności |
| `test_hal_dht` | zależności czasowe transakcji GPIO DHT, obsługa sumy kontrolnej, funkcje odczytujące zapisaną próbkę i odtwarzanie stanu sekcji krytycznej |
| `test_hal_onewire` | adaptery operacji `reset`/`read`/`write`/`select`/`search`, funkcje pomocnicze CRC8/CRC16 i blokowanie magistrali w mocku |
| `test_hal_rtc` | inicjalizacja, odczyt i ustawianie daty oraz czasu RTC, zgłaszanie wewnętrznego lub zewnętrznego źródła zegara, wybór providera innego niż I2C, pełna walidacja kalendarza gregoriańskiego, wartości graniczne lat 1970/2000/2099 i przepełnienie, flaga integralności, maska przerwań, flagi zdarzeń czyszczone przy odczycie, jednorazowe i zapamiętywane wybudzenie względne, konfiguracja CLKOUT, timera i alarmu, starsze zabezpieczenia przed nieprawidłowymi danymi oraz odwzorowanie statusu `_ex` |
| `test_jh_rtc_i2c_provider` | wybór wspólnego providera PCF8563/DS3231, metadane, konwersja daty, czasu i zdarzeń, bezpieczne sterowanie CLKOUT układu DS3231, propagacja błędów I2C i odwzorowanie funkcji obsługiwanych przez backend na status z użyciem mocka HAL I2C |
| `test_stm32_rtc_codec` | Kodowanie rejestrów BCD TR/DR i Alarmu A RTC STM32G474, odrzucanie kalendarza/zakresu, ograniczenia dzień-kontra-dzień tygodnia, preskalery LSE/LSI 1 Hz oraz zaokrąglanie/granice licznika wybudzenia |
| `test_rtc_architecture` | jedna fasada RTC, wyraźny podział między providerem I2C i wewnętrznym, wspólna walidacja i blokowanie, drivery układów korzystające wyłącznie z HAL, wybór timera wybudzenia STM32G474 i alarmu AON RP oraz poprawny manifest źródeł |
| `test_hal_power` / `test_hal_power_header_c` | obsługiwane funkcje zasilania, walidacja żądań, kolejność callbacków, wybudzenie RTC, monotoniczny upływ czasu, zachowanie mocka odpowiadające resetowi, czyszczenie i zgodność nagłówka C |
| `test_power_architecture` | rozdzielenie odpowiedzialności RTC i zasilania, obecność backendu dla układu docelowego, odwzorowanie trybów STOP/Standby STM32, integracja AON RP, wygenerowana zależność funkcjonalna oraz poprawne źródła przykładu |
| `test_jh_calendar` | walidacja gregoriańskich lat przestępnych, długości miesięcy i dni tygodnia, niemożliwe daty, początek epoki Uniksa, konwersja dnia przestępnego w obie strony, górna granica RTC i statusy przepełnienia 64-bitowego |
| `test_calendar_architecture` | jedno wspólne źródło implementacji kalendarza, starsze adaptery czasu korzystające wyłącznie z HAL oraz wykrywanie algorytmów kalendarza powielonych lokalnie dla układu docelowego lub drivera, w tym kopii `hal_time_from_components()` |
| `test_hal_eeprom` | zapis i odczyt bajtu oraz wartości typu `int`, flaga `commit` |
| `test_hal_serial` | granice danych przesyłanych przewodowo i komunikatów portu szeregowego, wstrzyknięcie binarnych danych RX oraz operacje `available`/`read`, prefiksy debugowania zadania/ISR, akceptowane i odrzucane znaczniki czasu, konfiguracja i cykl życia ogranicznika szybkości oraz izolacja jego źródeł, strumieniowe formatowanie przekraczające `HAL_DEBUG_BUF_SIZE`, odroczone w ISR podsumowania bufora pierścieniowego i odrzuconych danych, działanie wyciszenia i `flush` |
| `test_hal_serial_session` | cykl życia ramek HELLO/AUTH, deterministyczne i kolejne losowe wartości challenge, bezpieczne odrzucenie operacji przy braku entropii, czyszczenie wartości challenge, zgodność poleceń, obsługa nieznanej funkcji obsługującej, echo numeru sekwencji, odrzucanie uszkodzonych ramek i bezpieczna obsługa argumentów null |
| `test_hal_serial_commands`, `test_hal_serial_commands_header_c`, `test_hal_serial_commands_header_cpp` | odrzucanie poleceń wymagających aktywnej sesji i obsługa wybranych poleceń przed HELLO, parsowanie nazwy i argumentów SC, metadane sekwencji, sesji i uwierzytelniania, odpowiedzi tekstowe o dokładnie oczekiwanej treści, starszy sposób formatowania, prefiks zastępczy, granice ładunku, cykl życia callbacków, bezpieczne ponowne wejście w funkcję oraz samodzielne nagłówki C/C++ |
| `test_hal_sc_auth` | stabilne wektory klucza i odpowiedzi właściwe dla danego urządzenia, czyszczenie wyjścia przy nieprawidłowych danych wejściowych oraz wspólne porównywanie MAC w stałym czasie |
| `test_jh_security_primitives` | bezpieczne zerowanie, porównywanie zgodnych i niezgodnych danych w stałym czasie, deterministyczny wektor entropii w mocku i czyszczenie wyjścia przy błędzie |
| `test_security_architecture` | poprawne dołączenie Serial Session i uwierzytelniania, jedna wspólna implementacja entropii, zerowania i porównania w stałym czasie, użycie jej przez BLE oraz poprawny manifest źródeł |
| `test_serial_architecture` | jeden wspólny rdzeń obsługi portu szeregowego i debugowania, trzy kompletne porty transportu dołączane podczas linkowania, brak powielonej implementacji dla układu docelowego oraz poprawny manifest źródeł |
| `test_hal_swserial` | status powodzenia lub błędu programowego UART, wyczerpanie puli, wstrzykiwanie RX, przechwytywanie TX, format ramki i ponowne przypisanie pinów |
| `test_rp2040_swserial_backend` | kontrola wyboru źródeł RP2040: wymagane programy PIO z Pico SDK; zabronione własne implementacje adaptera portu szeregowego, callbacki RX GPIO, mikrosekundowe opóźnienia bitów i sekcje krytyczne HAL |
| `test_hal_uart` | wstrzyknięcie RX sprzętowego UART, przechwycenie TX, przypisanie pinów |
| `test_hal_spi` | inicjalizacja i ponowna inicjalizacja SPI, reset, blokady dla każdej magistrali, transfery, walidacja statusu i odwzorowanie błędów DMA |
| `test_hal_lora_radio_lifecycle` | limity przydzielania nieprzezroczystych uchwytów, nieaktualne uchwyty, czyszczenie po zakończeniu cyklu życia i propagacja błędów providera |
| `test_hal_lora_radio` | profile i presety SX1262, ograniczenia modelu SX1261, blokujące TX, ograniczone czasowo i ciągłe odpytywanie RX, diagnostyka przepełnienia, CRC i timeoutu, stan zasilania, czas transmisji radiowej oraz dwie połączone radiostacje w mocku |
| `test_hal_lora_link` | ustawienia domyślne i cykl życia łącza, nieaktualne uchwyty, adresowana fragmentacja danych jawnych i AEAD, utrata i retransmisja ACK, ograniczony timeout wraz z maksymalną liczbą ponowień, pomijanie ponownie dostarczonych danych, integralność całej wiadomości, niepełne składanie fragmentów otrzymanych poza kolejnością, uszkodzone pakiety podczas oczekiwania na ACK, odzyskiwanie po rozpoczęciu od późniejszego fragmentu i szeregowanie równoczesnego wysyłania przez połączone radiostacje w mocku |
| `test_lora_link_frame` | ścisłe, wersjonowane formaty ramek, pojemność danych jawnych i ich konwersja w obie strony, szyfrowanie uwierzytelnione, odrzucanie zmodyfikowanego nagłówka lub szyfrogramu, kodowanie ACK, obcinanie danych i granice wyjścia |
| `test_hal_lora_link_header_c`, `test_hal_lora_link_header_cpp` | Samodzielna kompatybilność publicznego nagłówka łącza w C11 i C++17 |
| `test_lora_link_plain_compile` | rygorystyczny build kodeka łącza i ramki bez opcjonalnej kryptografii, z ostrzeżeniami traktowanymi jako błędy |
| `test_sx126x_adapter` | wykonywanie poleceń oficjalnego drivera we właściwej kolejności, wybór PA i OCP dla SX1261/SX1262, czyszczenie transakcji SPI, terminy BUSY, poziomy przełącznika RF, konfiguracja elektryczna, kalibracja pasma, timeout TX i odwzorowanie IRQ błędu CRC podczas RX |
| `test_hal_lora_sx127x` | walidacja deskryptora właściwego dla modelu SX1276/SX1278 oraz cykl życia wspólnej fasady, obsługiwane funkcje, granica kalibracji, TX, RX, CAD i stany zasilania |
| `test_sx127x_adapter` | Transport rejestrów SX127x, sonda wersji, konfiguracja modemu/częstotliwości/PA, mapowanie IRQ/status, metadane FIFO, RSSI, CAD, timeout, anulowanie, błędy magistrali i zachowanie sen/wybudzenie TCXO |
| `test_hal_pga2311` | Walidacja statusu/konfiguracji PGA2311, wyczerpanie puli, wstrzyknięte błędy SPI i ponowienie, zapisy ramek, konwersja dB/kod, zachowanie wyciszenia programowego/sprzętowego |
| `test_irsmall_decoder_driver` | Dekodowanie ramek NEC/NECx/SIRC/Samsung IRsmallDecoder, dekodowanie tabeli przejść RC5 wraz z rozszerzonym bitem polecenia, raportowanie powtórzenia/przytrzymania, reset timeoutu i ścieżki wyłączenia/włączenia przerwania |
| `test_hal_i2c` | ścieżki transferu i statusu magistral bus0/bus1, bezpośrednie funkcje pomocnicze odczytu, blokowanie, inicjalizacja i deinicjalizacja, czyszczenie magistrali, ograniczone wyniki skanowania, działanie trybu zliczania i przepełnienia oraz pokrycie callbacku dla każdego adresu |
| `test_hal_rgb_led` | init/init_ex zorientowane na status, nieprawidłowa konfiguracja, błąd alokacji/transportu, ponowienie, ograniczenie jasności, wyłączenie i strażnik przed inicjalizacją |
| `test_hal_display` | API wyświetlacza zwracające status, obsługiwane funkcje i reguły surowego zapisu, formatowanie i rozmiar tekstu, presety, rysowanie, inicjalizacja SSD1306, stan transmisji strumieniowej i asynchronicznego DMA, walidacja oraz wstrzyknięte błędy I/O backendu |
| `test_hal_can` | wysyłanie/odbieranie, bufor pierścieniowy, strażnik null-data, ograniczenie ładunku, wybór backendu, walidacja ramki classic-kontra-FD, API filtrów, `hal_can_process_all`, `hal_can_create_with_retry`, `hal_can_encode_temp_i8` |
| `test_hal_thermocouple` | wstrzyknięcie MCP9600 + MAX6675, zwroty NAN dla nieobsługiwanej operacji, rozdzielczość ADC, włącz/wyłącz, alert/status |
| `test_max6675_driver` | wspólne dekodowanie surowych danych MAX6675, wykrywanie otwartego obwodu, konfiguracja pinów GPIO i programowa sekwencja odczytu bit-bang |
| `test_mcp9600_driver` | obsługa identyfikatorów układów MCP9600/MCP9601 przez wspólną implementację, transakcje rejestrowe, dekodowanie stałoprzecinkowe, rozszerzenie znaku ADC, zachowanie bitów konfiguracji, alerty i status oraz starsze odwzorowanie rozdzielczości temperatury otoczenia |
| `test_bh1750_driver` | wspólne polecenie inicjalizacji BH1750, opóźnienie pierwszego pomiaru, wybór magistrali I2C i dwubajtowe dekodowanie natężenia oświetlenia w luksach |
| `test_adp5360_driver` | walidacja identyfikatora układu ADP5360 we wspólnej implementacji, operacje rejestrowe ładowarki, fuel gauge i regulatora, konwersja statusu, błędy I2C i pokrycie muteksu instancji |
| `test_simple_io_drivers` | wspólne sekwencje inicjalizacji MCP23017/PCA9654E/PCF8574/74HC595/MCP3221/MCP4725, zapis i odczyt pojedynczego pinu oraz całego portu, konfiguracja odwrócenia, podciągania i IRQ, a także pokrycie muteksu instancji |
| `test_hd44780_driver` | wspólna inicjalizacja GPIO HD44780, ramkowanie poleceń 4- i 8-bitowych, przesunięcia wierszy kursora, zapisy CGRAM, operacje `print`/`write` i pokrycie muteksu instancji |
| `test_hal_dma_pwm_audio` | cykl życia mocka DMA dla dźwięku PWM, wywoływanie callbacków, wstrzymywanie, wznawianie i interpolacja |
| `test_dacless_driver` | normalizacja konfiguracji wspólnego drivera DACless, ponowne wypełnianie przez callback próbki lub bloku DMA oraz przez odpytywanie, bufor ADC, wyciszanie i przywracanie dźwięku, funkcje pomocnicze interpolacji i pokrycie muteksu |
| `test_tsc2007_driver` | wspólny format bajtu poleceń TSC2007, dekodowanie 12-bitowej odpowiedzi, sekwencja odczytu dotyku, odrzucanie niestabilnych pomiarów, wybór magistrali i pokrycie muteksu instancji |
| `test_stmpe610_driver` | wspólna sekwencja konfiguracji STMPE610, odczyt identyfikatora układu, transakcje I2C, SPI i rejestrowe, dekodowanie FIFO, programowa ścieżka bit-bang SPI i pokrycie muteksu instancji |
| `test_ads1x15_driver` | wspólna konfiguracja rejestrów ADS1X15, odczyty wyników konwersji ADS1115/ADS1015, odwzorowanie wzmocnienia, trybu i szybkości danych, zapisy progu komparatora oraz przekazywanie częstotliwości zegara I2C |
| `test_hal_external_adc` | konfiguracja zakresu ADS1115, surowe i skalowane odczyty z każdego kanału oraz bezpieczna obsługa wartości spoza zakresu |
| `test_hal_gps` | wspólna publiczna ścieżka kodowania NMEA i odczytu danych, wstrzykiwanie lokalizacji, prędkości, daty, czasu i rozszerzonych danych pozycji, flagi poprawności, aktualizacji i wieku danych, reset oraz diagnostyka |
| `test_gps_architecture` | jedna fasada transportu GPS, brak kopii zależnych od układu docelowego, wspólna implementacja funkcji odczytujących i parsera, jasno wydzielone wstrzykiwanie danych przez mock oraz poprawny manifest źródeł |
| `test_hal_system` | działanie opóźnień oraz liczników milisekund i mikrosekund, nieblokujące funkcje pomocnicze `hal_millis_interval_*` odporne na zawijanie licznika (warianty dla upływu czasu i callbacku), flagi watchdoga, funkcje pomocnicze sterty i temperatury układu, niezależne od typu `hal_constrain`/`hal_map` (w tym zabezpieczenie dla równych granic zakresu), `COUNTOF`, `hal_u32_to_bytes_be`, `NONULL` |
| `test_hal_bits` | makra pomocnicze bitów (`is_set`, `set_bit`, `clr_bit`, `bitSet`, `bitClear`, `bitRead`, `set_bit_v`, `clr_bit_v`) |
| `test_hal_wifi` | tryb/nazwa hosta/RSSI/ping, wstrzyknięcie IP/DNS/MAC, walidacja wejścia |
| `test_hal_net` | wspólny format punktu końcowego i statusu, limity sieci oraz działanie mechanizmu rozpoznawania literału IPv4, nazwy localhost i testowego DNS |
| `test_hal_littlefs` | montowanie i odmontowywanie przez wspólną fasadę, wielokrotne bezpieczne montowanie, czyszczenie stanu zamontowania po błędzie odmontowania, szeregowanie równoczesnych operacji cyklu życia i statystyk, statystyki rozmiaru i inicjalizacja wyjścia, funkcje sprawdzające istnienie i usuwające ścieżkę, dokładna propagacja statusu providera, wynik destrukcyjnego formatowania i próby ponownego montowania bez gwarancji powodzenia, konfiguracja callbacku postępu oraz walidacja wejścia |
| `test_jh_littlefs_lfs_provider` | rzeczywisty cykl życia wersji littlefs wskazanej w repozytorium, działającej na backendzie RAM zachowującym się jak pamięć flash, zachowanie operacji `format`/`mount`/`file`/`stat`/`remove`/`size`, sprawdzanie granic, wyrównania i przepełnienia bloków, reguły programowania pamięci flash oraz wstrzyknięty błąd surowego I/O i odzyskiwanie po nim |
| `test_littlefs_architecture` | jedno miejsce definiujące publiczne zachowanie LittleFS i cykl życia biblioteki, jedynie geometria i operacje blokowe w implementacjach układów docelowych, oddzielenie mocka od stanu fasady, wspólna serializacja dostępu do flash STM32G474 oraz poprawny wykaz źródeł |
| `test_hal_sdlogger` | numerowanie plików przechowywane w EEPROM, buforowane opróżnianie i zamykanie pliku dziennika, formatowanie raportu o awarii oraz ścieżki błędów karty SD i otwierania pliku |
| `test_hal_udp` | przebieg operacji `begin`/`parse`/`read`, rozdzielenie wiązania, RX i TX wielu gniazd opartych na uchwytach, częściowe odczyty datagramów, zapamiętywanie zdalnego punktu końcowego i jego resetowanie przy zatrzymaniu, warianty `beginPacket` z jawnym adresem i adresem ostatniego nadawcy, działanie `write`/`endPacket` oraz walidacja wejścia |
| `test_hal_tcp` | łączenie, wysyłanie, odbieranie i zamykanie klienta TCP, wiązanie, nasłuchiwanie oraz akceptowanie połączeń przez serwer, limity kolejki oczekujących połączeń i puli, sprawdzanie gotowości oraz niezależność zaakceptowanego gniazda |
| `test_hal_http_server` | wybór tras HTTP, parsowanie zapytania, treści i nagłówków, trasy dokładne i prefiksowe, nagłówki i treść odpowiedzi, obsługa HEAD, błędy funkcji obsługującej oraz nieprawidłowa konfiguracja |
| `test_hal_http_files` | udostępnianie plików przez HTTP z użyciem callbacku, odwzorowanie typów MIME, ETag/`If-None-Match`, surowy PUT, wysyłanie plików w formacie `multipart` i odrzucanie prób wyjścia poza dozwoloną ścieżkę |
| `test_hal_websocket` | uzgadnianie połączenia przez HTTP Upgrade, `Sec-WebSocket-Accept`, maskowane ramki tekstowe, rozsyłanie do wszystkich klientów, ping/pong, callbacki zamknięcia i odrzucanie nieprawidłowego uzgadniania |
| `test_hal_net_console` | uruchamianie konsoli TCP wymagającej hasła i przebieg uwierzytelniania, przekazywanie komunikatów portu szeregowego i debugowania do uwierzytelnionych klientów, rozsyłanie do wielu klientów, dwukierunkowe przekazywanie poleceń, odpowiedzi dla poszczególnych klientów oraz callbacki rozłączenia |
| `test_hal_net_commands` | rejestracja i obsługa poleceń JSON oraz tekstowych, integracja tras HTTP i wiadomości WebSocket, ustrukturyzowane błędy oraz walidacja API |
| `test_hal_notify` | walidacja fasady powiadomień, wybór backendu testowego, sprawdzanie ważności uchwytu na podstawie generacji, treść JSON żądania Telegram, odrzucenie publicznego hosta HTTP i odwzorowanie limitu częstotliwości |
| `test_bsd_sockets` | odwzorowanie deskryptorów plików przez adapter BSD/POSIX, konwersja `sockaddr`, obsługa `errno`/EAI, komunikacja TCP/UDP, tryb nieblokujący, `select()`, `getaddrinfo()` i `setsockopt()` |
| `test_bsd_socket_headers_c` | przenośne deklaracje, stałe i struktury C dla nagłówków socket BSD; działa pod hostami zbliżonymi do GNU oraz MSVC |
| `test_hal_tls` / `test_bearssl_provider` | publiczny cykl życia TLS, natywny transport HAL TCP, ograniczony postęp przetwarzania BearSSL i opcjonalne callbacki TLS korzystające z BSD |
| Testy kompilacji TLS/BSD | potwierdzają, że TLS kompiluje się bez BSD, BSD kompiluje się bez TLS, a każda flaga włącza tylko wymagane przez nią moduły sieciowe |
| `test_bsd_sockets_c_compile` | podstawowy test buildu i linkowania C dla nagłówków gniazd, `netdb.h`, interfejsów klienta i serwera TCP/UDP oraz funkcji `fcntl()`, `select()`, `getaddrinfo()` i `setsockopt()` |
| `test_hal_wireguard` | walidacja parsera IPv4, ścieżki `begin`/`begin_advanced`/`kick` WireGuard przy danych wejściowych w postaci tablicy bajtów lub tekstu, raportowanie punktu końcowego aktywnego peera (`hal_wireguard_peer_up` + `hal_wireguard_peer_up_quick`), wyzwalanie uzgadniania przez `kick` oraz walidacja wejścia |
| `test_hal_mqtt` | konfiguracja serwera i połączenia, przechwytywanie operacji `publish`/`subscribe`/`unsubscribe`, wywoływanie callbacku przez `hal_mqtt_loop` oraz odrzucanie nieprawidłowych danych wejściowych |
| `test_hal_network_status` | Walidacja API statusu WiFi/DNS, TCP/UDP, MQTT i WireGuard między modułami, inicjalizacja wyjścia, wyczerpanie puli, mapowanie stanu i błędu |
| `test_hal_ota` | ustawianie konfiguracji OTA, przebieg `begin`/`is_started`, status i potwierdzenie rozruchu, wywoływanie callbacku dla wstrzykniętych zdarzeń `start`/`progress`/`error`/`end`, zastępowanie i wyrejestrowywanie callbacku, czyszczenie kolejki przy ponownym `begin` oraz odrzucanie nieprawidłowych danych wejściowych |
| `test_ota_protocol` | Ścisła gramatyka zaproszenia/AUTH2, normalizacja numeryczna i szesnastkowa, dokładna tożsamość punktu końcowego UDP, wiązanie pola transkryptu, porównanie tagu o stałym kształcie i współdzielony wektor HMAC-SHA256 hosta/urządzenia |
| `test_ota_image` | Wersjonowany manifest OTA i redundantne kodowanie stanu rozruchu, walidacja CRC/HMAC, obsługa uszkodzeń, zawijanie sekwencji i wybór najnowszego rekordu |
| `test_ota_swap_engine` | Wznawialna zamiana sektora program/staging przez każdą symulowaną granicę błędu przed/po mutacji, wycofanie odwrotnej zamiany i odrzucanie uszkodzonej fazy |
| `test_rp_ota_artifacts` | Natywny pomocnik pakowania OTA RP, w tym wyrównanie sektora RP2040-E14, zachowanie rzeczywistej strony, renumeracja UF2 i odrzucanie nakładania się |
| `test_hal_time` | wspólna funkcja ustawiająca i zwracany przez nią status, monotoniczny wzrost wartości 64-bitowej mimo zawinięcia licznika 32-bitowego, przywracanie czasu z RTC i utrwalanie czasu NTP, stan powodzenia lub błędu NTP, formatowanie strefy czasowej i czasu lokalnego, konwersja składników daty, CET/CEST, zakresy oraz wyodrębnianie minut |
| `test_hal_kv` | CRUD u32/blob, usuwanie, pomijanie niezmienionych, GC, równoległe aktualizacje, bezpośrednia propagacja statusu EEPROM, błędy niezainicjalizowania/zakresu/pojemności i inicjalizacja wyjścia |
| `test_hal_crypto` | Zachowanie pomocników Base64/MD5/jednorazowego i przyrostowego SHA-256/HMAC-SHA256/ChaCha20/ChaCha20-Poly1305, walidacja wejścia oraz regresyjne sprawdzenia odrzucania zawinięcia licznika ChaCha20 |
| `test_wireguard_crypto_shared` | wspólne prymitywy kryptograficzne WireGuard (`crypto_equal/zero`, BLAKE2s, X25519, ChaCha20, ChaCha20-Poly1305, w tym wektory RFC8439 IETF dla AEAD z oddzielnym tagiem) |
| `test_hal_soft_timer` | pokrycie adaptera C: `create`/`begin`/`tick`/`abort`/`restart`, konfiguracja tabeli i funkcje pomocnicze `tick`, ścieżka callbacku `delay`/`idle`, walidacja nieprawidłowego wejścia (tabela `NULL` / `count==0`) |
| `test_SmartTimers` | `tick`, wywołanie callbacku, `abort`, `restart` (zachowanie rdzenia używane przez `hal_soft_timer_*`) |
| `test_pidController` | wyjście P, ograniczanie wyjścia, reset całkowania, wykrywanie stabilności (zachowanie rdzenia używane przez `hal_pid_controller_*`) |
| `test_multicoreWatchdog` | wymaganie oznak aktywności obu rdzeni, reset zewnętrzny oraz bezpieczne pomijanie operacji przed inicjalizacją |
| `test_tools` | pokrycie narzędzi z `tools.cpp` przy użyciu mocków HAL, w tym `debugInit`, `setDebugPrefixWithColon`, funkcje pomocnicze do obsługi liczb i łańcuchów znaków, starsze adaptery czasu przekazujące operacje do HAL oraz funkcje formatujące bez ryzyka przepełnienia bufora |
| `test_hal_critical_section` | zagnieżdżanie sekcji krytycznej i zachowanie przywracania stanu przerwań |
| `test_hal_dac` | zgodność inicjalizacji DAC oraz zapisy surowych wartości i napięcia w miliwoltach zwracające status, walidacja kanału, zakresu i stanu niezainicjalizowania oraz raportowanie nieobsługiwanego układu docelowego |
| `test_hal_digipot` | zachowanie init/set fasady MCP401x/MAX5395, walidacja zakresu i mapowanie statusu |
| `test_hal_pcnt` | powodzenie inicjalizacji, odczytu i resetowania licznika impulsów, nieprawidłowe argumenty, niezainicjalizowane kanały oraz adaptery zgodności |
| `test_hal_i2c_slave` | mapa rejestrów I2C-slave, callbacki, transakcje RX/TX i obsługa nieprawidłowego wejścia |
| `test_hal_serial_session_vocabulary` | stałe słownika poleceń/statusu sesji serial i pomocnicy konwersji |
| `test_hal_status` | wspólne wartości `hal_status_t`, konwersja na tekst, predykaty oraz adaptery wartości boolowskich i statusu |
| `test_hal_modem_at` | parsowanie poleceń i odpowiedzi ogólnego silnika AT, URC, przekroczenia czasu oraz wywoływanie callbacków |
| `test_hal_simcom_a76xx` | przebieg poleceń zasilania, SIM, PDP, GNSS, LBS i MQTT modemu SIMCom A76xx oraz obsługa URC |
| `test_pcf8563_driver` | wspólne kodowanie rejestrów PCF8563, daty i czasu, alarmu, timera i CLKOUT oraz zachowanie wskaźnika integralności |
| `test_ds3231_driver` | wspólna obsługa daty i czasu DS3231, zapisy pełnej daty kalendarzowej, alarm, status, sterowanie CLKOUT bezpieczne dla oscylatora, temperatura, błędy I2C i zachowanie rejestrów |
| `test_ili9341_driver` | wspólna sekwencja poleceń i inicjalizacji ILI9341, okna adresowe i zapisy pikseli |
| `test_st77xx_driver` | wspólna inicjalizacja ST7735/ST7789/ST7796S/GC9A01, przesunięcia, okna i zapisy pikseli |
| `test_ssd1306_driver` | wspólna inicjalizacja rodziny SSD1306, aktualizacje bufora ramki, przesunięcia adresowania kontrolera, wstrzymywanie, wznawianie oraz przesyłanie poleceń i danych przez I2C/SPI |
| `test_rgb_oled_driver` | wspólna inicjalizacja SSD1331/SSD135x, sekwencja poleceń kontrastu i zmiany mapowania, okna adresowe oraz zapisy pikseli RGB565 |
| `test_st7567_driver` | wspólna inicjalizacja ST7567, rozmiar bufora strony, zapisy wyrównane do strony i walidacja nieprawidłowego obszaru |
| `test_hal_display_rgb_oled_facade` | rzeczywisty wybór backendu przez wspólną fasadę na podstawie funkcji obsługiwanych przez SSD1331/SSD135x, surowe zapisy RGB565, GFX i ograniczenia obrotu z użyciem mocka SPI |
| `test_hal_display_st7567_facade` | rzeczywisty wybór backendu przez wspólną fasadę na podstawie formatów MONO01/MONO10 obsługiwanych przez ST7567, przełączanie formatu i surowe zapisy wyrównane do strony z użyciem mocka SPI |
| `test_jh_gfx_geometry` | wspólne przycinanie GFX, prymitywy geometryczne, bitmapy i układ tekstu |
| `test_mcp2515_driver` | wspólne transakcje rejestrowe i SPI układu MCP2515, zależności czasowe bitów, TX/RX, filtry i błędy |
| `test_mfrc522_driver` | wspólna obsługa transportów rejestrowych MFRC522, inicjalizacja i funkcje pomocnicze protokołu RFID |
| `test_pn532_driver` | wspólne ramkowanie komunikacji PN532 przez SPI/I2C/UART, parsowanie ACK i odpowiedzi oraz polecenia NFC |
| `test_ff16_memdisk` | zarządzana integracja FatFs R0.16 nad dyskiem w pamięci, montowanie i zachowanie I/O plików |
| `test_stm32_pwm_clock` | testy obliczeń zegara timera PWM, preskalera i okresu STM32G474 |
| `test_hal_onewire_driver` | wspólne zależności czasowe programowej komunikacji bit-bang OneWire, reset i wykrywanie obecności, wejście i wyjście bitów oraz bajtów, a także wyszukiwanie urządzeń |
| `test_hal_config_storage_flags` | testy propagacji flag funkcji pamięci masowej i konfiguracji podczas buildu oraz w runtime |
| `test_jpeg` | zarządzane dekodowanie TJpgDec, wymiary, konwersja RGB565 i nieprawidłowe wejście |
| `test_lodepng` | kontrolowane kodowanie i dekodowanie LodePNG, zarządzanie pamięcią, konwersja oraz obsługa błędów |
| `test_gps_nmea_parser` | ramkowanie i suma kontrolna NMEA, parsowanie pozycji, daty, czasu i prędkości oraz odzyskiwanie po nieprawidłowych danych wejściowych |
| `test_stm32_hal_system` | zegar systemowy STM32G474, stan resetu i błędu oraz symulacja usługi systemowej backendu |
| `test_stm32_hal_i2c_slave` | backend rejestrowy I2C-slave STM32G474, zdarzenia, callbacki i obsługa błędów |
| `test_freertos_posix_runtime` | planista FreeRTOS POSIX na hoście, uruchamianie zadań, muteksy, opóźnienia i leniwe jednorazowe tworzenie zasobów, w tym granice wiadomości portu szeregowego i debugowania przy współbieżności |

### Dodawanie nowego zestawu testów

1. Utwórz `tests/test_<name>.cpp` z `#include "utils/unity.h"`, wywołaniami
   Unity `setUp`, `tearDown`, `UNITY_BEGIN`, `RUN_TEST` i `UNITY_END`.
2. Dodaj `add_hal_test(test_<name>)` do `tests/CMakeLists.txt`.
    Dla zestawów kompilujących dodatkowe źródła (na przykład `test_tools` i
    `test_multicoreWatchdog`), utwórz dedykowany wpis `add_executable(...)`.
3. Przekompiluj:
   `cmake --build .build/host && ctest --test-dir .build/host`.

### Sterowanie czasem w mocku

SmartTimers i PIDController zależą od `hal_millis()`.
Zegar w mocku zaczyna od 0 i jest sterowany przez:

```cpp
hal_mock_set_millis(uint32_t ms);     // ustaw czas bezwzględny
hal_mock_advance_millis(uint32_t ms); // przesuń względem bieżącego
hal_mock_timer_advance_us(uint64_t us); // wyzwala oczekujące alarmy hal_timer
```

**Ważne:** `SmartTimers` używa `_lastTime == 0` jako wartownika stanu
"niezainicjalizowanego". Ustaw czas w mocku na wartość niezerową (np.
`hal_mock_set_millis(1000)`) przed wywołaniem `SmartTimers::begin()`, aby
uniknąć uruchomienia się tego strażnika w testach.

---

*Powrót do [Dokumentacji API JaszczurHAL](../../pl/JaszczurHAL_API.md)*

*Dalej: [Bezpieczeństwo wielordzeniowe, drivery, przewodnik migracji](04_multicore_drivers_migration.md)*
