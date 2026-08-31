# 29 - Gamepad Bluetooth

Przykład gamepada Bluetooth Classic HID używający publicznego API snapshotów,
niezależnego od stosu. Bazowy obraz uruchamia tylko profil Classic. Wariant
`ble` dodatkowo inicjalizuje i odpytuje BLE, aby skompilować oraz uruchomić oba
profile na współdzielonym środowisku kontrolera.

## Build i uruchomienie

```bash
./scripts/examples_dispatcher.py build --target rp2040 \
  --example 29_bluetooth_gamepad
./scripts/examples_dispatcher.py build --target rp2350-arm \
  --example 29_bluetooth_gamepad
./scripts/examples_dispatcher.py build --target stm32g474 \
  --example 29_bluetooth_gamepad
```

Domyślne boardy to RP2040 `picow`, RP2350 ARM `pico2w` i STM32G474
`nucleo-g474re-pim730`. RP2350 RISC-V nie jest wspierany, ponieważ transport
Bluetooth CYW43 nie jest dla niego włączony. Backend Bluedroid oryginalnego
ESP32 ma osobne stanowisko buildu i linkowania w
`tests/fixtures/esp32_gamepad`; nie przeszedł jeszcze bramki radiowej na
sprzęcie.

Tylko wariant łączący BLE i Classic można zbudować poleceniem:

```bash
vscode/entry/jh-vscode build \
  --project examples/29_bluetooth_gamepad \
  --target rp2040 --board picow --variant ble
```

Przy pierwszym uruchomieniu włącz tryb parowania gamepada. Przykład otwiera
ograniczone okno wykrywania i zatwierdza oczekujące żądanie Just Works albo
legacy PIN `0000`. Zaakceptowany adres może być użyty do reconnectu podczas
tego samego uruchomienia firmware. Aplikacja musi być gotowa ponownie otworzyć
parowanie po restarcie; trwały wybór urządzenia przez HAL nie wchodzi w zakres
tego wydania.

## Model snapshotu

`hal_gamepad_snapshot_next()` zwraca zmiany wejść bez udostępniania typów
BTstack. Bit przycisku 0 oznacza HID Button 1, bit 1 oznacza HID Button 2 itd.
Obecne osie używają indeksów `HAL_GAMEPAD_AXIS_*` i są normalizowane do zakresu
`-32767..32767`. D-pad jest maską kierunków `HAL_GAMEPAD_DPAD_*`.

Kolejka ma stały rozmiar. `HAL_EOVERFLOW` potwierdza utratę stanów pośrednich;
wywołujący kontynuuje odbiór, aby dostać najnowszy zachowany stan. Snapshoty
połączenia i rozłączenia ustawiają lub zerują wszystkie wejścia, więc aplikacja
nie zachowa wciśniętego przycisku po utracie linku.

Przykład pokazuje inicjalizację po uruchomieniu schedulera, parowanie,
autoryzację, reconnect, diagnostykę stanów, obsługę przepełnienia oraz odbiór
snapshotów. Wariant `ble` celowo nie ogłasza usługi BLE; jedynie potwierdza, że
oba publiczne profile używają tego samego hosta CYW43/BTstack. Nie jest dostępny
na żadnym targetcie ESP: ESP32-S3 udostępnia bazowe BLE bez Classic HID, a
oryginalny ESP32 udostępnia gamepad Classic bez publicznego API BLE.
