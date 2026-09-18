# Sprzętowy test gamepada Bluetooth Classic HID

`tests/hardware/bluetooth_gamepad` zawiera wewnętrzny test hosta Classic HID,
opracowany przed publicznym API. Zanonimizowane dane testowe
`tests/fixtures/bluetooth_gamepad/zero2_android_dinput.json` obejmują
137-bajtowy deskryptor raportu, tożsamość PnP, metadane SDP, wszystkie dwanaście
stanów wejściowych, niedeklarowany końcowy bajt wejścia i powtórzony surowy
raport. Nie zawiera adresów Bluetooth, kluczy połączeń, tożsamości hosta ani
numerów seryjnych USB.

Firmware inicjalizuje wspólne środowisko wykonawcze HCI/L2CAP, ulotną bazę na
jeden klucz połączenia, klienta SDP, jedno połączenie HID Host i jedną funkcję
obsługi zdarzeń. Wyszukiwanie rozpoczyna się wyłącznie po odebraniu przez port
szeregowy polecenia `DISCOVER`. Kończy się po 120 sekundach albo po
zaakceptowaniu pierwszego zgodnego urządzenia. Urządzenie musi mieć klasę
peryferyjną, zapisaną nazwę, usługę Classic HID i zapisaną tożsamość PnP.
Wewnętrznego mechanizmu wyboru nie wolno łączyć z publicznym BLE ani z
wcześniejszym testem etapu 1.

Wewnętrzny parser korzysta z deskryptora raportów HID, a nie ze stałych
offsetów bajtów kontrolera Zero 2. Obsługuje kolekcje aplikacyjne Generic
Desktop Game Pad i Joystick. Normalizuje maksymalnie 32 przyciski, dziewięć osi
pulpitu oraz jeden przełącznik kierunkowy do rekordów stanu przechowywanych w
pamięci o stałym rozmiarze. Limity C6 wynoszą 256 bajtów deskryptora, 32 bajty
raportu wejściowego i 16 rekordów w kolejce. Nieznane zastosowania HID są
ignorowane. Ograniczona diagnostyka sygnalizuje nieprawidłowe lub zbyt duże
deskryptory, skrócone lub zbyt duże raporty, nieznane identyfikatory raportów,
powtórzone mapowania zastosowań oraz przepełnienie kolejki. Ponowny raport,
który nie zmienia stanu, nie dodaje do kolejki kolejnego rekordu.

`test_bluetooth_gamepad_parser` używa zanonimizowanych danych niezależnie od
testu sprzętowego. Obejmuje zapisane raporty, układy pól wynikające z deskryptora,
wielokrotne podanie tego samego stanu wejścia, czyszczenie stanu po ponownym
połączeniu, nieprawidłowe i skrócone dane, nieznane identyfikatory raportów i
zastosowania HID, powtórzone zastosowania,
przepełnienie kolejki oraz brak alokacji dynamicznej podczas działania
parsera.

Zbuduj wymagany obraz Pico 2 W:

```sh
vscode/entry/jh-vscode build \
  --project tests/hardware/bluetooth_gamepad \
  --target rp2350-arm --board pico2w --variant classic-hid
```

Wgraj go i uruchom weryfikator sprzętowy na powstałym porcie CDC:

```sh
vscode/entry/jh-vscode upload \
  --project tests/hardware/bluetooth_gamepad \
  --target rp2350-arm --board pico2w --variant classic-hid \
  --port /dev/ttyACM0

python3 tests/hardware/bluetooth_gamepad/verify_zero2.py \
  --port /dev/ttyACM0
```

Po pojawieniu się komunikatu uruchom Zero 2 w trybie Android D-input przez
`B+Start`, a następnie przytrzymaj `Select`, aż dioda parowania zacznie migać.
Weryfikator zatwierdza metodę parowania zgłoszoną przez kontroler, sprawdza
zapisany deskryptor i wszystkie wejścia, wykonuje rozłączenia/ponowne
połączenia oraz pełne wyłączenie i ponowne włączenie zasilania, a następnie
utrzymuje ciągłe połączenie przez 30 minut. Podczas pierwszego ponownego
łączenia prosi o przytrzymanie jednego elementu sterującego, aby procedura
rozłączenia mogła zwolnić aktywny stan.

Weryfikator zapisuje `zero2_pico2w_c6_result.json`; wcześniejszy wynik C5
pozostaje punktem odniesienia sprzed integracji parsera. Raport C6 zawiera
wersje targetu i bibliotek, czasy, liczniki transportu, diagnostykę parsera
oraz maksymalne zajęcie pul. Nie może zawierać adresów Bluetooth, materiału
link key, tożsamości hosta, nazwy portu szeregowego ani numeru seryjnego USB.
Pliki ELF/map i lista symboli muszą również potwierdzać obecność HID Host
`ENABLE_CLASSIC`, klienta SDP, parsera HID oraz przechowywanej w pamięci bazy
kluczy połączeń, a także brak ATT, GATT, SM, RFCOMM, serwera SDP, HID Device i
profili audio.
