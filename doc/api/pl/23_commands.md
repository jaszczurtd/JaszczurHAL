<a id="router-komend-niezależny-od-transportu"></a>

# Polecenia aplikacji niezależne od transportu

*Dostępne również [po angielsku](../en/23_commands.md).*

> **Część [Dokumentacji API JaszczurHAL](../../pl/JaszczurHAL_API.md)**

Zarejestruj polecenie raz i udostępnij je przez różne kanały komunikacji. `hal_command_router` dobiera funkcję obsługi według nazwy, sprawdza dozwolone źródło i wymagane zabezpieczenia, a następnie wykonuje żądanie. `hal_command_wire` określa binarny format wiadomości o ograniczonym rozmiarze.

Dostępne adaptery obsługują HTTP/WebSocket przez zgodne API `hal_net_commands`, sesję szeregową przez `hal_serial_commands`, łącze LoRa z potwierdzeniami przez `hal_lora_commands` i uwierzytelniony BLE Stream przez `hal_ble_commands`.

## Włączanie modułów

Włącz sam router, jeśli komendy będą wywoływane bezpośrednio lub przez własny
adapter:

```c
#define HAL_ENABLE_COMMAND_ROUTER
```

Włącz obsługę komend przez ramkowaną sesję szeregową:

```c
#define HAL_ENABLE_SERIAL_COMMANDS
```

Flaga automatycznie włącza `HAL_ENABLE_COMMAND_ROUTER`. Samo
ramkowanie Serial Session pozostaje dostępne bez adaptera.

Włącz adapter LoRa razem z obsługą jednej rodziny układów radiowych:

```c
#define HAL_ENABLE_SX126X
#define HAL_ENABLE_LORA_COMMANDS
```

`HAL_ENABLE_LORA_COMMANDS` automatycznie włącza `HAL_ENABLE_COMMAND_ROUTER` oraz
`HAL_ENABLE_LORA_LINK`; łącze włącza następnie `HAL_ENABLE_LORA` i
`HAL_ENABLE_CRC`. Włącz adapter BLE za pomocą:

```c
#define HAL_ENABLE_BLE_COMMANDS
```

`HAL_ENABLE_BLE_COMMANDS` automatycznie włącza `HAL_ENABLE_BLE_STREAM` i
`HAL_ENABLE_COMMAND_ROUTER`; BLE Stream włącza następnie BLE i CRYPTO.
`HAL_ENABLE_NET_COMMANDS` również włącza router, zachowując przy tym swoje
zależności od HTTP, WebSocket, cJSON, TCP i WiFi.

<a id="router"></a>

## Rejestracja i wykonywanie poleceń

```c
#include <hal/commands/hal_command_router.h>
```

Żądanie zawiera argumenty binarne i ich kodowanie, nazwę polecenia, kontekst źródła oraz identyfikatory żądania, urządzenia zdalnego i sesji. Router nie przejmuje pamięci nazwy ani kontekstu. Przed synchronicznym uruchomieniem funkcji obsługi sprawdza maski dozwolonych źródeł i wymaganych zabezpieczeń.

Adaptery transportowe domyślnie korzystają ze wspólnego routera. Utwórz osobną instancję, gdy potrzebujesz niezależnego zestawu poleceń.

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

Podczas rejestracji definicja i nazwa komendy są kopiowane do miejsca w puli
o stałej pojemności. Ponowna rejestracja tej samej nazwy zastępuje wpis, jeśli
handler nie jest aktualnie wykonywany. Próba wyrejestrowania albo zastąpienia
aktywnego handlera oraz wyczyszczenia lub zniszczenia zawierającego go routera
zwraca `HAL_EBUSY`. Routera domyślnego nie można zniszczyć; taka próba zwraca
`HAL_EPERM`. Brak miejsca w puli routerów lub handlerów powoduje zwrócenie
`HAL_ENOMEM`, a nieaktualny uchwyt - `HAL_EUNINIT`. Wskaźniki w żądaniu,
w tym `source_context`, są ważne tylko podczas wykonywania funkcji zwrotnej.

Moduł korzystający ze wspólnego routera może użyć
`hal_command_router_register_unique()`, aby nie zastąpić komendy
zarejestrowanej przez inny moduł. Jeśli nazwa już istnieje, funkcja zwraca
`HAL_EEXIST` i nie zmienia wpisu. Podczas zamykania
`hal_command_router_unregister_if_matches()` usuwa komendę tylko wtedy, gdy
zgadzają się zarówno funkcja obsługi w C, jak i wskaźnik `user`. Wpis
zarejestrowany przez inny kod albo aktualnie wykonywany handler powoduje
zwrócenie `HAL_EBUSY`, a brak wpisu o podanej nazwie - `HAL_ENOENT`.
Rejestracja oraz sprawdzanie danych identyfikujących wpis odbywają się atomowo
pod blokadą routera. Między sprawdzeniem a zmianą wpisu inny wątek nie może
więc zmienić jego stanu.

**Współbieżność funkcji obsługi:** Router nie szereguje ich wykonania. Ta sama funkcja może działać równocześnie w kilku zadaniach lub adapterach, dlatego aplikacja musi chronić współdzielony stan wskazywany przez `user`.

`hal_command_response_write()` i `hal_command_response_write_str()` dopisują
dane do bufora odpowiedzi o stałym rozmiarze. Funkcja pomocnicza ustawiająca
kodowanie wybiera również typową wartość Content-Type. W razie przepełnienia
zwracany jest `HAL_EOVERFLOW`, a `response.overflow` otrzymuje wartość `true`.
Router nie kopiuje danych wskazywanych przez `message` i `content_type`.
Wartości ustawione przez handler muszą więc pozostać ważne przez cały czas,
w którym wywołujący korzysta z gotowej odpowiedzi.

Przed wyszukaniem handlera router zeruje odpowiedź. Jeżeli handler ustawi
`response.status` na wartość inną niż `HAL_OK`, ma ona pierwszeństwo przed
zwróconym przez niego `HAL_OK`. Jeśli natomiast handler zwróci błąd, a status
odpowiedzi nadal wynosi `HAL_OK`, wynik handlera staje się statusem odpowiedzi.
Wartość `HAL_NONE` w którymkolwiek z tych miejsc jest zamieniana na
`HAL_EINTERNAL`. Funkcje pomocnicze zamieniające źródło, kodowanie i typ
wiadomości na tekst zwracają `"UNKNOWN"` dla wartości spoza odpowiednich typów
wyliczeniowych.

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

Źródłem żądania może być wywołanie bezpośrednie, HTTP, WebSocket, Serial Session, łącze LoRa lub BLE Stream. Adapter deklaruje flagami uwierzytelnienie, szyfrowanie, integralność i ochronę przed powtórzeniem (replay). Router sprawdza wymagane flagi, ale sam nie zabezpiecza transmisji.

<a id="format-transmisyjny-wiadomości"></a>

## Binarny format wiadomości

```c
#include <hal/commands/hal_command_wire.h>
#include <string.h>
```

Funkcje `hal_command_wire` kodują pojedynczą wiadomość `REQUEST`, `RESPONSE`
lub `EVENT` do bufora dostarczonego przez wywołującego. Dekoder odczytuje
dokładnie jedną pełną wiadomość i kopiuje dane do pól struktury o stałej
pojemności. `hal_command_message_frame_size()` pozwala adapterom pakietowym
i strumieniowym określać długość pierwszej ramki w miarę napływania danych.
Na przykład:

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
| 2 | wersja formatu |
| 3 | typ wiadomości |
| 4 | kodowanie argumentów lub danych |
| 5 | długość nazwy komendy lub zdarzenia |
| 6..7 | zarezerwowane, zero |
| 8..11 | identyfikator żądania |
| 12..13 | wartość statusu odpowiedzi `hal_status_t` ze znakiem |
| 14..15 | długość danych |
| 16.. | nazwa, po której następują dane |

Żądanie ma niezerowy identyfikator, nazwę i status `HAL_NONE`. Zdarzenie ma
nazwę, identyfikator zero i status `HAL_NONE`. Odpowiedź ma niezerowy
identyfikator, brak nazwy i status inny niż `HAL_NONE`. Dekoder odrzuca
nieznane wersje, nieprawidłowe wartości typów wyliczeniowych, niezerowe bajty zarezerwowane,
uszkodzone nazwy i każdą niezgodność rozmiaru, zwracając `HAL_EPROTO`.

Jeśli bufor wyjściowy kodera jest zbyt mały, funkcja zwraca `HAL_EOVERFLOW`
i zapisuje wymagany rozmiar w `out_length`, nie tworząc częściowej wiadomości.
`hal_command_source_to_string()`, `hal_command_encoding_to_string()` oraz
`hal_command_message_type_to_string()` zwracają stałe nazwy przeznaczone do
diagnostyki.

Jeśli bufor strumienia nie zawiera jeszcze pełnej ramki,
`hal_command_message_frame_size()` zwraca `HAL_EAGAIN`. Po odebraniu całego
stałego nagłówka funkcja podaje również wymaganą łączną długość. Gdy w buforze
znajduje się już tyle bajtów, zwraca `HAL_OK`, nawet jeśli za nimi zaczyna się
kolejna ramka. Do dekodera przekaż dokładnie wskazany prefiks, a pozostałe
bajty zachowaj do następnego wywołania.
`HAL_COMMAND_WIRE_MAX_FRAME_SIZE` określa podczas kompilacji maksymalny rozmiar
wewnętrznego bufora adaptera. Format transmisyjny nie dodaje szyfrowania ani
uwierzytelnienia; odpowiada za nie adapter transportowy.

<a id="adapter-ramkowanej-sesji-szeregowej-framed-serial-session"></a>

## Polecenia przez sesję szeregową

```c
#include <hal/serial/hal_serial_commands.h>
```

Zainicjalizuj `hal_serial_session`, zarejestruj w routerze funkcje obsługi pod dotychczasowymi nazwami SC i dołącz jeden adapter, którego pamięcią zarządza aplikacja:

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

Kompilowalny przykład
[`28_serial_commands`](../../../examples/28_serial_commands/README.pl.md)
pokazuje cały cykl życia: niezależny router, handlery `echo` i `info`, reguły
dotyczące źródła, ramkowane żądania oraz cofnięcie częściowej inicjalizacji po
błędzie uruchamiania.

`hal_serial_commands_init()` przejmuje funkcję zwrotną sesji obsługującą
nierozpoznane dane. Jeśli funkcja jest już ustawiona, inicjalizacja zwraca
`HAL_EBUSY` zamiast ją zastępować. HELLO, BYE oraz opcjonalna wymiana
uwierzytelniająca pozostają w `hal_serial_session`.
Dane aplikacji pasujące do konfiguracji adaptera są akceptowane dopiero po
tym, jak HELLO aktywuje sesję. Białe znaki wyznaczają koniec pierwszego tokenu.
Adapter pozostawia jego nazwę bez zmian i traktuje resztę jako argumenty. Dlatego
`SC_GET_PARAM nominal_rpm` wywołuje w routerze komendę `SC_GET_PARAM`
z `nominal_rpm` przekazanym jako argument TEXT lub JSON.

Opcjonalny predykat `allow_inactive(request, user)` może dopuścić wybrane
pasujące komendy jeszcze przed HELLO. Wynik `true` przekazuje żądanie do
zwykłego sprawdzenia reguł routera; nie oznacza uwierzytelnienia i nie pomija
wymagań dotyczących źródła ani zabezpieczeń. Pozwala to na przykład zachować
dotychczasową, nieuwierzytelnioną odpowiedź `NOT_AUTHORIZED` komendy przejścia
do bootloadera przed skonfigurowaniem sesji. Ustaw predykat na `NULL`, aby każda
pasująca komenda nadal wymagała wcześniejszego HELLO.

Adapter ustawia w żądaniu `HAL_COMMAND_SOURCE_SERIAL_SESSION`, sekwencję ramki
SC jako `request_id`, identyfikator Serial Session jako `session_id`,
skonfigurowany `peer_id` oraz wskaźnik sesji jako `source_context`.
Uwierzytelniona sesja ustawia wyłącznie
`HAL_COMMAND_SECURITY_AUTHENTICATED`. Serial Session nie informuje routera
o szyfrowaniu, kryptograficznej ochronie integralności wiadomości ani ochronie
przed powtórzeniem.

Niepusta odpowiedź handlera w formacie TEXT lub JSON jest wysyłana bez zmian
w ramce o tym samym numerze sekwencyjnym. Puste odpowiedzi i dane w innym
kodowaniu obsługuje opcjonalna funkcja zwrotna `formatter`. Aplikacja może
dzięki temu przypisywać statusom routera dotychczasowe odpowiedzi SC bez
dodawania do JaszczurHAL tokenów charakterystycznych dla konkretnego projektu.
Bez funkcji `formatter` adapter wysyła `OK` albo `ERR <HAL_STATUS>`.

`command_prefix` jest opcjonalny i pozostaje częścią nazwy przekazywanej do
routera. Jeśli prefiks został ustawiony, dane bez pasującego prefiksu trafiają
do opcjonalnej funkcji zwrotnej `fallback`. Pozwala to zachować istniejące
komendy diagnostyczne spoza przestrzeni nazw SC. Bez funkcji `fallback` adapter
korzysta z odpowiedzi „nieznana komenda" zdefiniowanej w słowniku sesji.
Komenda SC z pasującym prefiksem, ale bez zarejestrowanego handlera, zawsze
trafia do routera, więc funkcja `formatter` może sformatować odpowiedź routera.

Maksymalny rozmiar odpowiedzi określa `HAL_SERIAL_FRAME_PAYLOAD_MAX`. Adapter
odrzuca treść zawierającą NUL, `*`, CR albo LF i akceptuje wyłącznie kodowania
TEXT oraz JSON, ponieważ dane SC mają postać pojedynczego wiersza.
`hal_serial_commands_get_last_status()` zwraca ostatni zapamiętany błąd
routera, formatowania lub danych. `hal_serial_commands_deinit()` zwraca
`HAL_EBUSY`, jeśli trwa wykonywanie `allow_inactive`, handlera, funkcji
`formatter`, funkcji `fallback` albo wysyłanie odpowiedzi. Funkcja zwrotna
sesji jest usuwana tylko wtedy, gdy nadal jest zarejestrowana przez ten adapter.

<a id="reliable-lora-adapter"></a>

<a id="niezawodny-adapter-lora"></a>

## Polecenia przez łącze LoRa

```c
#include <hal/radio/hal_lora_commands.h>
```

Utwórz i skonfiguruj radio oraz łącze LoRa. Przed dołączeniem adaptera łącze musi odbierać dane. Pole `router` równe `NULL` wybiera wspólny router domyślny.

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

Przed rozpoczęciem wysyłania przez łącze adapter kopiuje i koduje całe żądanie.
Identyfikator żądania jest niezerowy, a licznik zwiększa się dopiero po
przyjęciu transmisji przez łącze. W przypadku `HAL_EBUSY` lub `HAL_EAGAIN`
licznik się nie zmienia, a do `out_request_id` zapisywane jest zero. Ustawienie
`acknowledged` dotyczy żądań, odpowiedzi oraz zdarzeń przesyłanych bezpośrednio
między urządzeniami.

`hal_lora_commands_event_start()` wysyła nazwane zdarzenie z identyfikatorem
zero. Zdarzenia rozgłoszeniowe (broadcast) zawsze wyłączają potwierdzenie
transportowe niezależnie od skonfigurowanego ustawienia. Żądania nie mogą
używać adresu rozgłoszeniowego.

Po dołączeniu tylko adapter może wywoływać funkcje przetwarzania i odbioru
łącza. Używaj `hal_lora_commands_process()` zamiast
`hal_lora_link_process()` i `hal_lora_link_receive()`. Przychodzące żądania
są dekodowane i synchronicznie przekazywane do routera, a odpowiedzi wysyłane
automatycznie. Odebrane odpowiedzi i zdarzenia można skopiować przez
`hal_lora_commands_receive()`:

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

`hal_lora_commands_receive()` zwraca `HAL_EAGAIN`, gdy nie ma odpowiedzi ani zdarzenia do odczytu; nie usuwa wtedy niczego z kolejki. Po poprawnym zniszczeniu adaptera wywołanie API ze starym uchwytem zwraca `HAL_EUNINIT`.

`hal_lora_commands_process()` można wywoływać tylko z jednego kontekstu
wykonania. Wywołanie współbieżne lub ponowne przed zakończeniem poprzedniego
zwraca `HAL_EBUSY`. Przed uruchomieniem handlera adapter zwalnia swój muteks.
Handler może dzięki temu bezpiecznie sprawdzić stan adaptera, odebrać już
zakolejkowaną wiadomość dla
aplikacji albo spróbować wysłać żądanie lub zdarzenie. Próba wysyłki w czasie,
gdy łącze potwierdza przychodzące żądanie, zwykle zwraca `HAL_EBUSY` albo
`HAL_EAGAIN`.

Adapter przechowuje najwyżej jedną odpowiedź lub jedno zdarzenie oczekujące na
odbiór przez aplikację. Jeśli łącze jest zajęte wysyłaniem potwierdzenia
transportowego, odpowiedź na przychodzące żądanie pozostaje w wewnętrznym
buforze adaptera. Kontynuuj wywoływanie `hal_lora_commands_process()`, aby
ponowić wysyłkę. Adapter i łącze kopiują dane do buforów o stałej pojemności.
Router i łącze muszą pozostać dostępne przez cały czas działania adaptera.

Próba zniszczenia adaptera zwraca `HAL_EBUSY`, jeśli trwa przetwarzanie lub wykonywanie polecenia, odpowiedź czeka na wysłanie, aplikacja nie odebrała wiadomości albo łącze nie wróciło do odbioru. Kontynuuj przetwarzanie i odbierz oczekujące wiadomości przed ponowną próbą.

Szyfrowane łącze LoRa ustawia wszystkie flagi bezpieczeństwa komend, natomiast
łącze przesyłające dane jawne nie ustawia żadnej. Handler może więc wymagać
uwierzytelnienia i ochrony przed powtórzeniem bez analizowania metadanych
właściwych dla LoRa. Podczas wykonywania handlera kompletny
`hal_lora_link_message_info_t` jest dostępny jako kontekst źródła żądania.

`hal_lora_commands_get_info()` zwraca stan kolejki, łącza, przetwarzania
i wykonywania handlera. `hal_lora_commands_get_diagnostics()` zwraca liczniki
żądań, odpowiedzi, zdarzeń, błędów protokołu, wywołań handlerów, ponowień
i odrzuceń.

<a id="authenticated-ble-stream-adapter"></a>

<a id="uwierzytelniony-adapter-ble-stream"></a>

## Polecenia przez uwierzytelniony BLE Stream

```c
#include <hal/bluetooth/hal_ble_commands.h>
```

Zainicjalizuj `hal_ble`, udostępnij usługę `hal_ble_stream`, ustaw jej sekret i dołącz jeden adapter poleceń. Nadal samodzielnie obsługuj kontroler oraz rozgłaszanie:

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

Po dołączeniu adapter jako jedyny może przesyłać przez wspólny BLE Stream dane
w formacie komend. Nie wywołuj wtedy `hal_ble_stream_send()` ani
`hal_ble_stream_receive()`. Adapter nie wykonuje `hal_ble_poll()`, nie
inicjalizuje Stream, nie ustawia sekretu, nie uruchamia advertisingu i nie
zarządza routerem. Obecny transport BLE działa jako Peripheral połączony
z jednym urządzeniem Central. Wiadomości komend są dwukierunkowe, dlatego
adapter nie wymaga określenia roli ani adresu docelowego.

`hal_ble_commands_request_start()` i `hal_ble_commands_event_start()` kopiują
całą wiadomość do wewnętrznego bufora adaptera o stałej pojemności. Wymagają
uwierzytelnionej sesji Stream i w przeciwnym razie zwracają `HAL_EAUTH`.
Pomyślnie zaakceptowane żądanie otrzymuje niezerowy identyfikator
natychmiast. Kolejne wywołania zwracają `HAL_EBUSY`, dopóki bufor nadawczy
zajmuje inna zakodowana wiadomość lub automatyczna odpowiedź. Wywołuj
`hal_ble_commands_process()` do czasu umieszczenia wszystkich fragmentów
w kolejce Stream.

Jedna wiadomość w formacie transmisyjnym może obejmować wiele
uwierzytelnionych bloków danych `DATA`. Maksymalną liczbę bajtów jednego
fragmentu wyznacza wzór:

```text
min(HAL_BLE_STREAM_MAX_PAYLOAD, negotiated_ATT_MTU - 31)
```

Narzut 31 bajtów obejmuje ATT, ramkowanie Stream, licznik kierunkowy i tag
uwierzytelnienia. Przy ATT MTU wynoszącym co najmniej 159 bajtów treść komendy
o długości 500 bajtów potrzebuje pięciu fragmentów w każdym kierunku. Odbiornik
wywołuje `hal_command_message_frame_size()` w miarę
napływania danych, zachowuje bajty znajdujące się za kompletną wiadomością
i podczas jednego wywołania `process()` przekazuje do routera najwyżej jedno
żądanie.

Adapter synchronicznie przekazuje przychodzące żądania do routera i automatycznie wysyła odpowiedzi. Odpowiedzi oraz zdarzenia przeznaczone dla aplikacji odczytuj przez `hal_ble_commands_receive()`:

```c
hal_command_message_t message;
hal_ble_commands_peer_info_t peer;
if (hal_ble_commands_receive(commands, &message, &peer) == HAL_OK) {
  /* Dopasuj odpowiedź po request_id lub odbierz zdarzenie po nazwie. */
}
```

Każde żądanie BLE przekazywane do routera ma ustawione
`HAL_COMMAND_SECURITY_ALL`. Do adaptera trafiają bowiem wyłącznie dane
odebrane przez Stream, który zapewnia wzajemne uwierzytelnienie, szyfrowanie,
ochronę integralności i ochronę przed powtórzeniem. `peer_id` zawiera bez
utraty informacji typ adresu oraz sześć bajtów adresu BLE. `session_id` jest
publicznym, losowym identyfikatorem uzgodnionej sesji Stream, zapisanym jako
64-bitowa wartość little-endian. Podczas wykonywania handlera `source_context`
wskazuje na tymczasowo udostępnioną strukturę `hal_ble_commands_peer_info_t`.
Zawiera ona adres, połączenie, MTU, wynegocjowany zestaw funkcji, numery
generacji, identyfikator sesji oraz wartości licznika `DATA` Stream dla
pierwszego i ostatniego fragmentu wiadomości. Wskaźnik jest ważny tylko do
zakończenia handlera.

Rozłączenie lub nowa uwierzytelniona sesja czyszczą niekompletne dane odbioru,
niedokończone wysyłki i nieprzeczytane wiadomości z poprzedniej sesji
z urządzeniem zdalnym.
Po utracie fragmentu RX Stream, odebraniu uszkodzonego nagłówka komendy,
wykryciu nieciągłości liczników fragmentów albo przekroczeniu limitu czasu
oczekiwania na resztę ramki nie można już wiarygodnie określić jej granicy.
Adapter zamyka wtedy sesję Stream zamiast próbować odgadnąć punkt ponownej
synchronizacji. Wartość limitu czasu określa `partial_frame_timeout_ms`; zero
w konfiguracji wybiera
`HAL_BLE_COMMANDS_PARTIAL_FRAME_TIMEOUT_MS`.

`hal_ble_commands_process()` zwraca `HAL_OK`, jeśli przetworzył dane, oraz
`HAL_EAGAIN`, gdy nie ma nic do zrobienia albo oczekuje na miejsce w Stream lub
uwierzytelnienie. Błąd protokołu, upływ limitu czasu albo przepełnienie są
zwracane tylko raz i zapisywane w diagnostyce. W kolejce może znajdować się
tylko jedna odpowiedź lub jedno zdarzenie oczekujące na odbiór przez aplikację.
Próba zniszczenia adaptera zwraca `HAL_EBUSY`, jeśli pozostały nieprzetworzone
dane zakodowanej wiadomości, router przetwarza żądanie albo aplikacja nie
odebrała wiadomości. Użycie nieaktualnego uchwytu powoduje zwrócenie
`HAL_EUNINIT`.

<a id="kompatybilność-sieciowa"></a>

## Zgodność z dotychczasowym API sieciowym

`hal_net_commands` zachowuje dotychczasowe API dla tekstu/JSON, cJSON, HTTP
i WebSocket, ale rejestruje i wykonuje komendy przez wspólny router domyślny.
Dotychczasowy typ handlera nadal przyjmuje tylko wywołania bezpośrednie, HTTP
i WebSocket. Zarejestruj ogólny
`hal_command_definition_t` w domyślnym routerze, gdy ten sam handler musi
obsługiwać zarówno źródła sieciowe, jak i LoRa. W trybie TEXT handler otrzymuje
bajty znajdujące się za nazwą komendy. W trybie JSON otrzymuje zwartą
serializację wartości `args` albo `params`. Dla źródeł sieciowych identyfikatory
żądania, węzła zdalnego i sesji oraz flagi zabezpieczeń są równe zero.
Dotychczasowe funkcje zliczania i wyrejestrowywania działają na wspólnym
zestawie handlerów.

`hal_net_commands_clear()` czyści cały router domyślny, również wpisy innych adapterów. Zwraca `hal_status_t`. Jeśli wykonywane jest dowolne zarejestrowane polecenie, zwraca `HAL_EBUSY` i pozostawia zestaw bez zmian.

Wspólna struktura odpowiedzi zachowuje dotychczasowe pola odpowiedzi sieciowej
w niezmienionej kolejności, a na końcu dodaje niezależne od transportu pole
`encoding`.

Samo `HAL_ENABLE_BLE_STREAM` udostępnia ogólny uwierzytelniony strumień bajtów, bez routera. Włącz `HAL_ENABLE_BLE_COMMANDS` tylko wtedy, gdy cały strumień ma służyć wiadomościom protokołu poleceń.

## Limity ustalane podczas kompilacji

Zdefiniuj limity przed dołączeniem nagłówków HAL:

| Makro | Domyślnie | Dozwolony zakres | Przeznaczenie |
|---|---:|---:|---|
| `HAL_COMMAND_ROUTER_MAX_INSTANCES` | 2 | 1..16 | Pula routerów, łącznie z domyślnym routerem |
| `HAL_COMMAND_ROUTER_MAX_COMMANDS` | 8 | 1..64 | Liczba miejsc na handlery w jednym routerze |
| `HAL_COMMAND_ROUTER_NAME_MAX` | 32 | 2..256 | Rozmiar bufora nazwy komendy wraz z terminatorem |
| `HAL_COMMAND_RESPONSE_BUFFER_SIZE` | 512 | 32..65535 | Rozmiar bufora odpowiedzi handlera |
| `HAL_COMMAND_MESSAGE_MAX_PAYLOAD` | 512 | 1..65535 | Rozmiar wewnętrznego bufora danych wiadomości |
| `HAL_BLE_COMMANDS_PARTIAL_FRAME_TIMEOUT_MS` | 5000 | większe od zera | Maksymalny czas oczekiwania na resztę wiadomości BLE w formacie komend |

Starsze makra określające rozmiary `net-command` są aliasami odpowiadających im
wspólnych limitów routera. Jeśli zdefiniowano obie nazwy tego samego limitu,
ich wartości muszą być jednakowe.
