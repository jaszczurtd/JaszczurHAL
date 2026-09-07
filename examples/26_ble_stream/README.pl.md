<a id="26---ble-stream"></a>

# 26 - Wymiana danych i poleceń przez BLE

Przykład udostępnia usługę JH BLE Stream v1 i wymienia dane z klientem po
obustronnym uwierzytelnieniu. Płytka działa jako BLE Peripheral: ogłasza
swoją obecność i przyjmuje połączenie od urządzenia Central.

Wersja podstawowa jest widoczna jako `JH Stream`. Każdy klient może odczytać
wersję protokołu i maskę obsługiwanych funkcji. Dane aplikacji są dostępne
dopiero po potwierdzeniu znajomości sekretu urządzenia. Po uwierzytelnieniu
aplikacja co sekundę wysyła linię telemetrii i wypisuje otrzymane dane
w konsoli. Gdy nadawanie jest chwilowo niemożliwe, zachowuje najwyżej jedną
próbkę do ponownej próby.

Warianty `commands` i `commands-freertos` są widoczne jako `JH Commands`.
Wymieniają z uwierzytelnionym klientem żądania, odpowiedzi i zdarzenia
binarnego protokołu poleceń. Dane Stream obsługuje wtedy wyłącznie
`hal_ble_commands`, a procedury wykonujące polecenia nie zależą od sposobu
ich przesyłania.

## Kompilacja i uruchomienie

Uruchom z głównego katalogu repozytorium:

```bash
./scripts/examples_dispatcher.py build --target rp2040 --example 26_ble_stream
./scripts/examples_dispatcher.py build --target rp2350-arm --example 26_ble_stream
./scripts/examples_dispatcher.py build --target stm32g474 --example 26_ble_stream
```

Dla tego projektu skrypt kompiluje wersję podstawową i oba warianty poleceń.
Aby zbudować tylko wskazany wariant, użyj:

```bash
vscode/entry/jh-vscode build --project examples/26_ble_stream \
  --target rp2040 --board picow --variant commands
vscode/entry/jh-vscode build --project examples/26_ble_stream \
  --target rp2040 --board picow --variant commands-freertos
```

Domyślne płytki to `picow` dla RP2040, `pico2w` dla RP2350 ARM oraz
`nucleo-g474re-pim730` dla STM32G474. Można też jawnie wybrać `pico-rm2`
dla RP2040; konfiguracja kompilacji jest dostępna, ale jej osobny test
sprzętowy pozostaje do wykonania:

```bash
vscode/entry/jh-vscode build \
  --project examples/26_ble_stream \
  --target rp2040 --board pico-rm2
```

RP2350 RISC-V nie jest obsługiwany, ponieważ nie włączono dla niego
komunikacji Bluetooth przez CYW43.

Inicjalizacja CYW43/BLE następuje przy pierwszym wywołaniu `app_task0()`.
Przy włączonym FreeRTOS oznacza to uruchomienie jej po starcie schedulera.
Konfiguracja rezerwuje wtedy 1024 słowa stosu zadania; domyślne 512 słów
nie wystarczało do uzgodnienia uwierzytelnionej sesji na sprzęcie RP.

## Przygotowanie sekretu

`kDeviceSecret` w [`app.cpp`](app.cpp) jest wartością przykładową. W docelowym
urządzeniu zastąp ją indywidualnym sekretem o długości co najmniej 256 bitów.
Przekaż go klientowi innym kanałem, np. przez kod QR na etykiecie lub
uwierzytelnione połączenie USB. Nie używaj tego samego sekretu we wszystkich
urządzeniach.

`hal_ble_stream_set_secret()` ustawia sekret, a
`hal_ble_stream_clear_secret()` go usuwa, np. podczas przywracania ustawień
fabrycznych. Ustawienie nowego sekretu unieważnia sesję opartą na poprzednim.

## Strona klienta

Klient wysyła `HELLO`, sprawdza dowód znajomości sekretu otrzymany
w `HELLO_ACK` i odpowiada komunikatem `AUTH`. Dowody obu stron oraz osobne
klucze dla każdego kierunku transmisji są wyliczane za pomocą HMAC-SHA256.
Obliczenia obejmują nazwę profilu, wersję protokołu, funkcje obu stron,
identyfikator sesji i obie wartości nonce. Ramki `DATA` są zabezpieczane
ChaCha20-Poly1305 i mają oddzielne liczniki dla obu kierunków.
Format ramek i stałe definiuje
[`hal_ble_stream.h`](../../src/hal/bluetooth/hal_ble_stream.h).

Przed uzgadnianiem sesji ATT MTU musi osiągnąć
`HAL_BLE_STREAM_MIN_ATT_MTU`, aby komunikaty mieściły się w wymaganym
zapisie. Przykład wypisuje uzgodnione MTU w diagnostyce.

Klientem wariantów poleceń jest Linux z BlueZ w roli Central. JaszczurHAL
w opisanej integracji udostępnia Peripheral i pasywny Observer. Druga płytka
z tym samym przykładem również jest Peripheral, więc nie zastępuje klienta.
Program sprawdzający połączenie wykonuje uzgadnianie sesji i dzieli komunikaty
poleceń zgodnie z MTU:

```bash
python3 tests/hardware/bluetooth_stream/verify_commands.py \
  --address XX:XX:XX:XX:XX:XX \
  --target rp2040 --board picow --runtime baremetal
```

Test obejmuje 500-bajtowe binarne `echo` przesyłane we fragmentach,
informacje o wywołanej procedurze i zabezpieczeniach, ograniczenia źródła
poleceń, nieznane polecenia, wysłanie zdarzenia i żądania przez Peripheral
oraz jedno ponowne połączenie. Dla `commands-freertos` użyj
`--runtime freertos`.

## Co pokazuje przykład

Wersja podstawowa uruchamia kontroler BLE, odczytuje jego adres, udostępnia
usługę i obsługuje zdarzenia połączenia. Odbiera kolejne dane z kolejki,
zgłasza przepełnienie i nie dopuszcza danych aplikacji poza sesją.
Po `HAL_EAGAIN` ponawia wysłanie jednej zachowanej próbki telemetrii.
Zachowuje też żądanie rozgłaszania, aby wznowić je po rozłączeniu.

Warianty poleceń rejestrują w routerze reguły określające dozwolone źródło
żądań. Przetwarzają kolejne fragmenty wiadomości, obsługują odpowiedzi
oraz pozwalają urządzeniu Peripheral wysłać własne zdarzenie i żądanie do
Central. Adapter poleceń jest dołączany raz do wcześniej uruchomionego Stream.

Niezależnego klienta oraz testy stabilności i bezpieczeństwa na kilku
platformach opisują
[testy sprzętowe `bluetooth_stream`](../../doc/api/pl/03_build_tests.md#bramka-sprzętowa-jh-ble-stream-v1).
