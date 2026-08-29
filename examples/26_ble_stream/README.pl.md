# 26 - BLE Stream

Przykład roli BLE Peripheral i JH BLE Stream v1 obejmujący lifecycle
advertising/połączenia oraz uwierzytelniony strumień.

Aplikacja jest połączalnym Peripheralem, który publikuje usługę strumienia i
wymienia payloady wyłącznie we wzajemnie uwierzytelnionej sesji.

Urządzenie ogłasza się jako `JH Stream`, udostępnia każdemu klientowi wersję
protokołu i maskę capabilities oraz odrzuca ruch payloadu, dopóki klient nie
udowodni znajomości sekretu właściwego dla urządzenia. Po uwierzytelnieniu co
sekundę publikuje linię telemetrii, zachowuje najwyżej jedną próbkę do ponowienia
przy backpressure TX i loguje dane wysłane przez klienta.

Osobne warianty `commands` i `commands-freertos` ogłaszają się jako
`JH Commands`. Przekazują `hal_ble_commands` wyłączną własność payloadów Stream,
rejestrują handlery niezależne od transportu i wymieniają żądania, odpowiedzi
oraz zdarzenia command-wire z uwierzytelnionym Centralem.

## Build i uruchomienie

```bash
./scripts/examples_dispatcher.py build --target rp2040 --example 26_ble_stream
./scripts/examples_dispatcher.py build --target rp2350-arm --example 26_ble_stream
./scripts/examples_dispatcher.py build --target stm32g474 --example 26_ble_stream
```

Polecenie dispatchera dla przykładu 26 buduje bazowy firmware i oba warianty
poleceń. Aby zbudować tylko jeden wariant, użyj wspólnego entrypointu projektu:

```bash
vscode/entry/jh-vscode build --project examples/26_ble_stream \
  --target rp2040 --board picow --variant commands
vscode/entry/jh-vscode build --project examples/26_ble_stream \
  --target rp2040 --board picow --variant commands-freertos
```

Domyślne profile dispatchera to RP2040 `picow`, RP2350 ARM `pico2w` i STM32G474
`nucleo-g474re-pim730`. RP2040 `pico-rm2` także obsługuje build po jawnym
wyborze boardu, ale dedykowany gate sprzętowy nadal oczekuje:

```bash
vscode/entry/jh-vscode build \
  --project examples/26_ble_stream \
  --target rp2040 --board pico-rm2
```

RP2350 RISC-V nie jest wspierany, ponieważ transport Bluetooth CYW43 nie jest
dla niego włączony.

Przykład odkłada inicjalizację CYW43/BLE do pierwszej iteracji `app_task0()`, po
uruchomieniu schedulera FreeRTOS. Konfiguracja projektu wybiera stos taska o
rozmiarze 1024 słów przy włączonym FreeRTOS; domyślne 512 słów nie wystarcza na
uwierzytelniony handshake na sprzęcie RP.

## Provisioning sekretu

`kDeviceSecret` w [`app.cpp`](app.cpp) zastępuje provisioning, aby przykład
działał bez dodatkowych kroków. Produkt powinien zastąpić go co najmniej
256-bitową wartością właściwą dla urządzenia, przekazaną klientowi poza pasmem,
na przykład w kodzie QR na etykiecie lub przez uwierzytelniony kanał USB. Nie
należy współdzielić jednego sekretu między urządzeniami.

`hal_ble_stream_set_secret()` instaluje sekret,
`hal_ble_stream_clear_secret()` realizuje reset fabryczny, a instalacja nowego
sekretu unieważnia sesje utworzone z poprzednim.

## Strona klienta

Klient kończy handshake, wysyłając `HELLO`, sprawdzając dowód urządzenia w
`HELLO_ACK` i odpowiadając `AUTH`. Oba dowody oraz dwa kierunkowe klucze powstają
z HMAC-SHA256 nad transkrypcją obejmującą nazwę profilu, wersję protokołu, oba
zbiory capabilities, identyfikator sesji i oba nonce. Ramki `DATA` używają
ChaCha20-Poly1305 z licznikiem kierunkowym. Układ ramki i wszystkie stałe
znajdują się w [`hal_ble_stream.h`](../../src/hal/bluetooth/hal_ble_stream.h).

Wynegocjowane ATT MTU musi osiągnąć `HAL_BLE_STREAM_MIN_ATT_MTU`, zanim handshake
zmieści się w pojedynczym zapisie; przykład loguje zaobserwowane MTU.

Warianty poleceń używają Linux/BlueZ jako Centrala. JaszczurHAL udostępnia
obecnie role Peripheral i pasywnego Observera; drugi board z tym przykładem jest
również Peripheralem. Krótki weryfikator sprzętowy wykonuje handshake po stronie
klienta i dzieli dane command-wire według wynegocjowanego MTU:

```bash
python3 tests/hardware/bluetooth_stream/verify_commands.py \
  --address XX:XX:XX:XX:XX:XX \
  --target rp2040 --board picow --runtime baremetal
```

Weryfikator obejmuje pofragmentowane binarne echo 500 bajtów, provenance
handlera i metadane bezpieczeństwa, politykę źródła, nieznane polecenia,
zdarzenie wychodzące, żądanie zapoczątkowane przez Peripheral oraz jedno ponowne
połączenie. Dla obrazu `commands-freertos` użyj `--runtime freertos`.

## Co pokazuje przykład

- inicjalizację kontrolera BLE, odczyt adresu, advertising i reakcję na zdarzenia
  połączenia;
- publikowanie usługi ze zbiorem capabilities;
- odrzucanie ruchu payloadu bez sesji;
- odbieranie payloadów z jawnym raportowaniem przepełnienia;
- zachowanie i ponowienie jednej ograniczonej próbki telemetrii po `HAL_EAGAIN`;
- zachowanie jednego żądania advertising, aby wznowić je automatycznie po
  rozłączeniu.

Warianty poleceń pokazują dodatkowo:

- dołączenie jednego adaptera poleceń do zainicjalizowanego, uwierzytelnionego
  Stream;
- rejestrowanie tras BLE-only i ograniczonych do źródła we wspólnym routerze;
- przyrostowe przetwarzanie żądań przychodzących i automatycznych odpowiedzi;
- wysyłanie zdarzenia i żądania od Peripherala do Centrala.

Niezależną implementację klienta oraz wielotargetowy test stabilności i
bezpieczeństwa opisuje [sprzętowy gate `bluetooth_stream`](../../doc/api/pl/03_build_tests.md#bramka-sprzętowa-jh-ble-stream-v1).
