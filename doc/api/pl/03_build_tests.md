<a id="zależności-buildu-testy-i-stanowiska-testowe-sprzętu"></a>

# Kompilacja, testy automatyczne i testy na sprzęcie

*Dostępne również [po angielsku](../en/03_build_tests.md).*

> **Część [Dokumentacji API JaszczurHAL](../../pl/JaszczurHAL_API.md)**

Ten rozdział opisuje zależności potrzebne do kompilacji, uruchamianie testów na komputerze oraz procedury sprawdzania biblioteki na urządzeniach. Wynik kompilacji, test z użyciem mocka i test na fizycznym sprzęcie potwierdzają różne właściwości - nie należy traktować ich zamiennie.

<a id="zależności-build-sprzętowy"></a>

## Zależności przy kompilacji dla urządzenia

| Moduł HAL | Zależność zewnętrzna |
|---|---|
| Model komponentowy ESP32-S3 | Wersja ESP-IDF wskazana przez repozytorium, z jednym generowanym grafem źródeł i zależności: podstawowy kod systemu i prostego PWM, opcjonalnie wybierane peryferia fazy 2 oraz natywna łączność i usługi fazy 3. |
| `hal_gpio`, `hal_pwm`, `hal_adc`, `hal_system` | API `hardware_*` / `pico_*` Pico SDK w rodzinie RP; backend rejestrowy STM32G474; usługi GPIO, LEDC PWM, ADC i systemowe ESP-IDF dla ESP32-S3. `hal_system` używa też API zadań FreeRTOS w obsługiwanych konfiguracjach `HAL_ENABLE_FREERTOS`. |
| `hal_usb` | Urządzenie TinyUSB zarządzane przez HAL na RP: deskryptory CDC, obsługa IRQ i timera w konfiguracjach bare-metal, zadanie robocze na rdzeniu 0 w konfiguracjach FreeRTOS oraz reset do trybu BOOTSEL. STM32G474 nie jest obecnie obsługiwany. Mock udostępnia deterministyczne bufory CDC i pozwala wykryć reset. |
| `hal_serial` | Jedna niezależna od targetu implementacja portu szeregowego i debugowania oraz porty dołączane podczas linkowania: RP CDC `hal_usb`, VFS USB Serial/JTAG uruchamiany podczas startu ESP32-S3, debugowy USART2 lub stdout hosta na STM32G474 oraz przechwytywanie stdout i dane RX ustawiane przez testy w mocku. |
| `hal_sync` | RP: `pico/mutex.h` z Pico SDK w konfiguracjach bare i semafory FreeRTOS w konfiguracjach RTOS. STM32G474: atomowa spinlock w konfiguracjach bare i muteksy FreeRTOS w konfiguracjach RTOS. ESP32-S3: muteksy FreeRTOS z ESP-IDF oraz sekcje krytyczne `portMUX_TYPE`. |
| `hal_timer` | RP2040: API alarmów/czasu Pico SDK (`pico/time.h`); STM32G474: backend rejestrowy TIM6 + NVIC; ESP32-S3: domyślny GPTimer ESP-IDF oraz dedykowane pule. |
| `hal_soft_timer` | wewnętrzne narzędzie `SmartTimers` |
| `hal_pid_controller` | wewnętrzne narzędzie `pidController` |
| `hal_can` | ogólna fasada CAN plus sterowniki CAN wybierane przez backend: MCP2515 (`hal/can/mcp2515/*`), MCP251XFD (`hal/can/mcp251xfd/*`) oraz natywny FDCAN STM32G474 (`impl/stm32g474/hal_can_stm32g474_fdcan.*`) |
| `hal_display` | Współdzielony stos wyświetlacza (`hal/display/drivers/hal_display.cpp`, `jh_gfx.*`, `ili9341_driver.*`, `st77xx_driver.*`, `ssd1306_driver.*`) używany przez RP2040 i STM32G474; backendy targetowe dostarczają transport SPI/I2C/GPIO |
| `hal_hd44780` | współdzielony sterownik znakowego LCD kompatybilnego z HD44780 (`hal/display/hd44780/hd44780.*`) nad HAL GPIO/timingiem systemowym |
| `hal_dma_pwm_audio` | obsługa transmisji DMA próbek PWM audio, taktowana timerem, używany przez DACless na RP2040, STM32G474 i mocku |
| `hal_dacless` | współdzielony silnik audio-PWM DACless (`hal/audio/dacless/dacless.*`) nad HAL DMA/PWM-freq, ADC, timingiem i synchronizacją |
| `hal_tsc2007` | współdzielony sterownik rezystancyjnego kontrolera dotyku TSC2007 (`hal/input/tsc2007/tsc2007.cpp`) nad HAL I2C/timingiem systemowym |
| `hal_stmpe610` | współdzielony sterownik rezystancyjnego kontrolera dotyku STMPE610 (`hal/input/stmpe610/stmpe610.cpp`) nad HAL I2C lub HAL SPI/GPIO |
| `hal_irsmall_decoder` | współdzielony dekoder odbiornika IR (`hal/input/irsmall_decoder/irsmall_decoder.cpp`) nad przerwaniami HAL GPIO i timingiem systemowym |
| `hal_spi` | natywny `hardware/spi.h` Pico SDK dla RP2040; backend rejestrowy STM32G474; ESP-IDF SPI master na SPI2/SPI3 dla ESP32-S3. |
| `hal_lora_radio` | Wzajemnie wykluczające się backendy rodzin układów: oficjalny sterownik Semtech SX126x w wersji wskazanej przez repozytorium, z adapterem HAL dla zwalidowanego SX1262 i eksperymentalnego SX1261, sprawdzonego wyłącznie programowo, albo własny backend rejestrowy HAL dla eksperymentalnych SX1276/SX1278, również sprawdzonych wyłącznie programowo. Oba kompilują się dla RP i STM32G474 i są deterministycznie testowane z użyciem mocka. |
| `hal_lora_link` | Własny protokół HAL nad jednym skonfigurowanym `hal_lora_radio`; CRC-32 jest wewnętrzne, ChaCha20-Poly1305 używa opcjonalnego modułu `hal_crypto`, i nie wprowadza się żadnej dodatkowej zależności zewnętrznej |
| `hal_i2c` | natywny `hardware/i2c.h` Pico SDK dla RP2040; backend rejestrowy STM32G474; ESP-IDF I2C master na I2C0/I2C1 dla ESP32-S3. |
| `hal_swserial` | natywny backend PIO/DMA Pico SDK na RP2040; współdzielony backend HAL GPIO/timing/sync na pozostałych targetach |
| `hal_gps` | jedna przenośna fasada wybierająca `hal_uart` / `hal_swserial` podczas kompilacji, plus współdzielony wbudowany silnik NMEA |
| `hal_rgb_led` | współdzielony rdzeń NeoPixel (`hal/gpio/neopixel/jh_neopixel.*`) + implementacja transportu targetowego, w tym RMT na ESP32-S3 |
| `hal_thermocouple` (MCP9600/MCP9601) | współdzielony sterownik (`hal/temperature/mcp9600/mcp9600_driver.*`) |
| `hal_thermocouple` (MAX6675) | współdzielony sterownik (`hal/temperature/max6675/max6675_driver.*`) |
| `hal_onewire` | współdzielony sterownik bit-bang (`hal/onewire/onewire_driver.*`) nad HAL GPIO/czasem |
| `hal_ds18b20` | współdzielony backend DS18B20 (`hal/temperature/ds18b20/hal_ds18b20.cpp`) nad współdzielonym OneWire |
| `hal_external_adc` | współdzielony sterownik ADS1X15/ADS1115 (`hal/analog/ads1x15/ads1x15_driver.*`) |
| `hal_pga2311` | współdzielony sterownik stereo-głośności PGA2311 (`hal/audio/pga2311/pga2311_driver.*`) nad HAL SPI/GPIO |
| `hal_wifi` | sterownik CYW43/lwIP w wersji wskazanej przez repozytorium na RP i STM32G474 lub natywne WiFi ESP-IDF/`esp_netif`/lwIP na ESP32-S3 |
| `hal_littlefs` | jedna niezależna od targetu fasada i wspólny provider oparty na `third_party/littlefs` w wersji wskazanej przez repozytorium; RP i STM32G474 określają geometrię pamięci i udostępniają skoordynowane operacje bezpośredniego dostępu do wewnętrznego flash, a osobny test integracyjny hosta używa modelu flash w RAM |
| `hal_udp` | wspólna implementacja bezpośredniego API UDP lwIP działająca na wybranym backendzie sieciowym CYW43 |
| `hal_tls` | wbudowany BearSSL nad natywnym `hal_tcp`; opcjonalny adapter transportu BSD jest kompilowany tylko, gdy dodatkowo włączono `HAL_ENABLE_BSD_SOCKETS` |
| Adapter BSD sockets | współdzielony `hal/network/adapters/bsd/hal_bsd_sockets.cpp` nad HAL UDP/TCP; pozostaje niezależnie wybieralny bez TLS |
| `hal_wireguard` | wspólna implementacja WireGuard/lwIP oraz backend udostępniany przez target korzystający z lwIP |
| `hal_mqtt` | wbudowany `PubSubClient` nad HAL TCP, z opcjonalnym transportem MQTTS przez BearSSL |
| `hal_notify` | fasada wybierająca backend oraz Telegram korzystający ze współdzielonego klienta HTTP/HTTPS |
| `hal_ota` | staging/aplikator RP z uwierzytelnionym transportem VS Code nad HAL UDP/TCP |
| `hal_time` | Wspólne funkcje kalendarza gregoriańskiego, konwersji CET/CEST i interwałów oraz klient NTP korzystający z HAL UDP i zegara platformy |
| `hal_kv` | wewnętrzne `hal_eeprom` + `hal_sync` |
| `hal_sdlogger` | rdzeń FatFs R0.16 w wersji wskazanej przez repozytorium oraz współdzielona warstwa plikowa w `hal/storage/filesystem/` |
| `tools` | API HAL |
| `multicoreWatchdog` | wewnętrzne `SmartTimers` + muteks `hal_sync` |

<a id="zależności-mock--build-pc"></a>

## Zależności testów na komputerze

Wszystkie pliki `impl/.mock/` zależą tylko od standardowych nagłówków hosta,
takich jak `<cstdio>`, `<cstring>`, `<mutex>`, `<queue>` i `<stdarg.h>`. Żadne
SDK wbudowane nie jest wymagane.

---

<a id="mapa-systemu-testów-i-miarodajne-źródła"></a>

## Organizacja testów i ich konfiguracja

| Warstwa testów | Źródło konfiguracji | Wykonanie | Punkt rozszerzenia |
|---|---|---|---|
| Testy jednostkowe hosta/mock | `tests/CMakeLists.txt`, `tests/test_*.cpp`, główny `CMakeLists.txt` | CMake plus CTest | Dodaj zestaw Unity i zarejestruj go przez `add_hal_test(...)`, lub zadeklaruj dedykowany plik wykonywalny dla dodatkowych źródeł. |
| Implementacje sprzętowe na hoście | nagłówki i atrapy w `tests/fakes/<sdk>/`, osobny plik wykonywalny w `tests/CMakeLists.txt` | CMake plus CTest | Skompiluj prawdziwą implementację z atrapą SDK i sprawdzaj jej zachowanie przez atrapę: zarejestrowane wywołania sterownika, wstrzyknięte błędy i symulowane przerwania. |
| Testy hosta FreeRTOS POSIX | `tests/freertos_posix/`, `JH_ENABLE_FREERTOS_POSIX_TESTS` | CTest w konfiguracji dla komputera lub pełną kontrolę jakości | Dodaj target przez `add_hal_freertos_posix_test(...)`. |
| Kontrola jakości repozytorium | `runalltests.sh`, `.github/workflows/ci.yml` oraz dane narzędziowe opisane w `00_scripts.md` | `./runalltests.sh` | Rozszerz odpowiedni etap i jego ukierunkowane testy regresyjne; zapisuj generowane artefakty wyłącznie w `.build/`. |
| Projekty sprawdzające kompilację firmware | `tests/fixtures/<fixture>/.vscode/jaszczurhal.project.json` | `jh-vscode` lub właściwy skrypt produkcyjny | Rozszerz macierz targetów, płytek i wariantów w manifeście oraz test układu artefaktów. |
| Fizyczne stanowiska sprzętowe | źródło, manifest i weryfikator w `tests/hardware/<fixture>/` | Kompilacja i wgranie przez `jh-vscode` lub właściwy skrypt produkcyjny, a następnie uruchomienie weryfikatora opisanego w README stanowiska | Dodaj firmware, jawną macierz sprzętową, mechanizm sprawdzający wynik na hoście, README z procedurą i kryteriami akceptacji w obu językach oraz wiersz w tabeli poniżej. |

W razie rozbieżności między opisem a działaniem sprawdź wskazane pliki konfiguracji i programy testowe. README każdego stanowiska sprzętowego zawiera jego procedurę, połączenia i wymagania; wszystkie stanowiska wymienia sekcja [Testy na fizycznych urządzeniach](#stanowiska-sprzętowe).

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

<a id="bramki-jakości-repozytorium"></a>

## Kontrola jakości repozytorium

<a id="skrypty-szybkiego-startu"></a>

### Przygotowanie środowiska i uruchomienie kontroli

W katalogu głównym repozytorium znajdują się dwa podstawowe skrypty:

**`runmefirst.sh`** - przygotowanie środowiska i narzędzi
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
- przygotowuje katalogi kompilacji i początkową konfigurację CMake.

Uruchom go po sklonowaniu repozytorium oraz po zmianie środowiska.

Hook pre-commit normalizuje i formatuje pliki dodane do commita, a następnie
sprawdza wszystkie wersjonowane artefakty generowane. Jeśli któregoś brakuje
albo jest nieaktualny, blokuje commit i prosi o uruchomienie
`python3 scripts/sync_generated.py --write`, przejrzenie zmian oraz dodanie ich
do commita przed kolejną próbą.

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
3. Testy Clang ASan/UBSan, testy natywne pod ThreadSanitizerem i krótkie
   testy libFuzzer przez ten sam skrypt, którego używa CI
4. Bezpieczeństwo pamięci (Valgrind memcheck na wszystkich natywnych plikach wykonywalnych testów C/C++)
5. Analiza statyczna: cppcheck
6. Analiza statyczna: clang-tidy (bazy danych kompilacji hosta + STM32 poniżej
   `.build/gate/`)
7. Wykrywanie duplikatów PMD CPD w obrębie własnych implementacji C/C++ oraz
   skryptów Python
8. Kompilacje targetów (STM32G474 oraz testy startu i rdzenia Pico SDK
   RP2040/RP2350 ARM/RP2350 RISC-V, profile funkcjonalne RP, sześć
   reprezentatywnych kompilacji ELF/BIN/UF2 `01_core_runtime`/`18_freertos_suite`
   jedna czysta kompilacja
   `tests/fixtures/esp32s3_phase3` z ESP-IDF w wersji wskazanej przez
   repozytorium i zwalidowanym
   manifestem zawierającym wiele obrazów oraz `libJaszczurHAL.a` ESP32-S3
   z pełnym zestawem cech, obejmująca całą allowlistę targetu)
9. Kompilacje przykładów (macierz `gateTargets` wyprowadzona ze wspólnego mechanizmu kompilacji
   dla RP2040, STM32G474 i ESP32-S3 plus dedykowane stanowiska target/runtime)

Kończy działanie z niezerowym kodem przy pierwszym błędzie; logi rejestrują
wszelkie ostrzeżenia/błędy zarówno ze standardowego wyjścia, jak i ze
standardowego wyjścia błędów. Bramka Valgrind wybiera każdy bezpośrednio
zarejestrowany natywny plik wykonywalny testu C/C++ przez etykietę CTest
`memcheck`. `MEMCHECK_REQUIRED_TESTS` w `runalltests.sh` to krytyczny podzbiór
sprawdzany przed wykonaniem, ale nie jest pełną listą. Testy Python, CMake i
sterowane powłoką nie są uruchamiane przez memcheck. Sprawiedliwe planowanie
wątków Valgrind pozwala uruchamiać natywne testy schedulera FreeRTOS POSIX bez
ich zawieszania. Postęp CTest/Valgrind jest na bieżąco zapisywany w terminalu i w
`.build/gate/logs/jh_memcheck.log`.

Pliki wynikowe kompilacji i testów trafiają do ignorowanego przez Git katalogu `.build/`. Testy kompilatora uruchamiane przez CMake w trybie skryptowym używają `.build/tests/`; nie zapisują plików `.o` w katalogu głównym repozytorium.

Etap clang-tidy tworzy dla każdego profilu osobną bazę poleceń kompilacji, z jednym wpisem na plik źródłowy. Dzięki temu nie analizuje wielokrotnie tego samego wspólnego sterownika, nawet gdy testy API kompilują go z różnymi zestawami modułów. Zwykła kompilacja nadal obejmuje wszystkie skonfigurowane warianty.

Etap CPD korzysta z dystrybucji PMD 7.26.0 o zweryfikowanej autentyczności, zarządzanej w `third_party/pmd`. Skanuje pliki implementacji C/C++ oraz skrypty Pythona w `scripts/`; pomija nagłówki, kod generowany i kod zewnętrzny. Kontrola kończy się niepowodzeniem po wykryciu choć jednej grupy duplikatów obejmującej co najmniej 100 tokenów C/C++ w kodzie produkcyjnym, testach lub przykładach albo co najmniej 50 tokenów Pythona. Nie ma listy wyjątków ani zaakceptowanych wcześniej duplikatów. Raport podaje łączny zakres powielonych tokenów oraz wyniki osobno dla mocka, RP2040, STM32G474, kodu wspólnego, pozostałego kodu przenośnego i skryptów Pythona. Raporty XML i uporządkowane listy plików trafiają do `.build/gate/cpd/`. Wynik CPD `PASS` oznacza brak grup duplikatów przy tych progach.

Uruchamiaj pełną kontrolę przed commitem i wysłaniem zmian do repozytorium. Ten sam zestaw kontroli służy do weryfikacji w CI/CD.

<a id="natywna-bramka-ci-dla-windows"></a>

### Kontrola CI w natywnym środowisku Windows

`.github/workflows/ci.yml` uruchamia dwie natywne bramki `windows-2025`, oprócz
kompletnej bramki jakości dla Linuksa:

- `windows-tooling` przygotowuje uwierzytelnione zarządzane środowisko,
  powtarza `runmefirst.ps1 -VerifyOnly`, uruchamia współdzielone testy
  runtime/platform/bootstrap i generatora, weryfikuje wybór źródeł zależności
  CMake dla FreeRTOS na RP i STM32, wykonuje czysty produkcyjna kompilacja
  ESP32-S3/ESP-IDF i publikuje artefakty zawierające wiele obrazów, a następnie kompiluje
  i uruchamia przenośne testy hosta z MSVC `/W4 /permissive- /WX`;
- `Windows firmware (<target>)` buduje wygenerowany projekt użytkownika ze
  ścieżki zawierającej spacje przez Ninja dla `rp2040`, `rp2350-arm`,
  `rp2350-riscv` i `stm32g474`, sprawdza artefakty targetu oraz bazę danych
  kompilacji ze ścieżkami dostosowanymi do Windows, a następnie publikuje
  reprezentatywne artefakty kompilacji.

Inwentarz CTest dla Windows utrzymuje adapter BSD POSIX, integrację
Bash/POSIX BearSSL oraz runtime FreeRTOS GCC/POSIX jako
widoczne, wyłączone testy. Ich aktywne pokrycie, wraz z Valgrind, cppcheck,
clang-tidy i PMD CPD, pozostaje w bramce dla Linuksa. Fiesta, DoomConsole i
Ford DPF Tracker mają osobne natywne procesy CI firmware dla Windows. Każdy z
nich uruchamia właściwe dla danego projektu testy integracyjne, niezależnie od
projektu testowego generowanego przez JaszczurHAL.

<a id="stanowiska-sprzętowe"></a>

## Testy na fizycznych urządzeniach

Testy na urządzeniach korzystają z tych samych narzędzi VS Code co aplikacje użytkownika. Ich pliki wynikowe trafiają do `.build/hardware/`.

Domyślna konfiguracja `runalltests.sh`, CTest i CI nie odczytuje ani nie
kompiluje tego firmware'u. Polecenia z tej sekcji uruchamiaj jawnie, gdy masz
dostęp do wymaganych urządzeń. Dodatkowe testy hostowe plików związanych z tymi
testami można zarejestrować przez `-DJH_ENABLE_HARDWARE_FIXTURE_CHECKS=ON`.

| Stanowisko | Pokrycie |
|---|---|
| [`tests/hardware/bluetooth_stage1`](../../../tests/hardware/bluetooth_stage1/README.pl.md) | Wewnętrzny test kontrolera CYW43/BTstack opracowany przed publicznym API: rozgłaszanie, statyczny GATT oraz wariant odniesienia `wifi-only` na Pico W i STM32G474/PIM730. |
| [`tests/hardware/bluetooth_gamepad`](../../../tests/hardware/bluetooth_gamepad/README.pl.md) | Zanonimizowany deskryptor i raporty 8BitDo Zero 2 Android D-input oraz prywatna sonda parsera gamepada Classic HID Host dla Pico 2 W. |
| [`tests/hardware/bluetooth_classic_hid_device`](../../../tests/hardware/bluetooth_classic_hid_device/README.pl.md) | Prywatna mysz Classic HID na Pico W używana do sprawdzenia publicznego ogólnego HID Host na drugim radiu Pico. |
| [`tests/hardware/bluetooth_classic_hci_trace`](../../../tests/hardware/bluetooth_classic_hci_trace/README.pl.md) | Prywatny, chroniący dane surowy ślad inquiry HCI oraz diagnostyka transportu i zegara CYW43 dla Pico W i Pico 2 W. |
| [`tests/hardware/bluetooth_observer`](../../../tests/hardware/bluetooth_observer/README.pl.md) | Publiczne pasywne skanowanie Observer, ograniczona kolejka raportów oraz parsowanie BLE Teltonika/iBeacon/Eddystone na Pico W, Pico 2 W i STM32G474/PIM730. |
| [`tests/hardware/bluetooth_stream`](../../../tests/hardware/bluetooth_stream/README.pl.md) | Publiczny cykl życia BLE i uwierzytelniona bramka Stream w różnych krotkach target/board/runtime, w tym ponowne łączenie, watchdog, ciągły ruch, nasycenie i negatywne przypadki bezpieczeństwa. |
| [`tests/hardware/rp_usb_cdc_echo`](../../../tests/hardware/rp_usb_cdc_echo/README.pl.md) | Natywne zgłaszanie interfejsu CDC TinyUSB, mechanizm ograniczania nadawcy (`backpressure`), ponowne łączenie i przepustowość |
| [`tests/hardware/rp_usb_multicore`](../../../tests/hardware/rp_usb_multicore/README.pl.md) | Równoległe zadania generujące dane CDC na obu rdzeniach RP, integralność i kompletność danych oraz przypisanie rekordów do właściwego producenta w trybach bare metal i FreeRTOS |
| [`tests/hardware/rp_freertos_smp`](../../../tests/hardware/rp_freertos_smp/README.pl.md) | Scheduler, oba rdzenie, muteks/opóźnienie, sterta i USB pod FreeRTOS SMP |
| [`tests/hardware/rp_flash_transaction`](../../../tests/hardware/rp_flash_transaction/README.pl.md) | Sekwencjonowanie koordynatora flash, ścieżki odrzucenia, kasowanie/programowanie i odzyskiwanie |
| [`tests/hardware/rp_kv_power_loss`](../../../tests/hardware/rp_kv_power_loss/README.pl.md) | Odzyskiwanie dwóch banków KV po przerwaniu po kasowaniu, zapisie treści, weryfikacji i publikacji |
| [`tests/hardware/rp_storage`](../../../tests/hardware/rp_storage/README.pl.md) | Trwały zapis w EEPROM, formatowanie i ponowne montowanie LittleFS oraz montowanie po resecie |
| [`tests/hardware/rp_sdlogger`](../../../tests/hardware/rp_sdlogger/README.pl.md) | Fizyczne montowanie karty SD przez SPI, deterministyczne dopisywanie, opróżnianie bufora i zamykanie pliku, reset, ponowne montowanie, zawartość pliku i trwałość licznika logów w EEPROM |
| [`tests/hardware/rp_ota`](../../../tests/hardware/rp_ota/README.pl.md) | Odkrywanie, uwierzytelnianie, transfer, próba/potwierdzenie, wycofanie (rollback), współpraca trwałego storage i odzyskiwanie USB/sieci |
| [`tests/hardware/lora_sx1262`](../../../tests/hardware/lora_sx1262/README.pl.md) | Inicjalizacja dwóch urządzeń SX1262, dwukierunkowe surowe pakiety, cykl życia niezawodnego łącza oraz fragmentowane transakcje żądanie/odpowiedź routera poleceń na parach zintegrowanych LF lub zewnętrznych HF |
| [`tests/hardware/esp32s3_phase1`](../../../tests/hardware/esp32s3_phase1/README.pl.md) | Tożsamość targetu/płytki ESP32-S3 Fazy 1, generowana sygnatura linkowania, model układu/liczba rdzeni, fizyczny flash, zainicjalizowany Quad PSRAM oraz powtarzane bicie serca `app_task0()` FreeRTOS nad natywnym USB Serial/JTAG. |
| [`tests/hardware/esp32s3_phase2`](../../../tests/hardware/esp32s3_phase2/README.pl.md) | Sonda runtime Fazy 2 ESP32-S3 dla obu zadań aplikacji, system/sync, GPIO/IRQ, ADC, USB Serial/JTAG TX/RX, sprzętowy UART, skanowanie mastera I2C, ścieżka transferu mastera SPI, callbacki timera z dedykowanej puli oraz włączona konfiguracja stack-guard FreeRTOS. |

README każdego stanowiska opisuje połączenia, polecenia i kryteria zaliczenia. Trzy ostatnie sprawdzenia poniżej korzystają z przykładów zamiast osobnego stanowiska. Udana kompilacja potwierdza zbudowanie firmware, nie jego poprawne działanie na urządzeniu. Akceptacja sprzętowa wymaga wykonania procedury, wyniku programu weryfikującego na komputerze lub wskazanej kontroli wzrokowej oraz spełnienia opisanych kryteriów PASS.

### Sprzętowy test managera Bluetooth Classic

Publiczny wariant `classic-scan` przykładu 29 jest ogólnym testem sprzętowym
Classic. Używa wyłącznie `HAL_ENABLE_BLUETOOTH_CLASSIC`, nadaje wykrytym
urządzeniom ulotne indeksy i celowo nie umieszcza w logu adresów Bluetooth ani
materiału link key. Zbuduj i wgraj obraz, a następnie otwórz konsolę szeregową:

```sh
vscode/entry/jh-vscode build \
  --project examples/29_bluetooth_gamepad \
  --target rp2350-arm --board pico2w --variant classic-scan
vscode/entry/jh-vscode upload \
  --project examples/29_bluetooth_gamepad \
  --target rp2350-arm --board pico2w --variant classic-scan \
  --port /dev/ttyACM0
```

Po początkowym inquiry i wykonywanych kolejno zapytaniach SDP użyj `PAIR n`,
zatwierdź zgłoszone żądanie przez `AUTHORIZE`, opublikuj zweryfikowanego peera
przez `SAVE n` i sprawdź `INFO`. `FORGET n` musi zmniejszyć liczbę peerów
do zera. `SCAN`, a następnie `STOP`, sprawdza jawne przerwanie inquiry.
Konsola korzysta wyłącznie z RAM-u; trwałość po restarcie należy do osobnej
bramki bondingu.

Sprawdzenie nie obejmuje profilu transmisji audio.

<a id="bramka-współistnienia-ble-i-gamepada-classic"></a>

### Test współdziałania BLE i gamepada Classic

Publiczny wariant `ble` przykładu 29 uruchamia pasywnego Observera BLE obok
profilu Classic HID/gamepad na wspólnym hoście CYW43:

```sh
vscode/entry/jh-vscode build \
  --project examples/29_bluetooth_gamepad \
  --target rp2350-arm --board pico2w --variant ble
vscode/entry/jh-vscode upload \
  --project examples/29_bluetooth_gamepad \
  --target rp2350-arm --board pico2w --variant ble \
  --port /dev/ttyACM0
```

Wykonaj parowanie zgodnie z
[README przykładu](../../../examples/29_bluetooth_gamepad/README.pl.md).
Warunkiem zaliczenia są poprawne dane z gamepada podczas ciągłego odbioru
raportów BLE, prawidłowe wykonanie `BLE_STOP`/`BLE_START` oraz rozłączenie i
ponowne połączenie HID bez zatrzymywania BLE. `INFO` musi zachować obu
użytkowników runtime radia i nie może zgłaszać błędów transportu HCI, alokacji
ze stałych pul ani kolejek.

<a id="bramka-sprzętowa-a2dp-sink-i-avrcp-target"></a>

### Test sprzętowy A2DP Sink i AVRCP Target

Przykład 30 służy do sprzętowego sprawdzania A2DP/AVRCP na Pico W i Pico 2 W:

```sh
vscode/entry/jh-vscode build \
  --project examples/30_bluetooth_speaker \
  --target rp2040 --board picow --variant avrcp
vscode/entry/jh-vscode upload \
  --project examples/30_bluetooth_speaker \
  --target rp2040 --board picow --variant avrcp \
  --port /dev/ttyACM0
vscode/entry/jh-vscode build \
  --project examples/30_bluetooth_speaker \
  --target rp2350-arm --board pico2w --variant avrcp
vscode/entry/jh-vscode upload \
  --project examples/30_bluetooth_speaker \
  --target rp2350-arm --board pico2w --variant avrcp \
  --port /dev/ttyACM0
```

Wykonaj okablowanie, parowanie i komendy szeregowe opisane w
[README przykładu](../../../examples/30_bluetooth_speaker/README.pl.md).
Warunkiem zaliczenia są czysty dźwięk SBC, pause/resume/stop, bezwzględna
regulacja głośności AVRCP, reconnect z bondem po restarcie, osobna próba
reconnectu po resecie watchdogiem oraz brak błędów kolejek, pul, DMA i czasu
obsługi. Fizyczny tor wyjściowy wymaga osobnej walidacji w produkcie. Wariant
`ble-a2dp` należy do bramki kompilacji; aktywny dźwięk+BLE nie jest jeszcze
wymaganiem sprzętowym.

<a id="projekty-do-sprawdzania-buildu-i-linkowania-firmwareu"></a>

## Projekty sprawdzające kompilację i linkowanie firmware

<a id="projekt-sprawdzający-build-i-linkowanie-esp32-s3"></a>

### Kompilacja i linkowanie projektu ESP32-S3

| Projekt testowy | Zakres |
|---|---|
| `tests/fixtures/esp32s3_phase3` | Projekt ESP-IDF przeznaczony wyłącznie do sprawdzania kompilacji, wybierający każdy backend ESP32-S3 dostarczony w fazie 3. Sprawdza dobór funkcji, źródeł i zależności, kompilację, linkowanie, generowanie partycji `two-ota-large` oraz publikowanie artefaktów. |

Ten projekt jest kompilowany przez CI i lokalny etap kontroli jakości nr 8. Udana kompilacja nie potwierdza działania WiFi, gniazd sieciowych, TLS, usług, OTA ani WireGuard podczas pracy urządzenia; nie zastępuje też testów nowych peryferiów fazy 2. Potrzebne są osobne testy sprzętowe, sprawdzenie inicjalizacji, pracy i zamykania modułów oraz testy odrzucania nieprawidłowych i nieuprawnionych operacji.

---

<a id="architektura-testów-hosta"></a>

## Jak zbudowane są testy na komputerze

<a id="jak-to-działa"></a>

### Biblioteka testowa i zależności

Konfiguracja CMake w katalogu głównym projektu kompiluje bibliotekę statyczną
`hal_mock` z:

- wszystkich zaślepek `src/hal/impl/.mock/*.cpp`,
- niezależnych od backendu źródeł HAL w `UTIL_SOURCES` (patrz
  `CMakeLists.txt`), w tym pozostałych współdzielonych adapterów statusu
  MQTT/WireGuard w `hal_network_status.cpp`, fasad HAL, warstw
  kompatybilności, przenośnych sterowników urządzeń i dołączonych bibliotek,
- `src/utils/unity.c` (warstwa integracyjna Unity).

Dokładną listę zawiera zbiór `UTIL_SOURCES` w `CMakeLists.txt`; jest on
miarodajnym źródłem danych.

Każdy program testowy w `tests/` jest linkowany wyłącznie z `hal_mock`, bez
nagłówków Pico SDK i bez dostępu do sprzętu.

Testy korzystają z Unity 2.5.4 w `third_party/Unity/src`. Wersjonowane pliki integracji z JaszczurHAL to:

- `src/utils/unity.c`
- `src/utils/unity.h`
- `src/utils/unity_internals.h`
- `src/utils/unity_config.h`

Konfiguracja CMake dla hosta kompiluje warstwę `src/utils/unity.c` jako część
`hal_mock` oraz włącza `HAL_ENABLE_UNITY` i `UNITY_INCLUDE_CONFIG_H`. Źródła testów
dołączają `"utils/unity.h"` i używają lokalnego dla repozytorium
`unity_config.h`. Uruchom `scripts/ensure_unity.sh` lub główny skrypt
aktualizujący komponenty, aby odtworzyć wersję źródeł wskazaną przez
repozytorium. Poza kompilacją
testowym Unity pozostaje nieaktywne, chyba że jawnie włączono
`HAL_ENABLE_UNITY`.

Tematyczne moduły narzędziowe i ich adaptery zgodnościowe sprawdza
`test_tools` z użyciem mocków HAL.
Plik `multicoreWatchdog.cpp` sprawdza `test_multicoreWatchdog` z użyciem
lokalnej zaślepki zamknięcia loggera i mocków HAL.

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

Ten zapis oczekuje pliku `tests/test_my_module.cpp` i tworzy program testowy linkowany z `hal_mock`.

Test wymagający dodatkowych plików implementacji zarejestruj jako osobny cel CMake:

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

<a id="przewodnik-po-zestawach-testów"></a>

### Lista zestawów testów

Pełny rejestr testów znajduje się w `tests/CMakeLists.txt`. Aby wyświetlić testy dostępne w bieżącej kopii repozytorium, uruchom:

```bash
ctest --test-dir .build/host -N
```

Zestawy testów hosta znajdują się w `tests/test_<nazwa>.c` lub `.cpp`, kontrole skryptów i repozytorium w `tests/test_<nazwa>.py`, a kontrole CMake w `tests/test_<nazwa>.cmake`; nazwa wskazuje, co sprawdzają.

### Dodawanie nowego zestawu testów

1. Utwórz `tests/test_<name>.cpp` z `#include "utils/unity.h"`, wywołaniami
   Unity `setUp`, `tearDown`, `UNITY_BEGIN`, `RUN_TEST` i `UNITY_END`.
2. Dodaj `add_hal_test(test_<name>)` do `tests/CMakeLists.txt`.
    Dla zestawów kompilujących dodatkowe źródła (na przykład `test_tools` i
    `test_multicoreWatchdog`), utwórz dedykowany wpis `add_executable(...)`.
3. Przekompiluj:
   `cmake --build .build/host && ctest --test-dir .build/host`.

<a id="sterowanie-czasem-w-mocku"></a>

### Sterowanie czasem w testach z użyciem mocka

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

*Dalej: [Bezpieczeństwo wielordzeniowe, sterowniki, przewodnik migracji](04_multicore_drivers_migration.md)*
