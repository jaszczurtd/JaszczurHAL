# API niezawodnego łącza LoRa

*Dostępne również [po angielsku](../en/22_lora_link.md).*

> **Część [dokumentacji API JaszczurHAL](../../pl/JaszczurHAL_API.md)**

`hal_lora_link` jest niewielką, prywatną warstwą komunikacji point-to-point nad
jednym skonfigurowanym uchwytem [`hal_lora_radio`](21_lora.md). Dodaje
adresowanie 16-bitowe, 32-bitowe sekwencje wiadomości, potwierdzenia,
ograniczoną liczbę ponowień całej wiadomości, eliminowanie duplikatów i
przezroczystą fragmentację. Opcjonalny ChaCha20-Poly1305 zapewnia poufność i
uwierzytelnianie każdego fragmentu danych oraz uwierzytelnia potwierdzenia.

Ten protokół jest specyficzny dla JaszczurHAL. Nie jest LoRaWAN, nie ma
certyfikacji LoRa Alliance, nie jest routowalny ani zgodny z bramkami LoRaWAN.
Aplikacja odpowiada za zgodne z prawem częstotliwość, moc, airtime i duty cycle.

## Włączanie modułu

Wybierz łącze oraz dokładnie jednego providera surowej komunikacji radiowej:

```c
#pragma once

#define HAL_ENABLE_SX126X
#define HAL_ENABLE_LORA_LINK
```

`HAL_ENABLE_LORA_LINK` propaguje `HAL_ENABLE_LORA` i `HAL_ENABLE_CRC`.
Wybrany provider SX126x lub SX127x propaguje `HAL_ENABLE_SPI`. Jeśli używasz
`HAL_LORA_LINK_SECURITY_CHACHA20_POLY1305`, zdefiniuj również
`HAL_ENABLE_CRYPTO`.

Przed dołączeniem `hal_config.h` można ustawić następujące ograniczenia czasu
buildu:

| Makro | Domyślnie | Poprawny zakres | Przeznaczenie |
|---|---:|---:|---|
| `HAL_LORA_LINK_MAX_INSTANCES` | 2 | 1..255 | Statyczne sloty łączy oznaczone generacją |
| `HAL_LORA_LINK_MAX_MESSAGE_SIZE` | 1024 | 1..4096 | Należące do łącza bufory kopiowanych wiadomości TX i RX |
| `HAL_LORA_LINK_MAX_PEERS` | 8 | 1..32 | Liczba okien eliminowania duplikatów źródło/sesja przechowywanych przez łącze |

Każde łącze ma również dwa 255-bajtowe bufory robocze ramek. Po utworzeniu
muteksu dla uchwytu żadna operacja protokołu nie alokuje pamięci z heapu.

## Cykl życia

Najpierw utwórz i skonfiguruj surowe radio, a następnie dołącz łącze:

```c
hal_lora_link_t link = NULL;
hal_lora_link_config_t config =
    hal_lora_link_config_defaults(radio, UINT16_C(0x1001), session_id);

hal_status_t status = hal_lora_link_create(&config, &link);
if (status != HAL_OK) {
  return status;
}
```

Lokalny adres zero jest zarezerwowany, a `0xFFFF` oznacza broadcast. Niezerowy
identyfikator sesji rozróżnia restarty tego samego adresu. Musi być nowy dla
każdej sesji adresu/klucza. Gdy szyfrowanie jest włączone, użyj wartości
losowej kryptograficznie albo trwałego, monotonicznego licznika uruchomień;
nigdy nie wyprowadzaj jej wyłącznie z przewidywalnego zegara uptime.

Łącze przejmuje wyłączną kontrolę operacyjną nad radiem, usuwa jego callback
surowych zdarzeń i rozpoczyna ciągły odbiór. Wywołujący musi utrzymywać radio
przy życiu, ale do powrotu `hal_lora_link_destroy()` nie może wykonywać surowych
operacji TX, RX, CAD, sleep ani kalibracji. Zniszczenie łącza anuluje aktywne
I/O radia, zeruje skopiowany klucz i pozostawia radio w stanie standby; nie
niszczy uchwytu radia.

Nieprzezroczyste uchwyty łączy mają znacznik generacji. Nieaktualny uchwyt
zwraca `HAL_EUNINIT`, a próba utworzenia więcej niż
`HAL_LORA_LINK_MAX_INSTANCES` łączy zwraca `HAL_ENOMEM`.

## Wysyłanie i odbieranie

`hal_lora_link_send_start()` kopiuje całą wiadomość, rozpoczyna transmisję
pierwszego fragmentu i wraca. Wywołuj `hal_lora_link_process()` regularnie z
jednej głównej pętli lub taska FreeRTOS będącego właścicielem:

```c
static const uint8_t message[] = "acknowledged telemetry";

status = hal_lora_link_send_start(link, UINT16_C(0x1002), 3u, message,
                                  sizeof(message) - 1u, true);
while (status == HAL_OK || status == HAL_EAGAIN || status == HAL_IGNORED) {
  status = hal_lora_link_process(link);

  hal_lora_link_send_status_t send;
  if (hal_lora_link_get_send_status(link, &send) == HAL_OK &&
      send.state != HAL_LORA_OPERATION_IN_PROGRESS) {
    break;
  }
}
```

Port zdefiniowany przez aplikację jest przenoszony bez zmian. Unicast może być
potwierdzany lub niepotwierdzany; broadcast musi być niepotwierdzany. Łącze
może przechowywać jednocześnie tylko jedną wiadomość wysyłaną przez aplikację i
jedną kompletną wiadomość odebraną.

`hal_lora_link_receive()` kopiuje i zużywa oczekującą kompletną wiadomość.
`HAL_EAGAIN` oznacza, że żadna wiadomość nie jest gotowa. Jeśli bufor
wywołującego jest za mały, funkcja zwraca `HAL_EOVERFLOW`, podaje wymagany
rozmiar i mimo to zużywa wiadomość.

```c
uint8_t buffer[HAL_LORA_LINK_MAX_MESSAGE_SIZE];
size_t length = 0u;
hal_lora_link_message_info_t info;

status = hal_lora_link_receive(link, buffer, sizeof(buffer), &length, &info);
if (status == HAL_OK) {
  /* info contains source, destination, session, sequence, port and RF data. */
}
```

`hal_lora_link_cancel()` zatrzymuje wyłącznie aktywne wysyłanie aplikacji i
wznawia ciągły odbiór. Snapshoty stanu, statusu wysyłania i diagnostyki są
chronione muteksem uchwytu i można je odczytywać z innego taska. Cała maszyna
stanów `process()` nadal musi mieć jednego logicznego właściciela.

## Adapter poleceń

`HAL_ENABLE_LORA_COMMANDS` dodaje adapter
[`hal_lora_commands`](23_commands.md#reliable-lora-adapter) oraz propaguje
`HAL_ENABLE_COMMAND_ROUTER` i `HAL_ENABLE_LORA_LINK`. Koduje ograniczone
wiadomości żądań, odpowiedzi i zdarzeń na jednym porcie łącza zdefiniowanym
przez aplikację:

```c
hal_lora_commands_config_t commands_config =
    hal_lora_commands_config_defaults(link, 7u);
hal_lora_commands_t commands = NULL;

status = hal_lora_commands_create(&commands_config, &commands);
if (status == HAL_OK) {
  uint32_t request_id = 0u;
  status = hal_lora_commands_request_start(
      commands, UINT16_C(0x1002), "status", HAL_COMMAND_ENCODING_TEXT,
      NULL, 0u, &request_id);
}
```

Przed dołączeniem adaptera łącze musi już odbierać. Adapter staje się wtedy
jedynym właścicielem przetwarzania i odbioru: wywołuj
`hal_lora_commands_process()` zamiast dwóch odpowiadających mu funkcji łącza.
Przychodzące żądania są synchronicznie przekazywane do skonfigurowanego routera,
a odpowiedzi wysyłane automatycznie. Odpowiedzi i zdarzenia widoczne dla
aplikacji odbiera się przez `hal_lora_commands_receive()`.

Łącza plaintext nie zgłaszają flag bezpieczeństwa poleceń. Łącze używające
`HAL_LORA_LINK_SECURITY_CHACHA20_POLY1305` zgłasza dostarczenie uwierzytelnione,
zaszyfrowane, chronione integralnościowo i przed replayem, dzięki czemu polityki
routera mogą odrzucać słabiej chronione żądania. Handler otrzymuje adres źródła,
sesję i kompletne metadane łącza bez wiązania logiki polecenia z providerem
radia.

## Niezawodność i fragmentacja

Domyślna polityka czeka 1500 ms na jedno potwierdzenie po wysłaniu kompletnej
wiadomości, stosuje backoff 200 ms i ponawia całą niezmienną wiadomość do trzech
razy. Konfiguracja może ograniczyć timeout potwierdzenia, liczbę ponowień,
backoff i czas życia niepełnej rekonstrukcji. `attempts` obejmuje pierwszą
transmisję i jest wystarczająco szerokie, aby zgłosić wszystkie 256 prób
dozwolonych przez 8-bitowe pole `max_retries`. Wyczerpanie prób kończy wysyłanie
wynikiem `HAL_ETIMEOUT`.

Wersjonowany nagłówek o długości 25 bajtów pozostawia 230 bajtów na
niechroniony fragment albo 214 bajtów, gdy dołączony jest 16-bajtowy tag AEAD.
Wiadomość jest dzielona na najwyżej 32 fragmenty. Odbiornik sprawdza
zadeklarowaną strukturę i rekonstruuje wyłącznie fragmenty o zgodnym źródle,
targetu, sesji, sekwencji, porcie i tożsamości wiadomości. Wiadomości plaintext
zawierają jedno CRC-32 całej wiadomości; wykrywa ono przypadkowe uszkodzenie,
ale nie zapewnia uwierzytelniania.

Po pełnej rekonstrukcji odbiornik zapisuje sekwencję źródła/sesji w przesuwnym
oknie 32 wiadomości. Ponowiona wiadomość nie jest dostarczana drugi raz, ale jej
ostatni fragment wywołuje kolejne potwierdzenie, co umożliwia odzyskanie po
utracie ACK. Okna wykraczające poza skonfigurowaną tablicę peerów są usuwane
według zasady least recently used.

## Opcjonalna ochrona kryptograficzna

Przy włączonym `HAL_ENABLE_CRYPTO` ustaw jeden 32-bajtowy klucz współdzielony i
wybierz AEAD:

```c
uint8_t provisioned_key[HAL_LORA_LINK_CRYPTO_KEY_BYTES];
/* Load a secret from protected provisioning or storage. */

hal_lora_link_config_t config =
    hal_lora_link_config_defaults(radio, local_address, fresh_session_id);
config.security = HAL_LORA_LINK_SECURITY_CHACHA20_POLY1305;
config.key = provisioned_key;
config.key_length = sizeof(provisioned_key);
status = hal_lora_link_create(&config, &link);
```

Klucz jest kopiowany do pamięci należącej do łącza. Nonce łączy sesję nadawcy,
adres źródłowy, sekwencję, indeks fragmentu i typ ramki. Dlatego ta sama
kombinacja klucza, adresu i sesji nie może zostać użyta ponownie, a szyfrowane
łącze odmawia wysyłania po wyczerpaniu 32-bitowej przestrzeni sekwencji.
Ponowienia używają dokładnie tej samej tożsamości wiadomości, a więc tej samej
uwierzytelnionej ramki; nigdy nie szyfrują innym plaintextem przy tym nonce.

AEAD uwierzytelnia cały nagłówek, ciphertext i potwierdzenia. Nie ukrywa
adresów, identyfikatorów sesji, sekwencji, rozmiarów ani liczby fragmentów.
Provisioning i rotacja kluczy oraz trwałe zarządzanie sesją pozostają
odpowiedzialnością aplikacji. Łącze plaintext odrzuca ramki szyfrowane, a łącze
szyfrowane odrzuca ramki plaintext lub nieuwierzytelnione.

## Diagnostyka i przykład

`hal_lora_link_get_diagnostics()` zwraca sumy wiadomości i ramek,
potwierdzenia, retransmisje, duplikaty, błędy formatu, uwierzytelniania i
integralności, odrzucenia i timeouty rekonstrukcji, odrzucenia kolejki, timeouty
wysyłania, anulowania oraz ostatnio obserwowane adresy i parametry RF.

`examples/27_lora_point_to_point` udostępnia warianty `link` i
`link-responder`. Wymieniają one skorelowane, 500-bajtowe żądanie polecenia i
odpowiedź przez `hal_lora_commands`, wymuszając po trzy fragmenty w obu
kierunkach na tych samych fixture'ach SX1262 co przykład surowego radia.
Przykład celowo używa plaintextu; produkt powinien włączać AEAD dopiero po
opracowaniu rzeczywistego provisioningu kluczy i strategii unikalnych sesji.
