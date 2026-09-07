<a id="api-niezawodnego-łącza-lora"></a>

# LoRa - wiadomości, potwierdzenia i ponowienia

*Dostępne również [po angielsku](../en/22_lora_link.md).*

> **Część [dokumentacji API JaszczurHAL](../../pl/JaszczurHAL_API.md)**

Wymiana wiadomości między urządzeniami LoRa z adresowaniem, potwierdzeniami i ponawianiem transmisji. `hal_lora_link` działa na jednym skonfigurowanym uchwycie [`hal_lora_radio`](21_lora.md). Obsługuje adresy 16-bitowe, 32-bitowe numery sekwencyjne, ograniczoną liczbę ponowień całej wiadomości, pomijanie duplikatów i automatyczny podział na fragmenty. Opcjonalne ChaCha20-Poly1305 szyfruje i uwierzytelnia fragmenty oraz uwierzytelnia potwierdzenia.

To własny protokół JaszczurHAL, nie LoRaWAN. Nie ma certyfikacji LoRa Alliance, routingu ani zgodności z bramkami LoRaWAN. Aplikacja odpowiada za zgodny z przepisami dobór częstotliwości, mocy, czasu transmisji i współczynnika zajętości pasma (duty cycle).

## Włączanie modułu

Włącz łącze oraz obsługę dokładnie jednej rodziny układów radiowych:

```c
#pragma once

#define HAL_ENABLE_SX126X
#define HAL_ENABLE_LORA_LINK
```

`HAL_ENABLE_LORA_LINK` automatycznie włącza `HAL_ENABLE_LORA` i `HAL_ENABLE_CRC`.
Wybrana rodzina SX126x lub SX127x włącza również `HAL_ENABLE_SPI`. Jeśli używasz
`HAL_LORA_LINK_SECURITY_CHACHA20_POLY1305`, zdefiniuj również
`HAL_ENABLE_CRYPTO`.

Limity można ustawić przed dołączeniem `hal_config.h`:

| Makro | Domyślnie | Dozwolony zakres | Przeznaczenie |
|---|---:|---:|---|
| `HAL_LORA_LINK_MAX_INSTANCES` | 2 | 1..255 | Liczba miejsc w statycznej puli uchwytów oznaczonych numerem generacji |
| `HAL_LORA_LINK_MAX_MESSAGE_SIZE` | 1024 | 1..4096 | Rozmiar wewnętrznych buforów na kopie wiadomości TX i RX |
| `HAL_LORA_LINK_MAX_PEERS` | 8 | 1..32 | Liczba przechowywanych okien wykrywania duplikatów dla par źródło/sesja |

Każde łącze ma dodatkowo dwa bufory robocze ramek, po 255 bajtów. Po utworzeniu muteksu uchwytu operacje protokołu nie przydzielają pamięci na stercie.

<a id="cykl-życia"></a>

## Utworzenie łącza i dostęp do radia

Najpierw utwórz i skonfiguruj radio przez API niskopoziomowe. Następnie dołącz łącze:

```c
hal_lora_link_t link = NULL;
hal_lora_link_config_t config =
    hal_lora_link_config_defaults(radio, UINT16_C(0x1001), session_id);

hal_status_t status = hal_lora_link_create(&config, &link);
if (status != HAL_OK) {
  return status;
}
```

Lokalny adres zero jest zarezerwowany, a `0xFFFF` oznacza adres rozgłoszeniowy.
Niezerowy identyfikator sesji pozwala rozróżnić kolejne uruchomienia urządzenia
o tym samym adresie. Każda sesja dla danej pary adresu i klucza musi otrzymać
nowy identyfikator. Przy włączonym szyfrowaniu użyj wartości wygenerowanej
przez kryptograficznie bezpieczne źródło losowe albo trwałego, monotonicznego
licznika uruchomień. Nie wyprowadzaj jej wyłącznie z przewidywalnego czasu
działania urządzenia.

Po dołączeniu łącze przejmuje wyłączną obsługę radia: wyrejestrowuje callback niskopoziomowy i uruchamia ciągły odbiór. Zachowaj uchwyt radia, ale do zakończenia `hal_lora_link_destroy()` nie uruchamiaj bezpośrednio TX, RX, CAD, uśpienia ani kalibracji. Zniszczenie łącza anuluje operacje wejścia/wyjścia, zeruje kopię klucza i pozostawia radio w stanie standby. Nie niszczy uchwytu radia.

Nieprzezroczyste uchwyty łączy zawierają numer generacji. Użycie nieaktualnego
uchwytu powoduje zwrócenie `HAL_EUNINIT`, a próba utworzenia większej liczby
łączy niż `HAL_LORA_LINK_MAX_INSTANCES` kończy się błędem `HAL_ENOMEM`.

## Wysyłanie i odbieranie

`hal_lora_link_send_start()` kopiuje całą wiadomość, rozpoczyna nadawanie pierwszego fragmentu i wraca bez oczekiwania na koniec całej transmisji. Dalej regularnie wywołuj `hal_lora_link_process()` z jednej pętli głównej lub jednego zadania FreeRTOS obsługującego łącze:

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

Numer portu zdefiniowany przez aplikację jest przesyłany bez zmian. Wiadomość
do jednego odbiorcy (unicast) może wymagać potwierdzenia lub nie, natomiast
transmisja rozgłoszeniowa musi odbywać się bez potwierdzenia. Łącze może
przechowywać jednocześnie tylko jedną wiadomość wysyłaną przez aplikację
i jedną kompletną wiadomość odebraną.

`hal_lora_link_receive()` kopiuje gotową wiadomość i usuwa ją z kolejki. `HAL_EAGAIN` oznacza brak wiadomości. **Zbyt mały bufor również powoduje usunięcie wiadomości:** funkcja zwraca wtedy `HAL_EOVERFLOW` i podaje wymagany rozmiar.

```c
uint8_t buffer[HAL_LORA_LINK_MAX_MESSAGE_SIZE];
size_t length = 0u;
hal_lora_link_message_info_t info;

status = hal_lora_link_receive(link, buffer, sizeof(buffer), &length, &info);
if (status == HAL_OK) {
  /* info contains source, destination, session, sequence, port and RF data. */
}
```

`hal_lora_link_cancel()` anuluje tylko transmisję rozpoczętą przez aplikację i przywraca ciągły odbiór. Stan łącza, stan transmisji i diagnostykę można odczytywać z innego zadania: funkcje zwracają spójne kopie chronione muteksem. Samo `process()` może jednak wykonywać tylko jedno zadanie lub jedna pętla.

<a id="adapter-poleceń"></a>

## Przesyłanie poleceń aplikacji

`HAL_ENABLE_LORA_COMMANDS` dodaje adapter
[`hal_lora_commands`](23_commands.md#reliable-lora-adapter) oraz propaguje
`HAL_ENABLE_COMMAND_ROUTER` i `HAL_ENABLE_LORA_LINK`. Adapter koduje wiadomości
żądań, odpowiedzi i zdarzeń o ograniczonym rozmiarze na jednym porcie łącza
zdefiniowanym przez aplikację:

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

Przed dołączeniem adaptera łącze musi już działać w trybie odbioru. Od tego
momentu tylko adapter może wywoływać funkcje przetwarzania i odbioru łącza:
używaj `hal_lora_commands_process()` zamiast dwóch odpowiadających im funkcji
łącza. Przychodzące żądania są synchronicznie przekazywane do skonfigurowanego
routera, a odpowiedzi wysyłane automatycznie. Odpowiedzi i zdarzenia
przeznaczone dla aplikacji odbiera się przez `hal_lora_commands_receive()`.

Łącza przesyłające dane jawne nie ustawiają flag bezpieczeństwa poleceń. Łącze
korzystające z `HAL_LORA_LINK_SECURITY_CHACHA20_POLY1305` oznacza wiadomość jako
uwierzytelnioną, zaszyfrowaną oraz chronioną przed zmianą i powtórzeniem.
Reguły routera mogą dzięki temu odrzucać słabiej chronione żądania. Funkcja
obsługi otrzymuje adres źródłowy, identyfikator sesji i kompletne metadane
łącza, a jej logika nie zależy od wybranej rodziny radia.

<a id="niezawodność-i-fragmentacja"></a>

## Potwierdzenia, ponowienia i składanie wiadomości

Domyślnie łącze czeka 1500 ms na potwierdzenie po wysłaniu całej wiadomości.
Przed kolejną próbą odczekuje 200 ms i może ponowić całą, niezmienioną
wiadomość maksymalnie trzy razy. W konfiguracji można ustawić limit czasu
oczekiwania na potwierdzenie, liczbę ponowień, odstęp między próbami oraz czas
przechowywania niekompletnie złożonej wiadomości. Pole `attempts` uwzględnia
pierwszą transmisję i ma zakres wystarczający do zapisania wszystkich 256 prób
dopuszczanych przez 8-bitowe `max_retries`. Po ich wyczerpaniu
wysyłanie kończy się statusem `HAL_ETIMEOUT`.

Wersjonowany nagłówek o długości 25 bajtów pozostawia 230 bajtów na
niechroniony fragment albo 214 bajtów, gdy dołączony jest 16-bajtowy tag AEAD.
Wiadomość jest dzielona na najwyżej 32 fragmenty. Odbiornik sprawdza
zadeklarowany układ i łączy wyłącznie fragmenty o zgodnym adresie źródłowym
i docelowym, identyfikatorze sesji, numerze sekwencyjnym, porcie oraz
identyfikatorze wiadomości. Wiadomości przesyłane bez szyfrowania
zawierają jedno CRC-32 całej wiadomości; wykrywa ono przypadkowe uszkodzenie,
ale nie zapewnia uwierzytelniania.

Po złożeniu całej wiadomości odbiornik zapisuje jej numer sekwencyjny
w 32-elementowym przesuwającym się oknie przypisanym do źródła i sesji. Ponowiona
wiadomość nie jest dostarczana drugi raz, ale jej ostatni fragment powoduje
ponowne wysłanie potwierdzenia. Pozwala to zakończyć transmisję po utracie ACK.
Gdy brakuje miejsca w skonfigurowanej tablicy urządzeń, zgodnie z zasadą LRU
usuwane jest najdłużej nieużywane okno.

## Opcjonalna ochrona kryptograficzna

Aby włączyć AEAD, zdefiniuj `HAL_ENABLE_CRYPTO`, przekaż 32-bajtowy klucz współdzielony (PSK) i wybierz odpowiedni tryb ochrony:

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

Klucz jest kopiowany do wewnętrznego bufora łącza. Wartość nonce składa się
z sesji nadawcy, adresu źródłowego, sekwencji, indeksu fragmentu i typu ramki.
Dlatego ta sama kombinacja klucza, adresu i sesji nie może zostać użyta
ponownie, a szyfrowane
łącze odmawia wysyłania po wyczerpaniu 32-bitowej przestrzeni sekwencji.
Ponowienia zachowują identyfikator wiadomości, a więc wysyłają ponownie tę samą
uwierzytelnioną ramkę. Ten sam nonce nigdy nie służy do zaszyfrowania różnych
danych jawnych.

AEAD uwierzytelnia cały nagłówek, szyfrogram i potwierdzenia. Nie ukrywa
adresów, identyfikatorów sesji, sekwencji, rozmiarów ani liczby fragmentów.
Aplikacja odpowiada za bezpieczne dostarczanie i rotację kluczy oraz trwałe
zarządzanie sesją. Łącze bez szyfrowania odrzuca ramki szyfrowane, a łącze
szyfrowane odrzuca ramki niezaszyfrowane lub nieuwierzytelnione.

<a id="diagnostyka-i-przykład"></a>

## Diagnostyka i przykładowa aplikacja

`hal_lora_link_get_diagnostics()` zwraca łączną liczbę wiadomości, ramek,
potwierdzeń, retransmisji i duplikatów. Zawiera też liczniki błędów formatu,
uwierzytelniania i integralności, porzuconych prób składania wiadomości i
przekroczeń ich limitu czasu, przepełnień kolejki, przekroczeń limitu czasu
wysyłania oraz anulowań. Oprócz tego podaje
ostatnio zaobserwowane adresy i parametry RF.

`examples/27_lora_point_to_point` udostępnia warianty `link` i
`link-responder`. Wymieniają one przez `hal_lora_commands` powiązane
identyfikatorem żądanie komendy i odpowiedź, obie o rozmiarze 500 bajtów.
Wymusza to po trzy fragmenty w obu
kierunkach na tych samych stanowiskach SX1262 co przykład niskopoziomowego API
radia.

Przykład celowo nie używa szyfrowania. Produkt powinien włączać AEAD dopiero po
opracowaniu bezpiecznego sposobu dostarczania kluczy i strategii unikalnych sesji.
