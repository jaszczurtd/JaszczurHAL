# API Bluetooth Low Energy i Bluetooth Classic

*Dostępne również [po angielsku](../en/20_bluetooth.md).*

> **Część [Dokumentacji API JaszczurHAL](../../pl/JaszczurHAL_API.md)**

Moduły Bluetooth są opcjonalne. `HAL_ENABLE_BLE` udostępnia przez
`hal/bluetooth/hal_ble.h` API Bluetooth Low Energy dla roli Peripheral oraz
pasywnego Observera. `HAL_ENABLE_BLUETOOTH_CLASSIC` dodaje wykrywanie,
parowanie, SDP i zarządzanie zapisanymi peerami. `HAL_ENABLE_BLUETOOTH_HID_HOST`
dodaje surowe deskryptory i raporty Classic HID, a
`HAL_ENABLE_BLUETOOTH_GAMEPAD` - adapter normalizujący stan gamepada.
`HAL_ENABLE_BLUETOOTH_A2DP_SINK` dodaje odbiór dźwięku SBC, a
`HAL_ENABLE_BLUETOOTH_AVRCP_TARGET` - sterowanie głośnością bezwzględną.
Zależności mają kierunek gamepad -> HID Host -> Classic oraz AVRCP Target ->
A2DP Sink -> Classic. Wszystkie API są dostępne również przez zbiorczy
nagłówek `JaszczurHAL.h`.

Bieżące wydanie obsługuje jedno połączenie Peripheral, pakiety advertising
typu legacy z możliwością połączenia oraz ich pasywne skanowanie w trybie
Observer. Raporty advertising są kopiowane do kolejki, a struktury AD można
parsować. API przekazuje zdarzenia kontrolera i połączenia, udostępnia ATT MTU
oraz korzysta ze statycznej bazy GATT z obowiązkowymi usługami GAP i GATT.

Nie ma jeszcze aktywnego skanowania ani wysyłania żądań Scan Response,
dowolnie definiowanych charakterystyk aplikacji, klienta GATT, parowania czy
bondingu. Opcjonalny profil `HAL_ENABLE_BLE_STREAM` dodaje jedną stałą,
uwierzytelnioną usługę aplikacyjną z powiadomieniami. Flaga
`HAL_ENABLE_BLE_COMMANDS` przeznacza dane tej usługi wyłącznie dla wspólnego
routera poleceń; nie dodaje klienta GATT.

## Obsługiwane profile

| API | Target | Płytka | Radio/host | Walidacja |
|---|---|---|---|---|
| BLE | `rp2040` | `picow` | wbudowany CYW43439 z BTstack | zaliczone testy sprzętowe Observera oraz Stream w trybach bare metal i FreeRTOS |
| BLE | `rp2350-arm` | `pico2w` | wbudowany CYW43439 z BTstack | zaliczone testy Observera, Stream w trybach bare metal i FreeRTOS oraz aktywnej współpracy Stream+WiFi/MQTT |
| BLE | `rp2040` | `pico-rm2` | zewnętrzny CYW43439 PIM730/RM2 przez PIO | build potwierdza obsługę; dedykowany test sprzętowy oczekuje na wykonanie |
| BLE | `stm32g474` | `nucleo-g474re-pim730` | zewnętrzny CYW43439 PIM730/RM2 przez gSPI | zaliczone testy Peripheral i Observer oraz pełne testy obciążeniowe Stream z wyświetlaczem w trybach bare metal i FreeRTOS |
| BLE | `esp32s3` | `waveshare-esp32-s3-zero` | zintegrowany kontroler LE z ESP-IDF NimBLE | pełny test kompilacji i linkowania; test radia na sprzęcie oczekuje na wykonanie |
| Classic / HID Host / gamepad | `rp2040` / `rp2350-arm` / `stm32g474` | profile Bluetooth wymienione wyżej | CYW43439 z BTstack | zaliczone na `pico2w` bramki gamepada Zero 2 i ogólnego Classic z XY-BT; fixture myszy na Pico W zaliczył także bramkę deskryptora/raportów HID innej klasy z hostem Pico 2 W |
| Classic / HID Host / gamepad | `esp32` | `esp32-devkitc-v4` | zintegrowany kontroler BR/EDR z ESP-IDF Bluedroid i ESP HID Host | pełny test kompilacji i linkowania; ogólna bramka radia na sprzęcie oczekuje |
| A2DP Sink / AVRCP Target | `rp2040` / `rp2350-arm` | `picow` / `pico2w` | wbudowany CYW43439 z BTstack i dekoderem SBC Bluedroid | obsługa potwierdzona buildem i deterministycznymi testami kodeka/runtime na hoście; produkt wymaga testu ze źródłem dźwięku i właściwym wyjściem audio |
| BLE i profile Classic | `mock` | `host-mock` | deterministyczne backendy testowe | testy hostowe Classic, HID innej klasy i gamepada |

Backend RP2350 obsługuje wyłącznie Pico 2 W z targetem `rp2350-arm`. Pico 2 W
z `rp2350-riscv` jest nieobsługiwane, ponieważ transport Bluetooth CYW43 nie
jest włączony dla tego targetu. `HAL_ENABLE_BLE` i
`HAL_ENABLE_BLUETOOTH_CLASSIC` powodują błąd buildu, gdy wymagany transport
jest niedostępny. Sprawdzenia wykonywane w runtime rozróżniają
`HAL_BOARD_CAP_BLUETOOTH_LE_CONTROLLER` i
`HAL_BOARD_CAP_BLUETOOTH_CLASSIC_CONTROLLER`; starszy ogólny bit Bluetooth
pozostaje dostępny dla zgodności. Moduły zewnętrzne wymagają dodatkowo
`HAL_BOARD_CAP_EXTERNAL_RADIO_FRONTEND`.

ESP32-S3 obsługuje obecnie bazowe API BLE Peripheral/Observer, ale nie
`HAL_ENABLE_BLE_STREAM`, klienta GATT ani Classic. Oryginalny ESP32 obsługuje
manager Classic, HID Host i adapter gamepada, ale nie włącza publicznego API
BLE. Są to jawne ograniczenia poszczególnych targetów, a nie fallbacki
wybierane w runtime.

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

`hal_ble_initialize()` uruchamia backend wybranego targetu i kończy działanie,
gdy żądanie uruchomienia zostanie przyjęte. Kontroler staje się gotowy
asynchronicznie, co sygnalizuje zdarzenie `HAL_BLE_EVENT_CONTROLLER_READY`.
Po pomyślnym uruchomieniu kolejne wywołania inicjalizacji i deinicjalizacji są
idempotentne.
Deinicjalizacja unieważnia wszystkie uchwyty połączeń i advertisingu, czyści
kolejkę zdarzeń oraz wyrejestrowuje funkcję zwrotną.

Wywołuj `hal_ble_poll()` często z jednego zadania lub z pętli kooperacyjnej.
Funkcja obsługuje kontroler, zwalnia blokadę radia backendu, a dopiero potem
wywołuje funkcje zwrotne. Próba ponownego wywołania `hal_ble_poll()`, zmiany
funkcji zwrotnej lub deinicjalizacji z jej poziomu kończy się `HAL_EBUSY`.
Zapytania tylko do odczytu są dozwolone.

## Zdarzenia

`HAL_BLE_EVENT_QUEUE_DEPTH` określa pojemność kolejki przechowującej kopie
zdarzeń; domyślna wartość to 8. Publiczne zdarzenia to:

- `HAL_BLE_EVENT_CONTROLLER_READY`;
- `HAL_BLE_EVENT_ADVERTISING_STARTED` oraz
  `HAL_BLE_EVENT_ADVERTISING_STOPPED`;
- `HAL_BLE_EVENT_CONNECTED` oraz `HAL_BLE_EVENT_DISCONNECTED`;
- `HAL_BLE_EVENT_MTU_UPDATED`;
- `HAL_BLE_EVENT_SCAN_STARTED`, `HAL_BLE_EVENT_SCAN_STOPPED` oraz
  `HAL_BLE_EVENT_SCAN_REPORT_AVAILABLE`;
- `HAL_BLE_EVENT_ERROR`.

Wybierz jeden sposób odbierania zdarzeń: zarejestruj funkcję zwrotną wywoływaną
przez `hal_ble_poll()` albo pobieraj je przez `hal_ble_event_next()`. Oba
mechanizmy korzystają z tej samej kolejki. Gdy jest pusta,
`hal_ble_event_next()` zwraca `HAL_EAGAIN`. Po zapełnieniu kolejki nowe
zdarzenia są odrzucane, licznik
`hal_ble_info_t::dropped_events` wzrasta, a następne wywołanie
`hal_ble_poll()` zwraca `HAL_EOVERFLOW`. BLE nadal pozostaje aktywne.

Zdarzenie gotowości nie zawiera adresu drugiej strony połączenia; wywołaj
`hal_ble_get_local_address()` po jego otrzymaniu. Zdarzenie połączenia
zawiera jej adres i nowy nieprzezroczysty uchwyt połączenia.
Zdarzenia MTU i rozłączenia odnoszą się do tego samego uchwytu.

## Advertising

`hal_ble_advertising_start()` kopiuje całą konfigurację przed zakończeniem
wywołania. Dane advertisingowe typu legacy muszą zawierać od 1 do 31 bajtów.
Minimalny interwał musi mieścić się między `0x0020` a `0x4000`
jednostek (od 20 ms do 10,24 s), a maksymalny musi być co najmniej równy
minimalnemu i nie większy niż `0x4000`.

`HAL_OK` oznacza, że żądanie zostało przyjęte. Na potwierdzenie uruchomienia
poczekaj na `HAL_BLE_EVENT_ADVERTISING_STARTED`.
Jeśli advertising zostanie zlecony przed osiągnięciem gotowości przez kontroler,
rozpocznie się później automatycznie. Udane połączenie wstrzymuje go, a rozłączenie
uruchamia go ponownie, dopóki pierwotne żądanie pozostaje aktywne. Aby je
zatrzymać, przekaż nieprzezroczysty uchwyt advertisingu. Po
`HAL_BLE_EVENT_DISCONNECTED` nie wysyłaj kolejnego żądania uruchomienia;
automatyczne wznowienie wynika już z pierwotnego żądania.

## Pasywne skanowanie Observer

`hal_ble_scan_start()` akceptuje interwał, okno oraz opcjonalny filtr
duplikatów. Obie wartości czasowe używają jednostek Bluetooth 0,625 ms i
muszą mieścić się między `HAL_BLE_SCAN_INTERVAL_MIN` a
`HAL_BLE_SCAN_INTERVAL_MAX`; okno nie może przekraczać interwału. `HAL_OK`
oznacza, że żądanie zostało zaakceptowane. Poczekaj na
`HAL_BLE_EVENT_SCAN_STARTED`, które potwierdza uruchomienie skanowania.

Skanowanie jest pasywne i odbiera wyłącznie pakiety advertising typu legacy.
Nie wysyła pakietów Scan Request, nie inicjuje połączeń, nie paruje i nie udostępnia
klienta GATT. W obecnej implementacji Observera skanowanie nie może działać
jednocześnie z advertisingiem ani połączeniem Peripheral. Sprzeczne żądania
uruchomienia zwracają `HAL_EBUSY`.

Raporty są kopiowane do osobnej kolejki o stałej pojemności, określanej przez
`HAL_BLE_SCAN_REPORT_QUEUE_DEPTH`; domyślna wartość to 8. Zdarzenie
`HAL_BLE_EVENT_SCAN_REPORT_AVAILABLE` oznacza, że co najmniej jeden raport
można odczytać za pomocą `hal_ble_scan_report_next()`. Opróżnij wszystkie
dostępne raporty po tym zdarzeniu. Wywołanie zwraca `HAL_EAGAIN`, gdy
kolejka jest pusta. Jeśli część raportów została odrzucona, funkcja najpierw
zwraca `HAL_EOVERFLOW`, informując o utracie danych. Wywołaj ją ponownie, aby
odczytać najstarszy raport pozostały w kolejce. `hal_ble_info_t` zawiera łączną
liczbę utraconych raportów oraz liczbę zgłoszeń utraty oczekujących na
potwierdzenie przez aplikację.

Każdy raport zawiera własną kopię adresu, RSSI, typ zdarzenia zgodny z formatem
legacy oraz do 31 bajtów danych. `hal_ble_advertising_field_next()` pozwala bez
alokowania pamięci przejść kolejno przez struktury AD poprzedzone długością.
Zacznij od przesunięcia równego zero. `HAL_EAGAIN` oznacza koniec danych, a `HAL_EIO`
sygnalizuje nieprawidłowy format wejścia. Dane zwróconego pola znajdują się
w obiekcie raportu i pozostają ważne przez cały czas jego istnienia.

### Przykład Observer

Poniższa pętla uruchamia pasywne skanowanie z interwałem 60 ms i oknem 30 ms,
a następnie odczytuje wszystkie raporty pozostałe w kolejce. Zastąp
`consume_ad_field()` kodem aplikacji obsługującym potrzebne typy AD, na
przykład pełną nazwę lokalną (`0x09`) lub dane producenta (`0xff`).

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

Obsługiwane jest tylko jedno połączenie Peripheral. Uchwyty połączenia
i advertisingu są niezerowe i nieprzezroczyste. Tracą ważność po zdarzeniu
kończącym ich cykl życia, po deinicjalizacji oraz po awarii kontrolera.
Przekazanie nieaktualnego uchwytu powoduje zwrócenie `HAL_ENOENT`.

`hal_ble_disconnect()` umieszcza w kolejce żądanie lokalnego rozłączenia.
Jego zakończenie sygnalizuje `HAL_BLE_EVENT_DISCONNECTED`.
`hal_ble_get_mtu()` zwraca 23 do czasu, aż wybrany stos przekaże wynegocjowaną
wartość w zdarzeniu `HAL_BLE_EVENT_MTU_UPDATED`.

## Zachowanie backendów ESP-IDF

ESP32-S3 używa NimBLE do implementacji podstawowego API LE. Funkcje zwrotne
ESP-IDF kopiują adresy, dane advertisingowe, stan połączenia i zmiany MTU do
kolejek HAL o stałej pojemności. Funkcje zwrotne aplikacji nadal są wywoływane
wyłącznie z `hal_ble_poll()`, a nigdy z zadania zdarzeń ESP-IDF.

Oryginalny ESP32 używa Bluedroid do ogólnego inquiry Classic, SDP, parowania i
usuwania natywnego bondingu oraz `esp_hidh`, gdy wybrano HID Host. Wspólny
backend nie filtruje nazwy, Class of Device, VID/PID, deskryptora ani raportów.
Wyłącznie adapter gamepada stosuje własną politykę urządzenia i parser
deskryptora. PIN `0000` i potwierdzenie SSP pozostają oczekujące, dopóki
aplikacja jawnie ich nie zatwierdzi albo nie odrzuci.

Oba backendy ESP oraz backend sieciowy korzystają z jednego wspólnego,
idempotentnego inicjalizatora NVS. Niezgodna lub pełna partycja NVS powoduje
zwrócenie `HAL_ECONFIG`; HAL nie usuwa automatycznie danych aplikacji. Parser
HAL, kolejki zdarzeń i kolejka stanów gamepada mają stałą pojemność. NimBLE,
Bluedroid, pętla zdarzeń i host HID z ESP-IDF mogą wewnętrznie przydzielać
pamięć dynamicznie.

## Model statusu i niepowodzeń

API używa `hal_status_t` wszędzie. Typowe wyniki to:

| Status | Znaczenie |
|---|---|
| `HAL_OK` | synchroniczne zapytanie powiodło się lub polecenie asynchroniczne zostało zaakceptowane |
| `HAL_EUNINIT` | BLE nie zostało zainicjalizowane |
| `HAL_EAGAIN` | informacja o gotowości lub dane zdarzenia nie są jeszcze dostępne |
| `HAL_EBUSY` | żądania są ze sobą sprzeczne albo funkcja zwrotna próbuje ponownie uruchomić odpytywanie |
| `HAL_ENOENT` | nieaktualny lub nieznany nieprzezroczysty uchwyt |
| `HAL_EOVERFLOW` | ograniczona kolejka zdarzeń lub raportów skanowania odrzuciła dane |
| `HAL_EUNSUPPORTED` | wybrana płytka nie ma wymaganego sprzętu radiowego |
| `HAL_EHW` / `HAL_EIO` | awaria kontrolera lub transportu |

`hal_ble_get_info()` zwraca spójny zestaw informacji o stanie podsystemu:
adres lokalny i adres drugiej strony połączenia, bieżące uchwyty, numer
generacji, ostatni status,
MTU, stan skanowania, liczbę oczekujących raportów oraz oba liczniki odrzuceń.
Krytyczny błąd kontrolera lub transportu przenosi podsystem do
`HAL_BLE_STATE_FAILED`, unieważnia jego uchwyty, zatrzymuje skanowanie
i zwiększa numer generacji.

## Manager Bluetooth Classic i profile

### Manager Classic

`HAL_ENABLE_BLUETOOTH_CLASSIC` dodaje `hal_bluetooth_classic.h`. Otwórz jeden
nieprzezroczysty manager, często wywołuj `hal_bluetooth_classic_poll()` i
zamknij wszystkie dołączone profile przed zamknięciem managera. Wyniki inquiry
są kopiowane do ograniczonej kolejki i zawierają BD_ADDR, ograniczoną nazwę,
Class of Device, opcjonalne RSSI oraz maskę usług SDP.
`hal_bluetooth_classic_sdp_query()` aktualizuje wykrytego peera przez tę samą
kolejkę. `HAL_EOVERFLOW` potwierdza utratę wyników; kolejne wywołania zwracają
zachowane rekordy.

Parowanie wymaga jawnej decyzji. `hal_bluetooth_classic_pair()` rozpoczyna
bonding tam, gdzie backend udostępnia żądanie niezależne od profilu. Oczekujące
Just Works, PIN lub passkey jest widoczne w `hal_bluetooth_classic_info_t`;
aplikacja musi wywołać `hal_bluetooth_classic_pairing_authorize()` albo
`hal_bluetooth_classic_pairing_reject()`. Autoryzacja powinna następować po
zaufanym lokalnym geście. Nazwa, adres ani nieuwierzytelniona wymiana nie są
dowodem tożsamości użytkownika.

Profil przyjmujący połączenia może zamiast tego użyć
`hal_bluetooth_classic_pairing_window_open()`. Podczas ograniczonego czasowo
okna urządzenie jest connectable i discoverable, a aplikacja może zatwierdzić
zgłoszone żądanie parowania. Po upływie czasu pozostaje connectable dla
odtworzonych peerów, przestaje być discoverable i odrzuca próby nieznanych
urządzeń. Wspólną nazwę lokalną i 24-bitowy Class of Device ustaw przez
`hal_bluetooth_classic_set_identity()` przed otwarciem okna.

`hal_bluetooth_classic_open_ex()` przyjmuje indeksowany
`hal_bluetooth_classic_bond_provider_t`. Każdy nieprzezroczysty rekord zawiera
jednego peera, wersję formatu, identyfikator reguł weryfikacji profilu,
sekwencję, typ link key, jeden link key oraz CRC. Manager jest jedynym
właścicielem trwałej kopii link key. Provider przechowuje rekordy bez ich
interpretowania i może używać dowolnego nośnika. Opcjonalny
`jh_bluetooth_classic_bond_kv_provider()` mapuje kolejne sloty na kolejne
klucze `hal_kv`. Niepoprawne rekordy są pomijane podczas odtwarzania. Profil
woła `hal_bluetooth_classic_peer_save()` dopiero po zweryfikowaniu peera i
przepływu danych; manager zapisuje go później z `poll()`, poza callbackami
stosu. Peerów można wyliczać lub usuwać po adresie.

Na BTstack/CYW43 link keys są kopiowane ze zdarzenia HCI i odtwarzane do
ograniczonej bazy BTstack. Bluedroid zapisuje rzeczywiste link keys we własnym
NVS i ich nie udostępnia. Dlatego przenośny provider zwraca
`HAL_EUNSUPPORTED` na oryginalnym ESP32; wyliczanie w RAM, natywny reconnect i
`hal_bluetooth_classic_peer_forget()` pozostają dostępne. Ogólne
`hal_bluetooth_classic_pair()` także nie jest tam obsługiwane - uwierzytelnienie
rozpoczyna połączenie wybranego profilu.

`hal_bluetooth_classic_peer_forget_all()` jest wspólnym punktem factory reset
dla produktów z wieloma profilami. Dla każdego peera kasuje slot providera
przed odpowiadającym mu natywnym bondem. Błąd pamięci pozostawia stan tego
peera w runtime, aby operację można było powtórzyć.

### A2DP Sink i AVRCP Target

`HAL_ENABLE_BLUETOOTH_A2DP_SINK` dodaje `hal_bluetooth_a2dp_sink.h` i implikuje
manager Classic. Jeden Sink dołącza do jednego otwartego managera i odbiera SBC
44,1 lub 48 kHz w trybie mono, stereo albo joint stereo. Publiczne API nie
zawiera typów BTstack ani drivera audio. Zwraca przeplatany PCM signed 16-bit w
wynegocjowanej liczbie kanałów albo mono z saturacją po wybraniu
`HAL_BLUETOOTH_A2DP_OUTPUT_MONO`.

Callbacki stosu jedynie kopiują kompletne pakiety mediów do ograniczonej
kolejki. Wywołuj `hal_bluetooth_classic_poll()`, aby obsłużyć wspólny kontroler,
a następnie `hal_bluetooth_a2dp_sink_poll()`, aż zwróci `HAL_EAGAIN`. Parsowanie,
dekodowanie SBC, programowa głośność, downmix i małe korekty zegara odbywają się
w tym kontekście. `hal_bluetooth_a2dp_sink_pcm_next()` stosuje stały prebuffer
po starcie strumienia i po underrunie. Aplikacja odpowiada za fizyczne wyjście i
powinna przenosić PCM do własnych gotowych buforów; przerwanie DMA powinno tylko
wybrać gotowy bufor albo ciszę.

`hal_bluetooth_a2dp_sink_info_t` podaje format i stan strumienia, straty
pakietów, poziomy i high-water marks ograniczonych kolejek pakietów/PCM,
odrzucenia, uszkodzone ramki, przepełnienia/underruny PCM oraz bieżącą korektę
zegara. Nacisk na kolejkę nigdy nie powoduje nieograniczonej alokacji. Nowy peer
jest zapisywany przez wspólnego providera bondingu Classic dopiero po lokalnej
autoryzacji parowania, przechwyceniu link key i poprawnym zdekodowaniu pierwszej
ramki SBC. Identyfikator profilu to
`HAL_BLUETOOTH_A2DP_SINK_PROFILE_ID`.

`HAL_ENABLE_BLUETOOTH_AVRCP_TARGET` dodaje
`hal_bluetooth_avrcp_target.h` i implikuje A2DP Sink. Minimalny Target przyjmuje
od Controllera bezwzględną głośność od 0 do 127, zastępuje oczekującą zmianę
najnowszą wartością i potrafi zgłosić bieżącą wartość lokalną subskrybującemu
Controllerowi. Dzieli połączenie i bond A2DP/Classic; nie tworzy drugiego
zapisanego klucza. Zamykaj kolejno AVRCP, A2DP i Classic. Kompletny konsument w
C oraz adapter wyjścia PWM/DMA znajdują się w
[`examples/30_bluetooth_speaker`](../../../examples/30_bluetooth_speaker/).

### Ogólny HID Host

`HAL_ENABLE_BLUETOOTH_HID_HOST` dodaje `hal_bluetooth_hid_host.h` i implikuje
manager Classic. Jeden uchwyt HID Host dołącza do otwartego managera i
obsługuje jedno aktywne połączenie HID. Udostępnia skopiowany deskryptor
raportów oraz ograniczoną kolejkę surowych raportów Input, Output i Feature,
bez interpretowania klasy urządzenia. Aplikacja może wysyłać raporty
Output/Feature oraz żądać raportów Input/Feature. Wybór urządzenia i walidacja
deskryptora należą do aplikacji lub adaptera profilu. Zamknięcie HID rozłącza
aktywne łącze, ale pozostawia manager Classic otwarty.

Deterministyczny mock potrafi wstrzykiwać gotowość Classic, wyniki inquiry/SDP,
parowanie, link keys, ogólne deskryptory i surowe raporty. Test hostowy używa
deskryptora myszy, aby dowieść braku filtra gamepada we wspólnej ścieżce HID.
Warianty `classic-scan` i `hid-host` projektu
[`examples/29_bluetooth_gamepad`](../../../examples/29_bluetooth_gamepad/)
kompilują te warstwy bez `HAL_ENABLE_BLUETOOTH_GAMEPAD`.

### Adapter gamepada

API gamepada jest adapterem managera Classic i ogólnego HID Host. Wewnętrznie
posiada ich uchwyty i zwraca jeden
nieprzezroczysty uchwyt `hal_gamepad_t`. `hal_gamepad_open()` uruchamia profil
asynchronicznie. Od tego momentu trzeba często wywoływać `hal_gamepad_poll()`
z jednego zadania lub z pętli kooperacyjnej. `hal_gamepad_get_info()` zwraca
stan publiczny, ostatni status, bieżący numer generacji połączenia, flagi
parowania, informację o zapisanym urządzeniu oraz dane diagnostyczne kolejki
o ograniczonej pojemności.

Publiczne stany to `UNINITIALIZED`, `STARTING`, `READY`, `DISCOVERING`,
`CONNECTING`, `CONNECTED` i `FAILED`. Krytyczna awaria kontrolera lub transportu
przenosi profil do `FAILED`. `hal_gamepad_close()` zatrzymuje profil i
czyści wybrane urządzenie oraz unieważnia jego uchwyt.

#### Parowanie i ponowne łączenie

Parowanie jest sterowane przez aplikację i ma ograniczony czas trwania. Gdy
profil osiągnie `READY`, wywołaj `hal_gamepad_pairing_open()`, aby rozpocząć okno
wykrywania. Jest to dozwolone także wtedy, gdy istnieje znane urządzenie, co
pozwala aplikacji je zastąpić. Kiedy
`hal_gamepad_info_t::pairing_pending` zostanie ustawione, aplikacja może wywołać
`hal_gamepad_pairing_authorize()` i zaakceptować Just Works albo legacy PIN
`0000`. Nieobsługiwane procedury wymagające passkey są odrzucane.
Zaakceptowany adres identyfikuje urządzenie, z którym
`hal_gamepad_reconnect()` łączy się w bieżącej sesji otwartego profilu. Bez
providera bondingu (patrz niżej) klucz połączenia (`link key`) trzymany jest
tylko w RAM stosu: zamknięcie profilu lub ponowne uruchomienie firmware usuwa
tożsamość wybraną przez HAL, dlatego aplikacja musi być przygotowana na
ponowne otwarcie okna parowania.

Otwarcie okna parowania jest jawną decyzją autoryzacyjną. Produkt powinien je
udostępnić dopiero po lokalnej akcji użytkownika i nie powinien traktować nazwy
urządzenia, adresu Bluetooth ani nieuwierzytelnionej wymiany Just Works jako
dowodu tożsamości użytkownika.

#### Trwały bonding

`hal_gamepad_open_ex(&handle, bond_provider)` przyjmuje opcjonalny
`hal_gamepad_bond_provider_t` -- funkcje `load()`/`store()`/`erase()` nad
nieprzezroczystym, stałej wielkości `hal_gamepad_bond_blob_t`.
`hal_gamepad_open()` jest równoważne `hal_gamepad_open_ex(&handle, NULL)` i
zachowuje dotychczasowe zachowanie tylko-w-RAM. Ten stary jednoslotowy provider
jest mostkowany do indeksowanego managera Classic. Gamepad decyduje, kiedy
spełniono jego warunki profilu, a manager odpowiada za kodowanie, link keys i
moment zapisu:

```c
#include <hal/bluetooth/hal_gamepad.h>
#include <hal/bluetooth/jh_gamepad_bond_kv_provider.h>

hal_gamepad_t gamepad;
jh_gamepad_bond_kv_context_t bond_context;
const hal_gamepad_bond_provider_t provider =
    jh_gamepad_bond_kv_provider(&bond_context, MY_BOND_KV_KEY);
hal_gamepad_open_ex(&gamepad, &provider);
```

`jh_gamepad_bond_kv_provider()` (zadeklarowany w
`hal/bluetooth/jh_gamepad_bond_kv_provider.h`, aktywny gdy włączone są
zarówno `HAL_ENABLE_BLUETOOTH_GAMEPAD`, jak i `HAL_ENABLE_KV`) to gotowy
adapter nad `hal_kv_set_blob_ex()`/`get_blob_ex()`/`delete_ex()`; konsument,
który chce innego trwałego nośnika, implementuje bezpośrednio trzy funkcje
providera. `hal_kv_init_ex()` musi się już powieść przed użyciem providera i
pozostać zainicjalizowane tak długo, jak długo profil gamepada jest otwarty.
Należący do wywołującego `bond_context` musi być ważny przez ten sam czas.

Nowy peer gamepada trafia do wspólnego rekordu Classic dopiero po pełnej
akceptacji -- lokalnej
autoryzacji parowania, dopasowanej tożsamości, przyjętym deskryptorze
raportów, co najmniej jednym raporcie HID (dowód, że łącze faktycznie
przesyła dane) i przechwyconym kluczu połączenia. Do tego momentu
dotychczasowy bond, jeśli istnieje, pozostaje aktywny. `store()`/`erase()` są
wołane wyłącznie z `hal_gamepad_poll()`, po powrocie backendu z dowolnego
callbacka stosu Bluetooth i po zwolnieniu radio locka -- nigdy z wnętrza
callbacka stosu ani pod lockiem. Przy `hal_gamepad_open_ex()` zapisany blob
jest walidowany (magic, wersja formatu, CRC oraz reguły weryfikacji peera
wpisane w działający firmware) zanim jego klucz połączenia zostanie
zainstalowany w kontrolerze; strukturalnie niepoprawny blob albo taki, który
powstał pod starymi regułami, jest traktowany dokładnie jak "brak bondingu",
a nie jak zaufany.

`hal_gamepad_forget()` to punkt wejścia dla factory reset: rozłącza aktywne
łącze, czyści znanego peera w kontrolerze i w RAM oraz usuwa zapisany blob
przez provider bondingu (no-op, gdy providera nie podano). Kolejne
`hal_gamepad_pairing_open()` rozpoczyna świeże parowanie.
Manager najpierw usuwa rekord z trwałego storage, a dopiero potem zapomina
peera w runtime. Jeśli provider zwróci błąd, peer pozostaje znany, dzięki czemu
aplikacja może ponowić factory reset, a stary bond nie wróci po restarcie.

Zachowanie link keys poszczególnych backendów opisuje sekcja managera Classic.
`hal_gamepad_forget()` deleguje do niego usunięcie natywnego bondingu i rekordu
providera.

#### Znormalizowany stan wejść

Parser raportów HID nie zależy od BTstack ani ESP-IDF. Sprawdza deskryptor
raportów o ograniczonym rozmiarze i na każdym backendzie wypełnia ten sam
znormalizowany model stanu wejść. Poprawnie zapisane długie elementy HID
z nieobsługiwanymi tagami są pomijane; ucięty długi element powoduje
odrzucenie deskryptora.

`hal_gamepad_snapshot_t` zawiera generację połączenia, 32-bitową maskę
przycisków, dziewięć osi Generic Desktop, maskę obecności osi, maskę kierunku
D-pada i stan połączenia. Bit przycisku 0 odpowiada HID Button 1. Osie są
normalizowane do `-32767..32767` i indeksowane przez `HAL_GAMEPAD_AXIS_*`; dla
nieobsługiwanych osi bit w `axes_present` jest wyzerowany. Maska D-pada jest
kombinacją `HAL_GAMEPAD_DPAD_UP`, `RIGHT`, `DOWN` i `LEFT`.

`hal_gamepad_snapshot()` odczytuje najnowszy stan bez usuwania go.
`hal_gamepad_snapshot_next()` pobiera zmiany z kolejki o stałym rozmiarze
`HAL_GAMEPAD_SNAPSHOT_QUEUE_DEPTH`. Zwraca `HAL_EAGAIN`, gdy kolejka jest pusta.
Jeśli utracono stany pośrednie, najpierw zwraca `HAL_EOVERFLOW`; wywołaj funkcję
ponownie, aby pobrać najnowszy zachowany stan. Połączenie i rozłączenie również
dodają stan do kolejki. Stan utworzony po rozłączeniu zeruje wszystkie wejścia,
dzięki czemu po utracie połączenia aplikacja nie traktuje żadnego elementu jako
nadal wciśniętego.
`hal_gamepad_disconnect()` jedynie przyjmuje żądanie, a zakończenie jest
asynchroniczne. Aplikacja musi sprawdzać stan profilu lub kolejne rekordy stanu
wejść. Nie może zakładać, czy backend zakończy operację przed następnym
wywołaniem `hal_gamepad_poll()`, czy podczas niego.

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

Deterministyczny mock obsługuje zarówno zgodnościowe wstrzykiwanie
znormalizowanego stanu, jak i pełną ścieżkę Classic -> surowy HID -> parser.
Kompletny przykład w C, izolowane warianty Classic/HID oraz build BLE+Classic
znajdują się w
[`examples/29_bluetooth_gamepad`](../../../examples/29_bluetooth_gamepad/).

## JH BLE Stream v1

`HAL_ENABLE_BLE_STREAM` dodaje `hal_ble_stream.h`: strumień bajtów z buforami
o stałej pojemności, przenoszony przez jedną statyczną usługę GATT. Flaga włącza
`HAL_ENABLE_BLE` oraz `HAL_ENABLE_CRYPTO`.

Włączony samodzielnie BLE Stream pozostaje ogólnym strumieniem bajtów dla
aplikacji. Osobny moduł
[`hal_ble_commands`](23_commands.md#uwierzytelniony-adapter-ble-stream)
dzieli wiadomości wspólnego binarnego formatu poleceń na fragmenty i przesyła
je jako uwierzytelnione dane Stream, a żądania przekazuje do
`hal_command_router`.
`HAL_ENABLE_BLE_STREAM` nie włącza tego zachowania ani routera;
`HAL_ENABLE_BLE_COMMANDS` włącza obie zależności i sprawia, że tylko adapter
poleceń może wysyłać oraz odbierać dane Stream.

Nagłówek jest jedynym źródłem definicji UUID-ów usługi, układu ramki oraz bitów
opisujących obsługiwane funkcje. Zmiana którejkolwiek z tych wartości wymaga
podniesienia wersji profilu.

| Element | UUID |
|---|---|
| Usługa | `B7CE0001-3C13-4FE2-801F-D71BDAB1369B` |
| RX (write, write-without-response) | `B7CE0002-3C13-4FE2-801F-D71BDAB1369B` |
| TX (notify) | `B7CE0003-3C13-4FE2-801F-D71BDAB1369B` |
| Wersja protokołu (read) | `B7CE0004-3C13-4FE2-801F-D71BDAB1369B` |
| Obsługiwane funkcje (read) | `B7CE0005-3C13-4FE2-801F-D71BDAB1369B` |

### Model bezpieczeństwa

Klient bez sesji odczytuje wersję protokołu, bitową maskę obsługiwanych funkcji
i nic więcej. Każda wymiana danych wymaga wzajemnie uwierzytelnionej sesji
opartej na unikalnym sekrecie urządzenia o długości co najmniej 256 bitów,
dostarczonym poza pasmem.

Procedura uzgadniania sesji obejmuje transkrypt zawierający nazwę profilu,
wersję protokołu, oba zestawy obsługiwanych funkcji, identyfikator sesji oraz
dwie losowe wartości nonce. Cztery odrębne domeny HMAC-SHA256 służą do
utworzenia dowodu urządzenia, dowodu klienta i dwóch kierunkowych kluczy sesji.
Ramki `DATA` chroni ChaCha20-Poly1305. Kierunek transmisji oraz ściśle rosnący
licznik są częścią zarówno wartości nonce, jak i danych uwierzytelnianych
(associated data). Odbiorca akceptuje tylko licznik większy o jeden od
poprzedniego. Powtórzenie wartości, jej
zmniejszenie albo przeskok do przodu powoduje zamknięcie sesji.

Każdy błąd bezwarunkowo zamyka sesję. Dzieje się tak po otrzymaniu błędnego
dowodu lub sfałszowanego tagu, powtórzeniu albo
zmniejszeniu licznika, zbliżeniu licznika do przepełnienia, błędzie źródła
entropii, rozłączeniu, zmianie generacji kontrolera, anulowaniu subskrypcji
lub upływie timeoutu bezczynności. Klucze kierunkowe są wtedy zerowane.
Po kolejnych błędach uwierzytelniania profil na ograniczony czas wstrzymuje
próby uwierzytelnienia (backoff) i odrzuca nowe próby uzgodnienia sesji. Zmiana
lub usunięcie sekretu unieważnia wszystkie sesje utworzone przy użyciu jego
poprzedniej wartości.

BLE Stream i Serial Session korzystają z tych samych, niezależnych od targetu
funkcji `jh_secure_random_bytes()`, `jh_secure_zeroize()` oraz
`jh_constant_time_compare()`. Bufory dowodów, nonce, transkryptu, kluczy
kierunkowych oraz oczekujących danych jawnych są czyszczone zawsze, gdy
kończy się ich użycie. BLE nie ma osobnej implementacji zerowania pamięci ani
porównywania tagów.

Adres urządzenia ani parowanie na poziomie warstwy łącza nie stanowią
autoryzacji. `Just Works` szyfruje połączenie, lecz nie chroni przed atakiem
MITM. Operacje wymagające autoryzacji muszą więc opierać się na sesji
aplikacyjnej, a nie tylko na połączeniu BLE.

### ATT MTU

Każda ramka jest przesyłana w pojedynczym zapisie lub powiadomieniu. Procedura
uzgadniania sesji wymaga co najmniej `HAL_BLE_STREAM_MIN_ATT_MTU`, a ramka
z danymi o pełnym rozmiarze wymaga `HAL_BLE_STREAM_FULL_PAYLOAD_ATT_MTU`.
Obserwuj `HAL_BLE_EVENT_MTU_UPDATED` i nie wysyłaj danych większych niż pozwala
wynegocjowane MTU. Próba wysłania danych, które nie mieszczą się w bieżącym
MTU, zwraca `HAL_EOVERFLOW` bez zamykania uwierzytelnionej sesji.

Odpowiedzi procedury uzgadniania i dane aplikacji oczekują w buforach o stałej
liczbie miejsc. Jeśli kontroler zwróci `HAL_EAGAIN`, ramka pozostaje w buforze,
a jej licznik kierunkowy nie jest zwiększany. Wysyłka jest ponawiana podczas
następnego odpytywania lub po zdarzeniu can-send. Powiadomienie BTstack wysyła
wyłącznie wspólna usługa radia CYW43, gdy ma założoną blokadę radia.
Stream może mieć najwyżej jedno powiadomienie przyjęte przez backend i nadal
oczekujące na zakończenie. `pending_tx` obejmuje zarówno to powiadomienie,
jak i dane oczekujące w lokalnej kolejce.

Przed przyjęciem nowego `HELLO` Stream usuwa powiadomienie, które nadal czeka
w backendzie. Jeżeli trwa właśnie lokalna operacja wysyłania lub funkcja
zwrotna informująca o jej zakończeniu, `HELLO` jest odrzucane z `HAL_EBUSY`.
Bieżąca sesja pozostaje aktywna i można ponowić żądanie. Każdy inny błąd
podczas usuwania powiadomienia zamyka sesję bez wysłania `HELLO_ACK`. Dzięki
temu ponowne
uzgodnienie kluczy w ramach tego samego połączenia nie spowoduje, że dane
z poprzedniej sesji zostaną wysłane już po danych nowej sesji.

Inicjalizacja i deinicjalizacja Stream synchronizują rejestrowanie i usuwanie
usługi GATT. Współbieżne wywołanie funkcji cyklu życia zwraca `HAL_EBUSY`.
Jeśli nie uda się zarejestrować usługi, stan Stream wraca do
`HAL_BLE_STREAM_STATE_UNINITIALIZED`.

### Przykład Stream

Zainicjalizuj podsystem BLE w pierwszej iteracji zadania aplikacji, ustaw
unikalny sekret nadany wcześniej podczas konfiguracji urządzenia, a następnie
obsługuj obie warstwy z tego samego zadania. Na targetach FreeRTOS
`app_start()` uruchamia się przed schedulerem i nie może uruchamiać CYW43.
Advertising konfiguruje się tak, jak w pokazanym wyżej przykładzie Peripheral.
Stała usługa Stream jest automatycznie dodawana do bazy danych GATT. Zadanie
Stream wymaga stosu o rozmiarze co najmniej 1024 słów. Rozmiar ten zweryfikowano
na sprzęcie i zastosowano w przykładzie oraz teście sprzętowym.

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

Po otrzymaniu `HAL_EAGAIN` przykład przechowuje najwyżej jedno echo i ponawia
jego wysłanie przed pobraniem kolejnych danych RX. Rozłączenie lub każdy
inny błąd wysyłania usuwa oczekujące echo, dzięki czemu dane ze starej sesji
nie trafią do nowej.

`hal_ble_stream_receive_ex()` zachowuje się tak samo w przypadku pustej lub
przepełnionej kolejki, a dodatkowo zwraca niezmienne informacje o pochodzeniu
pobranych danych `DATA`: numer generacji Stream, publiczny identyfikator sesji
uzgadniania oraz uwierzytelniony licznik kierunkowy. Adaptery Stream korzystają
z tych danych, aby nie łączyć fragmentów pochodzących z różnych sesji lub
zakresów liczników. Gdy metadane nie są potrzebne, można użyć prostszej funkcji
`hal_ble_stream_receive()`.

`hal_ble_stream_get_info()` zwraca stan, wynegocjowany zestaw funkcji,
publiczny identyfikator sesji, liczniki kierunkowe, niepowodzenia
uwierzytelniania, odrzucone próby ponownego użycia ramek oraz liczbę elementów
kolejki.

## Współdzielenie kontrolera Bluetooth

BLE, Bluetooth Classic i WiFi korzystają z jednego kontrolera CYW43,
transportu, runtime radia oraz blokady usługi. Aplikacja nie może być linkowana
jednocześnie z modułami Pico SDK `pico_cyw43_arch` lub `pico_btstack_cyw43`
i tym backendem. Funkcje zwrotne BLE są wywoływane dopiero po zakończeniu
obsługi radia, dlatego kod aplikacji nigdy nie działa z założoną blokadą.
Firmware korzystający tylko z BLE, tylko
z Classic albo z obu trybów używa tej samej instancji hosta Bluetooth,
zarządzanej licznikiem referencji. Aktywna współpraca gamepada i BLE na
sprzęcie nie jest jeszcze kryterium wydania.

Test aktywnej współpracy na Pico 2 W z 2026-08-25 utrzymał
uwierzytelnione połączenie Stream, podczas gdy ruch MQTT wymuszał
rozłączenie i ponowne połączenie WiFi. Zarówno bare metal, jak i FreeRTOS
utrzymały tempo 10,00 komunikatów echo BLE na sekundę przez ponad 607 s bez
żadnej straty. Tryb bare metal ukończył 6079/6079 ech (średnie opóźnienie
94,7 ms, maksymalne 249,0 ms);
FreeRTOS ukończył 6077/6077 (średnie 93,7 ms, maksymalne 204,1 ms).

W każdym uruchomieniu podczas ponownego łączenia z WiFi przesłano 34 echa BLE,
po czym przywrócono połączenia WiFi i MQTT. Odwołania obu modułów do runtime
radia pozostały aktywne, a test nie wykrył błędów BLE, Stream, MQTT, HCI,
kolejki ani zdarzeń. Maksymalny zmierzony czas jednego wywołania
`hal_ble_poll()` wyniósł
4,768 ms w trybie bare metal i 5,618 ms z FreeRTOS. W ostatnim powtórzeniu
FreeRTOS zastosowano bardziej rygorystyczne kryterium kontroli postępu MQTT.
W czasie obserwacji zarejestrowano dodatkowe 5794 echa przy częstotliwości
9,66 Hz, bez żadnego jednosekundowego podsumowania wskazującego brak postępu.

<a id="license-and-distribution-boundary"></a>

## Granica licencji i dystrybucji

Firmware z obsługą Bluetooth jest linkowany z utrzymywanego przez projekt
forka BlueKitchen BTstack w dokładnej wersji zapisanej w
`third_party/btstack_version.conf`. JaszczurHAL nie nakłada lokalnych patchy na
źródła. Repozytorium zawiera trzy istotne teksty licencyjne:

- standardowa licencja BlueKitchen
  [`third_party/LICENSE.BTstack`](../../../third_party/LICENSE.BTstack)
  zezwala na redystrybucję, użycie i modyfikację wyłącznie dla osobistej
  korzyści, a nie w celach komercyjnych lub zarobkowych. Jej warunki
  redystrybucji źródłowej i binarnej wymagają zachowania lub odtworzenia
  informacji o prawach autorskich, warunków i zastrzeżenia w sposób
  określony w tym tekście;
- osobna licencja Raspberry Pi
  [`src/hal/bluetooth/LICENSE.RP`](../../../src/hal/bluetooth/LICENSE.RP)
  ma zastosowanie do `Customer`, zdefiniowanego jako nabywca wymienionego
  `Product`. Zezwala takiemu podmiotowi `Customer` na użycie, modyfikację,
  integrację i dystrybucję BTstack wyłącznie z określonymi `Products` lub
  `Customer Products`. Wymienione Products to Pico W, Pico WH, Pico 2 W,
  Pico 2 WH oraz RM2; `Customer Products` to produkty wytwarzane lub
  dystrybuowane przez podmioty określone jako `Customer`, które używają tych
  `Products` lub są od nich pochodne.
  Jest to licencja ograniczona do konkretnych produktów, a nie ogólne
  zezwolenie dla każdej płytki lub urządzenia zawierającego kontroler
  CYW43;
- dekoder SBC Bluedroid dołączony do BTstack zachowuje informacje o prawach
  autorskich Android Open Source Project, Broadcom i Open Interface oraz
  licencję Apache-2.0. Pełny tekst znajduje się w
  [`third_party/LICENSE.BLUEDROID-SBC`](../../../third_party/LICENSE.BLUEDROID-SBC),
  a kodek ma osobny wpis w generowanym SBOM.

Właściwa licencja zależy od fizycznego produktu i jego dystrybucji.
Przeczytaj kompletne teksty licencyjne znajdujące się w repozytorium i spełnij
warunki licencji, na którą się powołujesz. Zastosowania wykraczające poza jej
zakres mogą wymagać osobnej licencji BlueKitchen. Ta sekcja jest technicznym
podsumowaniem, a nie poradą prawną. Warunki dotyczą firmware i innych plików
wynikowych zawierających BTstack, a nie buildów JaszczurHAL, które go nie
kompilują.

Można zbudować [przykład `26_ble_stream`](../../../examples/26_ble_stream/),
który pokazuje pełne uruchomienie Peripheral i advertisingu wraz
z uwierzytelnionym odbiorcą strumienia. Wieloplatformowy
[test sprzętowy `bluetooth_stream`](03_build_tests.md#bramka-sprzętowa-jh-ble-stream-v1)
wykonuje cały protokół przy użyciu niezależnego klienta BlueZ.

[Przykład `29_bluetooth_gamepad`](../../../examples/29_bluetooth_gamepad/)
pokazuje obsługę stanów wejść Classic HID i wariant buildu BLE+Classic.

[Przykład `30_bluetooth_speaker`](../../../examples/30_bluetooth_speaker/)
pokazuje cykl życia A2DP Sink, ograniczone czasowo parowanie, wspólny bond,
opcjonalną głośność bezwzględną AVRCP, diagnostykę i adapter wyjścia PWM z DMA.
