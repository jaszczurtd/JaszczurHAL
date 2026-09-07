<a id="29---gamepad-bluetooth"></a>

# 29 - Obsługa gamepada i urządzeń HID przez Bluetooth

Przykład odczytuje przyciski, osie i kierunki D-pada z gamepada Bluetooth
Classic. Zwraca je we wspólnym formacie, bez udostępniania aplikacji typów
BTstack. Dodatkowe warianty pozwalają wykrywać urządzenia i usługi Classic,
odczytywać surowe raporty HID albo skanować BLE podczas pracy gamepada.

| Wariant | Działanie |
|---|---|
| Podstawowy | Łączy gamepad, odczytuje stan wejść i zapisuje zaakceptowane urządzenie do ponownego połączenia po restarcie. |
| `classic-scan` | Wykrywa urządzenia Classic i ich usługi za pomocą inquiry oraz SDP. |
| `hid-host` | Łączy się z wykrytą usługą HID, kopiuje deskryptor i odbiera surowe raporty, bez interpretowania ich jako stanu gamepada. |
| `ble` | Dodaje pasywne skanowanie BLE do obsługi gamepada Classic na wspólnym kontrolerze CYW43. |

## Kompilacja i uruchomienie

Uruchom z głównego katalogu repozytorium:

```bash
./scripts/examples_dispatcher.py build --target rp2040 \
  --example 29_bluetooth_gamepad
./scripts/examples_dispatcher.py build --target rp2350-arm \
  --example 29_bluetooth_gamepad
./scripts/examples_dispatcher.py build --target stm32g474 \
  --example 29_bluetooth_gamepad
```

Domyślne płytki to `picow` dla RP2040, `pico2w` dla RP2350 ARM oraz
`nucleo-g474re-pim730` dla STM32G474. RP2350 RISC-V nie jest obsługiwany,
ponieważ nie włączono dla niego komunikacji Bluetooth przez CYW43.

Wskazany wariant można zbudować osobno:

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

Implementacja oparta na Bluedroid dla oryginalnego ESP32 ma osobny projekt
sprawdzający kompilację i linkowanie: `tests/fixtures/esp32_gamepad`.
Nie potwierdzono jeszcze jej działania radiowego na sprzęcie. Wspólny
skrypt kompilacji przykładów nie obsługuje platform ESP.

## Parowanie gamepada

Sprzętowe testy całego przykładu wykonano dla 8BitDo Zero 2, model 80EH,
w trybie Android D-input, połączonego z `rp2350-arm:pico2w`. Pozostałe
modele, tryby i płytki wymagają osobnych testów.

Przy pierwszym uruchomieniu włącz gamepad kombinacją `B+Start`, a następnie
przytrzymaj `Select`, aż dioda parowania zacznie migać. Przykład otwiera
ograniczone czasowo okno wykrywania i automatycznie zatwierdza oczekujące
żądanie Just Works albo starszego parowania z PIN-em `0000`. Jeśli nie
wybierze urządzenia przed upływem tego czasu, otwiera kolejne okno.
**Automatyczna zgoda jest ustawieniem przykładu, a nie gotową polityką
bezpiecznego parowania dla produktu.**

Po zapisaniu urządzenia włączaj gamepad zwykłym naciśnięciem `Start`.
Nie uruchamiaj ponownie trybu parowania, gdy chcesz jedynie wznowić połączenie.
Funkcje zapisu przekazane do `hal_gamepad_open_ex()` zachowują zaakceptowane
urządzenie w pamięci nieulotnej. Interfejs zgodności gamepada udostępnia
jedno miejsce na taki zapis, korzystając z indeksowanej listy urządzeń
Bluetooth Classic. Klucz KV `0xd001` zachowuje zgodność z zapisem używanym
przez obraz doomConsole do testów regresji na sprzęcie.

## Wykrywanie urządzeń i usług

Wariant `classic-scan` po starcie wykonuje dziesięciosekundowe wyszukiwanie
inquiry. Po jego zakończeniu kolejno odpytuje urządzenia o usługi przez SDP.
Każdemu wykrytemu urządzeniu nadaje tymczasowy indeks `n`; nie wypisuje
adresów Bluetooth.

| Polecenie | Działanie |
|---|---|
| `SCAN`, `STOP` | Rozpoczynają lub kończą okno wyszukiwania. |
| `SDP n` | Ponawia wykrywanie usług urządzenia `n`. |
| `PAIR n` | Rozpoczyna parowanie z urządzeniem `n`. |
| `AUTHORIZE`, `REJECT` | Zatwierdzają lub odrzucają oczekujące żądanie parowania. |
| `SAVE n`, `FORGET n` | Zlecają zapis lub usunięcie urządzenia `n`. |
| `INFO` | Wyświetla stan, informacje o parowaniu, liczniki kolejki i liczbę urządzeń. |

Ten wariant nie podłącza funkcji trwałego zapisu: zapisane urządzenia są
pamiętane tylko do restartu. Polecenie `AUTHORIZE` służy do ręcznych testów.
W docelowej aplikacji powiąż zgodę z zaufaną lokalną czynnością użytkownika,
a zapis urządzenia dopuść dopiero po sprawdzeniu go zgodnie z wymaganiami
profilu. Samo wykrycie urządzenia ani wywołanie `SAVE` nie zastępuje tej oceny.

## Odbiór surowych raportów HID

Wariant `hid-host` wybiera wykrytą usługę HID i udostępnia aplikacji kopię
deskryptora oraz surowe raporty. Żądanie parowania pozostaje do decyzji
operatora: konsola udostępnia `AUTHORIZE` i `REJECT`. Dodatkowe polecenia to
`SCAN` i `INFO`. Samo wykrycie usługi nie zatwierdza parowania; w tej implementacji zgodę
wydaje się poleceniem konsoli.

Przykład prosi o zapis urządzenia dopiero po autoryzacji, skopiowaniu
deskryptora i otrzymaniu raportu Input. Nie konfiguruje trwałego zapisu.
Przed wykorzystaniem w produkcie zastąp testową autoryzację z konsoli
zaufanym mechanizmem zgody i określ, jakie deskryptory oraz raporty aplikacja
może zaakceptować.

## Model stanu wejścia

`hal_gamepad_snapshot_next()` zwraca kolejne zmiany stanu wejść. Bit 0
przycisków oznacza HID Button 1, bit 1 oznacza HID Button 2 itd. Osie używają
indeksów `HAL_GAMEPAD_AXIS_*` i zakresu `-32767..32767`. D-pad jest maską
kierunków `HAL_GAMEPAD_DPAD_*`.

Kolejka ma stałą pojemność. `HAL_EOVERFLOW` oznacza utratę stanów pośrednich;
aplikacja powinna kontynuować odczyt, aby dotrzeć do najnowszego zachowanego
stanu. Rekordy połączenia i rozłączenia określają stan wszystkich wejść,
w tym zerują go po utracie połączenia. Dzięki temu aplikacja nie pozostawia
przycisku w stanie wciśniętym po odłączeniu gamepada.

Inicjalizacja przy włączonym FreeRTOS następuje po uruchomieniu schedulera.
Przykład pokazuje parowanie, autoryzację, ponowne łączenie, odbieranie zmian
stanu oraz obsługę przepełnienia kolejki.

## Równoczesna obsługa BLE i Classic

Wariant `ble` skanuje pasywnie jako BLE Observer; nie ogłasza usługi BLE.
Podczas startu zamyka i ponownie otwiera każdy profil, gdy drugi nadal
korzysta ze wspólnego kontrolera CYW43 i stosu BTstack. Polecenia `INFO`,
`BLE_START`, `BLE_STOP` oraz `DISCONNECT` służą do sprawdzenia skanowania
BLE podczas pracy gamepada i ponownego łączenia HID.

Diagnostyka podaje użycie stosu, maksymalne wykorzystanie pul
HCI/L2CAP/link-key/HID, nieudane przydziały pamięci i ruch HCI. Liczy też
przypadki osiągnięcia limitu liczby zdarzeń obsługiwanych w jednym przebiegu.
Konfiguracje RP rezerwują 4 KiB stosu rdzenia 0, zgodnie z pomiarami dla
rozbudowanej diagnostyki tego przykładu.
