# Przykłady JaszczurHAL

Katalog `examples/` zawiera 30 projektów pokazujących, jak korzystać
z JaszczurHAL: od odczytu czujników i sterowania wyświetlaczami po komunikację
sieciową, Bluetooth i aktualizację firmware. Każdy projekt ma opis po polsku
w `README.pl.md` i po angielsku w `README.md`.

Otwórz katalog wybranego projektu w VS Code, aby korzystać z jego zadań
`Build`, `Upload`, `Serial Monitor`, `Clean`, `Config Dump`, `OTA` i wyboru
płytki. Wygenerowany plik `.vscode/jaszczurhal.project.json` opisuje projekt
w taki sam sposób jak samodzielną aplikację firmware.

Rejestr `config/tooling/examples.json` określa źródła, funkcje, platformy,
profile płytek i warianty. Skrypt `scripts/examples_dispatcher.py` odczytuje
te ustawienia i generuje manifesty dla `vscode/entry/jh-vscode` oraz
`cmake/jh_firmware_project`.

## Katalog projektów

Skróty w tabeli: `R0` oznacza `rp2040`, `RA` - `rp2350-arm`, `RV` -
`rp2350-riscv`, a `S` - `stm32g474`. Kolumna `gateTargets` wskazuje podzbiór
platform objętych domyślną kontrolą kompilacji. **Dostępna konfiguracja
kompilacji nie oznacza potwierdzenia działania na każdej płytce.** Zakres
testów i wymagane połączenia opisują README poszczególnych projektów.

| Projekt | Co pokazuje przykład | Obsługiwane platformy | `gateTargets` | Warianty |
|---|---|---|---|---|
| `01_core_runtime` | Miganie diodą, diagnostyka platformy, timery programowe i obliczenia regulatora PID. | R0, RA, RV, S | R0, S | `capture` |
| `02_crypto` | Obliczanie MD5 oraz szyfrowanie i odszyfrowywanie ChaCha20-Poly1305. | R0, RA, RV, S | R0, S | - |
| `03_modem_A7670E` | Uruchomienie modemu A7670/A7672 i wysyłanie wiadomości MQTT przez sieć komórkową. | R0, RA, RV | R0 | - |
| `04_sensor_hub` | Pomiar temperatury DS18B20, temperatury i wilgotności DHT oraz oświetlenia BH1750. | R0, RA, RV, S | R0, S | - |
| `05_serial_gps` | Odczyt danych GPS przez UART; wariant z pętlą zwrotną programowego portu szeregowego. | R0, RA, RV, S | R0, S | `swserial` na R0, RA, RV; domyślna kontrola na R0 |
| `06_thermocouple` | Odczyt temperatury z termopar przez MCP9600 i MAX6675. | R0, RA, RV, S | R0, S | - |
| `07_display_media` | Wyświetlanie grafiki na ILI9341, dekodowanie PNG/JPEG i konwersja Base64/RGB565. | R0, RA, RV, S | R0, S | - |
| `08_mqtt` | Publikowanie i odbieranie wiadomości MQTT przez sieć obsługiwaną przez CYW43. | R0, RA, S | R0, S | - |
| `09_wireguard` | Przygotowanie konfiguracji WireGuard; samo uruchomienie przykładu nie potwierdza zestawienia tunelu. | R0, RA, S | R0, S | - |
| `10_storage` | Zapis ustawień i licznika uruchomień w KV, plików w LittleFS oraz logów na karcie SD/FatFs. | R0, RA, RV, S | R0, S | - |
| `11_i2c_slave` | Udostępnienie statusu, licznika i czasu w rejestrach urządzenia I2C slave. | R0, RA, RV, S | R0, S | - |
| `12_i2c_scan` | Wykrywanie adresów na I2C z limitem czasu; połączenia w kodzie dobrano dla STM32G474. | R0, RA, RV, S | R0, S | - |
| `13_adc` | Odczyt napięcia z wewnętrznego ADC i przetwornika ADS1115; w osobnym wariancie ciągły, sprzętowo taktowany skan DMA wejść wewnętrznych. | R0, RA, RV, S | R0, S | `scan` na R0, RA, RV, S; domyślna kontrola na R0, S |
| `14_can_mcp2515` | Wysyłanie i odbieranie ramek klasycznego CAN przez MCP2515. | R0, RA, RV, S | R0, S | - |
| `15_display_oled_lcd` | Wyświetlanie tekstu na OLED SSD1306 i znakowym LCD HD44780. | R0, RA, RV, S | R0, S | - |
| `16_rtc_backends` | Odczyt RTC, wybudzanie i tryby oszczędzania energii; wariant zegara DS3231/ILI9341. | R0, RA, RV, S | R0, S | wybierany osobno `display-clock` na S |
| `17_audio_output` | Regulacja wzmocnienia PGA2311 oraz generowanie dźwięku przez PWM z DMA. | R0, RA, RV, S | R0, S | - |
| `18_freertos_suite` | Zadania FreeRTOS; wariant sieciowy z WiFi, cJSON, BSD, serwerem HTTP, klientem HTTP/HTTPS, plikami, WebSocket i konsolą. Telegram jest włączony do kompilacji, ale przykład nie wysyła powiadomień. | R0, RA, RV, S | R0, S | `network` na R0, RA, S; domyślna kontrola na R0, S |
| `19_touch` | Odczyt dotyku z TSC2007 i STMPE610. | R0, RA, RV, S | R0, S | - |
| `20_irsmall_decoder` | Odbiór i dekodowanie sygnałów podczerwieni przez IRsmall. | R0, RA, RV, S | R0, S | - |
| `21_stm32g474_fdcan_native` | Wysyłanie i odbieranie ramek przez wbudowany kontroler FDCAN w STM32G474. | S | S | - |
| `22_rfid_nfc` | Odczyt identyfikatorów kart przez MFRC522 i PN532. | R0, RA, RV, S | R0, S | - |
| `23_io_pmic` | Sterowanie diodą RGB, ekspanderem I/O i DAC oraz odczyt stanu zasilania z ADP5360. | R0, RA, RV, S | R0, S | - |
| `24_epd_display` | Wyświetlanie wzoru testowego i odświeżanie ekranu e-paper 200 × 200. | R0, RA, RV, S | R0, S | - |
| `25_ota` | Aktualizacja OTA: wykrywanie urządzenia, przygotowanie po uwierzytelnieniu, potwierdzenie nowej wersji, wycofanie aktualizacji i odzyskiwanie przez BOOTSEL. | R0, RA | R0 | - |
| `26_ble_stream` | Wymiana danych i poleceń przez JH BLE Stream v1 po obustronnym uwierzytelnieniu. | R0, RA, S | R0, RA, S | `commands` i `commands-freertos` na R0, RA, S; domyślna kontrola na R0 |
| `27_lora_point_to_point` | Wymiana ping/pong przez SX1262 oraz 500-bajtowych poleceń i odpowiedzi we fragmentach przez `hal_lora_link`. | R0, S | R0, S | `probe`, `responder`, `link` i `link-responder` na R0, S; warianty sprzętowe wybierane osobno `sf7` i `responder-sf7` |
| `28_serial_commands` | Odbieranie poleceń Serial Session i kierowanie ich do wspólnych procedur obsługi. | R0, RA, RV, S | R0, S | - |
| `29_bluetooth_gamepad` | Odczyt gamepada, wykrywanie urządzeń Classic i odbiór surowych raportów HID. | R0, RA, S | R0 | `classic-scan`, `hid-host` i `ble` na R0, RA, S; domyślna kontrola na R0 |
| `30_bluetooth_speaker` | Odbiór dźwięku A2DP i odtwarzanie przez PWM; opcjonalna regulacja głośności AVRCP i kompilacja z BLE. | R0, RA | R0, RA | `avrcp` i `ble-a2dp` na R0, RA; oba w domyślnej kontroli |

Projekty sieciowe na RP używają domyślnie `picow` dla RP2040 i `pico2w`
dla RP2350 ARM. Konfiguracje RP2350 RISC-V wymagające CYW43 nie są
obsługiwane. Dla sieci i Bluetooth na STM32G474 wybierany jest profil
NUCLEO-G474RE z zewnętrznym modułem PIM730/RM2.

Projekt LoRa używa domyślnie profili `pico-core1262-hf` i
`nucleo-g474re-core1262-hf`. Dla zintegrowanej płytki Waveshare LF wybierz
jawnie `rp2040-lora-lf`. Urządzenia LF i HF pracują w różnych pasmach;
nie zestawiaj z nich jednej pary radiowej. Wariant `probe` sprawdza funkcje
układu, kalibrację, bieżące RSSI i CAD bez nadawania. Wersja podstawowa
i `responder` używają SF9/10 dBm, a `sf7` i `responder-sf7` tworzą parę
testową z SF7/6 dBm.

Warianty `link` i `link-responder` wymieniają 500-bajtowe binarne polecenie
`echo` i odpowiedź, po trzy fragmenty w każdym kierunku. Sprawdzają
adresowanie, identyfikatory żądań, składanie wiadomości, odrzucanie
duplikatów i retransmisję. Reguła routera dopuszcza także źródło
`BLE_STREAM`, ale ten przykład nie uruchamia komunikacji BLE.
SX1261, SX1276 i SX1278 pozostają eksperymentalnymi integracjami
programowymi: nie mają tu własnych profili płytek ani deklarowanego
potwierdzenia działania na sprzęcie.

<a id="targety-obsługiwane-podczas-kompilacji"></a>

## Platformy obsługiwane podczas kompilacji

| Platforma | Domyślna płytka | Narzędzia kompilacji | Pliki wynikowe firmware |
|---|---|---|---|
| `rp2040` | `pico` | oficjalny Pico SDK + GNU Arm | ELF, BIN, HEX, UF2, MAP |
| `rp2350-arm` | `pico2` | oficjalny Pico SDK + GNU Arm | ELF, BIN, HEX, UF2, MAP |
| `rp2350-riscv` | `pico2` | oficjalny Pico SDK + ustalona wersja narzędzi Hazard3 | ELF, BIN, HEX, UF2, MAP |
| `stm32g474` | `nucleo-g474re` | GNU Arm | ELF, BIN, HEX, MAP |

ESP32-S3 korzysta z osobnych projektów ESP-IDF, a nie ze wspólnego skryptu
kompilacji tych przykładów. Projekt `tests/fixtures/esp32s3_phase3`
sprawdza kompilację i linkowanie implementacji z etapów 2 i 3. Dostępne
raporty testów sprzętowych znajdują się w `tests/hardware/esp32s3_phase1`
i `tests/hardware/esp32s3_phase2`. Dołączenie ESP32-S3 do wspólnego
skryptu wymaga obsługi kompilacji ESP-IDF oraz sprawdzenia płytki
i zasobów dla każdego przykładu.

## Wymagania

Potrzebne są CMake 3.20 lub nowszy, Python 3 i odpowiedni kompilator.
Dla RP2040, RP2350 ARM i STM32G474 jest to `arm-none-eabi-gcc`.
Komponenty Pico SDK, picotool, FreeRTOS, lwIP, BearSSL, LittleFS oraz
narzędzia RISC-V przygotuj przez `./runmefirst.sh` albo
`./third_party/update_components.sh`.

## Dodawanie przykładu

Najpierw sprawdź, czy nową funkcję można pokazać w istniejącym projekcie.
Łączenie powiązanych funkcji ogranicza powtarzanie tej samej konfiguracji
i wielokrotną kompilację całego HAL-a dla każdej platformy.

Utwórz osobny projekt, gdy połączenie nie jest praktyczne ze względu na
platformę, narzędzia kompilacji, sposób wykonywania zadań, profil płytki,
konflikt zasobów lub wymagania sprzętowe. Opisz przyczynę w tym katalogu
i podaj dokładne `targets` oraz `gateTargets` w
`config/tooling/examples.json`. Wariant dodawaj wtedy, gdy zachowania
nie można wybrać podczas działania programu. Po zmianie sprawdź liczbę
konfiguracji w pełnym zestawie kompilacji i w domyślnych kontrolach.

## Polecenia kompilacji

Pełny zestaw obsługiwanych konfiguracji dla wybranej platformy:

```bash
scripts/examples_dispatcher.py build --target rp2040 --jobs "$(nproc)"
scripts/examples_dispatcher.py build --target rp2350-arm --jobs "$(nproc)"
scripts/examples_dispatcher.py build --target rp2350-riscv --jobs "$(nproc)"
scripts/examples_dispatcher.py build --target stm32g474 --jobs "$(nproc)"
```

Jeden projekt, z użyciem tego samego narzędzia co VS Code:

```bash
vscode/entry/jh-vscode build \
  --project examples/01_core_runtime --target rp2040 --board pico
```

Wybrane projekty z rejestru:

```bash
scripts/examples_dispatcher.py build \
  --target rp2040 \
  --example 01_core_runtime --example 10_storage
```

Wyświetlenie zestawu konfiguracji i odświeżenie plików generowanych po
zmianie rejestru:

```bash
scripts/examples_dispatcher.py list
python3 scripts/sync_generated.py --write
```

Pliki wynikowe trafiają do `.build/examples/<example>/`. Katalogi robocze
CMake mają postać `.build/examples/<example>/cmake/<target>/<board>/`.
Plik `examples/CMakeLists.txt` pozwala wywołać ten sam skrypt przez CMake:

```bash
cmake -S examples -B .build/examples-cmake/rp2040 \
  -DJH_EXAMPLE_TARGET=rp2040
cmake --build .build/examples-cmake/rp2040
```

## Struktura aplikacji

```text
NN_example_name/
  app.c
  hal_project_config.h
  .vscode/
    jaszczurhal.project.json
    tasks.json
    settings.json
```

Aplikacja udostępnia następujące funkcje:

```c
void app_start(void);
void app_task0(void);
void app_task1(void); /* opcjonalnie z HAL_ENABLE_APP_TASK1 */
```

Wybrana implementacja obsługi aplikacji dostarcza `main()`. Na RP bez
systemu operacyjnego `app_task0()` działa na rdzeniu 0, a rdzeń 1 jest
uruchamiany dla `app_task1()` tylko na żądanie. Z FreeRTOS funkcje aplikacji
wykonują się jako zadania przypisane do odpowiednich rdzeni. STM32G474
wykonuje obie funkcje kooperacyjnie bez systemu operacyjnego albo jako
osobne zadania FreeRTOS. Przykłady uruchamiane na komputerze korzystają
z pętli kooperacyjnej.

Minimalna aplikacja migająca diodą:

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

Moduły biblioteki włącza się w `hal_project_config.h`:

```c
#pragma once

#define HAL_ENABLE_I2C
#define HAL_ENABLE_BH1750
```

Definiuj makra `HAL_ENABLE_*` bez wartości albo z wartością `1`.
Narzędzia projektu odrzucają wartość `0`; aby wyłączyć moduł, pomiń makro.
Nagłówek powinien zawierać wyłącznie makra, ponieważ jest odczytywany
przed ustaleniem końcowej konfiguracji platformy i płytki. Definicje muszą
być bezwarunkowe. Wyjątkiem jest `#ifndef` chroniący definicję tego samego
symbolu: narzędzie wybierające źródła odczytuje te deklaracje jako tekst,
a nie wynik pełnego przetwarzania przez preprocesor.

Proces kompilacji ustawia `HAL_PROVIDE_APP_ENTRY`. Przypisanie pinów
pochodzi z wybranego, wygenerowanego profilu płytki. Gdy żaden gotowy
profil zestawu nie odpowiada połączeniom aplikacji, można opisać je
własnym deskryptorem sprzętu.

## VS Code

Nazwy zadań i skróty klawiszowe opisano w rozdziale
[JaszczurHAL w VS Code](../vscode/README.pl.md). Konfigurację projektu,
wybór platformy i płytki, wyszukiwanie źródeł oraz katalogi wynikowe
przedstawia [praca z projektem firmware](../doc/pl/FwProjectWorkflow.md).

Pliki projektów i zadań są generowane na podstawie rejestru i narzędzi
repozytorium. Trwałe zmiany opisów trzeba wprowadzać także w odpowiednim
źródle generatora; ręczna zmiana wygenerowanego pliku może zostać nadpisana
przy kolejnym odświeżeniu.

Wariant `capture` przykładu `01_core_runtime` ma osobny punkt wejścia, ponieważ rezerwuje wejście pomiarowe i zasoby timera/DMA; bazowa aplikacja diagnostyczna ich nie potrzebuje.
