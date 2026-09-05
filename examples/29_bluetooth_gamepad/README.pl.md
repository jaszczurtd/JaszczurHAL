# 29 - Gamepad Bluetooth

Projekt pokazuje trzy publiczne warstwy Bluetooth Classic. Bazowy obraz używa
niezależnego od stosu adaptera gamepada ze znormalizowanym stanem. Wariant
`classic-scan` buduje tylko manager i wypisuje skopiowane wyniki inquiry/SDP.
Wariant `hid-host` łączy się z dowolną wykrytą usługą HID i udostępnia
skopiowany deskryptor oraz surowe raporty bez założeń o gamepadzie. Wariant
`ble` dodaje BLE do obrazu gamepada, aby sprawdzić wspólny runtime kontrolera.

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

Izolowane warstwy publiczne albo wariant BLE + Classic można zbudować tak:

```bash
vscode/entry/jh-vscode build \
  --project examples/29_bluetooth_gamepad \
  --target rp2040 --board picow --variant classic-scan
vscode/entry/jh-vscode build \
  --project examples/29_bluetooth_gamepad \
  --target rp2040 --board picow --variant hid-host
vscode/entry/jh-vscode build \
  --project examples/29_bluetooth_gamepad \
  --target rp2040 --board picow --variant ble
```

Konsola szeregowa wariantu `classic-scan` nadaje każdemu wykrytemu adresowi
ulotny indeks i nie wypisuje samego adresu. Po uruchomieniu wykonuje jedno
inquiry, a po jego zakończeniu kolejno obsługuje oczekujące zapytania SDP.
Dostępne polecenia:

- `SCAN` i `STOP` sterują dziesięciosekundowym oknem inquiry;
- `SDP n` powtarza wykrywanie usług peera `n`;
- `PAIR n`, a następnie `AUTHORIZE` albo `REJECT`, realizuje jawną lokalną
  decyzję o parowaniu;
- `SAVE n` publikuje uwierzytelnionego peera po walidacji właściwej dla
  aplikacji, a `FORGET n` go usuwa;
- `INFO` wypisuje stan, pairing, liczniki ograniczonej kolejki i liczbę
  peerów.

Przykład otwiera manager bez trwałego providera, dlatego zapisani peerzy
pozostają ważni tylko do restartu. Aplikacja produkcyjna musi zastąpić
szeregowe polecenie `AUTHORIZE` zaufanym lokalnym gestem i wywoływać `SAVE`
dopiero po zweryfikowaniu peera przez swój profil.

Przy pierwszym uruchomieniu włącz sprzętowo zweryfikowany 8BitDo Zero 2 model
80EH w trybie Android D-input przez `B+Start`, a następnie przytrzymaj `Select`,
aż dioda parowania zacznie migać. Przykład otwiera okno wykrywania o ograniczonym
czasie i zatwierdza oczekujące żądanie Just Works albo starszy PIN `0000`. Jeśli
okno wygaśnie bez wybrania urządzenia, przykład otwiera nowe. Po zapisaniu bondu
używaj do reconnectu zwykłego włączenia przyciskiem `Start`; nie przełączaj pada
ponownie w tryb parowania. Provider przekazany do `hal_gamepad_open_ex()`
zachowuje zaakceptowany adres po restarcie; zgodnościowy provider gamepada jest
jednoslotowym adapterem indeksowanego managera Classic. Pełną bramkę sprzętową
gamepada zaliczyła wyłącznie ta kombinacja kontrolera, trybu i hosta
`rp2350-arm:pico2w`; inne kombinacje wymagają osobnej walidacji.

Ogólny wariant HID celowo odrzuca parowanie, dopóki
`localPairingConsent()` nie zostanie połączone z zaufanym lokalnym gestem. Po
lokalnej autoryzacji, skopiowaniu deskryptora i otrzymaniu raportu Input prosi
manager Classic o zapis peera. Przykład jest więc domyślnie bezpieczny, a
jednocześnie pokazuje pełną granicę polityki.

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

Przykład bazowy pokazuje inicjalizację po uruchomieniu planisty, parowanie,
autoryzację, ponowne łączenie, diagnostykę stanów, obsługę przepełnienia oraz
opróżnianie kolejki stanów. Wariant `ble` dodaje pasywny Observer BLE do profilu
gamepada Classic. Podczas startu zwalnia i ponownie uzyskuje każdy profil,
podczas gdy drugi utrzymuje wspólny host CYW43/BTstack. Polecenia `INFO`,
`BLE_START`, `BLE_STOP` i `DISCONNECT` sprawdzają równoczesne skanowanie oraz
ponowne łączenie HID. Okresowa diagnostyka podaje użycie stosu, high-water i
błędy alokacji pul HCI/L2CAP/link-key/HID oraz ruch transportu HCI i trafienia
limitu drain. Buildy RP rezerwują zmierzony stos core 0 o rozmiarze 4 KiB dla
tej rozbudowanej ścieżki diagnostycznej. Wariant używa klucza KV `0xd001`, więc
jego bond gamepada pozostaje zgodny z obrazem regresji sprzętowej doomConsole.
Nie ogłasza usługi BLE.

Implementację Classic/HID dla oryginalnego ESP32 obejmuje osobny test
kompilacji i linkowania ESP-IDF; dispatcher natywnych przykładów nie obsługuje
jeszcze targetów ESP.
