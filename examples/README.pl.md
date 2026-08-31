# Przykłady JaszczurHAL

Drzewo `examples/` zawiera skonsolidowane projekty firmware obsługiwane przez
dispatcher. Każdy projekt ma własny wygenerowany
`.vscode/jaszczurhal.project.json`; otwarcie jego katalogu w VS Code udostępnia
taski Build, Upload, Serial Monitor, Clean, Config Dump, OTA i wyboru boardu tak
samo jak w samodzielnym projekcie firmware.

Rejestr w `scripts/examples_dispatcher.py` jest źródłem prawdy dla zakresu
projektów, wspieranych targetów, targetów domyślnego gate, profili boardów,
wariantów, źródeł i definicji funkcji. Wygenerowane manifesty są używane przez
`vscode/entry/jh-vscode` i `cmake/jh_firmware_project`.

## Polityka macierzy i gate

Konfiguracja to jeden projekt bazowy lub jego wariant zbudowany dla jednego
targetu. Rejestr dispatchera wyprowadza pełną wspieraną macierz oraz macierz
domyślnego gate. Sprawdzaj bieżący wynik zamiast utrzymywać drugi licznik:

```bash
scripts/examples_dispatcher.py list
```

Gate 6 buduje też reprezentatywny firmware core runtime i FreeRTOS przez
bezpośrednią ścieżkę native dla każdego toolchaina i architektury RP.

Generowane manifesty rozróżniają dwie listy targetów:

- `example.targets` zawiera wszystkie targety wspierające projekt bazowy;
- `example.gateTargets` jest sprawdzonym podzbiorem wybieranym przez domyślny
  gate przykładów. Jeżeli wpis rejestru tego nie nadpisuje, generator wybiera
  wspierane targety `rp2040` i `stm32g474`.

Warianty mają własne `targets` i `gateTargets`. Target nieobecny w `targets` nie
jest wspierany. Target obecny w `targets`, lecz nie w `gateTargets`, pozostaje
dostępny w pełnej macierzy bez rozszerzania domyślnego gate.

`scripts/examples_dispatcher.py build` bez `--gate` buduje wszystkie wspierane
konfiguracje bazowe i wariantowe dla żądanego targetu. `--gate` ogranicza
wykonanie do konfiguracji, których `gateTargets` zawiera ten target:

```bash
# Pełna macierz jednego targetu.
scripts/examples_dispatcher.py build --target rp2350-arm --jobs "$(nproc)"

# Domyślny gate przykładów dla każdego wspieranego targetu.
scripts/examples_dispatcher.py build \
  --target rp2040 --gate --jobs "$(nproc)"
scripts/examples_dispatcher.py build \
  --target rp2350-arm --gate --jobs "$(nproc)"
scripts/examples_dispatcher.py build \
  --target stm32g474 --gate --jobs "$(nproc)"
```

## Katalog projektów

Rejestr opisuje tylko bieżące projekty. Projekty skonsolidowane utrzymują
powiązane zachowania w jednym obrazie firmware lub małym zbiorze jawnych
wariantów, zamiast budować cały HAL dla każdej pojedynczej demonstracji.

Skróty targetów w tabeli: `R0` = `rp2040`, `RA` = `rp2350-arm`, `RV` =
`rp2350-riscv`, `S` = `stm32g474`.

| Projekt | Przeznaczenie | Wspierane targety | `gateTargets` | Warianty |
|---|---|---|---|---|
| `01_core_runtime` | Miganie LED-em, raport debug/architektury, tabela soft timerów, regulator PID, zarządzany timer | R0, RA, RV, S | R0, S | - |
| `02_crypto` | Prymitywy hash, uwierzytelniania, szyfrowania i Base64 | R0, RA, RV, S | R0, S | - |
| `03_modem_A7670E` | Lifecycle modemu SIMCom A7670/A7672 i usługi AT | R0, RA, RV | R0 | - |
| `04_sensor_hub` | Czujniki temperatury i wilgotności DS18B20, BH1750 i DHT | R0, RA, RV, S | R0, S | - |
| `05_serial_gps` | UART, analiza/transport GPS i loopback software serial | R0, RA, RV, S | R0, S | `swserial` na R0, RA, RV; gate na R0 |
| `06_thermocouple` | Fasada termopary i wspierane backendy | R0, RA, RV, S | R0, S | - |
| `07_display_media` | Grafika ILI9341, kodeki PNG/JPEG, konwersja Base64 i rendering RGB565 | R0, RA, RV, S | R0, S | - |
| `08_mqtt` | MQTT przez wybrany backend sieciowy CYW43 | R0, RA, S | R0, S | - |
| `09_wireguard` | Konfiguracja tunelu WireGuard przez wybrany backend sieciowy | R0, RA, S | R0, S | - |
| `10_storage` | Magazyn KV, LittleFS, logowanie SD/FatFs i trwałe liczniki | R0, RA, RV, S | R0, S | - |
| `11_i2c_slave` | Mapa rejestrów I2C slave | R0, RA, RV, S | R0, S | - |
| `12_i2c_scan` | Ograniczone skanowanie magistrali I2C | R0, RA, RV, S | R0, S | - |
| `13_adc` | Próbowanie wewnętrznego ADC i konwersja zewnętrznego ADS1115 | R0, RA, RV, S | R0, S | - |
| `14_can_mcp2515` | Backend klasycznego CAN MCP2515 | R0, RA, RV, S | R0, S | - |
| `15_display_oled_lcd` | OLED SSD1306 i znakowy LCD HD44780 | R0, RA, RV, S | R0, S | - |
| `16_rtc_backends` | Fasada RTC, native wybudzanie względne, przenośne przejścia low-power i zegar DS3231/ILI9341 z podtrzymaniem | R0, RA, RV, S | R0, S | ręczny `display-clock` na S |
| `17_audio_output` | Regulacja głośności PGA2311 i wyjście audio DMA/PWM | R0, RA, RV, S | R0, S | - |
| `18_freertos_suite` | Taski i affinity FreeRTOS, WiFi, cJSON, sockety BSD, klient/serwer HTTP/HTTPS, pliki, WebSocket, konsola sieciowa, polecenia i powiadomienia Telegram | R0, RA, RV, S | R0, S | `network` na R0, RA, S; gate na R0, S |
| `19_touch` | Kontrolery dotyku TSC2007 i STMPE610 | R0, RA, RV, S | R0, S | - |
| `20_irsmall_decoder` | Dekodowanie protokołu IRsmall | R0, RA, RV, S | R0, S | - |
| `21_stm32g474_fdcan_native` | Native FDCAN STM32G474 | S | S | - |
| `22_rfid_nfc` | Czytniki MFRC522 RFID i PN532 NFC/RFID | R0, RA, RV, S | R0, S | - |
| `23_io_pmic` | LED RGB, proste ekspandery I/O/DAC i PMIC ADP5360 | R0, RA, RV, S | R0, S | - |
| `24_epd_display` | Fasada wyświetlacza e-paper i ścieżka odświeżania | R0, RA, RV, S | R0, S | - |
| `25_ota` | Wykrywanie, uwierzytelnione przygotowanie OTA, potwierdzenie próbne, rollback i odzyskiwanie BOOTSEL | R0, RA | R0 | - |
| `26_ble_stream` | Lifecycle BLE Peripheral, uwierzytelniony JH BLE Stream v1 i adapter routera poleceń | R0, RA, S | R0, RA, S | `commands` i `commands-freertos` na R0, RA, S; gate na R0 |
| `27_lora_point_to_point` | Surowy ping/pong SX1262 oraz pofragmentowane żądanie/odpowiedź routera przez `hal_lora_link` | R0, S | R0, S | `probe`, `responder`, `link` i `link-responder` na R0, S; ręczne warianty sprzętowe `sf7` i `responder-sf7` |
| `28_serial_commands` | Dispatch ramkowanej Serial Session przez niezależny router poleceń | R0, RA, RV, S | R0, S | - |
| `29_bluetooth_gamepad` | Parowanie Bluetooth Classic HID, reconnect i znormalizowane snapshoty gamepada | R0, RA, S | R0 | `ble` na R0, RA, S; gate na R0 |

Buildy sieciowe rodziny RP używają `picow` dla RP2040 i `pico2w` dla RP2350
ARM. Konfiguracje RP2350 RISC-V wymagające CYW43 nie są wspierane. Projekty
sieciowe i Bluetooth STM32G474 wybierają profil NUCLEO-G474RE z zewnętrznym
PIM730/RM2.

Projekt LoRa domyślnie wybiera stałe fixture `pico-core1262-hf` i
`nucleo-g474re-core1262-hf`. Dla zintegrowanego boardu Waveshare LF jawnie użyj
`rp2040-lora-lf`; urządzenia LF i HF pracują w innych pasmach i należą do
oddzielnych fizycznych par radiowych. Wariant `probe` bez nadawania sprawdza
capabilities, kalibrację, bieżące RSSI i CAD. Warianty bazowy i `responder`
używają SF9/10 dBm, a `sf7` i `responder-sf7` tworzą deterministyczną parę
sprzętową SF7/6 dBm. Warianty `link` i `link-responder` wymieniają skorelowane,
binarne polecenie `echo` o rozmiarze 500 bajtów oraz odpowiedź przez wspólny
router. Oba kierunki sprawdzają adresowanie, identyfikatory żądań, składanie
trzech fragmentów, tłumienie duplikatów i retransmisję. Trasa handlera dopuszcza
też zaimplementowane źródło `BLE_STREAM` bez dodawania transportu BLE do tego
przykładu. SX1261, SX1276 i SX1278 pozostają eksperymentalnymi integracjami
wyłącznie programowymi i nie dodają profili boardów ani deklaracji fizycznego
wsparcia przez ten fixture.

## Wspierane targety builda

| Target | Domyślny board | Toolchain | Artefakty firmware |
|---|---|---|---|
| `rp2040` | `pico` | oficjalny Pico SDK + GNU Arm | ELF, BIN, HEX, UF2, MAP |
| `rp2350-arm` | `pico2` | oficjalny Pico SDK + GNU Arm | ELF, BIN, HEX, UF2, MAP |
| `rp2350-riscv` | `pico2` | oficjalny Pico SDK + przypięty toolchain Hazard3 | ELF, BIN, HEX, UF2, MAP |
| `stm32g474` | `nucleo-g474re` | GNU Arm | ELF, BIN, HEX, MAP |

ESP32-S3 używa obecnie dedykowanych projektów ESP-IDF zamiast tego natywnego
dispatchera przykładów CMake. `tests/fixtures/esp32s3_phase3` buduje i linkuje
pełny graf backendów Phase 2/3, a `tests/hardware/esp32s3_phase1` i
`tests/hardware/esp32s3_phase2` zachowują dostępne raporty sprzętowe. Dodanie
przykładów ESP32-S3 obsługiwanych przez dispatcher wymaga również trybu builda
ESP-IDF i walidacji boardu oraz zasobów dla każdego przykładu.

## Wymagania

- CMake 3.20 lub nowszy dla buildów firmware obsługiwanych przez dispatcher;
- Python 3;
- `arm-none-eabi-gcc` dla RP2040, RP2350 ARM i STM32G474;
- zarządzane komponenty Pico SDK, picotool, FreeRTOS, lwIP, BearSSL, LittleFS i
  RISC-V przygotowane przez `./runmefirst.sh` lub
  `./third_party/update_components.sh`.

## Dodawanie przykładu

Przed utworzeniem kolejnego katalogu sprawdź, czy nowe zachowanie może rozszerzyć
istniejący projekt lub wariant. Konsolidacja jest domyślna: utrzymuje powiązane
ścieżki runtime razem i zapobiega wielokrotnemu budowaniu całego HAL w małych
projektach firmware na każdym targecie.

Osobny projekt jest właściwy, gdy target, toolchain, runtime, profil boardu,
wzajemnie wykluczające się zasoby lub wymagania sprzętowe uniemożliwiają
użyteczny wspólny obraz. Opisz to ograniczenie tutaj i zadeklaruj dokładne
`targets` oraz `gateTargets` w `config/tooling/examples.json`. Wariant stosuj
tylko wtedy, gdy zachowania nie można wybrać w runtime. Każda zmiana wymaga
sprawdzenia liczby konfiguracji pełnej macierzy i domyślnego gate.

## Polecenia builda

Build pełnej wspieranej macierzy dla wybranego targetu:

```bash
scripts/examples_dispatcher.py build --target rp2040 --jobs "$(nproc)"
scripts/examples_dispatcher.py build --target rp2350-arm --jobs "$(nproc)"
scripts/examples_dispatcher.py build --target rp2350-riscv --jobs "$(nproc)"
scripts/examples_dispatcher.py build --target stm32g474 --jobs "$(nproc)"
```

Build jednego projektu przez CLI używane również przez VS Code:

```bash
vscode/entry/jh-vscode build \
  --project examples/01_core_runtime --target rp2040 --board pico
```

Ograniczenie wykonania rejestru do jednego lub kilku projektów:

```bash
scripts/examples_dispatcher.py build \
  --target rp2040 \
  --example 01_core_runtime --example 10_storage
```

Wyświetlenie wygenerowanej macierzy lub odświeżenie wszystkich śledzonych
artefaktów po zmianie rejestru:

```bash
scripts/examples_dispatcher.py list
python3 scripts/sync_generated.py --write
```

Końcowe artefakty należące do repozytorium pozostają w
`.build/examples/<example>/`. Drzewa CMake targetów używają
`.build/examples/<example>/cmake/<target>/<board>/`.

`examples/CMakeLists.txt` jest cienkim wejściem do tego samego dispatchera:

```bash
cmake -S examples -B .build/examples-cmake/rp2040 \
  -DJH_EXAMPLE_TARGET=rp2040
cmake --build .build/examples-cmake/rp2040
```

## Struktura aplikacji

```text
NN_example_name/
  app.c lub app.cpp
  hal_project_config.h
  .vscode/
    jaszczurhal.project.json
    tasks.json
    settings.json
```

Aplikacje udostępniają:

```c
void app_start(void);
void app_task0(void);
void app_task1(void); /* opcjonalnie z HAL_ENABLE_APP_TASK1 */
```

Wybrany runtime dostarcza `main()`. Bare-metal RP uruchamia `app_task0()` na
core 0 i uruchamia core 1 dla `app_task1()` tylko na żądanie. RP FreeRTOS tworzy
taski aplikacji z odpowiadającym affinity. STM32G474 wykonuje obie funkcje
kooperacyjnie w trybie bare-metal albo jako osobne taski FreeRTOS. Aplikacje demo
hosta używają pętli kooperacyjnej.

Minimalna aplikacja:

```c
#include <hal/core/hal_app.h>
#include <hal/system/hal_board.h>
#include <hal/gpio/hal_gpio.h>
#include <hal/system/hal_system.h>

void app_start(void) {
  hal_gpio_set_mode(HAL_LED_BUILTIN, HAL_GPIO_OUTPUT);
}

void app_task0(void) {
  hal_gpio_write(HAL_LED_BUILTIN, true);
  hal_delay_ms(500u);
  hal_gpio_write(HAL_LED_BUILTIN, false);
  hal_delay_ms(500u);
}
```

Wybór funkcji należy do `hal_project_config.h`:

```c
#pragma once

#define HAL_ENABLE_I2C
#define HAL_ENABLE_BH1750
```

Używaj samych nazw funkcji lub jawnej wartości `1`. Wspierany tooling odrzuca
`HAL_ENABLE_*=0`; aby wyłączyć funkcję, pomiń makro. Nagłówek projektu powinien
zawierać wyłącznie makra, ponieważ jest ładowany przed normalizacją targetu i
boardu. Definicje funkcji muszą być bezwarunkowe; wspierana jest tylko osłona
`#ifndef` tego samego symbolu, ponieważ wczesny kolektor wyboru źródeł odczytuje
plik tekstowo.

Przepływ builda ustawia `HAL_PROVIDE_APP_ENTRY`. Fakty o pinach właściwe dla
boardu pochodzą z wybranego, wygenerowanego profilu. Połączenia należące do
aplikacji można nadal opisać jawnym deskryptorem sprzętu, gdy żaden stały profil
złożony nie pasuje.

## VS Code

Etykiety generowanych tasków i skróty klawiszowe opisuje
[JaszczurHAL VS Code Entry](../vscode/README.pl.md). Konfigurację projektu,
rozwiązywanie targetu/boardu, wykrywanie źródeł i ścieżki artefaktów opisuje
[Workflow projektu firmware](../doc/pl/FwProjectWorkflow.md).
