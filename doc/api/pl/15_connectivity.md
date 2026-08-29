# Łączność sieciowa

*Dostępne również [po angielsku](../en/15_connectivity.md).*

> **Część [Dokumentacji API JaszczurHAL](../../pl/JaszczurHAL_API.md)**

Obejmuje: `hal_wifi`, `hal_udp`, `hal_tcp`, `hal_http_server`,
`hal_http_files`, `hal_websocket`, `hal_net_console`, `hal_net_commands`,
`hal_notify`, `hal_wireguard`, `hal_mqtt`, `hal_ota`, `hal_time` oraz
opcjonalny adapter zgodności `HAL_ENABLE_BSD_SOCKETS`.
Współdzielone typy sieciowe znajdują się w `hal_net.h`.

## Sieciowe API zwracające status

Nowy kod może korzystać z dodatkowych operacji `_ex` zwracających
`hal_status_t` dla WiFi, resolvera, TCP, UDP, MQTT i WireGuard. Istniejące API
pozostają dostępne bez zmian. Operacje, które historycznie zwracały licznik,
przyjmowały gniazdo lub stan peera, używają jawnego parametru wyjściowego,
więc konwersja na status nie odrzuca oryginalnego wyniku.

WiFi, resolver, TCP i UDP implementują obsługę statusu bezpośrednio w mocku
oraz backendach rodziny RP. Ich historyczne API oparte na `bool`, liczniku i
uchwycie to sąsiadujące, cienkie wrappery zgodności; nie zawierają one
rzeczywistej ścieżki we/wy.

Przykładowe wpisy obejmują `hal_wifi_begin_station_ex()`,
`hal_wifi_ping_status_ex()`, `hal_net_resolve_ipv4_ex()`,
`hal_tcp_socket_{connect,send,recv}_ex()`,
`hal_tcp_listener_{bind,listen,accept}_ex()`,
`hal_udp_socket_{bind,sendto,recvfrom}_ex()`, pomocnicze funkcje `_ex` UDP dla
pakietów w starym stylu (legacy-packet), `hal_mqtt_{connect,publish,subscribe}_ex()`
oraz `hal_notify_{open,send,poll,close}()`, a także
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

Typowa walidacja zgłasza `HAL_EINVAL`. Brak wyniku resolvera/wyszukiwania
używa `HAL_ENOENT`, niedostępny accept używa `HAL_EAGAIN`, wyczerpanie puli
używa `HAL_ENOMEM`, a próba operacji w niewłaściwym stanie gniazda używa
`HAL_ESTATE`. Na backendach RP CYW43 publiczne operacje statusu konsekwentnie
zgłaszają:

- `HAL_EUNSUPPORTED`, gdy wybrany profil płytki nie deklaruje całego
  wymaganego sprzętu radiowego;
- `HAL_EUNINIT`, gdy ten sprzęt jest zadeklarowany, ale nie zainicjalizował
  się pomyślnie;
- `HAL_EHW` po tym, jak sondowanie lub inicjalizacja oznaczyły sprzęt jako
  uszkodzony.

Samo wywołanie inicjalizacji zwraca swój oryginalny status drivera;
późniejszy dostęp sieciowy zgłasza utrwalony (sticky) stan `HAL_EHW`.
Pico+PIM730 wymaga zarówno możliwości CYW43, jak i zewnętrznego frontendu
radiowego. Nieudany preflight nie dotyka backendu ani pinów radiowych.
Numeryczne parsowanie IPv4, parsowanie adresów WireGuard oraz settery MQTT
dotyczące wyłącznie konfiguracji pozostają użyteczne offline. Błędy
transportu natywnego, które nie zmieniają stanu sprzętowego płytki, używają
`HAL_EIO`. Pełne sygnatury znajdziesz w publicznych nagłówkach modułów.

## Współdzielone typy sieciowe

`hal_net.h` zawiera proste typy wartości w C, współdzielone przez oparte na
uchwytach UDP i TCP oraz warstwy zgodności BSD/POSIX. Punkty końcowe
(endpoints) mają przechowywanie oznaczone rodziną adresów dla IPv4 i IPv6.
Obecne backendy CYW43 deklarują IPv4. ESP32-S3 deklaruje rodziny włączone w
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
`addr_len` wynosi cztery dla IPv4 i szesnaście dla IPv6; `scope_id` niesie
zakres interfejsu IPv6. Pole `port` jest w kolejności hosta (host byte
order); adaptery POSIX wykonują własną konwersję `htons()` / `ntohs()` na
granicy API.

**Uwagi dotyczące resolvera:**
- `hal_net_resolve_ex(...)` przyjmuje adresy numeryczne lub nazwy hostów,
  podpowiedź rodziny adresów oraz ograniczoną tablicę wyników będącą
  własnością wywołującego. `HAL_EOVERFLOW` zgłasza wymaganą liczbę wyników
  bez zapisywania częściowego wyniku.
- `hal_net_resolve_ipv4(...)` przyjmuje dosłowny zapis IPv4 z kropkami lub
  nazwę hosta i zapisuje cztery oktety IPv4. Wywołujący przechowuje port
  transportowy osobno.
- Backend mocka rozwiązuje dosłowne adresy IPv4, `localhost` oraz wpisy
  testowe dodane przez `hal_mock_net_set_dns_entry(...)`.
- Backendy CYW43 rozwiązują dosłowne wartości numeryczne lokalnie, a dla nazw
  hostów używają własnego resolvera lwIP. Rozwiązywanie nazw hostów wymaga
  zainicjalizowanego sprzętu; parsowanie wartości dosłownych - nie.
- ESP32-S3 rozwiązuje nazwy przez natywną ścieżkę `getaddrinfo()` ESP-IDF
  lwIP, po tym jak cykl życia WiFi/`esp_netif` osiągnie stan użyteczny.

**Pomocnicy mock resolvera:**
```c
void hal_mock_net_reset(void);
bool hal_mock_net_set_dns_entry(const char *host, const char *ip);
```

### Natywny backend ESP32-S3 i granica weryfikacji

Provider ESP32-S3 inicjalizuje NVS, `esp_netif`, domyślną pętlę zdarzeń ESP,
netif stacji oraz natywny driver WiFi za istniejącymi publicznymi fasadami
HAL. Handlery zdarzeń tłumaczą stan startu/połączenia/rozłączenia stacji,
dzierżawy IPv4, skanowania, uwierzytelniania, braku sieci, ponownego łączenia
oraz zamknięcia. Uchwyty TCP i UDP używają ograniczonych pul ze sprawdzaną
generacją nad natywnymi gniazdami lwIP i `select()`. `HAL_ENABLE_BSD_SOCKETS`
udostępnia natywne API BSD z ESP-IDF, zamiast definiować współdzielone
symbole zgodności po raz drugi.

Ten sam graf buduje współdzielonego klienta TLS BearSSL, klienta HTTP/HTTPS,
tekstowy serwer HTTP, tekstowy serwer WebSocket, MQTT z opcjonalnym TLS,
NTP/czas, surowe OTA aplikacji ESP oraz WireGuard nad portem rozszerzenia
lwIP targetu. Publiczne API nie ma serwera TLS, serwera HTTPS, WSS ani
klienta WebSocket. `tests/fixtures/esp32s3_phase3` dowodzi rozwiązywania
funkcji, doboru źródeł/zależności, buildu, konsolidacji (linkingu),
partycji i artefaktów; nie dowodzi zachowania sprzętu w runtime,
cyklu życia, rollbacku ani zachowania w negatywnych scenariuszach
bezpieczeństwa.

## `hal_wifi` - WiFi  *(opcjonalny - `HAL_ENABLE_WIFI`)*

Buildy RP, które potrzebują WiFi, wybierają profil zdolny do obsługi radia:
`picow`, `pico2w` lub `pico-rm2`. ESP32-S3 używa natywnego radia
zadeklarowanego przez swój profil płytki. `HAL_ENABLE_WIFI` wybiera fasadę;
zależne moduły, takie jak MQTT i WireGuard, propagują tę flagę. Moduły
sieciowe mogą też być kompilowane dla zwykłego profilu Pico; wywołania
publiczne zwracają wtedy `HAL_EUNSUPPORTED` bez dostępu do pinów CYW43. Na
zdolnym profilu, `hal_wifi_set_mode_ex(HAL_WIFI_MODE_STA)` i
`hal_wifi_begin_station_ex(...)` są jawnymi punktami wejścia inicjalizacji.
Zapytania o stan, skanowania i otwarcia transportu nie inicjalizują radia
niejawnie.

CYW43 profile zachowują fabryczny adres MAC radia, gdy jest on obecny w
pamięci OTP modułu. Obejmuje to adresy przydzielone przez Raspberry Pi,
takie jak `28:CD:C1:xx:xx:xx`. Jeśli radio zgłasza, że jego MAC OTP jest
nieustawiony, JaszczurHAL używa konwencji fallback z Pico SDK: lokalnie
administrowanego adresu unicast wyprowadzonego z sześciu najmniej znaczących
bajtów UID płytki. Fallback jest stabilny dla danej płytki i nie powiela
wspólnego prefiksu UID współdzielonego przez wiele płytek RP.
`hal_wifi_get_mac_ex()` oraz interfejs lwIP używają tego samego adresu
przechowywanego w stanie kontrolera CYW43 po inicjalizacji; aplikacje nie
mogą podstawiać osobno wygenerowanego adresu opartego na UID płytki, gdy OTP
jest obecne.

Dla profili RP CYW43, `hal_wifi_begin_station_ex(..., true)` uruchamia
żądanie dołączenia i zwraca sterowanie, gdy tylko CYW43 je zaakceptuje.
Wywołujący może zwolnić lub nadpisać bufory SSID i hasła po powrocie z
wywołania. Obsługuj połączenie, odpytując `hal_wifi_get_state_ex()` lub
`hal_wifi_is_connected()`; te wywołania przesuwają backend odpytywania i
ujawniają stany łączenia, braku sieci, uwierzytelniania, DHCP i połączenia.
Przekazanie `false` zachowuje ograniczone czasowo, blokujące dołączenie,
które czeka na dzierżawę DHCP.

STM32G474 obsługuje zewnętrzny CYW43/PIM730 przez eksperymentalny profil
`nucleo-g474re-pim730`, jego jednoprzewodowy transport gSPI oraz ten sam
przypięty stos lwIP. Zwykły profil `nucleo-g474re` celowo nie ma możliwości
radiowych i odrzuca build sieciowy CYW43. Dołączenie stacji na STM32G474 jest
ograniczone czasowo i blokujące; zażądanie formy nieblokującej zwraca
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

**impl/rp2040:** własny driver CYW43 JaszczurHAL i stos lwIP nad PIO/gSPI.
**impl/stm32g474:** ten sam właściciel CYW43/lwIP nad jednoprzewodowym
transportem gSPI STM32G474.
**impl/esp32:** natywny cykl życia stacji ESP-IDF nad NVS, `esp_netif`,
domyślną pętlą zdarzeń, `esp_wifi`, DHCP/DNS, skanowaniem, pingiem i
zdarzeniami ponownego łączenia.
**impl/.mock:** wstrzykiwanie stanu przez pomocnicze funkcje mocka.
**Thread safety:** Backendy sprzętowe RP, STM32G474 i ESP32-S3
serializują publiczne wywołania wrappera HAL. Wewnętrzne mutexy singletonowe
chronią stan providera, postęp usługi sieciowej i dostęp do stosu. Backend
mocka jest deterministycznym test double z wstrzykiwaniem stanu dla testów
jednowątkowych.

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

Wszystkie sprzętowe buildy CYW43 wybierają jeden backend fasady, jedną
magistralę oraz przypięty stos lwIP:

```c
#define HAL_NETWORK_BACKEND_CYW43
#define HAL_CYW43_STACK_LWIP
```

Profile płytek RP emitują `HAL_CYW43_BUS_PICO_PIO` oraz odpowiadające im
piny. Pico W, Pico 2 W i Pico+PIM730 używają tego samego cyklu życia
providera. Transport PIO wyprowadza swój dzielnik zegara 16.8 z aktywnego
`clk_sys` oraz `HAL_CYW43_GSPI_TARGET_HZ` (domyślnie 31,25 MHz), wybierając
odpowiedni program próbkowania wysokiej/niskiej prędkości bez przekraczania
wartości docelowej. Zmiana `clk_sys`, gdy provider jest aktywny, jest
odrzucana; zdeinicjalizuj sieć, zmień zegar i zainicjalizuj ją ponownie.

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
przecinalna (cuttable) ścieżka `BT_ON`-do-`WL_ON` na PIM730 jest nienaruszona,
więc `BT_ON`/`BL_ON` pozostaje w przeciwnym razie niepodłączone. Sprawdź tę
ścieżkę przed użyciem profilu; przecięta ścieżka to inna topologia sprzętowa
i nie jest obecnie opisana przez żaden profil płytki.

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

Cztery piny muszą być odrębnymi, prawidłowymi GPIO STM32G474. Pojemność
transakcji wynosi co najmniej osiem bajtów i musi być wielokrotnością
czterech. Odpytywana ścieżka danych gSPI jest taktowana na podstawie
liczników cykli DWT, zachowując swój konserwatywny półokres przy zmianach
zegara systemowego. DAT zmienia kierunek między nadawaniem a odbiorem.
Zbocze wybudzenia hosta jest obsługiwane przez jednorazowe (one-shot) EXTI o
wysokim priorytecie: ISR maskuje linię i planuje pracę, a ścieżka usługi
ponownie ją uzbraja po odprowadzeniu (draining) pracy CYW43/lwIP.

Oba backendy docelowe są właścicielami uruchomienia zasilania CYW43,
pobrania firmware'u, netif lwIP, DHCP, DNS, echa ICMP, surowego UDP/TCP,
skanowań i zamknięcia. `hal_net_service()` wykonuje jeden ograniczony
przebieg usługi. RP bare-metal i STM32G474 używają modelu wykonania opartego
na odpytywaniu (poll); wywołujący z FreeRTOS nadal używają tego samego
zserializowanego kontekstu stosu. Inicjalizacja, pule gniazd/nasłuchiwaczy
oraz deinicjalizacja są chronione osobno, więc zamknięcie zamyka wszystkie
uchwyty fasady przed zatrzymaniem lwIP, radia i magistrali.

Pamięć sieciowa jest ograniczona przez pule ustalane podczas buildu.
Głównymi ustawieniami są `HAL_TCP_SOCKET_MAX_INSTANCES` (domyślnie 4),
`HAL_TCP_LISTENER_MAX_INSTANCES` (domyślnie 2), `HAL_UDP_SOCKET_MAX_INSTANCES`
(domyślnie 4), `HAL_LWIP_TCP_RX_LIMIT` (domyślnie 16 KiB na silnik TCP),
`HAL_LWIP_TCP_ACCEPT_QUEUE_DEPTH` (domyślnie 5) oraz
`HAL_LWIP_UDP_RX_QUEUE_DEPTH` (domyślnie 4). Dobieraj te wartości razem z
`HAL_CYW43_MAX_TRANSACTION_BYTES` oraz wybraną konfiguracją lwIP pod budżet
SRAM targetu.

---

<a id="halhttpclient-httphttps-client-opt-in-halenablehttpclient"></a>

## `hal_http_client` - klient HTTP/HTTPS  *(opt-in - `HAL_ENABLE_HTTP_CLIENT`)*

`hal_http_client` wykonuje jedno ograniczone czasowo żądanie HTTP/1.1 przez
HAL TCP lub zweryfikowanego klienta TLS BearSSL. Ta flaga włącza TCP i WiFi.
Dla HTTPS wybierz dodatkowo `HAL_ENABLE_TLS`.

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

Żądanie może odwoływać się do nagłówków będących własnością wywołującego oraz
opcjonalnego ciała. Walidacja danych wejściowych odrzuca puste hosty,
nieprawidłowe metody, ścieżki niebezwzględne, wstrzyknięcia CR/LF, zerowe
porty/timeouty oraz niespójne pary wskaźnik/licznik.
`hal_http_client_request_init()` wybiera zwykłe GET `/`, port 80 oraz
15-sekundowy timeout.

Dla HTTPS ustaw `transport` na `HAL_HTTP_CLIENT_TRANSPORT_TLS`, zwykle użyj
portu 443 i wskaż w `tls_security` skonfigurowany magazyn zaufania, callback
czasu i callback entropii. Jednorazowe wywołanie tworzy klienta TLS w trybie
ograniczonego workera (bounded-worker), używa timeoutu żądania dla
connect/read/write/shutdown, weryfikuje nazwę hosta żądania przez BearSSL i
zamyka transport przed powrotem. Kotwica zaufania i pamięć callbacków muszą
pozostać ważne przez cały czas trwania wywołania.

Klient wysyła `Connection: close`, parsuje linie statusu HTTP/1.0 lub
HTTP/1.1, rozpoznaje `Content-Length` i czyta ciało do zadeklarowanej
długości lub zamknięcia połączenia. Ciała odpowiedzi są kopiowane bez
terminatora. `HAL_EOVERFLOW` zgłasza wymaganą długość ciała, gdy bufor
wywołującego jest za mały. Kodowanie transferu typu chunked zwraca
`HAL_EUNSUPPORTED`.

**Implementacja:** `hal/network/http/hal_http_client.cpp`.
**Testy:** `test_hal_http_client` obejmuje walidację, fragmentowane nagłówki
odpowiedzi, metadane odpowiedzi oraz ograniczone kopiowanie ciała.
`test_hal_http_client_plaintext_compile` utrzymuje budowalność kombinacji
flag tylko-plaintext. Zweryfikowana ścieżka klienta HTTP/HTTPS jest częścią
[`examples/18_freertos_suite`](../../../examples/18_freertos_suite/README.md).

---

<a id="halnotify-notifications-opt-in-halenablenotify"></a>

## `hal_notify` - powiadomienia  *(opt-in - `HAL_ENABLE_NOTIFY`)*

`hal_notify` to niewielka fasada powiadomień z uchwytami kanałów
sprawdzanymi generacyjnie oraz deskryptorami backendów. Fasada zarządza
cyklem życia kanału, domyślnym rozwiązywaniem formatu/timeoutu oraz
serializacją per-kanał; konkretne backendy dostarczania są właścicielami
konfiguracji swojego protokołu.

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

Pierwszy backend jest włączany przez `HAL_ENABLE_NOTIFY_TELEGRAM`, który
propaguje `HAL_ENABLE_NOTIFY`, `HAL_ENABLE_HTTP_CLIENT`, `HAL_ENABLE_TLS` i
`HAL_ENABLE_CJSON`. Wysyła wywołania `sendMessage` Telegram Bot API przez
`hal_http_client_perform_ex()`. Publiczne dostarczanie do `api.telegram.org`
wymaga HTTPS i niepustego (non-NULL) `hal_tls_security_config_t`; zwykłe
HTTP jest akceptowane wyłącznie dla niestandardowego hosta dostarczonego
przez wywołującego, takiego jak lokalne proxy lub lokalne wdrożenie Bot API.
Dopasowywanie publicznego hosta nie rozróżnia wielkości liter ASCII i
akceptuje końcową kropkę bezwzględnej nazwy DNS, więc warianty pisowni nie
mogą obejść tej polityki.

Konfiguracje backendu zachowują referencjonowane łańcuchy znaków i pamięć
bezpieczeństwa TLS jako własność wywołującego. JaszczurHAL nie utrwala ani
nie provisionuje tokenu bota ani ID czatu. Aplikacje powinny pobierać je ze
swojego komponentu poświadczeń/przechowywania, utrzymywać referencjonowane
bufory żywe przez cały czas do `hal_notify_close()`, a następnie zwalniać je
zgodnie z regułami własności tego komponentu. `device_name` na poziomie
kanału jest dziedziczone przez wiadomości, które nie dostarczają własnego
nadpisania.

Backend Telegram poprzedza wiadomość ważnością (severity) i opcjonalną
tożsamością urządzenia, na przykład `[ERROR] [garage] ECU alert`. Wiadomości
czysto tekstowe dłuższe niż limit `HAL_NOTIFY_TELEGRAM_TEXT_MAX` na żądanie
(domyślnie 3500 bajtów) są dzielone na granicach UTF-8, najlepiej blisko
białych znaków, i oznaczane jako `(1/N)`, `(2/N)` i tak dalej. Limit obejmuje
wygenerowany prefiks i pozostawia margines poniżej limitu 4096 znaków
`sendMessage` Telegrama. Bogaty tekst MarkdownV2/HTML nie jest dzielony
automatycznie, ponieważ podział mógłby uszkodzić encje dostarczone przez
wywołującego; zbyt duża wiadomość w formacie rich-text zwraca
`HAL_EOVERFLOW`.

`HAL_NOTIFY_MESSAGE_SILENT` odwzorowuje się na `disable_notification`
Telegrama, natomiast `HAL_NOTIFY_MESSAGE_SUPPRESS_LINK_PREVIEW` używa opcji
podglądu linku Telegrama. Błędy HTTP/API Telegrama są zgłaszane przez
`hal_status_t` oraz opcjonalne pola `hal_notify_receipt_t`: status HTTP/API,
błąd providera, retry-after oraz ID wiadomości providera. `parts_sent` i
`parts_total` ujawniają postęp wieloczęściowy;
`HAL_NOTIFY_RECEIPT_PARTIAL_DELIVERY` oznacza, że co najmniej jedna część
została zaakceptowana, zanim kolejna część zawiodła. Dostarczanie
wieloczęściowe nie jest więc atomowe.

`hal_notify_send()` jest ograniczonym czasowo wywołaniem synchronicznym i
może wykonać wiele żądań HTTP dla podzielonej wiadomości. Wywołuj ją z
dedykowanego workera aplikacji/RTOS, gdy główna pętla sterująca musi
pozostać responsywna. `hal_notify_poll()` obsługuje wyłącznie backendy
implementujące postęp sterowany odpytywaniem (poll-driven); nie zamienia
synchronicznego backendu w asynchroniczny. `hal_notify_close()` zwraca
status zamknięcia backendu, gdy zamknięcie jest natychmiastowe. Jeśli inna
operacja już przetrzymuje kanał, zamknięcie jest odroczone, a jego wynik
jest zwracany przez tę ostatnią operację, gdy sama operacja skądinąd się
powiedzie.

**Implementacja:** `hal/network/notify/hal_notify.cpp` oraz
`hal/network/notify/hal_notify_telegram.cpp`.
**Testy:** `test_hal_notify` obejmuje walidację fasady, dispatch fałszywego
(fake) backendu, cykl życia uchwytu i błędy zamknięcia, JSON/prefiksy żądań
Telegram, znormalizowane odrzucanie HTTP dla publicznego hosta, dostarczanie
wieloczęściowe oraz mapowanie limitów szybkości (rate-limit).
`test_hal_notify_c_compile` obejmuje interfejs nagłówka C.

---

## `hal_http_server` - serwer HTTP/1.1  *(opt-in - `HAL_ENABLE_HTTP_SERVER`)*

Mały, sterowany odpytywaniem (poll-driven) serwer HTTP zaimplementowany nad
opartym na uchwytach API nasłuchiwacza/gniazda `hal_tcp`. Włączenie
`HAL_ENABLE_HTTP_SERVER` propaguje `HAL_ENABLE_TCP`, który z kolei propaguje
`HAL_ENABLE_WIFI` w obecnych buildach zdolnych do obsługi sieci.

Pierwsza wersja jest celowo zwarta i deterministyczna:

- dokładne dopasowywanie tras po metodzie/ścieżce,
- jedno żądanie na połączenie TCP,
- parsowanie metod `GET`, `HEAD`, `POST`, `PUT`, `DELETE` i `OPTIONS`,
- udostępnienie handlerowi query string, nagłówków żądania i ciała żądania,
- buforowane ciało odpowiedzi z automatycznym `Content-Length`,
- rejestracja tras dokładnych i prefiksowych,
- jawne pomocnicy statusu, typu zawartości i nagłówków odpowiedzi,
- kooperacyjna pętla usługi `hal_http_server_poll()`.

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

**wspólna implementacja tematyczna:** `hal/network/http/hal_http_server.cpp`.
**impl/.mock:** pokryte przez mockowy backend nasłuchiwacza/gniazda TCP oraz
`test_hal_http_server`.

---

## `hal_http_files` - serwowanie i przesyłanie plików  *(opt-in - `HAL_ENABLE_HTTP_FILES`)*

Mały adapter plików zbudowany na `hal_http_server`. Włączenie
`HAL_ENABLE_HTTP_FILES` włącza również `HAL_ENABLE_HTTP_SERVER`,
`HAL_ENABLE_TCP` i `HAL_ENABLE_WIFI`.

Adapter jest neutralny względem systemu plików. Mapuje adresy URL HTTP na
zamontowany katalog główny i wywołuje callbacki aplikacji/backendu dla
`stat`, `read` oraz opcjonalnego `write`. Dzięki temu warstwa HTTP pozostaje
wielokrotnego użytku dla zasobów RAM, LittleFS, FatFs/SD, zasobów flash lub
testów.

Obsługiwane zachowanie:

- serwowanie plików `GET` / `HEAD` przez trasy prefiksowe,
- wybór typu MIME na podstawie rozszerzenia,
- generowane słabe ETagi na podstawie ścieżki, rozmiaru i mtime,
- `If-None-Match` -> `304 Not Modified`,
- surowy upload `PUT` na ścieżkę pod zamontowanym prefiksem,
- upload `POST` typu multipart/form-data z polami `path` i `file`,
- odrzucanie path traversal dla `..` i ukośników odwrotnych.

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

Podstawowy przepływ:

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

Uploady są fail-closed: `enable_upload = true` wymaga zarówno `write`, jak i
`authorize_upload`. Callback autoryzacji uruchamia się przed parsowaniem
multipart lub zapisami do systemu plików i musi zwrócić `HAL_OK`; każdy inny
status produkuje HTTP 403. Używaj TLS, gdy poświadczenia przechodzą przez
niezaufaną sieć.

Przykład uploadu multipart:

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

Przy powyższej konfiguracji callback pliku otrzymuje ścieżkę
`/www/logs/boot.txt`.

Obecny `hal_http_server` buforuje każde żądanie i odpowiedź w statycznych
buforach o stałym rozmiarze, więc ten adapter jest przeznaczony dla małych
plików wbudowanych, uploadów konfiguracji i diagnostyki, a nie dla dużych
transferów strumieniowych.

Domyślne limity statyczne można nadpisać przed dołączeniem nagłówków HAL:

```c
#define HAL_HTTP_FILES_MAX_MOUNTS 2u
#define HAL_HTTP_FILES_PATH_MAX 128u
#define HAL_HTTP_FILES_ETAG_MAX 48u
#define HAL_HTTP_FILES_IO_BUFFER_SIZE 128u
```

**wspólna implementacja tematyczna:** `hal/network/http/hal_http_files.cpp`.
**impl/.mock:** pokryte przez mockowe HTTP/TCP oraz `test_hal_http_files`.

---

## `hal_websocket` - serwer WebSocket  *(opt-in - `HAL_ENABLE_WEBSOCKET`)*

Mały, sterowany odpytywaniem serwer WebSocket zaimplementowany bezpośrednio
nad `hal_tcp`. Włączenie `HAL_ENABLE_WEBSOCKET` propaguje `HAL_ENABLE_TCP`,
który z kolei propaguje `HAL_ENABLE_WIFI` w obecnych buildach z łącznością
sieciową.

Serwer akceptuje klientów TCP, wykonuje handshake HTTP Upgrade dla jednej
skonfigurowanej ścieżki, a następnie przełącza każde zaakceptowane gniazdo w
parsowanie ramek WebSocket. Pierwsza implementacja jest celowo zwarta:

- handshake `Sec-WebSocket-Accept` zgodny z RFC 6455,
- maskowane ramki klienta i niemaskowane ramki serwera,
- jednoramkowe wiadomości tekstowe/binarne,
- automatyczna odpowiedź pong na ping,
- obsługa ramki close z callbackiem rozłączenia,
- pomocnicy wysyłki per-klient oraz pomocnicy rozgłaszania (broadcast),
- kooperacyjna pętla usługi `hal_websocket_server_poll()`.

Nie implementuje fragmentowanych wiadomości, permessage-deflate, TLS,
ciasteczek ani negocjacji subprotokołu. Umieść uwierzytelnianie lub politykę
sesji w protokole aplikacji lub na stronie HTTP, która otwiera gniazdo.

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

**wspólna implementacja tematyczna:** `hal/network/websocket/hal_websocket.cpp`.
**impl/.mock:** pokryte przez mockowy backend nasłuchiwacza/gniazda TCP oraz
`test_hal_websocket`.

---

## `hal_net_console` - konsola debugowania TCP  *(opt-in - `HAL_ENABLE_NET_CONSOLE`)*

Chroniona hasłem konsola TCP zaimplementowana nad opartym na uchwytach API
nasłuchiwacza/gniazda `hal_tcp`. Włączenie `HAL_ENABLE_NET_CONSOLE` propaguje
`HAL_ENABLE_TCP`, który z kolei propaguje `HAL_ENABLE_WIFI` w buildach z
łącznością sieciową.

Konsola jest transportem, a nie zamiennikiem zwykłego portu debugowania:
`hal_serial`, `deb` i `derr` nadal piszą do UART/USB, a uwierzytelnieni
klienci TCP otrzymują dodatkową kopię. Wejście TCP jest dostępne dla
firmware'u przez callback linii oraz odpytywany bufor RX, więc aplikacje
mogą udostępnić mały shell poleceń lub interfejs diagnostyczny.

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

**wspólna implementacja tematyczna:** `hal/network/net_console/hal_net_console.cpp`.
**impl/.mock:** pokryte przez mockowy backend nasłuchiwacza/gniazda TCP oraz
`test_hal_net_console`.

---

## `hal_net_commands` - warstwa komend HTTP/WebSocket  *(opt-in - `HAL_ENABLE_NET_COMMANDS`)*

Adaptery tekst/JSON dla wbudowanych kanałów sterujących WebUI. Moduł parsuje
wejście HTTP i WebSocket, przekazuje je przez współdzielony
domyślny [`hal_command_router`](23_commands.md) i formatuje ograniczoną
odpowiedź. Włączenie `HAL_ENABLE_NET_COMMANDS` włącza również
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

`cmd` i `command` są akceptowane jako pola nazwy polecenia. `args` i `params`
są udostępniane handlerom jako `json_args`; argumenty tekstowe są też
odzwierciedlane przez `args_text`.

Generyczny `hal_command_handler_t` widzi argumenty tekstowe jako bezpieczne
binarnie (binary-safe) bajty pozostałe po nazwie polecenia. Dla JSON widzi
zwartą serializację samej wartości `args` lub `params`; brakująca wartość
tworzy pusty widok argumentu. Żądania sieciowe używają zerowych
identyfikatorów żądania, peera i sesji i obecnie nie zgłaszają żadnych flag
bezpieczeństwa poleceń.

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

Funkcje rejestracji zgodności przechowują handlery w domyślnym routerze, ale
ograniczają je do źródeł direct, HTTP i WebSocket, ponieważ ich widok
żądania zawiera pola parsowane wyłącznie sieciowo. Zarejestruj
`hal_command_definition_t` bezpośrednio na domyślnym routerze, gdy jeden
handler bezpieczny binarnie musi też akceptować LoRa lub inny adapter.
Ścieżki sieciowe obecnie nie potwierdzają żadnych flag bezpieczeństwa
poleceń, więc polityki routera wymagające takich flag odrzucają te żądania.
`hal_net_commands_count()`, unregister i clear widzą ten sam współdzielony
zestaw handlerów. `hal_net_commands_clear()` usuwa też rejestracje
generyczne, gdy żaden handler nie jest aktywny, i zwraca `hal_status_t`:
`HAL_EBUSY` podczas aktywnego dispatchu na współdzielonym
domyślnym routerze pozostawia zestaw handlerów niezmieniony.

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

Współdzielona odpowiedź zachowuje kolejność ustalonych pól sieciowych i
dodaje `encoding` na końcu. `message` i `content_type` są pożyczonymi
(borrowed) wskaźnikami; wartości dostarczone przez handler muszą pozostać
ważne, dopóki odpowiedź nie zostanie sformatowana i wysłana.

Podstawowy przepływ:

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

Jeśli handler nie zapisze ciała, dispatcher emituje małą domyślną odpowiedź
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

Poprzednie zapisy `HAL_NET_COMMANDS_MAX_COMMANDS`, `HAL_NET_COMMANDS_NAME_MAX`
i `HAL_NET_COMMANDS_RESPONSE_BUFFER_SIZE` pozostają aliasami współdzielonych
limitów routera. Jeśli obie formy są zdefiniowane, ich wartości muszą się
zgadzać.

**wspólna implementacja tematyczna:** `hal/network/net_commands/hal_net_commands.cpp`.
**impl/.mock:** pokryte przez mockowe backendy TCP HTTP/WebSocket oraz
`test_hal_net_commands`.

---


## `hal_ota` - aktualizacja firmware'u z opcjonalnym AUTH2  *(opt-in - `HAL_ENABLE_OTA`)*

Thread-safe natywna usługa OTA nad HAL UDP/TCP. RP i ESP32-S3
współdzielą wykrywanie, wymianę AUTH2 opartą na HMAC-SHA256 wyprowadzonym z
hasła, transfer, callbacki oraz publiczne zachowanie statusu rozruchu,
zachowując przy tym modele obrazu i aktywacji specyficzne dla targetu.

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
- Propaguje `WIFI`, `UDP`, `TCP`, `CRYPTO` i `CRC`.
- `hal_ota_begin()` inicjalizuje usługę OTA i rejestruje wewnętrzne hooki
  zdarzeń.
- `hal_ota_handle()` odpytuje transport OTA i dysponuje zakolejkowane
  zdarzenia do callbacków użytkownika.
- Handlery callbacków można zastąpić lub wyrejestrować, przekazując `NULL`.
- Ponowne wejście do `hal_ota_begin()` czyści zakolejkowane zdarzenia
  mocka/drivera przed przetwarzaniem.
- Przy skonfigurowanym niepustym haśle, AUTH2 wiąże polecenie, port
  callbacku, rozmiar obrazu, MD5 obrazu oraz niezależne nonce
  urządzenia/klienta. Uwierzytelnianie jest akceptowane wyłącznie z adresu
  UDP i portu źródłowego zaproszenia; callback TCP musi pochodzić z tego
  samego adresu peera. Starsze (legacy) wiadomości AUTH/200 są odrzucane, a
  niepuste hasło hosta nie może zaakceptować bezpośredniego `OK`.
- AUTH2 używa ścisłego ramkowania pól ASCII i liczb dziesiętnych w
  najkrótszej formie; niejednoznaczne białe znaki, osadzone NUL-e, dodatkowe
  pola/linie, źle sformułowane długości oraz numeryczne aliasy z wiodącymi
  zerami są odrzucane. Nonce urządzenia/klienta pochodzą odpowiednio z
  docelowego providera bezpiecznej losowości i CSPRNG systemu operacyjnego
  hosta.
- Pominięcie `hal_ota_set_password()` lub przekazanie pustego łańcucha
  pomija AUTH2. Ten tryb jest nieuwierzytelniony i nadaje się wyłącznie do
  izolowanych sieci deweloperskich.
- Natywne obrazy RP zawierają id targetu, offset programu, generację,
  wersję, SHA-256 ładunku, HMAC-SHA256 oraz CRC nagłówka. Klucz HMAC jest
  wyprowadzany z tego samego hasła aplikacji, które jest używane przez
  uwierzytelnianie transportu.
- Natywna flash RP jest podzielona na 16-kilobajtowy niezmienny region
  rozruchowy, równe sloty `program`/`staging`, dziennik faz (phase journal),
  sektor roboczy (scratch), dwa redundantne sektory stanu oraz istniejący
  ogon LittleFS/EEPROM.
- Aplikator rozruchu RP zamienia miejscami `program` i `staging` sektor po
  sektorze. Jego monotoniczny dziennik faz pozwala mu wznowić działanie po
  utracie zasilania. Niepotwierdzona próba jest wycofywana po
  `HAL_RP_OTA_MAX_BOOT_ATTEMPTS` rozruchach.
- ESP32-S3 przyjmuje surowy plik BIN aplikacji ESP, weryfikuje MD5 transferu
  oraz walidację obrazu ESP-IDF, zapisuje nieaktywną partycję aplikacji OTA
  przez `esp_ota_*`, wybiera ją do rozruchu i restartuje się. Jego
  wygenerowane wartości domyślne wybierają `two-ota-large` z włączonym
  rollbackiem aplikacji ESP-IDF. Status rozruchu mapuje partycje
  running/boot oraz stany obrazu ESP OTA na publiczne tryby: stabilny,
  oczekujący, próbny, rollback i odzyskiwania.
- Wywołuj `hal_ota_confirm_boot_ex()` dopiero po przejściu testów
  samokontrolnych aplikacji. Na ESP32-S3 wywołuje to
  `esp_ota_mark_app_valid_cancel_rollback()`. Wywołanie tej funkcji w stanie
  stabilnym jest nieszkodliwe.

**impl/rp2040:** implementacja staging/aplikatora dla RP2040 i RP2350.
**impl/esp32:** natywne partycje OTA ESP-IDF i surowe obrazy aplikacji. Hasło
AUTH2 jest opcjonalne na poziomie API urządzenia; wdrożone systemy muszą
skonfigurować niepusty sekret oraz zastosować politykę secure-boot/szyfrowania
flash ESP-IDF odpowiednią dla swojego modelu zagrożeń.
**impl/.mock:** deterministyczny test double z wstrzykiwaniem zdarzeń.
**Thread safety:** Backendy rodziny RP i ESP32-S3 są thread-safe
i bezpieczne wielordzeniowo dla publicznych API. Singletonowy `hal_mutex_t`
serializuje wszystkie wywołania wrappera, a callbacki są wywoływane poza tą
blokadą. Leniwa (lazy) alokacja mutexu jest fail-closed:
operacje boolowskie zwracają `false`, operacje statusu zwracają
`HAL_ENOMEM`, a handler usługi zwraca sterowanie bez dotykania stanu.

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

Wymagania specyficzne dla targetu dotyczące projektu, firmware'u, VS Code,
zapory sieciowej, potwierdzenia, rollbacku, odzyskiwania i bezpieczeństwa są
udokumentowane w [Natywnym workflow OTA](../../pl/OTAWorkflow.md).
Referencyjna aplikacja RP jest dostępna w
[`examples/25_ota`](../../../examples/25_ota/).

---

## `hal_udp` - datagramy UDP  *(opt-in - `HAL_ENABLE_UDP`)*

Oparte na uchwytach API transportu UDP dla niezależnych gniazd
datagramowych. Oryginalne API `hal_udp_*` dla pojedynczego gniazda pozostaje
dostępne jako wrapper zgodności na domyślnym uchwycie UDP.

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
- `hal_udp_socket_open()` alokuje gniazdo z `HAL_UDP_SOCKET_MAX_INSTANCES`;
  zamykaj nieużywane gniazda przez `hal_udp_socket_close()`.
- `hal_udp_socket_bind(...)` wiąże lokalny punkt końcowy IPv4. Rodzina
  adresów musi być `HAL_NET_AF_INET`, a port musi być niezerowy.
- `hal_udp_socket_sendto(...)` wysyła jeden datagram do punktu końcowego
  IPv4 i zwraca liczbę zaakceptowanych bajtów lub `<0` w przypadku błędu.
- `hal_udp_socket_recvfrom(...)` czyta z jednego związanego gniazda.
  `timeout_ms == 0` to natychmiastowe odpytanie; `HAL_NET_TIMEOUT_FOREVER`
  żąda blokującego oczekiwania.
- `hal_udp_socket_can_recv(...)` i `hal_udp_socket_can_send(...)` to
  sondy gotowości, które nie pobierają danych, dla warstw zgodności takich jak BSD
  `select()`.
- `hal_udp_begin(...)` otwiera/wiąże starsze (legacy) domyślne gniazdo UDP.
- `hal_udp_parse_packet()` zwraca rozmiar pakietu, `0` gdy żaden pakiet nie
  jest dostępny.
- `hal_udp_remote_ip(...)` i `hal_udp_remote_port()` ujawniają punkt
  końcowy nadawcy przechwycony z ostatniego udanego `hal_udp_parse_packet()`.
- `hal_udp_begin_packet_remote()` wysyła datagram odpowiedzi do tego
  przechwyconego nadawcy.
- `hal_udp_write(...)` / `hal_udp_write_str(...)` dopisują bajty ładunku do
  datagramu otwartego przez `hal_udp_begin_packet*()`.
- `hal_udp_stop()` czyści zbuforowany zdalny punkt końcowy oraz aktywny
  kontekst wysyłania pakietu.
- Gdy `hal_wireguard` jest aktywny, datagramy do celów objętych
  trasą/AllowedIPs WireGuard są przenoszone przez zaszyfrowany tunel.

**impl/rp2040:** własny silnik surowego UDP lwIP JaszczurHAL ze statyczną
pulą gniazd.
**impl/esp32:** ograniczona pula uchwytów HAL nad natywnymi gniazdami UDP
ESP-IDF lwIP oraz gotowością/timeoutami `select()`.
**impl/.mock:** deterministyczny test double z wieloma gniazdami,
wstrzykiwanymi pakietami przychodzącymi oraz przechwyconymi metadanymi i
ładunkiem pakietów wychodzących.
**Thread safety:** Backendy rodziny RP i ESP32-S3 są thread-safe
i bezpieczne wielordzeniowo dla publicznych API. Mutexy lokalne dla backendu
chronią ich statyczne pule UDP oraz operacje stosu.

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

Oparte na uchwytach API transportu TCP dla wychodzących połączeń
strumieniowych oraz przychodzących gniazd nasłuchiwacza/serwera.

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
- `hal_tcp_socket_open()` alokuje gniazdo klienckie z
  `HAL_TCP_SOCKET_MAX_INSTANCES`; zamykaj nieużywane gniazda przez
  `hal_tcp_socket_close()`.
- `hal_tcp_socket_connect(...)` łączy się z punktem końcowym IPv4. Rodzina
  adresów musi być `HAL_NET_AF_INET`, a port musi być niezerowy.
- `timeout_ms == 0` oznacza natychmiastowe/nieblokujące odpytanie odbioru.
  `HAL_NET_TIMEOUT_FOREVER` żąda blokującego odbioru bez ustalonego limitu
  czasu.
- `hal_tcp_socket_send(...)` zwraca liczbę zaakceptowanych bajtów lub `<0`
  w przypadku błędu.
- `hal_tcp_socket_recv(...)` zwraca liczbę odczytanych bajtów, `0` przy
  timeoucie/braku danych/zamknięciu przez peera lub `<0` dla nieprawidłowych
  uchwytów/argumentów.
- `hal_tcp_socket_can_recv(...)` i `hal_tcp_socket_can_send(...)` to
  sondy gotowości, które nie pobierają danych, dla warstw zgodności takich jak BSD
  `select()`.
- `hal_tcp_socket_shutdown(...)` zatrzymuje we/wy, ale pozostawia uchwyt
  zaalokowany.
- `hal_tcp_socket_close(...)` zatrzymuje klienta backendu i zwraca uchwyt do
  puli statycznej.
- `hal_tcp_listener_open()` alokuje nasłuchiwacz z
  `HAL_TCP_LISTENER_MAX_INSTANCES`; zamykaj nieużywane nasłuchiwacze przez
  `hal_tcp_listener_close()`.
- `hal_tcp_listener_bind(...)` wiąże lokalny punkt końcowy IPv4. Rodzina
  adresów musi być `HAL_NET_AF_INET`, a port musi być niezerowy.
- `hal_tcp_listener_listen(...)` rozpoczyna akceptowanie klientów z
  niezerowym backlogiem. Przenośny mock ogranicza oczekujących klientów do
  `HAL_TCP_LISTENER_BACKLOG_MAX`; rzeczywiste backendy mogą stosować własny
  limit platformy.
- `hal_tcp_listener_accept(...)` zwraca połączony `hal_tcp_socket_t` ze
  zwykłej puli gniazd TCP. `timeout_ms == 0` odpytuje natychmiast, a
  `HAL_NET_TIMEOUT_FOREVER` żąda blokującego oczekiwania.
- `hal_tcp_listener_can_accept(...)` sonduje gotowość oczekujących klientów
  bez pobierania zaakceptowanego gniazda.
- `hal_tcp_listener_close(...)` zatrzymuje wyłącznie nasłuchiwacz. Już
  zaakceptowane gniazda klientów pozostają niezależne i muszą być zamknięte
  osobno.
- Gdy `hal_wireguard` jest aktywny, połączenia do celów objętych
  trasą/AllowedIPs WireGuard są przenoszone przez zaszyfrowany tunel.

**impl/rp2040:** własny silnik surowego TCP lwIP JaszczurHAL ze statycznymi
pulami gniazd i nasłuchiwaczy.
**impl/esp32:** ograniczona pula uchwytów HAL nad natywnymi gniazdami TCP
ESP-IDF lwIP, w tym connect z timeoutem, bind/listen/accept, shutdown oraz
gotowość `select()`.
**impl/.mock:** deterministyczny test double klienta/nasłuchiwacza ze
skryptowanym wynikiem connect, wstrzykiwanymi bajtami RX, przechwyconym
ładunkiem TX, przechwyconym zdalnym punktem końcowym oraz kolejkami
oczekujących klientów per-nasłuchiwacz.
**Thread safety:** Backendy rodziny RP i ESP32-S3 są thread-safe
i bezpieczne wielordzeniowo dla publicznych API. Mutexy lokalne dla backendu
chronią ich statyczne pule TCP oraz operacje stosu.

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

`hal_tls` to neutralna względem providera fasada klienta TLS ze sprawdzaniem
generacji, oparta na dołączonym silniku BearSSL. Włączenie jej automatycznie
włącza TCP i WiFi, ale nie włącza ani nie wymaga opcjonalnego adaptera
gniazd BSD.

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

`hal_tls_client_config_init()` wybiera wykonanie oparte na odpytywaniu
(poll), skończony 5-sekundowy timeout transportu, 15-sekundowy timeout
operacji oraz cztery kroki providera na odpytanie. Aplikacje mogą wybrać
`HAL_TLS_EXECUTION_BOUNDED_WORKER`, gdy dedykowany worker jest właścicielem
wszystkich skończonych wywołań blokujących. Oba pola timeout muszą pozostać
skończone i niezerowe.

Konfiguracja bezpieczeństwa wymaga co najmniej jednej kotwicy zaufania RSA
lub EC oraz callbacków czasu i entropii. `hal_tls_trust_anchor_from_der_ex()`
dekoduje certyfikat CA w formacie DER do stałej pamięci będącej własnością
wywołującego; wszystkie referencjonowane bufory zaufania muszą pozostać
żywe do zamknięcia klienta. `hal_tls_default_time()` wymaga wiarygodnego
zsynchronizowanego zegara, a `hal_tls_default_entropy()` używa bezpiecznego
providera entropii wybranego targetu.

BearSSL otrzymuje skonfigurowaną nazwę hosta do SNI i weryfikacji
tożsamości certyfikatu. Waliduje łańcuch, okres ważności certyfikatu oraz
nazwę hosta. Opcjonalne `server_public_key_sha256` dodaje pinowanie klucza
publicznego SHA-256 po walidacji certyfikatu. Callbacki anulowania i usługi
pozwalają długim operacjom zatrzymać się kooperacyjnie oraz utrzymywać
postęp sieci/watchdoga. Uchwyty klienta są sprawdzane generacyjnie;
zamknięcie zwalnia slot puli, a nieaktualne kopie pozostają nieważne.

Rdzeniowa ścieżka TLS rozwiązuje adresy przez `hal_net_resolve_ex()` i jest
właścicielem natywnego `hal_tcp_socket_t`. Postęp rekordów BearSSL używa
małego prywatnego interfejsu transportu zamiast deskryptorów POSIX. Dzięki
temu TLS pozostaje użyteczne, gdy `HAL_ENABLE_BSD_SOCKETS` jest wyłączone.

Gniazda BSD pozostają niezależnie obsługiwanymi transportami TLS. Gdy obie
flagi są włączone, adapter BearSSL BSD mapuje nieblokujące operacje
`send()`/`recv()` istniejącego deskryptora na ten sam prywatny interfejs
transportu BearSSL. Aplikacje i klienty TLS firm trzecich, które używają
we/wy BSD, nadal działają nad publicznym API gniazd; włączenie natywnego
`hal_tls` nie zmienia własności deskryptora ani semantyki BSD.

**Implementacja:**

- `hal_tls.cpp` jest właścicielem cyklu życia, rozwiązywania DNS, natywnego
  transportu HAL TCP oraz konfiguracji bezpieczeństwa niezależnej od
  providera;
- `hal/network/tls/BearSSL/jh_bearssl_hal_tcp_io.*` adaptuje HAL TCP;
- `hal/network/tls/BearSSL/jh_bearssl_bsd_io.*` to opcjonalny most
  TLS-nad-BSD;
- `hal/network/tls/BearSSL/jh_bearssl_engine.*` przesuwa rekordy przez
  dowolny z transportów, nie zależąc od żadnej z reprezentacji gniazd.

**Testy:** `test_hal_tls` obejmuje publiczny cykl życia, `test_bearssl_provider`
obejmuje zachowanie silnika niezależne od transportu natywnego oraz we/wy
TLS-nad-BSD, a sondy konfiguracji sprawdzają niezależny wybór TLS/BSD.
`tests/run_bearssl_native_integration.sh` tworzy tymczasowe CA RSA oraz
certyfikat serwera `localhost` z SAN-ami DNS/IP, uruchamia serwer OpenSSL na
loopback i weryfikuje poprawny przypadek oraz błędy niewłaściwego hosta,
przed okresem ważności i po okresie ważności. Wygenerowane klucze prywatne i
certyfikaty są usuwane przy zakończeniu.

---

## Adapter gniazd BSD  *(opt-in - `HAL_ENABLE_BSD_SOCKETS`)*

Włączenie tego modułu automatycznie włącza UDP, TCP i WiFi. Buildy CYW43 i
mocka używają minimalnej warstwy zgodności IPv4 BSD/POSIX nad `hal_udp` i
`hal_tcp`, opisanej poniżej. ESP32-S3 udostępnia natywne API BSD już
dostarczane przez ESP-IDF lwIP; współdzielony adapter celowo nie definiuje
konkurujących symboli gniazd na tym targecie.

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

**Zakres MVP współdzielonego adaptera:** `AF_INET`, `SOCK_STREAM`,
`SOCK_DGRAM`, protokoły TCP/UDP, `sockaddr_in`, pomocnicy kolejności
bajtów, konwersja tekst/binarnie dla IPv4 oraz jednowynikowe `getaddrinfo()`
dla IPv4. Wartości deskryptorów zaczynają się od `HAL_BSD_SOCKET_FD_BASE` i
są przechowywane w tabeli o rozmiarze `HAL_BSD_SOCKET_MAX_FDS`.

**Uwagi dotyczące zachowania:**
- `socket(AF_INET, SOCK_DGRAM, 0/IPPROTO_UDP)` odwzorowuje się na
  `hal_udp_socket_open()`.
- `socket(AF_INET, SOCK_STREAM, 0/IPPROTO_TCP)` odwzorowuje się na
  `hal_tcp_socket_open()`.
- Adapter dziedziczy zwykłe trasowanie `hal_udp`/`hal_tcp`. Gdy
  `hal_wireguard` jest aktywny, ruch do celów objętych trasą/AllowedIPs
  WireGuard jest przenoszony przez zaszyfrowany tunel. Jest to tunelowanie
  warstwy sieciowej i nie zastępuje gniazd TLS, które zapewniają szyfrowanie
  end-to-end na warstwie aplikacji/sesji.
- Gniazda BSD mogą być użyte jako transport dla bibliotek TLS. Dołączony
  adapter BearSSL BSD pozostaje dostępny, gdy wybrano zarówno
  `HAL_ENABLE_BSD_SOCKETS`, jak i `HAL_ENABLE_TLS`; natywna fasada `hal_tls`
  używa bezpośrednio HAL TCP i dlatego nie wymusza gniazd BSD w
  niepowiązanych buildach.
- UDP `sendto()` automatycznie wiąże się do efemerycznego portu lokalnego,
  gdy gniazdo nie zostało jawnie związane.
- UDP `connect()` zapisuje domyślny punkt końcowy peera i w razie potrzeby
  automatycznie wiąże gniazdo. Następnie `send()`/`write()` przesyłają
  datagramy do tego peera, natomiast `recv()`/`read()` odbierają datagramy
  bez zwracania adresu źródłowego. W przeciwieństwie do połączonego UDP w
  POSIX, adapter nie filtruje przychodzących datagramów według tego peera;
  akceptuje kolejny datagram dostarczony przez gniazdo HAL UDP.
- TCP `bind()` przygotowuje (stages) lokalny punkt końcowy; `listen()`
  konwertuje deskryptor na nasłuchiwacz HAL TCP. Zaakceptowani klienci
  otrzymują osobne deskryptory gniazd.
- `getaddrinfo(...)` rozwiązuje dosłowne adresy IPv4 lub nazwy hostów przez
  `hal_net_resolve_ipv4(...)`. `service` musi być numeryczne. Obsługiwane
  flagi podpowiedzi to `AI_PASSIVE`, `AI_CANONNAME`, `AI_NUMERICHOST`,
  `AI_NUMERICSERV` i `AI_ADDRCONFIG`; IPv6 pozostaje poza zakresem adaptera.
- `setsockopt(...)` przyjmuje `SOL_SOCKET` + `SO_REUSEADDR`/`SO_REUSEPORT`,
  `SO_RCVTIMEO` i `SO_SNDTIMEO`. `getsockopt(...)` zgłasza te wartości oraz
  `SO_ERROR`; odczyt `SO_ERROR` czyści zapisany błąd adaptera. Opcje
  timeoutu są przechowywane z rozdzielczością milisekundową, więc wartości
  `timeval` poniżej milisekundy mogą zostać zaokrąglone w górę przy
  odczycie.
- `getsockname(...)` zgłasza lokalny punkt końcowy znany adapterowi.
  Klienci TCP, którzy nie wykonali jawnego `bind()`, mogą zgłaszać
  `0.0.0.0:0`, ponieważ API HAL TCP nie ujawnia lokalnego portu przypisanego
  przez backend.
- `getpeername(...)` zgłasza połączonego peera TCP lub UDP, w tym gniazda
  TCP zwrócone przez `accept()`. Zawodzi z `ENOTCONN`, zanim peer jest
  znany.
- Wywołania blokujące domyślnie używają `HAL_NET_TIMEOUT_FOREVER`.
  `SO_RCVTIMEO` wpływa na `accept()`, `recv()`/`read()` i `recvfrom()`;
  `SO_SNDTIMEO` wpływa na wybór timeoutu dla `connect()`.
  `fcntl(F_SETFL, O_NONBLOCK)` sprawia, że `accept()`, `connect()`,
  `recv()`/`read()` i `recvfrom()` używają natychmiastowych odpytań HAL;
  `MSG_DONTWAIT` robi to samo per wywołanie dla `recv`, `recvfrom`, `send` i
  `sendto`.
- Minimalny `select()` obsługuje gotowość odczytu/zapisu dla deskryptorów
  gniazd HAL. `exceptfds` jest akceptowane i czyszczone; `poll()` pozostaje
  poza zakresem tego etapu.
- Nieblokujący `connect()` TCP działa na zasadzie best-effort, a nie jako
  pełny automat stanów oczekującego połączenia POSIX. Adapter wykonuje
  jedną natychmiastową próbę connect HAL. Jeśli się powiedzie, deskryptor
  staje się zapisywalny, a `SO_ERROR` wynosi zero. Jeśli nie zakończy się
  natychmiast, `connect()` zwraca `-1`/`EINPROGRESS` i zapisuje
  `EINPROGRESS` w `SO_ERROR`, ale żadne połączenie w tle nie pozostaje
  oczekujące; ponów `connect()` później lub użyj connect
  blokującego/opartego na timeoucie.
- Zamknięcie deskryptora z innego zadania, gdy blokujące `connect()`,
  `accept()`, `recv()` lub `recvfrom()` oczekuje, nie zapewnia
  asynchronicznego anulowania. Adapter zwalnia swoją blokadę tabeli fd
  podczas oczekiwania i ponownie waliduje deskryptory po powrocie wywołania
  backendu, ale wywołujący potrzebujący anulowalnych oczekiwań powinni
  używać `O_NONBLOCK` wraz z odpytywaniem `select()`.
- Nieobsługiwane flagi/operacje zawodzą z `errno`.

**wspólna implementacja tematyczna:** `hal/network/adapters/bsd/hal_bsd_sockets.cpp`
zawiera adapter tabeli fd, pomocników konwersji adresów oraz obsługę
resolvera `netdb.h`.
**impl/esp32:** natywne nagłówki i symbole BSD ESP-IDF lwIP; zachowanie
deskryptorów i opcji podąża za przypiętą konfiguracją ESP-IDF, a nie za
stałą tabelą fd współdzielonego adaptera.
**impl/.mock testy:** `test_bsd_sockets` obejmuje zachowanie i mapowanie
errno; `test_bsd_sockets_c_compile` weryfikuje, że proste kształty
klienta/serwera TCP/UDP w C, `getaddrinfo()` i `setsockopt()` kompilują się
i linkują względem nagłówków zgodności.

---

## `hal_wireguard` - opakowanie tunelu WireGuard  *(opt-in - `HAL_ENABLE_WIREGUARD`)*

Thread-safe fasada nad współdzielonym silnikiem WireGuard/lwIP.

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
- `hal_wireguard_parse_ipv4(...)` waliduje i parsuje tekst IPv4 z kropkami
  (`a.b.c.d`) na oktety.
- `hal_wireguard_begin(...)` używa trybu pełnego tunelu (full-tunnel)
  (`AllowedIPs = 0.0.0.0/0`).
- `hal_wireguard_begin_text(...)` parsuje tekstowy lokalny adres IP z
  kropkami i deleguje do `hal_wireguard_begin(...)`.
- `hal_wireguard_begin_advanced(...)` włącza tryb podzielonego tunelu
  (split-tunnel) przez jawne AllowedIPs.
- `hal_wireguard_begin_advanced_text(...)` parsuje tekstowe adresy IPv4 z
  kropkami dla local/allowed/mask i deleguje do
  `hal_wireguard_begin_advanced(...)`.
- `hal_wireguard_peer_up(...)` może opcjonalnie zwrócić bieżący IP/port
  punktu końcowego.
- `hal_wireguard_peer_up_quick(...)` to wygodne sprawdzenie bez argumentów,
  równoważne `hal_wireguard_peer_up(NULL, 0u, NULL)`.
- `hal_wireguard_kick_handshake(...)` wyzwala nieblokującą sondę handshake.
- `hal_wireguard_kick_handshake_text(...)` parsuje tekstowy adres IP sondy z
  kropkami i deleguje do `hal_wireguard_kick_handshake(...)`.

**wspólna implementacja tematyczna:** dołączony silnik protokołu/kryptografii
wraz z prywatnym portem rozszerzenia lwIP używanym przez backendy stosu
hosta reklamujące tę możliwość.
**impl/rp2040:** rozszerzenie lwIP będące własnością HAL oraz bezpieczne
hooki platformy.
**impl/stm32g474:** współdzielona warstwa spodnia CYW43/lwIP, entropia ze
sprzętowego RNG oraz czas NTP zsynchronizowany z HAL.
**impl/esp32:** współdzielony silnik WireGuard nad natywną warstwą spodnią
ESP-IDF lwIP, z jawnym blokowaniem stosu/dostępem do netif, natywnym
resolverem, bezpieczną entropią ESP oraz zsynchronizowanym czasem libc dla
handshake'ów TAI64N.
**impl/.mock:** deterministyczny stanowy test double z przechwyconą
konfiguracją, wstrzykiwaniem punktu końcowego peera oraz obserwowalnością
wyzwalania handshake.
**Thread safety:** singletonowy `hal_mutex_t` serializuje wszystkie
publiczne wywołania wrappera; wybrany backend serializuje dostęp do
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

Thread-safe wrapper MQTT wokół dołączonego PubSubClient, z dispatchem
callbacków poza wewnętrznym mutexem, aby uniknąć deadlocków wynikających z
kolejności blokad w handlerach użytkownika.

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
- Z `HAL_ENABLE_TLS`, wywołaj `hal_mqtt_configure_tls_ex()` przed
  połączeniem, aby włączyć MQTTS. Referencjonowane kotwice zaufania i
  callbacki podlegają regułom cyklu życia `hal_tls`. Rekonfiguracja zamyka
  bieżący transport. `hal_mqtt_disable_tls_ex()` również rozłącza i
  przywraca przyszłe połączenia do zwykłego MQTT.
- MQTTS tworzy sprawdzanego generacyjnie klienta TLS w trybie
  bounded-worker. Timeout gniazda MQTT wyznacza deadline transportu TLS i
  operacji; connect jest odpytywany aż do zakończenia, a
  następnie read/write używają klienta TLS aż do rozłączenia.
- `hal_mqtt_loop()` musi być regularnie odpytywane, aby napędzać keepalive i
  odbierać przychodzące publikacje.
- Wiadomości przychodzące są kopiowane do wewnętrznego bufora i dostarczane
  z `hal_mqtt_loop()` po zwolnieniu wewnętrznego mutexu.

**impl/rp2040/stm32g474/esp32:** dołączony `PubSubClient`
(`frameworks/PubSubClient`) nad `hal_tcp` lub klientem BearSSL `hal_tls`.
**impl/.mock:** deterministyczny stanowy test double z możliwym do
wstrzyknięcia wynikiem connect, wynikiem loop oraz wiadomościami
przychodzącymi.
**Thread safety:** Singletonowy `hal_mutex_t` serializuje wszystkie
wywołania klienta MQTT. Callbacki są dostarczane po zwolnieniu wewnętrznego
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

Czyste (pure) pomocniki używają współdzielonego rdzenia kalendarza
proleptycznie gregoriańskiego. Konwersja komponentów akceptuje daty od
epoki Unix aż do ostatniej sekundy reprezentowalnej przez `uint32_t`; jej
wartość zwracana dla zgodności wynosi `0` zarówno dla błędu, jak i dla
ważnego początku epoki. Pomocnik CET/CEST używa starszej (legacy) polityki
opartej wyłącznie na dacie: czas letni zaczyna się w ostatnią niedzielę
marca (włącznie) i kończy w ostatnią niedzielę października (wyłącznie),
przy czym każde przejście następuje o 00:00, ponieważ żaden argument pory
dnia nie jest dostępny. Nieprawidłowe daty są odrzucane, a dostosowanie
normalizuje przekroczenie zakresu dnia/miesiąca/roku.

`hal_time_is_in_range()` implementuje półotwarty przedział `[start, end)`.
`hal_time_extract_minutes()` używa semantyki ilorazu/reszty z C i akceptuje
każdy ze wskaźników wyjściowych jako opcjonalny.

**Wspólna implementacja:** `hal/time/hal_time_ntp.cpp` jest jedynym
właścicielem zegara ściennego (wall-clock) w runtime.
`hal_time_set_unix_ex()` jest wspólnym setterem używanym przez wywołujących
ręcznie, przywracanie z RTC, NTP oraz adaptery libc targetu. Zegar postępuje
na bazie 64-bitowej podstawy monotonicznej w mikrosekundach, więc
zawinięcie 32-bitowego licznika milisekund nie cofa czasu ściennego.
Adaptery `gettimeofday()` i `settimeofday()` dla RP i STM32G474 odczytują i
aktualizują ten sam stan, zamiast utrzymywać drugą epokę programową.

`hal_time_status_t` zwraca jeden spójny snapshot: ważność, źródło, sekundy i
mikrosekundy Unix, stan NTP, ostatni wynik NTP i epokę synchronizacji, a
także pola dołączenia/wyniku RTC. `HAL_TIME_NTP_IN_PROGRESS` używa
`HAL_EAGAIN`; `HAL_TIME_NTP_FAILED` zachowuje konkretny błąd transportu lub
timeoutu. `HAL_TIME_NTP_IDLE` nie ma historii i zgłasza `HAL_NONE`. Pozwala
to aplikacji odróżnić ważny zegar przywrócony z RTC od zakończenia nowego
żądania NTP.

Przy włączonym RTC dołącz RTC będący własnością wywołującego, używając
`HAL_TIME_RTC_RESTORE_IF_VALID`, `HAL_TIME_RTC_WRITE_AFTER_NTP` lub obu
naraz. Ważny RTC zasila (seeduje) wyłącznie nieustawiony zegar czasu
działania. Nieważny RTC pozostaje dołączony i jest inicjalizowany przez
następny zwalidowany wynik NTP. Błąd odczytu przy przywracaniu również
zachowuje dołączenie i jest ujawniany przez `last_rtc_status`. Uchwyt RTC
musi przeżyć dołączenie; `hal_time_detach_rtc_ex()` czeka na zakończenie
dowolnego aktywnego zapisu utrwalania NTP przed powrotem, po czym
wywołujący może zdeinicjalizować RTC.

**Thread safety:** Czyste pomocniki są reentrantne. Opcjonalne API
systemowe/NTP używają snapshotów stanu chronionych mutexem i obsługują
współbieżne zadania/rdzenie. We/wy DNS, UDP i RTC działa bez mutexu stanu
zegara ściennego, więc callback usługi sieciowej może ponownie wejść do
gettera czasu bez deadlocku. Wywołanie dowolnego gettera czasu lub
`hal_time_get_status_ex()` obsługuje oczekujące żądanie; 5-sekundowy
timeout podstawowy uruchamia opcjonalny sekundarny.

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
