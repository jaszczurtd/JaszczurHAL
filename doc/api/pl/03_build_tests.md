# Zależności buildu, testy i stanowiska testowe sprzętu

*Dostępne również [po angielsku](../en/03_build_tests.md).*

> **Część [Dokumentacji API JaszczurHAL](../../pl/JaszczurHAL_API.md)**

## Zależności (build sprzętowy)

| Moduł HAL | Zależność zewnętrzna |
|---|---|
| Model komponentowy ESP32-S3 | Przypięty ESP-IDF z jednym generowanym grafem źródeł/zależności: bazowe źródła core/prosty-PWM, wybierane funkcjonalnie peryferia Fazy 2 oraz natywna łączność/usługi Fazy 3. |
| `hal_gpio`, `hal_pwm`, `hal_adc`, `hal_system` | API `hardware_*` / `pico_*` Pico SDK w rodzinie RP; backend rejestrowy STM32G474; usługi GPIO, LEDC PWM, ADC i systemowe ESP-IDF dla ESP32-S3. `hal_system` używa też API zadań FreeRTOS w obsługiwanych buildach `HAL_ENABLE_FREERTOS`. |
| `hal_usb` | Urządzenie TinyUSB własne HAL na RP: deskryptory CDC, pompa IRQ/timer w buildach bare, zadanie robocze na rdzeniu 0 w buildach FreeRTOS oraz reset BOOTSEL. STM32G474 obecnie nieobsługiwany. Mock dostarcza deterministyczne bufory CDC oraz obserwator resetu. |
| `hal_serial` | Jeden niezależny od targetu rdzeń serial/debug plus porty dowiązywane w czasie linkowania: RP CDC `hal_usb`, VFS USB Serial/JTAG uruchamiane przy starcie na ESP32-S3, debugowy USART2/host stdout na STM32G474 oraz przechwytywanie stdout/wstrzykiwalny RX w mocku. |
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
| `hal_lora_radio` | Wzajemnie wykluczający się providerzy rodzin: przypięty oficjalny driver Semtech SX126x z adapterem HAL dla zwalidowanego SX1262 i eksperymentalnego, wyłącznie programowego SX1261, lub własny provider rejestrowy HAL dla eksperymentalnych, wyłącznie programowych SX1276/SX1278; oba kompilują się dla RP i STM32G474 i mają deterministyczne pokrycie mockiem |
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
| `hal_wifi` | przypięty driver CYW43/lwIP na RP i STM32G474, lub natywne WiFi ESP-IDF/`esp_netif`/lwIP na ESP32-S3 |
| `hal_littlefs` | przypięty rdzeń `third_party/littlefs` plus skoordynowany wewnętrzny flash na RP i STM32G474 |
| `hal_udp` | współdzielony silnik surowego UDP lwIP nad wybranym backendem sieciowym CYW43 |
| `hal_tls` | wbudowany BearSSL nad natywnym `hal_tcp`; opcjonalny adapter transportu BSD jest kompilowany tylko, gdy dodatkowo włączono `HAL_ENABLE_BSD_SOCKETS` |
| Adapter BSD sockets | współdzielony `hal/network/adapters/bsd/hal_bsd_sockets.cpp` nad HAL UDP/TCP; pozostaje niezależnie wybieralny bez TLS |
| `hal_wireguard` | współdzielony silnik WireGuard/lwIP + backend hosta lwIP zgłaszany przez capability |
| `hal_mqtt` | wbudowany `PubSubClient` nad HAL TCP, z opcjonalnym transportem MQTTS przez BearSSL |
| `hal_notify` | fasada wybierająca backend oraz Telegram korzystający ze współdzielonego klienta HTTP/HTTPS |
| `hal_ota` | staging/aplikator RP z uwierzytelnionym transportem VS Code nad HAL UDP/TCP |
| `hal_time` | Współdzielone pomocniki gregoriańskie/CET/CEST oraz interwałów, plus klient HAL UDP/NTP i integracja z zegarem targetu |
| `hal_kv` | wewnętrzne `hal_eeprom` + `hal_sync` |
| `hal_sdlogger` | przypięty rdzeń FatFs R0.16 plus współdzielona warstwa plikowa w `hal/storage/filesystem/` |
| `tools` | API HAL |
| `multicoreWatchdog` | wewnętrzne `SmartTimers` + mutex `hal_sync` |

## Zależności (mock / build PC)

Wszystkie pliki `impl/.mock/` zależą tylko od standardowych nagłówków hosta,
takich jak `<cstdio>`, `<cstring>`, `<mutex>`, `<queue>` i `<stdarg.h>`. Żadne
SDK wbudowane nie jest wymagane.

---

## Mapa systemu testów i źródła prawdy

| Warstwa testów | Źródło konfiguracji | Wykonanie | Punkt rozszerzenia |
|---|---|---|---|
| Testy jednostkowe hosta/mock | `tests/CMakeLists.txt`, `tests/test_*.cpp`, główny `CMakeLists.txt` | CMake plus CTest | Dodaj zestaw Unity i zarejestruj go przez `add_hal_test(...)`, lub zadeklaruj dedykowany plik wykonywalny dla dodatkowych źródeł. |
| Testy hosta FreeRTOS POSIX | `tests/freertos_posix/`, `JH_ENABLE_FREERTOS_POSIX_TESTS` | CTest przez build hosta lub pełną bramkę | Dodaj target przez `add_hal_freertos_posix_test(...)`. |
| Bramka jakości repozytorium | `runalltests.sh`, `.github/workflows/ci.yml` oraz dane narzędziowe opisane w `00_scripts.md` | `./runalltests.sh` | Rozszerz właściwą bramkę i jej ukierunkowane testy regresyjne; utrzymuj generowane artefakty poniżej `.build/`. |
| Stanowiska buildu firmware | `tests/fixtures/<fixture>/.vscode/jaszczurhal.project.json` | `jh-vscode` lub docelowy runner produkcyjny | Rozszerz macierz manifestu target/board/wariant oraz jej test układu artefaktów. |
| Fizyczne stanowiska sprzętowe | źródło, manifest i weryfikator w `tests/hardware/<fixture>/` | Build/wgranie przez `jh-vscode` lub docelowy runner produkcyjny, następnie uruchomienie weryfikatora opisanego poniżej | Dodaj firmware, jawną macierz sprzętową, wyrocznię hosta, kryteria akceptacji i podsekcję w tym dokumencie. |

Powyższe pliki wykonywalne są źródłem prawdy, gdy opis i zachowanie się nie
zgadzają. Każde stanowisko sprzętowe zachowuje tylko krótkie odnośniki README
do lokalnego odkrycia. Kompletne instrukcje operatora, okablowanie, wymagania
oraz zarejestrowane wyniki akceptacji są scentralizowane w sekcji
[Stanowiska sprzętowe](#stanowiska-sprzętowe) poniżej.

---

## Testy jednostkowe

### Wymagania

- CMake ≥ 3.16
- GCC / Clang z C++17

### Build i uruchomienie

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
Konfiguruje lokalne środowisko po raz pierwszy:
- Instaluje hooki gita (pre-commit i commit-msg z `.githooks/`)
- Synchronizuje wszystkie przypięte komponenty przez `third_party/update_components.sh`
- Instaluje trwałe reguły dostępu USB i `/dev/ttyACM*` dla RP2040/RP2350,
  umożliwiające wgrywanie bez sudo i automatyczny reset BOOTSEL przez 1200 bps
- Oferuje trwałą konfigurację zapory sieciowej TCP/8266 ograniczoną do sieci LAN, na potrzeby callbacków OTA
- Ustawia katalogi buildu i wstępną konfigurację CMake
- Uruchom to raz, przy klonowaniu repozytorium lub po zmianach środowiska

**`runalltests.sh`** - pełna bramka walidacyjna
```bash
./runalltests.sh
./runalltests.sh --check-generated
```
Domyślny tryb odświeża deterministyczne śledzone dane wyjściowe przed
bramkami i wypisuje w podsumowaniu końcowym pliki zmienione przez tę
synchronizację. `--check-generated` weryfikuje te same dane wyjściowe bez
naprawiania rozjazdu; CI używa tego bardziej rygorystycznego trybu przez
`scripts/sync_generated.py --check`.

Uruchamia kompletny zestaw bramek jakości (8 bramek, w kolejności):
1. Sprawdzenie obecności narzędzi
2. Testy jednostkowe hosta/mock (`.build/gate/host/` + ctest, w tym FreeRTOS POSIX)
3. Bezpieczeństwo pamięci (Valgrind memcheck na wszystkich natywnych plikach wykonywalnych testów C/C++)
4. Analiza statyczna: cppcheck
5. Analiza statyczna: clang-tidy (bazy danych buildu hosta + STM32 poniżej
   `.build/gate/`)
6. Wykrywanie duplikatów PMD CPD w obrębie własnych implementacji C/C++ oraz
   skryptów Python
7. Buildy docelowe (STM32G474 plus sondy startowe/rdzenia Pico SDK
   RP2040/RP2350 ARM/RP2350 RISC-V, profile funkcjonalne RP, sześć
   reprezentatywnych buildów ELF/BIN/UF2 `01_core_runtime`/`18_freertos_suite`
   oraz jeden czysty build
   `tests/fixtures/esp32s3_phase3` z przypiętym ESP-IDF i zwalidowanym
   manifestem multi-image)
8. Buildy przykładów (macierz `gateTargets` wyprowadzona z dispatchera
   plus dedykowane stanowiska target/runtime)

Kończy działanie z niezerowym kodem przy pierwszym błędzie; logi rejestrują
wszelkie ostrzeżenia/błędy. Bramka Valgrind wybiera każdy bezpośrednio
zarejestrowany natywny plik wykonywalny testu C/C++ przez etykietę CTest
`memcheck`. `MEMCHECK_REQUIRED_TESTS` w `runalltests.sh` to krytyczny podzbiór
sprawdzany przed wykonaniem, a nie kompletny wybór. Testy Python, CMake i
sterowane powłoką pozostają poza memcheck. Sprawiedliwe (fair) planowanie
wątków Valgrind utrzymuje natywne testy schedulera FreeRTOS POSIX w wyborze.
Postęp CTest/Valgrind na żywo jest strumieniowany do terminala oraz do
`.build/gate/logs/jh_memcheck.log`.

Wszystkie dane wyjściowe buildu należące do repozytorium są utrzymywane
poniżej jednego ignorowanego korzenia `.build/`. Sondy kompilatora CMake w
trybie skryptowym używają `.build/tests/`; nie emitują plików `.o` do
korzenia repozytorium.

Bramka clang-tidy tworzy bazy danych analizy specyficzne dla profilu, z jedną
komendą buildu na plik źródłowy. Zapobiega to sytuacji, w której testy
fasad kompilujące ten sam współdzielony driver pod kilkoma zestawami
funkcjonalności wyzwalałyby zduplikowane przebiegi analizatora, podczas gdy
zwykłe buildy docelowe nadal kompilują każdy skonfigurowany wariant.

Bramka CPD używa uwierzytelnionej dystrybucji PMD 7.26.0 zarządzanej w
`third_party/pmd`. Skanuje pliki implementacji C/C++, a nie nagłówki, oraz
pliki Python poniżej `scripts/`, wykluczając źródła generowane i wendorowane.
Każda grupa duplikatów C/C++ od 100 tokenów blokuje w produkcji, testach i
przykładach; każda grupa skryptów Python od 50 tokenów również blokuje. Żaden
baseline ani allowlist nie może ukryć istniejącej grupy. Raport oblicza sumę
(union) zduplikowanych zakresów tokenów i wypisuje pokrycie globalnie oraz dla
mocka, RP2040, STM32G474, kodu współdzielonego, pozostałego kodu przenośnego
i skryptów Python. Raporty XML i deterministyczne listy plików są zapisywane
poniżej `.build/gate/cpd/`. CPD `PASS` oznacza zero grup przy skonfigurowanych
progach specyficznych dla danego języka.

To jest **zalecana walidacja przed commitem** oraz **bramka testowa CI/CD**.
Uruchamiaj przed wypchnięciem zmian, aby wcześnie wychwycić problemy między
platformami.

### Natywna bramka CI dla Windows

`.github/workflows/ci.yml` uruchamia dwie natywne bramki `windows-2025`, oprócz
kompletnej bramki jakości dla Linuksa:

- `windows-tooling` przygotowuje uwierzytelnione zarządzane środowisko,
  powtarza `runmefirst.ps1 -VerifyOnly`, uruchamia współdzielone testy
  runtime/platform/bootstrap i generatora, weryfikuje wybór źródeł zależności
  CMake dla FreeRTOS na RP i STM32, wykonuje czysty produkcyjny build
  ESP32-S3/ESP-IDF i wgrywa jej artefakty multi-image, a następnie kompiluje
  i uruchamia przenośne testy hosta z MSVC `/W4 /permissive- /WX`;
- `Windows firmware (<target>)` buduje wygenerowany projekt użytkownika ze
  ścieżki zawierającej spacje przez Ninja dla `rp2040`, `rp2350-arm`,
  `rp2350-riscv` i `stm32g474`, sprawdza artefakty docelowe oraz spatchowaną
  bazę danych buildu, i wgrywa reprezentatywne artefakty buildu.

Inwentarz CTest dla Windows utrzymuje adapter BSD POSIX, integrację
Bash/POSIX BearSSL oraz runtime FreeRTOS GCC/POSIX jako
widoczne, wyłączone testy. Ich aktywne pokrycie, wraz z Valgrind, cppcheck,
clang-tidy i PMD CPD, pozostaje w bramce dla Linuksa. Fiesta, DoomConsole i
Ford DPF Tracker mają osobne natywne workflow firmware dla
Windows, które zapewniają pokrycie integracyjne specyficzne dla projektu,
oprócz fixture'a generowanego projektu JaszczurHAL.

## Stanowiska sprzętowe

Powtarzalne sondy fizycznego urządzenia używają tego samego dispatchera VS
Code co aplikacje i przechowują swoje artefakty poniżej `.build/hardware/`:

| Stanowisko | Pokrycie |
|---|---|
| `tests/hardware/bluetooth_stage1` | Wewnętrzny, przed-API kontroler CYW43/BTstack, advertising, statyczny GATT oraz baza pamięciowa tylko-WiFi na Pico W i STM32G474/PIM730. |
| `tests/hardware/bluetooth_gamepad` | Zanonimizowany deskryptor i raporty 8BitDo Zero 2 Android D-input oraz prywatna sonda buildu Classic HID Host dla Pico 2 W. |
| `tests/hardware/bluetooth_observer` | Publiczne pasywne skanowanie Observer, ograniczona kolejka raportów oraz parsowanie BLE Teltonika/iBeacon/Eddystone na Pico W, Pico 2 W i STM32G474/PIM730. |
| `tests/hardware/bluetooth_stream` | Publiczny cykl życia BLE i uwierzytelniona bramka Stream w różnych krotkach target/board/runtime, w tym ponowne łączenie, watchdog, ciągły ruch, nasycenie i negatywne przypadki bezpieczeństwa. |
| `tests/hardware/rp_usb_cdc_echo` | Natywna enumeracja CDC TinyUSB, przeciwciśnienie (backpressure), ponowne łączenie i przepustowość |
| `tests/hardware/rp_usb_multicore` | Równoległe producenty CDC na obu rdzeniach RP, integralność, kompletność i afiniacja rekordów w bare-metal/FreeRTOS |
| `tests/hardware/rp_freertos_smp` | Scheduler, oba rdzenie, mutex/opóźnienie, sterta i USB pod FreeRTOS SMP |
| `tests/hardware/rp_flash_transaction` | Sekwencjonowanie koordynatora flash, ścieżki odrzucenia, kasowanie/programowanie i odzyskiwanie |
| `tests/hardware/rp_storage` | Commit/trwałość EEPROM, formatowanie/remontowanie LittleFS i montowanie po resecie |
| `tests/hardware/rp_sdlogger` | Fizyczne montowanie karty SD po SPI, deterministyczne dopisywanie, flush/close, reset/remontowanie, zawartość i trwałość licznika logów w EEPROM |
| `tests/hardware/rp_ota` | Odkrywanie, uwierzytelnianie, transfer, próba/potwierdzenie, wycofanie (rollback) i odzyskiwanie USB/sieci |
| `tests/hardware/lora_sx1262` | Inicjalizacja dwóch urządzeń SX1262, dwukierunkowe surowe pakiety, cykl życia niezawodnego łącza oraz fragmentowane transakcje żądanie/odpowiedź routera poleceń na parach zintegrowanych LF lub zewnętrznych HF |
| `tests/hardware/esp32s3_phase1` | Tożsamość targetu/płytki ESP32-S3 Fazy 1, generowana sygnatura linkowania, model chipu/liczba rdzeni, fizyczny flash, zainicjalizowany Quad PSRAM oraz powtarzane bicie serca `app_task0()` FreeRTOS nad natywnym USB Serial/JTAG. |
| `tests/hardware/esp32s3_phase2` | Sonda runtime Fazy 2 ESP32-S3 dla obu zadań aplikacji, system/sync, GPIO/IRQ, ADC, USB Serial/JTAG TX/RX, sprzętowy UART, skanowanie mastera I2C, ścieżka transferu mastera SPI, callbacki timera z dedykowanej puli oraz włączona konfiguracja stack-guard FreeRTOS. |

Poniższe podsekcje są kompletną referencją operatora dla każdego stanowiska
fizycznego. Udany build firmware jest jedynie wynikiem programowym,
chyba że stanowisko wyraźnie stwierdza inaczej; akceptacja fizyczna wymaga
jego wyroczni hosta lub wizualnej oraz zarejestrowanych kryteriów PASS.

### Sprzętowy test USB CDC na RP

`tests/hardware/rp_usb_cdc_echo` weryfikuje natywnego właściciela TinyUSB RP
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
Gdy inna płytka jest już w BOOTSEL, neutralne względem targetu `upload`
zapisuje migawkę istniejących dysków przed dotknięciem 1200 bps i zapisuje
tylko na nowo pojawiającym się dysku.

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
kompatybilnych płytek. Workflow celowo nie zgaduje między dwoma
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
integralność, kompletność, afiniację producenta oraz końcowy status HAL.
Zniekształcona linia wykrywa przeplot na poziomie bajtów między równoległymi
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

`tests/hardware/rp_freertos_smp` weryfikuje przypięte natywne jądro FreeRTOS
na fizycznym Pico lub Pico 2. Weryfikuje start schedulera, afiniację zadania
aplikacji na obu rdzeniach, działanie muteksu HAL między rdzeniami,
raportowanie sterty FreeRTOS oraz natywny ruch USB CDC z opóźnionymi
odczytami hosta.

Zbuduj i wgraj przez zwykły natywny workflow VS Code:

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

Zbuduj i wgraj wariant bare-metal przez zwykły workflow:

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

`tests/hardware/rp_storage` weryfikuje natywne EEPROM i LittleFS na
fizycznym sprzęcie RP2040/RP2350. Zatwierdza (commit) licznik startu EEPROM,
formatuje i remontuje partycję LittleFS, resetuje przez watchdog, a następnie
weryfikuje trwałość EEPROM i montowanie LittleFS bez kolejnego formatowania.

Zbuduj i wgraj przez zwykły natywny workflow:

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

`tests/hardware/rp_sdlogger` weryfikuje współdzielony SDLogger z fizyczną
kartą SD po SPI. Montuje kartę, otwiera log numerowany przez EEPROM, dopisuje
deterministyczną zawartość, wykonuje flush i zamyka plik, resetuje przez
watchdog, remontuje kartę, sprawdza dokładny dopisany fragment końcowy pliku
oraz weryfikuje, że licznik logów w EEPROM przetrwał.

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

Gdy istnieje jednocześnie kilka buildu target/runtime, wskaż
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
Nazwa hosta WiFi jest wysyłana w opcji DHCP 12, a jej zmiana przy aktywnym
leasie wyzwala odnowienie DHCP.

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
ale adres IP urządzenia jest znany, użyj unicastowego celu odkrywania:

```sh
python3 tests/hardware/rp_ota/verify_ota.py \
  <other-options> --broadcast <device-ip>
```

Gdy fabryczny UF2 zastępuje aktywny program buildem innego
target/runtime, lub płytka reużyta przez inne stanowisko flash zgłasza
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

`tests/hardware/bluetooth_stage1` to wewnętrzna sonda integracji CYW43/BTstack
sprzed API. Jej macierz buildu obejmuje STM32G474 Nucleo + PIM730,
Raspberry Pi Pico W oraz RP2350 ARM Pico 2 W. Celowo nie włącza żadnego
publicznego makra funkcjonalności Bluetooth i nie może być używana jako
przykład publicznego API aplikacji.

Historyczne przebiegi sprzętowe Etapu 1 poniżej obejmują Nucleo+PIM730 oraz
Pico W. Akceptacja sprzętowa Pico 2 W używa zamiast tego publicznych stanowisk
[Bluetooth Observer](#sprzętowy-test-bluetooth-observer) oraz
[BLE Stream](#bramka-sprzętowa-jh-ble-stream-v1). RP2350 RISC-V jest
nieobsługiwane, ponieważ jego transport Bluetooth CYW43 nie jest włączony.

Build posiada bezpośrednio źródła BTstack i nie linkuje
`pico_cyw43_arch`, `pico_btstack_cyw43` ani integracji pamięci masowej
Bluetooth z Pico SDK. Uruchamia współdzielonego właściciela radia JH CYW43
przez swoją referencję BLE, pobiera firmware Bluetooth przez tę samą instancję
CYW43, uruchamia advertising z możliwością połączenia jako `JH BLE Stage 1`
oraz eksponuje ograniczoną statyczną charakterystykę GATT do odczytu/zapisu.

Udany build jest jedynie bramką programową. Wyniki sprzętowe muszą
rejestrować wyjście `JHBT1`, zachowanie połączenia/zapisu, użycie pamięci
ELF/map oraz dokładną płytkę/okablowanie testowane. Przebieg STM32
dodatkowo weryfikuje, że ścieżka `BT_ON` PIM730 nadal podąża za `WL_ON` w
zmontowanym zestawie.

Wariant `bluetooth` to sonda; `wifi-only` to poza tym równoważna baza
pamięciowa. Oba warianty muszą być mierzone z ich plików ELF/map z tym samym
targetem, płytką, kompilatorem i typem buildu.

Buildy programowe Etapu 1 zmierzone 2026-08-04 to:

| Target i wariant | Obciążenie FLASH | SRAM statyczne | Zarezerwowana sterta/stos |
|---|---:|---:|---:|
| STM32G474 + PIM730, `bluetooth` | 332,3 KiB | 50,0 KiB | 3,0 KiB |
| STM32G474 + PIM730, `wifi-only` | 276,9 KiB | 43,2 KiB | 3,0 KiB |
| RP2040 Pico W, `bluetooth` | 403,2 KiB | 60,4 KiB | 6,0 KiB |
| RP2040 Pico W, `wifi-only` | 326,0 KiB | 53,6 KiB | 6,0 KiB |

Te pomiary nie wymagają zmniejszonego ATT MTU ani mniejszych kolejek Etapu 1.

Po migracji na współdzielonego właściciela w Etapie 2, dopasowane obrazy
zmierzono:

| Target i wariant | Obciążenie FLASH | SRAM statyczne | Zarezerwowana sterta/stos |
|---|---:|---:|---:|
| STM32G474 + PIM730, `bluetooth` | 326,0 KiB | 48,4 KiB | 3,0 KiB |
| STM32G474 + PIM730, `wifi-only` | 278,1 KiB | 43,2 KiB | 3,0 KiB |
| RP2040 Pico W, `bluetooth` | 393,8 KiB | 57,3 KiB | 6,0 KiB |
| RP2040 Pico W, `wifi-only` | 327,8 KiB | 53,6 KiB | 6,0 KiB |

Obrazy tylko-WiFi nadal wykluczają BTstack, firmware Bluetooth oraz
współdzielone pule Bluetooth na magistrali. Migracja właściciela nie dodaje
żadnego statycznego SRAM do żadnej z baz tylko-WiFi.

Po migracji Etapu 3 na interfejs kontrolera, ograniczone HCI i własną pętlę
runtime JH (run-loop), dopasowane obrazy zmierzono:

| Target i wariant | Obciążenie FLASH | SRAM statyczne | Zarezerwowana sterta/stos |
|---|---:|---:|---:|
| STM32G474 + PIM730, `bluetooth` | 327,1 KiB | 48,5 KiB | 3,0 KiB |
| STM32G474 + PIM730, `wifi-only` | 278,1 KiB | 43,3 KiB | 3,0 KiB |
| RP2040 Pico W, `bluetooth` | 390,1 KiB | 57,3 KiB | 6,0 KiB |
| RP2040 Pico W, `wifi-only` | 322,5 KiB | 53,6 KiB | 6,0 KiB |

Bramka sprzętowa Etapu 3 powtórzyła start kontrolera, advertising, połączenie
BlueZ oraz rozwiązanie usługi GATT na obu płytkach. STM32G474 + PIM730
zarejestrował symetryczny ruch ACL na poziomie `11/11` oraz dwa trafienia
budżetu drenażu ograniczone do inicjalizacji. Pico W zarejestrował symetryczny
ruch ACL na poziomie `11/11` bez trafień budżetu drenażu. Oba transporty
pozostały `HAL_OK`, a obie płytki pozostały uruchomione z wariantem
`bluetooth`.

Podetap sprzętowy 1.a został ukończony na obu profilach 2026-08-04. Sonda
STM32G474 + PIM730 użyła okablowania poniżej. Sonda Pico W użyła wbudowanego
CYW43439 i wyliczyła się (enumerowała) jako `JaszczurHAL RP` przez USB. Na
obu płytkach sonda osiągnęła stany gotowości kontrolera i advertisingu z
możliwością połączenia, BlueZ rozwiązał statyczną usługę GATT, odczyt i zapis
charakterystyki przeszły, a peryferium zaakceptowało rozłączenie, po którym
nastąpiło świeże połączenie i odczyt GATT. Dopasowane obrazy `wifi-only`
również zgłosiły `HAL_OK`. Wstępne wykrywanie ATT na STM32 ujawniło brakującą
inicjalizację Security Managera; sonda inicjalizuje teraz `sm_init()` przed
`att_server_init()`. Cykl życia połączenia jest obserwowany przez jedną
rejestrację zdarzenia HCI, dzięki czemu każde fizyczne łącze jest liczone
raz. Ostateczny obraz przywrócony na każdej płytce to wariant `bluetooth`.
Przebieg połączenia Pico W nie zarejestrował trafień budżetu drenażu. Sonda
STM32 zarejestrowała dwa ograniczone trafienia drenażu podczas inicjalizacji
kontrolera, po czym pozostała stabilna ze statusem transportu `HAL_OK`.

Bramka smoke Etapu 2 powtórzyła start kontrolera, advertising, połączenie
BlueZ oraz rozwiązanie usługi GATT ze współdzielonym właścicielem na obu
płytkach. Pico W również zarejestrował symetryczny ruch ACL bez trafień
budżetu drenażu. Obie płytki pozostały uruchomione z wariantem `bluetooth`.

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

Nie używaj 5 V. Wizualnie potwierdź, że przecinalna ścieżka `BT_ON`-do-`WL_ON`
w PIM730 jest nienaruszona; pozostaw `BT_ON`/`BL_ON` poza tym niepodłączone.
Dopiero po potwierdzeniu okablowania i stanu ścieżki obraz Bluetooth STM32
powinien zostać zaprogramowany przez ST-Link Nucleo. Zarejestruj okresowy
status `JHBT1` przed testowaniem odkrywania, połączenia, odczytu/zapisu
charakterystyki, rozłączenia/ponownego połączenia oraz regresji tylko-WiFi.
Przebieg z radiem wbudowanym w Pico W następuje jako drugi profil sprzętowy.

### Sprzętowy test gamepada Bluetooth Classic HID

`tests/hardware/bluetooth_gamepad` zawiera prywatną sondę Classic HID Host
sprzed API i zanonimizowany zapis `zero2_android_dinput.json`. Zapis obejmuje
137-bajtowy deskryptor raportu, tożsamość PnP, metadane SDP, wszystkie dwanaście
stanów wejściowych, niedeklarowany końcowy bajt wejścia i powtórzony surowy
raport. Pomija adresy Bluetooth, link keys, tożsamość hosta i numery seryjne
USB.

Firmware inicjalizuje współdzielony runtime HCI/L2CAP, ulotną bazę jednego link
key, klienta SDP, jedno połączenie HID Host i jeden event handler. Inquiry
uruchamia się wyłącznie po komendzie szeregowej `DISCOVER` i kończy po 120
sekundach albo po zaakceptowaniu jednego zgodnego urządzenia. Kandydat musi
mieć klasę peryferium, zapisaną nazwę, usługę Classic HID i zapisaną tożsamość
PnP. Prywatnego selectora nie wolno łączyć z publicznym BLE ani wcześniejszą
sondą Etapu 1.

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
Weryfikator autoryzuje metodę pairingu zgłoszoną przez kontroler, sprawdza
zapisany deskryptor i wszystkie wejścia, wykonuje rozłączenia/ponowne
połączenia oraz power-cycle, a następnie utrzymuje ciągłe połączenie przez 30
minut. Podczas pierwszego reconnectu prosi o przytrzymanie wejścia, aby ścieżka
disconnect mogła zwolnić aktywny stan.

Weryfikator zapisuje `zero2_pico2w_c5_result.json`. Raport zawiera wersje
targetu i bibliotek, czasy, liczniki transportu oraz maksymalne zajęcie pul.
Nie może zawierać adresów Bluetooth, materiału link key, tożsamości hosta,
nazwy portu szeregowego ani numeru seryjnego USB. ELF/map i lista symboli muszą
również wykazać HID Host `ENABLE_CLASSIC`, klienta SDP, parser HID oraz
pamięciową bazę link keys, jednocześnie wykluczając ATT, GATT, SM, RFCOMM,
serwer SDP, HID Device i profile audio.

### Sprzętowy test Bluetooth Observer

`tests/hardware/bluetooth_observer` weryfikuje pasywne API BLE Observer na
Raspberry Pi Pico W, Pico 2 W oraz STM32G474 Nucleo z PIM730/RM2. Uruchamia
pasywne skanowanie w trybie legacy, opróżnia ograniczoną kolejkę raportów,
parsuje struktury AD i rejestruje dane firmowe Teltonika, iBeacon i sygnatury
Eddystone bez inicjowania połączenia BLE.

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
zakresem tej sondy.

RP2350 RISC-V jest nieobsługiwane, ponieważ jego transport Bluetooth CYW43
nie jest włączony.

### Bramka sprzętowa JH BLE Stream v1

`tests/hardware/bluetooth_stream` weryfikuje publiczny cykl życia BLE oraz
uwierzytelniony strumień aplikacji na Raspberry Pi Pico W, Pico 2 W, RP2040
Pico z RM2/PIM730 oraz STM32G474 Nucleo z PIM730/RM2. Firmware reklamuje się
(advertisuje) jako `JH Stream HW`, wymaga stałego, testowego 256-bitowego
sekretu i odbija uwierzytelnione ładunki. `verify.py` działa jako Central na
Linuksie przez BlueZ.

#### Macierz buildu

Skompiluj i wgraj wszystkie osiem kombinacji target, board i runtime osobno:

| Target | Board | Runtime |
|---|---|---|
| `rp2040` | `picow` | bare-metal, FreeRTOS |
| `rp2040` | `pico-rm2` | bare-metal, FreeRTOS |
| `rp2350-arm` | `pico2w` | bare-metal, FreeRTOS |
| `stm32g474` | `nucleo-g474re-pim730` | bare-metal, FreeRTOS |

Te same osiem krotek jest zadeklarowanych jako `example.hardwareMatrix` w
manifeście stanowiska i są sprawdzane przez test układu artefaktów
repozytorium.

Buildy bare-metal:

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
obrazu FreeRTOS. Stanowisko inicjalizuje BLE od pierwszego wywołania
`app_task0()`, po starcie schedulera FreeRTOS. Stos jego zadania task-0 to
1024 słowa, ponieważ uwierzytelniony handshake i jego kryptograficzne
zmienne tymczasowe przekraczają ogólny domyślny rozmiar stanowiska. Użyj tego
samego jawnego targetu, płytki i wariantu do wgrywania. Udany build jest
wynikiem programowym; nie liczy się jako przebieg sprzętowy.

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

Ekran raportuje adres kontrolera, stan BLE/Stream, MTU, RX/TX, liczniki
odrzuceń/przepełnień/bezpieczeństwa, restarty cyklu życia, status i czas
działania. Statyczne i niezmienione pola są przerysowywane tylko wtedy, gdy
zmienia się ich wartość; RX/TX oraz status/czas działania nadal aktualizują
się raz na sekundę. Utrzymuje to trwałe obciążenie SPI1 bez wielokrotnego
przesyłania identycznych pełnych wierszy tekstu. Błąd inicjalizacji lub
aktualizacji wyświetlacza zatrzymuje normalny postęp stanowiska i jest
zgłaszany jako błąd cyklu życia. LCD jest tylko-do-zapisu, więc inspekcja
wzrokowa pozostaje fizyczną wyrocznią dla wyjścia panelu.

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

Domyślna bramka wykonuje:

- smoke test publicznych metadanych, ATT MTU, inicjalizacji, advertisingu,
  połączenia i uwierzytelnionego strumienia, w tym dokładnego posiadania
  usługi i flag GATT plus obu ścieżek DATA write-request i write-command;
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
- sprawdzenia błędnego dowodu, sfałszowanego tagu, powtórki (replay), luki
  licznika w przód oraz backoffu uwierzytelniania.

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

`--reconnects` nie może być niższe niż 50, `--stream-seconds` nie może być
niższe niż 300, a `--stream-rate` nie może być niższe niż 10. Bramka
zawodzi też, gdy obserwowana szybkość uwierzytelnionych wiadomości jest
poniżej 90% `--stream-rate`. Zwiększ czas trwania strumienia dla przebiegu
nocnego (soak). Użyj runtime `baremetal` dla obrazu bazowego i `freertos`
dla wariantu manifestu `freertos`. Weryfikator jawnie wybiera odkrywanie LE,
więc nieaktualny alias BlueZ nie wpływa na wybór po adresie. Wymaga
systemowych pakietów Pythona dla D-Bus i GLib plus pakietu `cryptography`.

#### Zarejestrowane wyniki sprzętowe - 2026-08-25

| Target i runtime | Ponowne połączenia | Ciągły uwierzytelniony strumień | Wynik |
|---|---:|---:|---|
| RP2040 Pico W, bare-metal | 50/50 | 2773 wiadomości / 300,1 s (9,24 Hz) | PASS |
| RP2040 Pico W, FreeRTOS | 50/50 | 3000 wiadomości / 300,0 s (10,00 Hz) | PASS po zażądaniu interwału połączenia 15 ms |
| RP2350 ARM Pico 2 W, bare-metal | 50/50 | 3000 wiadomości / 300,0 s (10,00 Hz) | PASS |
| RP2350 ARM Pico 2 W, FreeRTOS | 50/50 | 3000 wiadomości / 300,0 s (10,00 Hz) | PASS |
| RP2040 Pico + RM2/PIM730, oba runtime | - | - | oczekująca bramka fizyczna |
| STM32G474 + RM2/PIM730, oba runtime | - | - | oczekująca rozszerzona bramka fizyczna |
| STM32G474 + RM2/PIM730 + ILI9341, bare-metal | 50/50 | 2910 wiadomości / 300,1 s (9,70 Hz) | HOST PASS, w tym reset IWDG |
| STM32G474 + RM2/PIM730 + ILI9341, FreeRTOS | 50/50 | 2930 wiadomości / 300,0 s (9,77 Hz) | HOST PASS, w tym reset IWDG |

Przebiegi obciążeniowe wyświetlacza STM32G474 z 2026-08-25 użyły wariantów
`display` i `display-freertos` z PIM730 na PB12-PB15 oraz ILI9341 na SPI1.
Oba przeszły restart cyklu życia, niepilnowany reset IWDG, 50 uwierzytelnionych
ponownych połączeń, pięciominutowy strumień, nasycenie z czterema
zachowanymi i ośmioma odrzuconymi ramkami plus jednym zgłoszonym
przepełnieniem, oraz wszystkie negatywne przypadki bezpieczeństwa/odzyskiwania
z 62 unikalnymi handshake'ami. Krok IWDG zmienił powód resetu `2 -> 4` oraz
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

Po dodaniu wyroczni na każdy rozruch, kompletny przebieg bare-metal Pico 2 W
ponownie przeszedł 50/50 ponownych połączeń oraz 3000 wiadomości w 300,0 s
(10,00 Hz), plus nasycenie i wszystkie przypadki bezpieczeństwa. Jego krok
watchdoga zmienił powód resetu `3 -> 4` oraz identyfikator rozruchu
`cb3ef2a0b00439b4 -> bc75beed8bfd5cf1`, zachowując adres
`2C:CF:67:BB:40:2E`.

Zarejestrowane udane przebiegi ukończyły publiczny restart cyklu życia,
zachowały lokalny adres w teście resetu, zmieniły powód resetu z `3` na
powód watchdoga `4` oraz uwierzytelniły świeżą sesję. Bieżąca wyrocznia
akceptuje dowolny powód sprzed resetu, ale wymaga powodu watchdoga `4`
później oraz zmiany niezerowego, losowego identyfikatora na rozruch. W
konsekwencji stary powód watchdoga nie może sprawić, by zwykły restart BLE
wyglądał jak reset MCU. Każdy przebieg zachował też cztery z 12 ramek
nasycenia, zgłaszając osiem odrzuceń i jedno potwierdzenie przepełnienia,
oraz przeszedł sprawdzenia sfałszowanego tagu, powtórki, luki licznika,
błędnego dowodu, backoffu i odzyskiwania świeżej sesji. Komenda watchdoga
dostarcza niepilnowany test przerwania przez reset MCU; nie usuwa ona
fizycznie VBUS.

Wcześniejszy obraz Pico W FreeRTOS użył stosu zadania 512 słów i resetował
się podczas pierwszego uwierzytelnionego handshake'u. Zwiększenie stosu
stanowiska do 1024 słów usunęło ten reset, ale kolejny przebieg utracił
łącze BLE podczas ponownych połączeń, a inny osiągnął zaledwie 8,06 Hz.
Backend Stream pozostawiał interwał połączenia całkowicie centralnemu,
przez co sekwencyjny uwierzytelniony round trip żądanie/powiadomienie był
zbyt wolny na RP2040 FreeRTOS. Po tym, jak peryferium zaczęło żądać
interwału 15 ms z zerowym opóźnieniem peryferium (peripheral latency), pełny
ponowny przebieg przeszedł: odzyskiwanie po watchdogu, 50/50 ponownych
połączeń, 3000 wiadomości w 300,0 sekundy (10,00 Hz, 60,2/153,5 ms
średnie/maksymalne opóźnienie), nasycenie oraz wszystkie negatywne przypadki
bezpieczeństwa/odzyskiwania z 62 unikalnymi handshake'ami.

Obecna wyrocznia hosta to Linux/BlueZ. Natywne wykonanie na Windows jest
odłożone, podobnie jak integracja z projektem lights-timer; żadne z nich
nie jest wymaganiem dla wyników zarejestrowanych powyżej.

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
| `JHBL5/STATS` | zwarta binarna wyrocznia odzyskiwania `J5S1` |

Odpowiedź tożsamości jest kompilowana bezpośrednio z `HAL_TARGET_NAME`,
`HAL_BOARD_PROFILE_NAME` i `HAL_ENABLE_FREERTOS`; runtime to dokładnie
`baremetal` lub `freertos`. Weryfikator porównuje ją z wszystkimi trzema
wymaganymi wartościami CLI przed akceptacją wyników obciążenia. Na przykład
odpowiedź Pico W bare-metal to `J5I1|rp2040|picow|baremetal`.

Udany przebieg fizyczny kończy się `JHBL5 HOST PASS`. Log urządzenia używa
prefiksu `JHBL5` i rejestruje wynegocjowane MTU, liczniki, błędy
uwierzytelniania, odrzucenia powtórek, błędy cyklu życia, restarty oraz
straty ograniczonej kolejki.

Końcowa faza bezpieczeństwa celowo wchodzi w okno backoffu uwierzytelniania,
wysyła odrzucane sondy HELLO co najmniej raz na sekundę przez całe
skonfigurowane okno 30 sekund oraz dowodzi świeżego uwierzytelnionego
odzyskania dopiero po jego upływie, zanim wypisze `JHBL5 HOST PASS`. Udany
przebieg pozostawia więc stanowisko gotowe do kolejnej bramki bez ponownego
wgrania.

Wbudowany sekret i jego kopia w `verify.py` to publiczny materiał testowy.
Nigdy nie mogą być ponownie użyte przez produkt. Produkt potrzebuje unikalnego,
losowego sekretu na urządzenie, dostarczanego poza pasmem (out of band) i
przechowywanego przez jego przepływ provisioningu.

<a id="sx1262-raw-lora-hardware-gate"></a>

### Bramka sprzętowa surowego LoRa SX1262

`tests/hardware/lora_sx1262` używa kompilowalnego firmware
[`27_lora_point_to_point`](../../../examples/27_lora_point_to_point/) oraz
dwóch radiostacji w tym samym paśmie. Weryfikuje inicjalizację, dwukierunkowe
pakiety over-the-air, ciągłość sekwencji, asynchroniczne callbacki sterowane
przez DIO1, diagnostykę IRQ/anulowania, metadane RSSI/SNR, sen/wybudzenie
oraz reinicjalizację destroy/create radia.

Na profilach z sprzętową diodą LED statusu, ciągłe świecenie diody wskazuje
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

Firmware celowo używa 434,0 MHz dla tego stanowiska; to konfiguracja testowa,
a nie uniwersalny preset regulacyjny.

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

Oba urządzenia Core1262-HF używają tego samego profilu elektrycznego modułu
i konfiguracji technicznej EU868. Wygenerowane fakty płytki posiadają swoje
własne mapy pinów hosta; przykład nie zawiera okablowania stanowiska
specyficznego dla targetu. Profil Nucleo używa SPI2 na PB13/PB14/PB15 i
zachowuje LD2/`HAL_LED_BUILTIN` na PA5.

Przed przebiegiem OTA, sonda bez nadawania może być skompilowana i wgrana na
dowolnym hoście z `--variant probe`. Sukces weryfikuje możliwości providera,
jawną kalibrację, aktualne RSSI, CAD i standby bez włączania ścieżki
nadawania RF.

#### Weryfikacja

Uruchom wystarczająco długo, aby zaobserwować automatyczne sondy cyklu życia
przy sekwencji 10 i 20:

```bash
python3 tests/hardware/lora_sx1262/verify_pair.py \
  --initiator-port /dev/serial/by-id/<initiator> \
  --responder-port /dev/serial/by-id/<responder> \
  --duration 75
```

Sukces wymaga co najmniej pięciu dopasowanych sekwencji ping/pong, metadanych
pakietów, znacznika asynchronicznej pętli zdarzeń na obu radiostacjach,
niezerowych liczników IRQ/callback/anulowania, `HAL_OK` snu/wybudzenia oraz
`HAL_OK` reinicjalizacji. Następnie zamień, które fizyczne urządzenie
otrzymuje build `responder`, i powtórz.

Powtórz bramkę dla dwóch deterministycznych kombinacji testowych. Warianty
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
skorelowane, 500-bajtowe binarne żądanie `echo`; responder dysponuje je przez
współdzielony router i zwraca dokładny ładunek. Obie kodowane strony wymagają
trzech tekstowych (plaintext) fragmentów łącza przy domyślnych granicach.

Polityka handlerów zezwala zarówno na `LORA_LINK`, jak i `BLE_STREAM`. Ta
bramka dostarcza tylko adapter LoRa. Wpis źródłowy BLE demonstruje, że trasa
i API wire mogą zostać ponownie użyte przez późniejszy adapter BLE; nie oznacza to, że
taki adapter jest zaimplementowany.

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

Po wyliczeniu (enumeracji) obu urządzeń CDC, użyj stabilnych ścieżek
`/dev/serial/by-id/` i przechwyć co najmniej trzy kompletne transakcje:

```bash
python3 tests/hardware/lora_sx1262/verify_commands.py \
  --initiator-port /dev/serial/by-id/<command-initiator> \
  --responder-port /dev/serial/by-id/<command-responder> \
  --duration 75 --minimum-transactions 3
```

Sukces wymaga oczekiwanego znacznika roli od obu urządzeń oraz braku błędu
lub timeoutu `JHCMD1`. Dla co najmniej trzech niezerowych identyfikatorów
żądania, żądanie inicjatora, handler respondera i odpowiedź inicjatora muszą
zgadzać się co do długości 500 bajtów, CRC-32 i liczby trzech fragmentów.
Peer handlera i źródło odpowiedzi muszą wynosić odpowiednio `0x1001` i
`0x1002`; oba identyfikatory sesji muszą pasować do niezerowej wartości
`READY` swojej roli. Flagi bezpieczeństwa w plaintext muszą wynosić zero,
RSSI musi mieścić się w ujemnym zakresie LoRa, SNR musi być ograniczone,
źródło handlera musi być `LORA_LINK`, wywołania handlera muszą ściśle rosnąć,
a końcowy status i porównanie bajtów muszą się obie powieść. Zapisane logi
można sprawdzić za pomocą `--initiator-log` i `--responder-log` zamiast
portów szeregowych na żywo.

Zamień role dwóch fizycznych urządzeń i powtórz. Zarejestruj ich etykiety
modułu i anteny, wersję firmware, odległość, dopasowane identyfikatory
żądań, zakres RSSI/SNR oraz JSON weryfikatora tylko w prywatnym raporcie
sprzętowym. Ustawienia stanowiska 434,0 MHz to wartości testowe techniczne;
podłącz anteny LF i przestrzegaj lokalnych wymogów dotyczących widma, mocy i
cyklu pracy.

### Sprzętowy test ESP32-S3 - faza 1

`tests/hardware/esp32s3_phase1` domyka instalację (plumbing) targetu, płytki,
buildu, flashowania i monitora dla Waveshare ESP32-S3-Zero SKU 25081.
Celowo nie testuje API HAL GPIO, serial, magistrali, sieci ani pamięci
masowej, przypisanych do późniejszych faz.

Firmware raportuje dokładną wygenerowaną tożsamość targetu i płytki, a
następnie sprawdza wykryty model chipu, liczbę rdzeni, fizyczny rozmiar
flash, inicjalizację PSRAM oraz fizyczny rozmiar PSRAM względem rejestru
płytek. `verify_phase1.py` uzyskuje swoje oczekiwania z tych samych
deskryptorów targetu i płytki oraz oczekuje na powtarzany raport na natywnym
porcie USB Serial/JTAG.

Użyj stabilnej ścieżki `/dev/serial/by-id/`, gdy jest dostępna. Skompiluj i
zwaliduj artefakty przez produkcyjny runner ESP-IDF:

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

Ten sam projekt jest śledzony jako projekt ESP-IDF `jh-vscode`. Ustaw `PORT`
na stabilny alias podłączonej płytki, następnie skompiluj, odśwież
IntelliSense, wgraj i monitoruj przez publiczny workflow:

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

Wybrane urządzenie musi pasować do VID/PID USB Serial/JTAG profilu płytki
`303a:1001`. Aby przetestować przekazanie wgrywania, pozostaw monitor
uruchomiony i wywołaj tę samą komendę `upload` z drugiego terminala. Wgranie
musi zwolnić monitor wyłącznie tego projektu, wgrać kompletny manifest
trzech obrazów, zresetować płytkę oraz umożliwić ponowne połączenie
monitora.

Zatrzymaj monitor przed uruchomieniem samodzielnego weryfikatora, ponieważ
obie komendy przejmują wyłączne posiadanie portu szeregowego:

```bash
python3 tests/hardware/esp32s3_phase1/verify_phase1.py \
  --port "$PORT"
```

Udany przebieg wypisuje jeden obiekt JSON z `"phase": "task0"`, sekwencją co
najmniej jeden oraz `"status": "PASS"`.

#### Zweryfikowany punkt odniesienia fazy 1

Przebieg domknięcia fizycznego zakończył się czystym, 555-krokowym buildem
ESP-IDF. Obraz aplikacji miał 150544 bajtów, z 86% wolnej partycji. Trzy
kompletne wgrania każdorazowo flashowały bootloader, tabelę partycji i obraz
aplikacji. Raport runtime pasował do ESP32-S3 z dwoma rdzeniami, 4194304
bajtami fizycznego flash oraz zainicjalizowanym Quad PSRAM o pojemności
2097152 bajtów. Trwały monitor ESP również zwolnił port do wgrywania,
ponownie połączył się po resecie i wznowił powtarzane bicie serca
`app_task0()`.

Ten wynik waliduje workflow target/płytka/build/flash/monitor
fazy 1 dla Waveshare ESP32-S3-Zero SKU 25081. Nie rozszerza wsparcia na
API GPIO, portu szeregowego, magistrali, sieci, pamięci masowej ani
opcjonalnego drugiego zadania, przypisane do fazy 2.

### Sprzętowy test ESP32-S3 - faza 2

`tests/hardware/esp32s3_phase2` weryfikuje HAL peryferiów fazy 2 na profilu
`waveshare-esp32-s3-zero`. Wymaga jedynie natywnego kabla USB płytki; żaden
zewnętrzny czujnik, zworka ani urządzenie SPI/I2C nie jest wymagane.

Firmware sprawdza:

- czas systemowy, architekturę, UID, stertę, temperaturę matrycy, watchdog,
  granicę zachowanej usterki (retained fault) oraz włączone zachowanie
  stack-guard FreeRTOS;
- muteksy FreeRTOS, sekcje krytyczne oraz afiniację `app_task0`/`app_task1`
  na rdzeniach 0/1;
- wejście GPIO z podciągnięciem (pull-up), wyjście/odczyt zwrotny oraz
  przekonfigurowane przerwanie GPIO tego samego właściciela;
- odczyty 12-bitowego ADC rozstawione przez wewnętrzne podciągnięcie/
  podwieszenie (pull-down/pull-up) GPIO;
- sprzętowy UART1 TX/RX przez jeden pin pętli zwrotnej macierzy GPIO;
- czyszczenie magistrali mastera I2C, inicjalizację oraz kompletne
  skanowanie adresów (zero wykrytych urządzeń jest prawidłowe dla
  nieokablowanej płytki);
- transakcje mastera SPI2, blokujące DMA oraz synchroniczny fallback
  asynchronicznego DMA bez zakładania odebranych danych od nieobecnego
  slave'a;
- zarządzane pauzę/wznowienie GPTimer z dedykowanej puli, powtarzane
  callbacki ISR oraz teardown;
- dwukierunkowy ruch debugowania nad natywnym VFS USB Serial/JTOG konsoli
  startowej.

Skompiluj i zmaterializuj relokowalny manifest artefaktów:

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
wynik ADC poza zakresem lub nieudany status peryferium nie mogą więc zostać
zgłoszone jako udany test smoke.

Stanowisko celowo nie wywołuje `hal_enter_bootloader()`: udane wejście w
tryb download resetuje MCU i wymaga osobnego testu ponownego
połączenia/odzyskiwania. Jego symbol jest objęty pokryciem buildu/
linkowania przez stanowisko fazy 3, podczas gdy przejście resetu należy do
kampanii sprzętowej fazy 3.5.

#### Zarejestrowany stan fazy 2

Wcześniejsza rewizja ukończyła domknięcie fizyczne na Waveshare
ESP32-S3-Zero: afiniacja task0/task1 to rdzenie 0/1, dwa callbacki GPIO
działały w kontekście ISR, odczyty pull-down/pull-up ADC wyniosły 37/4095,
pętla zwrotna UART GPIO i SPI przeszły, nieokablowane skanowanie I2C zwróciło
`HAL_OK` z zerem urządzeń, 20 callbacków GPTimer z domyślnej puli działało w
kontekście ISR, a dwukierunkowe USB Serial/JTAG oraz sprawdzenia
systemowe/synchronizacji przeszły.

Ten historyczny wynik obejmuje wyłącznie oryginalny podzbiór stanowiska.
Bieżące stanowisko wymaga teraz dedykowanej puli timerów oraz
zaimplementowanego stack-guard, ale te sprawdzenia nie zostały jeszcze
powtórzone na sprzęcie. I2C target, PWM/PWM_FREQ, RMT/RGB, PCNT, wejście w
tryb download-boot, destrukcyjne wstrzykiwanie usterek stosu/faultu oraz
odzyskiwanie zachowanej usterki również pozostają w kampanii sprzętowej
fazy 3.5.

## Stanowiska buildu/konsolidacji firmware

### Stanowisko buildu/konsolidacji ESP32-S3

| Stanowisko | Pokrycie |
|---|---|
| `tests/fixtures/esp32s3_phase3` | Projekt ESP-IDF ograniczony do buildu, wybierający każdy backend ESP32-S3 dostarczony przez fazę 3. Sprawdza rozwiązywanie funkcjonalności/źródeł/zależności, build, linkowanie, generowanie partycji `two-ota-large` oraz publikację artefaktów. |

CI oraz lokalna bramka 7 budują ten fixture. Udany build nie
ustanawia zachowania runtime WiFi/socket/TLS/usług/OTA/WireGuard ani nowo
ukończonego zachowania peryferiów fazy 2; te wymagają osobnej kampanii
weryfikacji sprzętowej, cyklu życia i bezpieczeństwa negatywnego.

---

## Architektura testów hosta

### Jak to działa

Build CMake w korzeniu projektu kompiluje statyczną bibliotekę
`hal_mock` z:

- wszystkich zaślepek `src/hal/impl/.mock/*.cpp`,
- niezależnych od backendu źródeł HAL w `UTIL_SOURCES` (patrz
  `CMakeLists.txt`), w tym pozostałych współdzielonych adapterów statusu
  MQTT/WireGuard w `hal_network_status.cpp`, fasad HAL, warstw
  kompatybilności, przenośnych driverów urządzeń i dołączonych
  frameworków,
- `src/utils/unity.c` (wrapper integracji Unity).

Dokładna lista to zbiór `UTIL_SOURCES` w `CMakeLists.txt` - traktuj to jako
źródło prawdy.

Każdy plik wykonywalny testu w `tests/` linkuje wyłącznie z `hal_mock`, bez
nagłówków, bez Pico SDK, bez sprzętu.

Zarządzany framework Unity 2.5.4 znajduje się w `third_party/Unity/src`.
Śledzona integracja JaszczurHAL składa się z:

- `src/utils/unity.c`
- `src/utils/unity.h`
- `src/utils/unity_internals.h`
- `src/utils/unity_config.h`

Build hosta CMake kompiluje wrapper `src/utils/unity.c` do `hal_mock` i
włącza `HAL_ENABLE_UNITY` plus `UNITY_INCLUDE_CONFIG_H`. Źródła testów
dołączają `"utils/unity.h"` i używają lokalnego dla repozytorium
`unity_config.h`. Uruchom `scripts/ensure_unity.sh` lub centralny updater
komponentów, aby odtworzyć przypięty checkout. Poza buildem testów/
wsparcia, Unity jest nieaktywne, chyba że jawnie włączono `HAL_ENABLE_UNITY`.

`tools.cpp` jest objęte przez `test_tools` przy użyciu mocków HAL.
`multicoreWatchdog.cpp` jest objęte przez `test_multicoreWatchdog` przy
użyciu lokalnej zaślepki zamknięcia loggera plus mocków HAL.
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
| `test_hal_timer` | ścieżki dodawania/anulowania alarmów niskiego poziomu, diagnostyka `_ex`, zachowanie zarządzanego timera start/stop/pauza/wznowienie/okres/pozostały czas |
| `test_stm32_hal_timer` | rzeczywisty backend timera STM32G474 pod symulacją hosta: alarmy jednorazowe, przeplanowanie callbacku, anulowanie, limity/zniszczenie puli, chunking długiego opóźnienia, zarządzane stop/pauza/wznowienie |
| `test_hal_ds18b20` | nieblokujący przepływ żądanie/odpytanie/pobierz-ostatnią, zachowanie stanu zajętości, obsługa CRC/obecności |
| `test_hal_dht` | timing transakcji GPIO DHT, obsługa sumy kontrolnej, cache'owane gettery próbek i przywracanie sekcji krytycznej |
| `test_hal_onewire` | wrappery reset/read/write/select/search, pomocnicy CRC8/CRC16 i blokowanie magistrali mock |
| `test_hal_rtc` | init/get/set datetime RTC, raportowanie źródła zegara wewnętrznego/zewnętrznego, dispatch providera nie-I2C, pełna walidacja gregoriańska, granice epoki 1970/2000/2099 i przepełnienie, flaga integralności, maska przerwań, flagi zdarzeń czyszczone przy odczycie, zachowanie jednorazowe/stanowe względnego wybudzenia, konfiguracja CLKOUT/timera/alarmu, starsze zabezpieczenia nieprawidłowego wejścia i mapowanie statusu `_ex` |
| `test_jh_rtc_i2c_provider` | Wybór współdzielonego providera PCF8563/DS3231, metadane, translacja datetime/zdarzeń, bezpieczna kontrola CLKOUT DS3231, propagacja błędów I2C i mapowanie statusu możliwości backendu przez mock HAL I2C |
| `test_stm32_rtc_codec` | Kodowanie rejestrów BCD TR/DR i Alarmu A RTC STM32G474, odrzucanie kalendarza/zakresu, ograniczenia dzień-kontra-dzień tygodnia, preskalery LSE/LSI 1 Hz oraz zaokrąglanie/granice licznika wybudzenia |
| `test_rtc_architecture` | Posiadanie jednej fasady RTC, granice providera I2C/wewnętrznego, współdzielona walidacja/blokowanie, drivery chipów tylko-HAL, dispatch WUT STM32G474 i alarmu AON RP oraz okablowanie manifestu źródeł |
| `test_hal_power` / `test_hal_power_header_c` | Możliwości zasilania, walidacja żądań, kolejność callbacków, wybudzenie RTC, monotoniczny upływ czasu, zachowanie mocka w stylu resetu, czyszczenie i kompatybilność nagłówka C |
| `test_power_architecture` | Osobne posiadanie RTC/zasilania, obecność backendu docelowego, mapowania STM32 STOP/Standby, integracja AON RP, generowana zależność funkcjonalności i okablowanie przykładu |
| `test_jh_calendar` | Walidacja roku przestępnego/długości miesiąca/dnia tygodnia gregoriańskiego, niemożliwe daty, zerowa epoka Unix, round-trip dnia przestępnego, górna granica RTC i statusy przepełnienia 64-bitowego |
| `test_calendar_architecture` | Posiadanie współdzielonego źródła kalendarza, starsze wrappery czasu tylko-HAL oraz odrzucanie algorytmów kalendarza lokalnych dla targetu/drivera lub kopii `hal_time_from_components()` |
| `test_hal_eeprom` | zapis-odczyt bajtu/inta, flaga `commit` |
| `test_hal_serial` | Granice wire/message serial, wstrzyknięcie binarnego RX + `available`/`read`, prefiksy debug zadania/ISR, akceptowane/odrzucone znaczniki czasu, konfiguracja/cykl życia/izolacja źródła limitu szybkości, strumieniowe formatowanie ponad `HAL_DEBUG_BUF_SIZE`, podsumowania pierścienia/odrzuceń odroczone przez ISR, semantyka wyciszenia i flush |
| `test_hal_serial_session` | Cykl życia ramkowanego HELLO/AUTH, deterministyczne i kolejne losowe wyzwania (challenges), zachowanie fail-closed przy braku entropii, czyszczenie wyzwań, kompatybilność poleceń, dispatch nieznanego handlera, echo seq, odrzucanie zniekształconych ramek i bezpieczeństwo argumentów null |
| `test_hal_serial_commands`, `test_hal_serial_commands_header_c`, `test_hal_serial_commands_header_cpp` | Bramkowanie sesji aktywnej i wybrany routing przed HELLO, parsowanie nazwy/argumentów SC, metadane sekwencji/sesji/uwierzytelniania, dosłowne odpowiedzi tekstowe, starsze formatowanie, fallback prefiksu, granice ładunku, posiadanie callbacków, bezpieczeństwo cyklu życia reentrant oraz samodzielne nagłówki C/C++ |
| `test_hal_sc_auth` | Stabilne wektory klucza/odpowiedzi na urządzenie, czyszczenie wyjścia przy nieprawidłowym wejściu i współdzielone porównanie MAC w stałym czasie |
| `test_jh_security_primitives` | Bezpieczne zerowanie, zachowanie równości/niezgodności w stałym czasie, deterministyczny wektor entropii mock i czyszczenie wyjścia przy błędzie |
| `test_security_architecture` | Skompilowane posiadanie Serial Session/auth, jedna współdzielona implementacja entropii/zerowania/porównania w stałym czasie, adopcja BLE i okablowanie manifestu źródeł |
| `test_serial_architecture` | Jeden współdzielony rdzeń serial/debug, trzy kompletne porty transportu dowiązywane w czasie linkowania, odrzucanie duplikacji rdzenia targetu i okablowanie manifestu źródeł |
| `test_hal_swserial` | ścieżki sukcesu/porażki statusu software UART, wyczerpanie puli, wstrzyknięcie RX, przechwycenie TX, format ramki i przypisanie pinów |
| `test_rp2040_swserial_backend` | Strażnik wyboru źródeł RP2040: wymagane programy PIO Pico SDK; zabronione implementacje serial wrapper, callbacki RX GPIO, mikrosekundowe opóźnienia bitów i sekcje krytyczne HAL |
| `test_hal_uart` | wstrzyknięcie RX sprzętowego UART, przechwycenie TX, przypisanie pinów |
| `test_hal_spi` | init/reinit SPI, reset, blokady per-magistrala, transfery, walidacja statusu i mapowanie błędów DMA |
| `test_hal_lora_radio_lifecycle` | Limity alokacji nieprzezroczystego uchwytu, przestarzałe uchwyty, czyszczenie cyklu życia i propagacja błędów providera |
| `test_hal_lora_radio` | Profile i presety SX1262, limity modelu SX1261, blokujące TX, ograniczone/ciągłe odpytujące RX, diagnostyka przepełnienia/CRC/timeoutu, stan zasilania, czas nadawania (time-on-air) oraz dwie połączone radiostacje mock |
| `test_hal_lora_link` | Domyślne ustawienia/cykl życia łącza, przestarzałe uchwyty, adresowana fragmentacja plaintext i AEAD, utrata/retransmisja ACK, ograniczony timeout łącznie z maksymalną liczbą ponowień, tłumienie duplikatów dostarczenia, integralność całej wiadomości, rekonstrukcja poza kolejnością/niekompletna, uszkodzone pakiety podczas oczekiwania na ACK, odzyskiwanie od startu późniejszego fragmentu i serializacja równoległego wysyłania przez połączone radiostacje mock |
| `test_lora_link_frame` | Ścisłe wersjonowane kształty ramek, pojemność/round-trip plaintext, szyfrowanie uwierzytelnione, odrzucanie manipulacji nagłówka/szyfrogramu, kodowanie ACK, obcięcie i granice wyjścia |
| `test_hal_lora_link_header_c`, `test_hal_lora_link_header_cpp` | Samodzielna kompatybilność publicznego nagłówka łącza w C11 i C++17 |
| `test_lora_link_plain_compile` | Ścisła build z ostrzeżeniami jako błędami dla kodeka łącza i ramki bez opcjonalnej funkcjonalności kryptograficznej |
| `test_sx126x_adapter` | Orkiestracja poleceń oficjalnego drivera, wybór PA i OCP SX1261/SX1262, czyszczenie transakcji SPI, terminy BUSY, poziomy przełącznika RF, konfiguracja elektryczna, kalibracja pasma, timeout TX i mapowanie IRQ CRC RX |
| `test_hal_lora_sx127x` | Walidacja deskryptora specyficznego dla modelu SX1276/SX1278 oraz cykl życia wspólnej fasady, możliwości, granica kalibracji, TX, RX, CAD i stany zasilania |
| `test_sx127x_adapter` | Transport rejestrów SX127x, sonda wersji, konfiguracja modemu/częstotliwości/PA, mapowanie IRQ/status, metadane FIFO, RSSI, CAD, timeout, anulowanie, błędy magistrali i zachowanie sen/wybudzenie TCXO |
| `test_hal_pga2311` | Walidacja statusu/konfiguracji PGA2311, wyczerpanie puli, wstrzyknięte błędy SPI i ponowienie, zapisy ramek, konwersja dB/kod, zachowanie wyciszenia programowego/sprzętowego |
| `test_irsmall_decoder_driver` | Dekodowanie ramek NEC/NECx/SIRC/Samsung IRsmallDecoder, dekodowanie tabeli przejść RC5 wraz z rozszerzonym bitem polecenia, raportowanie powtórzenia/przytrzymania, reset timeoutu i ścieżki wyłączenia/włączenia przerwania |
| `test_hal_i2c` | ścieżki transferu i statusu bus0/bus1, bezpośrednie pomocniki odczytu, blokowanie, init/deinit, czyszczenie magistrali, ograniczone wyniki skanowania, zachowanie tylko-licznik/przepełnienie i pokrycie callbacku per-adres |
| `test_hal_rgb_led` | init/init_ex zorientowane na status, nieprawidłowa konfiguracja, błąd alokacji/transportu, ponowienie, ograniczenie jasności, wyłączenie i strażnik przed inicjalizacją |
| `test_hal_display` | API wyświetlacza zorientowane na status, możliwości/reguły surowego zapisu, formatowanie/rozmiar tekstu, presety, rysowanie, init SSD1306, stan strumieniowania/asynchronicznego DMA, walidacja i wstrzyknięte błędy I/O backendu |
| `test_hal_can` | wysyłanie/odbieranie, bufor pierścieniowy, strażnik null-data, ograniczenie ładunku, wybór backendu, walidacja ramki classic-kontra-FD, API filtrów, `hal_can_process_all`, `hal_can_create_with_retry`, `hal_can_encode_temp_i8` |
| `test_hal_thermocouple` | wstrzyknięcie MCP9600 + MAX6675, zwroty NAN dla nieobsługiwanej operacji, rozdzielczość ADC, włącz/wyłącz, alert/status |
| `test_max6675_driver` | Współdzielone surowe dekodowanie MAX6675, usterka obwodu otwartego, konfiguracja pinów GPIO i sekwencja odczytu bit-bang |
| `test_mcp9600_driver` | Obsługa ID urządzenia współdzielonego MCP9600/MCP9601, transakcje rejestrów, dekodowanie stałoprzecinkowe, rozszerzenie znaku ADC, zachowanie bitów konfiguracji, alert/status i starsze mapowanie rozdzielczości temperatury otoczenia |
| `test_bh1750_driver` | Współdzielona komenda inicjalizacji BH1750, opóźnienie pierwszego pomiaru, routing magistrali I2C i dwubajtowe dekodowanie luksów |
| `test_adp5360_driver` | Walidacja ID urządzenia współdzielonego ADP5360, przepływy rejestrów ładowarki/fuel-gauge/regulatora, konwersja statusu, błędy I2C i pokrycie muteksu instancji |
| `test_simple_io_drivers` | Współdzielone sekwencje inicjalizacji MCP23017/PCA9654E/PCF8574/74HC595/MCP3221/MCP4725, ścieżki zapisu/odczytu per-pin/pełny port, konfiguracja odwrócenia/pull-up/IRQ i pokrycie muteksu instancji |
| `test_hd44780_driver` | Współdzielona inicjalizacja GPIO HD44780, ramkowanie poleceń 4-bit/8-bit, przesunięcia wierszy kursora, zapisy CGRAM, ścieżka print/write i pokrycie muteksu instancji |
| `test_hal_dma_pwm_audio` | Cykl życia mock DMA PWM-audio, dispatch callbacków, pauza/wznowienie i pokrycie interpolacji |
| `test_dacless_driver` | Normalizacja konfiguracji współdzielonego DACless, przepływ ponownego wypełniania callbacku próbki/bloku DMA i odpytywania, bufor ADC, wyciszenie/wyłączenie wyciszenia, pomocnicy interpolacji i pokrycie muteksu |
| `test_tsc2007_driver` | Współdzielony układ bajtu poleceń TSC2007, dekodowanie 12-bitowej odpowiedzi, sekwencja odczytu dotyku, odrzucanie stabilności, routing magistrali i pokrycie muteksu instancji |
| `test_stmpe610_driver` | Współdzielona sekwencja konfiguracji STMPE610, sondowanie ID chipu, transakcje I2C/SPI/rejestrów, dekodowanie FIFO, ścieżka bit-bang soft-SPI i pokrycie muteksu instancji |
| `test_ads1x15_driver` | Współdzielona konfiguracja rejestrów ADS1X15, odczyty konwersji ADS1115/ADS1015, mapowanie wzmocnienia/trybu/szybkości danych, zapisy progu komparatora i przekazywanie zegara I2C |
| `test_hal_external_adc` | konfiguracja zakresu ADS1115, odczyty surowe/skalowane per kanał, bezpieczeństwo poza zakresem |
| `test_hal_gps` | współdzielona publiczna ścieżka enkodowania/getterów NMEA, wstrzyknięcie lokalizacji/prędkości/daty/czasu i rozszerzonego fixa, flagi ważności/aktualizacji/wieku, reset i diagnostyka |
| `test_gps_architecture` | Posiadanie jednej fasady transportu GPS, usunięte kopie targetu, współdzielone posiadanie getterów/silnika, granica wstrzykiwania mocka i okablowanie manifestu źródeł |
| `test_hal_system` | zachowanie delay/millis/micros, bezpieczne wobec zawijania (wrap-safe) pomocnicy nieblokujący `hal_millis_interval_*` (warianty upływu czasu + callbacku), flagi watchdoga, pomocnicy sterty/temperatury chipu, niezależne od typu `hal_constrain`/`hal_map` (w tym strażnik równego zakresu), `COUNTOF`, `hal_u32_to_bytes_be`, `NONULL` |
| `test_hal_bits` | makra pomocnicze bitów (`is_set`, `set_bit`, `clr_bit`, `bitSet`, `bitClear`, `bitRead`, `set_bit_v`, `clr_bit_v`) |
| `test_hal_wifi` | tryb/nazwa hosta/RSSI/ping, wstrzyknięcie IP/DNS/MAC, walidacja wejścia |
| `test_hal_net` | współdzielony kształt punktu końcowego/statusu, limity sieci, zachowanie resolvera literału IPv4/localhost/mock-DNS |
| `test_hal_littlefs` | przepływ mount/unmount, statystyki rozmiaru, pomocnicy istnienia/usuwania ścieżki, zachowanie sukcesu/porażki formatowania, bezpośrednie operacje statusu, semantyka brakującej ścieżki i niezamontowanego stanu, walidacja wejścia |
| `test_hal_sdlogger` | numerowanie plików wspierane przez EEPROM, buforowany flush/close logu, formatowanie raportu awarii, ścieżki błędów SD/otwarcia |
| `test_hal_udp` | przepływ begin/parse/read, wiązanie/RX/TX wielu gniazd oparte na uchwytach, odczyty datagramów w kawałkach, przechwycenie punktu końcowego zdalnego/reset przy stopie, ścieżki beginPacket jawne/nadawcy zdalnego, zachowanie write/endPacket, walidacja wejścia |
| `test_hal_tcp` | połączenie/wysyłanie/odbieranie/zamknięcie klienta TCP, wiązanie/nasłuchiwanie/akceptacja listenera, limity kolejki oczekujących/puli, sondy gotowości i niezależność zaakceptowanego gniazda |
| `test_hal_http_server` | dispatch tras HTTP, parsowanie zapytania/ciała/nagłówka, trasy dokładne/prefiksowe, nagłówki/ciało odpowiedzi, obsługa HEAD, błędy handlera i nieprawidłowa konfiguracja |
| `test_hal_http_files` | Serwowanie plików HTTP wspierane przez callback, mapowanie MIME, ETag/`If-None-Match`, surowy PUT, upload multipart i odrzucanie przechodzenia po ścieżkach (path traversal) |
| `test_hal_websocket` | Handshake HTTP Upgrade, `Sec-WebSocket-Accept`, maskowane ramki tekstowe, broadcast, ping/pong, callbacki zamknięcia i nieprawidłowe handshake |
| `test_hal_net_console` | Start konsoli TCP wymagającej hasła i przepływ uwierzytelniania, lustrzenie serial/debug do uwierzytelnionych klientów, broadcast do wielu klientów, dwukierunkowe wejście poleceń, odpowiedzi per-klient i callbacki rozłączenia |
| `test_hal_net_commands` | Rejestracja i dispatch poleceń JSON/text, integracja tras HTTP, integracja wiadomości WebSocket, ustrukturyzowane błędy i walidacja API |
| `test_hal_notify` | Walidacja fasady powiadomień, dispatch fałszywego backendu, żywotność uchwytu sprawdzana przez generację, JSON żądania Telegram, odrzucenie publicznego hosta HTTP i mapowanie limitu szybkości |
| `test_bsd_sockets` | Mapowanie fd adaptera BSD/POSIX, translacja sockaddr, ścieżki errno/EAI, przepływ TCP/UDP, tryb nieblokujący, `select()`, `getaddrinfo()` i `setsockopt()` |
| `test_bsd_socket_headers_c` | przenośne deklaracje, stałe i struktury C dla nagłówków socket BSD; działa pod hostami zbliżonymi do GNU oraz MSVC |
| `test_hal_tls` / `test_bearssl_provider` | publiczny cykl życia TLS, natywny transport HAL TCP, ograniczona progresja BearSSL i opcjonalne callbacki TLS-nad-BSD |
| Sondy buildu TLS/BSD | dowodzą, że TLS kompiluje się bez BSD, BSD kompiluje się bez TLS, a każda flaga propaguje tylko wymagane przez nią moduły sieciowe |
| `test_bsd_sockets_c_compile` | test smoke buildu/linkowania C dla nagłówków socket, `netdb.h`, kształtów klient/serwer TCP/UDP, `fcntl()`, `select()`, `getaddrinfo()` i `setsockopt()` |
| `test_hal_wireguard` | walidacja parsera IPv4, ścieżki begin/begin_advanced/kick WireGuard z tablicy bajtów i tekstu, raportowanie punktu końcowego peer-up (`hal_wireguard_peer_up` + `hal_wireguard_peer_up_quick`), wyzwalacz kick handshake, walidacja wejścia |
| `test_hal_mqtt` | przepływ serwer/połączenie, przechwycenie publish/subscribe/unsubscribe, dispatch callbacku z `hal_mqtt_loop`, strażnicy nieprawidłowego wejścia |
| `test_hal_network_status` | Walidacja API statusu WiFi/DNS, TCP/UDP, MQTT i WireGuard między modułami, inicjalizacja wyjścia, wyczerpanie puli, mapowanie stanu i błędu |
| `test_hal_ota` | Settery konfiguracji OTA, przepływ begin/is_started, status/potwierdzenie rozruchu, dispatch callbacku ze wstrzykniętych zdarzeń start/progress/error/end, przepływ zamiany/wyrejestrowania callbacku, zachowanie czyszczenia kolejki przy ponownym begin, strażnicy nieprawidłowego wejścia |
| `test_ota_protocol` | Ścisła gramatyka zaproszenia/AUTH2, normalizacja numeryczna i szesnastkowa, dokładna tożsamość punktu końcowego UDP, wiązanie pola transkryptu, porównanie tagu o stałym kształcie i współdzielony wektor HMAC-SHA256 hosta/urządzenia |
| `test_ota_image` | Wersjonowany manifest OTA i redundantne kodowanie stanu rozruchu, walidacja CRC/HMAC, obsługa uszkodzeń, zawijanie sekwencji i wybór najnowszego rekordu |
| `test_ota_swap_engine` | Wznawialna zamiana sektora program/staging przez każdą symulowaną granicę błędu przed/po mutacji, wycofanie odwrotnej zamiany i odrzucanie uszkodzonej fazy |
| `test_rp_ota_artifacts` | Natywny pomocnik pakowania OTA RP, w tym wyrównanie sektora RP2040-E14, zachowanie rzeczywistej strony, renumeracja UF2 i odrzucanie nakładania się |
| `test_hal_time` | współdzielony setter/status, 64-bitowa monotoniczna progresja przez zawinięcie 32-bitowe, przywrócenie RTC i trwałość NTP, stan sukcesu/porażki NTP, formatowanie strefy czasowej/lokalne, konwersja komponentów, CET/CEST, zakresy i ekstrakcja minut |
| `test_hal_kv` | CRUD u32/blob, usuwanie, pomijanie niezmienionych, GC, równoległe aktualizacje, bezpośrednia propagacja statusu EEPROM, błędy niezainicjalizowania/zakresu/pojemności i inicjalizacja wyjścia |
| `test_hal_crypto` | Zachowanie pomocników Base64/MD5/jednorazowego i przyrostowego SHA-256/HMAC-SHA256/ChaCha20/ChaCha20-Poly1305, walidacja wejścia oraz regresyjne sprawdzenia odrzucania zawinięcia licznika ChaCha20 |
| `test_wireguard_crypto_shared` | współdzielone prymitywy kryptograficzne WireGuard (`crypto_equal/zero`, BLAKE2s, X25519, ChaCha20, ChaCha20-Poly1305 w tym wektory detached AEAD RFC8439 IETF) |
| `test_hal_soft_timer` | pokrycie wrappera C: create/begin/tick/abort/restart, konfiguracja tabeli/pomocnicy tick, ścieżka callbacku delay/idle, walidacja nieprawidłowego wejścia (`NULL` tabeli / `count==0`) |
| `test_SmartTimers` | `tick`, wywołanie callbacku, `abort`, `restart` (zachowanie rdzenia używane przez `hal_soft_timer_*`) |
| `test_pidController` | wyjście P, ograniczanie wyjścia, reset całkowania, wykrywanie stabilności (zachowanie rdzenia używane przez `hal_pid_controller_*`) |
| `test_multicoreWatchdog` | bramkowanie żywotności dwóch rdzeni, ścieżka resetu zewnętrznego, bezpieczeństwo no-op przed setupem |
| `test_tools` | pokrycie narzędzi z `tools.cpp` przy użyciu mocków HAL, w tym `debugInit`, `setDebugPrefixWithColon`, pomocnicy numeryczni/łańcuchowi, starsze wrappery czasu delegujące do HAL i pomocnicy formatowania bezpieczni dla bufora |
| `test_hal_critical_section` | zagnieżdżanie sekcji krytycznej i zachowanie przywracania stanu przerwań |
| `test_hal_dac` | kompatybilność init DAC plus zorientowane na status zapisy surowe/w miliwoltach, walidacja kanału/zakresu/niezainicjalizowania i raportowanie nieobsługiwanego targetu |
| `test_hal_digipot` | zachowanie init/set fasady MCP401x/MAX5395, walidacja zakresu i mapowanie statusu |
| `test_hal_pcnt` | sukces init/odczytu/resetu licznika impulsów, nieprawidłowe argumenty, niezainicjalizowane kanały i wrappery kompatybilności |
| `test_hal_i2c_slave` | mapa rejestrów I2C-slave, callbacki, transakcje RX/TX i obsługa nieprawidłowego wejścia |
| `test_hal_serial_session_vocabulary` | stałe słownika poleceń/statusu sesji serial i pomocnicy konwersji |
| `test_hal_status` | współdzielone wartości `hal_status_t`, konwersja na string, predykaty i adaptery bool/status |
| `test_hal_modem_at` | parsowanie poleceń/odpowiedzi ogólnego silnika AT, URC, timeouty i dispatch callbacków |
| `test_hal_simcom_a76xx` | przepływy poleceń SIMCom A76xx power/SIM/PDP/GNSS/LBS/MQTT i obsługa URC |
| `test_pcf8563_driver` | współdzielone kodowanie rejestrów PCF8563, datetime, alarm, timer, CLKOUT i zachowanie integralności |
| `test_ds3231_driver` | współdzielony datetime DS3231, pełne zapisy kalendarza, alarm, status, bezpieczny dla oscylatora CLKOUT, temperatura, błędy I2C i zachowanie rejestrów |
| `test_ili9341_driver` | współdzielona sekwencja komend/init ILI9341, okna adresowe i zapisy pikseli |
| `test_st77xx_driver` | współdzielona inicjalizacja ST7735/ST7789/ST7796S/GC9A01, przesunięcia, okna i zapisy pikseli |
| `test_ssd1306_driver` | współdzielona inicjalizacja rodziny SSD1306, aktualizacje bufora ramki, przesunięcia adresowania kontrolera, wstrzymanie/wznowienie i transfery komend/danych I2C/SPI |
| `test_rgb_oled_driver` | współdzielona inicjalizacja SSD1331/SSD135x, przepływ komend kontrastu/remapu, okna adresowe i zapisy pikseli RGB565 |
| `test_st7567_driver` | współdzielona inicjalizacja ST7567, rozmiar bufora strony, zapisy wyrównane do strony i walidacja nieprawidłowego obszaru |
| `test_hal_display_rgb_oled_facade` | rzeczywisty wybór backendu przez współdzieloną fasadę dla capabilities SSD1331/SSD135x, surowe zapisy RGB565, GFX i limity rotacji nad mock SPI |
| `test_hal_display_st7567_facade` | rzeczywisty wybór backendu przez współdzieloną fasadę dla capabilities MONO01/MONO10 ST7567, przełączanie formatu i surowe zapisy wyrównane do strony nad mock SPI |
| `test_jh_gfx_geometry` | współdzielone przycinanie GFX, prymitywy geometrii, bitmapa i zachowanie układu tekstu |
| `test_mcp2515_driver` | współdzielone transakcje rejestrów/SPI MCP2515, timing bitów, TX/RX, filtry i błędy |
| `test_mfrc522_driver` | współdzielone transporty rejestrów MFRC522, inicjalizacja i pomocnicy protokołu RFID |
| `test_pn532_driver` | współdzielone ramkowanie SPI/I2C/UART PN532, parsowanie ACK/odpowiedzi i polecenia NFC |
| `test_ff16_memdisk` | zarządzana integracja FatFs R0.16 nad dyskiem w pamięci, montowanie i zachowanie I/O plików |
| `test_stm32_pwm_clock` | pokrycie obliczeń zegara timera PWM, preskalera i okresu STM32G474 |
| `test_hal_onewire_driver` | współdzielony timing bit-bang OneWire, reset/obecność, I/O bitu/bajtu i zachowanie wyszukiwania |
| `test_hal_config_storage_flags` | pokrycie buildu/runtime dla propagacji flag funkcjonalności pamięci masowej i konfiguracji |
| `test_jpeg` | zarządzane dekodowanie TJpgDec, wymiary, konwersja RGB565 i nieprawidłowe wejście |
| `test_lodepng` | zarządzane kodowanie/dekodowanie LodePNG, posiadanie pamięci, konwersja i obsługa błędów |
| `test_gps_nmea_parser` | ramkowanie/suma kontrolna NMEA, parsowanie fixa/daty/czasu/prędkości i odzyskiwanie po nieprawidłowym wejściu |
| `test_stm32_hal_system` | zegar systemowy STM32G474, stan resetu/faultu i symulacja usługi systemowej backendu |
| `test_stm32_hal_i2c_slave` | backend rejestrowy I2C-slave STM32G474, zdarzenia, callbacki i obsługa błędów |
| `test_freertos_posix_runtime` | Scheduler FreeRTOS POSIX hosta, dispatch zadań, mutex/delay i leniwe zachowanie create-once, w tym granice wiadomości serial/debug przy współbieżności |

### Dodawanie nowego zestawu testów

1. Utwórz `tests/test_<name>.cpp` z `#include "utils/unity.h"`, wywołaniami
   Unity `setUp`, `tearDown`, `UNITY_BEGIN`, `RUN_TEST` i `UNITY_END`.
2. Dodaj `add_hal_test(test_<name>)` do `tests/CMakeLists.txt`.
    Dla zestawów kompilujących dodatkowe źródła (na przykład `test_tools` i
    `test_multicoreWatchdog`), utwórz dedykowany wpis `add_executable(...)`.
3. Przekompiluj:
   `cmake --build .build/host && ctest --test-dir .build/host`.

### Sterowanie czasem mock

SmartTimers i PIDController zależą od `hal_millis()`.
Zegar mock startuje od 0 i jest sterowany przez:

```cpp
hal_mock_set_millis(uint32_t ms);     // ustaw czas bezwzględny
hal_mock_advance_millis(uint32_t ms); // przesuń względem bieżącego
hal_mock_timer_advance_us(uint64_t us); // wyzwala oczekujące alarmy hal_timer
```

**Ważne:** `SmartTimers` używa `_lastTime == 0` jako wartownika stanu
"niezainicjalizowanego". Ustaw czas mock na wartość niezerową (np.
`hal_mock_set_millis(1000)`) przed wywołaniem `SmartTimers::begin()`, aby
uniknąć uruchomienia się tego strażnika w testach.

---

*Powrót do [Dokumentacji API JaszczurHAL](../../pl/JaszczurHAL_API.md)*

*Dalej: [Bezpieczeństwo wielordzeniowe, drivery, przewodnik migracji](04_multicore_drivers_migration.md)*
