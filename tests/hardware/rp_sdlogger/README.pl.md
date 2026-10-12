# Sprzętowy test RP SDLogger

`tests/hardware/rp_sdlogger` sprawdza wspólną implementację SDLogger z fizyczną
kartą SD podłączoną przez SPI. Montuje kartę, otwiera log numerowany na
podstawie EEPROM, dopisuje deterministyczną zawartość, opróżnia bufor i zamyka
plik, wywołuje reset przez watchdog, ponownie montuje kartę, sprawdza dokładny
dopisany fragment na końcu pliku oraz potwierdza, że licznik logów w EEPROM
został zachowany.

Plik `device_tree.json` opisuje połączenia każdego montażu: `pico` dla Pico
oraz `pico2-arm` i `pico2-riscv` dla Pico 2. Karta to węzeł `sd` na magistrali
SPI0 z zegarem 12 MHz, którym sterownik SD przesyła dane. Program bierze
magistralę, jej piny i linię CS karty z wygenerowanego nagłówka, a poniższa
tabela powtarza te połączenia.

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
  --assembly pico
vscode/entry/jh-vscode upload \
  --project tests/hardware/rp_sdlogger \
  --assembly pico \
  --port /dev/serial/by-id/<device>
python3 tests/hardware/rp_sdlogger/verify_sdlogger.py \
  --port /dev/serial/by-id/<device> \
  --target rp2040 --board pico --runtime baremetal
```

Dla Pico 2 zbuduj firmware z `--assembly pico2-arm` albo
`--assembly pico2-riscv`, a weryfikatorowi przekaż `--target rp2350-arm` albo
`--target rp2350-riscv` razem z `--board pico2`. Dodaj `--variant FREERTOS`
do komend kompilacji i wgrywania oraz użyj `--runtime freertos` dla przebiegu
FreeRTOS.

Weryfikator jest powtarzalny bez formatowania karty. Jeśli istnieje stary
plik logu o tej samej nazwie, weryfikuje on nowo dopisany deterministyczny
fragment końcowy.
