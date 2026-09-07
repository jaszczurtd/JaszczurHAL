<a id="api-bluetooth-low-energy-i-bluetooth-classic"></a>

# Bluetooth - BLE, dźwięk i urządzenia HID

*Dostępne również [po angielsku](../en/20_bluetooth.md).*

> **Część [Dokumentacji API JaszczurHAL](../../pl/JaszczurHAL_API.md)**

Obsługa urządzeń Bluetooth Low Energy i Bluetooth Classic zależy od włączonych modułów oraz platformy. `HAL_ENABLE_BLE` udostępnia rolę BLE Peripheral i pasywne skanowanie Observer przez `hal/bluetooth/hal_ble.h`. `HAL_ENABLE_BLUETOOTH_CLASSIC` włącza wykrywanie urządzeń, parowanie, SDP i zapamiętywanie sparowanych urządzeń. `HAL_ENABLE_BLUETOOTH_HID_HOST` pozwala odczytywać surowe deskryptory i raporty Classic HID, a `HAL_ENABLE_BLUETOOTH_GAMEPAD` przekształca je w ujednolicony stan gamepada. `HAL_ENABLE_BLUETOOTH_A2DP_SINK` włącza odbiór dźwięku SBC, a `HAL_ENABLE_BLUETOOTH_AVRCP_TARGET` - sterowanie głośnością bezwzględną.

Zależności są włączane w kierunku gamepad -> HID Host -> Classic oraz AVRCP Target -> A2DP Sink -> Classic. Wszystkie interfejsy są dostępne również przez `JaszczurHAL.h`.

Podstawowe API BLE obsługuje jedno połączenie Peripheral, rozgłaszanie pakietów legacy umożliwiających połączenie oraz pasywne skanowanie w roli Observer. Raporty są kopiowane do kolejki; aplikacja może odczytywać ich struktury AD. Dostępne są zdarzenia kontrolera i połączenia, informacja o ATT MTU oraz statyczna baza GATT z obowiązkowymi usługami GAP i GATT.

Podstawowe API BLE nie obsługuje aktywnego skanowania, żądań Scan Response, dowolnych charakterystyk aplikacji, klienta GATT, parowania ani zapamiętywania parowania (bondingu). `HAL_ENABLE_BLE_STREAM` dodaje jedną stałą usługę aplikacyjną z uwierzytelnianiem i powiadomieniami. `HAL_ENABLE_BLE_COMMANDS` przeznacza jej dane wyłącznie dla wspólnego routera poleceń; nie dodaje klienta GATT.

<a id="obsługiwane-profile"></a>

## Platformy i zakres weryfikacji

| API | Target | Płytka | Radio/host | Walidacja |
|---|---|---|---|---|
| BLE | `rp2040` | `picow` | wbudowany CYW43439 z BTstack | zaliczone testy sprzętowe Observera oraz Stream w trybach bare metal i FreeRTOS |
| BLE | `rp2350-arm` | `pico2w` | wbudowany CYW43439 z BTstack | zaliczone bramki Observera, Stream i aktywnego współistnienia Stream+WiFi/MQTT |
| BLE | `rp2040` | `pico-rm2` | zewnętrzny CYW43439 PIM730/RM2 przez PIO | kompilacja została sprawdzona; dedykowany test sprzętowy oczekuje na wykonanie |
| BLE | `stm32g474` | `nucleo-g474re-pim730` | zewnętrzny CYW43439 PIM730/RM2 przez gSPI | zaliczone testy Peripheral i Observer oraz pełne testy obciążeniowe Stream z wyświetlaczem w trybach bare metal i FreeRTOS |
| BLE | `esp32s3` | `waveshare-esp32-s3-zero` | zintegrowany kontroler LE z ESP-IDF NimBLE | pełny test kompilacji i linkowania; test radia na sprzęcie oczekuje na wykonanie |
| Classic / HID Host / gamepad | `rp2350-arm` | `pico2w` | wbudowany CYW43439 z BTstack | zaliczone bramki sprzętowe managera, surowego HID i gamepada |
| Classic / HID Host / gamepad | `rp2040` / `stm32g474` | `picow` / `pico-rm2` / `nucleo-g474re-pim730` | CYW43439 z BTstack | sprawdzono kompilację; te hosty nie przeszły dedykowanej sprzętowej bramki HID/gamepada |
| Classic / HID Host / gamepad | `esp32` | `esp32-devkitc-v4` | zintegrowany kontroler BR/EDR z ESP-IDF Bluedroid i ESP HID Host | pełny test kompilacji i linkowania; ogólna bramka radia na sprzęcie oczekuje |
| BLE + Classic HID/gamepad | `rp2350-arm` | `pico2w` | wspólny wbudowany CYW43439 z BTstack | pasywny Observer i połączony gamepad zaliczyły aktywną bramkę współistnienia, wraz z rozłączeniem i ponownym połączeniem HID podczas ciągłego skanowania |
| A2DP Sink / AVRCP Target | `rp2040` | `picow` | wbudowany CYW43439 z BTstack i dekoderem SBC Bluedroid | zaliczone bramki sprzętowe A2DP/AVRCP, wyjścia PWM i ponownego połączenia z bondem |
| A2DP Sink / AVRCP Target | `rp2350-arm` | `pico2w` | wbudowany CYW43439 z BTstack i dekoderem SBC Bluedroid | zaliczone bramki sprzętowe A2DP/AVRCP i ponownego połączenia; produkt nadal musi sprawdzić wybrane fizyczne wyjście audio |
| BLE i profile Classic | `mock` | `host-mock` | deterministyczne backendy testowe | regresja hostowa BLE, Classic, HID, gamepada, A2DP i AVRCP |

Backend RP2350 obsługuje wyłącznie Pico 2 W z targetem `rp2350-arm`. Pico 2 W
z `rp2350-riscv` jest nieobsługiwane, ponieważ transport Bluetooth CYW43 nie
jest włączony dla tego targetu. `HAL_ENABLE_BLE` i
`HAL_ENABLE_BLUETOOTH_CLASSIC` powodują błąd kompilacji, gdy wymagany transport
jest niedostępny. Sprawdzenia wykonywane w runtime rozróżniają
`HAL_BOARD_CAP_BLUETOOTH_LE_CONTROLLER` i
`HAL_BOARD_CAP_BLUETOOTH_CLASSIC_CONTROLLER`; starszy ogólny bit Bluetooth
pozostaje dostępny dla zgodności. Moduły zewnętrzne wymagają dodatkowo
`HAL_BOARD_CAP_EXTERNAL_RADIO_FRONTEND`.

ESP32-S3 obsługuje podstawowe BLE Peripheral/Observer, ale nie `HAL_ENABLE_BLE_STREAM`, klienta GATT ani Bluetooth Classic. Oryginalny ESP32 obsługuje zarządzanie Classic, HID Host i gamepad, lecz nie publiczne API BLE. To ograniczenia dostępności na danej platformie, nie automatyczne przełączanie implementacji podczas pracy.

<a id="cykl-życia-i-odpytywanie"></a>

## Uruchamianie i regularna obsługa BLE

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

`hal_ble_initialize()` rozpoczyna uruchamianie kontrolera i wraca po przyjęciu żądania. Na gotowość poczekaj do zdarzenia `HAL_BLE_EVENT_CONTROLLER_READY`. Po poprawnym uruchomieniu ponawianie inicjalizacji lub deinicjalizacji jest idempotentne. Deinicjalizacja unieważnia uchwyty połączeń i rozgłaszania, usuwa zdarzenia z kolejki i wyrejestrowuje callback.

Wywołuj `hal_ble_poll()` często, z jednego zadania lub pętli kooperacyjnej. Funkcja najpierw obsługuje kontroler, zwalnia blokadę radia i dopiero wtedy uruchamia callbacki aplikacji. Z callbacku można wykonywać zapytania tylko do odczytu. Ponowne wejście do `hal_ble_poll()`, zmiana callbacku lub deinicjalizacja zwracają w tym kontekście `HAL_EBUSY`.

<a id="zdarzenia"></a>

## Odbiór zdarzeń

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

Zdarzenia można odbierać na dwa sposoby: przez funkcję zwrotną wywoływaną przez `hal_ble_poll()` albo przez bezpośrednie wywołania `hal_ble_event_next()`. Wybierz jeden z nich, ponieważ oba pobierają zdarzenia z tej samej kolejki. Pusta kolejka powoduje zwrócenie `HAL_EAGAIN` przez `hal_ble_event_next()`. Po jej zapełnieniu nowe zdarzenia są odrzucane, licznik `hal_ble_info_t::dropped_events` wzrasta, a następne `hal_ble_poll()` zwraca `HAL_EOVERFLOW`. BLE pozostaje aktywne.

Zdarzenie gotowości nie zawiera adresu drugiej strony połączenia; wywołaj
`hal_ble_get_local_address()` po jego otrzymaniu. Zdarzenie połączenia
zawiera jej adres i nowy nieprzezroczysty uchwyt połączenia.
Zdarzenia MTU i rozłączenia odnoszą się do tego samego uchwytu.

<a id="advertising"></a>

## Rozgłaszanie BLE (advertising)

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

## Pasywne skanowanie (Observer)

`hal_ble_scan_start()` akceptuje interwał, okno oraz opcjonalny filtr
duplikatów. Obie wartości czasowe używają jednostek Bluetooth 0,625 ms i
muszą mieścić się między `HAL_BLE_SCAN_INTERVAL_MIN` a
`HAL_BLE_SCAN_INTERVAL_MAX`; okno nie może przekraczać interwału. `HAL_OK`
oznacza, że żądanie zostało zaakceptowane. Poczekaj na
`HAL_BLE_EVENT_SCAN_STARTED`, które potwierdza uruchomienie skanowania.

Skanowanie jest pasywne i odbiera wyłącznie pakiety rozgłoszeniowe typu legacy.
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

<a id="model-statusu-i-niepowodzeń"></a>

## Wyniki operacji i obsługa błędów

Operacje zwracają `hal_status_t`. Najczęstsze wyniki:

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

<a id="manager-bluetooth-classic-i-profile"></a>

## Bluetooth Classic: urządzenia i profile

<a id="manager-classic"></a>

### Zarządzanie kontrolerem i urządzeniami

`HAL_ENABLE_BLUETOOTH_CLASSIC` udostępnia `hal_bluetooth_classic.h`. Jeden obiekt zarządzający Classic (manager) obsługuje kontroler i dołączone profile. Otwórz go, regularnie wywołuj `hal_bluetooth_classic_poll()` i przed jego zamknięciem zamknij wszystkie profile.

Wyniki inquiry
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

Odbiór dźwięku SBC w roli A2DP Sink włącza `HAL_ENABLE_BLUETOOTH_A2DP_SINK`. Flaga udostępnia `hal_bluetooth_a2dp_sink.h` i włącza zarządzanie Classic. Jeden odbiornik dołącza do jednego otwartego obiektu Classic. Obsługuje 44,1 i 48 kHz oraz mono, stereo i joint stereo. Zwraca przeplatane próbki PCM typu signed 16-bit w wynegocjowanej liczbie kanałów lub, po wybraniu `HAL_BLUETOOTH_A2DP_OUTPUT_MONO`, sygnał mono z ograniczaniem do zakresu. API nie udostępnia typów BTstack ani sterownika wyjścia audio.

Callbacki stosu kopiują tylko całe pakiety mediów do kolejki o ograniczonej pojemności. Najpierw wywołaj `hal_bluetooth_classic_poll()`, a następnie powtarzaj `hal_bluetooth_a2dp_sink_poll()` do otrzymania `HAL_EAGAIN`. W tym kontekście wykonywane są parsowanie, dekodowanie SBC, programowa regulacja głośności, mieszanie kanałów i niewielka korekcja zegara.

`hal_bluetooth_a2dp_sink_pcm_next()` wymaga zgromadzenia określonego zapasu próbek po uruchomieniu strumienia i po opróżnieniu bufora PCM. Aplikacja odpowiada za fizyczne wyjście audio i powinna wcześniej przygotowywać własne bufory PCM. Przerwanie DMA powinno jedynie wybierać gotowy bufor lub ciszę.

`hal_bluetooth_a2dp_sink_info_t` podaje format i stan strumienia, straty
pakietów, bieżące oraz maksymalne zajęcie ograniczonych kolejek pakietów/PCM,
odrzucenia, uszkodzone ramki, przepełnienia/underruny PCM oraz bieżącą korektę
zegara. Nacisk na kolejkę nigdy nie powoduje nieograniczonej alokacji. Nowy peer
jest zapisywany przez wspólnego providera bondingu Classic dopiero po lokalnej
autoryzacji parowania, przechwyceniu link key i poprawnym zdekodowaniu pierwszej
ramki SBC. Identyfikator profilu to
`HAL_BLUETOOTH_A2DP_SINK_PROFILE_ID`.

`HAL_ENABLE_BLUETOOTH_AVRCP_TARGET` udostępnia `hal_bluetooth_avrcp_target.h` i włącza A2DP Sink. Rola AVRCP Target przyjmuje od urządzenia Controller głośność bezwzględną z zakresu 0-127. Nowsza oczekująca wartość zastępuje poprzednią; lokalna wartość może być zgłoszona subskrybującemu urządzeniu Controller. Profil współdzieli połączenie i zapisane parowanie A2DP/Classic, bez tworzenia drugiego klucza. Zwalniaj zasoby w kolejności AVRCP, A2DP, Classic. Kompletną przykładową aplikację w C i adapter wyjścia PWM/DMA znajdziesz w [`examples/30_bluetooth_speaker`](../../../examples/30_bluetooth_speaker/).

<a id="ogólny-hid-host"></a>

### Odczyt i wysyłanie raportów HID

`HAL_ENABLE_BLUETOOTH_HID_HOST` udostępnia `hal_bluetooth_hid_host.h` i włącza zarządzanie Classic. Jeden uchwyt HID Host dołącza do otwartego obiektu Classic i obsługuje jedno aktywne połączenie HID. Aplikacja otrzymuje kopię deskryptora oraz ograniczoną kolejkę surowych raportów Input, Output i Feature, bez narzuconej interpretacji klasy urządzenia. Może wysyłać Output/Feature oraz żądać Input/Feature. Wybór urządzenia i sprawdzenie deskryptora należą do aplikacji lub adaptera profilu. Zamknięcie HID rozłącza połączenie, ale nie zamyka obiektu Classic.

Deterministyczny mock potrafi wstrzykiwać gotowość Classic, wyniki inquiry/SDP,
parowanie, link keys, ogólne deskryptory i surowe raporty. Test hostowy używa
deskryptora myszy, aby dowieść braku filtra gamepada we wspólnej ścieżce HID.
Warianty `classic-scan` i `hid-host` projektu
[`examples/29_bluetooth_gamepad`](../../../examples/29_bluetooth_gamepad/)
kompilują te warstwy bez `HAL_ENABLE_BLUETOOTH_GAMEPAD`.

<a id="adapter-gamepada"></a>

### Odczyt stanu gamepada

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

Bramka sprzętowa `rp2350-arm:pico2w` obejmuje jeden profil gamepada D-input,
w tym analizę deskryptora, raporty, bonding i reconnect. Inne profile gamepadów
i płytki z HID wymagają osobnej walidacji deskryptora, raportów, reconnectu oraz
zasobów.

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

`hal_gamepad_forget()` usuwa zapamiętane parowanie: rozłącza aktywne połączenie, czyści dane urządzenia w kontrolerze i RAM oraz usuwa zapisany rekord przez skonfigurowaną obsługę trwałego przechowywania. Bez niej ten ostatni krok nic nie robi. Następne `hal_gamepad_pairing_open()` rozpoczyna nowe parowanie.

Najpierw usuwany jest rekord trwały, a dopiero potem stan urządzenia w działającej aplikacji. Gdy zapis lub usunięcie z nośnika kończy się błędem, urządzenie pozostaje znane. Aplikacja może dzięki temu ponowić reset, zamiast pozornie usunąć parowanie, które powróciłoby po restarcie.

Zachowanie link keys poszczególnych backendów opisuje sekcja managera Classic.
`hal_gamepad_forget()` deleguje do niego usunięcie natywnego bondingu i rekordu
providera.

<a id="znormalizowany-stan-wejść"></a>

#### Ujednolicony stan przycisków i osi

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
Kompletny przykład w C, izolowane warianty Classic/HID oraz konfiguracja BLE+Classic
znajdują się w
[`examples/29_bluetooth_gamepad`](../../../examples/29_bluetooth_gamepad/).

<a id="jh-ble-stream-v1"></a>

## BLE Stream - uwierzytelniony strumień danych

Wymiana danych aplikacji przez jedną stałą usługę GATT z buforami o ograniczonej pojemności. `HAL_ENABLE_BLE_STREAM` udostępnia `hal_ble_stream.h` i automatycznie włącza `HAL_ENABLE_BLE` oraz `HAL_ENABLE_CRYPTO`. Protokół ma wersję JH BLE Stream v1.

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

<a id="przykład-stream"></a>

### Przykład użycia BLE Stream

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

Po `HAL_EAGAIN` przykład zachowuje najwyżej jedną odpowiedź echo i ponawia jej wysłanie przed pobraniem następnych danych RX. Rozłączenie lub inny błąd wysyłania usuwa oczekującą odpowiedź, aby dane poprzedniej sesji nie zostały wysłane w nowej.

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

<a id="współdzielenie-kontrolera-bluetooth"></a>

## Wspólna praca BLE, Classic i WiFi

BLE, Bluetooth Classic i WiFi korzystają z jednego kontrolera CYW43,
transportu, runtime radia oraz blokady usługi. Aplikacja nie może być linkowana
jednocześnie z modułami Pico SDK `pico_cyw43_arch` lub `pico_btstack_cyw43`
i tym backendem. Funkcje zwrotne BLE są wywoływane dopiero po zakończeniu
obsługi radia, dlatego kod aplikacji nigdy nie działa z założoną blokadą.
Firmware korzystający tylko z BLE, tylko z Classic albo z obu trybów używa tej
samej instancji hosta Bluetooth, zarządzanej licznikiem referencji.

Aktywna bramka współistnienia na Pico 2 W obejmuje pasywnego Observera BLE i
połączony gamepad Classic HID. Sprawdza też rozłączenie i ponowne połączenie HID
bez zatrzymywania skanowania BLE. Bramka uwierzytelnionego BLE Stream z
WiFi/MQTT obejmuje rozłączenie i ponowne połączenie WiFi w konfiguracjach bare metal
i FreeRTOS. Wspólny obraz BLE+A2DP jest sprawdzany podczas kompilacji; aktywny
dźwięk nie należy jeszcze do sprzętowej bramki współistnienia.

<a id="license-and-distribution-boundary"></a>

<a id="granica-licencji-i-dystrybucji"></a>

## Licencje i warunki dystrybucji

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
wynikowych zawierających BTstack, a nie kompilacji JaszczurHAL, które go nie
kompilują.

[Przykład `26_ble_stream`](../../../examples/26_ble_stream/) pokazuje uruchomienie Peripheral, rozgłaszanie i odbiór uwierzytelnionego strumienia. Wieloplatformowy [test sprzętowy `bluetooth_stream`](03_build_tests.md#bramka-sprzętowa-jh-ble-stream-v1) sprawdza pełny protokół przy użyciu niezależnego klienta BlueZ.

[Przykład `29_bluetooth_gamepad`](../../../examples/29_bluetooth_gamepad/)
pokazuje obsługę stanów wejść Classic HID i wariant kompilacji BLE+Classic.

[Przykład `30_bluetooth_speaker`](../../../examples/30_bluetooth_speaker/)
pokazuje cykl życia A2DP Sink, ograniczone czasowo parowanie, wspólny bond,
opcjonalną głośność bezwzględną AVRCP, diagnostykę i adapter wyjścia PWM z DMA.
