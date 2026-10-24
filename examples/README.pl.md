# Przykłady JaszczurHAL

Katalog `examples/` zawiera 30 projektów pokazujących, jak korzystać
z JaszczurHAL: od odczytu czujników i sterowania wyświetlaczami po komunikację
sieciową, Bluetooth i aktualizację firmware. Każdy projekt ma opis po polsku
w `README.pl.md` i po angielsku w `README.md`.

Otwórz katalog wybranego projektu w VS Code, aby korzystać z jego zadań
`Build`, `Upload`, `Serial Monitor`, `Clean`, `Config Dump`, `OTA` i wyboru
montażu.

Każdy przykład jest zwykłym projektem firmware. Jego `device_tree.json`
opisuje sprzęt każdego obsługiwanego montażu: platformę, płytkę, podłączone
urządzenia z ich połączeniami, diodę statusu i drzewo zegara. Plik
`hal_project_config.h` deklaruje funkcje HAL i warianty (`JH_PROJECT_VARIANTS`).
Pisany ręcznie `.vscode/jaszczurhal.project.json` zawiera ustawienia narzędzi,
np. domyślny montaż (`assembly`). Skrypt `scripts/examples_dispatcher.py`
generuje na ich podstawie pozostałe pliki VS Code i kompiluje każdy montaż
z każdym wariantem przez `vscode/entry/jh-vscode`. Zasady opisują część
[platformy i warianty](../doc/pl/FwProjectWorkflow.md#platformy-i-warianty)
oraz [format device tree](../doc/pl/device_tree_format.md).

## Katalog projektów

Skróty w tabeli: `R0` oznacza `rp2040`, `RA` - `rp2350-arm`, `RV` -
`rp2350-riscv`, `S` - `stm32g474`, a `E` - `esp32s3`. Wariant kompiluje się
dla wszystkich platform projektu, chyba że tabela wymienia konkretne
platformy. Lokalne etapy `examples-<target>` skryptu `runalltests.sh` kompilują wszystkie konfiguracje
dla `rp2040`, `stm32g474` i `esp32s3`. **Dostępna konfiguracja kompilacji nie
oznacza potwierdzenia działania na każdej płytce.** Zakres testów i wymagane
połączenia opisują README poszczególnych projektów. Montaże każdego projektu
pokazuje `scripts/examples_dispatcher.py list`.

| Projekt | Co pokazuje przykład | Obsługiwane platformy | Warianty |
|---|---|---|---|
| `01_core_runtime` | Miganie diodą, diagnostyka platformy, timery programowe i obliczenia regulatora PID. | R0, RA, RV, S, E | `CAPTURE` |
| `02_crypto` | Obliczanie MD5 oraz szyfrowanie i odszyfrowywanie ChaCha20-Poly1305. | R0, RA, RV, S, E | - |
| `03_modem_A7670E` | Uruchomienie modemu A7670/A7672 i wysyłanie wiadomości MQTT przez sieć komórkową. | R0, RA, RV, E | - |
| `04_sensor_hub` | Pomiar temperatury DS18B20, temperatury i wilgotności DHT oraz oświetlenia BH1750. | R0, RA, RV, S, E | - |
| `05_serial_gps` | Odczyt danych GPS przez UART; wariant z pętlą zwrotną programowego portu szeregowego. | R0, RA, RV, S, E | `SWSERIAL` na R0, RA, RV |
| `06_thermocouple` | Odczyt temperatury z termopar przez MCP9600 i MAX6675. | R0, RA, RV, S, E | - |
| `07_display_media` | Wyświetlanie grafiki na ILI9341, dekodowanie PNG/JPEG i konwersja Base64/RGB565. | R0, RA, RV, S, E | - |
| `08_mqtt` | Publikowanie i odbieranie wiadomości MQTT przez sieć obsługiwaną przez CYW43. | R0, RA, S, E | - |
| `09_wireguard` | Przygotowanie konfiguracji WireGuard; samo uruchomienie przykładu nie potwierdza zestawienia tunelu. | R0, RA, S, E | - |
| `10_storage` | Zapis ustawień i licznika uruchomień w KV, plików w LittleFS oraz logów na karcie SD/FatFs. | R0, RA, RV, S | - |
| `11_i2c_slave` | Udostępnienie statusu, licznika i czasu w rejestrach urządzenia I2C slave. | R0, RA, RV, S, E | - |
| `12_i2c_scan` | Wykrywanie adresów na I2C z limitem czasu; połączenia w kodzie dobrano dla STM32G474. | R0, RA, RV, S, E | - |
| `13_adc` | Odczyt napięcia z wewnętrznego ADC i przetwornika ADS1115; w osobnym wariancie ciągły, sprzętowo taktowany skan DMA wejść wewnętrznych. | R0, RA, RV, S, E | `SCAN` |
| `14_can_mcp2515` | Wysyłanie i odbieranie ramek klasycznego CAN przez MCP2515. | R0, RA, RV, S, E | - |
| `15_display_oled_lcd` | Wyświetlanie tekstu na OLED SSD1306 i znakowym LCD HD44780. | R0, RA, RV, S, E | - |
| `16_rtc_backends` | Odczyt RTC, wybudzanie i tryby oszczędzania energii; wariant zegara DS3231/ILI9341. | R0, RA, RV, S | `DISPLAY_CLOCK` na S |
| `17_audio_output` | Regulacja wzmocnienia PGA2311 oraz generowanie dźwięku przez PWM z DMA. | R0, RA, RV, S, E | - |
| `18_freertos_suite` | Zadania FreeRTOS; wariant sieciowy z WiFi, cJSON, BSD, serwerem HTTP, klientem HTTP/HTTPS, plikami, WebSocket i konsolą. Telegram jest włączony do kompilacji, ale przykład nie wysyła powiadomień. | R0, RA, RV, S, E | `NETWORK` na R0, RA, S, E |
| `19_touch` | Odczyt dotyku z TSC2007 i STMPE610. | R0, RA, RV, S, E | - |
| `20_irsmall_decoder` | Odbiór i dekodowanie sygnałów podczerwieni przez IRsmall. | R0, RA, RV, S, E | - |
| `21_stm32g474_fdcan_native` | Wysyłanie i odbieranie ramek CAN FD na wszystkich kanałach płytki przez wbudowane kontrolery FDCAN w STM32G474; domyślnie pętla zwrotna bez okablowania. | S | - |
| `22_rfid_nfc` | Odczyt identyfikatorów kart przez MFRC522 i PN532. | R0, RA, RV, S, E | - |
| `23_io_pmic` | Sterowanie diodą RGB, ekspanderem I/O i DAC oraz odczyt stanu zasilania z ADP5360. | R0, RA, RV, S, E | - |
| `24_epd_display` | Wyświetlanie wzoru testowego i odświeżanie ekranu e-paper 200 × 200. | R0, RA, RV, S, E | - |
| `25_ota` | Aktualizacja OTA: wykrywanie urządzenia, przygotowanie po uwierzytelnieniu, potwierdzenie nowej wersji, wycofanie aktualizacji i odzyskiwanie przez BOOTSEL. | R0, RA, E | - |
| `26_ble_stream` | Wymiana danych i poleceń przez JH BLE Stream v1 po obustronnym uwierzytelnieniu. | R0, RA, S | `COMMANDS`, `COMMANDS_FREERTOS` |
| `27_lora_point_to_point` | Wymiana ping/pong przez SX1262 oraz 500-bajtowych poleceń i odpowiedzi we fragmentach przez `hal_lora_link`. | R0, S | `PROBE`, `RESPONDER`, `SF7`, `RESPONDER_SF7`, `LINK`, `LINK_RESPONDER` |
| `28_serial_commands` | Odbieranie poleceń Serial Session i kierowanie ich do wspólnych procedur obsługi. | R0, RA, RV, S, E | - |
| `29_bluetooth_gamepad` | Odczyt gamepada, wykrywanie urządzeń Classic i odbiór surowych raportów HID. | R0, RA, S | `CLASSIC_SCAN`, `HID_HOST`, `BLE` |
| `30_bluetooth_speaker` | Odbiór dźwięku A2DP i odtwarzanie przez PWM; opcjonalna regulacja głośności AVRCP i kompilacja z BLE. | R0, RA | `AVRCP`, `BLE_A2DP` |

Projekty sieciowe na RP używają montażu `picow` dla RP2040 i `pico2w`
dla RP2350 ARM. Konfiguracje RP2350 RISC-V wymagające CYW43 nie są
obsługiwane. Sieć i Bluetooth na STM32G474 korzystają z montażu
`nucleo-pim730`: NUCLEO-G474RE z zewnętrznym modułem PIM730/RM2 podłączonym
tak, jak opisuje `device_tree.json` projektu.

Projekt LoRa ma montaże `pico-core1262-hf` i `nucleo-core1262-hf`
z zewnętrznym modułem Waveshare Core1262-HF oraz `rp2040-lora-lf` dla
zintegrowanej płytki Waveshare LF. Urządzenia LF i HF pracują w różnych
pasmach; nie zestawiaj z nich jednej pary radiowej. Wariant `PROBE` sprawdza funkcje
układu, kalibrację, bieżące RSSI i CAD bez nadawania. Wersja podstawowa
i `RESPONDER` używają SF9/10 dBm, a `SF7` i `RESPONDER_SF7` tworzą parę
testową z SF7/6 dBm.

Warianty `LINK` i `LINK_RESPONDER` wymieniają 500-bajtowe binarne polecenie
`echo` i odpowiedź, po trzy fragmenty w każdym kierunku. Sprawdzają
adresowanie, identyfikatory żądań, składanie wiadomości, odrzucanie
duplikatów i retransmisję. Reguła routera dopuszcza także źródło
`BLE_STREAM`, ale ten przykład nie uruchamia komunikacji BLE.
SX1261, SX1276 i SX1278 pozostają eksperymentalnymi integracjami
programowymi: nie mają tu własnych profili płytek ani deklarowanego
potwierdzenia działania na sprzęcie.

<a id="targety-obsługiwane-podczas-kompilacji"></a>

## Platformy obsługiwane podczas kompilacji

| Platforma | Płytka podstawowych montaży | Narzędzia kompilacji | Pliki wynikowe firmware |
|---|---|---|---|
| `rp2040` | `pico` | oficjalny Pico SDK + GNU Arm | ELF, BIN, HEX, UF2, MAP |
| `rp2350-arm` | `pico2` | oficjalny Pico SDK + GNU Arm | ELF, BIN, HEX, UF2, MAP |
| `rp2350-riscv` | `pico2` | oficjalny Pico SDK + ustalona wersja narzędzi Hazard3 | ELF, BIN, HEX, UF2, MAP |
| `stm32g474` | `nucleo-g474re` | GNU Arm | ELF, BIN, HEX, MAP |
| `esp32s3` | `waveshare-esp32-s3-zero` | ESP-IDF w ustalonej wersji | ELF, BIN, MAP, bootloader, tabela partycji, `jh_esp_idf_artifacts.json` |

Dla `esp32s3` `jh-vscode` uruchamia `scripts/build_esp_idf.py` z nagłówkiem
projektu i wybranym wariantem zamiast dispatchera CMake. Przykład ma montaż
`esp32s3` tylko wtedy, gdy każda żądana przez niego funkcja jest na liście
`supportedFeatures` targetu; lista obejmuje backendy ESP-IDF oraz przenośne
sterowniki magistral, czujników, wyświetlaczy, kodeków i komend, a poza nią
pozostają pamięć flash, zasilanie, wewnętrzny RTC, LoRa, BLE Stream
i Bluetooth Classic. Projekt `tests/fixtures/esp32s3_phase3`
nadal sprawdza pełne domknięcie funkcji z etapów 2 i 3, a raporty testów
sprzętowych znajdują się w `tests/hardware/esp32s3_phase1`
i `tests/hardware/esp32s3_phase2`. Audio PWM jest dostępne w montażu
`esp32s3-zero` przykładu [`17_audio_output`](17_audio_output/); odtwarzanie
odmierza timer zamiast DMA używanego na RP. Montaże `esp32s3-zero` używają wyłącznie
pinów wyprowadzonych na ESP32-S3-Zero; generator odrzuca każdy inny pin.

## Wymagania

Potrzebne są CMake 3.22 lub nowszy, Python 3 i odpowiedni kompilator.
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
konflikt zasobów lub wymagania sprzętowe, i opisz przyczynę w tym katalogu.
Nowy przykład to katalog w `examples/` z ręcznie napisanym
`.vscode/jaszczurhal.project.json`, plikiem `device_tree.json` z montażami
i plikiem `hal_project_config.h`. Pozostałe pliki VS Code zapisze
`scripts/examples_dispatcher.py generate`.

Wariant dodawaj do `JH_PROJECT_VARIANTS` tylko wtedy, gdy zachowania nie można
wybrać podczas działania programu. Ograniczenie wariantu do części platform
zapisz w nagłówku przez `#error`. Wynikowe konfiguracje pokazuje
`scripts/examples_dispatcher.py list`; po zmianie zaktualizuj liczby
konfiguracji pełnej macierzy i bramki zapisane w
`tests/test_vscode_native_workflow.py`.

## Polecenia kompilacji

Pełny zestaw obsługiwanych konfiguracji dla wybranej platformy:

```bash
scripts/examples_dispatcher.py build --target rp2040 --jobs "$(nproc)"
scripts/examples_dispatcher.py build --target rp2350-arm --jobs "$(nproc)"
scripts/examples_dispatcher.py build --target rp2350-riscv --jobs "$(nproc)"
scripts/examples_dispatcher.py build --target esp32s3 --jobs "$(nproc)"
scripts/examples_dispatcher.py build --target stm32g474 --jobs "$(nproc)"
```

Dodaj `--compiler-cache`, aby używać wydania ccache przypiętego w jhbase.
Przygotuj je przez `scripts/ensure_ccache.sh` lub updater komponentów. Bramka
stosuje wspólny cache do wszystkich buildów CMake: testów hosta i sanitizerów,
firmware, bibliotek, przykładów oraz buildów uruchamianych wewnątrz testów.
Nadal wykonuje konfigurację od zera, linkowanie i testy. Sondy samego
preprocessora (`-E`), w tym używane przez `test_feature_registry`, nie są cachowane.

Domyślny cache znajduje się w `${XDG_CACHE_HOME:-$HOME/.cache}/jaszczurhal/ccache`
na Linuksie oraz `%LOCALAPPDATA%/jaszczurhal/ccache` na Windows.
`JH_COMPILER_CACHE_DIR` wybiera inny katalog; umieść go poza checkoutem, aby
przetrwał `--commit`. Prywatna konfiguracja ustawia limit 5 GB, sprawdzanie
zawartości kompilatora, `base_dir` na katalog checkoutu i `hash_dir = false`.
Zgodne ścieżki względne i wejścia kompilatora pozwalają współdzielić wyniki między
katalogami buildów i checkoutami. Zapisane informacje debugowania mogą wskazywać
pierwotny katalog buildu; do pracy z debuggerem użyj buildu bez tej opcji.

Wyszukiwanie wyniku używa preprocessora: tryb direct może pominąć nowo utworzony
nagłówek wybierany przez `__has_include`, w tym `hal_project_config.h`.
Sprawdzanie nagłówków i opcji kompilatora pozostaje ścisłe. Każdy etap bramki
wypisuje statystyki cache i zapisuje swój czas w `.build/gate/logs/stage-times.tsv`.

`--jobs` określa łączny budżet kompilacji, dzielony między najwyżej dwa
projekty. ESP-IDF konfiguruje projekt przez `idf.py`, a CMake buduje go
przez Ninja z jawnym limitem. Zagnieżdżony build bootloadera używa jednego zadania.

Jeden projekt, z użyciem tego samego narzędzia co VS Code:

```bash
vscode/entry/jh-vscode build \
  --project examples/01_core_runtime --assembly pico
```

Wybrane projekty:

```bash
scripts/examples_dispatcher.py build \
  --target rp2040 \
  --example 01_core_runtime --example 10_storage
```

Jeden wariant:

```bash
vscode/entry/jh-vscode build \
  --project examples/16_rtc_backends \
  --assembly nucleo --variant DISPLAY_CLOCK
```

Wyświetlenie zestawu konfiguracji i odświeżenie wygenerowanych plików VS Code
po zmianie manifestu lub nagłówka:

```bash
scripts/examples_dispatcher.py list
scripts/examples_dispatcher.py generate
scripts/examples_dispatcher.py check
```

`python3 scripts/sync_generated.py --write` uruchamia `generate` razem
z pozostałymi generatorami repozytorium.

Wszystko, co dotyczy jednego montażu, trafia do
`.build/examples/<example>/<target>/<assembly>/`: pliki wynikowe, katalog
roboczy CMake w `cmake/` i każdy wariant w `variants/<id>/` pod nim.

## Struktura aplikacji

```text
NN_example_name/
  app.c
  device_tree.json
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
przed ustaleniem końcowej konfiguracji platformy i płytki. Definicje mogą
zależeć od makra platformy lub definicji wariantu; zobacz
[platformy i warianty](../doc/pl/FwProjectWorkflow.md#platformy-i-warianty).

Proces kompilacji ustawia `HAL_PROVIDE_APP_ENTRY`. Piny, magistrale
i ustawienia urządzeń pochodzą z wygenerowanych makr `JH_HW_*` wybranego
montażu, a `HAL_LED_BUILTIN` z jego diody statusu.

## VS Code

Nazwy zadań i skróty klawiszowe opisano w rozdziale
[JaszczurHAL w VS Code](../vscode/README.pl.md). Konfigurację projektu,
wybór platformy i płytki, wyszukiwanie źródeł oraz katalogi wynikowe
przedstawia [praca z projektem firmware](../doc/pl/FwProjectWorkflow.md).

Manifest pisze się ręcznie. Pliki `settings.json`, `tasks.json`,
`launch.json`, `keybindings.reference.json` i `extensions.json` generuje
na podstawie manifestu, nagłówka i `device_tree.json`
`scripts/examples_dispatcher.py generate`.
Trwałą zmianę opisu w tych plikach wprowadź w źródle generatora; ręczna
edycja zostanie nadpisana przy kolejnym odświeżeniu.

Wariant `CAPTURE` przykładu `01_core_runtime` ma osobny punkt wejścia, ponieważ rezerwuje wejście pomiarowe i zasoby timera/DMA; bazowa aplikacja diagnostyczna ich nie potrzebuje.
