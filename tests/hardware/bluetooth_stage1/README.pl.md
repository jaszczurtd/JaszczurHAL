# Sprzętowy test Bluetooth - etap 1

`tests/hardware/bluetooth_stage1` to wewnętrzny test integracji CYW43/BTstack,
opracowany jeszcze przed publicznym API. Macierz kompilacji obejmuje STM32G474 Nucleo + PIM730,
Raspberry Pi Pico W oraz RP2350 ARM Pico 2 W. Celowo nie włącza żadnego
publicznego makra włączającego Bluetooth i nie może służyć jako
przykład publicznego API aplikacji.

Testy sprzętowe tej sondy obejmują STM32G474 + PIM730 oraz Pico W. Pico 2 W
sprawdzają zamiast niej publiczne stanowiska
[Bluetooth Observer](../bluetooth_observer/README.pl.md) oraz
[BLE Stream](../bluetooth_stream/README.pl.md). RP2350 RISC-V jest
nieobsługiwane, ponieważ jego transport Bluetooth CYW43 nie jest włączony.

Projekt kompiluje źródła BTstack bezpośrednio i nie linkuje
`pico_cyw43_arch`, `pico_btstack_cyw43` ani integracji pamięci masowej
Bluetooth z Pico SDK. Uruchamia wspólną instancję JH zarządzającą radiem CYW43
przez interfejs BLE i za jej pośrednictwem pobiera firmware Bluetooth.
Następnie rozpoczyna rozgłaszanie z możliwością nawiązania połączenia pod nazwą
`JH BLE Stage 1` oraz udostępnia niewielką, statyczną charakterystykę GATT do
odczytu i zapisu.

Udana kompilacja potwierdza zbudowanie firmware, nie jego działanie na urządzeniu. Wyniki testu
sprzętowego muszą
rejestrować wyjście `JHBT1`, zachowanie połączenia/zapisu, użycie pamięci
ELF/map oraz dokładną płytkę/okablowanie testowane. Przebieg STM32
dodatkowo sprawdza, czy na zmontowanym stanowisku sygnał `BT_ON` modułu PIM730
nadal jest połączony z `WL_ON`.

Wariant `bluetooth` służy do właściwego testu, a `wifi-only` jest równoważnym
punktem odniesienia dla pomiaru pamięci. Oba warianty należy mierzyć na
podstawie plików ELF/map, przy tym samym układzie docelowym, tej samej płytce,
wersji kompilatora i konfiguracji kompilacji.

Obraz `wifi-only` nie zawiera BTstack, firmware Bluetooth ani pul Bluetooth
na współdzielonej magistrali.

Zbuduj sondę dla każdej płytki, a następnie powtórz kompilację z
`--variant wifi-only`, aby uzyskać punkt odniesienia:

```sh
vscode/entry/jh-vscode build \
  --project tests/hardware/bluetooth_stage1 \
  --target stm32g474 --board nucleo-g474re-pim730 --variant bluetooth
vscode/entry/jh-vscode build \
  --project tests/hardware/bluetooth_stage1 \
  --target rp2040 --board picow --variant bluetooth
```

## Podetap sprzętowy 1.a - okablowanie i procedura

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
