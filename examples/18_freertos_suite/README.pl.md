# 18 - Zestaw FreeRTOS

Ten zbiorczy przykład ma dwa profile sterujące kompilacją:

- bazowy profil `app.c` jest podstawowym testem FreeRTOS na różnych targetach.
  Sprawdza muteksy HAL i FreeRTOS, dwa zadania aplikacji, dwa dodatkowe zadania
  robocze, opóźnienia, przetwarzanie w stanie bezczynności, GPIO i stan
  współdzielony;
- wariant `network` buduje `network_app.cpp` na targetach RP z WiFi oraz
  STM32G474 z PIM730. Uruchamia jedną skoordynowaną usługę HTTP/WebSocket/
  plików i poleceń, konsolę sieciową, zadanie BSD TCP/UDP oraz zadanie klienta
  HTTP/HTTPS, zachowując dwie ścieżki testowe zadań aplikacji. Profil dołącza
  też obsługę powiadomień Telegram jako test integracji, ale nie zawiera danych
  dostępowych i nie wysyła wiadomości w czasie działania.

Wariant sieciowy tworzy tylko po jednej instancji każdej usługi. Serwer HTTP
używa siedmiu z domyślnych ośmiu tras: `/`, `/api/status`, endpointu poleceń
HTTP i czterech tras plików w RAM. Wiadomości WebSocket i linie konsoli
korzystają z tych samych procedur obsługi poleceń `status` i `echo`.

## Konfiguracja sieci

Dla rzeczywistej sieci nadpisz definicje:

- `NETWORK_SUITE_WIFI_SSID` i `NETWORK_SUITE_WIFI_PASSWORD`;
- `NETWORK_SUITE_REMOTE_HOST`, `NETWORK_SUITE_BSD_TCP_PORT` i
  `NETWORK_SUITE_BSD_UDP_PORT` dla okresowych prób klienta BSD;
- `NETWORK_SUITE_HTTP_HOST` dla zadania klienta HTTP/HTTPS;
- `NETWORK_SUITE_CONSOLE_PASSWORD` dla konsoli na porcie 2323.

Firmware domyślnie udostępnia HTTP na porcie 80, WebSocket pod
`ws://<ip>:81/ws`, serwer echo BSD TCP na porcie 8080 i serwer echo BSD UDP na
porcie 9000. Konfiguracja ogranicza pule usług do `4` gniazd nasłuchujących TCP,
`6` gniazd TCP, kolejki oczekujących połączeń o długości `2`, jednego uchwytu TLS
i po jednym kliencie dla każdej z usług HTTP, WebSocket oraz konsoli. Profil
sieciowy rezerwuje 1536 słów stosu FreeRTOS dla zadania serwera/aplikacji, 384
dla drugiego zadania aplikacji, 768 dla zadania BSD i 1536 dla zadania HTTP/HTTPS. Bazowy
profil pozostaje dostępny na RP2350 RISC-V; profil sieciowy jest ograniczony do
płytek RP2040/RP2350 Arm z WiFi oraz STM32G474 z PIM730.

Nie da się bezpiecznie zademonstrować zweryfikowanego HTTPS bez kotwicy zaufania
wybranej przez aplikację. Domyślnie zadanie robocze wykonuje żądanie HTTP i
wypisuje jednoznaczną informację diagnostyczną o konfiguracji HTTPS. Aby
wykonać również HTTPS, zdefiniuj
`HTTP_EXAMPLE_CA_AVAILABLE` i dodaj obok źródła `ca_certificate.h`:

```c
const unsigned char http_example_ca_der[] = { /* DER CA certificate */ };
const unsigned int http_example_ca_der_len = sizeof(http_example_ca_der);
```

Zadanie robocze czeka na synchronizację NTP, przekształca certyfikat DER w
kotwicę zaufania HAL, a następnie wykonuje zweryfikowane żądanie TLS. TLS i
obsługa Telegram są kompilowane w zwykłym wariancie sieciowym nawet bez
certyfikatu, więc bramka nadal sprawdza całość integracji. Podczas działania na
STM32G474 pozostaje celowo niewielki zapas pamięci RAM; przed wdrożeniem sprawdź
HTTPS na docelowej płytce i przejrzyj mapę linkera.
