# Łączność sieciowa

*Dostępne również [po angielsku](../en/15_connectivity.md).*

> **Część [Dokumentacji API JaszczurHAL](../../pl/JaszczurHAL_API.md)**

Obejmuje: `hal_wifi`, `hal_udp`, `hal_tcp`, `hal_http_server`,
`hal_http_files`, `hal_websocket`, `hal_net_console`, `hal_net_commands`,
`hal_notify`, `hal_wireguard`, `hal_mqtt`, `hal_ota`, `hal_time` oraz
opcjonalny adapter zgodności `HAL_ENABLE_BSD_SOCKETS`.
Wspólne typy sieciowe znajdują się w `hal_net.h`.

## Sieciowe API zwracające status

Nowy kod może korzystać z dodatkowych funkcji `_ex`, które zwracają
`hal_status_t` dla WiFi, resolvera, TCP, UDP, MQTT i WireGuard. Dotychczasowe
API pozostaje dostępne bez zmian. Funkcje, które wcześniej zwracały liczbę,
zaakceptowane gniazdo lub stan peera, przekazują teraz pierwotny wynik przez
jawny parametr wyjściowy. Zmiana sposobu zgłaszania statusu nie powoduje więc
utraty żadnej informacji.

Statusy WiFi, resolvera, TCP i UDP są obsługiwane bezpośrednio przez
implementację testową oraz backendy rodziny RP. Dotychczasowe funkcje
zwracające `bool`, liczbę lub uchwyt są cienką warstwą zgodności wywołującą
odpowiednie warianty statusowe; same nie wykonują operacji wejścia/wyjścia.

Przykładowe funkcje to `hal_wifi_begin_station_ex()`,
`hal_wifi_ping_status_ex()`, `hal_net_resolve_ipv4_ex()`,
`hal_tcp_socket_{connect,send,recv}_ex()`,
`hal_tcp_listener_{bind,listen,accept}_ex()`,
`hal_udp_socket_{bind,sendto,recvfrom}_ex()`, funkcje pomocnicze UDP `_ex`
zgodne ze starszym API pakietowym, `hal_mqtt_{connect,publish,subscribe}_ex()`,
`hal_notify_{open,send,poll,close}()` oraz
`hal_wireguard_{begin,peer_up,kick_handshake}_ex()`.

Kompletne rozszerzenie API o warianty statusowe wygląda następująco:

```c
// WiFi i resolver
hal_status_t hal_wifi_set_mode_ex(hal_wifi_mode_t mode);
hal_status_t hal_wifi_disconnect_ex(bool erase_credentials);
hal_status_t hal_wifi_set_hostname_ex(const char *hostname);
hal_status_t hal_wifi_begin_station_ex(const char *ssid, const char *password,
                                       bool non_blocking);
hal_status_t hal_wifi_set_timeout_ms_ex(uint32_t timeout_ms);
hal_status_t hal_wifi_get_local_ip_ex(char *out, size_t out_size);
hal_status_t hal_wifi_get_dns_ip_ex(char *out, size_t out_size);
hal_status_t hal_wifi_get_mac_ex(char *out, size_t out_size);
hal_status_t hal_wifi_ping_status_ex(const char *host_or_ip,
                                     uint32_t timeout_ms, int *out_result);
hal_status_t hal_wifi_scan_networks_ex(int *out_count);
hal_status_t hal_wifi_get_scan_result_ex(size_t index,
                                         hal_wifi_scan_result_t *out);
hal_status_t hal_net_resolve_ipv4_ex(
    const char *host_or_ip, uint8_t out_addr[HAL_NET_IPV4_ADDR_LEN]);

// TCP oparte na uchwytach
hal_status_t hal_tcp_socket_open_ex(hal_tcp_socket_t *out_socket);
hal_status_t hal_tcp_socket_connect_ex(hal_tcp_socket_t socket,
                                       const hal_net_endpoint_t *remote,
                                       uint32_t timeout_ms);
hal_status_t hal_tcp_socket_send_ex(hal_tcp_socket_t socket, const void *data,
                                    size_t len, size_t *out_sent);
hal_status_t hal_tcp_socket_recv_ex(hal_tcp_socket_t socket, void *buffer,
                                    size_t max_len, uint32_t timeout_ms,
                                    size_t *out_received);
hal_status_t hal_tcp_listener_bind_ex(hal_tcp_listener_t listener,
                                      const hal_net_endpoint_t *local);
hal_status_t hal_tcp_listener_listen_ex(hal_tcp_listener_t listener,
                                        uint8_t backlog);
hal_status_t hal_tcp_listener_accept_ex(hal_tcp_listener_t listener,
                                        hal_net_endpoint_t *remote,
                                        uint32_t timeout_ms,
                                        hal_tcp_socket_t *out_socket);
hal_status_t hal_tcp_listener_open_ex(hal_tcp_listener_t *out_listener);

// UDP oparte na uchwytach i zgodności
hal_status_t hal_udp_socket_open_ex(hal_udp_socket_t *out_socket);
hal_status_t hal_udp_socket_bind_ex(hal_udp_socket_t socket,
                                    const hal_net_endpoint_t *local);
hal_status_t hal_udp_socket_sendto_ex(hal_udp_socket_t socket, const void *data,
                                      size_t len,
                                      const hal_net_endpoint_t *remote,
                                      size_t *out_sent);
hal_status_t hal_udp_socket_recvfrom_ex(hal_udp_socket_t socket, void *buffer,
                                        size_t max_len,
                                        hal_net_endpoint_t *remote,
                                        uint32_t timeout_ms,
                                        size_t *out_received);
hal_status_t hal_udp_begin_ex(uint16_t local_port);
hal_status_t hal_udp_parse_packet_ex(int *out_size);
hal_status_t hal_udp_read_ex(uint8_t *buffer, uint16_t max_len,
                             uint16_t *out_read);
hal_status_t hal_udp_remote_ip_ex(char *out, size_t out_size);
hal_status_t hal_udp_remote_port_ex(uint16_t *out_port);
hal_status_t hal_udp_begin_packet_ex(const char *host_or_ip,
                                     uint16_t remote_port);
hal_status_t hal_udp_begin_packet_remote_ex(void);
hal_status_t hal_udp_write_ex(const uint8_t *data, uint16_t len,
                              uint16_t *out_written);
hal_status_t hal_udp_write_str_ex(const char *text, uint16_t *out_written);
hal_status_t hal_udp_end_packet_ex(void);

// MQTT
hal_status_t hal_mqtt_set_server_ex(const char *host, uint16_t port);
hal_status_t hal_mqtt_set_callback_ex(hal_mqtt_message_callback_t callback,
                                      void *user);
hal_status_t hal_mqtt_set_keepalive_ex(uint16_t keepalive_s);
hal_status_t hal_mqtt_set_socket_timeout_ex(uint16_t timeout_s);
hal_status_t hal_mqtt_set_buffer_size_ex(uint16_t size);
hal_status_t hal_mqtt_connect_ex(const char *client_id);
hal_status_t hal_mqtt_connect_auth_ex(const char *client_id, const char *user,
                                      const char *pass);
hal_status_t hal_mqtt_loop_ex(void);
hal_status_t hal_mqtt_publish_ex(const char *topic, const uint8_t *payload,
                                 uint16_t payload_len, bool retained);
hal_status_t hal_mqtt_publish_str_ex(const char *topic, const char *payload,
                                     bool retained);
hal_status_t hal_mqtt_subscribe_ex(const char *topic, uint8_t qos);
hal_status_t hal_mqtt_unsubscribe_ex(const char *topic);

// WireGuard
hal_status_t hal_wireguard_begin_ex(
    const uint8_t local_ip[HAL_WIREGUARD_IPV4_OCTETS],
    const char *private_key, const char *remote_peer_address,
    const char *remote_peer_public_key, uint16_t remote_peer_port);
hal_status_t hal_wireguard_begin_advanced_ex(
    const uint8_t local_ip[HAL_WIREGUARD_IPV4_OCTETS], const char *private_key,
    const char *remote_peer_address, const char *remote_peer_public_key,
    uint16_t remote_peer_port,
    const uint8_t allowed_ip[HAL_WIREGUARD_IPV4_OCTETS],
    const uint8_t allowed_mask[HAL_WIREGUARD_IPV4_OCTETS]);
hal_status_t hal_wireguard_peer_up_ex(char *endpoint_ip_out,
                                      size_t endpoint_ip_out_size,
                                      uint16_t *endpoint_port_out,
                                      bool *out_peer_up);
hal_status_t hal_wireguard_kick_handshake_ex(
    const uint8_t probe_ip[HAL_WIREGUARD_IPV4_OCTETS], uint16_t probe_port,
    uint32_t min_interval_ms);
```

Nieprawidłowe argumenty zwykle powodują zwrot `HAL_EINVAL`. Brak wyniku
resolvera lub wyszukiwania jest zgłaszany jako `HAL_ENOENT`, brak klienta
oczekującego na `accept()` jako `HAL_EAGAIN`, wyczerpanie puli jako
`HAL_ENOMEM`, a próba wykonania operacji w niewłaściwym stanie gniazda jako
`HAL_ESTATE`. Publiczne funkcje statusowe backendów RP CYW43 konsekwentnie
zgłaszają:

- `HAL_EUNSUPPORTED`, gdy wybrany profil płytki nie deklaruje całego
  wymaganego sprzętu radiowego;
- `HAL_EUNINIT`, gdy ten sprzęt jest zadeklarowany, ale nie zainicjalizował
  się pomyślnie;
- `HAL_EHW`, gdy podczas sondowania lub inicjalizacji sprzęt został uznany za
  uszkodzony.

Sama inicjalizacja zwraca pierwotny status drivera. Późniejsze operacje
sieciowe zachowują i zwracają stan `HAL_EHW`. Zestaw Pico+PIM730 wymaga zarówno
obsługi CYW43, jak i zewnętrznego modułu radiowego. Jeśli wstępna kontrola
konfiguracji zakończy się niepowodzeniem, backend nie zostanie uruchomiony,
a piny radia pozostaną nietknięte.
Analiza numerycznych adresów IPv4 i adresów WireGuard oraz funkcje MQTT
zmieniające wyłącznie konfigurację pozostają dostępne bez połączenia z siecią. Błędy
transportu natywnego, które nie zmieniają stanu sprzętowego płytki, są
zgłaszane jako `HAL_EIO`. Pełne sygnatury znajdziesz w publicznych nagłówkach modułów.

## Wspólne typy sieciowe

`hal_net.h` zawiera proste typy wartościowe C, wspólne dla opartych na
uchwytach API UDP i TCP oraz warstw zgodności BSD/POSIX. Struktura punktu
końcowego przechowuje adres IPv4 lub IPv6 wraz z oznaczeniem jego rodziny.
Obecne backendy CYW43 zgłaszają obsługę IPv4. ESP32-S3 zgłasza rodziny włączone w
swojej konfiguracji ESP-IDF lwIP; nieobsługiwane rodziny zwracają
`HAL_EUNSUPPORTED`.

```c
#include <hal/network/hal_net.h>

#define HAL_NET_IPV4_ADDR_LEN 4u
#define HAL_NET_IPV6_ADDR_LEN 16u
#define HAL_NET_MAX_ADDR_LEN HAL_NET_IPV6_ADDR_LEN
#define HAL_NET_TIMEOUT_FOREVER UINT32_MAX

typedef enum {
  HAL_NET_AF_UNSPEC = 0,
  HAL_NET_AF_INET = 2,
  HAL_NET_AF_INET6 = 10
} hal_net_family_t;

typedef struct {
  hal_net_family_t family;
  uint8_t addr[HAL_NET_MAX_ADDR_LEN];
  uint8_t addr_len;
  uint16_t port;
  uint32_t scope_id;
} hal_net_endpoint_t;

#define HAL_NET_CAP_IPV4      (1u << 0u)
#define HAL_NET_CAP_IPV6      (1u << 1u)
#define HAL_NET_CAP_DUAL_STACK (1u << 2u)

#ifdef HAL_ENABLE_WIFI
hal_status_t hal_net_get_capabilities_ex(
    hal_net_capabilities_t *out_capabilities);
hal_status_t hal_net_service(void);
hal_status_t hal_net_resolve_ex(const char *host_or_ip,
                                hal_net_family_t family_hint,
                                hal_net_endpoint_t *results,
                                size_t capacity,
                                size_t *out_count);
bool hal_net_resolve_ipv4(const char *host_or_ip,
                          uint8_t out_addr[HAL_NET_IPV4_ADDR_LEN]);
hal_status_t hal_net_resolve_ipv4_ex(
    const char *host_or_ip, uint8_t out_addr[HAL_NET_IPV4_ADDR_LEN]);
#endif
```

Bajty adresu są przechowywane w kolejności sieciowej (network byte order).
`addr_len` wynosi cztery dla IPv4 i szesnaście dla IPv6; `scope_id` określa
zakres interfejsu IPv6. Pole `port` jest w kolejności hosta (host byte
order); adaptery POSIX wykonują własną konwersję `htons()` / `ntohs()` na
granicy API.

**Uwagi dotyczące resolvera:**

- `hal_net_resolve_ex(...)` przyjmuje adresy numeryczne lub nazwy hostów,
  preferowaną rodzinę adresów oraz tablicę wyników o rozmiarze określonym przez
  wywołującego. `HAL_EOVERFLOW` podaje wymaganą liczbę elementów i nie zapisuje
  niekompletnego wyniku.
- `hal_net_resolve_ipv4(...)` przyjmuje dosłowny zapis IPv4 z kropkami lub
  nazwę hosta i zapisuje cztery oktety IPv4. Wywołujący przechowuje port
  transportowy osobno.
- Implementacja testowa rozpoznaje dosłowne adresy IPv4, `localhost` oraz wpisy
  testowe dodane przez `hal_mock_net_set_dns_entry(...)`.
- Backendy CYW43 rozpoznają dosłowne adresy numeryczne lokalnie, natomiast
  nazwy hostów rozwiązują przez własny resolver lwIP. Rozwiązywanie nazw
  wymaga zainicjalizowanego sprzętu; analiza adresu dosłownego go nie wymaga.
- ESP32-S3 rozwiązuje nazwy przez natywną ścieżkę `getaddrinfo()` ESP-IDF
  lwIP, gdy WiFi/`esp_netif` osiągnie stan gotowości.

**Pomocnicy mock resolvera:**
```c
void hal_mock_net_reset(void);
bool hal_mock_net_set_dns_entry(const char *host, const char *ip);
```

### Natywny backend ESP32-S3 i zakres objęty weryfikacją

Backend ESP32-S3 inicjalizuje NVS, `esp_netif`, domyślną pętlę zdarzeń ESP,
interfejs stacji oraz natywny driver WiFi używany przez istniejące publiczne
API HAL. Handlery zdarzeń odwzorowują w stanie HAL zdarzenia uruchomienia,
połączenia i rozłączenia stacji, przydzielenia adresu IPv4, skanowania,
uwierzytelniania, braku sieci, ponownego łączenia i zamknięcia. Uchwyty TCP i UDP są przydzielane
z pul o stałej pojemności, a liczniki generacji chronią przed użyciem
nieaktualnego uchwytu. Pod tą warstwą działają natywne gniazda lwIP oraz
`select()`. `HAL_ENABLE_BSD_SOCKETS` udostępnia natywne API BSD z ESP-IDF,
zamiast ponownie definiować wspólne symbole warstwy zgodności.

W ramach tego samego grafu zależności budowane są: wspólny klient TLS BearSSL,
klient HTTP/HTTPS, nieszyfrowane serwery HTTP i WebSocket, MQTT z opcjonalnym
TLS, obsługa NTP i czasu, OTA przyjmujące surowy obraz aplikacji ESP oraz
WireGuard korzystający z portu rozszerzenia lwIP dla tej platformy. Publiczne
API nie udostępnia serwera TLS, serwera HTTPS ani WSS, ani klienta WebSocket.
`tests/fixtures/esp32s3_phase3` sprawdza wyliczanie zestawu funkcji, dobór
plików źródłowych i zależności, build, linkowanie, partycje oraz artefakty.
Nie weryfikuje działania na sprzęcie, cyklu życia, rollbacku ani zachowania
w negatywnych testach bezpieczeństwa.

## `hal_wifi` - WiFi  *(opcjonalny - `HAL_ENABLE_WIFI`)*

Konfiguracje RP korzystające z WiFi wybierają profil obsługujący radio:
`picow`, `pico2w` lub `pico-rm2`. ESP32-S3 używa natywnego radia
zadeklarowanego przez profil płytki. `HAL_ENABLE_WIFI` włącza wspólne API,
a moduły zależne, takie jak MQTT i WireGuard, automatycznie włączają tę flagę. Moduły
sieciowe mogą też być budowane dla zwykłego profilu Pico; wywołania
publiczne zwracają wtedy `HAL_EUNSUPPORTED` bez dostępu do pinów CYW43. W
profilu z obsługą radia `hal_wifi_set_mode_ex(HAL_WIFI_MODE_STA)` i
`hal_wifi_begin_station_ex(...)` są jawnymi punktami wejścia inicjalizacji.
Zapytania o stan, skanowania i otwarcia transportu nie inicjalizują radia
niejawnie.

Profile CYW43 zachowują fabryczny adres MAC radia, gdy jest on obecny w
pamięci OTP modułu. Obejmuje to adresy przydzielone przez Raspberry Pi,
takie jak `28:CD:C1:xx:xx:xx`. Jeśli radio zgłasza, że jego MAC OTP jest
nieustawiony, JaszczurHAL stosuje fallback z Pico SDK: lokalnie
administrowany adres unicast wyliczony z sześciu najmniej znaczących bajtów
UID płytki. Adres ten jest stały dla danej płytki i nie korzysta ze wspólnego
prefiksu UID występującego na wielu płytkach RP.
Po inicjalizacji `hal_wifi_get_mac_ex()` oraz interfejs lwIP używają tego
samego adresu zapisanego w stanie kontrolera CYW43. Jeśli adres znajduje się
w OTP, aplikacja nie może zastępować go osobno wygenerowanym adresem opartym
na UID płytki.

Dla profili RP CYW43 wywołanie `hal_wifi_begin_station_ex(..., true)` rozpoczyna
łączenie i wraca, gdy tylko CYW43 przyjmie żądanie. Po powrocie z funkcji
wywołujący może zwolnić lub nadpisać bufory SSID i hasła. Postęp połączenia
należy obsługiwać przez regularne wywoływanie `hal_wifi_get_state_ex()` albo
`hal_wifi_is_connected()`. Każde takie wywołanie wykonuje kolejny krok
backendu i udostępnia jeden ze stanów: łączenie, brak sieci, uwierzytelnianie,
DHCP lub połączenie. Przekazanie `false` wybiera blokujący wariant z timeoutem,
który czeka na uzyskanie dzierżawy DHCP.

STM32G474 obsługuje zewnętrzny CYW43/PIM730 przez eksperymentalny profil
`nucleo-g474re-pim730`, jego jednoprzewodowy transport gSPI oraz ten sam
stos lwIP w wersji ustalonej w projekcie. Zwykły profil `nucleo-g474re` celowo nie ma możliwości
radiowych i odrzuca konfigurację sieciową CYW43. Łączenie stacji na STM32G474
jest blokujące i ograniczone timeoutem; żądanie wariantu nieblokującego zwraca
`HAL_EUNSUPPORTED`.

```c
#include <hal/network/hal_wifi.h>

typedef enum {
    HAL_WIFI_MODE_OFF    = 0,
    HAL_WIFI_MODE_STA    = 1,
    HAL_WIFI_MODE_AP     = 2,
    HAL_WIFI_MODE_AP_STA = 3,
} hal_wifi_mode_t;

typedef enum {
    HAL_WIFI_ENC_UNKNOWN = 0,
    HAL_WIFI_ENC_NONE,
    HAL_WIFI_ENC_WPA,
    HAL_WIFI_ENC_WPA2,
    HAL_WIFI_ENC_AUTO,
} hal_wifi_encryption_t;

typedef struct {
    char                  ssid[HAL_WIFI_SSID_MAX_LEN];
    uint8_t               bssid[HAL_WIFI_BSSID_LEN];
    hal_wifi_encryption_t encryption;
    int32_t               rssi;
    int32_t               channel;
} hal_wifi_scan_result_t;

bool    hal_wifi_set_mode(hal_wifi_mode_t mode);
bool    hal_wifi_disconnect(bool erase_credentials);
bool    hal_wifi_set_hostname(const char *hostname);
bool    hal_wifi_begin_station(const char *ssid, const char *password, bool non_blocking);
bool    hal_wifi_set_timeout_ms(uint32_t timeout_ms);
bool    hal_wifi_is_connected(void);
int     hal_wifi_status(void);
bool    hal_wifi_has_local_ip(void);
int32_t hal_wifi_rssi(void);                        // dBm
int     hal_wifi_get_strength(void);                // 0..5 kresek
bool    hal_wifi_get_local_ip(char *out, size_t out_size);
bool    hal_wifi_get_dns_ip(char *out, size_t out_size);
bool    hal_wifi_get_mac(char *out, size_t out_size);
hal_status_t hal_wifi_set_mode_ex(hal_wifi_mode_t mode);
hal_status_t hal_wifi_disconnect_ex(bool erase_credentials);
hal_status_t hal_wifi_set_hostname_ex(const char *hostname);
hal_status_t hal_wifi_begin_station_ex(const char *ssid, const char *password,
                                       bool non_blocking);
hal_status_t hal_wifi_set_timeout_ms_ex(uint32_t timeout_ms);
hal_status_t hal_wifi_get_local_ip_ex(char *out, size_t out_size);
hal_status_t hal_wifi_get_dns_ip_ex(char *out, size_t out_size);
hal_status_t hal_wifi_get_mac_ex(char *out, size_t out_size);
int     hal_wifi_ping(const char *host_or_ip);      // >=0 ok, <0 błąd (używa timeoutu ustawionego przez hal_wifi_set_timeout_ms)
int     hal_wifi_ping_ex(const char *host_or_ip, uint32_t timeout_ms); // >=0 ok, <0 błąd (timeout dla pojedynczego wywołania)
hal_status_t hal_wifi_ping_status_ex(const char *host_or_ip,
                                     uint32_t timeout_ms, int *out_result);
int     hal_wifi_scan_networks(void);               // >=0 liczba wyników, <0 błąd
bool    hal_wifi_get_scan_result(size_t index, hal_wifi_scan_result_t *out);
hal_status_t hal_wifi_scan_networks_ex(int *out_count);
hal_status_t hal_wifi_get_scan_result_ex(size_t index,
                                         hal_wifi_scan_result_t *out);
const char *hal_wifi_encryption_to_string(hal_wifi_encryption_t encryption);
```

- **impl/rp2040:** driver CYW43 opracowany w JaszczurHAL oraz stos lwIP nad
  PIO/gSPI.
- **impl/stm32g474:** ta sama implementacja CYW43/lwIP nad jednoprzewodowym
  transportem gSPI STM32G474.
- **impl/esp32:** natywna obsługa cyklu życia stacji ESP-IDF korzystająca z NVS,
  `esp_netif`, domyślnej pętli zdarzeń, `esp_wifi`, DHCP/DNS, skanowania, pingu
  i zdarzeń ponownego łączenia.
- **impl/.mock:** funkcje pomocnicze pozwalają ustawiać stan implementacji
  testowej.

**Thread safety:** Backendy sprzętowe RP, STM32G474 i ESP32-S3
serializują wywołania publicznego API HAL. Wewnętrzne mutexy chronią stan
backendu, postęp obsługi sieci oraz dostęp do stosu. Deterministyczna
implementacja testowa jest przeznaczona do testów jednowątkowych, a jej stan
można ustawiać za pomocą funkcji pomocniczych.

**Pomocnicy mock:**
```c
void        hal_mock_wifi_reset(void);
void        hal_mock_wifi_set_connected(bool connected);
void        hal_mock_wifi_set_status(int status);
void        hal_mock_wifi_set_rssi(int32_t rssi);
void        hal_mock_wifi_set_local_ip(const char *ip);
void        hal_mock_wifi_set_dns_ip(const char *ip);
void        hal_mock_wifi_set_mac(const char *mac);
void        hal_mock_wifi_set_ping_result(int result);
const char *hal_mock_wifi_get_hostname(void);
uint32_t    hal_mock_wifi_get_timeout_ms(void);
bool        hal_mock_wifi_set_scan_result(size_t index,
                                          const char *ssid,
                                          hal_wifi_encryption_t encryption,
                                          const uint8_t bssid[HAL_WIFI_BSSID_LEN],
                                          int32_t channel,
                                          int32_t rssi);
```

### Konfiguracja i cykl życia backendu CYW43

We wszystkich konfiguracjach sprzętowych CYW43 używany jest jeden backend
wspólnego API, jedna magistrala i przypisany do nich stos lwIP:

```c
#define HAL_NETWORK_BACKEND_CYW43
#define HAL_CYW43_STACK_LWIP
```

Profile płytek RP definiują `HAL_CYW43_BUS_PICO_PIO` oraz odpowiednie piny.
Pico W, Pico 2 W i Pico+PIM730 korzystają z tego samego mechanizmu obsługi
cyklu życia backendu. Transport PIO wylicza swój dzielnik zegara 16.8 na
podstawie bieżącego `clk_sys` i `HAL_CYW43_GSPI_TARGET_HZ` (domyślnie
31,25 MHz). Następnie wybiera odpowiedni program próbkowania dla wysokiej
lub niskiej prędkości, nie przekraczając częstotliwości docelowej. Próba
zmiany `clk_sys` przy aktywnym backendzie jest odrzucana. Najpierw wyłącz
sieć, potem zmień zegar i ponownie ją zainicjalizuj.

Projekty STM32G474 z zewnętrznym PIM730 wybierają profil płytki
`nucleo-g474re-pim730`. Wygenerowany profil dostarcza backend, magistralę,
stos oraz zakodowane piny; aplikacje nie mogą ręcznie powielać tych
definicji. Stałe okablowanie wygląda następująco:

| PIM730 | STM32G474 | Złącze Nucleo |
|---|---|---|
| `CS` | `PB12` | CN10 pin 16 |
| `DAT` | `PB15` | CN10 pin 26 |
| `WL_ON` | `PB14` | CN10 pin 28 |
| `CLK` | `PB13` | CN10 pin 30 |
| `GND` | GND | CN10 pin 20 |
| `3V3` | 3.3 V | CN7 pin 16 |

PIM730 to urządzenie 3,0-3,3 V; nigdy nie podłączaj go do 5 V. `DAT` to
połączona linia wejścia/wyjścia danych oraz wybudzania hosta (host-wake).
Używaj krótkiego, bezpośredniego okablowania. Obsługiwany profil zakłada, że
ścieżka `BT_ON`-`WL_ON` na PIM730, przeznaczona w razie potrzeby do przecięcia,
pozostaje nienaruszona. Pin `BT_ON`/`BL_ON` nie wymaga wtedy żadnego innego
połączenia. Sprawdź tę ścieżkę przed użyciem profilu. Jej przecięcie tworzy
inną topologię sprzętową, której nie opisuje obecnie żaden profil płytki.

Równoważna wygenerowana konfiguracja wygląda następująco:

```c
#define HAL_NETWORK_BACKEND_CYW43
#define HAL_CYW43_BUS_STM32_GSPI
#define HAL_CYW43_STACK_LWIP
#define HAL_CYW43_PIN_WL_ON       /* zakodowany GPIO STM32 */
#define HAL_CYW43_PIN_CHIP_SELECT /* zakodowany GPIO STM32 */
#define HAL_CYW43_PIN_DATA        /* współdzielony GPIO DAT/wybudzania hosta */
#define HAL_CYW43_PIN_CLOCK       /* zakodowany GPIO STM32 */
#define HAL_CYW43_MAX_TRANSACTION_BYTES 2048u
```

Cztery piny muszą być różnymi, prawidłowymi GPIO STM32G474. Maksymalny rozmiar
transakcji nie może być mniejszy niż osiem bajtów i musi być wielokrotnością
czterech. Obsługa danych gSPI działa w trybie odpytywania, a jej taktowanie
jest wyznaczane na podstawie licznika cykli DWT. Dzięki temu także po zmianie
zegara systemowego półokres jest wyznaczany z bezpiecznym zapasem. Linia DAT zmienia
kierunek zależnie od tego, czy dane są nadawane, czy odbierane. Zbocze sygnału
wybudzenia hosta obsługuje przerwanie EXTI o wysokim priorytecie, uzbrajane
na jedno wywołanie: ISR maskuje linię i zleca pracę, a kod obsługi uzbraja ją
ponownie po obsłużeniu wszystkich oczekujących prac CYW43/lwIP.

Oba backendy odpowiadają za zasilenie CYW43, wgranie firmware'u, interfejs
sieciowy lwIP, DHCP, DNS, odpowiedzi ICMP echo, bezpośrednią obsługę UDP/TCP,
skanowanie oraz zamknięcie całego podsystemu. `hal_net_service()` wykonuje
jedną iterację obsługi o ograniczonym zakresie. W konfiguracjach RP bez systemu RTOS oraz
na STM32G474 stosowany jest model oparty na odpytywaniu. Kod działający pod
kontrolą FreeRTOS korzysta jednak z tego samego, serializowanego kontekstu
stosu. Inicjalizacja, pule gniazd i nasłuchiwaczy oraz deinicjalizacja są
chronione oddzielnie. Dzięki temu deinicjalizacja zamyka wszystkie uchwyty
publicznego API przed zatrzymaniem lwIP, radia i magistrali.

Zużycie pamięci przez sieć ograniczają pule określane podczas buildu.
Głównymi ustawieniami są `HAL_TCP_SOCKET_MAX_INSTANCES` (domyślnie 4),
`HAL_TCP_LISTENER_MAX_INSTANCES` (domyślnie 2), `HAL_UDP_SOCKET_MAX_INSTANCES`
(domyślnie 4), `HAL_LWIP_TCP_RX_LIMIT` (domyślnie 16 KiB na silnik TCP),
`HAL_LWIP_TCP_ACCEPT_QUEUE_DEPTH` (domyślnie 5) oraz
`HAL_LWIP_UDP_RX_QUEUE_DEPTH` (domyślnie 4). Dobieraj te wartości razem z
`HAL_CYW43_MAX_TRANSACTION_BYTES` oraz wybraną konfiguracją lwIP, uwzględniając
ilość SRAM dostępną na platformie docelowej.

---

<a id="halhttpclient-httphttps-client-opt-in-halenablehttpclient"></a>

## `hal_http_client` - klient HTTP/HTTPS  *(opt-in - `HAL_ENABLE_HTTP_CLIENT`)*

`hal_http_client` wykonuje pojedyncze żądanie HTTP/1.1 z timeoutem przez HAL
TCP albo klienta TLS BearSSL z weryfikacją certyfikatu. Ta flaga włącza TCP
i WiFi. W przypadku HTTPS należy dodatkowo wybrać `HAL_ENABLE_TLS`.

```c
#include <hal/network/http/hal_http_client.h>

hal_http_client_request_t request;
hal_http_client_request_init(&request);
request.transport = HAL_HTTP_CLIENT_TRANSPORT_PLAINTEXT;
request.host = "example.com";
request.port = 80u;
request.method = "GET";
request.path = "/";
request.timeout_ms = 15000u;

uint8_t body[512];
hal_http_client_response_t response;
hal_status_t status =
    hal_http_client_perform_ex(&request, body, sizeof(body), &response);
```

Żądanie może odwoływać się do nagłówków przechowywanych przez wywołującego oraz
opcjonalnego ciała. Walidacja danych wejściowych odrzuca puste hosty,
nieprawidłowe metody, ścieżki, które nie są bezwzględne, wstrzyknięcia CR/LF, zerowe
porty/timeouty oraz niespójne pary wskaźnik/licznik.
`hal_http_client_request_init()` wybiera zwykłe GET `/`, port 80 oraz
15-sekundowy timeout.

Dla HTTPS ustaw `transport` na `HAL_HTTP_CLIENT_TRANSPORT_TLS`, zwykle użyj
portu 443 i wskaż w `tls_security` skonfigurowany magazyn zaufania, callback
czasu i callback entropii. Jednorazowe wywołanie tworzy klienta TLS w trybie
`bounded-worker`. Timeout żądania obowiązuje podczas łączenia, odczytu,
zapisu i zamykania. BearSSL weryfikuje nazwę hosta, a transport jest zamykany
przed powrotem z funkcji. Kotwica zaufania oraz dane używane przez callbacki
muszą pozostać ważne przez cały czas wykonywania wywołania.

Klient wysyła `Connection: close`, parsuje linie statusu HTTP/1.0 lub
HTTP/1.1, rozpoznaje `Content-Length` i odczytuje treść do zadeklarowanej
długości albo do zamknięcia połączenia. Treść odpowiedzi jest kopiowana bez
terminatora. Jeśli bufor wywołującego okaże się za mały, `HAL_EOVERFLOW`
zwraca wymaganą długość treści. Kodowanie transferu `chunked` powoduje zwrot
`HAL_EUNSUPPORTED`.

- **Implementacja:** `hal/network/http/hal_http_client.cpp`.
- **Testy:** `test_hal_http_client` obejmuje walidację, obsługę fragmentowanych
  nagłówków i metadanych odpowiedzi oraz kopiowanie treści do bufora o ograniczonym
  rozmiarze. `test_hal_http_client_plaintext_compile` sprawdza możliwość zbudowania
  konfiguracji bez TLS. Zweryfikowana ścieżka klienta HTTP/HTTPS jest częścią
  [`examples/18_freertos_suite`](../../../examples/18_freertos_suite/README.pl.md).

---

<a id="halnotify-notifications-opt-in-halenablenotify"></a>

## `hal_notify` - powiadomienia  *(opt-in - `HAL_ENABLE_NOTIFY`)*

`hal_notify` udostępnia wspólne API powiadomień. Korzysta ono z uchwytów
kanałów zabezpieczonych licznikami generacji oraz z deskryptorów backendów.
Warstwa wspólna zarządza cyklem życia kanału, wyborem domyślnego formatu
i timeoutu oraz oddzielną serializacją każdego kanału. Poszczególne backendy
przechowują natomiast konfigurację właściwego im protokołu.

```c
#include <hal/network/notify/hal_notify.h>

hal_notify_telegram_config_t telegram;
hal_notify_telegram_config_init(&telegram);
telegram.bot_token = TELEGRAM_BOT_TOKEN;
telegram.default_chat_id = TELEGRAM_CHAT_ID;
telegram.tls_security = &telegram_tls_security;

hal_notify_config_t notify;
hal_notify_config_init(&notify);
notify.backend = hal_notify_telegram_backend();
notify.backend_config = &telegram;
notify.device_name = "garage";

hal_notify_channel_t channel;
hal_notify_open(&notify, &channel);

hal_notify_message_t message;
hal_notify_message_init(&message);
message.title = "ECU alert";
message.body = "Coolant temperature threshold exceeded.";
message.severity = HAL_NOTIFY_SEVERITY_ERROR;

hal_notify_receipt_t receipt;
hal_notify_send(channel, &message, &receipt);
```

Pierwszym dostępnym backendem jest Telegram. Włącza go
`HAL_ENABLE_NOTIFY_TELEGRAM`, które włącza też `HAL_ENABLE_NOTIFY`,
`HAL_ENABLE_HTTP_CLIENT`, `HAL_ENABLE_TLS` i `HAL_ENABLE_CJSON`. Backend wysyła
żądania `sendMessage` Telegram Bot API przez `hal_http_client_perform_ex()`.
Wysyłanie do publicznego hosta `api.telegram.org` wymaga HTTPS oraz wskaźnika
`hal_tls_security_config_t` różnego od `NULL`. Zwykły HTTP jest dozwolony
wyłącznie dla niestandardowego hosta podanego przez wywołującego, na przykład
lokalnego proxy lub lokalnego wdrożenia Bot API. Porównanie z publicznym hostem
nie rozróżnia wielkości liter ASCII i dopuszcza końcową kropkę w bezwzględnej
nazwie DNS. Różna pisownia nie pozwala więc ominąć tej reguły.

Konfiguracja backendu przechowuje jedynie odwołania do napisów i danych
konfiguracyjnych TLS przekazanych przez wywołującego. JaszczurHAL nie zapisuje
ani nie konfiguruje tokenu bota czy identyfikatora czatu. Aplikacja powinna
pobrać te dane z własnego magazynu poświadczeń, zapewnić ważność wskazywanych
buforów aż do wywołania `hal_notify_close()`, a następnie zwolnić je zgodnie
z zasadami tego magazynu. Wiadomość dziedziczy `device_name` ustawiony dla
kanału, jeśli sama nie podaje innej wartości.

Backend Telegram dodaje na początku wiadomości poziom ważności i opcjonalną
tożsamość urządzenia, na przykład `[ERROR] [garage] ECU alert`. Wiadomości
w postaci zwykłego tekstu, które przekraczają limit jednego żądania
`HAL_NOTIFY_TELEGRAM_TEXT_MAX` (domyślnie 3500 bajtów), są dzielone na
granicach znaków UTF-8, najlepiej w pobliżu białych znaków. Poszczególne
części otrzymują oznaczenia `(1/N)`, `(2/N)` itd. Limit uwzględnia wygenerowany
prefiks i pozostawia zapas względem limitu 4096 znaków funkcji `sendMessage`
Telegrama. Wiadomości sformatowane jako MarkdownV2 lub HTML nie są dzielone
automatycznie, ponieważ mogłoby to uszkodzić składnię przekazaną przez
wywołującego. Zbyt długa wiadomość sformatowana zwraca `HAL_EOVERFLOW`.

`HAL_NOTIFY_MESSAGE_SILENT` odwzorowuje się na `disable_notification`
Telegrama, natomiast `HAL_NOTIFY_MESSAGE_SUPPRESS_LINK_PREVIEW` używa opcji
podglądu linku Telegrama. Błędy HTTP/API Telegrama są zgłaszane przez
`hal_status_t` oraz opcjonalne pola `hal_notify_receipt_t`: status HTTP/API,
błąd zwrócony przez providera, wartość `retry-after` oraz identyfikator
wiadomości nadany przez providera. Pola `parts_sent` i `parts_total` informują
o postępie wysyłania wiadomości wieloczęściowej;
`HAL_NOTIFY_RECEIPT_PARTIAL_DELIVERY` oznacza, że co najmniej jedna część
została zaakceptowana, zanim kolejna część zawiodła. Dostarczanie
wieloczęściowe nie jest więc atomowe.

`hal_notify_send()` jest synchronicznym wywołaniem z timeoutem. W przypadku
podzielonej wiadomości może wykonać kilka żądań HTTP. Jeżeli główna pętla
sterująca musi pozostać responsywna, należy wywoływać tę funkcję z osobnego
zadania aplikacji lub RTOS. `hal_notify_poll()` obsługuje tylko backendy
działające w modelu odpytywania; nie zmienia backendu
synchronicznego w asynchroniczny. Gdy backend można zamknąć natychmiast,
`hal_notify_close()` zwraca jego status. Jeśli kanał jest już używany przez
inną operację, zamknięcie zostaje odroczone. Wynik zamknięcia zwróci ostatnia
operacja, o ile sama nie zakończy się błędem.

- **Implementacja:** `hal/network/notify/hal_notify.cpp` oraz
  `hal/network/notify/hal_notify_telegram.cpp`.
- **Testy:** `test_hal_notify` obejmuje walidację wspólnego API, wywołanie
  testowego backendu, cykl życia uchwytu i błędy zamknięcia, JSON/prefiksy żądań
  Telegrama, ujednolicone odrzucanie HTTP dla publicznego hosta, dostarczanie
  wieloczęściowe oraz mapowanie ograniczeń liczby żądań (`rate limit`).
  `test_hal_notify_c_compile` obejmuje interfejs nagłówka C.

---

## `hal_http_server` - serwer HTTP/1.1  *(opt-in - `HAL_ENABLE_HTTP_SERVER`)*

Niewielki serwer HTTP pracujący w trybie odpytywania, zbudowany na API
nasłuchiwaczy i gniazd `hal_tcp` opartym na uchwytach. Włączenie
`HAL_ENABLE_HTTP_SERVER` powoduje włączenie `HAL_ENABLE_TCP`, a ta flaga
z kolei włącza `HAL_ENABLE_WIFI` w obecnych konfiguracjach obsługujących sieć.

Pierwsza wersja jest celowo niewielka i deterministyczna:

- dokładne dopasowywanie tras na podstawie metody i ścieżki,
- jedno żądanie na połączenie TCP,
- parsowanie metod `GET`, `HEAD`, `POST`, `PUT`, `DELETE` i `OPTIONS`,
- przekazywanie handlerowi ciągu zapytania, nagłówków i treści żądania,
- buforowanie treści odpowiedzi z automatycznym nagłówkiem `Content-Length`,
- rejestracja tras dokładnych i prefiksowych,
- jawne funkcje pomocnicze do ustawiania statusu, typu zawartości i nagłówków
  odpowiedzi,
- kooperacyjna pętla obsługi `hal_http_server_poll()`.

```c
#include <hal/network/http/hal_http_server.h>

typedef enum {
  HAL_HTTP_METHOD_UNKNOWN = 0,
  HAL_HTTP_METHOD_GET,
  HAL_HTTP_METHOD_HEAD,
  HAL_HTTP_METHOD_POST,
  HAL_HTTP_METHOD_PUT,
  HAL_HTTP_METHOD_DELETE,
  HAL_HTTP_METHOD_OPTIONS
} hal_http_method_t;

typedef struct {
  const char *name;
  const char *value;
} hal_http_header_t;

typedef struct {
  hal_http_method_t method;
  const char *path;
  const char *query;
  const char *body;
  size_t body_len;
  const hal_http_header_t *headers;
  size_t header_count;
  hal_net_endpoint_t remote;
} hal_http_request_t;

typedef hal_status_t (*hal_http_handler_t)(const hal_http_request_t *request,
                                           hal_http_response_t *response,
                                           void *user);

hal_status_t hal_http_server_route(hal_http_method_t method, const char *path,
                                   hal_http_handler_t handler, void *user);
hal_status_t hal_http_server_route_prefix(hal_http_method_t method,
                                          const char *path_prefix,
                                          hal_http_handler_t handler,
                                          void *user);
void hal_http_server_clear_routes(void);
hal_status_t hal_http_server_start(uint16_t port);
void hal_http_server_stop(void);
bool hal_http_server_is_running(void);
void hal_http_server_poll(void);

hal_status_t hal_http_response_set_status(hal_http_response_t *response,
                                          uint16_t status_code,
                                          const char *reason);
hal_status_t hal_http_response_set_content_type(
    hal_http_response_t *response,
    const char *content_type);
hal_status_t hal_http_response_set_header(hal_http_response_t *response,
                                          const char *name,
                                          const char *value);
hal_status_t hal_http_response_write(hal_http_response_t *response,
                                     const void *data,
                                     size_t len);
hal_status_t hal_http_response_write_str(hal_http_response_t *response,
                                         const char *text);
const char *hal_http_request_get_header(const hal_http_request_t *request,
                                        const char *name);
const char *hal_http_method_to_string(hal_http_method_t method);
```

Przykładowy handler:

```c
static hal_status_t status_route(const hal_http_request_t *request,
                                 hal_http_response_t *response,
                                 void *user) {
  (void)user;
  const char *content_type =
      hal_http_request_get_header(request, "Content-Type");
  (void)content_type;
  hal_status_t status =
      hal_http_response_set_content_type(response, "application/json");
  if (status != HAL_OK) {
    return status;
  }
  return hal_http_response_write_str(response, "{\"ok\":true}");
}
```

Uruchamianie i obsługa z pętli aplikacji:

```c
hal_http_server_route(HAL_HTTP_METHOD_GET, "/api/status", status_route, NULL);
hal_http_server_start(80);

for (;;) {
  hal_http_server_poll();
}
```

Domyślne limity statyczne można nadpisać przed dołączeniem nagłówków HAL:

```c
#define HAL_HTTP_SERVER_MAX_ROUTES 8u
#define HAL_HTTP_SERVER_MAX_CLIENTS 2u
#define HAL_HTTP_SERVER_REQUEST_BUFFER_SIZE 512u
#define HAL_HTTP_SERVER_RESPONSE_BUFFER_SIZE 1024u
#define HAL_HTTP_SERVER_MAX_REQUEST_HEADERS 12u
#define HAL_HTTP_SERVER_MAX_RESPONSE_HEADERS 8u
#define HAL_HTTP_SERVER_RESPONSE_HEADER_SIZE 512u
#define HAL_HTTP_SERVER_DEFAULT_BACKLOG 2u
```

- **Wspólna implementacja modułu:** `hal/network/http/hal_http_server.cpp`.
- **impl/.mock:** `test_hal_http_server` korzysta z testowego backendu
  nasłuchiwacza i gniazda TCP.

---

## `hal_http_files` - serwowanie i przesyłanie plików  *(opt-in - `HAL_ENABLE_HTTP_FILES`)*

Niewielki adapter plików zbudowany na `hal_http_server`. Włączenie
`HAL_ENABLE_HTTP_FILES` włącza również `HAL_ENABLE_HTTP_SERVER`,
`HAL_ENABLE_TCP` i `HAL_ENABLE_WIFI`.

Adapter nie zależy od konkretnego systemu plików. Odwzorowuje adresy URL HTTP
na zamontowany katalog główny i wywołuje callbacki aplikacji lub backendu dla
operacji `stat`, `read` oraz opcjonalnie `write`. Dzięki temu ta sama warstwa
HTTP może obsługiwać zasoby w RAM-ie, LittleFS, FatFs/SD, pamięci flash oraz
implementacje testowe.

Obsługiwane operacje i mechanizmy:

- serwowanie plików `GET` / `HEAD` przez trasy prefiksowe,
- wybór typu MIME na podstawie rozszerzenia,
- generowanie słabych ETagów na podstawie ścieżki, rozmiaru i `mtime`,
- `If-None-Match` -> `304 Not Modified`,
- przesyłanie surowej treści metodą `PUT` na ścieżkę pod zamontowanym prefiksem,
- przesyłanie metodą `POST` danych `multipart/form-data` z polami `path` i `file`,
- odrzucanie prób wyjścia poza katalog za pomocą `..` lub ukośników odwrotnych.

```c
#include <hal/network/http/hal_http_files.h>

typedef struct {
  bool exists;
  bool is_dir;
  size_t size;
  uint32_t mtime;
  const char *content_type;
  const char *etag;
} hal_http_file_info_t;

typedef hal_status_t (*hal_http_file_stat_cb_t)(
    const char *path,
    hal_http_file_info_t *out_info,
    void *user);

typedef hal_status_t (*hal_http_file_read_cb_t)(
    const char *path,
    size_t offset,
    void *buffer,
    size_t max_len,
    size_t *out_len,
    void *user);

typedef hal_status_t (*hal_http_file_write_cb_t)(
    const char *path,
    size_t offset,
    const void *data,
    size_t len,
    bool final,
    void *user);

typedef enum {
  HAL_HTTP_FILE_UPLOAD_RAW = 0,
  HAL_HTTP_FILE_UPLOAD_MULTIPART
} hal_http_file_upload_t;

typedef hal_status_t (*hal_http_file_authorize_cb_t)(
    const hal_http_request_t *request,
    hal_http_file_upload_t upload,
    void *user);

typedef struct {
  const char *url_prefix;
  const char *fs_root;
  const char *index_name;
  const char *upload_path;
  bool enable_upload;
  hal_http_file_stat_cb_t stat;
  hal_http_file_read_cb_t read;
  hal_http_file_write_cb_t write;
  hal_http_file_authorize_cb_t authorize_upload;
  void *user;
} hal_http_files_config_t;

hal_status_t hal_http_files_mount(const hal_http_files_config_t *config);
void hal_http_files_clear(void);
const char *hal_http_files_content_type_for_path(const char *path);
hal_status_t hal_http_files_make_etag(const char *path,
                                      const hal_http_file_info_t *info,
                                      char *out,
                                      size_t out_size);
```

Podstawowy sposób użycia:

```c
hal_http_files_config_t cfg = {0};
cfg.url_prefix = "/files";
cfg.fs_root = "/www";
cfg.upload_path = "/upload";
cfg.enable_upload = true;
cfg.stat = my_stat;
cfg.read = my_read;
cfg.write = my_write;
cfg.authorize_upload = my_authorize_upload;

hal_http_files_mount(&cfg);
hal_http_server_start(80);

for (;;) {
  hal_http_server_poll();
}
```

Przesyłanie plików działa zgodnie z zasadą fail-closed: ustawienie
`enable_upload = true` wymaga zarówno callbacku `write`, jak i
`authorize_upload`. Callback autoryzacji jest wywoływany przed analizą danych
multipart i przed zapisem w systemie plików. Musi zwrócić `HAL_OK`; każdy inny
status powoduje odpowiedź HTTP 403. Jeśli poświadczenia są przesyłane przez
niezaufaną sieć, używaj TLS.

Przykład przesyłania danych multipart:

```http
POST /upload HTTP/1.1
Content-Type: multipart/form-data; boundary=AaB03x

--AaB03x
Content-Disposition: form-data; name="path"

logs
--AaB03x
Content-Disposition: form-data; name="file"; filename="boot.txt"
Content-Type: text/plain

hello
--AaB03x--
```

Przy powyższej konfiguracji callback obsługi pliku otrzymuje ścieżkę
`/www/logs/boot.txt`.

Obecny `hal_http_server` przechowuje każde żądanie i każdą odpowiedź
w statycznych buforach o stałym rozmiarze. Adapter nadaje się więc do małych
plików wbudowanych, przesyłania konfiguracji i diagnostyki, ale nie do dużych
transferów strumieniowych.

Domyślne limity statyczne można nadpisać przed dołączeniem nagłówków HAL:

```c
#define HAL_HTTP_FILES_MAX_MOUNTS 2u
#define HAL_HTTP_FILES_PATH_MAX 128u
#define HAL_HTTP_FILES_ETAG_MAX 48u
#define HAL_HTTP_FILES_IO_BUFFER_SIZE 128u
```

- **Wspólna implementacja modułu:** `hal/network/http/hal_http_files.cpp`.
- **impl/.mock:** `test_hal_http_files` korzysta z testowych implementacji
  HTTP i TCP.

---

## `hal_websocket` - serwer WebSocket  *(opt-in - `HAL_ENABLE_WEBSOCKET`)*

Niewielki serwer WebSocket działający w trybie odpytywania, zaimplementowany
bezpośrednio na bazie `hal_tcp`. Włączenie `HAL_ENABLE_WEBSOCKET` powoduje
włączenie `HAL_ENABLE_TCP`, a ta flaga z kolei włącza `HAL_ENABLE_WIFI`
w obecnych konfiguracjach z obsługą sieci.

Serwer przyjmuje klientów TCP i przeprowadza uzgadnianie HTTP Upgrade dla
jednej skonfigurowanej ścieżki. Następnie każde zaakceptowane gniazdo
przechodzi w tryb analizy ramek WebSocket. Pierwsza implementacja jest celowo
niewielka:

- handshake `Sec-WebSocket-Accept` zgodny z RFC 6455,
- maskowane ramki klienta i niemaskowane ramki serwera,
- wiadomości tekstowe i binarne mieszczące się w jednej ramce,
- automatyczna odpowiedź pong na ping,
- obsługa ramki zamknięcia z callbackiem rozłączenia,
- funkcje pomocnicze do wysyłania danych do jednego klienta lub do wszystkich
  klientów (`broadcast`),
- kooperacyjna pętla obsługi `hal_websocket_server_poll()`.

Nie implementuje fragmentowanych wiadomości, permessage-deflate, TLS,
ciasteczek ani negocjacji subprotokołu. Uwierzytelnianie lub zasady sesji
należy zaimplementować w protokole aplikacji albo na stronie HTTP otwierającej
gniazdo.

```c
#include <hal/network/websocket/hal_websocket.h>

typedef uint8_t hal_websocket_client_t;

typedef enum {
  HAL_WEBSOCKET_MESSAGE_TEXT = 1,
  HAL_WEBSOCKET_MESSAGE_BINARY = 2
} hal_websocket_message_type_t;

typedef struct {
  void (*on_connect)(hal_websocket_client_t client, void *user);
  void (*on_message)(hal_websocket_client_t client,
                     hal_websocket_message_type_t type,
                     const uint8_t *data,
                     size_t len,
                     void *user);
  void (*on_disconnect)(hal_websocket_client_t client,
                        uint16_t close_code,
                        void *user);
} hal_websocket_callbacks_t;

hal_status_t hal_websocket_server_set_callbacks(
    const hal_websocket_callbacks_t *callbacks,
    void *user);
hal_status_t hal_websocket_server_start(uint16_t port, const char *path);
void hal_websocket_server_stop(void);
bool hal_websocket_server_is_running(void);
void hal_websocket_server_poll(void);

size_t hal_websocket_client_count(void);
bool hal_websocket_client_is_connected(hal_websocket_client_t client);

hal_status_t hal_websocket_send(hal_websocket_client_t client,
                                hal_websocket_message_type_t type,
                                const void *data,
                                size_t len);
hal_status_t hal_websocket_send_text(hal_websocket_client_t client,
                                     const char *text);
hal_status_t hal_websocket_broadcast(hal_websocket_message_type_t type,
                                     const void *data,
                                     size_t len,
                                     size_t *sent_count);
hal_status_t hal_websocket_broadcast_text(const char *text,
                                          size_t *sent_count);
hal_status_t hal_websocket_close(hal_websocket_client_t client,
                                 uint16_t close_code);
```

Minimalna konfiguracja callbacków:

```c
static void ws_message(hal_websocket_client_t client,
                       hal_websocket_message_type_t type,
                       const uint8_t *data,
                       size_t len,
                       void *user) {
  (void)type;
  (void)user;
  hal_websocket_send(client, HAL_WEBSOCKET_MESSAGE_TEXT, data, len);
}

hal_websocket_callbacks_t cb = {0};
cb.on_message = ws_message;
hal_websocket_server_set_callbacks(&cb, NULL);
hal_websocket_server_start(81, "/ws");

for (;;) {
  hal_websocket_server_poll();
}
```

Rozgłaszanie telemetrii:

```c
char msg[64];
size_t sent_count = 0u;
snprintf(msg, sizeof(msg), "uptime=%lu", (unsigned long)hal_millis());
hal_websocket_broadcast_text(msg, &sent_count);
```

Domyślne limity statyczne można nadpisać przed dołączeniem nagłówków HAL:

```c
#define HAL_WEBSOCKET_MAX_CLIENTS 2u
#define HAL_WEBSOCKET_REQUEST_BUFFER_SIZE 512u
#define HAL_WEBSOCKET_FRAME_BUFFER_SIZE 256u
#define HAL_WEBSOCKET_DEFAULT_BACKLOG 2u
```

- **Wspólna implementacja modułu:** `hal/network/websocket/hal_websocket.cpp`.
- **impl/.mock:** `test_hal_websocket` korzysta z testowego backendu
  nasłuchiwacza i gniazda TCP.

---

## `hal_net_console` - konsola debugowania TCP  *(opt-in - `HAL_ENABLE_NET_CONSOLE`)*

Chroniona hasłem konsola TCP zbudowana na API nasłuchiwaczy i gniazd `hal_tcp`
opartym na uchwytach. Włączenie `HAL_ENABLE_NET_CONSOLE` powoduje włączenie
`HAL_ENABLE_TCP`, a ta flaga z kolei włącza `HAL_ENABLE_WIFI` w konfiguracjach
z obsługą sieci.

Konsola stanowi dodatkową warstwę transportową, a nie zamiennik zwykłego
portu debugowania:
`hal_serial`, `deb` i `derr` nadal piszą do UART/USB, a uwierzytelnieni
klienci TCP otrzymują dodatkową kopię. Firmware odbiera dane z TCP przez
callback wywoływany dla każdej linii oraz bufor RX obsługiwany przez
odpytywanie. Pozwala to aplikacji udostępnić prostą powłokę poleceń lub
interfejs diagnostyczny.

Model bezpieczeństwa: API wymaga niepustego hasła, ale transport to zwykłe
TCP. Używaj jej wyłącznie w zaufanych sieciach lub za bezpiecznym
tunelem/VPN, gdy liczy się dostęp zdalny.

```c
#include <hal/network/net_console/hal_net_console.h>

#define HAL_NET_CONSOLE_DEFAULT_PORT 2323u

typedef uint8_t hal_net_console_client_t;

typedef enum {
  HAL_NET_CONSOLE_EVENT_CONNECT = 0,
  HAL_NET_CONSOLE_EVENT_AUTHENTICATED,
  HAL_NET_CONSOLE_EVENT_DISCONNECT
} hal_net_console_event_t;

typedef void (*hal_net_console_event_cb_t)(hal_net_console_client_t client,
                                           hal_net_console_event_t event,
                                           void *user);
typedef hal_status_t (*hal_net_console_line_cb_t)(
    hal_net_console_client_t client,
    const char *line,
    void *user);

hal_status_t hal_net_console_set_callbacks(hal_net_console_event_cb_t event_cb,
                                           hal_net_console_line_cb_t line_cb,
                                           void *user);
hal_status_t hal_net_console_start(uint16_t port, const char *password);
void hal_net_console_stop(void);
bool hal_net_console_is_running(void);
void hal_net_console_poll(void);

size_t hal_net_console_client_count(void);
size_t hal_net_console_authenticated_count(void);
bool hal_net_console_client_is_authenticated(hal_net_console_client_t client);

hal_status_t hal_net_console_write(const void *data, size_t len);
hal_status_t hal_net_console_write_text(const char *text);
hal_status_t hal_net_console_write_to(hal_net_console_client_t client,
                                      const void *data,
                                      size_t len);
hal_status_t hal_net_console_write_text_to(hal_net_console_client_t client,
                                           const char *text);

int hal_net_console_available(void);
int hal_net_console_read(void *buffer, size_t max_len);
void hal_net_console_close(hal_net_console_client_t client);
```

Minimalny callback poleceń:

```c
static hal_status_t console_line(hal_net_console_client_t client,
                                 const char *line,
                                 void *user) {
  (void)user;
  if (strcmp(line, "status") == 0) {
    return hal_net_console_write_text_to(client, "ok\r\n");
  }
  return hal_net_console_write_text_to(client, "unknown\r\n");
}

hal_net_console_set_callbacks(NULL, console_line, NULL);
hal_net_console_start(HAL_NET_CONSOLE_DEFAULT_PORT, "change-me");

for (;;) {
  hal_net_console_poll();
}
```

Domyślne limity statyczne można nadpisać przed dołączeniem nagłówków HAL:

```c
#define HAL_NET_CONSOLE_MAX_CLIENTS 2u
#define HAL_NET_CONSOLE_RX_BUFFER_SIZE 256u
#define HAL_NET_CONSOLE_TX_BUFFER_SIZE 1024u
#define HAL_NET_CONSOLE_LINE_BUFFER_SIZE 128u
#define HAL_NET_CONSOLE_PASSWORD_MAX 64u
#define HAL_NET_CONSOLE_DEFAULT_BACKLOG 2u
```

- **Wspólna implementacja modułu:** `hal/network/net_console/hal_net_console.cpp`.
- **impl/.mock:** `test_hal_net_console` korzysta z testowego backendu
  nasłuchiwacza i gniazda TCP.

---

## `hal_net_commands` - warstwa komend HTTP/WebSocket  *(opt-in - `HAL_ENABLE_NET_COMMANDS`)*

Adaptery tekstowe i JSON dla wbudowanych kanałów sterowania WebUI. Moduł
analizuje dane z HTTP i WebSocket, przekazuje polecenia do współdzielonego,
domyślnego [`hal_command_router`](23_commands.md), a następnie formatuje
odpowiedź w buforze o ograniczonym rozmiarze. Włączenie
`HAL_ENABLE_NET_COMMANDS` włącza również
`HAL_ENABLE_COMMAND_ROUTER`, `HAL_ENABLE_HTTP_SERVER`,
`HAL_ENABLE_WEBSOCKET`, `HAL_ENABLE_CJSON`, `HAL_ENABLE_TCP` i
`HAL_ENABLE_WIFI`.

Żądania mogą być zwykłym tekstem:

```text
status
echo hello
```

lub JSON-em parsowanym przez cJSON:

```json
{"cmd":"status","args":{"verbose":true}}
```

Pola `cmd` i `command` mogą zawierać nazwę polecenia. Pola `args` i `params`
są przekazywane handlerom jako `json_args`, a argumenty tekstowe są dostępne
również przez `args_text`.

Handler ogólnego przeznaczenia `hal_command_handler_t` otrzymuje argumenty tekstowe jako
ciąg bajtów po nazwie polecenia, bez założeń o ich zawartości (binary-safe).
Dla JSON otrzymuje zwartą serializację wyłącznie wartości `args` lub
`params`; brak wartości oznacza pusty widok argumentów. Żądania sieciowe
używają zerowych identyfikatorów żądania, peera i sesji. Obecnie nie ustawiają
też żadnych flag bezpieczeństwa poleceń.

```c
#include <hal/network/net_commands/hal_net_commands.h>

typedef enum {
  HAL_NET_COMMANDS_FORMAT_TEXT = 0,
  HAL_NET_COMMANDS_FORMAT_JSON,
  HAL_NET_COMMANDS_FORMAT_AUTO
} hal_net_commands_format_t;

typedef hal_command_source_t hal_net_commands_source_t;

#define HAL_NET_COMMANDS_SOURCE_DIRECT HAL_COMMAND_SOURCE_DIRECT
#define HAL_NET_COMMANDS_SOURCE_HTTP HAL_COMMAND_SOURCE_HTTP
#define HAL_NET_COMMANDS_SOURCE_WEBSOCKET HAL_COMMAND_SOURCE_WEBSOCKET

typedef struct {
  hal_net_commands_source_t source;
  const char *command;
  const char *args_text;
  const cJSON *json_root;
  const cJSON *json_args;
  const hal_http_request_t *http_request;
  hal_websocket_client_t websocket_client;
} hal_net_command_request_t;

typedef hal_command_response_t hal_net_command_response_t;

typedef hal_status_t (*hal_net_command_handler_t)(
    const hal_net_command_request_t *request,
    hal_net_command_response_t *response,
    void *user);

hal_status_t hal_net_commands_register(const char *name,
                                       hal_net_command_handler_t handler,
                                       void *user);
hal_status_t hal_net_commands_unregister(const char *name);
hal_status_t hal_net_commands_clear(void);
size_t hal_net_commands_count(void);

hal_status_t hal_net_commands_execute_text(
    const char *text,
    hal_net_command_response_t *response);
hal_status_t hal_net_commands_execute_json(
    const char *json,
    size_t len,
    hal_net_command_response_t *response);
hal_status_t hal_net_commands_execute(
    const void *data,
    size_t len,
    hal_net_commands_format_t format,
    hal_net_command_response_t *response);

hal_status_t hal_net_commands_register_http_route(
    const char *path,
    hal_net_commands_format_t format);
hal_status_t hal_net_commands_handle_http_request(
    const hal_http_request_t *request,
    hal_http_response_t *response,
    hal_net_commands_format_t format);
hal_status_t hal_net_commands_handle_websocket_message(
    hal_websocket_client_t client,
    hal_websocket_message_type_t type,
    const uint8_t *data,
    size_t len,
    hal_net_commands_format_t format);
```

Funkcje rejestracji zachowujące zgodność zapisują handlery w domyślnym
routerze, ale ograniczają je do źródeł bezpośrednich, HTTP i WebSocket. Ich
widok żądania zawiera bowiem pola dostępne wyłącznie po analizie danych
sieciowych. Jeśli jeden handler bezpiecznie obsługujący dane binarne ma również
obsługiwać LoRa lub inny adapter, zarejestruj `hal_command_definition_t`
bezpośrednio w domyślnym routerze. Ścieżki sieciowe nie ustawiają obecnie flag
bezpieczeństwa poleceń, dlatego polityki routera wymagające takich flag
odrzucają te żądania. `hal_net_commands_count()`, wyrejestrowywanie
i czyszczenie korzystają z tego samego, współdzielonego zestawu
handlerów. Gdy żaden handler nie jest aktywny, `hal_net_commands_clear()`
usuwa również rejestracje ogólne i zwraca `hal_status_t`. Jeśli w domyślnym
routerze trwa obsługa polecenia, funkcja zwraca `HAL_EBUSY` i nie zmienia
zestawu handlerów.

Pomocnicy odpowiedzi dopisują do bufora odpowiedzi o stałym rozmiarze i
używają `hal_status_t`:

```c
void hal_net_command_response_reset(hal_net_command_response_t *response);
hal_status_t hal_net_command_response_set_status(
    hal_net_command_response_t *response,
    hal_status_t status,
    const char *message);
hal_status_t hal_net_command_response_set_content_type(
    hal_net_command_response_t *response,
    const char *content_type);
hal_status_t hal_net_command_response_write(
    hal_net_command_response_t *response,
    const void *data,
    size_t len);
hal_status_t hal_net_command_response_write_str(
    hal_net_command_response_t *response,
    const char *text);
hal_status_t hal_net_command_response_write_json(
    hal_net_command_response_t *response,
    const cJSON *json);

const char *hal_net_commands_format_to_string(
    hal_net_commands_format_t format);
```

Wspólna struktura odpowiedzi zachowuje ustaloną kolejność pól sieciowych,
a na końcu dodaje `encoding`. Napisy wskazywane przez `message` i
`content_type` nie są kopiowane. Handler musi więc zapewnić ich ważność do
chwili sformatowania i wysłania odpowiedzi.

Podstawowy sposób użycia:

```c
static hal_status_t status_command(const hal_net_command_request_t *request,
                                   hal_net_command_response_t *response,
                                   void *user) {
  (void)request;
  (void)user;
  cJSON *root = cJSON_CreateObject();
  if (!root) {
    return HAL_ENOMEM;
  }
  cJSON_AddStringToObject(root, "status", "ok");
  hal_status_t status = hal_net_command_response_write_json(response, root);
  cJSON_Delete(root);
  return status;
}

hal_net_commands_register("status", status_command, NULL);
hal_net_commands_register_http_route(HAL_NET_COMMANDS_DEFAULT_HTTP_PATH,
                                     HAL_NET_COMMANDS_FORMAT_AUTO);
```

Dla WebSocket wywołaj pomocnika ze zwykłego callbacku wiadomości:

```c
static void ws_message(hal_websocket_client_t client,
                       hal_websocket_message_type_t type,
                       const uint8_t *data,
                       size_t len,
                       void *user) {
  (void)user;
  hal_net_commands_handle_websocket_message(
      client, type, data, len, HAL_NET_COMMANDS_FORMAT_AUTO);
}
```

Jeśli handler nie zapisze treści, kod obsługi tworzy małą odpowiedź domyślną
w formacie żądania. Nieznane polecenia zwracają `HAL_ENOENT`; błędy
parsowania JSON zwracają `HAL_EPROTO`. Integracja HTTP mapuje typowe błędy
HAL na kody statusu HTTP (`400`, `403`, `404`, `413`, `500`) i nadal zwraca
`HAL_OK` do serwera HTTP, gdy tylko odpowiedź została zapisana.

Domyślne limity statyczne można nadpisać przed dołączeniem nagłówków HAL:

```c
#define HAL_COMMAND_ROUTER_MAX_COMMANDS 8u
#define HAL_COMMAND_ROUTER_NAME_MAX 32u
#define HAL_NET_COMMANDS_TEXT_BUFFER_SIZE 256u
#define HAL_COMMAND_RESPONSE_BUFFER_SIZE 512u
```

Poprzednie nazwy `HAL_NET_COMMANDS_MAX_COMMANDS`, `HAL_NET_COMMANDS_NAME_MAX`
i `HAL_NET_COMMANDS_RESPONSE_BUFFER_SIZE` pozostają aliasami wspólnych limitów
routera. Jeśli zdefiniowano obie formy, ich wartości muszą być takie same.

- **Wspólna implementacja modułu:** `hal/network/net_commands/hal_net_commands.cpp`.
- **impl/.mock:** `test_hal_net_commands` korzysta z testowych backendów TCP,
  HTTP i WebSocket.

---


## `hal_ota` - aktualizacja firmware'u z opcjonalnym AUTH2  *(opt-in - `HAL_ENABLE_OTA`)*

Natywna usługa OTA nad HAL UDP/TCP, przystosowana do pracy wielowątkowej.
Implementacje dla RP i ESP32-S3 współdzielą mechanizmy wykrywania, wymianę
AUTH2 opartą na HMAC-SHA256 wyprowadzonym z hasła, przesyłanie danych,
callbacki oraz sposób prezentowania stanu rozruchu przez publiczne API.
Format obrazu i model aktywacji pozostają zależne od platformy docelowej.

```c
#include <hal/network/ota/hal_ota.h>

typedef enum {
  HAL_OTA_COMMAND_SKETCH = 0,
  HAL_OTA_COMMAND_FILESYSTEM = 1,
  HAL_OTA_COMMAND_UNKNOWN = 255
} hal_ota_command_t;

typedef enum {
  HAL_OTA_ERROR_AUTH = 1,
  HAL_OTA_ERROR_BEGIN = 2,
  HAL_OTA_ERROR_CONNECT = 3,
  HAL_OTA_ERROR_RECEIVE = 4,
  HAL_OTA_ERROR_END = 5,
  HAL_OTA_ERROR_UNKNOWN = 255
} hal_ota_error_t;

typedef void (*hal_ota_on_start_callback_t)(hal_ota_command_t command, void *user);
typedef void (*hal_ota_on_end_callback_t)(void *user);
typedef void (*hal_ota_on_progress_callback_t)(uint32_t progress, uint32_t total, void *user);
typedef void (*hal_ota_on_error_callback_t)(hal_ota_error_t error, void *user);

bool hal_ota_set_port(uint16_t port);
bool hal_ota_set_hostname(const char *hostname);
bool hal_ota_set_password(const char *password);

bool hal_ota_on_start(hal_ota_on_start_callback_t callback, void *user);
bool hal_ota_on_end(hal_ota_on_end_callback_t callback, void *user);
bool hal_ota_on_progress(hal_ota_on_progress_callback_t callback, void *user);
bool hal_ota_on_error(hal_ota_on_error_callback_t callback, void *user);

bool hal_ota_begin(void);
void hal_ota_handle(void);
bool hal_ota_is_started(void);

hal_status_t hal_ota_confirm_boot_ex(void);
hal_status_t hal_ota_get_boot_info_ex(hal_ota_boot_info_t *out_info);
```

**Uwagi dotyczące zachowania:**
- Moduł jest dostępny wyłącznie, gdy zdefiniowano `HAL_ENABLE_OTA`.
- Włącza również `WIFI`, `UDP`, `TCP`, `CRYPTO` i `CRC`.
- `hal_ota_begin()` inicjalizuje usługę OTA i rejestruje wewnętrzne mechanizmy
  obsługi zdarzeń.
- `hal_ota_handle()` odpytuje transport OTA i przekazuje zakolejkowane
  zdarzenia do callbacków użytkownika.
- Callback można zastąpić albo wyrejestrować, przekazując `NULL`.
- Ponowne wywołanie `hal_ota_begin()` przed pierwszym wywołaniem obsługi czyści
  zdarzenia zakolejkowane przez implementację testową lub driver.
- Gdy skonfigurowano niepuste hasło, AUTH2 wiąże polecenie, port połączenia
  zwrotnego, rozmiar obrazu, jego MD5 oraz niezależne wartości nonce
  urządzenia i klienta. Uwierzytelnienie jest przyjmowane wyłącznie z adresu IP
  i portu źródłowego, z których nadeszło zaproszenie UDP. Połączenie zwrotne TCP
  musi pochodzić z tego samego adresu IP. Starsze wiadomości AUTH/200 są
  odrzucane, a niepuste hasło hosta nie może zaakceptować bezpośredniego `OK`.
- AUTH2 wymaga ściśle określonego formatu pól ASCII oraz liczb dziesiętnych
  zapisanych w najkrótszej postaci. Odrzuca niejednoznaczne białe znaki,
  osadzone znaki NUL, dodatkowe pola lub linie, nieprawidłowo zapisane długości
  oraz alternatywne postacie liczb z zerami wiodącymi. Wartości nonce
  urządzenia i klienta pochodzą
  odpowiednio z bezpiecznego generatora losowego platformy docelowej oraz
  z CSPRNG systemu operacyjnego hosta.
- Pominięcie `hal_ota_set_password()` lub przekazanie pustego łańcucha
  pomija AUTH2. Ten tryb jest nieuwierzytelniony i nadaje się wyłącznie do
  izolowanych sieci deweloperskich.
- Natywne obrazy RP zawierają identyfikator platformy docelowej, offset
  programu, generację,
  wersję, SHA-256 ładunku, HMAC-SHA256 oraz CRC nagłówka. Klucz HMAC jest
  wyprowadzany z tego samego hasła aplikacji, które jest używane przez
  uwierzytelnianie transportu.
- Wbudowana pamięć flash RP jest podzielona na niezmienny region rozruchowy
  o rozmiarze 16 KiB, równe sloty `program` i `staging`, dziennik faz
  (`phase journal`), sektor roboczy (`scratch`), dwa nadmiarowe sektory stanu
  oraz istniejący obszar końcowy LittleFS/EEPROM.
- Kod aktualizujący przy rozruchu RP zamienia miejscami `program` i `staging`,
  sektor po sektorze. Monotoniczny dziennik faz pozwala wznowić działanie po
  utracie zasilania. Niepotwierdzona wersja próbna jest wycofywana po
  `HAL_RP_OTA_MAX_BOOT_ATTEMPTS` rozruchach.
- ESP32-S3 przyjmuje surowy plik BIN aplikacji ESP, sprawdza MD5 transferu
  oraz poprawność obrazu przez ESP-IDF, po czym zapisuje nieaktywną partycję
  aplikacji OTA za pomocą `esp_ota_*`, wybiera ją do rozruchu i restartuje
  urządzenie. Wygenerowane ustawienia domyślne wybierają `two-ota-large`
  z włączonym rollbackiem aplikacji ESP-IDF. Status rozruchu odwzorowuje
  bieżącą partycję, partycję rozruchową i stany obrazu ESP OTA na publiczne
  tryby: stabilny, oczekujący, próbny, rollback oraz odzyskiwanie.
- Wywołuj `hal_ota_confirm_boot_ex()` dopiero po przejściu testów
  samokontrolnych aplikacji. Na ESP32-S3 wywołuje to
  `esp_ota_mark_app_valid_cancel_rollback()`. Wywołanie tej funkcji w stanie
  stabilnym jest nieszkodliwe.

- **impl/rp2040:** implementacja obszaru `staging` i kodu aktualizującego przy
  rozruchu dla RP2040 i RP2350.
- **impl/esp32:** natywne partycje OTA ESP-IDF i surowe obrazy aplikacji. Hasło
  AUTH2 jest opcjonalne na poziomie API urządzenia; wdrożone systemy muszą
  skonfigurować niepusty sekret oraz zastosować zasady bezpiecznego rozruchu
  i szyfrowania pamięci flash ESP-IDF odpowiednie do swojego modelu zagrożeń.
- **impl/.mock:** deterministyczna implementacja testowa z możliwością
  wstrzykiwania zdarzeń.

**Thread safety:** Backendy rodziny RP i ESP32-S3 umożliwiają
bezpieczne korzystanie z publicznego API z wielu wątków i rdzeni. Jeden
`hal_mutex_t` serializuje wszystkie wywołania warstwy wspólnej, a callbacki
są wywoływane poza tą blokadą. Mutex jest przydzielany przy pierwszym użyciu,
a błąd przydziału jest obsługiwany zgodnie z zasadą fail-closed:
funkcje zwracające `bool` zwracają `false`, funkcje statusowe zwracają
`HAL_ENOMEM`, a funkcja obsługi kończy działanie bez zmieniania stanu.

**Pomocnicy mock:**
```c
void        hal_mock_ota_reset(void);
void        hal_mock_ota_set_begin_result(bool result);
void        hal_mock_ota_inject_start(hal_ota_command_t command);
void        hal_mock_ota_inject_end(void);
void        hal_mock_ota_inject_progress(uint32_t progress, uint32_t total);
void        hal_mock_ota_inject_error(hal_ota_error_t error);
uint16_t    hal_mock_ota_get_port(void);
const char *hal_mock_ota_get_hostname(void);
const char *hal_mock_ota_get_password(void);
uint32_t    hal_mock_ota_get_handle_count(void);
```

Wymagania właściwe dla danej platformy, dotyczące projektu, firmware'u,
VS Code, zapory sieciowej, potwierdzania aktualizacji, rollbacku, odzyskiwania
i bezpieczeństwa, opisano w [procedurze natywnej aktualizacji OTA](../../pl/OTAWorkflow.md).
Przykładowa aplikacja RP jest dostępna w
[`examples/25_ota`](../../../examples/25_ota/).

---

## `hal_udp` - datagramy UDP  *(opt-in - `HAL_ENABLE_UDP`)*

API transportu UDP oparte na uchwytach, przeznaczone do obsługi niezależnych
gniazd datagramowych. Pierwotne API `hal_udp_*` dla jednego gniazda pozostaje
dostępne jako warstwa zgodności korzystająca z domyślnego uchwytu UDP.

```c
#include <hal/network/hal_udp.h>

#define HAL_UDP_IP_STR_LEN 16u

typedef struct hal_udp_socket_impl_t *hal_udp_socket_t;

hal_udp_socket_t hal_udp_socket_open(void);
bool hal_udp_socket_bind(hal_udp_socket_t socket,
                         const hal_net_endpoint_t *local);
int  hal_udp_socket_sendto(hal_udp_socket_t socket,
                           const void *data,
                           size_t len,
                           const hal_net_endpoint_t *remote);
int  hal_udp_socket_recvfrom(hal_udp_socket_t socket,
                             void *buffer,
                             size_t max_len,
                             hal_net_endpoint_t *remote,
                             uint32_t timeout_ms);
bool hal_udp_socket_can_recv(hal_udp_socket_t socket);
bool hal_udp_socket_can_send(hal_udp_socket_t socket);
void hal_udp_socket_close(hal_udp_socket_t socket);

bool hal_udp_begin(uint16_t local_port);
void hal_udp_stop(void);

int  hal_udp_parse_packet(void);
int  hal_udp_read(uint8_t *buffer, uint16_t max_len);

bool     hal_udp_remote_ip(char *out, size_t out_size);
uint16_t hal_udp_remote_port(void);

bool     hal_udp_begin_packet(const char *host_or_ip, uint16_t remote_port);
bool     hal_udp_begin_packet_remote(void);
uint16_t hal_udp_write(const uint8_t *data, uint16_t len);
uint16_t hal_udp_write_str(const char *text);
bool     hal_udp_end_packet(void);
```

**Uwagi dotyczące zachowania:**
- Moduł jest dostępny wyłącznie, gdy zdefiniowano `HAL_ENABLE_UDP`.
- `hal_udp_socket_open()` alokuje gniazdo z puli mieszczącej
  `HAL_UDP_SOCKET_MAX_INSTANCES` instancji; zamykaj nieużywane gniazda przez
  `hal_udp_socket_close()`.
- `hal_udp_socket_bind(...)` wiąże lokalny punkt końcowy IPv4. Rodzina
  adresów musi być `HAL_NET_AF_INET`, a port musi być niezerowy.
- `hal_udp_socket_sendto(...)` wysyła jeden datagram do punktu końcowego
  IPv4 i zwraca liczbę zaakceptowanych bajtów lub `<0` w przypadku błędu.
- `hal_udp_socket_recvfrom(...)` odbiera datagram z gniazda związanego
  z lokalnym punktem końcowym.
  `timeout_ms == 0` oznacza natychmiastową próbę odbioru, a
  `HAL_NET_TIMEOUT_FOREVER` wybiera oczekiwanie blokujące.
- `hal_udp_socket_can_recv(...)` i `hal_udp_socket_can_send(...)` sprawdzają
  gotowość bez pobierania danych. Są przeznaczone między innymi dla warstw
  zgodności takich jak BSD `select()`.
- `hal_udp_begin(...)` otwiera i wiąże domyślne gniazdo UDP starszego API.
- `hal_udp_parse_packet()` zwraca rozmiar pakietu, `0` gdy żaden pakiet nie
  jest dostępny.
- `hal_udp_remote_ip(...)` i `hal_udp_remote_port()` zwracają punkt końcowy
  nadawcy zapamiętany podczas ostatniego udanego `hal_udp_parse_packet()`.
- `hal_udp_begin_packet_remote()` wysyła datagram odpowiedzi do tego
  zapamiętanego nadawcy.
- `hal_udp_write(...)` / `hal_udp_write_str(...)` dopisują bajty ładunku do
  datagramu otwartego przez `hal_udp_begin_packet*()`.
- `hal_udp_stop()` usuwa zapamiętany zdalny punkt końcowy oraz aktywny kontekst
  wysyłania pakietu.
- Gdy `hal_wireguard` jest aktywny, datagramy do celów objętych
  trasą/AllowedIPs WireGuard są przenoszone przez zaszyfrowany tunel.

- **impl/rp2040:** opracowany w JaszczurHAL silnik UDP korzystający z surowego
  API lwIP i statycznej puli gniazd.
- **impl/esp32:** ograniczona pula uchwytów HAL nad natywnymi gniazdami UDP
  lwIP z ESP-IDF oraz obsługa gotowości i timeoutów przez `select()`.
- **impl/.mock:** deterministyczna implementacja testowa obsługująca wiele
  gniazd, wstrzykiwane pakiety przychodzące oraz rejestrowanie metadanych
  i zawartości pakietów wychodzących.

**Thread safety:** Backendy rodziny RP i ESP32-S3 pozwalają
bezpiecznie korzystać z publicznego API z wielu wątków i rdzeni. Mutexy
poszczególnych backendów chronią ich statyczne pule UDP oraz operacje stosu.

**Pomocnicy mock:**
```c
void        hal_mock_udp_reset(void);
void        hal_mock_udp_inject_packet(const char *remote_ip,
                                       uint16_t remote_port,
                                       const uint8_t *payload,
                                       uint16_t len);
void        hal_mock_udp_inject_packet_to(hal_udp_socket_t socket,
                                          const char *remote_ip,
                                          uint16_t remote_port,
                                          const uint8_t *payload,
                                          uint16_t len);
void        hal_mock_udp_set_end_packet_result(bool result);
void        hal_mock_udp_set_end_packet_result_for(hal_udp_socket_t socket,
                                                   bool result);
uint16_t    hal_mock_udp_get_local_port(void);
uint16_t    hal_mock_udp_get_local_port_for(hal_udp_socket_t socket);
const char *hal_mock_udp_get_last_begin_packet_host(void);
uint16_t    hal_mock_udp_get_last_begin_packet_port(void);
const uint8_t *hal_mock_udp_get_last_tx_payload(void);
const uint8_t *hal_mock_udp_get_last_tx_payload_for(hal_udp_socket_t socket);
uint16_t    hal_mock_udp_get_last_tx_len(void);
uint16_t    hal_mock_udp_get_last_tx_len_for(hal_udp_socket_t socket);
bool        hal_mock_udp_get_last_tx_remote_for(hal_udp_socket_t socket,
                                                hal_net_endpoint_t *out);
bool        hal_mock_udp_was_end_packet_called(void);
```

---

## `hal_tcp` - gniazda i nasłuchiwacze TCP  *(opt-in - `HAL_ENABLE_TCP`)*

API transportu TCP oparte na uchwytach. Obsługuje wychodzące połączenia
strumieniowe oraz gniazda nasłuchujące, które przyjmują połączenia przychodzące.

```c
#include <hal/network/hal_tcp.h>

typedef struct hal_tcp_socket_impl_t *hal_tcp_socket_t;
typedef struct hal_tcp_listener_impl_t *hal_tcp_listener_t;

hal_tcp_socket_t hal_tcp_socket_open(void);
bool hal_tcp_socket_connect(hal_tcp_socket_t socket,
                            const hal_net_endpoint_t *remote,
                            uint32_t timeout_ms);
int  hal_tcp_socket_send(hal_tcp_socket_t socket,
                         const void *data,
                         size_t len);
int  hal_tcp_socket_recv(hal_tcp_socket_t socket,
                         void *buffer,
                         size_t max_len,
                         uint32_t timeout_ms);
bool hal_tcp_socket_can_recv(hal_tcp_socket_t socket);
bool hal_tcp_socket_can_send(hal_tcp_socket_t socket);
bool hal_tcp_socket_is_connected(hal_tcp_socket_t socket);
void hal_tcp_socket_shutdown(hal_tcp_socket_t socket);
void hal_tcp_socket_close(hal_tcp_socket_t socket);

hal_tcp_listener_t hal_tcp_listener_open(void);
bool hal_tcp_listener_bind(hal_tcp_listener_t listener,
                           const hal_net_endpoint_t *local);
bool hal_tcp_listener_listen(hal_tcp_listener_t listener, uint8_t backlog);
hal_tcp_socket_t hal_tcp_listener_accept(hal_tcp_listener_t listener,
                                         hal_net_endpoint_t *remote,
                                         uint32_t timeout_ms);
bool hal_tcp_listener_can_accept(hal_tcp_listener_t listener);
void hal_tcp_listener_close(hal_tcp_listener_t listener);
```

**Uwagi dotyczące zachowania:**
- Moduł jest dostępny wyłącznie, gdy zdefiniowano `HAL_ENABLE_TCP`.
- `hal_tcp_socket_open()` alokuje gniazdo klienckie z puli mieszczącej
  `HAL_TCP_SOCKET_MAX_INSTANCES` instancji; zamykaj nieużywane gniazda przez
  `hal_tcp_socket_close()`.
- `hal_tcp_socket_connect(...)` łączy się z punktem końcowym IPv4. Rodzina
  adresów musi być `HAL_NET_AF_INET`, a port musi być niezerowy.
- `timeout_ms == 0` oznacza natychmiastową, nieblokującą próbę odbioru.
  `HAL_NET_TIMEOUT_FOREVER` wybiera odbiór blokujący bez ustalonego limitu czasu.
- `hal_tcp_socket_send(...)` zwraca liczbę zaakceptowanych bajtów lub `<0`
  w przypadku błędu.
- `hal_tcp_socket_recv(...)` zwraca liczbę odczytanych bajtów, `0` po upływie
  timeoutu, przy braku danych lub po zamknięciu połączenia przez peera, albo
  wartość `<0` dla nieprawidłowego uchwytu lub argumentu.
- `hal_tcp_socket_can_recv(...)` i `hal_tcp_socket_can_send(...)` sprawdzają
  gotowość bez pobierania danych. Są przeznaczone między innymi dla warstw
  zgodności takich jak BSD `select()`.
- `hal_tcp_socket_shutdown(...)` zatrzymuje we/wy, ale pozostawia uchwyt
  zaalokowany.
- `hal_tcp_socket_close(...)` zatrzymuje klienta backendu i zwraca uchwyt do
  puli statycznej.
- `hal_tcp_listener_open()` alokuje nasłuchiwacz z puli mieszczącej
  `HAL_TCP_LISTENER_MAX_INSTANCES` instancji; zamykaj nieużywane nasłuchiwacze przez
  `hal_tcp_listener_close()`.
- `hal_tcp_listener_bind(...)` wiąże lokalny punkt końcowy IPv4. Rodzina
  adresów musi być `HAL_NET_AF_INET`, a port musi być niezerowy.
- `hal_tcp_listener_listen(...)` rozpoczyna przyjmowanie klientów i wymaga
  niezerowego parametru `backlog`. Przenośna implementacja testowa ogranicza
  liczbę oczekujących klientów do `HAL_TCP_LISTENER_BACKLOG_MAX`; rzeczywiste
  backendy mogą stosować własne limity platformy.
- `hal_tcp_listener_accept(...)` zwraca połączony `hal_tcp_socket_t` ze
  zwykłej puli gniazd TCP. `timeout_ms == 0` odpytuje natychmiast, a
  `HAL_NET_TIMEOUT_FOREVER` żąda blokującego oczekiwania.
- `hal_tcp_listener_can_accept(...)` sprawdza, czy oczekuje klient, ale nie
  pobiera jeszcze zaakceptowanego gniazda.
- `hal_tcp_listener_close(...)` zatrzymuje wyłącznie nasłuchiwacz. Już
  zaakceptowane gniazda klientów pozostają niezależne i muszą być zamknięte
  osobno.
- Gdy `hal_wireguard` jest aktywny, połączenia do celów objętych
  trasą/AllowedIPs WireGuard są przenoszone przez zaszyfrowany tunel.

- **impl/rp2040:** opracowany w JaszczurHAL silnik TCP korzystający z surowego
  API lwIP oraz statycznych pul gniazd i nasłuchiwaczy.
- **impl/esp32:** ograniczona pula uchwytów HAL nad natywnymi gniazdami TCP
  lwIP z ESP-IDF. Obejmuje połączenie z timeoutem, operacje
  `bind`/`listen`/`accept`, zamykanie oraz sprawdzanie gotowości przez `select()`.
- **impl/.mock:** deterministyczna implementacja testowa klienta
  i nasłuchiwacza. Pozwala ustawić wynik połączenia, wstrzykiwać bajty RX,
  rejestrować zawartość TX i zdalny punkt końcowy oraz utrzymuje osobną kolejkę
  oczekujących klientów dla każdego nasłuchiwacza.

**Thread safety:** Backendy rodziny RP i ESP32-S3 pozwalają
bezpiecznie korzystać z publicznego API z wielu wątków i rdzeni. Mutexy
poszczególnych backendów chronią ich statyczne pule TCP oraz operacje stosu.

**Pomocnicy mock:**
```c
void        hal_mock_tcp_reset(void);
void        hal_mock_tcp_set_connect_result(bool result);
void        hal_mock_tcp_inject_rx(hal_tcp_socket_t socket,
                                   const uint8_t *payload,
                                   uint16_t len);
void        hal_mock_tcp_set_next_rx(const uint8_t *payload, uint16_t len);
bool        hal_mock_tcp_queue_next_rx(const uint8_t *payload, uint16_t len);
const uint8_t *hal_mock_tcp_get_last_tx_payload(hal_tcp_socket_t socket);
uint16_t    hal_mock_tcp_get_last_tx_len(hal_tcp_socket_t socket);
bool        hal_mock_tcp_get_remote_endpoint(hal_tcp_socket_t socket,
                                             hal_net_endpoint_t *out);
bool        hal_mock_tcp_listener_inject_client(hal_tcp_listener_t listener,
                                                const hal_net_endpoint_t *remote);
uint16_t    hal_mock_tcp_listener_get_local_port(hal_tcp_listener_t listener);
uint8_t     hal_mock_tcp_listener_get_backlog(hal_tcp_listener_t listener);
uint8_t     hal_mock_tcp_listener_get_pending_count(hal_tcp_listener_t listener);
```

---

## `hal_tls` - klient TLS  *(opt-in - `HAL_ENABLE_TLS`)*

`hal_tls` udostępnia wspólne API klienta TLS, niezależne od backendu
i zabezpieczone licznikami generacji uchwytów. Korzysta z dołączonego silnika
BearSSL. Włączenie modułu automatycznie włącza TCP i WiFi, ale nie włącza ani
nie wymaga opcjonalnego adaptera gniazd BSD.

```c
#include <hal/network/tls/hal_tls.h>

hal_tls_client_config_t config;
hal_tls_client_t client = NULL;
hal_tls_trust_anchor_storage_t ca_storage;

hal_tls_trust_anchor_from_der_ex(ca_der, ca_der_length, &ca_storage);

hal_tls_security_config_t security = {
    .trust_anchors = &ca_storage.anchor,
    .trust_anchor_count = 1u,
    .get_time = hal_tls_default_time,
    .get_entropy = hal_tls_default_entropy,
};

hal_tls_client_config_init(&config);
hal_tls_client_create_ex(&config, &client);
hal_tls_client_configure_server_ex(client, "example.com", 443u);
hal_tls_client_configure_security_ex(client, &security);
hal_tls_client_connect_ex(client);

while (hal_tls_client_poll_ex(client) == HAL_EAGAIN) {
  hal_net_service();
}

hal_tls_client_write_ex(client, request, request_length, &written);
hal_tls_client_read_ex(client, response, sizeof(response), &received);
hal_tls_client_shutdown_ex(client);
hal_tls_client_close_ex(client);
```

`hal_tls_client_config_init()` wybiera tryb pracy oparty na odpytywaniu,
5-sekundowy timeout transportu, 15-sekundowy timeout operacji oraz cztery
kroki backendu na każde wywołanie funkcji odpytywania. Aplikacja może wybrać
`HAL_TLS_EXECUTION_BOUNDED_WORKER`, jeśli wszystkie blokujące wywołania
z ograniczonym czasem wykonuje dedykowane zadanie. Oba timeouty muszą mieć
niezerową, skończoną wartość.

Konfiguracja bezpieczeństwa wymaga co najmniej jednej kotwicy zaufania RSA
lub EC oraz callbacków czasu i entropii. `hal_tls_trust_anchor_from_der_ex()`
dekoduje certyfikat CA w formacie DER do pamięci o stałym rozmiarze
dostarczonej przez wywołującego. Wszystkie wskazywane bufory zaufania muszą
pozostać ważne aż do zamknięcia klienta. `hal_tls_default_time()` wymaga
wiarygodnego, zsynchronizowanego zegara, a `hal_tls_default_entropy()` korzysta
z bezpiecznego źródła entropii wybranego dla danej platformy.

BearSSL otrzymuje skonfigurowaną nazwę hosta do SNI i weryfikacji
tożsamości certyfikatu. Waliduje łańcuch, okres ważności certyfikatu oraz
nazwę hosta. Opcjonalne `server_public_key_sha256` dodaje pinning klucza
publicznego SHA-256 po walidacji certyfikatu. Callbacki anulowania i obsługi
pozwalają kooperacyjnie przerywać długie operacje, jednocześnie kontynuując
obsługę sieci i watchdoga. Uchwyty klienta są zabezpieczone licznikami
generacji. Zamknięcie zwalnia miejsce w puli, a wcześniejsze kopie uchwytu
pozostają nieważne.

Podstawowa ścieżka TLS rozwiązuje adresy przez `hal_net_resolve_ex()`
i zarządza cyklem życia natywnego `hal_tcp_socket_t`. Rekordy BearSSL są
przetwarzane przez niewielki, prywatny interfejs transportowy zamiast
deskryptorów POSIX. Dzięki temu TLS działa również przy wyłączonym
`HAL_ENABLE_BSD_SOCKETS`.

Gniazda BSD mogą niezależnie służyć jako transport TLS. Gdy obie flagi są
włączone, adapter BSD dla BearSSL odwzorowuje nieblokujące operacje
`send()`/`recv()` istniejącego deskryptora na ten sam prywatny interfejs
transportowy BearSSL. Aplikacje i zewnętrzni klienci TLS korzystający
z operacji wejścia/wyjścia BSD nadal działają przez publiczne API gniazd.
Włączenie natywnego `hal_tls` nie zmienia zasad zarządzania deskryptorem ani
semantyki BSD.

**Implementacja:**

- `hal_tls.cpp` odpowiada za cykl życia, rozwiązywanie DNS, natywny transport
  HAL TCP oraz konfigurację zabezpieczeń niezależną od backendu;
- `hal/network/tls/BearSSL/jh_bearssl_hal_tcp_io.*` adaptuje HAL TCP;
- `hal/network/tls/BearSSL/jh_bearssl_bsd_io.*` to opcjonalny most
  TLS-nad-BSD;
- `hal/network/tls/BearSSL/jh_bearssl_engine.*` przetwarza rekordy przez
  dowolny z transportów, nie zależąc od żadnej z reprezentacji gniazd.

**Testy:** `test_hal_tls` obejmuje publiczny cykl życia, a
`test_bearssl_provider` sprawdza zachowanie silnika niezależnie od transportu
natywnego oraz operacje wejścia/wyjścia TLS przez BSD. Testy konfiguracji
weryfikują niezależny wybór TLS i BSD.
`tests/run_bearssl_native_integration.sh` tworzy tymczasowe CA RSA oraz
certyfikat serwera `localhost` z nazwami SAN DNS/IP, uruchamia serwer OpenSSL
na interfejsie pętli zwrotnej i sprawdza poprawne połączenie oraz przypadki
błędnej nazwy hosta, certyfikatu jeszcze nieważnego i certyfikatu wygasłego.
Wygenerowane klucze prywatne i certyfikaty są usuwane po zakończeniu testu.

---

## Adapter gniazd BSD  *(opt-in - `HAL_ENABLE_BSD_SOCKETS`)*

Włączenie tego modułu automatycznie włącza UDP, TCP i WiFi. Konfiguracje CYW43
oraz implementacja testowa korzystają z opisanej niżej minimalnej warstwy
zgodności IPv4 BSD/POSIX nad `hal_udp` i `hal_tcp`. ESP32-S3 udostępnia
natywne API BSD dostarczane przez lwIP z ESP-IDF. Wspólny adapter celowo nie
definiuje na tej platformie konkurencyjnych symboli gniazd.

```c
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <netdb.h>
#include <fcntl.h>
#include <sys/select.h>
#include <unistd.h>

int socket(int domain, int type, int protocol);
int bind(int sockfd, const struct sockaddr *addr, socklen_t addrlen);
int listen(int sockfd, int backlog);
int accept(int sockfd, struct sockaddr *addr, socklen_t *addrlen);
int connect(int sockfd, const struct sockaddr *addr, socklen_t addrlen);
ssize_t send(int sockfd, const void *buf, size_t len, int flags);
ssize_t recv(int sockfd, void *buf, size_t len, int flags);
ssize_t sendto(int sockfd, const void *buf, size_t len, int flags,
               const struct sockaddr *dest_addr, socklen_t addrlen);
ssize_t recvfrom(int sockfd, void *buf, size_t len, int flags,
                 struct sockaddr *src_addr, socklen_t *addrlen);
int setsockopt(int sockfd, int level, int optname,
               const void *optval, socklen_t optlen);
int getsockopt(int sockfd, int level, int optname,
               void *optval, socklen_t *optlen);
int getsockname(int sockfd, struct sockaddr *addr, socklen_t *addrlen);
int getpeername(int sockfd, struct sockaddr *addr, socklen_t *addrlen);
int shutdown(int sockfd, int how);
int fcntl(int fd, int cmd, ...);
int select(int nfds, fd_set *readfds, fd_set *writefds, fd_set *exceptfds,
           struct timeval *timeout);
int close(int fd);
ssize_t read(int fd, void *buf, size_t count);
ssize_t write(int fd, const void *buf, size_t count);

uint16_t htons(uint16_t hostshort);
uint16_t ntohs(uint16_t netshort);
uint32_t htonl(uint32_t hostlong);
uint32_t ntohl(uint32_t netlong);
in_addr_t inet_addr(const char *cp);
int inet_pton(int af, const char *src, void *dst);
const char *inet_ntop(int af, const void *src, char *dst, socklen_t size);

struct addrinfo;
int getaddrinfo(const char *node, const char *service,
                const struct addrinfo *hints, struct addrinfo **res);
void freeaddrinfo(struct addrinfo *res);
const char *gai_strerror(int errcode);
```

**Zakres MVP wspólnego adaptera:** `AF_INET`, `SOCK_STREAM`, `SOCK_DGRAM`,
protokoły TCP i UDP, `sockaddr_in`, funkcje pomocnicze kolejności bajtów,
konwersja adresów IPv4 między postacią tekstową i binarną oraz funkcja
`getaddrinfo()` dla IPv4, zwracająca jeden wynik. Wartości deskryptorów zaczynają
się od `HAL_BSD_SOCKET_FD_BASE` i są przechowywane w tabeli o rozmiarze
`HAL_BSD_SOCKET_MAX_FDS`.

**Uwagi dotyczące zachowania:**
- `socket(AF_INET, SOCK_DGRAM, 0/IPPROTO_UDP)` odwzorowuje się na
  `hal_udp_socket_open()`.
- `socket(AF_INET, SOCK_STREAM, 0/IPPROTO_TCP)` odwzorowuje się na
  `hal_tcp_socket_open()`.
- Adapter korzysta ze standardowego trasowania `hal_udp`/`hal_tcp`. Gdy
  `hal_wireguard` jest aktywny, ruch do celów objętych trasą/AllowedIPs
  WireGuard jest przenoszony przez zaszyfrowany tunel. Jest to tunelowanie
  warstwy sieciowej i nie zastępuje gniazd TLS, które zapewniają szyfrowanie
  end-to-end w warstwie aplikacji lub sesji.
- Gniazda BSD mogą być użyte jako transport dla bibliotek TLS. Dołączony
  adapter BearSSL BSD pozostaje dostępny, gdy wybrano zarówno
  `HAL_ENABLE_BSD_SOCKETS`, jak i `HAL_ENABLE_TLS`; natywne API `hal_tls`
  używa bezpośrednio HAL TCP, dlatego nie włącza gniazd BSD w niezależnych
  konfiguracjach.
- UDP `sendto()` automatycznie wiąże gniazdo z efemerycznym portem lokalnym,
  gdy gniazdo nie zostało jawnie związane.
- UDP `connect()` zapisuje domyślny punkt końcowy peera i w razie potrzeby
  automatycznie wiąże gniazdo. Następnie `send()`/`write()` przesyłają
  datagramy do tego peera, natomiast `recv()`/`read()` odbierają datagramy
  bez zwracania adresu źródłowego. W przeciwieństwie do gniazda UDP połączonego
  przez `connect()` w POSIX adapter nie filtruje przychodzących datagramów
  według tego peera;
  akceptuje kolejny datagram dostarczony przez gniazdo HAL UDP.
- TCP `bind()` zapamiętuje lokalny punkt końcowy, a `listen()` przekształca
  deskryptor w nasłuchiwacz HAL TCP. Zaakceptowani klienci otrzymują osobne
  deskryptory gniazd.
- `getaddrinfo(...)` rozwiązuje dosłowne adresy IPv4 lub nazwy hostów przez
  `hal_net_resolve_ipv4(...)`. Parametr `service` musi mieć postać liczbową.
  Obsługiwane flagi w `hints` to `AI_PASSIVE`, `AI_CANONNAME`, `AI_NUMERICHOST`,
  `AI_NUMERICSERV` i `AI_ADDRCONFIG`; IPv6 pozostaje poza zakresem adaptera.
- `setsockopt(...)` przyjmuje `SOL_SOCKET` + `SO_REUSEADDR`/`SO_REUSEPORT`,
  `SO_RCVTIMEO` i `SO_SNDTIMEO`. `getsockopt(...)` zgłasza te wartości oraz
  `SO_ERROR`; odczyt `SO_ERROR` kasuje zapamiętany błąd adaptera. Timeouty są
  przechowywane z rozdzielczością milisekundową, dlatego wartości `timeval`
  krótsze niż milisekunda mogą po odczycie zostać zaokrąglone w górę.
- `getsockname(...)` zwraca lokalny punkt końcowy znany adapterowi.
  Klienci TCP, którzy nie wykonali jawnego `bind()`, mogą zgłaszać
  `0.0.0.0:0`, ponieważ API HAL TCP nie ujawnia lokalnego portu przypisanego
  przez backend.
- `getpeername(...)` zwraca połączonego peera TCP lub UDP, również dla gniazd
  TCP uzyskanych z `accept()`. Zanim peer będzie znany, funkcja zgłasza
  `ENOTCONN`.
- Wywołania blokujące domyślnie używają `HAL_NET_TIMEOUT_FOREVER`.
  `SO_RCVTIMEO` wpływa na `accept()`, `recv()`/`read()` i `recvfrom()`;
  `SO_SNDTIMEO` wpływa na wybór timeoutu dla `connect()`.
  `fcntl(F_SETFL, O_NONBLOCK)` sprawia, że `accept()`, `connect()`,
  `recv()`/`read()` i `recvfrom()` używają natychmiastowych odpytań HAL;
  `MSG_DONTWAIT` wybiera ten tryb dla pojedynczego wywołania `recv`,
  `recvfrom`, `send` lub `sendto`.
- Minimalny `select()` obsługuje gotowość do odczytu i zapisu dla deskryptorów
  gniazd HAL. Zbiór `exceptfds` jest przyjmowany, ale zawsze czyszczony;
  `poll()` pozostaje poza zakresem tego etapu.
- Nieblokujący `connect()` TCP zapewnia jedynie zachowanie best-effort, a nie
  pełny automat stanów oczekującego połączenia POSIX. Adapter wykonuje jedną
  natychmiastową próbę połączenia przez HAL. Po jej powodzeniu deskryptor
  staje się gotowy do zapisu, a `SO_ERROR` ma wartość zero. Jeśli próba nie
  zakończy się od razu, `connect()` zwraca `-1`/`EINPROGRESS` i zapisuje
  `EINPROGRESS` w `SO_ERROR`, lecz w tle nie pozostaje oczekujące połączenie.
  Wywołaj `connect()` ponownie później albo użyj wariantu blokującego lub
  ograniczonego timeoutem.
- Zamknięcie deskryptora z innego zadania, gdy blokujące `connect()`,
  `accept()`, `recv()` lub `recvfrom()` oczekuje, nie zapewnia
  asynchronicznego anulowania. Adapter zwalnia blokadę tabeli deskryptorów
  podczas oczekiwania i ponownie waliduje deskryptory po powrocie wywołania
  backendu, ale wywołujący potrzebujący anulowalnych oczekiwań powinni
  używać `O_NONBLOCK` wraz z odpytywaniem `select()`.
- Nieobsługiwane flagi i operacje kończą się błędem oraz ustawieniem `errno`.

- **Wspólna implementacja modułu:** `hal/network/adapters/bsd/hal_bsd_sockets.cpp`
  zawiera obsługę tabeli deskryptorów, funkcje konwersji adresów oraz obsługę
  resolvera `netdb.h`.
- **impl/esp32:** natywne nagłówki i symbole BSD lwIP z ESP-IDF; zachowanie
  deskryptorów i opcji wynika z używanej wersji konfiguracji ESP-IDF, a nie
  ze stałej tabeli deskryptorów wspólnego adaptera.
- **Testy impl/.mock:** `test_bsd_sockets` sprawdza zachowanie i mapowanie
  `errno`; `test_bsd_sockets_c_compile` weryfikuje, czy proste klienty
  i serwery TCP/UDP napisane w C oraz wywołania `getaddrinfo()` i
  `setsockopt()` kompilują się i linkują z nagłówkami warstwy zgodności.

---

## `hal_wireguard` - obsługa tunelu WireGuard  *(opt-in - `HAL_ENABLE_WIREGUARD`)*

Wspólne API nad silnikiem WireGuard/lwIP, przystosowane do pracy
wielowątkowej.

```c
#include <hal/network/wireguard/hal_wireguard.h>

#define HAL_WIREGUARD_IPV4_OCTETS 4u
#define HAL_WIREGUARD_IP_STR_LEN 16u

bool hal_wireguard_parse_ipv4(const char *ip_text,
                              uint8_t out_ip[HAL_WIREGUARD_IPV4_OCTETS]);

bool hal_wireguard_begin(const uint8_t local_ip[HAL_WIREGUARD_IPV4_OCTETS],
                         const char *private_key,
                         const char *remote_peer_address,
                         const char *remote_peer_public_key,
                         uint16_t remote_peer_port);

bool hal_wireguard_begin_text(const char *local_ip_text,
                              const char *private_key,
                              const char *remote_peer_address,
                              const char *remote_peer_public_key,
                              uint16_t remote_peer_port);

bool hal_wireguard_begin_advanced(const uint8_t local_ip[HAL_WIREGUARD_IPV4_OCTETS],
                                  const char *private_key,
                                  const char *remote_peer_address,
                                  const char *remote_peer_public_key,
                                  uint16_t remote_peer_port,
                                  const uint8_t allowed_ip[HAL_WIREGUARD_IPV4_OCTETS],
                                  const uint8_t allowed_mask[HAL_WIREGUARD_IPV4_OCTETS]);

bool hal_wireguard_begin_advanced_text(const char *local_ip_text,
                                       const char *private_key,
                                       const char *remote_peer_address,
                                       const char *remote_peer_public_key,
                                       uint16_t remote_peer_port,
                                       const char *allowed_ip_text,
                                       const char *allowed_mask_text);

void hal_wireguard_end(void);
bool hal_wireguard_is_initialized(void);

bool hal_wireguard_peer_up(char *endpoint_ip_out,
                           size_t endpoint_ip_out_size,
                           uint16_t *endpoint_port_out);

bool hal_wireguard_peer_up_quick(void);

bool hal_wireguard_kick_handshake(const uint8_t probe_ip[HAL_WIREGUARD_IPV4_OCTETS],
                                  uint16_t probe_port,
                                  uint32_t min_interval_ms);

bool hal_wireguard_kick_handshake_text(const char *probe_ip_text,
                                       uint16_t probe_port,
                                       uint32_t min_interval_ms);
```

**Uwagi dotyczące zachowania:**
- Moduł jest dostępny wyłącznie, gdy zdefiniowano `HAL_ENABLE_WIREGUARD`.
- `hal_wireguard_parse_ipv4(...)` sprawdza poprawność tekstowego adresu IPv4
  w zapisie kropkowym (`a.b.c.d`) i przekształca go na oktety.
- `hal_wireguard_begin(...)` używa trybu pełnego tunelu (full-tunnel)
  (`AllowedIPs = 0.0.0.0/0`).
- `hal_wireguard_begin_text(...)` analizuje lokalny adres IP w zapisie
  kropkowym i wywołuje `hal_wireguard_begin(...)`.
- `hal_wireguard_begin_advanced(...)` włącza tryb podzielonego tunelu
  (split-tunnel) przez jawne AllowedIPs.
- `hal_wireguard_begin_advanced_text(...)` analizuje adresy IPv4
  `local`/`allowed`/`mask` w zapisie kropkowym i wywołuje
  `hal_wireguard_begin_advanced(...)`.
- `hal_wireguard_peer_up(...)` może opcjonalnie zwrócić bieżący adres IP
  i port punktu końcowego.
- `hal_wireguard_peer_up_quick(...)` to uproszczone sprawdzenie bez argumentów,
  równoważne `hal_wireguard_peer_up(NULL, 0u, NULL)`.
- `hal_wireguard_kick_handshake(...)` wysyła nieblokującą sondę inicjującą
  uzgadnianie.
- `hal_wireguard_kick_handshake_text(...)` analizuje tekstowy adres IP sondy
  w zapisie kropkowym i wywołuje `hal_wireguard_kick_handshake(...)`.

- **Wspólna implementacja modułu:** dołączony silnik protokołu
  i kryptografii wraz z prywatnym portem rozszerzenia lwIP. Korzystają z niego
  backendy stosu sieciowego, które deklarują tę funkcję.
- **impl/rp2040:** rozszerzenie lwIP zarządzane przez HAL oraz bezpieczne
  hooki platformowe.
- **impl/stm32g474:** wspólna bazowa warstwa sieciowa CYW43/lwIP, entropia ze
  sprzętowego RNG oraz czas NTP zsynchronizowany z HAL.
- **impl/esp32:** wspólny silnik WireGuard nad natywną warstwą lwIP z ESP-IDF.
  Zapewnia jawne blokowanie stosu i dostęp do `netif`, natywny resolver,
  bezpieczną entropię ESP oraz zsynchronizowany czas libc dla uzgadniania
  TAI64N.
- **impl/.mock:** deterministyczna implementacja testowa z zachowywaniem
  stanu. Rejestruje konfigurację, pozwala wstrzyknąć punkt końcowy peera
  i sprawdzić wyzwolenie uzgadniania.

**Thread safety:** Jeden `hal_mutex_t` serializuje wszystkie
publiczne wywołania warstwy wspólnej, a wybrany backend serializuje dostęp do
prywatnego stosu lwIP.

**Pomocnicy mock:**
```c
void        hal_mock_wireguard_reset(void);
void        hal_mock_wireguard_set_begin_result(bool result);
void        hal_mock_wireguard_set_peer_up_result(bool result);
void        hal_mock_wireguard_set_kick_result(bool result);
void        hal_mock_wireguard_set_initialized(bool initialized);
void        hal_mock_wireguard_set_peer_endpoint(const uint8_t ip[HAL_WIREGUARD_IPV4_OCTETS], uint16_t port);
uint32_t    hal_mock_wireguard_get_peer_up_quick_call_count(void);
const uint8_t *hal_mock_wireguard_get_last_local_ip(void);
const uint8_t *hal_mock_wireguard_get_last_allowed_ip(void);
const uint8_t *hal_mock_wireguard_get_last_allowed_mask(void);
const char *hal_mock_wireguard_get_last_remote_peer_address(void);
uint16_t    hal_mock_wireguard_get_last_remote_peer_port(void);
bool        hal_mock_wireguard_was_begin_advanced(void);
const uint8_t *hal_mock_wireguard_get_last_probe_ip(void);
uint16_t    hal_mock_wireguard_get_last_probe_port(void);
uint32_t    hal_mock_wireguard_get_last_probe_min_interval_ms(void);
```

---

## `hal_mqtt` - klient MQTT  *(opt-in - `HAL_ENABLE_MQTT`)*

Warstwa obsługi MQTT oparta na dołączonej bibliotece PubSubClient
i przystosowana do pracy wielowątkowej. Callbacki są wywoływane poza
wewnętrznym mutexem, co zapobiega deadlockom wynikającym z kolejności
blokad w handlerach użytkownika.

```c
#include <hal/network/mqtt/hal_mqtt.h>

typedef void (*hal_mqtt_message_callback_t)(const char *topic,
                                            const uint8_t *payload,
                                            uint16_t length,
                                            void *user);

hal_status_t hal_mqtt_set_server_ex(const char *host, uint16_t port);
hal_status_t hal_mqtt_connect_ex(const char *client_id);
#ifdef HAL_ENABLE_TLS
hal_status_t hal_mqtt_configure_tls_ex(
    const hal_tls_security_config_t *security);
hal_status_t hal_mqtt_disable_tls_ex(void);
#endif

bool hal_mqtt_set_server(const char *host, uint16_t port);
bool hal_mqtt_set_callback(hal_mqtt_message_callback_t callback, void *user);
bool hal_mqtt_set_keepalive(uint16_t keepalive_s);
bool hal_mqtt_set_socket_timeout(uint16_t timeout_s);
bool hal_mqtt_set_buffer_size(uint16_t size);
uint16_t hal_mqtt_get_buffer_size(void);

bool hal_mqtt_connect(const char *client_id);
bool hal_mqtt_connect_auth(const char *client_id, const char *user, const char *pass);
void hal_mqtt_disconnect(void);
bool hal_mqtt_connected(void);
int  hal_mqtt_state(void);

bool hal_mqtt_loop(void);
bool hal_mqtt_publish(const char *topic, const uint8_t *payload, uint16_t payload_len, bool retained);
bool hal_mqtt_publish_str(const char *topic, const char *payload, bool retained);
bool hal_mqtt_subscribe(const char *topic, uint8_t qos);
bool hal_mqtt_unsubscribe(const char *topic);
```

**Uwagi dotyczące zachowania:**
- Moduł jest dostępny wyłącznie, gdy zdefiniowano `HAL_ENABLE_MQTT`.
- Backend RP używa dołączonego `PubSubClient` przez adapter klienta HAL TCP.
- STM32G474 używa tego samego adaptera PubSubClient/HAL TCP, gdy
  skonfigurowany jest jego backend CYW43 gSPI.
- ESP32-S3 używa tego samego adaptera PubSubClient nad swoimi natywnymi
  gniazdami HAL TCP.
- Przy włączonym `HAL_ENABLE_TLS` wywołaj `hal_mqtt_configure_tls_ex()` przed
  połączeniem, aby użyć MQTTS. Kotwice zaufania i callbacki wskazywane przez
  konfigurację podlegają zasadom cyklu życia `hal_tls`. Ponowna konfiguracja
  zamyka bieżący transport. `hal_mqtt_disable_tls_ex()` również rozłącza
  klienta i sprawia, że kolejne połączenia używają zwykłego MQTT.
- MQTTS tworzy klienta TLS zabezpieczonego licznikiem generacji i pracującego
  w trybie `bounded-worker`. Timeout gniazda MQTT wyznacza terminy dla
  transportu TLS i operacji. Kod odpytuje stan nawiązywania połączenia aż do
  jego zakończenia, a kolejne operacje odczytu i zapisu korzystają z klienta TLS
  do chwili rozłączenia.
- `hal_mqtt_loop()` należy wywoływać regularnie, aby obsługiwać keepalive
  i odbierać przychodzące publikacje.
- Wiadomości przychodzące są kopiowane do wewnętrznego bufora i dostarczane
  z `hal_mqtt_loop()` po zwolnieniu wewnętrznego mutexu.

- **impl/rp2040/stm32g474/esp32:** dołączony `PubSubClient`
  (`frameworks/PubSubClient`) nad `hal_tcp` lub klientem BearSSL `hal_tls`.
- **impl/.mock:** deterministyczna implementacja testowa z zachowywaniem
  stanu. Pozwala wstrzyknąć wynik połączenia, wynik obsługi pętli oraz
  wiadomości przychodzące.

**Thread safety:** Jeden `hal_mutex_t` serializuje wszystkie
wywołania klienta MQTT. Callbacki są wykonywane po zwolnieniu wewnętrznego
mutexu.

**Pomocnicy mock:**
```c
void        hal_mock_mqtt_reset(void);
void        hal_mock_mqtt_set_connect_result(bool result);
void        hal_mock_mqtt_set_loop_result(bool result);
void        hal_mock_mqtt_set_connected(bool connected);
void        hal_mock_mqtt_set_state(int state);
void        hal_mock_mqtt_inject_message(const char *topic, const uint8_t *payload, uint16_t length);
const char *hal_mock_mqtt_get_server_host(void);
uint16_t    hal_mock_mqtt_get_server_port(void);
const char *hal_mock_mqtt_get_last_publish_topic(void);
const uint8_t *hal_mock_mqtt_get_last_publish_payload(void);
uint16_t    hal_mock_mqtt_get_last_publish_len(void);
bool        hal_mock_mqtt_get_last_publish_retained(void);
const char *hal_mock_mqtt_get_last_subscribe_topic(void);
uint8_t     hal_mock_mqtt_get_last_subscribe_qos(void);
const char *hal_mock_mqtt_get_last_unsubscribe_topic(void);
uint16_t    hal_mock_mqtt_get_keepalive(void);
uint16_t    hal_mock_mqtt_get_socket_timeout(void);
```

---

## `hal_time` - Funkcje pomocnicze kalendarza oraz opcjonalny czas systemowy/NTP

```c
#include <hal/time/hal_time.h>

// Zawsze dostępne; brak zależności sieciowej.
unsigned long hal_get_seconds(void);
uint32_t hal_time_from_components(int year, int month, int day,
                                  int hour, int minute, int second);
bool     hal_time_is_daylight_saving_time(int year, int month, int day);
void     hal_time_adjust_cet_cest(int *year, int *month, int *day,
                                  int *hour, int *minute);
bool     hal_time_is_in_range(long now, long start, long end);
void     hal_time_extract_minutes(long time_in_minutes,
                                  int *hours, int *minutes);

// Dostępne z HAL_ENABLE_TIME.
typedef enum {
  HAL_TIME_SOURCE_UNSET = 0,
  HAL_TIME_SOURCE_MANUAL,
  HAL_TIME_SOURCE_RTC,
  HAL_TIME_SOURCE_NTP,
} hal_time_source_t;

typedef enum {
  HAL_TIME_NTP_IDLE = 0,
  HAL_TIME_NTP_IN_PROGRESS,
  HAL_TIME_NTP_SYNCHRONIZED,
  HAL_TIME_NTP_FAILED,
} hal_time_ntp_state_t;

hal_status_t hal_time_set_unix_ex(uint64_t unix_time, uint32_t micros,
                                  hal_time_source_t source);
hal_status_t hal_time_get_status_ex(hal_time_status_t *out_status);
bool     hal_time_set_timezone(const char *tz);     // łańcuch strefy czasowej POSIX TZ
bool     hal_time_sync_ntp(const char *primary_server, const char *secondary_server);
hal_status_t hal_time_sync_ntp_ex(const char *primary_server,
                                  const char *secondary_server);
uint64_t hal_time_unix(void);                       // sekundy od epoki (bezpieczne dla Y2038)
bool     hal_time_is_synced(uint64_t min_unix);     // ważne i czas >= min_unix
bool     hal_time_get_local(struct tm *out_tm);
bool     hal_time_format_local(char *out, size_t out_size, const char *format);

// Dodatkowo dostępne z HAL_ENABLE_RTC.
hal_status_t hal_time_attach_rtc_ex(hal_rtc_t rtc, uint32_t policy_flags);
hal_status_t hal_time_detach_rtc_ex(void);
```

`hal_get_seconds()` zwraca monotoniczny czas działania zaokrąglony według
`(hal_millis() + 500) / 1000`. Funkcje kalendarza korzystają ze wspólnego
rdzenia proleptycznego kalendarza gregoriańskiego. Konwersja składowych przyjmuje daty
od początku epoki Unix do ostatniej sekundy możliwej do przedstawienia przez
`uint32_t`. Zwraca `0` zarówno dla błędu, jak i dla poprawnego początku epoki.
Funkcja CET/CEST stosuje politykę opartą wyłącznie na dacie: czas letni
zaczyna się w ostatnią niedzielę marca
(włącznie), a kończy w ostatnią niedzielę października (wyłącznie). Obie
zmiany następują o 00:00, ponieważ funkcja nie otrzymuje informacji o porze
dnia. Nieprawidłowe daty są odrzucane, a korekta normalizuje przejście między
dniami, miesiącami i latami.

`hal_time_is_in_range()` implementuje półotwarty przedział `[start, end)`.
`hal_time_extract_minutes()` używa semantyki ilorazu/reszty z C i akceptuje
każdy ze wskaźników wyjściowych jako opcjonalny.

**Wspólna implementacja:** `hal/time/hal_time_ntp.cpp` jako jedyny zarządza
zegarem czasu rzeczywistego (`wall clock`) w runtime.
`hal_time_set_unix_ex()` jest wspólną funkcją ustawiającą czas. Korzystają
z niej ręczne ustawianie czasu, przywracanie czasu z RTC, NTP oraz adaptery libc
danej platformy. Upływ czasu jest obliczany na podstawie monotonicznego,
64-bitowego licznika mikrosekund. Zawinięcie 32-bitowego licznika milisekund
nie cofa więc zegara czasu rzeczywistego. Adaptery `gettimeofday()`
i `settimeofday()` dla RP i STM32G474 odczytują i aktualizują ten sam stan,
zamiast utrzymywać drugą, programową epokę.

`hal_time_status_t` zapewnia jeden spójny odczyt: informację o ważności
i źródle czasu, sekundy i mikrosekundy Unix, stan NTP, ostatni wynik NTP
i epokę synchronizacji, a także pola stanu dołączenia RTC i wyniku jego
obsługi. `HAL_TIME_NTP_IN_PROGRESS` używa `HAL_EAGAIN`, natomiast
`HAL_TIME_NTP_FAILED` zachowuje konkretny błąd transportu lub timeoutu.
`HAL_TIME_NTP_IDLE` oznacza brak historii i zgłasza `HAL_NONE`. Dzięki temu
aplikacja może odróżnić poprawny czas przywrócony z RTC od zakończenia nowego
żądania NTP.

Przy włączonym RTC dołącz uchwyt RTC zarządzany przez wywołującego, używając
`HAL_TIME_RTC_RESTORE_IF_VALID`, `HAL_TIME_RTC_WRITE_AFTER_NTP` lub obu
naraz. Poprawny odczyt z RTC inicjalizuje tylko nieustawiony zegar programu.
Nieprawidłowy RTC pozostaje dołączony i zostanie zainicjalizowany po następnym
zweryfikowanym wyniku NTP. Błąd odczytu podczas przywracania również nie
odłącza RTC i jest dostępny w `last_rtc_status`. Uchwyt RTC musi pozostać
ważny, dopóki RTC jest dołączony. Przed powrotem `hal_time_detach_rtc_ex()`
czeka na zakończenie trwającego zapisu czasu NTP w RTC; dopiero potem
wywołujący może zdeinicjalizować RTC.

**Thread safety:** Funkcje pomocnicze bez efektów ubocznych są
reentrantne. Opcjonalne API czasu systemowego i NTP używają chronionych
mutexem, spójnych kopii stanu i obsługują współbieżne zadania oraz rdzenie.
Operacje wejścia/wyjścia DNS, UDP i RTC odbywają się bez mutexu stanu zegara,
dzięki czemu callback obsługi sieci może bez deadlocku ponownie wywołać
funkcję odczytującą czas. Każde wywołanie takiej funkcji lub
`hal_time_get_status_ex()` obsługuje oczekujące żądanie. Po 5-sekundowym
timeoucie serwera podstawowego rozpoczyna się próba z opcjonalnym serwerem
zapasowym.

**Pomocnicy mock:**
```c
void        hal_mock_time_reset(void);
void        hal_mock_time_set_unix(uint64_t unix_time);
void        hal_mock_time_set_local(const struct tm *tm_local);
const char *hal_mock_time_get_timezone(void);
const char *hal_mock_time_get_ntp_primary(void);
const char *hal_mock_time_get_ntp_secondary(void);
```

---


---

*Dalej: [Narzędzia](16_utilities.md)*
