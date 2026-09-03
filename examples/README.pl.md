# Przykłady JaszczurHAL

Drzewo `examples/` zawiera zestaw prostych projektów, demonstrujące w sposób
podstawowy możliwości biblioteki JaszczurHAL. Każdy projekt ma własny wygenerowany
`.vscode/jaszczurhal.project.json`; otwarcie jego katalogu w VS Code udostępnia
zadania `Build`, `Upload`, `Serial Monitor`, `Clean`, `Config Dump`, `OTA` i
wyboru płytki, tak samo jak w samodzielnym projekcie firmware.

Rejestr w `scripts/examples_dispatcher.py` jest nadrzędnym opisem projektów,
obsługiwanych targetów, targetów objętych domyślną bramką, profili płytek,
wariantów, źródeł i definicji funkcji. Wygenerowane manifesty wykorzystują
`vscode/entry/jh-vscode` oraz `cmake/jh_firmware_project`.

## Katalog projektów

Skróty targetów w tabeli: `R0` = `rp2040`, `RA` = `rp2350-arm`, `RV` =
`rp2350-riscv`, `S` = `stm32g474`.

| Projekt | Przeznaczenie | Obsługiwane targety | `gateTargets` | Warianty |
|---|---|---|---|---|
| `01_core_runtime` | Miganie LED-em, raport diagnostyczny i informacje o architekturze, tabela timerów programowych, regulator PID, zarządzany timer | R0, RA, RV, S | R0, S | - |
| `02_crypto` | Funkcje skrótów kryptograficznych, uwierzytelniania, szyfrowania i Base64 | R0, RA, RV, S | R0, S | - |
| `03_modem_A7670E` | Cykl działania modemu SIMCom A7670/A7672 i usługi AT | R0, RA, RV | R0 | - |
| `04_sensor_hub` | Czujniki temperatury i wilgotności DS18B20/DHT oraz natężenia oświetlenia BH1750 | R0, RA, RV, S | R0, S | - |
| `05_serial_gps` | UART, analiza i przesyłanie danych GPS oraz pętla zwrotna programowego portu szeregowego | R0, RA, RV, S | R0, S | `swserial` na R0, RA, RV; bramka na R0 |
| `06_thermocouple` | Fasada termopary i obsługiwane backendy | R0, RA, RV, S | R0, S | - |
| `07_display_media` | Grafika ILI9341, kodeki PNG/JPEG, konwersja Base64 i renderowanie RGB565 | R0, RA, RV, S | R0, S | - |
| `08_mqtt` | MQTT przez wybrany backend sieciowy CYW43 | R0, RA, S | R0, S | - |
| `09_wireguard` | Konfiguracja tunelu WireGuard przez wybrany backend sieciowy | R0, RA, S | R0, S | - |
| `10_storage` | Magazyn KV, LittleFS, logowanie SD/FatFs i trwałe liczniki | R0, RA, RV, S | R0, S | - |
| `11_i2c_slave` | Mapa rejestrów I2C slave | R0, RA, RV, S | R0, S | - |
| `12_i2c_scan` | Ograniczone skanowanie magistrali I2C | R0, RA, RV, S | R0, S | - |
| `13_adc` | Próbkowanie za pomocą wewnętrznego ADC i pomiary zewnętrznym przetwornikiem ADS1115 | R0, RA, RV, S | R0, S | - |
| `14_can_mcp2515` | Backend klasycznego CAN MCP2515 | R0, RA, RV, S | R0, S | - |
| `15_display_oled_lcd` | OLED SSD1306 i znakowy LCD HD44780 | R0, RA, RV, S | R0, S | - |
| `16_rtc_backends` | Fasada RTC, natywne wybudzanie po zadanym czasie, przenośne tryby niskiego poboru mocy i zegar DS3231/ILI9341 z podtrzymaniem | R0, RA, RV, S | R0, S | ręczny `display-clock` na S |
| `17_audio_output` | Regulacja głośności PGA2311 i wyjście audio DMA/PWM | R0, RA, RV, S | R0, S | - |
| `18_freertos_suite` | Zadania FreeRTOS i ich przypisanie do rdzeni, WiFi, cJSON, gniazda BSD, klient i serwer HTTP/HTTPS, pliki, WebSocket, konsola sieciowa, polecenia i powiadomienia Telegram | R0, RA, RV, S | R0, S | `network` na R0, RA, S; bramka na R0, S |
| `19_touch` | Kontrolery dotyku TSC2007 i STMPE610 | R0, RA, RV, S | R0, S | - |
| `20_irsmall_decoder` | Dekodowanie protokołu IRsmall | R0, RA, RV, S | R0, S | - |
| `21_stm32g474_fdcan_native` | Natywna obsługa FDCAN w STM32G474 | S | S | - |
| `22_rfid_nfc` | Czytniki MFRC522 RFID i PN532 NFC/RFID | R0, RA, RV, S | R0, S | - |
| `23_io_pmic` | LED RGB, proste ekspandery I/O/DAC i PMIC ADP5360 | R0, RA, RV, S | R0, S | - |
| `24_epd_display` | Fasada wyświetlacza e-paper i ścieżka odświeżania | R0, RA, RV, S | R0, S | - |
| `25_ota` | Wykrywanie, uwierzytelnione przygotowanie OTA, potwierdzenie próbne, wycofanie aktualizacji i odzyskiwanie BOOTSEL | R0, RA | R0 | - |
| `26_ble_stream` | Cykl życia BLE Peripheral, uwierzytelniony JH BLE Stream v1 i adapter routera poleceń | R0, RA, S | R0, RA, S | `commands` i `commands-freertos` na R0, RA, S; bramka na R0 |
| `27_lora_point_to_point` | Niskopoziomowy ping/pong SX1262 oraz pofragmentowane żądanie i odpowiedź routera przez `hal_lora_link` | R0, S | R0, S | `probe`, `responder`, `link` i `link-responder` na R0, S; ręczne warianty sprzętowe `sf7` i `responder-sf7` |
| `28_serial_commands` | Kierowanie poleceń ramkowanej Serial Session przez niezależny router | R0, RA, RV, S | R0, S | - |
| `29_bluetooth_gamepad` | Wykrywanie Classic, surowy HID Host i adapter normalizujący gamepad | R0, RA, S | R0 | `classic-scan`, `hid-host` i `ble` na R0, RA, S; bramka na R0 |

Kompilacje sieciowe dla rodziny RP używają `picow` z RP2040 i `pico2w` z RP2350
ARM. Konfiguracje RP2350 RISC-V wymagające CYW43 nie są obsługiwane. Projekty
sieciowe i Bluetooth STM32G474 wybierają profil NUCLEO-G474RE z zewnętrznym
PIM730/RM2.

Projekt LoRa domyślnie wybiera stałe profile testowe `pico-core1262-hf` i
`nucleo-g474re-core1262-hf`. Dla zintegrowanej płytki Waveshare LF jawnie użyj
`rp2040-lora-lf`; urządzenia LF i HF pracują w innych pasmach i tworzą
oddzielne fizyczne pary radiowe. Wariant `probe` bez nadawania sprawdza
obsługiwane funkcje, kalibrację, bieżące RSSI i CAD. Warianty bazowy i `responder`
używają SF9/10 dBm, a `sf7` i `responder-sf7` tworzą deterministyczną parę
sprzętową SF7/6 dBm.

Warianty `link` i `link-responder` wymieniają przez wspólny router powiązane ze
sobą binarne polecenie `echo` o rozmiarze 500 bajtów i odpowiedź. Oba kierunki
sprawdzają adresowanie, identyfikatory żądań, składanie trzech fragmentów,
tłumienie duplikatów i retransmisję. Trasa kierująca polecenie do procedury
obsługi dopuszcza także zaimplementowane już źródło `BLE_STREAM`, bez dodawania
transportu BLE do tego przykładu.

SX1261, SX1276 i SX1278 pozostają eksperymentalnymi integracjami
wyłącznie programowymi i nie dodają profili płytek ani deklaracji fizycznego
wsparcia na tych stanowiskach sprzętowych.

## Targety obsługiwane podczas kompilacji

| Target | Domyślna płytka | Toolchain | Artefakty firmware |
|---|---|---|---|
| `rp2040` | `pico` | oficjalny Pico SDK + GNU Arm | ELF, BIN, HEX, UF2, MAP |
| `rp2350-arm` | `pico2` | oficjalny Pico SDK + GNU Arm | ELF, BIN, HEX, UF2, MAP |
| `rp2350-riscv` | `pico2` | oficjalny Pico SDK + ustalona wersja toolchainu Hazard3 | ELF, BIN, HEX, UF2, MAP |
| `stm32g474` | `nucleo-g474re` | GNU Arm | ELF, BIN, HEX, MAP |

ESP32-S3 używa obecnie dedykowanych projektów ESP-IDF zamiast wspólnego skryptu
przykładów CMake. `tests/fixtures/esp32s3_phase3` kompiluje i linkuje pełny
zestaw backendów z etapów 2 i 3, a `tests/hardware/esp32s3_phase1` i
`tests/hardware/esp32s3_phase2` zachowują dostępne raporty sprzętowe. Dodanie
przykładów ESP32-S3 obsługiwanych przez ten skrypt wymaga również trybu kompilacji
ESP-IDF oraz walidacji płytki i zasobów dla każdego przykładu.

## Wymagania

- CMake 3.20 lub nowszy dla kompilacji firmware obsługiwanych przez wspólny skrypt;
- Python 3;
- `arm-none-eabi-gcc` dla RP2040, RP2350 ARM i STM32G474;
- zarządzane komponenty Pico SDK, picotool, FreeRTOS, lwIP, BearSSL, LittleFS i
  RISC-V przygotowane przez `./runmefirst.sh` lub
  `./third_party/update_components.sh`.

## Dodawanie przykładu

Przed utworzeniem kolejnego katalogu sprawdź, czy nowe zachowanie może rozszerzyć
istniejący projekt lub wariant. Domyślnie należy łączyć powiązane funkcje w
jednym projekcie. Dzięki temu nie trzeba wielokrotnie kompilować całego HAL-a w
małych projektach firmware dla każdego targetu.

Osobny projekt jest właściwy, gdy target, toolchain, runtime, profil płytki,
wzajemnie wykluczające się zasoby lub wymagania sprzętowe uniemożliwiają
użyteczny wspólny obraz. Opisz to ograniczenie tutaj i zadeklaruj dokładne
`targets` oraz `gateTargets` w `config/tooling/examples.json`. Wariant stosuj
tylko wtedy, gdy zachowania nie można wybrać w runtime. Każda zmiana wymaga
sprawdzenia liczby konfiguracji pełnej macierzy i domyślnej bramki.

## Polecenia kompilacji

Kompilacja pełnej obsługiwanej macierzy dla wybranego targetu:

```bash
scripts/examples_dispatcher.py build --target rp2040 --jobs "$(nproc)"
scripts/examples_dispatcher.py build --target rp2350-arm --jobs "$(nproc)"
scripts/examples_dispatcher.py build --target rp2350-riscv --jobs "$(nproc)"
scripts/examples_dispatcher.py build --target stm32g474 --jobs "$(nproc)"
```

Kompilacja jednego projektu za pomocą CLI używanego również przez VS Code:

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

`examples/CMakeLists.txt` jest prostym punktem wejścia do tego samego skryptu:

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

Wybrany runtime dostarcza `main()`. W trybie bare-metal RP uruchamia
`app_task0()` na rdzeniu 0, a rdzeń 1 dla `app_task1()` uruchamia tylko na
żądanie. FreeRTOS na RP tworzy zadania aplikacji przypisane do odpowiednich
rdzeni. STM32G474 wykonuje obie funkcje kooperacyjnie w trybie bare-metal albo
jako osobne zadania FreeRTOS. Aplikacje demonstracyjne na hoście używają pętli
kooperacyjnej.

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

Używaj samych nazw funkcji lub jawnej wartości `1`. Obsługiwane narzędzia
odrzucają `HAL_ENABLE_*=0`; aby wyłączyć funkcję, pomiń makro. Nagłówek projektu
powinien zawierać wyłącznie makra, ponieważ jest ładowany przed normalizacją
targetu i płytki. Definicje funkcji muszą być bezwarunkowe. Dopuszczalny jest
jedynie blok `#ifndef` chroniący ten sam symbol, ponieważ narzędzie wybierające
źródła na wczesnym etapie odczytuje plik jako tekst.

Proces kompilacji ustawia `HAL_PROVIDE_APP_ENTRY`. Informacje o pinach właściwe
dla płytki pochodzą z wybranego, wygenerowanego profilu. Jeśli nie pasuje żaden
stały profil złożony, połączenia określone przez aplikację można nadal opisać
jawnym deskryptorem sprzętu.

## VS Code

Etykiety generowanych zadań i skróty klawiszowe opisano w dokumencie
[JaszczurHAL w VS Code](../vscode/README.pl.md). Konfigurację projektu, wybór
targetu i płytki, wykrywanie źródeł oraz ścieżki artefaktów przedstawia
[proces pracy z projektem firmware](../doc/pl/FwProjectWorkflow.md).
