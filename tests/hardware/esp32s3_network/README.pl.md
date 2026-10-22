# Sprzętowy test sieci ESP32-S3

`tests/hardware/esp32s3_network` uruchamia natywny backend sieci ESP-IDF
w profilu `waveshare-esp32-s3-zero` z fixture'em na komputerze z Linuksem
w tej samej sieci LAN. Płytka potrzebuje tylko natywnego kabla USB i sieci
WiFi. Komputer potrzebuje Pythona z `pyserial` (oraz `cryptography` dla
zestawu SERVICES) i musi przyjmować połączenia przychodzące na portach
fixture'u.

Domyślny build uruchamia zestaw podstawowy, a wariant `SERVICES` zestaw
usług.

JaszczurHAL wspiera sieć IPv4. Fixture wykonuje też próby IPv6 link-local
natywnego stosu ESP-IDF jako dodatkową diagnostykę backendu. Nie oznaczają
one wsparcia IPv6 przez JaszczurHAL. Ta sama granica wsparcia dotyczy IPv6
dostępnego przez stos systemu hosta, w tym Windows.

## Zestaw podstawowy

Firmware sprawdza:

- tryb stacji, skanowanie, które znajduje skonfigurowaną sieć, połączenie
  z błędnym hasłem (`HAL_EAUTH`) i z nieistniejącą siecią (`HAL_ENOENT`);
- połączenie z DHCP, rozwiązanie nazwy `example.com` i ping do komputera;
- echo UDP z portem nadawcy oraz odbiór, który czeka do końca timeoutu;
- echo TCP, odrzucone połączenie, koniec strumienia po zamknięciu przez drugą
  stronę i backpressure: wysyłanie do komputera, który nie czyta, wraca od razu
  z częściową liczbą bajtów, aż gniazdo zgłosi `HAL_EAGAIN`;
- 100 wymian echa TCP w `app_task0()`, gdy `app_task1()` wykonuje 100 wymian
  echa UDP na własnym gnieździe;
- echo TCP i UDP przez natywne API BSD z ESP-IDF;
- rozłączenie WiFi, gdy `app_task1()` czeka w odbiorze UDP, a potem TCP:
  odbiór kończy się po milisekundach kodem `HAL_ECANCELED`;
- unieważnienie otwartego gniazda TCP przez rozłączenie, ponowne połączenie
  i echo TCP po nim;
- wyłączenie WiFi przy otwartym gnieździe UDP, ponowne włączenie, nowe
  połączenie oraz DNS i UDP po nim;
- serwery na adresie płytki, następnie restart wszystkich i zatrzymanie, gdy
  klienci komputera są nadal połączeni.

Komputer sprawdza serwery: HTTP GET, HEAD, POST, 404, błąd handlera (500)
i błędne żądanie (400); pliki z ETagiem i `304`, upload odrzucony bez
nagłówka z tokenem (403), przyjęty upload odczytany z powrotem i ścieżkę
z `..`; polecenia sieciowe przez HTTP (poprawne, nieznane, błędny JSON) i przez
WebSocket; echo WebSocket, ramkę bez maski i za długą (obie kończą sesję)
oraz trzecią sesję odrzuconą kodem 503; konsolę sieciową z błędnym hasłem,
poleceniem i trzecim klientem odrzuconym jako zajęty; echo UDP i TCP przez
adres IPv6 link-local płytki; klienta, który nie kończy żądania (408, a inny
klient jest w tym czasie obsłużony); bezczynne połączenia, które zapełniają
pulę klientów, aż ich timeout ją zwolni. Po restarcie komputer znowu pobiera
stronę i wymienia wiadomość WebSocket; po zatrzymaniu każde trzymane
połączenie się kończy, a każdy port odrzuca nowe.

## Zestaw usług (wariant `SERVICES`)

Firmware sprawdza, w tej kolejności:

- połączenie TLS bez zegara (`HAL_ECONFIG`) i handshake WireGuard bez niego
  (brak peera);
- NTP do serwera, który nie odpowiada (żądanie kończy się błędem, zegar
  zostaje nieustawiony) i NTP, w którym nazwa serwera podstawowego się nie
  rozwiązuje, obsłużone przez zapasowy; czas libc podąża potem za zegarem HAL;
- klienta TLS: poprawną sesję z wymianą HTTP, dziewięć kolejnych sesji, które
  oddają pamięć sterty, błędną nazwę hosta, obce CA, certyfikat wygasły
  i jeszcze nieważny (`HAL_EAUTH`), błąd entropii (`HAL_EHW`), anulowanie
  (`HAL_ECANCELED`), serwer zrywający handshake (`HAL_EIO`) i serwer, który
  nie odpowiada (`HAL_ETIMEOUT`);
- klientów HTTPS i zwykłego HTTP;
- MQTT, dwa razy MQTTS, MQTTS do błędnej nazwy hosta (odrzucone) i MQTT po
  wyłączeniu TLS;
- tunel WireGuard split: handshake, UDP, TCP i MQTT przez tunel, LAN poza
  nim, zamknięcie tunelu przy otwartym przez niego połączeniu TCP; trzy cykle
  begin/end; pełny tunel z UDP i TCP; endpoint podany jako nazwa hosta; utratę
  WiFi przy aktywnym tunelu, nowe połączenie i nowy tunel;
- niewykorzystany stos wątku lwIP po testach tunelu.

Komputer udostępnia NTP na UDP/10123, serwery TLS i zwykłego HTTP, serwer TLS
zrywający handshake i taki, który nie odpowiada, brokery MQTT i MQTTS, a na
adresie WireGuard echo UDP, echo TCP i MQTT.

## Ustawienia sieci

Utwórz `net_test_config.h` obok `app.cpp`. Git ignoruje ten plik, więc dane
sieci zostają na komputerze:

```c
#pragma once
#define JH_NET_TEST_SSID "nazwa-sieci"
#define JH_NET_TEST_PASSWORD "haslo-sieci"
#define JH_NET_TEST_HOST "192.168.1.10"  /* komputer z verify_network.py */
/* Tylko zestaw SERVICES: klucz WireGuard płytki i klucz publiczny komputera. */
#define JH_NET_TEST_WG_PRIVATE_KEY "klucz-prywatny-plytki"
#define JH_NET_TEST_WG_PEER_PUBLIC_KEY "klucz-publiczny-komputera"
```

Domyślnie płytka ma w tunelu adres `10.77.0.2`, komputer `10.77.0.1`,
a endpoint komputera to UDP/51821; `JH_NET_TEST_WG_LOCAL_IP`,
`JH_NET_TEST_WG_PEER_IP` i `JH_NET_TEST_WG_PORT` je zmieniają.

Dla zestawu SERVICES uruchom na komputerze interfejs WireGuard z adresem
`10.77.0.1/24`, nasłuchujący na UDP/51821, z kluczem publicznym płytki jako
peerem i `AllowedIPs = 10.77.0.2/32`. Utworzenie interfejsu wymaga roota;
weryfikator działa potem jako zwykły użytkownik. TLS korzysta z testowego CA:

```bash
python3 tests/hardware/esp32s3_network/verify_network.py prepare-tls \
  --host 192.168.1.10
```

Polecenie zapisuje CA, certyfikat serwera dla publicznej nazwy wildcard-DNS
`192-168-1-10.sslip.io` (płytka rozwiązuje ją przez swój DNS) i obce CA do
ignorowanego katalogu `.tls/`, a nagłówek firmware do `net_test_tls.h`. Zbuduj
firmware po nim; certyfikaty są ważne 30 dni.

Firmware ogłasza adres komputera i swoje porty, a weryfikator obsługuje
dokładnie je. Numery portów istnieją tylko w źródłach zestawów.

## Build, wgranie i weryfikacja

```bash
python3 scripts/build_esp_idf.py build \
  --project tests/hardware/esp32s3_network \
  --assembly esp32s3-zero \
  --name jh_esp32_network_hardware

python3 scripts/build_esp_idf.py flash \
  --project tests/hardware/esp32s3_network \
  --assembly esp32s3-zero \
  --name jh_esp32_network_hardware \
  --port /dev/serial/by-id/usb-Espressif_USB_JTAG_serial_debug_unit_SERIAL-if00

python3 tests/hardware/esp32s3_network/verify_network.py run \
  --port /dev/serial/by-id/usb-Espressif_USB_JTAG_serial_debug_unit_SERIAL-if00
```

Dla zestawu usług dodaj `--variant SERVICES` do poleceń build i flash.
Weryfikator uruchamia swoje usługi po linii `ready` i wysyła `GO`. W zestawie
podstawowym sprawdza serwery, gdy firmware zgłosi `check=serve`, a potem
wysyła `RESTART`, `STOP` i `DONE`. Kończy się sukcesem tylko wtedy, gdy każdy
test przeszedł, żadnego nie brakuje, płytka się nie zresetowała i każde
sprawdzenie z komputera się udało; na końcu wypisuje podsumowanie JSON.
`--raw-log PLIK` zapisuje też każdą linię z portu szeregowego, łącznie
z wyjściem panic.

`--soak-seconds N` dodaje po zestawie test wytrzymałościowy: zestaw
podstawowy wykonuje echo TCP i DNS w `app_task0()` oraz echo UDP
w `app_task1()`, zestaw usług echo UDP i TCP przez tunel split (WireGuard
odnawia klucze co dwie minuty). Płytka raportuje postęp co 30 sekund
i przechodzi przy co najwyżej 0,5 % nieudanych wymian oraz ubytku wolnej
sterty nie większym niż 8 KiB względem początku testu wytrzymałościowego.

## Pokrycie sprzętowe

Oba zestawy wraz z trybami wytrzymałościowymi są zweryfikowane sprzętowo
na Waveshare ESP32-S3-Zero. Testów sprzętowych nadal wymagają utrata adresu
z DHCP bez rozłączenia i błąd entropii w WireGuard.
