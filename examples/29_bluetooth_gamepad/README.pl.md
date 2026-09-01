# 29 - Gamepad Bluetooth

Jest to przykład gamepada Bluetooth Classic HID korzystający z publicznego,
niezależnego od stosu API odczytu stanu. Bazowy obraz uruchamia tylko profil
Classic. Wariant `ble` dodatkowo inicjalizuje i odpytuje BLE, aby skompilować
oraz uruchomić oba profile z użyciem wspólnej obsługi kontrolera.

## Kompilacja i uruchomienie

```bash
./scripts/examples_dispatcher.py build --target rp2040 \
  --example 29_bluetooth_gamepad
./scripts/examples_dispatcher.py build --target rp2350-arm \
  --example 29_bluetooth_gamepad
./scripts/examples_dispatcher.py build --target stm32g474 \
  --example 29_bluetooth_gamepad
```

Domyślne płytki to RP2040 `picow`, RP2350 ARM `pico2w` i STM32G474
`nucleo-g474re-pim730`. RP2350 RISC-V nie jest obsługiwany, ponieważ transport
Bluetooth CYW43 nie jest dla niego włączony. Implementacja Bluedroid
oryginalnego ESP32 ma osobne stanowisko do testowania kompilacji i linkowania w
`tests/fixtures/esp32_gamepad`; nie przeszła jeszcze sprzętowego testu
łączności radiowej.

Tylko wariant łączący BLE i Classic można zbudować poleceniem:

```bash
vscode/entry/jh-vscode build \
  --project examples/29_bluetooth_gamepad \
  --target rp2040 --board picow --variant ble
```

Przy pierwszym uruchomieniu włącz tryb parowania gamepada. Przykład otwiera
okno wykrywania o ograniczonym czasie i zatwierdza oczekujące żądanie Just Works
albo starszy PIN `0000`. Jeśli okno wygaśnie bez wybrania urządzenia, przykład
otwiera nowe. Zaakceptowany adres może służyć do ponownego łączenia do czasu zamknięcia
profilu. Po zamknięciu profilu lub restarcie firmware aplikacja musi ponownie
otworzyć parowanie; trwały wybór urządzenia przez HAL nie wchodzi w zakres tego
wydania.

## Model stanu wejścia

`hal_gamepad_snapshot_next()` zwraca kolejne zmiany wejść bez udostępniania typów
BTstack. Bit przycisku 0 oznacza HID Button 1, bit 1 oznacza HID Button 2 itd.
Osie zgłaszane przez kontroler używają indeksów `HAL_GAMEPAD_AXIS_*` i są
normalizowane do zakresu `-32767..32767`. D-pad jest maską kierunków
`HAL_GAMEPAD_DPAD_*`.

Kolejka ma stałą pojemność. `HAL_EOVERFLOW` sygnalizuje utratę stanów
pośrednich; kod wywołujący kontynuuje odbiór, aby otrzymać najnowszy zachowany
stan. Rekordy stanu generowane przy połączeniu i rozłączeniu ustawiają lub
zerują wszystkie wejścia, więc aplikacja nie zachowa wciśniętego przycisku po
utracie połączenia.

Przykład pokazuje inicjalizację po uruchomieniu planisty, parowanie,
autoryzację, ponowne łączenie, diagnostykę stanów, obsługę przepełnienia oraz
opróżnianie kolejki stanów. Wariant `ble` celowo nie ogłasza usługi BLE; jedynie
potwierdza, że oba publiczne profile uzyskują dostęp do tej samej instancji hosta
CYW43/BTstack, odpytują ją i zwalniają. Nie jest dostępny na żadnym targecie
ESP: ESP32-S3 udostępnia bazowe BLE bez Classic HID, a
oryginalny ESP32 udostępnia gamepad Classic bez publicznego API BLE.
