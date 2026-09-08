<a id="18---zestaw-freertos"></a>

# 18 - Zadania FreeRTOS i usługi sieciowe

Przykład podstawowy pokazuje pracę zadań FreeRTOS i synchronizację dostępu
do wspólnych danych. Wariant `network` dodaje usługi sieciowe, aby można
było sprawdzić ich współpracę w jednym programie.

`app.c` uruchamia dwa zadania aplikacji i dwa dodatkowe zadania robocze.
Używa muteksów HAL oraz FreeRTOS, opóźnień, obsługi bezczynności i GPIO.
Jest to podstawowy test działania, dostępny także dla RP2350 RISC-V.

Wariant `network` kompiluje `network_app.c` dla RP2040/RP2350 ARM z WiFi
oraz STM32G474 z PIM730. Udostępnia serwer HTTP, WebSocket, pliki w RAM,
polecenia i konsolę sieciową. Osobne zadania obsługują gniazda BSD TCP/UDP
oraz klienta HTTP/HTTPS. Nadal działają dwa zadania aplikacji. W programie
uwzględniono też cJSON oraz obsługę Telegram, ale **kompilacja modułu Telegram
nie oznacza wysłania wiadomości**: przykład nie ma żadnych danych dostępowych
ani próby wysyłki.

Każda usługa ma jedną instancję. Serwer HTTP używa siedmiu z domyślnych ośmiu
tras: `/`, `/api/status`, adresu obsługi poleceń HTTP i czterech tras plików
w RAM. WebSocket oraz konsola korzystają ze wspólnych procedur obsługi
poleceń `status` i `echo`.

## Konfiguracja sieci

Przed uruchomieniem ustaw definicje kompilacji:

| Ustawienie | Przeznaczenie |
|---|---|
| `NETWORK_SUITE_WIFI_SSID`, `NETWORK_SUITE_WIFI_PASSWORD` | Dane sieci WiFi. |
| `NETWORK_SUITE_REMOTE_HOST`, `NETWORK_SUITE_BSD_TCP_PORT`, `NETWORK_SUITE_BSD_UDP_PORT` | Adres i porty serwerów używanych w okresowych próbach klienta BSD. |
| `NETWORK_SUITE_HTTP_HOST` | Serwer dla klienta HTTP/HTTPS. |
| `NETWORK_SUITE_CONSOLE_PASSWORD` | Hasło konsoli na porcie 2323. |

Domyślne adresy usług to HTTP na porcie 80, WebSocket pod
`ws://<ip>:81/ws`, echo TCP na porcie 8080 i echo UDP na porcie 9000.
Nie myl serwera HTTP z klientem HTTPS - przykład nie konfiguruje tutaj
serwera HTTPS.

Konfiguracja ogranicza zasoby do 4 gniazd nasłuchujących TCP, 6 gniazd TCP,
2 oczekujących połączeń w kolejce, jednego uchwytu TLS i po jednym kliencie
HTTP, WebSocket oraz konsoli. Stosy FreeRTOS mają 1536 słów dla zadania
serwera/aplikacji, 384 dla drugiego zadania aplikacji, 768 dla zadania BSD
oraz 1536 dla klienta HTTP/HTTPS. Są to słowa stosu, nie bajty.

## Włączenie klienta HTTPS

Domyślnie klient wykonuje żądanie HTTP, a dla HTTPS wypisuje informację
o brakującej konfiguracji certyfikatu. Nie pomija weryfikacji po to, aby
wykonać żądanie.

Aby uruchomić HTTPS, zdefiniuj `HTTP_EXAMPLE_CA_AVAILABLE` i dodaj
`ca_certificate.h` obok źródła aplikacji. Umieść w nim zaufany certyfikat CA
w formacie DER, używany do weryfikacji serwera:

```c
const unsigned char http_example_ca_der[] = { /* DER CA certificate */ };
const unsigned int http_example_ca_der_len = sizeof(http_example_ca_der);
```

Przed żądaniem HTTPS zadanie czeka na synchronizację NTP i przekształca
certyfikat do formatu wymaganego przez HAL. TLS i obsługa Telegram są
kompilowane także bez tego pliku, co pozwala sprawdzić ich integrację podczas
kompilacji, ale nie dowodzi poprawnego działania połączenia na urządzeniu.

STM32G474 ma w tej konfiguracji niewielki zapas RAM. Przed wdrożeniem sprawdź
wariant z HTTPS na docelowej płytce i przejrzyj mapę linkera.

## Przygotowanie certyfikatu CA

Plik `ca_certificate.h` powinien zawierać zaufany certyfikat CA, używany do
uwierzytelnienia serwera wskazanego przez `NETWORK_SUITE_HTTP_HOST`.

Do szybkiego testu można pobrać certyfikat serwera w formacie PEM z którym się
chcesz połączyć. Do tego celu można użyć poniższego polecenia:

```bash
HTTPS_HOST=example.com

openssl s_client \
  -showcerts \
  -connect "${HTTPS_HOST}:443" \
  -servername "${HTTPS_HOST}" </dev/null 2>/dev/null |
awk '
  /BEGIN CERTIFICATE/ { count++ }
  count == 2 { print }
  /END CERTIFICATE/ && count == 2 { exit }
' > /tmp/jh-https-ca.pem
```

Sprawdź, czy wybrany certyfikat jest certyfikatem CA. Jeśli polecenie nie
wypisze `CA:TRUE`, pobierz właściwy certyfikat bezpośrednio od wystawcy:

```bash
openssl x509 -in /tmp/jh-https-ca.pem -noout -subject -issuer
openssl x509 -in /tmp/jh-https-ca.pem -noout -text | grep "CA:TRUE"
```

Przekształć certyfikat do formatu DER i wygeneruj nagłówek używany przez
przykład. Polecenia uruchom w głównym katalogu repozytorium:

```bash
openssl x509 \
  -in /tmp/jh-https-ca.pem \
  -outform DER \
  -out /tmp/jh-https-ca.der

xxd -i -n http_example_ca_der /tmp/jh-https-ca.der |
sed \
  -e 's/^unsigned char /const unsigned char /' \
  -e 's/^unsigned int /const unsigned int /' \
  > examples/18_freertos_suite/ca_certificate.h
```

Następnie dodaj poniższe definicje do `hal_project_config.h`:

```c
#define HTTP_EXAMPLE_CA_AVAILABLE 1
#define NETWORK_SUITE_HTTP_HOST "example.com"
```

Nazwa hosta musi odpowiadać nazwie DNS albo adresowi IP z pola Subject
Alternative Name certyfikatu serwera. Jeśli kontrolujesz serwer HTTPS, możesz
wygenerować własny CA i podpisać nim certyfikat serwera. Sprawdzona sekwencja
poleceń OpenSSL znajduje się w
[`tests/run_bearssl_native_integration.sh`](../../tests/run_bearssl_native_integration.sh).
