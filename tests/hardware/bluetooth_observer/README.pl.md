# Sprzętowy test Bluetooth Observer

`tests/hardware/bluetooth_observer` sprawdza pasywne API BLE Observer na
Raspberry Pi Pico W, Pico 2 W oraz STM32G474 Nucleo z PIM730/RM2. Uruchamia
pasywne skanowanie w starszym trybie, opróżnia kolejkę raportów o ograniczonej
pojemności, analizuje struktury AD i zapisuje dane producenta Teltonika oraz
sygnatury iBeacon i Eddystone bez inicjowania połączenia BLE.

Skompiluj i wgraj każdą płytkę osobno:

```bash
vscode/entry/jh-vscode build \
  --project tests/hardware/bluetooth_observer \
  --target rp2040 --board picow

vscode/entry/jh-vscode build \
  --project tests/hardware/bluetooth_observer \
  --target rp2350-arm --board pico2w

vscode/entry/jh-vscode build \
  --project tests/hardware/bluetooth_observer \
  --target stm32g474 --board nucleo-g474re-pim730
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
