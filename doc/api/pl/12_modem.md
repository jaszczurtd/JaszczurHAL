# Modem komórkowy

*Dostępne również [po angielsku](../en/12_modem.md).*

> **Część [Dokumentacji API JaszczurHAL](../../pl/JaszczurHAL_API.md)**

Obejmuje: `hal_modem_at`, `hal_simcom_a76xx`.

## `hal_modem_at` - ogólny silnik poleceń AT  *(wspólne API - `HAL_ENABLE_CELLULAR_MODEM`)*

Warstwa transportowa stosu modemu komórkowego zarządza UART-em, buforem odbiorczym i
stanem protokołu. Sekwencje uruchamiania, maszyna stanów i składnia poleceń właściwe dla
producenta znajdują się w driverach poszczególnych rodzin, obecnie w
`hal_simcom_a76xx`.

```c
#include <hal/modem/hal_modem_at.h>

typedef enum {
    HAL_MODEM_AT_OK = 0,
    HAL_MODEM_AT_ERROR,
    HAL_MODEM_AT_TIMEOUT,
    HAL_MODEM_AT_NO_PROMPT,
    HAL_MODEM_AT_INVALID_ARG,
    HAL_MODEM_AT_BUSY
} hal_modem_at_result_t;

typedef hal_modem_at_impl_t *hal_modem_at_t;

typedef struct {
    hal_uart_t  uart;
    char       *rx_buf;
    size_t      rx_buf_size;
    uint32_t    default_timeout_ms;
    uint32_t    quiet_window_ms;
} hal_modem_at_config_t;

typedef void (*hal_modem_at_urc_cb_t)(const char *line, void *user);
typedef bool (*hal_modem_at_ready_cb_t)(const char *buf, size_t len, void *user);

hal_modem_at_t hal_modem_at_create(const hal_modem_at_config_t *cfg);
void           hal_modem_at_destroy(hal_modem_at_t h);

hal_modem_at_result_t hal_modem_at_send(hal_modem_at_t h, const char *cmd,
                                        const char *expected, uint32_t timeout_ms);
hal_modem_at_result_t hal_modem_at_send_with_data(hal_modem_at_t h, const char *cmd,
                                                  const uint8_t *data, size_t data_len,
                                                  uint32_t prompt_timeout_ms,
                                                  uint32_t resp_timeout_ms);
hal_modem_at_result_t hal_modem_at_listen_until(hal_modem_at_t h,
                                                hal_modem_at_ready_cb_t ready,
                                                void *user,
                                                uint32_t total_timeout_ms);

const char *hal_modem_at_last_response(hal_modem_at_t h);

bool hal_modem_at_urc_register(hal_modem_at_t h, const char *prefix,
                               hal_modem_at_urc_cb_t cb, void *user);
int  hal_modem_at_urc_poll(hal_modem_at_t h);
void hal_modem_at_set_log_filter(hal_modem_at_t h,
                                 const char *const *secrets, size_t count);
void hal_modem_at_set_line_observer(hal_modem_at_t h,
                                    hal_modem_at_urc_cb_t cb, void *user);

typedef void (*hal_modem_at_tick_cb_t)(void *user);
void hal_modem_at_set_tick_callback(hal_modem_at_t h,
                                    hal_modem_at_tick_cb_t cb, void *user);
void hal_modem_at_sleep_ms(hal_modem_at_t h, uint32_t ms);
```

- **Backend:** Wszystkie targety sprzętowe i mock korzystają z jednej implementacji
  `src/hal/modem/hal_modem_at.cpp`, opartej wyłącznie na `hal_uart`, `hal_millis` i
  `hal_mutex`.

**Thread safety:** Każdy uchwyt ma własny mutex serializujący dostęp. API można bezpiecznie
wywoływać z wielu wątków lub rdzeni.

- **Współpraca z watchdogiem:** każda wewnętrzna pętla odpytywania (send,
  send_with_data, listen_until) oraz każde oczekiwanie wyższego poziomu zbudowane na
  silniku, na przykład `hal_simcom_a76xx_wait_*` i impulsy zasilania, wywołuje
  callback ticku zarejestrowany przez `hal_modem_at_set_tick_callback()` co
  najmniej co ~20 ms. Zarejestruj callback, który wywołuje `hal_watchdog_feed()`
  (i opcjonalnie odświeża diodę statusu), aby regularnie odświeżać watchdog
  aplikacji podczas długich sekwencji rozruchu modemu.

---

## `hal_simcom_a76xx` - driver modemu SimCom A76xx  *(opcjonalny - `HAL_ENABLE_A7670`)*

Driver wysokiego poziomu dla modemów z rodziny SimCom A76xx (A7670E/SA/G, A7672E/S,
A7608, ...), zbudowany na `hal_modem_at`. Steruje zasilaniem i synchronizuje rozruch,
uruchamia kartę SIM i rejestrację w sieci, zestawia kontekst PDP oraz pobiera czas
sieciowy, przybliżoną lokalizację na podstawie sieci komórkowej (LBS) i pozycję GNSS.
Udostępnia też kompletnego klienta MQTT z publikacją i subskrypcją, opartego na
poleceniach `CMQTT*`.

```c
#include <hal/modem/hal_simcom_a76xx.h>

typedef enum {
    HAL_SIMCOM_A76XX_OK = 0,
    HAL_SIMCOM_A76XX_ERROR,
    HAL_SIMCOM_A76XX_TIMEOUT,
    HAL_SIMCOM_A76XX_INVALID_ARG,
    HAL_SIMCOM_A76XX_NOT_READY,
    HAL_SIMCOM_A76XX_PARSE
} hal_simcom_a76xx_result_t;

typedef hal_simcom_a76xx_impl_t *hal_simcom_a76xx_t;

typedef struct {
    hal_uart_t uart;
    int        pwr_pin;   // GPIO sterowania zasilaniem (stan bezczynności HIGH, aktywny impuls LOW), -1 aby wyłączyć
    char      *rx_buf;
    size_t     rx_buf_size;
    uint32_t   default_at_timeout_ms;
} hal_simcom_a76xx_config_t;

typedef struct {
    const char *apn;
    const char *user;      // może być NULL
    const char *password;  // może być NULL
} hal_simcom_a76xx_apn_t;

typedef struct {
  float latitude_deg;
  float longitude_deg;
  int   accuracy_m;   // -1, gdy odpowiedź modemu pomija dokładność
  float speed_kmh;    // szacowane przez HAL na podstawie kolejnych namiarów (fixów), -1 gdy niedostępne
} hal_simcom_a76xx_cell_location_t;

typedef struct {
  double latitude_deg;
  double longitude_deg;
  double altitude_m;      // -1 gdy niedostępne
  double speed_kmh;       // -1 gdy niedostępne
  double course_deg;      // -1 gdy niedostępne
  double hdop;            // -1 gdy niedostępne
  double pdop;            // -1 gdy niedostępne
  double vdop;            // -1 gdy niedostępne
  int    satellites_used; // -1 gdy niedostępne
  int    satellites_view; // -1 gdy niedostępne
  int    fix_mode;        // status specyficzny dla modemu, -1 gdy niedostępne
  char   utc[24];         // tekst UTC modemu lub pusty
} hal_simcom_a76xx_gnss_location_t;

typedef struct {
    bool        enabled;
    int         ssl_context_id;
    const char *ca_cert_name;       // wczytany wcześniej przez AT+CCERTDOWN lub NULL
    bool        ignore_local_time;
    bool        enable_sni;
    int         sslversion;         // 0..4, domyślnie 4 (TLS 1.2)
    int         authmode;           // 0..3, domyślnie 1 (tylko serwer)
} hal_simcom_a76xx_ssl_config_t;

typedef struct {
    const char *broker_host;
    uint16_t    broker_port;
    const char *client_id;
    const char *username;          // może być NULL
    const char *password;          // może być NULL
    uint16_t    keepalive_s;
    bool        clean_session;
    int         client_index;      // 0..1 (sloty SimCom CMQTT)
    hal_simcom_a76xx_ssl_config_t ssl;
} hal_simcom_a76xx_mqtt_config_t;

typedef void (*hal_simcom_a76xx_mqtt_message_cb_t)(int client_index,
                                                   const char *topic,
                                                   const uint8_t *payload,
                                                   size_t payload_len,
                                                   void *user);

/* Cykl życia */
hal_simcom_a76xx_t hal_simcom_a76xx_create(const hal_simcom_a76xx_config_t *cfg);
void                hal_simcom_a76xx_destroy(hal_simcom_a76xx_t h);
hal_modem_at_t      hal_simcom_a76xx_get_at(hal_simcom_a76xx_t h);

/* Zasilanie (opcjonalny pomocnik).

   Przebieg jest następujący: bezczynność HIGH -> aktywny impuls LOW -> HIGH. Użyj pwr_pin
   dla każdej płytki, której wejście sterowania zasilaniem odpowiada tej polaryzacji:
     - SimCom PWRKEY przez tranzystor, LUB
     - ENABLE przekaźnika / load-switcha, gdzie HIGH oznacza "zasilany"
       (pojedyncze power_toggle() jest wtedy pełnym fizycznym cyklem zasilania).

   Ustaw pwr_pin = -1 (i steruj GPIO z kodu aplikacji), gdy płytka
   używa odwróconej polaryzacji, wymaga bardziej rozbudowanej sekwencji
   zasilania lub uruchamiasz testy jednostkowe ze skryptem UART. W takim
   przypadku oba poniższe helpery nic nie robią (no-op).

   hard_reset() to sekwencja SimCom PWRKEY "wymuś wyłączenie, a następnie włączenie"
   (dwa impulsy + odstępy 5 s); dla płytek bramkowanych przekaźnikiem zwykle
   wystarczy pojedyncze power_toggle() - nie wykonuj podwójnego cyklu zasilania. */
hal_simcom_a76xx_result_t hal_simcom_a76xx_power_toggle(hal_simcom_a76xx_t h,
                                                        uint32_t pulse_ms);
hal_simcom_a76xx_result_t hal_simcom_a76xx_hard_reset(hal_simcom_a76xx_t h);

/* Rozruch / SIM / Sieć */
hal_simcom_a76xx_result_t hal_simcom_a76xx_wait_boot(hal_simcom_a76xx_t h,
                                                     uint32_t total_timeout_ms);
hal_simcom_a76xx_result_t hal_simcom_a76xx_init(hal_simcom_a76xx_t h);
hal_simcom_a76xx_result_t hal_simcom_a76xx_wait_sim_ready(hal_simcom_a76xx_t h,
                                                          uint32_t timeout_ms);
hal_simcom_a76xx_result_t hal_simcom_a76xx_wait_network_registered(hal_simcom_a76xx_t h,
                                                                   uint32_t timeout_ms);
hal_simcom_a76xx_result_t hal_simcom_a76xx_attach_pdp(hal_simcom_a76xx_t h,
                                                      const hal_simcom_a76xx_apn_t *apn);

/* Czas */
hal_simcom_a76xx_result_t hal_simcom_a76xx_get_network_time_iso8601(hal_simcom_a76xx_t h,
                                                                    char *out,
                                                                    size_t out_size);
hal_simcom_a76xx_result_t hal_simcom_a76xx_get_cell_location(hal_simcom_a76xx_t h,
                                                             hal_simcom_a76xx_cell_location_t *out_location,
                                                             uint32_t timeout_ms);
void hal_simcom_a76xx_gnss_location_init(hal_simcom_a76xx_gnss_location_t *loc);
hal_simcom_a76xx_result_t hal_simcom_a76xx_gnss_power_on(hal_simcom_a76xx_t h,
                                                         uint32_t timeout_ms);
bool hal_simcom_a76xx_gnss_is_powered(hal_simcom_a76xx_t h);
hal_simcom_a76xx_result_t hal_simcom_a76xx_get_gnss_location(hal_simcom_a76xx_t h,
                                                             hal_simcom_a76xx_gnss_location_t *out_location,
                                                             uint32_t timeout_ms);

/* MQTT */
hal_simcom_a76xx_result_t hal_simcom_a76xx_mqtt_connect(hal_simcom_a76xx_t h,
                                                        const hal_simcom_a76xx_mqtt_config_t *cfg);
const char *hal_simcom_a76xx_mqtt_result_string(int result_code);
int hal_simcom_a76xx_mqtt_last_connect_result(hal_simcom_a76xx_t h,
                                              int client_index);
hal_simcom_a76xx_result_t hal_simcom_a76xx_mqtt_disconnect(hal_simcom_a76xx_t h,
                                                           int client_index);
hal_simcom_a76xx_result_t hal_simcom_a76xx_mqtt_publish(hal_simcom_a76xx_t h,
                                                        int client_index,
                                                        const char *topic,
                                                        const void *payload,
                                                        size_t payload_len,
                                                        int qos);
hal_simcom_a76xx_result_t hal_simcom_a76xx_mqtt_subscribe(hal_simcom_a76xx_t h,
                                                          int client_index,
                                                          const char *topic,
                                                          int qos);
hal_simcom_a76xx_result_t hal_simcom_a76xx_mqtt_unsubscribe(hal_simcom_a76xx_t h,
                                                            int client_index,
                                                            const char *topic);
hal_simcom_a76xx_result_t hal_simcom_a76xx_mqtt_set_message_callback(hal_simcom_a76xx_t h,
                                                                     hal_simcom_a76xx_mqtt_message_cb_t cb,
                                                                     void *user);
int  hal_simcom_a76xx_mqtt_poll(hal_simcom_a76xx_t h);
bool hal_simcom_a76xx_mqtt_is_connected(hal_simcom_a76xx_t h, int client_index);
```

Helpery GNSS ujednolicają popularne warianty odpowiedzi SimCom:
`+CGNSSINFO`, `+CGNSINF` i `+CGPSINFO`. `hal_simcom_a76xx_get_gnss_location()`
najpierw upewnia się, że GNSS jest włączony, a następnie odpytuje o pozycję. Zwraca
`HAL_SIMCOM_A76XX_NOT_READY`, gdy modem odpowiada poprawnie, ale nie wyznaczył jeszcze pozycji,
na przykład `+CGNSSINFO: ,,,,,,,,`. W wariancie odpowiedzi A7670E
`+CGNSSINFO: <fix>,<sat_count>,...` pojedyncza liczba satelitów jest raportowana zarówno
jako `satellites_used`, jak i `satellites_view`.

**Odbiór subskrypcji MQTT:** Przychodzące komunikaty docierają jako sekwencja czterech URC
(`+CMQTTRXSTART:` / `+CMQTTRXTOPIC:` / `+CMQTTRXPAYLOAD:` / `+CMQTTRXEND:`) przeplatanych
z osobnymi liniami tematu i payloadu. Driver składa je z powrotem w jeden komunikat, a
aplikacja otrzymuje pojedyncze wywołanie `hal_simcom_a76xx_mqtt_message_cb_t`
z wnętrza `hal_simcom_a76xx_mqtt_poll()`.

`+CMQTTCONNECT: <client>,<result>` jest dekodowane przez driver. Nieudane połączenia
generują czytelną diagnostykę konsolową, na przykład:

```text
ERROR! [SIMCOM][MQTT] connect failed: socket connect failed (client=0, code=3)
```

Po bezpośredniej odpowiedzi modemu logowane jest również udane połączenie:

```text
[SIMCOM][MQTT] connected successfully (client=0, code=0)
```

Wynik liczbowy pozostaje dostępny przez `hal_simcom_a76xx_mqtt_last_connect_result()`,
natomiast `hal_simcom_a76xx_mqtt_result_string()` udostępnia pełną tabelę wyników SimCom
do celów diagnostyki aplikacji. Wynik `3` oznacza niepowodzenie połączenia gniazda przed
uwierzytelnieniem MQTT; błędna nazwa użytkownika/hasło to `30`, odrzucona autoryzacja to
`31`, a niepowodzenie handshake'u TLS to `32`.

**Thread safety:** Dostęp przez każdy uchwyt jest serializowany mutexem
`hal_modem_at`. API można bezpiecznie wywoływać z wielu wątków lub rdzeni.

---


---

*Dalej: [Urządzenia wyjściowe](13_output_devices.md)*
