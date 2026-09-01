# 12 - Skaner I2C i sprzętowa weryfikacja STM32G474

Przykład sprawdza rzeczywistą implementację `hal_i2c` na Nucleo-G474RE: skanuje
magistralę I2C i wypisuje każdy adres, który odpowie sygnałem ACK. Pozwala w ten
sposób potwierdzić na sprzęcie działanie I2C1 jako kontrolera magistrali w
trybie bare metal.

- **I2C1**: SCL = **PB8**, SDA = **PB9** (złącze rozszerzeń NUCLEO: D15 = SCL, D14 = SDA)
- tryb standardowy 100 kHz; TIMINGR używa jawnie wybranego zegara HSI16 i
  pozostaje niezależny od SYSCLK/PCLK1
- używa `hal_i2c_scan()` i przekazuje `hal_watchdog_feed` jako funkcję zwrotną
  wywoływaną przy każdej próbie; formatowanie i powtarzanie skanowania co dwie
  sekundy pozostają w aplikacji
- konsola: USART2 / ST-Link Virtual COM Port, **115200 8N1**

Połączenie I2C1 zostało fizycznie sprawdzone na NUCLEO-G474RE z modułami
PCF8563 (`0x51`) i DS3231 (`0x68`). Skaner używa 100 kHz; to samo połączenie
PB9/PB8 jest sprawdzane przy 400 kHz przez `examples/16_rtc_backends`.

## Połączenia sprzętowe

```
Nucleo-G474RE                 Urządzenie I2C (np. PCF8563 RTC, AT24C256, BME280)
  PB8 (D15, SCL) ───┬──────── SCL
  PB9 (D14, SDA) ──┬┼──────── SDA
  3V3 ─────────────┼┼──[4.7k]─┘   (podciągnięcie SDA)
                   └─────[4.7k]──── 3V3   (podciągnięcie SCL)
  3V3 ──────────────────────────── VCC
  GND ──────────────────────────── GND
```

Zewnętrzne rezystory podciągające 2,2-10 kΩ do 3V3 są wymagane; wewnętrzne
podciągnięcia STM32 są zbyt słabe dla niezawodnego I2C. Wiele modułów ma je już
wbudowane.

Dla modułu DS3231 ze złączem `+ D C NC -` użyj:

| Pin modułu | Połącz z |
|---|---|
| `+` | `3V3` |
| `D` | `D14` / `PB9` / SDA |
| `C` | `D15` / `PB8` / SCL |
| `NC` | Pozostaw niepodłączony |
| `-` | `GND` |

Zasil moduł napięciem 3,3 V, aby jego rezystory podciągające I2C również były
podłączone do 3,3 V.

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
  .build/examples/12_i2c_scan/firmware.bin 0x08000000
tio /dev/ttyACM0 -b 115200
```

Alternatywa OpenOCD:
`vscode/entry/jh-vscode upload --project examples/12_i2c_scan --target stm32g474`.

## Oczekiwane wyjście

Przy podłączonym RTC PCF8563 pod adresem `0x51`:

```
=== JaszczurHAL G474 I2C scanner ===
I2C1: SCL=PB8, SDA=PB9 (external pull-ups to 3V3 required)
scanning 0x08..0x77 ...
  device @ 0x51
scanning 0x08..0x77 ...
  device @ 0x51
...
```

Wypisane adresy muszą odpowiadać podłączonym urządzeniom. Potwierdza to pełną
obsługę START / adres / ACK / STOP.

## Rozwiązywanie problemów

| Objaw | Prawdopodobna przyczyna |
|---|---|
| `(no devices found)` przy każdym skanowaniu | Brak lub zbyt słabe podciągnięcia; zamienione SDA/SCL; brak zasilania urządzenia; niewłaściwy zakres adresów |
| **Każdy** adres 0x08..0x77 zgłasza urządzenie | SDA zwarte do masy albo bez podciągnięcia; nie są to prawdziwe ACK |
| Brak danych w konsoli mimo zasilania | Zły port lub baud albo terminal otwarty przed resetem; naciśnij czarny RESET B2 |
| `st-info --probe` niczego nie znajduje | Użyj portu USB CN1 ST-LINK; sprawdź `dmesg \| tail` pod kątem `ttyACM0` |
| Znaleziony adres jest przesunięty o jeden | To adresy 7-bitowe; część dokumentacji układów podaje przesuniętą postać 8-bitową |

## Uwagi

- To stanowisko testowe sprawdza magistralę 0, czyli I2C1. Implementacja dla
  STM32G474 obsługuje też magistralę 1, czyli I2C2, z prawidłową parą
  alternatywnych funkcji SDA/SCL.
- I2C1 i I2C2 jawnie wybierają HSI16 jako źródło zegara. Ustawienia TIMINGR dla
  16 MHz pozostają poprawne po zmianie SYSCLK lub zegara APB.
