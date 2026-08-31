# API Bluetooth Low Energy i gamepada Classic HID

*Dostępne również [po angielsku](../en/20_bluetooth.md).*

> **Część [Dokumentacji API JaszczurHAL](../../pl/JaszczurHAL_API.md)**

Moduły Bluetooth są opcjonalne. `HAL_ENABLE_BLE` udostępnia API Bluetooth Low
Energy dla ról Peripheral i pasywnego Observera przez
`hal/bluetooth/hal_ble.h`. `HAL_ENABLE_BLUETOOTH_GAMEPAD` udostępnia jeden
gamepad Bluetooth Classic HID przez `hal/bluetooth/hal_gamepad.h` i
automatycznie włącza `HAL_ENABLE_BLUETOOTH_CLASSIC`. Oba API są również
dostępne ze zbiorczego nagłówka `JaszczurHAL.h`.

Bieżące wydanie dostarcza jedno połączenie Peripheral, klasyczne (legacy)
advertising z możliwością połączenia, pasywne, klasyczne skanowanie
Observer, kopiowane raporty advertisingu, parsowanie struktur AD, zdarzenia
kontrolera i połączenia, raportowanie ATT MTU oraz statyczną bazę danych
GATT zawierającą obowiązkowe usługi GAP i GATT. Nie dostarcza jeszcze
aktywnego skanowania ani żądań scan response, dowolnych charakterystyk
aplikacji, klienta GATT, parowania ani bondingu. Opcjonalny profil
`HAL_ENABLE_BLE_STREAM` dodaje jedną stałą, uwierzytelnioną usługę
aplikacyjną wraz z jej ścieżką powiadomień. `HAL_ENABLE_BLE_COMMANDS`
dedykuje ten ładunek Stream współdzielonemu routerowi poleceń; nie dodaje
klienta GATT.

## Obsługiwane profile

| API | Target | Płytka | Radio/host | Walidacja |
|---|---|---|---|---|
| BLE | `rp2040` | `picow` | wbudowany CYW43439 z BTstack | Bramki sprzętowe Observer oraz Stream bare-metal/FreeRTOS przeszły |
| BLE | `rp2350-arm` | `pico2w` | wbudowany CYW43439 z BTstack | Bramki Observer, Stream bare-metal/FreeRTOS oraz aktywnej koegzystencji Stream+WiFi/MQTT przeszły |
| BLE | `rp2040` | `pico-rm2` | zewnętrzny CYW43439 PIM730/RM2 przez PIO | obsługiwane na etapie buildu; dedykowana bramka sprzętowa oczekująca |
| BLE | `stm32g474` | `nucleo-g474re-pim730` | zewnętrzny CYW43439 PIM730/RM2 przez gSPI | Bramki Peripheral i Observer oraz pełne bramki obciążeniowe Stream bare-metal/FreeRTOS z wyświetlaczem przeszły |
| BLE | `esp32s3` | `waveshare-esp32-s3-zero` | zintegrowany kontroler LE z ESP-IDF NimBLE | kompletna bramka buildu i linkowania; bramka radiowa na sprzęcie oczekuje |
| Gamepad Classic | `rp2040` / `rp2350-arm` / `stm32g474` | profile Bluetooth wymienione wyżej | CYW43439 z hostem HID BTstack | bramki sprzętowe Zero 2 przeszły na `pico2w`; pozostałe płytki przeszły bramki buildu |
| Gamepad Classic | `esp32` | `esp32-devkitc-v4` | zintegrowany kontroler BR/EDR z ESP-IDF Bluedroid i ESP HID Host | kompletna bramka buildu i linkowania; bramka radiowa na sprzęcie oczekuje |
| BLE i gamepad Classic | `mock` | `host-mock` | deterministyczne backendy testowe | testy hosta |

Backend RP2350 obsługuje wyłącznie Pico 2 W z targetem `rp2350-arm`. Pico 2 W
z `rp2350-riscv` jest nieobsługiwane, ponieważ transport Bluetooth CYW43 nie
jest włączony dla tego targetu. `HAL_ENABLE_BLE` i
`HAL_ENABLE_BLUETOOTH_CLASSIC` są odrzucane podczas buildu, gdy ich
konkretny transport jest niedostępny. Sprawdzenia możliwości w runtime
rozróżniają `HAL_BOARD_CAP_BLUETOOTH_LE_CONTROLLER` i
`HAL_BOARD_CAP_BLUETOOTH_CLASSIC_CONTROLLER`; starszy ogólny bit Bluetooth
pozostaje dostępny dla zgodności. Moduły zewnętrzne wymagają dodatkowo
`HAL_BOARD_CAP_EXTERNAL_RADIO_FRONTEND`.

ESP32-S3 obsługuje obecnie bazowe API BLE Peripheral/Observer, ale nie
`HAL_ENABLE_BLE_STREAM`, klienta GATT ani Classic HID. Oryginalny ESP32
obsługuje obecnie profil gamepada Classic, ale nie włącza publicznego API BLE.
Są to jawne granice targetów, a nie ścieżki zastępcze wybierane w runtime.

## Cykl życia i odpytywanie

```cpp
#include <JaszczurHAL.h>

#include <cstring>

static hal_ble_advertising_handle_t advertising;
static hal_status_t ble_status = HAL_NONE;
static bool ble_started;

static void on_ble_event(const hal_ble_event_t *event, void *) {
  if (event->type == HAL_BLE_EVENT_CONTROLLER_READY) {
    static const uint8_t payload[] = {
        0x02, 0x01, 0x06,                   // general-discoverable flags
        0x07, 0x09, 'J', 'H', ' ', 'B', 'L', 'E'}; // complete name
    hal_ble_advertising_config_t config{};
    config.interval_min = 0x00a0; // 100 ms
    config.interval_max = 0x00a0;
    config.data_length = static_cast<uint8_t>(sizeof(payload));
    std::memcpy(config.data, payload, sizeof(payload));
    (void)hal_ble_advertising_start(&config, &advertising);
  }
}

static hal_status_t start_ble(void) {
  hal_status_t status = hal_ble_initialize();
  if (status == HAL_OK) {
    status = hal_ble_set_event_callback(on_ble_event, nullptr);
  }
  return status;
}

extern "C" void app_task0(void) {
  if (!ble_started) {
    ble_started = true;
    ble_status = start_ble();
  }
  if (ble_status != HAL_OK) {
    hal_delay_ms(1u);
    return;
  }
  const hal_status_t poll_status = hal_ble_poll();
  if (poll_status != HAL_OK && poll_status != HAL_EOVERFLOW) {
    /* Record or recover from the controller error. */
  }
  hal_delay_ms(1u);
}
```

`hal_ble_initialize()` uruchamia backend wybranego targetu i zwraca sterowanie,
gdy start zostanie zaakceptowany. Gotowość jest
asynchroniczna i jest raportowana przez `HAL_BLE_EVENT_CONTROLLER_READY`.
Inicjalizacja i deinicjalizacja są idempotentne po sukcesie. Deinicjalizacja
unieważnia każdy uchwyt połączenia i advertisingu, czyści kolejkę zdarzeń
oraz usuwa callback.

Wywołuj `hal_ble_poll()` często z jednego zadania lub pętli kooperacyjnej.
Obsługuje kontroler, a następnie wywołuje callbacki poza blokadą radiową
backendu. Wywołanie `hal_ble_poll()`, zmiana callbacku
lub deinicjalizacja rekurencyjnie z callbacku zwraca `HAL_EBUSY`; zapytania
tylko do odczytu są dozwolone.

## Zdarzenia

`HAL_BLE_EVENT_QUEUE_DEPTH` konfiguruje kopiowaną, ograniczoną kolejkę
zdarzeń i domyślnie wynosi 8. Publiczne zdarzenia to:

- `HAL_BLE_EVENT_CONTROLLER_READY`;
- `HAL_BLE_EVENT_ADVERTISING_STARTED` oraz
  `HAL_BLE_EVENT_ADVERTISING_STOPPED`;
- `HAL_BLE_EVENT_CONNECTED` oraz `HAL_BLE_EVENT_DISCONNECTED`;
- `HAL_BLE_EVENT_MTU_UPDATED`;
- `HAL_BLE_EVENT_SCAN_STARTED`, `HAL_BLE_EVENT_SCAN_STOPPED` oraz
  `HAL_BLE_EVENT_SCAN_REPORT_AVAILABLE`;
- `HAL_BLE_EVENT_ERROR`.

Wybierz jeden model odbioru: zarejestruj callback wywoływany przez
`hal_ble_poll()`, lub zdejmuj zdarzenia przy pomocy `hal_ble_event_next()`.
Oba pobierają zdarzenia z tej samej kolejki. `hal_ble_event_next()` zwraca `HAL_EAGAIN`,
gdy jest pusta. Jeśli kolejka się zapełni, nowe zdarzenia są odrzucane,
`hal_ble_info_t::dropped_events` wzrasta, a kolejne odpytanie raportuje
`HAL_EOVERFLOW` bez zatrzymywania BLE.

Zdarzenie gotowości nie ma adresu peera; wywołaj
`hal_ble_get_local_address()` po jego otrzymaniu. Zdarzenie połączenia
zawiera adres peera i nowy, nieprzezroczysty uchwyt połączenia. Zdarzenia
MTU i rozłączenia odnoszą się do tego samego uchwytu.

## Advertising

`hal_ble_advertising_start()` kopiuje kompletną konfigurację przed
zwróceniem sterowania. Klasyczny (legacy) ładunek musi zawierać od 1 do 31
bajtów. Minimalny interwał musi mieścić się między `0x0020` a `0x4000`
jednostek (od 20 ms do 10,24 s), a maksymalny musi być co najmniej równy
minimalnemu i nie większy niż `0x4000`.

`HAL_OK` oznacza, że żądanie zostało zaakceptowane. Poczekaj na
`HAL_BLE_EVENT_ADVERTISING_STARTED` na potwierdzenie zakończenia.
Advertising zażądany przed gotowością kontrolera rozpoczyna się, gdy
kontroler stanie się gotowy. Udane połączenie wstrzymuje go, a rozłączenie
uruchamia go ponownie, dopóki żądanie pozostaje aktywne. Zatrzymaj żądanie
jego nieprzezroczystym uchwytem advertisingu. Nie składaj drugiego żądania
startu po `HAL_BLE_EVENT_DISCONNECTED`; oryginalne żądanie jest już
właścicielem automatycznego restartu.

## Pasywne skanowanie Observer

`hal_ble_scan_start()` akceptuje interwał, okno oraz opcjonalny filtr
duplikatów. Obie wartości czasowe używają jednostek Bluetooth 0,625 ms i
muszą mieścić się między `HAL_BLE_SCAN_INTERVAL_MIN` a
`HAL_BLE_SCAN_INTERVAL_MAX`; okno nie może przekraczać interwału. `HAL_OK`
oznacza, że żądanie zostało zaakceptowane. Poczekaj na
`HAL_BLE_EVENT_SCAN_STARTED` na potwierdzenie zakończenia.

Skanowanie jest pasywne i odbiera wyłącznie klasyczne (legacy) pakiety
advertisingu. Nie wysyła żądań scan, nie inicjuje połączeń, nie paruje ani
nie eksponuje klienta GATT. Początkowe wydanie Observer utrzymuje też
skanowanie jako wzajemnie wykluczające się ze advertisingiem i połączeniem
Peripheral; konfliktujące żądania startu zwracają `HAL_EBUSY`.

Raporty są kopiowane do osobnej, o stałym rozmiarze kolejki konfigurowanej
przez `HAL_BLE_SCAN_REPORT_QUEUE_DEPTH`, która domyślnie wynosi 8. Zdarzenie
`HAL_BLE_EVENT_SCAN_REPORT_AVAILABLE` oznacza, że co najmniej jeden raport
można odczytać przy pomocy `hal_ble_scan_report_next()`. Opróżnij wszystkie
dostępne raporty po tym zdarzeniu. Wywołanie zwraca `HAL_EAGAIN`, gdy
kolejka jest pusta. Jeśli raporty zostały odrzucone, najpierw zwraca
`HAL_EOVERFLOW`, aby potwierdzić utratę; wywołaj ponownie, aby odczytać
najstarszy zachowany raport. Liczniki skumulowane i oczekujące są dostępne
przez `hal_ble_info_t`.

Każdy raport zawiera skopiowany adres, RSSI, klasyczny (legacy) typ
zdarzenia oraz do 31 bajtów ładunku. `hal_ble_advertising_field_next()`
iteruje po poprzedzonych długością strukturach AD bez alokacji. Zacznij od
przesunięcia zero; `HAL_EAGAIN` oznacza koniec, a `HAL_EIO` odrzuca
zniekształcone wejście. Zwrócone dane pola wskazują na raport i pozostają
ważne, dopóki obiekt tego raportu istnieje.

### Przykład Observer

Poniższa pętla uruchamia pasywne skanowanie 60 ms/30 ms i opróżnia każdy
zachowany raport. Zastąp `consume_ad_field()` obsługą specyficzną dla
aplikacji dla typów AD takich jak kompletna nazwa lokalna (`0x09`) lub dane
producenta (`0xff`).

```cpp
static void consume_ad_field(const hal_ble_advertising_report_t &report,
                             const hal_ble_advertising_field_t &field);

static hal_status_t observer_status = HAL_NONE;
static bool observer_started;

static void on_observer_event(const hal_ble_event_t *event, void *) {
  if (event->type == HAL_BLE_EVENT_CONTROLLER_READY) {
    hal_ble_scan_config_t scan{};
    scan.interval = 0x0060; // 60 ms
    scan.window = 0x0030;   // 30 ms
    scan.filter_duplicates = true;
    (void)hal_ble_scan_start(&scan);
  }
}

static void drain_scan_reports(void) {
  for (;;) {
    hal_ble_advertising_report_t report{};
    const hal_status_t status = hal_ble_scan_report_next(&report);
    if (status == HAL_EOVERFLOW) {
      continue; // Loss acknowledged; retained reports are still available.
    }
    if (status == HAL_EAGAIN) {
      return;
    }
    if (status != HAL_OK) {
      return;
    }

    size_t offset = 0;
    hal_ble_advertising_field_t field{};
    while (hal_ble_advertising_field_next(&report, &offset, &field) ==
           HAL_OK) {
      consume_ad_field(report, field);
    }
  }
}

static hal_status_t start_observer(void) {
  hal_status_t status = hal_ble_initialize();
  if (status == HAL_OK) {
    status = hal_ble_set_event_callback(on_observer_event, nullptr);
  }
  return status;
}

extern "C" void app_task0(void) {
  if (!observer_started) {
    observer_started = true;
    observer_status = start_observer();
  }
  if (observer_status != HAL_OK) {
    hal_delay_ms(1u);
    return;
  }
  (void)hal_ble_poll();
  drain_scan_reports();
  hal_delay_ms(1u);
}
```

## Połączenia i MTU

Obsługiwane jest tylko jedno połączenie Peripheral. Uchwyty połączenia i
advertisingu są niezerowe, nieprzezroczyste i nieważne po ich zdarzeniu
terminalnym, deinicjalizacji lub awarii kontrolera. Przekazanie
nieaktualnego uchwytu zwraca `HAL_ENOENT`.

`hal_ble_disconnect()` kolejkuje lokalne rozłączenie. Zakończenie
nadchodzi jako `HAL_BLE_EVENT_DISCONNECTED`. `hal_ble_get_mtu()` zwraca 23,
dopóki wybrany stos nie zaraportuje wynegocjowanej wartości przez
`HAL_BLE_EVENT_MTU_UPDATED`.

## Zachowanie backendów ESP-IDF

ESP32-S3 używa NimBLE dla bazowego API LE. Callbacki ESP-IDF kopiują adresy,
ładunki advertisingu, stan połączenia i zmiany MTU do ograniczonych kolejek
HAL. Callbacki aplikacji nadal są wywoływane wyłącznie z `hal_ble_poll()`, a
nigdy z zadania zdarzeń ESP-IDF.

Oryginalny ESP32 używa Bluedroid i `esp_hidh` dla gamepada Classic. Wyszukiwanie
GAP przyjmuje wyłącznie zwalidowaną tożsamość Android D-input o nazwie
`8BitDo Zero 2 gamepad` i klasie urządzenia gamepad. Po otwarciu HID backend
sprawdza VID `0x2dc8`, PID `0x3230`, pojedynczą mapę raportów oraz deskryptor
zaakceptowany przez parser niezależny od targetu. PIN `0000` i potwierdzenie
SSP pozostają wstrzymane, dopóki aplikacja nie wywoła
`hal_gamepad_pairing_authorize()`.

Oba backendy ESP współdzielą z backendem sieciowym jeden idempotentny
inicjalizator NVS. Niezgodna albo pełna partycja NVS powoduje zwrot
`HAL_ECONFIG`; HAL nie kasuje automatycznie pamięci aplikacji. Parser HAL,
kolejki zdarzeń i snapshoty mają stałą pojemność. Wewnętrzne obiekty NimBLE,
Bluedroid, pętli zdarzeń i hosta HID w ESP-IDF mogą używać alokacji dynamicznej.

## Model statusu i niepowodzeń

API używa `hal_status_t` wszędzie. Typowe wyniki to:

| Status | Znaczenie |
|---|---|
| `HAL_OK` | synchroniczne zapytanie powiodło się lub polecenie asynchroniczne zostało zaakceptowane |
| `HAL_EUNINIT` | BLE nie zostało zainicjalizowane |
| `HAL_EAGAIN` | dane gotowości/zdarzenia nie są jeszcze dostępne |
| `HAL_EBUSY` | konfliktujące żądanie lub rekurencja callbacku/odpytywania |
| `HAL_ENOENT` | nieaktualny lub nieznany nieprzezroczysty uchwyt |
| `HAL_EOVERFLOW` | ograniczona kolejka zdarzeń lub raportów skanowania odrzuciła dane |
| `HAL_EUNSUPPORTED` | wybrana płytka nie ma wymaganego sprzętu radiowego |
| `HAL_EHW` / `HAL_EIO` | awaria kontrolera lub transportu |

Użyj `hal_ble_get_info()` po spójną migawkę stanu, adresy lokalny i peera,
bieżące uchwyty, generację, ostatni status, MTU, stan skanowania, liczbę
oczekujących raportów oraz oba liczniki odrzuceń. Krytyczny błąd kontrolera
lub transportu przenosi podsystem do stanu `HAL_BLE_STATE_FAILED`,
unieważnia jego uchwyty, zatrzymuje skanowanie i przesuwa generację.

## Gamepad Bluetooth Classic HID

API gamepada posiada jeden profil hosta Classic HID i zwraca jeden
nieprzezroczysty `hal_gamepad_t`. `hal_gamepad_open()` rozpoczyna profil
asynchronicznie; następnie `hal_gamepad_poll()` musi być często wywoływane z
jednego taska lub pętli kooperacyjnej. `hal_gamepad_get_info()` raportuje stan
publiczny, ostatni status, bieżącą generację połączenia, flagi parowania,
obecność znanego urządzenia i diagnostykę ograniczonej kolejki.

Publiczne stany to `UNINITIALIZED`, `STARTING`, `READY`, `DISCOVERING`,
`CONNECTING`, `CONNECTED` i `FAILED`. Krytyczna awaria kontrolera lub transportu
przenosi profil do `FAILED`. `hal_gamepad_close()` zatrzymuje profil i
unieważnia jego uchwyt.

### Parowanie i reconnect

Parowanie jest sterowane przez aplikację i ograniczone w czasie. Gdy profil
osiągnie `READY`, wywołaj `hal_gamepad_pairing_open()`, aby rozpocząć okno
wykrywania. Kiedy `hal_gamepad_info_t::pairing_pending` stanie się prawdziwe,
aplikacja może wywołać `hal_gamepad_pairing_authorize()` i zaakceptować Just
Works albo legacy PIN `0000`. Nieobsługiwane przepływy z passkey są odrzucane.
Zaakceptowany adres wskazuje znane urządzenie używane przez
`hal_gamepad_reconnect()` podczas bieżącego uruchomienia firmware.
Przechowywanie link key zależy od stosu. Zachowanie tożsamości wybranej przez
HAL po resecie watchdogiem lub zimnym starcie nie wchodzi w zakres tego
wydania, dlatego aplikacja musi być gotowa ponownie otworzyć okno parowania po
restarcie.

Otwarcie okna parowania jest jawną decyzją autoryzacyjną. Produkt powinien je
udostępnić dopiero po lokalnej akcji użytkownika i nie powinien traktować nazwy
urządzenia, adresu Bluetooth ani nieuwierzytelnionej wymiany Just Works jako
dowodu tożsamości użytkownika.

### Znormalizowane snapshoty

Parser raportów HID nie zależy od BTstack ani ESP-IDF. Waliduje ograniczony
deskryptor raportów i zasila ten sam znormalizowany model snapshotu na każdym
backendzie.

`hal_gamepad_snapshot_t` zawiera generację połączenia, 32-bitową maskę
przycisków, dziewięć osi Generic Desktop, maskę obecności osi, maskę kierunku
D-pada i stan połączenia. Bit przycisku 0 odpowiada HID Button 1. Osie są
normalizowane do `-32767..32767` i indeksowane przez `HAL_GAMEPAD_AXIS_*`; dla
nieobsługiwanych osi bit w `axes_present` jest wyzerowany. D-pad łączy
`HAL_GAMEPAD_DPAD_UP`, `RIGHT`, `DOWN` i `LEFT`.

`hal_gamepad_snapshot()` odczytuje najnowszy stan bez zużywania go.
`hal_gamepad_snapshot_next()` pobiera zmiany z kolejki o stałym rozmiarze
`HAL_GAMEPAD_SNAPSHOT_QUEUE_DEPTH`. Zwraca `HAL_EAGAIN`, gdy kolejka jest pusta.
Jeśli utracono stany pośrednie, najpierw zwraca `HAL_EOVERFLOW`; wywołaj funkcję
ponownie, aby dostać najnowszą zachowaną sekwencję. Połączenie i rozłączenie też
tworzą snapshoty, a snapshot rozłączenia zeruje wszystkie wejścia, dzięki czemu
aplikacja nie zachowa wciśniętego elementu po utracie linku.

```c
hal_gamepad_t gamepad = NULL;

void service_gamepad(void) {
  hal_status_t status = hal_gamepad_poll(gamepad);
  if (status != HAL_OK && status != HAL_EOVERFLOW) {
    return;
  }

  for (;;) {
    hal_gamepad_snapshot_t snapshot = {0};
    status = hal_gamepad_snapshot_next(gamepad, &snapshot);
    if (status == HAL_EOVERFLOW) {
      continue;
    }
    if (status != HAL_OK) {
      break;
    }
    /* Obsłuż przyciski, osie i stan D-pada. */
  }
}
```

Deterministyczny backend mock pozwala wstrzyknąć gotowość, żądanie parowania,
połączenie, snapshoty wejść, rozłączenie, przepełnienie kolejki i błędy
transportu. Kompletny konsument C oraz wariant buildu BLE+Classic znajdują się
w [`examples/29_bluetooth_gamepad`](../../../examples/29_bluetooth_gamepad/).

## JH BLE Stream v1

`HAL_ENABLE_BLE_STREAM` dodaje `hal_ble_stream.h`, ograniczony strumień
bajtów przenoszony przez jedną statyczną usługę GATT. Flaga włącza
`HAL_ENABLE_BLE` oraz `HAL_ENABLE_CRYPTO`.

BLE Stream pozostaje ogólnym, aplikacyjnym strumieniem bajtów, gdy jest
wybrany samodzielnie. Osobny moduł
[`hal_ble_commands`](23_commands.md#uwierzytelniony-adapter-ble-stream)
fragmentuje współdzielony, binarny format poleceń na uwierzytelnione
ładunki Stream i dysponuje żądania przez `hal_command_router`.
`HAL_ENABLE_BLE_STREAM` nie włącza tego zachowania ani routera;
`HAL_ENABLE_BLE_COMMANDS` włącza obie te zależności i daje adapterowi
poleceń wyłączną własność operacji wysyłania/odbierania ładunku Stream.

Nagłówek jest jedynym źródłem prawdy dla UUID-ów usługi, układu ramki oraz
bitów możliwości. Zmiana którejkolwiek z tych wartości podnosi wersję
profilu.

| Element | UUID |
|---|---|
| Usługa | `B7CE0001-3C13-4FE2-801F-D71BDAB1369B` |
| RX (write, write-without-response) | `B7CE0002-3C13-4FE2-801F-D71BDAB1369B` |
| TX (notify) | `B7CE0003-3C13-4FE2-801F-D71BDAB1369B` |
| Wersja protokołu (read) | `B7CE0004-3C13-4FE2-801F-D71BDAB1369B` |
| Możliwości (read) | `B7CE0005-3C13-4FE2-801F-D71BDAB1369B` |

### Model bezpieczeństwa

Klient bez sesji odczytuje wersję protokołu, maskę bitową możliwości i nic
więcej. Każda wymiana ładunku wymaga wzajemnie uwierzytelnionej sesji
opartej na sekrecie właściwym dla urządzenia, o długości co najmniej 256
bitów, dostarczanym poza pasmem (out of band).

Handshake wiąże transkrypt zbudowany z nazwy profilu, wersji protokołu,
obu zestawów możliwości, identyfikatora sesji oraz dwóch losowych nonce.
Cztery osobne domeny HMAC-SHA256 wytwarzają dowód urządzenia, dowód klienta
oraz dwa kierunkowe klucze sesji. Ramki `DATA` są chronione przy pomocy
ChaCha20-Poly1305; kierunek i ściśle rosnący licznik wchodzą zarówno do
nonce, jak i do danych powiązanych (associated data). Liczniki są kolejne:
odbiorca akceptuje wyłącznie dokładnie poprzednią wartość plus jeden, więc
powtórka (replay), zmniejszenie lub luka w przód zamyka sesję.

Sesje zawodzą w sposób zamknięty (fail closed). Błędny dowód, sfałszowany
tag, powtórzony lub zmniejszony licznik, licznik bliski przepełnienia,
niepowodzenie entropii, rozłączenie, zmiana generacji kontrolera, anulowanie
subskrypcji oraz timeout bezczynności - wszystkie te zdarzenia zamykają
sesję i zerują jej kierunkowe klucze. Powtarzające się niepowodzenia
uwierzytelniania przenoszą profil w ograniczone okno backoffu, podczas
którego handshake'i są odrzucane. Rotacja lub wyczyszczenie sekretu
unieważnia każdą sesję zbudowaną na poprzednim sekrecie.

BLE Stream współdzieli niezależne od targetu prymitywy
`jh_secure_random_bytes()`, `jh_secure_zeroize()` oraz
`jh_constant_time_compare()` z Serial Session. Bufory dowodu, nonce,
transkryptu, kluczy kierunkowych oraz zakolejkowanego jawnego tekstu są
czyszczone na ich ścieżkach terminalnych; nie istnieje żadna lokalna dla BLE
implementacja zerowania ani porównania tagów.

Adres urządzenia oraz parowanie na poziomie warstwy łącza nie stanowią
autoryzacji. `Just Works` szyfruje łącze bez ochrony przed atakiem MITM,
dlatego operacje produktowe zależą od sesji aplikacyjnej, a nie wyłącznie od
łącza BLE.

### ATT MTU

Jedna ramka podróżuje w pojedynczym zapisie lub powiadomieniu. Handshake
wymaga co najmniej `HAL_BLE_STREAM_MIN_ATT_MTU`, a ładunek o pełnym
rozmiarze wymaga `HAL_BLE_STREAM_FULL_PAYLOAD_ATT_MTU`. Obserwuj
`HAL_BLE_EVENT_MTU_UPDATED` i utrzymuj ładunki w granicach tego, co przenosi
wynegocjowane MTU. Wysyłka, która nie mieści się w bieżącym MTU, zwraca
`HAL_EOVERFLOW` bez zamykania uwierzytelnionej sesji.

Odpowiedzi handshake oraz ładunki aplikacyjne używają ograniczonych,
oczekujących slotów. `HAL_EAGAIN` od kontrolera zachowuje ramkę i nie
zużywa jej licznika kierunkowego; kolejne odpytanie lub zdarzenie
gotowości do wysyłki (can-send) ponawia ją. Samo powiadomienie BTstack jest
wydawane wyłącznie przez współdzieloną usługę radiową CYW43, gdy jest ona
właścicielem blokady radiowej. Stream utrzymuje w locie co najwyżej jedno
powiadomienie zaakceptowane przez backend, a `pending_tx` obejmuje to
powiadomienie oprócz lokalnie zakolejkowanych ładunków. Przed
zaakceptowaniem nowego `HELLO`, Stream odrzuca powiadomienie, które nadal
oczekuje w backendzie. Jeśli lokalne przesłanie lub jego callback zakończenia
jest już w trakcie, `HELLO` jest odrzucane z `HAL_EBUSY`, a bieżąca sesja
pozostaje dostępna do ponowienia. Każde inne niepowodzenie odrzucenia
zamyka sesję bez wysłania `HELLO_ACK`. Zapobiega to sytuacji, w której
ponowne uzgodnienie kluczy (rekey) w ramach tego samego łącza wyprzedziłoby
dane z poprzedniej sesji.

Inicjalizacja i deinicjalizacja Stream serializują operacje publikacji i
wycofania publikacji usługi. Współbieżne wywołanie cyklu życia zwraca
`HAL_EBUSY`; nieudana publikacja cofa stan Stream do
`HAL_BLE_STREAM_STATE_UNINITIALIZED`.

### Przykład Stream

Zainicjalizuj podsystem BLE od pierwszej iteracji zadania aplikacji,
zainstaluj unikalny, przypisany (provisioned) sekret, a następnie obsługuj
obie warstwy z tego samego zadania. Na targetach FreeRTOS `app_start()`
uruchamia się przed planistą i nie może uruchamiać CYW43. Advertising
używa przepływu Peripheral pokazanego powyżej; stała usługa Stream pojawia
się w jego bazie danych GATT automatycznie. Zadanie Stream wymaga co
najmniej budżetu stosu 1024 słów zwalidowanego sprzętowo, używanego przez
przykład i fixture sprzętowy.

```c
hal_status_t start_stream(const uint8_t *device_secret, size_t secret_length) {
  hal_status_t status = hal_ble_initialize();
  if (status != HAL_OK) {
    return status;
  }

  hal_ble_stream_config_t config = {0};
  config.capabilities =
      HAL_BLE_STREAM_CAP_TELEMETRY | HAL_BLE_STREAM_CAP_DIAGNOSTICS;
  status = hal_ble_stream_initialize(&config);
  if (status != HAL_OK) {
    return status;
  }
  return hal_ble_stream_set_secret(device_secret, secret_length);
}

static uint8_t echo_payload[HAL_BLE_STREAM_MAX_PAYLOAD];
static size_t echo_length;
static bool echo_pending;

static hal_status_t try_send_echo(void) {
  const hal_status_t status =
      hal_ble_stream_send(echo_payload, echo_length);
  if (status != HAL_EAGAIN) {
    echo_pending = false;
    echo_length = 0u;
  }
  return status;
}

void service_stream(void) {
  const hal_status_t poll_status = hal_ble_poll();
  if (poll_status != HAL_OK && poll_status != HAL_EOVERFLOW) {
    echo_pending = false;
    echo_length = 0u;
    return;
  }

  if (echo_pending) {
    const hal_status_t sent = try_send_echo();
    if (sent != HAL_OK) {
      /* HAL_EAGAIN keeps exactly one pending echo; other errors discard it. */
      return;
    }
  }

  for (;;) {
    const hal_status_t received =
        hal_ble_stream_receive(echo_payload, sizeof(echo_payload), &echo_length);
    if (received == HAL_EOVERFLOW) {
      continue; /* Loss acknowledged; drain the retained queue. */
    }
    if (received != HAL_OK) {
      echo_length = 0u;
      break;
    }

    const hal_status_t sent = try_send_echo();
    if (sent == HAL_EAGAIN) {
      echo_pending = true;
      return; /* Retry this echo before receiving another payload. */
    } else if (sent == HAL_EOVERFLOW) {
      /* The payload does not fit the negotiated ATT MTU. */
      return;
    } else if (sent == HAL_EAUTH) {
      /* The session closed; the client must authenticate again. */
      return;
    } else if (sent != HAL_OK) {
      return;
    }
  }
}
```

Przykład zachowuje co najwyżej jedno echo po `HAL_EAGAIN` i ponawia je
przed usunięciem kolejnego ładunku RX. Rozłączenie lub jakikolwiek inny
błąd wysyłki odrzuca to oczekujące echo, więc dane ze starej sesji nie mogą
trafić do nowej.

`hal_ble_stream_receive_ex()` ma to samo zachowanie kolejki i przepełnienia,
zwracając dodatkowo niezmienne pochodzenie dla zdjętego ładunku DATA:
generację Stream, publiczny identyfikator sesji handshake oraz
uwierzytelniony licznik kierunkowy. Adaptery Stream używają tego, aby
zapobiec łączeniu fragmentów z różnych sesji lub zakresów liczników.
Oryginalne `hal_ble_stream_receive()` pozostaje formą wygodną, gdy te
metadane nie są potrzebne.

`hal_ble_stream_get_info()` raportuje stan, wynegocjowane możliwości,
publiczny identyfikator sesji, liczniki kierunkowe, niepowodzenia
uwierzytelniania, odrzucenia powtórek (replay) oraz głębokość kolejki na
potrzeby diagnostyki.

## Koegzystencja i własność Bluetooth

BLE, Bluetooth Classic i WiFi współdzielą jeden kontroler CYW43, transport,
stan runtime radia oraz blokadę usługi. Aplikacje nie mogą linkować Pico SDK
`pico_cyw43_arch` ani `pico_btstack_cyw43` obok tego backendu. Callbacki BLE są
odraczane do momentu po obsłudze radia, więc kod aplikacji nigdy nie działa pod
tą blokadą. Obrazy BLE-only, Classic-only i połączone BLE+Classic używają tego
samego hosta Bluetooth z licznikami referencji. Aktywna koegzystencja
gamepad+BLE na sprzęcie nie jest jeszcze bramką wydania.

Bramka aktywnej koegzystencji Pico 2 W z 2026-08-25 utrzymała
uwierzytelnione połączenie Stream aktywne, podczas gdy ruch MQTT wymuszał
rozłączenie i ponowne połączenie WiFi. Zarówno bare-metal, jak i FreeRTOS
utrzymały 10,00 echa BLE/s przez ponad 607 s bez żadnej straty. Bare-metal
ukończył 6079/6079 ech (średnie opóźnienie 94,7 ms, maksymalne 249,0 ms);
FreeRTOS ukończył 6077/6077 (średnie 93,7 ms, maksymalne 204,1 ms). Każde
uruchomienie przeniosło 34 echa BLE przez okno ponownego połączenia WiFi,
odtworzyło WiFi i MQTT, zachowało obie referencje runtime
radia i nie zaraportowało żadnych błędów BLE, Stream, MQTT, HCI, kolejki ani
zdarzeń. Zmierzony maksymalny czas odpytywania BLE wyniósł 4,768 ms dla
bare-metal i 5,618 ms dla FreeRTOS. Finalne powtórzenie FreeRTOS użyło
wzmocnionej wyroczni postępu MQTT: jej delta obserwacji wyniosła 5794 echa
przy 9,66 Hz, z zerową liczbą stagnujących podsumowań jednosekundowych.

<a id="license-and-distribution-boundary"></a>

## Granica licencji i dystrybucji

Firmware Bluetooth linkuje BlueKitchen BTstack z dokładnej wersji zapisanej w
`third_party/btstack_version.conf`. Śledzone są dwa odrębne teksty
licencyjne:

- standardowa licencja BlueKitchen
  [`third_party/LICENSE.BTstack`](../../../third_party/LICENSE.BTstack)
  zezwala na redystrybucję, użycie i modyfikację wyłącznie dla osobistej
  korzyści, a nie w celach komercyjnych lub zarobkowych. Jej warunki
  redystrybucji źródłowej i binarnej wymagają zachowania lub odtworzenia
  informacji o prawach autorskich, warunków i zastrzeżenia w sposób
  określony w tym tekście;
- osobna licencja Raspberry Pi
  [`src/hal/bluetooth/LICENSE.RP`](../../../src/hal/bluetooth/LICENSE.RP)
  stosuje się do `Customer`, zdefiniowanego jako nabywca wymienionego
  `Product`. Zezwala temu Customerowi na użycie, modyfikację, integrację i
  dystrybucję BTstack wyłącznie z zdefiniowanymi `Products` lub `Customer
  Products`. Wymienione Products to Pico W, Pico WH, Pico 2 W, Pico 2 WH
  oraz RM2; Customer Products to produkty wytwarzane lub dystrybuowane
  przez Customerów, które używają tych Products lub są od nich pochodne.
  Jest to licencja ograniczona do konkretnych produktów, a nie ogólne
  zezwolenie dla każdej płytki lub urządzenia zawierającego kontroler
  CYW43.

Właściwa licencja zależy od fizycznego produktu i jego dystrybucji.
Przejrzyj kompletne, śledzone teksty licencyjne i spełnij warunki licencji,
na którą się powołujesz; zastosowania wykraczające poza nie mogą wymagać
osobnej licencji BlueKitchen. Ta sekcja jest inwentarzem technicznym, a nie
poradą prawną. Te warunki dotyczą artefaktów z włączonym BTstack, a nie buildu
JaszczurHAL, które go nie kompilują.

Zobacz kompilowalny [przykład `26_ble_stream`](../../../examples/26_ble_stream/)
po kompletny przepływ startu Peripheral i advertisingu wraz z
uwierzytelnionym odbiorcą strumienia. Wieloplatformowa
[bramka sprzętowa `bluetooth_stream`](03_build_tests.md#bramka-sprzętowa-jh-ble-stream-v1)
prowadzi kompletny protokół z niezależnego klienta BlueZ.

[Przykład `29_bluetooth_gamepad`](../../../examples/29_bluetooth_gamepad/)
pokazuje przepływ snapshotów Classic HID i wariant buildu BLE+Classic.
