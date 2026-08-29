# Routing komend niezależny od transportu

*Dostępne również [po angielsku](../en/23_commands.md).*

> **Część [Dokumentacji API JaszczurHAL](../../pl/JaszczurHAL_API.md)**

Podsystem komend oddziela rejestrację i dispatch komend od
transportu, który przenosi żądanie. `hal_command_router` zarządza nazwanymi
handlerami oraz ich polityką źródła/bezpieczeństwa. `hal_command_wire`
dostarcza ograniczony binarny format wiadomości dla adapterów pakietowych i
strumieniowych. Zaimplementowane adaptery to warstwa kompatybilności
HTTP/WebSocket w `hal_net_commands`, ramkowane sesje szeregowe w
`hal_serial_commands`, niezawodny adapter LoRa w `hal_lora_commands`, oraz
uwierzytelniony adapter BLE Stream w `hal_ble_commands`.

## Włączanie modułów

Włącz sam router do bezpośredniego dispatchu lub własnego adaptera:

```c
#define HAL_ENABLE_COMMAND_ROUTER
```

Włącz dispatch ramkowanych komend szeregowych:

```c
#define HAL_ENABLE_SERIAL_COMMANDS
```

To automatycznie włącza (propaguje) `HAL_ENABLE_COMMAND_ROUTER`. Samo
ramkowanie Serial Session pozostaje dostępne bez adaptera.

Włącz adapter LoRa razem z jednym providerem radia:

```c
#define HAL_ENABLE_SX126X
#define HAL_ENABLE_LORA_COMMANDS
```

`HAL_ENABLE_LORA_COMMANDS` propaguje `HAL_ENABLE_COMMAND_ROUTER` oraz
`HAL_ENABLE_LORA_LINK`; łącze (link) propaguje następnie `HAL_ENABLE_LORA` i
`HAL_ENABLE_CRC`. Włącz adapter BLE przy pomocy:

```c
#define HAL_ENABLE_BLE_COMMANDS
```

`HAL_ENABLE_BLE_COMMANDS` propaguje `HAL_ENABLE_BLE_STREAM` i
`HAL_ENABLE_COMMAND_ROUTER`; BLE Stream propaguje następnie BLE i CRYPTO.
`HAL_ENABLE_NET_COMMANDS` również propaguje router, zachowując przy tym swoje
zależności od HTTP, WebSocket, cJSON, TCP i WiFi.

## Router

```c
#include <hal/commands/hal_command_router.h>
```

Żądanie zawiera binarnie bezpieczne argumenty, kodowanie, nie-właścicielską
(non-owning) nazwę komendy i kontekst źródła, a także identyfikatory żądania,
peera i sesji. Maski źródła i bezpieczeństwa pozwalają jednemu handlerowi
akceptować tylko wybrane punkty wejścia. Router sprawdza te maski przed
synchronicznym wywołaniem handlera.

Domyślny router współdzielony w obrębie całego procesu jest wykorzystywany
przez adaptery transportowe. Niezależne routery są dostępne, gdy aplikacja
potrzebuje izolowanego zestawu handlerów.

```c
static hal_status_t echo_command(const hal_command_request_t *request,
                                 hal_command_response_t *response,
                                 void *user) {
  (void)user;
  return hal_command_response_write(response, request->arguments,
                                    request->arguments_length);
}

hal_command_router_t router = NULL;
hal_status_t status = hal_command_router_default(&router);
if (status != HAL_OK) {
  return status;
}

hal_command_definition_t echo = {
    .name = "echo",
    .allowed_sources = HAL_COMMAND_SOURCE_MASK_ALL,
    .required_security = 0u,
    .handler = echo_command,
    .user = NULL,
};
status = hal_command_router_register(router, &echo);
```

Rejestracja kopiuje definicję i nazwę komendy do ograniczonego slotu. Druga
rejestracja pod tą samą nazwą zastępuje nieaktywny (idle) slot. Wyrejestrowanie,
zastąpienie, wyczyszczenie lub zniszczenie routera, gdy dotknięty handler jest
aktywny, zwraca `HAL_EBUSY`; zniszczenie domyślnego routera zwraca `HAL_EPERM`.
Wyczerpanie puli routerów lub slotów handlerów zwraca `HAL_ENOMEM`, a
nieaktualny (stale) uchwyt zwraca `HAL_EUNINIT`. Wskaźniki żądania oraz
`source_context` są pożyczone (borrowed) jedynie na czas trwania callbacku.

Usługa, która współdzieli router i nie może zastąpić komendy należącej do
innego właściciela, może użyć `hal_command_router_register_unique()`. Operacja
zwraca `HAL_EEXIST` bez zmiany zarejestrowanego slotu, gdy nazwa jest już
obecna. Przy zamykaniu `hal_command_router_unregister_if_matches()` usuwa
komendę tylko wtedy, gdy zarówno jej publiczny handler w C, jak i wskaźnik
`user` pasują. Inny właściciel lub aktywny pasujący handler zwraca `HAL_EBUSY`;
brak nazwy zwraca `HAL_ENOENT`. Rejestracja i sprawdzenie własności są
wykonywane pod blokadą (lock) routera, więc uruchamianie, wycofywanie
(rollback) i zamykanie nie zawierają okna check-then-change.

Dispatch nie serializuje wykonania handlera. Ten sam handler może działać
współbieżnie, gdy wiele tasków lub adapterów transportowych wywołuje go
równocześnie, więc współdzielony stan `user` musi zapewnić własną
synchronizację.

`hal_command_response_write()` i `hal_command_response_write_str()` dopisują
do stałego bufora odpowiedzi. Pomocnik kodowania wybiera też zwyczajowy typ
zawartości (content type). Przepełnienie jest raportowane jako
`HAL_EOVERFLOW` i zapisywane w `response.overflow`. Wskaźniki `message` i
`content_type` są pożyczone; wartości dostarczone przez handler muszą
pozostać ważne tak długo, jak długo wywołujący korzysta z ukończonej
odpowiedzi.

Dispatch resetuje odpowiedź przed wyszukaniem (lookup). Jawny
`response.status` różny od `HAL_OK` ma pierwszeństwo przed udanym zwrotem
handlera. Jeśli handler zwróci błąd, podczas gdy odpowiedź jest wciąż
udana, ten zwrot staje się statusem odpowiedzi. `HAL_NONE` z dowolnej z tych
ścieżek jest normalizowane do `HAL_EINTERNAL`. Pomocnicze funkcje konwersji
źródła, kodowania i typu wiadomości na łańcuch znaków zwracają `"UNKNOWN"`
dla wartości spoza swoich wyliczeń.

```c
hal_command_request_t request = {
    .source = HAL_COMMAND_SOURCE_DIRECT,
    .encoding = HAL_COMMAND_ENCODING_TEXT,
    .command = "echo",
    .arguments = (const uint8_t *)"hello",
    .arguments_length = 5u,
    .request_id = 1u,
};
hal_command_response_t response;
status = hal_command_router_dispatch(router, &request, &response);
```

Zdefiniowane źródła to wywołania bezpośrednie, HTTP, WebSocket, Serial
Session, niezawodny LoRa oraz BLE Stream. Flagi bezpieczeństwa opisują
uwierzytelnienie, szyfrowanie, integralność i ochronę przed powtórzeniem
(replay), raportowane przez adapter. Router egzekwuje żądane bity, ale sam nie
realizuje bezpieczeństwa transportu.

## Wiadomości wire

```c
#include <hal/commands/hal_command_wire.h>
#include <string.h>
```

Helper warstwy wire koduje jedno `REQUEST`, `RESPONSE` lub `EVENT` do
pamięci należącej do wywołującego oraz dekoduje dokładnie jedną kompletną
wiadomość do pól o ograniczonym rozmiarze, przejmujących kopię danych.
`hal_command_message_frame_size()` pozwala adapterom pakietowym i
strumieniowym odkrywać pierwszą kompletną ramkę przyrostowo. Na przykład:

```c
hal_command_message_t message = {0};
message.type = HAL_COMMAND_MESSAGE_REQUEST;
message.encoding = HAL_COMMAND_ENCODING_TEXT;
message.request_id = 17u;
memcpy(message.name, "echo", sizeof("echo"));
memcpy(message.payload, "hello", 5u);
message.payload_length = 5u;

uint8_t frame[128];
size_t frame_length = 0u;
hal_status_t status = hal_command_message_encode(
    &message, frame, sizeof(frame), &frame_length);

hal_command_message_t decoded;
if (status == HAL_OK) {
  status = hal_command_message_decode(frame, frame_length, &decoded);
}
```

Wersja 1 zaczyna się 16-bajtowym nagłówkiem big-endian:

| Offset | Pole |
|---:|---|
| 0..1 | znacznik ASCII `JC` |
| 2 | wersja wire |
| 3 | typ wiadomości |
| 4 | kodowanie argumentu/payloadu |
| 5 | długość nazwy komendy lub zdarzenia |
| 6..7 | zarezerwowane, zero |
| 8..11 | identyfikator żądania |
| 12..13 | wartość odpowiedzi `hal_status_t` ze znakiem |
| 14..15 | długość payloadu |
| 16.. | nazwa, po której następuje payload |

Żądanie ma niezerowy identyfikator, nazwę i status `HAL_NONE`. Zdarzenie ma
nazwę, identyfikator zero i status `HAL_NONE`. Odpowiedź ma niezerowy
identyfikator, brak nazwy i status inny niż `HAL_NONE`. Dekoder odrzuca
nieznane wersje, nieprawidłowe wyliczenia, niezerowe bajty zarezerwowane,
zniekształcone nazwy oraz każde niedopasowanie rozmiaru poprzez `HAL_EPROTO`.

Jeśli pamięć wyjściowa kodera jest zbyt mała, zwraca on `HAL_EOVERFLOW` i
zapisuje wymagany rozmiar w `out_length`, nie produkując częściowej
wiadomości. `hal_command_source_to_string()`, `hal_command_encoding_to_string()`
oraz `hal_command_message_type_to_string()` dostarczają stabilnych nazw
diagnostycznych.

Dla częściowego bufora strumienia `hal_command_message_frame_size()` zwraca
`HAL_EAGAIN`. Gdy tylko stały nagłówek jest obecny, funkcja raportuje również
wymaganą łączną długość; gdy zbuforowana jest już taka liczba bajtów, zwraca
`HAL_OK`, nawet gdy następuje kolejna ramka. Przekaż do dekodera dokładnie
zaraportowany prefiks i zachowaj końcowe bajty na następne wywołanie.
`HAL_COMMAND_WIRE_MAX_FRAME_SIZE` określa górną granicę pamięci adaptera
ustaloną podczas buildu. Format wire nie dodaje
szyfrowania ani uwierzytelnienia; te właściwości należą do adaptera
transportowego.

## Adapter ramkowanej sesji szeregowej (Framed Serial Session)

```c
#include <hal/serial/hal_serial_commands.h>
```

Zainicjalizuj `hal_serial_session`, zarejestruj handlery routera używając ich
istniejących nazw SC, a następnie dołącz jeden adapter będący własnością
wywołującego:

```c
static hal_serial_session_t session;
static hal_serial_commands_t serial_commands;

hal_serial_session_init_with_vocabulary(
    &session, "ECU", "1.2.3", "build-id", &serial_vocabulary);

hal_serial_commands_config_t config =
    hal_serial_commands_config_defaults(&session);
config.router = router;
config.command_prefix = "SC_";

hal_status_t status = hal_serial_commands_init(&serial_commands, &config);

/* W pętli aplikacji: */
hal_serial_session_poll(&session);
```

Kompilowalny projekt
[`28_serial_commands`](../../../examples/28_serial_commands/README.md)
pokazuje pełny cykl życia z niezależnym routerem, handlerami `echo` i `info`,
polityką źródła, ramkowanymi żądaniami i wycofywaniem (rollback) przy
uruchamianiu.

`hal_serial_commands_init()` przejmuje callback nieznanego payloadu sesji i
zwraca `HAL_EBUSY` zamiast zastępować istniejący callback. HELLO, BYE oraz
opcjonalna wymiana uwierzytelniająca pozostają w `hal_serial_session`.
Pasujący payload aplikacji jest akceptowany dopiero po tym, jak HELLO
aktywuje sesję. Adapter dzieli pierwszy token oddzielony białym znakiem od
pozostałych argumentów bez zmiany jego nazwy, więc `SC_GET_PARAM nominal_rpm`
przekazuje do routera komendę `SC_GET_PARAM` z `nominal_rpm` jako argumentami TEXT
lub JSON.

Opcjonalny predykat `allow_inactive(request, user)` może dopuścić wybrane
pasujące komendy przed HELLO. Jego wynik prawdziwy dociera wyłącznie do
zwykłej polityki routera; nie dodaje uwierzytelnienia ani nie omija sprawdzeń
źródła/bezpieczeństwa. To wspiera operacje takie jak restart bootloadera
należący do routera, który musi zachować swoją istniejącą,
nieuwierzytelnioną odpowiedź `NOT_AUTHORIZED` przed konfiguracją sesji.
Pozostaw callback jako NULL, aby każda pasująca komenda pozostała bramkowana
przez HELLO.

Żądanie używa `HAL_COMMAND_SOURCE_SERIAL_SESSION`, sekwencji ramki SC jako
`request_id`, identyfikatora Serial Session jako `session_id`,
skonfigurowanego `peer_id` oraz wskaźnika sesji jako `source_context`.
Uwierzytelniona sesja wnosi wyłącznie `HAL_COMMAND_SECURITY_AUTHENTICATED`.
Serial Session nie raportuje do polityki routera szyfrowania, kryptograficznej
integralności wiadomości ani ochrony przed powtórzeniem.

Niepusta treść handlera TEXT lub JSON jest emitowana dosłownie w ramce
odpowiedzi z tą samą sekwencją. Puste i nietekstowe odpowiedzi używają
opcjonalnego callbacku `formatter`. Pozwala to projektowi mapować statusy
routera na swój istniejący słownik odpowiedzi SC bez osadzania tokenów
specyficznych dla projektu w JaszczurHAL. Bez formattera adapter emituje `OK`
lub `ERR <HAL_STATUS>`.

`command_prefix` jest opcjonalny i pozostaje częścią routowanej nazwy komendy.
Gdy jest ustawiony, niepasujący payload jest przekazywany do opcjonalnego
callbacku `fallback`; wspiera to istniejące komendy diagnostyczne spoza
przestrzeni nazw SC. Bez fallbacku adapter używa odpowiedzi "nieznana
komenda" ze słownika sesji. Pasująca, lecz niezarejestrowana komenda SC zawsze
dociera do routera i dlatego pozostaje widoczna dla formattera odpowiedzi.

Odpowiedzi są ograniczone przez `HAL_SERIAL_FRAME_PAYLOAD_MAX` i odrzucają
osadzone NUL, `*`, CR i LF. Adapter akceptuje wyłącznie kodowanie TEXT i JSON,
ponieważ payloady SC mają postać pojedynczego wiersza. `hal_serial_commands_get_last_status()`
zachowuje ostatni błąd routera, formatowania lub payloadu.
`hal_serial_commands_deinit()` zwraca `HAL_EBUSY` w trakcie predykatu
inactive, handlera, formattera, fallbacku lub emisji odpowiedzi i czyści
callback sesji tylko wtedy, gdy adapter wciąż jest jego właścicielem.

<a id="reliable-lora-adapter"></a>

## Niezawodny adapter LoRa

```c
#include <hal/radio/hal_lora_commands.h>
```

Najpierw utwórz i zainicjalizuj surowe radio oraz niezawodne łącze (link).
Łącze musi znajdować się w stanie odbierania, gdy adapter jest dołączany.
Router zerowy (null) w konfiguracji wybiera domyślny współdzielony router.

```c
hal_lora_commands_config_t config =
    hal_lora_commands_config_defaults(link, 7u);
config.router = router;
config.acknowledged = true;
config.initial_request_id = 1u;

hal_lora_commands_t commands = NULL;
hal_status_t status = hal_lora_commands_create(&config, &commands);
if (status != HAL_OK) {
  return status;
}

uint32_t request_id = 0u;
status = hal_lora_commands_request_start(
    commands, UINT16_C(0x1002), "echo", HAL_COMMAND_ENCODING_TEXT,
    "hello", 5u, &request_id);
```

Adapter kopiuje i koduje żądanie przed rozpoczęciem wysyłki przez łącze.
Identyfikator żądania jest niezerowy i zwiększa się dopiero po tym, jak łącze
zaakceptuje wysyłkę; `HAL_EBUSY` lub `HAL_EAGAIN` pozostawia licznik bez zmian
i zapisuje zero do `out_request_id`. Ustawienie `acknowledged` odnosi się do
żądań, odpowiedzi i zdarzeń typu punkt-punkt.

`hal_lora_commands_event_start()` wysyła nazwane zdarzenie z identyfikatorem
zero. Zdarzenia rozgłoszeniowe (broadcast) zawsze wyłączają potwierdzenie
transportowe niezależnie od skonfigurowanego ustawienia. Żądania nie mogą
używać adresu rozgłoszeniowego.

Po dołączeniu tylko adapter może wykonywać przetwarzanie i odbiór łącza. Wywołuj
`hal_lora_commands_process()` zamiast `hal_lora_link_process()`/
`hal_lora_link_receive()`. Przychodzące żądania są dekodowane, synchronicznie
przekazywane do routera i automatycznie obsługiwane odpowiedzią. Odebrane odpowiedzi i zdarzenia
są kopiowane przy pomocy `hal_lora_commands_receive()`:

```c
hal_status_t process_status = hal_lora_commands_process(commands);
if (process_status == HAL_OK || process_status == HAL_EAGAIN ||
    process_status == HAL_IGNORED) {
  hal_command_message_t incoming;
  hal_lora_link_message_info_t link_info;
  if (hal_lora_commands_receive(commands, &incoming, &link_info) == HAL_OK) {
    /* Dopasuj wiadomości RESPONSE po request_id; obsłuż wiadomości EVENT według nazwy. */
  }
}
```

`hal_lora_commands_receive()` zwraca `HAL_EAGAIN` bez usuwania danych z kolejki,
gdy żadna odpowiedź lub zdarzenie nie oczekuje w kolejce. Po pomyślnym
zniszczeniu każde poprawne wywołanie API przy użyciu starego uchwytu zwraca
`HAL_EUNINIT`.

`hal_lora_commands_process()` ma jednego logicznego właściciela. Współbieżne
lub reentrantne wywołanie zwraca `HAL_EBUSY`. Adapter zwalnia własny mutex
podczas wywoływania handlera, więc ten handler może bezpiecznie odpytać stan
adaptera, odebrać już zakolejkowaną wiadomość aplikacji lub podjąć próbę
żądania albo zdarzenia; próba wysyłki podjęta, gdy łącze potwierdza
przychodzące żądanie, zazwyczaj zwraca `HAL_EBUSY` lub `HAL_EAGAIN`.

Tylko jedna widoczna dla aplikacji odpowiedź lub zdarzenie jest zakolejkowana
na adapter. Odpowiedź na przychodzące żądanie pozostaje w pamięci należącej
do adaptera, gdy leżące u podstaw łącze jest zajęte swoim potwierdzeniem
transportowym. Kontynuuj wywoływanie `hal_lora_commands_process()`, aby
ponowić próbę. Adapter i łącze używają kopiowanych, ograniczonych buforów i
zachowują własność uchwytów routera i łącza po stronie wywołującego.

Zniszczenie adaptera zwraca `HAL_EBUSY`, gdy trwa przetwarzanie lub dispatch,
odpowiedź oczekuje, wiadomość aplikacji pozostaje nieprzeczytana albo
leżące u podstaw łącze nie powróciło do stanu odbierania. Kontynuuj
przetwarzanie i odbierz zakolejkowane wiadomości przed ponowną próbą
zniszczenia. Operacja, która już weszła do API, utrzymuje swój kontekst przy
życiu do momentu zwrócenia wyniku, a nieaktualny uchwyt nie może aliasować
późniejszego adaptera.

Szyfrowane łącze LoRa dostarcza wszystkie flagi bezpieczeństwa komend; łącze w
postaci jawnej (plaintext) nie dostarcza żadnej. Polityka handlera może więc
wymagać uwierzytelnionego i chronionego przed powtórzeniem dostarczenia bez
zależności od metadanych specyficznych dla LoRa. Pełny widok
`hal_lora_link_message_info_t` jest dostępny jako kontekst źródła żądania
podczas dispatchu.

`hal_lora_commands_get_info()` raportuje stan kolejki, łącza, przetwarzania i
dispatchu, natomiast `hal_lora_commands_get_diagnostics()` raportuje liczniki
żądań, odpowiedzi, zdarzeń, protokołu, dispatchu, ponowień i odrzuceń.

<a id="authenticated-ble-stream-adapter"></a>

## Uwierzytelniony adapter BLE Stream

```c
#include <hal/bluetooth/hal_ble_commands.h>
```

Zainicjalizuj `hal_ble`, opublikuj i wyposaż (provision) `hal_ble_stream`, a
następnie dołącz jeden adapter komend. Aplikacja nadal jest właścicielem
odpytywania kontrolera i rozgłaszania (advertising):

```c
hal_ble_commands_config_t config = hal_ble_commands_config_defaults();
config.router = router;
config.initial_request_id = 1u;

hal_ble_commands_t commands = NULL;
hal_status_t status = hal_ble_commands_create(&config, &commands);

/* W pętli aplikacji, po obsłudze kontrolera: */
status = hal_ble_poll();
if (status == HAL_OK || status == HAL_EOVERFLOW) {
  status = hal_ble_commands_process(commands);
}
```

Adapter jest jedynym odbiorcą command-wire współdzielonego w całym procesie BLE
Stream. Nie wywołuj `hal_ble_stream_send()` ani `hal_ble_stream_receive()`,
gdy jest dołączony. Adapter nie jest właścicielem `hal_ble_poll()`,
inicjalizacji Stream, provisioningu sekretów, rozgłaszania ani routera. Obecny
transport BLE to Peripheral z jednym peerem Central; same wiadomości komend
pozostają dwukierunkowe i dlatego nie potrzebują roli adaptera ani argumentu
adresu docelowego.

`hal_ble_commands_request_start()` i `hal_ble_commands_event_start()` kopiują
kompletną wiadomość do ograniczonej pamięci adaptera. Wymagają
uwierzytelnionej sesji Stream i w przeciwnym razie zwracają `HAL_EAUTH`.
Pomyślnie zaakceptowane żądanie otrzymuje niezerowy identyfikator
natychmiast; powtórzone wywołania zwracają `HAL_EBUSY`, dopóki inna
wiadomość wire lub automatyczna odpowiedź zajmuje bufor nadawczy. Kontynuuj
wywoływanie `hal_ble_commands_process()`, aż wszystkie fragmenty (chunks)
trafią do kolejki Stream.

Jedna wiadomość wire komend może obejmować wiele uwierzytelnionych
payloadów DATA. Adapter wybiera co najwyżej:

```text
min(HAL_BLE_STREAM_MAX_PAYLOAD, negotiated_ATT_MTU - 31)
```

bajtów na fragment. 31-bajtowy narzut obejmuje ATT, ramkowanie Stream,
licznik kierunkowy i tag uwierzytelnienia. Przy ATT MTU wynoszącym co najmniej
159, treść komendy o długości 500 bajtów potrzebuje pięciu fragmentów w
każdym kierunku. Odbiornik używa `hal_command_message_frame_size()`
przyrostowo, zachowuje bajty po kompletnej wiadomości i przekazuje co najwyżej
jedno żądanie na wywołanie process.

Przychodzące żądania są synchronicznie przekazywane do routera, a odpowiedzi
wysyłane automatycznie. Odpowiedzi i zdarzenia są kopiowane przy pomocy
`hal_ble_commands_receive()`:

```c
hal_command_message_t message;
hal_ble_commands_peer_info_t peer;
if (hal_ble_commands_receive(commands, &message, &peer) == HAL_OK) {
  /* Dopasuj odpowiedź po request_id lub odbierz zdarzenie po nazwie. */
}
```

Każde żądanie BLE przekazane do routera raportuje
`HAL_COMMAND_SECURITY_ALL`, ponieważ
do adaptera docierają wyłącznie payloady zwolnione przez wzajemnie
uwierzytelniony, szyfrowany, chroniony pod względem integralności i
chroniony przed powtórzeniem Stream. `peer_id` bezstratnie zawiera typ
adresu i sześć bajtów adresu BLE. `session_id` to publiczny losowy
identyfikator z handshake'u Stream, reprezentowany jako 64-bitowa wartość
little-endian. Podczas wywołania handlera `source_context` wskazuje na
pożyczony `hal_ble_commands_peer_info_t` z adresem, połączeniem, MTU,
wynegocjowanymi możliwościami, generacjami, identyfikatorem sesji oraz
pierwszym/ostatnim licznikiem DATA Stream użytymi przez wiadomość.

Rozłączenie lub nowa uwierzytelniona sesja czyszczą niekompletne dane odbioru,
niedokończone wysyłki i nieprzeczytane wiadomości z poprzedniej sesji peera.
Utracony fragment RX Stream, zniekształcony nagłówek komendy, nienastępujący
po sobie licznik fragmentów lub timeout częściowej ramki
sprawia, że wyrównanie bajtów staje się niepoznawalne, więc adapter zamyka tę
sesję Stream, zamiast zgadywać punkt resynchronizacji. Timeout pochodzi z
`partial_frame_timeout_ms`; zero w konfiguracji wybiera
`HAL_BLE_COMMANDS_PARTIAL_FRAME_TIMEOUT_MS`.

`hal_ble_commands_process()` zwraca `HAL_OK` po postępie i `HAL_EAGAIN`, gdy
jest bezczynny lub oczekuje na pojemność/uwierzytelnienie Stream. Błędy
protokołu, timeoutu i przepełnienia są zwracane jednorazowo i zachowywane
w diagnostyce. Zakolejkowana jest tylko jedna widoczna dla aplikacji
odpowiedź lub zdarzenie. Zniszczenie adaptera, gdy pozostają dane wire, trwa
dispatch albo czeka nieprzeczytana wiadomość, zwraca `HAL_EBUSY`; nieaktualny
uchwyt zwraca `HAL_EUNINIT`.

## Kompatybilność sieciowa

`hal_net_commands` zachowuje swoje dotychczasowe API text/JSON, cJSON, HTTP i
WebSocket, ale jego rejestracje i wykonania używają współdzielonego domyślnego
routera. Jego dotychczasowy typ handlera pozostaje ograniczony do wywołań
bezpośrednich, HTTP i WebSocket. Zarejestruj generyczny
`hal_command_definition_t` w domyślnym routerze, gdy ten sam handler musi
akceptować zarówno źródła sieciowe, jak i LoRa. Wykonanie TEXT przekazuje
bajty po nazwie komendy. Wykonanie JSON przekazuje kompaktową serializację
wartości `args` lub `params`. Sieciowe identyfikatory żądania, peera i sesji
oraz flagi bezpieczeństwa są zerowe. Kompatybilne operacje zliczania i
wyrejestrowania działają na tym współdzielonym zestawie handlerów.
`hal_net_commands_clear()` czyści cały domyślny router i zwraca
`hal_status_t`; zwraca `HAL_EBUSY` i pozostawia zestaw bez zmian, gdy
jakakolwiek zarejestrowana komenda, w tym należąca do innego adaptera na
współdzielonym domyślnym routerze, jest w trakcie dispatchu. Współdzielona
odpowiedź zachowuje dotychczasowe pola odpowiedzi sieciowej w ich
pierwotnym porządku i dopisuje swoje niezależne od transportu pole
`encoding`.

`HAL_ENABLE_BLE_STREAM` sam w sobie pozostaje ogólnym uwierzytelnionym
strumieniem bajtów i nie włącza routera. Wybierz `HAL_ENABLE_BLE_COMMANDS`
tylko wtedy, gdy payload Stream jest dedykowany ruchowi wire komend.

## Ograniczenia ustalane podczas buildu

Zdefiniuj limity przed dołączeniem nagłówków HAL:

| Makro | Domyślnie | Prawidłowy zakres | Przeznaczenie |
|---|---:|---:|---|
| `HAL_COMMAND_ROUTER_MAX_INSTANCES` | 2 | 1..16 | Pula routerów, łącznie z domyślnym routerem |
| `HAL_COMMAND_ROUTER_MAX_COMMANDS` | 8 | 1..64 | Sloty handlerów na router |
| `HAL_COMMAND_ROUTER_NAME_MAX` | 32 | 2..256 | Pamięć nazwy komendy łącznie z terminatorem |
| `HAL_COMMAND_RESPONSE_BUFFER_SIZE` | 512 | 32..65535 | Pamięć odpowiedzi handlera |
| `HAL_COMMAND_MESSAGE_MAX_PAYLOAD` | 512 | 1..65535 | Pamięć payloadu wire będącego własnością |
| `HAL_BLE_COMMANDS_PARTIAL_FRAME_TIMEOUT_MS` | 5000 | większe od zera | Maksymalny czas życia niekompletnej wiadomości wire komend BLE |

Przestarzałe makra rozmiaru net-command pozostają aliasami odpowiadających im
współdzielonych limitów routera i muszą się z nimi zgadzać, gdy zdefiniowane
są obie pisownie.
