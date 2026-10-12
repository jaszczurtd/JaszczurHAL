# Sprzętowy test Bluetooth Observer

`tests/hardware/bluetooth_observer` sprawdza pasywne API BLE Observer na
Raspberry Pi Pico W, Pico 2 W oraz STM32G474 Nucleo z PIM730/RM2. Uruchamia
pasywne skanowanie w starszym trybie, opróżnia kolejkę raportów o ograniczonej
pojemności, analizuje struktury AD i zapisuje dane producenta Teltonika oraz
sygnatury iBeacon i Eddystone bez inicjowania połączenia BLE.

`device_tree.json` opisuje sprzęt każdego montażu: `picow` (Pico W),
`pico2w` (Pico 2 W) i `nucleo-pim730` (domyślny). `nucleo-pim730` to zwykła
NUCLEO-G474RE z zewnętrznym modułem PIM730/RM2; tabela poniżej powtarza
połączenia modułu.

| Sygnał PIM730/RM2 | NUCLEO-G474RE |
|---|---|
| CS | PB12, pin 16 CN10 |
| DAT | PB15, pin 26 CN10 |
| WL_ON | PB14, pin 28 CN10 |
| CLK | PB13, pin 30 CN10 |

Skompiluj i wgraj każdy montaż osobno:

```bash
vscode/entry/jh-vscode build \
  --project tests/hardware/bluetooth_observer --assembly picow

vscode/entry/jh-vscode build \
  --project tests/hardware/bluetooth_observer --assembly pico2w

vscode/entry/jh-vscode build \
  --project tests/hardware/bluetooth_observer --assembly nucleo-pim730
```

Udane wyjście używa prefiksu `JHBL4A`. Zarejestruj co najmniej jeden raport
Teltonika EYE Beacon na każdej płytce, całkowite i odrzucone liczniki
raportów oraz podsumowanie pamięci ELF/map. Test pozostaje pasywny:
odpowiedzi skanowania, klient GATT, połączenia, parowanie i bonding są poza
zakresem tego testu.

Komendy `STOP`, `START`, `REOPEN` i `INFO` sprawdzają ponowne uruchomienie
skanowania, pełne ponowne uzyskanie profilu BLE bez resetu kontrolera oraz
ograniczoną diagnostykę. Poprawny raport musi nadejść zarówno po `START`, jak i
po `REOPEN`.

RP2350 RISC-V jest nieobsługiwane, ponieważ jego transport Bluetooth CYW43
nie jest włączony.
