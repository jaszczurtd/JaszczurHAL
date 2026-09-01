# 26 - BLE Stream

Jest to przykład urządzenia działającego w roli BLE Peripheral i używającego
protokołu JH BLE Stream v1. Pokazuje cykl rozgłaszania i obsługi połączenia oraz
uwierzytelniony strumień danych.

Aplikacja działa jako Peripheral przyjmujący połączenia. Udostępnia usługę
strumienia i wymienia dane wyłącznie w obustronnie uwierzytelnionej sesji.

Urządzenie ogłasza się jako `JH Stream`, udostępnia każdemu klientowi wersję
protokołu i bitową maskę obsługiwanych funkcji oraz odrzuca dane, dopóki klient
nie udowodni znajomości sekretu właściwego dla urządzenia. Po uwierzytelnieniu co
sekundę publikuje linię telemetrii, zachowuje najwyżej jedną próbkę do ponownego
wysłania, gdy transmisję wstrzymuje pełny bufor TX, i loguje dane wysłane przez
klienta.

Osobne warianty `commands` i `commands-freertos` ogłaszają się jako
`JH Commands`. Przekazują `hal_ble_commands` wyłączną obsługę danych Stream,
rejestrują procedury obsługi niezależne od transportu i wymieniają z
uwierzytelnionym urządzeniem Central żądania, odpowiedzi oraz zdarzenia
binarnego protokołu poleceń.

## Kompilacja i uruchomienie

```bash
./scripts/examples_dispatcher.py build --target rp2040 --example 26_ble_stream
./scripts/examples_dispatcher.py build --target rp2350-arm --example 26_ble_stream
./scripts/examples_dispatcher.py build --target stm32g474 --example 26_ble_stream
```

Polecenie skryptu przykładów dla przykładu 26 buduje bazowy firmware i oba
warianty poleceń. Aby zbudować tylko jeden wariant, użyj wspólnego punktu
wejścia projektu:

```bash
vscode/entry/jh-vscode build --project examples/26_ble_stream \
  --target rp2040 --board picow --variant commands
vscode/entry/jh-vscode build --project examples/26_ble_stream \
  --target rp2040 --board picow --variant commands-freertos
```

Domyślne profile to RP2040 `picow`, RP2350 ARM `pico2w` i STM32G474
`nucleo-g474re-pim730`. RP2040 `pico-rm2` także obsługuje kompilację po jawnym
wyborze płytki, ale dedykowana bramka sprzętowa nie jest jeszcze gotowa:

```bash
vscode/entry/jh-vscode build \
  --project examples/26_ble_stream \
  --target rp2040 --board pico-rm2
```

RP2350 RISC-V nie jest obsługiwany, ponieważ transport Bluetooth CYW43 nie jest
dla niego włączony.

Przykład odkłada inicjalizację CYW43/BLE do pierwszej iteracji `app_task0()`, po
uruchomieniu planisty FreeRTOS. Konfiguracja projektu wybiera stos zadania o
rozmiarze 1024 słów przy włączonym FreeRTOS; domyślne 512 słów nie wystarcza na
uzgadnianie uwierzytelnionej sesji na sprzęcie RP.

## Przygotowanie sekretu

`kDeviceSecret` w [`app.cpp`](app.cpp) zawiera przykładowy sekret, dzięki czemu
projekt działa bez dodatkowych kroków przygotowawczych. W produkcie należy go
zastąpić co najmniej 256-bitową wartością właściwą dla urządzenia, przekazaną
klientowi poza głównym kanałem komunikacji, na przykład w kodzie QR na etykiecie
lub przez uwierzytelniony kanał USB. Nie należy współdzielić jednego sekretu
między urządzeniami.

`hal_ble_stream_set_secret()` instaluje sekret,
`hal_ble_stream_clear_secret()` wykonuje reset do ustawień fabrycznych, a
instalacja nowego sekretu unieważnia sesje utworzone z poprzednim.

## Strona klienta

Klient kończy uzgadnianie sesji, wysyłając `HELLO`, sprawdzając dowód urządzenia
w `HELLO_ACK` i odpowiadając `AUTH`. Oba dowody oraz dwa klucze - po jednym dla
każdego kierunku - powstają z HMAC-SHA256 obliczanego na podstawie zapisu
przebiegu uzgadniania. Obejmuje on nazwę profilu, wersję protokołu, zestawy
funkcji obsługiwanych przez obie strony, identyfikator sesji i obie liczby
jednorazowe. Ramki `DATA` używają
ChaCha20-Poly1305 z osobnym licznikiem dla każdego kierunku. Układ ramki i
wszystkie stałe znajdują się w
[`hal_ble_stream.h`](../../src/hal/bluetooth/hal_ble_stream.h).

Wynegocjowana wartość ATT MTU musi osiągnąć `HAL_BLE_STREAM_MIN_ATT_MTU`, aby
uzgadnianie sesji zmieściło się w pojedynczym zapisie. Przykład zapisuje w logu
zaobserwowane MTU.

Warianty poleceń używają systemu Linux z BlueZ w roli Central. JaszczurHAL
udostępnia obecnie role Peripheral i pasywnego Observer; druga płytka z tym przykładem
również działa jako Peripheral. Krótki program weryfikujący wykonuje
uzgadnianie po stronie klienta i dzieli binarne komunikaty poleceń zgodnie z
wynegocjowanym MTU:

```bash
python3 tests/hardware/bluetooth_stream/verify_commands.py \
  --address XX:XX:XX:XX:XX:XX \
  --target rp2040 --board picow --runtime baremetal
```

Weryfikator obejmuje pofragmentowane binarne echo o rozmiarze 500 bajtów,
informacje o użytej procedurze obsługi, metadane bezpieczeństwa, reguły źródła,
nieznane polecenia, zdarzenie wychodzące, żądanie zainicjowane przez Peripheral
oraz jedno ponowne połączenie. Dla obrazu `commands-freertos` użyj
`--runtime freertos`.

## Co pokazuje przykład

- inicjalizację kontrolera BLE, odczyt adresu, rozgłaszanie i reakcję na zdarzenia
  połączenia;
- publikowanie usługi z informacją o obsługiwanych funkcjach;
- odrzucanie danych przesyłanych poza sesją;
- odbieranie danych z jawnym sygnalizowaniem przepełnienia;
- przechowywanie i ponowne wysłanie po `HAL_EAGAIN` jednej próbki telemetrii o
  ograniczonym rozmiarze;
- przechowywanie jednego żądania rozgłaszania, aby wznowić je automatycznie po
  rozłączeniu.

Warianty poleceń pokazują dodatkowo:

- dołączenie jednego adaptera poleceń do zainicjalizowanego, uwierzytelnionego
  strumienia;
- rejestrowanie we wspólnym routerze tras dostępnych wyłącznie przez BLE i
  ograniczonych do wskazanego źródła;
- przyrostowe przetwarzanie żądań przychodzących i automatycznych odpowiedzi;
- wysyłanie zdarzenia i żądania z urządzenia Peripheral do urządzenia Central.

Niezależną implementację klienta oraz wielotargetowy test stabilności i
bezpieczeństwa opisuje [sprzętowa bramka `bluetooth_stream`](../../doc/api/pl/03_build_tests.md#bramka-sprzętowa-jh-ble-stream-v1).
