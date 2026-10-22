<a id="12---skaner-i2c-i-sprzętowa-weryfikacja-stm32g474"></a>

# 12 - Wykrywanie urządzeń I2C na STM32G474

Przykład skanuje adresy I2C `0x08`-`0x77` i wypisuje urządzenia odpowiadające
sygnałem ACK. Służy do sprawdzenia połączeń i podstawowej komunikacji I2C
w każdym montażu z `device_tree.json`, w tym I2C1 na STM32G474.

Na NUCLEO-G474RE SCL jest na PB8 (D15), a SDA na PB9 (D14). Magistrala pracuje
z częstotliwością 100 kHz. Na STM32G474 ustawienia TIMINGR korzystają
z zegara HSI16 wybranego dla I2C, niezależnie od SYSCLK/PCLK1. Konsola NUCLEO
używa USART2 przez ST-Link Virtual COM Port, z ustawieniem **115200 8N1**.

Aplikacja wywołuje `hal_i2c_scan_bus()` i przekazuje `hal_watchdog_feed`,
aby obsłużyć watchdog przy każdej próbie. Po skanowaniu wypisuje wyniki
i czeka dwie sekundy przed kolejnym odczytem.

Ten przykład używa 100 kHz; przykład `examples/16_rtc_backends` korzysta
z tego samego połączenia przy 400 kHz.

## Połączenia sprzętowe

Połączenia każdego montażu opisuje `device_tree.json`, a program bierze
magistralę, jej piny i częstotliwość z wygenerowanego nagłówka. Poniższa
tabela powtarza te połączenia.

| Sygnał | Pico / Pico 2 | NUCLEO-G474RE | ESP32-S3-Zero |
|---|---|---|---|
| SDA | GPIO4 | PB9, pin 5 CN10 / D14 | GPIO4 |
| SCL | GPIO5 | PB8, pin 3 CN10 / D15 | GPIO5 |

Na Pico i Pico 2 magistrala korzysta z GPIO4/GPIO5 zamiast GPIO25/GPIO24,
które sterują tam diodą i wykrywają napięcie VBUS. Na ESP32-S3-Zero również
używa GPIO4/GPIO5, bo ESP32-S3 nie ma GPIO24 ani GPIO25.

Połączenie z NUCLEO-G474RE:

```
Nucleo-G474RE                 Urządzenie I2C (np. PCF8563 RTC, AT24C256, BME280)
  PB8 (D15, SCL) ───┬──────── SCL
  PB9 (D14, SDA) ──┬┼──────── SDA
  3V3 ─────────────┼┼──[4.7k]─┘   (podciągnięcie SDA)
                   └─────[4.7k]──── 3V3   (podciągnięcie SCL)
  3V3 ──────────────────────────── VCC
  GND ──────────────────────────── GND
```

Zastosuj zewnętrzne rezystory podciągające 2,2-10 kΩ do 3V3. Wewnętrzne
podciąganie STM32 nie zastępuje rezystorów wymaganych w tym połączeniu.
Sprawdź, czy moduł nie ma ich już wbudowanych.

Dla modułu DS3231 ze złączem `+ D C NC -` użyj:

| Pin modułu | Połącz z |
|---|---|
| `+` | `3V3` |
| `D` | `D14` / `PB9` / SDA |
| `C` | `D15` / `PB8` / SCL |
| `NC` | Pozostaw niepodłączony |
| `-` | `GND` |

Zasil moduł napięciem 3,3 V, aby wbudowane rezystory I2C także podciągały
linie do 3,3 V.

## Kompilacja i wgrywanie (Linux Mint oraz systemy oparte na Debianie)

```bash
sudo apt update
sudo apt install gcc-arm-none-eabi binutils-arm-none-eabi stlink-tools tio
sudo usermod -aG dialout "$USER"      # następnie wyloguj i zaloguj się ponownie

cd /path/to/JaszczurHAL
vscode/entry/jh-vscode build \
  --project examples/12_i2c_scan --target stm32g474

st-info --probe                       # sprawdź, czy ST-Link widzi G474
st-flash --reset write \
  .build/examples/12_i2c_scan/stm32g474/nucleo/firmware.bin 0x08000000
tio /dev/ttyACM0 -b 115200
```

Możesz też wgrać program przez OpenOCD:
`vscode/entry/jh-vscode upload --project examples/12_i2c_scan --target stm32g474`.

<a id="oczekiwane-wyjście"></a>

## Przykładowy wynik

Przy podłączonym PCF8563 pod adresem `0x51`:

```
=== JaszczurHAL I2C scanner ===
assembly nucleo: I2C bus 0 at 100000 Hz (external pull-ups to 3V3 required)
scanning 0x08..0x77 ...
  device @ 0x51
scanning 0x08..0x77 ...
  device @ 0x51
...
```

Wykryte adresy powinny odpowiadać podłączonym urządzeniom. Wynik pokazuje,
że urządzenie potwierdza adres podczas skanowania magistrali.

## Rozwiązywanie problemów

| Objaw | Co sprawdzić |
|---|---|
| `(no devices found)` po każdym skanowaniu | Zasilanie modułu, rezystory podciągające, połączenia SDA/SCL i zakres adresów. |
| Odpowiedź pod każdym adresem od `0x08` do `0x77` | SDA może być stale w stanie niskim, np. wskutek zwarcia lub braku podciągania. Nie traktuj takiego wyniku jako wykrycia urządzeń. |
| Brak danych w konsoli | Sprawdź port i prędkość transmisji. Po otwarciu terminala naciśnij RESET B2, aby zobaczyć komunikaty startowe. |
| `st-info --probe` nie wykrywa urządzenia | Sprawdź połączenie USB z CN1 ST-LINK. Komunikaty systemowe odczytasz przez `dmesg \| tail`; osobno sprawdź dostępność portu szeregowego. |
| Adres różni się od wartości w dokumentacji układu | Skaner pokazuje adresy 7-bitowe. Część dokumentacji podaje adres przesunięty o jeden bit, z miejscem na bit odczytu/zapisu. |

## Uwagi

Przykład używa magistrali 0, czyli I2C1. Implementacja STM32G474 obsługuje
również magistralę 1 (I2C2) po wybraniu zgodnej pary pinów SDA/SCL.
Oba kontrolery wybierają HSI16 jako źródło zegara. Ustawienia TIMINGR dla
16 MHz nie zależą wtedy od zmiany SYSCLK ani zegara APB.
