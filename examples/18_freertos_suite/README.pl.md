# 18 - Zestaw FreeRTOS

Ten skonsolidowany przykład ma dwa profile dispatchera:

- bazowy profil `app.c` jest małym międzytargetowym smoke testem FreeRTOS.
  Sprawdza mutexy HAL i native FreeRTOS, dwa taski aplikacji, dwa dodatkowe
  workery, opóźnienia, przetwarzanie idle, GPIO i współdzielony stan;
- wariant `network` buduje `network_app.cpp` na targetach RP z WiFi oraz
  STM32G474 z PIM730. Uruchamia jedną skoordynowaną usługę HTTP/WebSocket/
  plików/poleceń, konsolę sieciową, worker BSD TCP/UDP oraz worker klienta
  HTTP/HTTPS, zachowując dwie ścieżki testowe tasków aplikacji. Profil buduje
  też backend powiadomień Telegram jako test integracji, bez danych dostępowych
  i bez wysyłania próby w runtime.

Wariant sieciowy tworzy każdą usługę singleton tylko raz. Serwer HTTP używa
siedmiu z domyślnych ośmiu tras: `/`, `/api/status`, endpointu poleceń HTTP i
czterech tras plików w RAM. Wiadomości WebSocket i linie konsoli współdzielą
handlery poleceń `status` i `echo`.

## Konfiguracja sieci

Dla rzeczywistej sieci nadpisz definicje:

- `NETWORK_SUITE_WIFI_SSID` i `NETWORK_SUITE_WIFI_PASSWORD`;
- `NETWORK_SUITE_REMOTE_HOST`, `NETWORK_SUITE_BSD_TCP_PORT` i
  `NETWORK_SUITE_BSD_UDP_PORT` dla okresowych prób klienta BSD;
- `NETWORK_SUITE_HTTP_HOST` dla workera klienta HTTP/HTTPS;
- `NETWORK_SUITE_CONSOLE_PASSWORD` dla konsoli na porcie 2323.

Firmware domyślnie udostępnia HTTP na porcie 80, WebSocket pod
`ws://<ip>:81/ws`, serwer echo BSD TCP na porcie 8080 i serwer echo BSD UDP na
porcie 9000. Dispatcher ustawia ograniczone pule usług: `4` listenery TCP, `6`
socketów TCP, backlog listenera `2`, jeden handle TLS i po jednym kliencie dla
HTTP/WebSocket/konsoli. Profil sieciowy rezerwuje 1536 słów stosu FreeRTOS dla
taska serwera/aplikacji, 384 dla drugiego taska aplikacji, 768 dla workera BSD i
1536 dla workera HTTP/HTTPS. Bazowy profil pozostaje dostępny na RP2350 RISC-V;
profil sieciowy jest ograniczony do boardów RP2040/RP2350 Arm z WiFi oraz
STM32G474 z PIM730.

Zweryfikowanego HTTPS nie można bezpiecznie pokazać bez trust anchor wybranego
przez aplikację. Domyślnie worker wykonuje HTTP i wypisuje jawną diagnostykę
konfiguracji HTTPS. Aby wykonać również HTTPS, zdefiniuj
`HTTP_EXAMPLE_CA_AVAILABLE` i dodaj obok źródła `ca_certificate.h`:

```c
const unsigned char http_example_ca_der[] = { /* DER CA certificate */ };
const unsigned int http_example_ca_der_len = sizeof(http_example_ca_der);
```

Worker czeka na synchronizację NTP, konwertuje certyfikat DER do trust anchor
HAL, a następnie wykonuje zweryfikowane żądanie TLS. TLS i backend Telegram są
budowane w zwykłym wariancie sieciowym nawet bez certyfikatu, więc gate nadal
sprawdza całą powierzchnię integracji. RAM STM32G474 jest celowo wykorzystany
ciasno; przed wdrożeniem sprawdź konfigurację HTTPS na docelowym boardzie i
przejrzyj mapę linkera.
