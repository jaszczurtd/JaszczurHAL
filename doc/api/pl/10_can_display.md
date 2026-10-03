<a id="magistrala-can-i-wyświetlacz"></a>

# CAN i wyświetlacze

*Dostępne również [po angielsku](../en/10_can_display.md).*

> **Część [Dokumentacji API JaszczurHAL](../../pl/JaszczurHAL_API.md)**

Rozdział opisuje komunikację CAN, obsługę wyświetlaczy znakowych HD44780 oraz rysowanie i przesyłanie obrazu do wyświetlaczy graficznych.

<a id="hal_can---magistrala-can--opcjonalny---hal_enable_can-backendy-hal_enable_mcp2515--hal_enable_mcp251xfd--hal_enable_stm32g474_fdcan"></a>

## `hal_can` - komunikacja CAN i CAN FD  *(opcjonalny - `HAL_ENABLE_CAN`, backendy `HAL_ENABLE_MCP2515` / `HAL_ENABLE_MCP251XFD` / `HAL_ENABLE_STM32G474_FDCAN`)*

Wysyłanie i odbiór ramek CAN przez kontroler MCP2515, MCP251XFD lub wewnętrzny FDCAN STM32G474. Obsługa CAN FD zależy od kontrolera i jego konfiguracji; MCP2515 obsługuje wyłącznie klasyczny CAN.

```c
#include <hal/can/hal_can.h>

#define HAL_CAN_MAX_DATA_LEN 8
#define HAL_CAN_FD_MAX_DATA_LEN 64
#define HAL_CAN_DLC_INVALID 0xFFu
#define HAL_CAN_STD_ID_MASK 0x7FFu
#define HAL_CAN_EXT_ID_MASK 0x1FFFFFFFu
#define HAL_CAN_MAX_FILTERS 6u
#define HAL_CAN_NO_INT_PIN   0xFF
#define HAL_CAN_ID_EXTENDED_FLAG 0x80000000u
#define HAL_CAN_ID_RTR_FLAG      0x40000000u

// Uchwyt do struktury z ukrytymi polami (ang. opaque handle).
// Po jednym na fizyczną instancję kontrolera CAN.
typedef hal_can_impl_t *hal_can_t;
typedef void (*hal_can_frame_cb_t)(uint32_t id, uint8_t len, const uint8_t *data);

typedef enum {
    HAL_CAN_BACKEND_MCP2515 = 0,
    HAL_CAN_BACKEND_MCP251XFD = 1,
    HAL_CAN_BACKEND_STM32G474_FDCAN = 2
} hal_can_backend_t;

enum {
    HAL_CAN_FRAME_EXTENDED = 0x01u,
    HAL_CAN_FRAME_RTR      = 0x02u,
    HAL_CAN_FRAME_FD       = 0x04u,
    HAL_CAN_FRAME_BRS      = 0x08u,
    HAL_CAN_FRAME_ESI      = 0x10u
};

typedef struct {
    uint32_t id;
    uint8_t dlc;
    uint8_t len;
    uint8_t flags;
    uint8_t data[HAL_CAN_FD_MAX_DATA_LEN];
} hal_can_frame_t;

enum {
    HAL_CAN_FILTER_EXTENDED = 0x01u
};

typedef struct {
    uint32_t id;
    uint32_t mask;
    uint8_t flags;
} hal_can_filter_t;

typedef uint32_t hal_can_mode_t;

enum {
    HAL_CAN_MODE_NORMAL      = 0x00u,
    HAL_CAN_MODE_LOOPBACK    = 0x01u,
    HAL_CAN_MODE_LISTEN_ONLY = 0x02u,
    HAL_CAN_MODE_FD          = 0x04u,
    HAL_CAN_MODE_ONE_SHOT    = 0x08u,
    HAL_CAN_MODE_SLEEP       = 0x10u,
    HAL_CAN_MODE_EXTERNAL_LOOPBACK = 0x20u
};

enum {
    HAL_CAN_TDC_AUTO   = 0u,
    HAL_CAN_TDC_OFF    = 1u,
    HAL_CAN_TDC_MANUAL = 2u
};

typedef enum {
    HAL_CAN_STATE_ERROR_ACTIVE = 0,
    HAL_CAN_STATE_ERROR_WARNING,
    HAL_CAN_STATE_ERROR_PASSIVE,
    HAL_CAN_STATE_BUS_OFF,
    HAL_CAN_STATE_STOPPED
} hal_can_state_t;

typedef struct {
    uint8_t tx;
    uint8_t rx;
} hal_can_error_counters_t;

typedef struct {
    uint8_t spi_bus;
    uint8_t cs_pin;
    uint32_t bitrate_hz;
    uint32_t oscillator_hz;
    bool one_shot_tx;
    bool sleep_wakeup;
} hal_can_mcp2515_config_t;

typedef struct {
    uint8_t spi_bus;
    uint8_t cs_pin;
    uint32_t arbitration_bitrate_hz;
    uint32_t data_bitrate_hz;
    uint32_t oscillator_hz;
    uint32_t spi_clock_hz;
    uint16_t arbitration_sample_point_permille;
    uint16_t data_sample_point_permille;
    bool enable_fd;
    bool one_shot_tx;
    bool sleep_wakeup;
} hal_can_mcp251xfd_config_t;

typedef struct {
    uint8_t instance;
    uint8_t rx_pin;
    uint8_t tx_pin;
    bool has_standby;
    uint8_t standby_pin;
    bool standby_high;
    uint32_t arbitration_bitrate_hz;
    uint32_t data_bitrate_hz;
    uint16_t arbitration_sample_point_permille;
    uint16_t data_sample_point_permille;
    uint8_t tdc_mode;
    uint8_t tdc_offset;
    uint32_t transceiver_max_bitrate_hz;
    bool enable_fd;
    bool one_shot_tx;
} hal_can_stm32g474_fdcan_config_t;

typedef struct {
    hal_can_backend_t backend;
    union {
        hal_can_mcp2515_config_t mcp2515;
        hal_can_mcp251xfd_config_t mcp251xfd;
        hal_can_stm32g474_fdcan_config_t stm32g474_fdcan;
    };
} hal_can_config_t;

// Wartość domyślna zależy od włączonych backendów. Jeśli włączono kilka,
// domyślna konfiguracja zgodności pochodzi najpierw z MCP2515, potem
// z MCP251XFD, a na końcu ze STM32G474 FDCAN.
// MCP2515: magistrala SPI 0, pin CS 0, 500 kbps / kryształ 8 MHz.
// MCP251XFD: magistrala SPI 0, pin CS 0, arbitraż 500 kbit/s, dane 2 Mbit/s.
// STM32G474 FDCAN: FDCAN1 na PA11/PA12, arbitraż 500 kbit/s, dane 2 Mbit/s.
hal_can_config_t hal_can_default_config(void);

// Konfiguracja kanału CAN `channel` (od 0) zadeklarowanego w profilu płytki:
// kontroler, instancja, piny, standby, limit transceivera, 500 kbit/s z fazą
// danych FD 2 Mbit/s. HAL_ENOENT, gdy płytka nie ma takiego kanału.
hal_status_t hal_can_board_config(uint8_t channel, hal_can_config_t *out);

// Każde wywołanie, które może się nie udać, zwraca hal_status_t: HAL_OK albo
// błąd mówiący, co poszło źle (zob. „Statusy wyników" niżej).

// Tworzy i inicjalizuje kanał CAN na podstawie konfiguracji. NULL używa
// konfiguracji domyślnej.
hal_status_t hal_can_create(const hal_can_config_t *cfg, hal_can_t *out);

// Zwalnia wszystkie zasoby; uchwyt nie może być używany po tym wywołaniu
void hal_can_destroy(hal_can_t h);

// Wysyła klasyczną ramkę CAN i czeka na nią: ID 11-bitowe albo 29-bitowe
// z HAL_CAN_ID_EXTENDED_FLAG; HAL_CAN_ID_RTR_FLAG wysyła ramkę zdalną.
hal_status_t hal_can_send(hal_can_t h, uint32_t id, uint8_t len,
                          const uint8_t *data);

// Wysyła ramkę CAN/CAN FD i czeka na nią. CAN FD wymaga HAL_CAN_MODE_FD
// w bieżącym trybie; MCP2515 akceptuje wyłącznie klasyczne ramki CAN.
hal_status_t hal_can_send_frame(hal_can_t h, const hal_can_frame_t *frame);

// Odczytuje kolejną klasyczną ramkę; HAL_EAGAIN, gdy żadna nie czeka. Ramki
// rozszerzone i zdalne mają w id flagi HAL_CAN_ID_*.
hal_status_t hal_can_receive(hal_can_t h, uint32_t *id, uint8_t *len,
                             uint8_t *data);

// Odczytuje kolejną ramkę CAN/CAN FD; HAL_EAGAIN, gdy żadna nie czeka.
hal_status_t hal_can_receive_frame(hal_can_t h, hal_can_frame_t *frame);

// Start/stop oraz tryby kontrolera. Nowe uchwyty są domyślnie uruchomione.
hal_status_t hal_can_start(hal_can_t h);
hal_status_t hal_can_stop(hal_can_t h);
hal_status_t hal_can_set_mode(hal_can_t h, hal_can_mode_t mode);
hal_status_t hal_can_get_mode(hal_can_t h, hal_can_mode_t *mode);

// Stan kontrolera i diagnostyka.
hal_status_t hal_can_get_state(hal_can_t h, hal_can_state_t *state);
hal_status_t hal_can_get_error_counters(hal_can_t h,
                                        hal_can_error_counters_t *counters);

// Sprawdzenie nieblokujące: HAL_OK, gdy czeka ramka, HAL_EAGAIN, gdy nie.
hal_status_t hal_can_available(hal_can_t h);

// Konfiguruje sprzętowe filtry RX dla dwóch akceptowanych standardowych
// 11-bitowych ID.
// Niepasujące ID są odrzucane przez backendy ze sprzętowym wsparciem filtrów.
hal_status_t hal_can_set_std_filters(hal_can_t h, uint32_t id0, uint32_t id1);

// Konfiguruje jeden slot filtra akceptacji z id/maską/flagami; bez miejsca
// na nowy filtr zostaje stary, a wywołanie zwraca HAL_ENOMEM.
hal_status_t hal_can_set_filter(hal_can_t h, uint8_t index,
                                const hal_can_filter_t *filter);

// Pomocnik tworzenia z ponawianiem prób, z opcjonalną konfiguracją pinu IRQ;
// zwraca błąd ostatniej próby.
hal_status_t hal_can_create_with_retry(const hal_can_config_t *cfg,
                                       uint8_t int_pin,
                                       void (*isr)(void),
                                       int max_retries,
                                       void (*retry_idle)(void),
                                       hal_can_t *out);

// Opróżnia oczekujące ramki RX i wywołuje callback dla każdej poprawnej.
// HAL_OK po opróżnieniu bufora albo błąd odbioru, który przerwał
// opróżnianie; delivered (może być NULL) dostaje liczbę ramek przekazanych
// do cb.
hal_status_t hal_can_process_all(hal_can_t h, hal_can_frame_cb_t cb,
                                 uint32_t *delivered);

// Pomocnicy DLC dla CAN/CAN FD. bytes_to_dlc() zaokrągla w górę do
// najbliższej reprezentowalnej długości CAN FD i zwraca HAL_CAN_DLC_INVALID
// dla wartości >64 bajtów.
uint8_t hal_can_dlc_to_bytes(uint8_t dlc);
uint8_t hal_can_bytes_to_dlc(uint8_t bytes);
// HAL_OK albo HAL_EINVAL.
hal_status_t hal_can_validate_frame(const hal_can_frame_t *frame);
hal_status_t hal_can_validate_filter(const hal_can_filter_t *filter);
// HAL_OK z ustawionym *matches, HAL_EINVAL dla błędnej ramki albo filtra.
hal_status_t hal_can_frame_matches_filter(const hal_can_frame_t *frame,
                                          const hal_can_filter_t *filter,
                                          bool *matches);

// Koduje temperaturę w °C jako bajt payloadu CAN typu signed int8.
// Obcina w stronę zera, saturuje do zakresu [-128, 127], zwraca bajt w
// zapisie uzupełnienia do dwóch (two's complement).
uint8_t hal_can_encode_temp_i8(float temp_c);
```

- **Wspólna implementacja modułu:** `hal/can/hal_can.cpp` zawiera publiczną warstwę
  CAN dla wszystkich targetów, zarządza cyklem życia uchwytów i muteksami oraz
  kieruje wywołania do backendu. Pliki `hal_can.cpp` w katalogach `impl/` są
  pustymi kotwicami builda. Operacje specyficzne dla MCP2515 znajdują się w
  `hal/can/mcp2515/hal_can_mcp2515.*` i korzystają z dostępnego wyłącznie w HAL
  sterownika rejestrów/SPI MCP2515 z `hal/can/mcp2515/mcp2515_driver.*`.
  Operacje MCP251XFD znajdują się w `hal/can/mcp251xfd/hal_can_mcp251xfd.*`
  i korzystają z dostępnego wyłącznie w HAL pollingowego sterownika rejestrów/SPI
  z `hal/can/mcp251xfd/mcp251xfd_driver.*`.
  Natywne operacje FDCAN dla STM32G474 znajdują się w
  `impl/stm32g474/hal_can_stm32g474_fdcan.*` i programują bezpośrednio rejestry
  FDCAN1, FDCAN2 albo FDCAN3 oraz stały układ pamięci komunikatów (message RAM)
  tej instancji.
- **Wybór backendu:** API CAN przyjmuje `hal_can_config_t`. Włącz
  `HAL_ENABLE_MCP2515` dla klasycznego backendu MCP2515 lub
  `HAL_ENABLE_MCP251XFD` dla wsparcia CAN FD w MCP2517FD/MCP2518FD. Obie flagi
  kontrolerów zewnętrznych dołączają publiczną warstwę CAN oraz zależność SPI. Włącz
  `HAL_ENABLE_STM32G474_FDCAN` dla natywnych kontrolerów FDCAN STM32G474; ta flaga
  dołącza wyłącznie publiczną warstwę CAN i powoduje błąd kompilacji na innych
  targetach. Sama flaga `HAL_ENABLE_CAN` nie dołącza już SPI: włącza wspólne API
  i wymaga wskazania backendu.

**Współbieżność:** Każdy kanał ma własny `hal_mutex_t`, dlatego API może być wywoływane z wielu zadań i rdzeni. `hal_can_receive()` utrzymuje blokadę od sprawdzenia dostępności ramki do zakończenia jej odczytu. Inne zadanie nie może więc odebrać tej ramki pomiędzy sprawdzeniem a odczytem.

- **API CAN FD:** `hal_can_frame_t`, `hal_can_send_frame()`,
  `hal_can_receive_frame()` oraz pomocnicy DLC są niezależni od backendu.
  MCP2515 to klasyczny kontroler CAN 2.0, więc odrzuca ramki z
  `HAL_CAN_FRAME_FD`, `HAL_CAN_FRAME_BRS` lub `HAL_CAN_FRAME_ESI`. MCP251XFD
  akceptuje ramki CAN FD, gdy `cfg.mcp251xfd.enable_fd=true`; STM32G474 FDCAN
  akceptuje je, gdy `cfg.stm32g474_fdcan.enable_fd=true`. Ustaw
  `HAL_CAN_MODE_FD` dla trybu FD lub mieszanego na uchwytach obsługujących FD. Użyj
  `HAL_CAN_FRAME_EXTENDED` dla 29-bitowych ID i `HAL_CAN_FRAME_RTR` dla ramek
  zdalnych (remote frames).
- **Tryby i diagnostyka:** Nowe uchwyty są domyślnie uruchomione.
  `hal_can_stop()` przełącza kontroler w tryb nieuczestniczący/konfiguracyjny,
  a `hal_can_start()` ponownie stosuje zapamiętany tryb. MCP2515 obsługuje
  flagi trybu normalnego, loopback, listen-only, sleep i one-shot. MCP251XFD
  i STM32G474 FDCAN obsługują tryb FD, loopback wewnętrzny i zewnętrzny,
  listen-only, one-shot i sleep. `HAL_CAN_MODE_LOOPBACK` to w każdym backendzie loopback
  wewnętrzny: własne ramki wracają, a magistrala nie jest wysterowana.
  `HAL_CAN_MODE_EXTERNAL_LOOPBACK` (STM32G474 FDCAN) wysyła je także na
  magistralę i pomija brak ACK; ramki nadal wracają z wnętrza kontrolera, więc
  ten tryb pokazuje je analizatorowi magistrali, ale nie sprawdza odbioru przez
  transceiver.
  API stanu i liczników błędów przekształca zawartość rejestrów kontrolera do
  `hal_can_state_t` i `hal_can_error_counters_t`.
- **Kanały płytki:** profil płytki z sekcją `can.channels` (np.
  `nucleo-g474re-canhat`) wymienia podłączone kanały; `hal_can_board_config()`
  zamienia kanał *n* w gotową konfigurację, którą aplikacja może zmienić przed
  `hal_can_create()`. `HAL_CAN_MAX_INSTANCES` domyślnie równa się liczbie
  kanałów płytki, gdy przekracza ona 2; przy zajętych wszystkich uchwytach
  `hal_can_create()` zwraca `HAL_ENOMEM`.
- **STM32G474 FDCAN:** `instance` wybiera FDCAN1..3 (0 oznacza FDCAN1); na
  instancję przypada jeden uchwyt, drugie `hal_can_create()` dla tej samej
  instancji zwraca `HAL_EBUSY`. `rx_pin`/`tx_pin` równe 0 wybierają PA11/PA12,
  PB12/PB13 albo PA8/PB4; inne piny muszą być takimi, do których instancję da
  się poprowadzić. Przy `has_standby` backend steruje wejściem standby
  transceivera: standby przy konfiguracji, zatrzymaniu, uśpieniu i zniszczeniu
  kanału, praca 40 µs przed wejściem kontrolera na magistralę. Bit timing
  wynika z zegara jądra FDCAN (PCLK1 170 MHz albo PLL Q 80 MHz w drzewie
  zegarów HSE): prędkość musi wyjść z błędem
  do 0,5 %, punkt próbkowania domyślnie wynosi 80 % (arbitraż) i 75 % (dane),
  a faza danych używa preskalera arbitrażu, gdy daje on dokładną prędkość.
  Prędkość, której zegar nie zrobi, kończy `hal_can_create()` statusem
  `HAL_EUNSUPPORTED`; arbitraż powyżej 1 Mbit/s, faza danych wolniejsza od
  arbitrażu albo szybsza od `transceiver_max_bitrate_hz` statusem
  `HAL_EINVAL`. Przy 170 MHz wyklucza to fazę danych 4 i 8 Mbit/s, które
  daje 80 MHz. Kompensacja opóźnienia nadajnika (`HAL_CAN_TDC_AUTO`) włącza się
  powyżej 1 Mbit/s przy preskalerze danych 1 lub 2, z drugim punktem
  próbkowania w miejscu zwykłego. `hal_can_send_frame()` czeka, aż ramka
  wyjdzie: nieudana próba one-shot zwraca `HAL_EIO`, bus-off `HAL_EBUS`,
  a limit czasu 20 ramek (co najmniej 5 ms) `HAL_ETIMEOUT`; ramka zgłoszona
  jako nieudana jest porzucana i nigdy nie wychodzi później. Kontroler w stanie bus-off zaczyna wychodzenie z niego przy
  następnym nadaniu, odbiorze albo odczycie stanu. Filtry są zapisywane podczas
  pracy kontrolera, więc ich zmiana nie zdejmuje węzła z magistrali.
- **Filtry:** `hal_can_set_filter()` programuje jeden slot id/maska.
  `HAL_CAN_MAX_FILTERS` (6) to *minimalna* liczba sprzętowych filtrów akceptacji
  gwarantowana przez każdy backend, a zatem liczba slotów, na której może polegać
  przenośny kod. W MCP2515 odpowiadają one sześciu filtrom sprzętowym. MCP251XFD
  używa pierwszych sześciu sprzętowych obiektów filtrów kierowanych do swojego
  RX FIFO 1, a STM32G474 FDCAN wolnych elementów filtrów standardowych lub
  rozszerzonych kierowanych do RX FIFO 0; oba układy mają ich więcej.
  `hal_can_set_std_filters()` pozostaje funkcją pomocniczą dla dwóch
  dokładnych 11-bitowych ID. Zaprogramowanie filtra MCP2515 czyści też tryb
  odbioru dowolnego (receive-any) na obu sprzętowych buforach odbiorczych, więc
  niepasujące ramki są odrzucane, zanim zajmą którykolwiek bufor.
  `hal_can_create_with_retry()` ponawia inicjalizację do `max_retries + 1` prób
  i może automatycznie podłączyć handler IRQ, gdy `int_pin != HAL_CAN_NO_INT_PIN`.
  `hal_can_process_all()` wielokrotnie wywołuje `hal_can_receive()` i przekazuje
  dalej tylko ramki z `id != 0` i `len > 0`; błąd odbioru inny niż
  `HAL_EAGAIN` kończy opróżnianie i wraca jako jego wynik.
  `hal_can_encode_temp_i8()` to funkcja pomocnicza wspólnego formatu danych dla
  jednobajtowych pól temperatury ze znakiem w ramkach CAN. Obcina wejściową
  wartość typu `float` w stronę zera, nasyca ją do zakresu `int8_t` i zwraca
  odpowiadający bajt payloadu w zapisie uzupełnienia do dwóch.

**Filtry ponad klasyczne sloty** (STM32G474 FDCAN):

```c
enum { HAL_CAN_FILTER_MASK = 0, HAL_CAN_FILTER_RANGE = 1, HAL_CAN_FILTER_DUAL = 2 };
enum { HAL_CAN_FILTER_ACCEPT = 0, HAL_CAN_FILTER_REJECT = 1 };
#define HAL_CAN_FILTER_FIRST_ADDED HAL_CAN_MAX_FILTERS

typedef struct {
    uint8_t type;   /* HAL_CAN_FILTER_MASK, _RANGE or _DUAL */
    uint8_t action; /* HAL_CAN_FILTER_ACCEPT or _REJECT */
    uint8_t flags;  /* HAL_CAN_FILTER_EXTENDED */
    uint32_t id1;   /* ID, range start or first ID */
    uint32_t id2;   /* mask, range end or second ID */
} hal_can_filter_ex_t;

hal_status_t hal_can_add_filter(hal_can_t h, const hal_can_filter_ex_t *filter,
                                uint8_t *index);
hal_status_t hal_can_remove_filter(hal_can_t h, uint8_t index);
hal_status_t hal_can_set_unmatched_policy(hal_can_t h, bool accept_std,
                                          bool accept_ext, bool accept_rtr);
hal_status_t hal_can_validate_filter_ex(const hal_can_filter_ex_t *filter);
hal_status_t hal_can_frame_matches_filter_ex(const hal_can_frame_t *frame,
                                             const hal_can_filter_ex_t *filter,
                                             bool *matches);
```

- Filtry są elementami list kontrolera (28 standardowych, 8 rozszerzonych)
  i zmieniają się podczas pracy kanału, bez zdejmowania go z magistrali.
  Nowy filtr zajmuje najniższy wolny element; kontroler sprawdza elementy po
  kolei i decyduje pierwsze dopasowanie, więc filtr odrzucający dodany przed
  zakresem akceptującym wycina z niego wyjątek.
- `hal_can_add_filter()` zwraca indeksy od `HAL_CAN_FILTER_FIRST_ADDED` (6)
  w górę; klasyczne sloty `hal_can_set_filter()` zachowują 0-5 i leżą na tych
  samych listach. `hal_can_rx_info_t::filter_index` wskazuje filtr, który
  przyjął ramkę, a `HAL_CAN_FILTER_NONE`, gdy nie zrobił tego żaden.
  `hal_can_remove_filter()` usuwa filtry obu rodzajów.
- Bez filtrów kanał przyjmuje wszystko. Pierwszy filtr klasyczny odrzuca
  ramki bez dopasowania, chyba że wcześniej wywołano
  `hal_can_set_unmatched_policy()`. Przyjmowanie ramek bez dopasowania dla
  jednego rodzaju ID zajmuje ostatni element tej listy (zostaje 27 filtrów
  standardowych), więc kończy się `HAL_ENOMEM`, gdy ten element ma filtr.
  Odrzucanie ramek zdalnych (`accept_rtr` false) wymaga zatrzymanego kanału
  (inaczej `HAL_EBUSY`).
- MCP251XFD ma 32 filtry wspólne dla obu rodzajów ID i pasującą ramkę może
  tylko skierować do swojej kolejki odbiorczej: przyjmuje filtry maskowe
  akceptujące (`HAL_CAN_FILTER_MASK`, `HAL_CAN_FILTER_ACCEPT`) pod indeksami
  6..30, a na zakresy, pary ID i filtry odrzucające odpowiada
  `HAL_EUNSUPPORTED`. Filtr 31 przechowuje politykę ramek bez filtra. Układ nie
  umie odrzucać ramek zdalnych, więc przy `accept_rtr` równym false sterownik
  odrzuca je przy odczycie, także na pracującym kanale.
- MCP2515 odpowiada `HAL_EUNSUPPORTED`.

**Praca kolejkowa, zdarzenia i stan:**

```c
#define HAL_CAN_MODE_MANUAL_RECOVERY 0x40u  /* stay bus-off until hal_can_recover() */
#define HAL_CAN_WAIT_FOREVER 0xFFFFFFFFu
#define HAL_CAN_FILTER_NONE 0xFFu

hal_status_t hal_can_get_caps(hal_can_t h, hal_can_caps_t *out);

hal_status_t hal_can_send_frame_ex(hal_can_t h, const hal_can_frame_t *frame,
                                   uint32_t timeout_ms, uint32_t *tag);
hal_status_t hal_can_receive_frame_ex(hal_can_t h, hal_can_frame_t *frame,
                                      hal_can_rx_info_t *info,
                                      uint32_t timeout_ms);

hal_status_t hal_can_set_callbacks(hal_can_t h, hal_can_rx_cb_t rx,
                                   hal_can_tx_cb_t tx,
                                   hal_can_state_cb_t state, void *user);
int hal_can_service(hal_can_t h, int max_events);
hal_status_t hal_can_set_isr_notify(hal_can_t h, void (*notify)(void *),
                                    void *user);

hal_status_t hal_can_get_status(hal_can_t h, hal_can_status_t *out);
hal_status_t hal_can_recover(hal_can_t h, uint32_t timeout_ms);
```

- **Kanały kolejkowe:** STM32G474 FDCAN pracuje na przerwaniach (IT0 dla
  FIFO odbiorczych, IT1 dla reszty). Odebrane ramki trafiają do kolejki
  uchwytu o długości `HAL_CAN_RX_QUEUE_LEN` (16), nadania kolejkowe do kolejki
  `HAL_CAN_TX_QUEUE_LEN` (8) przed trzema slotami kontrolera, a wyniki nadań
  i zmiany stanu do kolejki `HAL_CAN_EVENT_QUEUE_LEN` (16). Długości można
  ustawić w `hal_project_config.h` w zakresie 0..1024; gdy któraś z nich
  wynosi 0, kanał pracuje synchronicznie, jak kontrolery SPI. Kanały
  kolejkowe zgłaszają `HAL_CAN_CAP_TX_EVENTS` i `HAL_CAN_CAP_RX_QUEUE`. Klasyczne
  funkcje odbioru czytają tę samą kolejkę; ramka CAN FD, której nie mogą
  zwrócić, jest zabierana i liczona w `rx_dropped_fd_on_classic_read`.
  Klasyczne nadanie nadal czeka na swoją ramkę i nie daje zdarzenia.
- **Nadawanie:** `hal_can_send_frame_ex()` kolejkuje ramkę i zwraca jej
  znacznik; `timeout_ms` ogranicza tylko czekanie na miejsce w pełnej
  kolejce. Wynik przychodzi jako `hal_can_tx_event_t`: `HAL_CAN_TX_DONE`,
  `HAL_CAN_TX_FAILED` (nieudana próba one-shot, wynik `HAL_EIO`),
  `HAL_CAN_TX_BUS_OFF` (`HAL_EBUS`) albo `HAL_CAN_TX_STOPPED`
  (`HAL_ECANCELED`; `hal_can_stop()` lub zmiana trybu: ramki czekające
  w kolejce były kolejkowane dla starego trybu). Każda ramka z kolejki
  dostaje dokładnie jedno zdarzenie, a ramka zgłoszona jako nieudana nigdy
  nie wychodzi później. Ramka CAN FD wymaga `HAL_CAN_MODE_FD` w bieżącym
  trybie kanału; kanał przełączony na klasyczny CAN odrzuca ją z
  `HAL_EUNSUPPORTED`. Na FDCAN nieudaną próbę one-shot
  wykrywają przerwania błędu protokołu, włączane tylko w
  `HAL_CAN_MODE_ONE_SHOT`.
- **Zdarzenia:** `hal_can_service()` przekazuje callbackom wyniki nadań
  i zmiany stanu, a potem odebrane ramki, jeśli ustawiono callback odbioru.
  Działa w zadaniu, które ją wywołuje; callbacki działają bez blokady uchwytu
  i mogą używać tego samego uchwytu. `hal_can_set_isr_notify()` ustawia
  funkcję, którą przerwanie wywołuje po dodaniu czegoś do kolejki, np. by
  obudzić zadanie wołające `hal_can_service()`. Każda kolejka liczy to, co
  musiała odrzucić (`rx_queue_overflow`, `event_overflow`).
- **Bus-off:** kanał kolejkowy po wejściu w bus-off porzuca wszystkie
  oczekujące ramki i, jeśli nie ustawiono `HAL_CAN_MODE_MANUAL_RECOVERY`,
  od razu zaczyna wychodzenie (128 x 11 bitów recesywnych). W trybie
  ręcznym zostaje poza magistralą do `hal_can_recover()`, także po
  klasycznym nadaniu, które trafiło na bus-off.
- **Stan:** `hal_can_get_status()` zwraca stan, TEC/REC, ostatnie kody
  błędów protokołu (FDCAN LEC/DLEC, zachowane, choć odczyt kontrolera je
  zeruje), zmierzone opóźnienie nadajnika i liczniki ruchu (ramki nadane
  i odebrane, ramki utracone w kolejce odbiorczej, nieudane nadania, liczba
  bus-off). `rx_hw_lost` liczy przepełnienia FIFO odbiorczego kontrolera:
  każde zgubiło co najmniej jedną ramkę, ale kontroler zatrzaskuje tylko sam
  fakt, więc to dolna granica ramek utraconych w kontrolerze.
  `ram_access_failures` liczy błędy dostępu do message RAM STM32G474 FDCAN
  (MRAF): odrzuconą ramkę odbieraną albo nadanie przerwane, bo kontroler nie
  zdążył odczytać ramki ze swojego message RAM. Po takim nadaniu kontroler
  nic nie wysyła, dopóki oprogramowanie nie zakończy trybu ograniczonego
  (restricted operation); HAL robi to od razu, więc czekające ramki
  wychodzą. Pozostałe backendy podają 0.
- **Znaczniki czasu:** na STM32G474 FDCAN `hal_can_rx_info_t::timestamp_us`
  i `hal_can_tx_event_t::timestamp_us` są w skali `hal_micros64()`. Domyślnie
  to chwila, w której przerwanie obsłużyło ramkę, jedna wartość na
  przerwanie. Zdefiniowanie `HAL_CAN_STM32G474_TIMESTAMP_TIM3` w
  `hal_project_config.h` zamienia je na znaczniki początku ramki: TIM3 liczy
  mikrosekundy, kontroler zatrzaskuje go przy SOF (`TSCC.TSS` zewnętrzny) dla
  ramek odebranych oraz, przez TX event FIFO, dla nadanych, a 16-bitowa
  wartość jest rozszerzana do 64 bitów (poprawnie, gdy przerwanie zdąży
  w 65 ms). Kanał zgłasza wtedy `HAL_CAN_CAP_TIMESTAMP_HW`, a piny TIM3 nie
  są dostępne dla `hal_pwm` / `hal_pwm_freq`. Wyniki ramek, które nie
  wyszły, mają 0.
- **MCP251XFD:** zegar 40 lub 20 MHz taktuje kontroler bezpośrednio, kwarc
  4 MHz włącza jego PLL x10. Timing bitów jest wyszukiwany w zakresach układu
  tak jak w FDCAN, z punktami próbkowania z
  `arbitration_sample_point_permille` / `data_sample_point_permille`
  (domyślnie 80,0 % / 75,0 %) i z tym samym preskalerem w obu fazach, jeśli
  się mieści; od 1 Mbit/s w fazie danych w trybie CAN FD automatyczna
  kompensacja opóźnienia nadajnika ustawia drugi punkt próbkowania na punkt
  próbkowania fazy danych. Każda zmiana trybu przechodzi przez tryb
  konfiguracji układu, który porzuca czekające w nim ramki. Nadanie czeka, aż
  ramka wyjdzie: nieudana próba one-shot zwraca `HAL_EIO`, brak sukcesu
  w ciągu ok. 1400 czasów bitu przy prędkości arbitrażu `HAL_ETIMEOUT`,
  a ramka jest usuwana i nigdy nie wychodzi później. Prędkość, której zegar
  nie zrobi, kończy `hal_can_create()` statusem `HAL_EUNSUPPORTED`,
  oscylator albo tryb, który nie ruszy w ok. 100 ms, statusem
  `HAL_ETIMEOUT`, a układ, który nie odpowiada po resecie, statusem
  `HAL_EIO`. Kolejka odbiorcza mieści 24 ramki. Backend
  jest `experimental`: przeszedł testy hostowe z modelem MCP2518FD i buildy
  targetów, ale nie test z fizycznym układem, więc żaden profil płytki go nie
  opisuje. Zmiana tego statusu wymaga udokumentowanego testu na sprzęcie.
- **Kanały synchroniczne:** MCP2515 i MCP251XFD nie mają kolejki:
  `hal_can_send_frame_ex()` nadaje przed powrotem, a zdarzenie z wynikiem
  i tak trafia do kolejki (domyślnie 4 zdarzenia), `hal_can_service()` czyta
  ramki z kontrolera i zgłasza zauważone zmiany stanu, a
  `hal_can_set_isr_notify()` zwraca `HAL_EUNSUPPORTED`.
- **Testy hostowe:** `hal_mock_can_set_queued(true)` sprawia, że uchwyty
  mocka tworzone później są kolejkowe jak FDCAN; `hal_mock_can_fail_sends()`
  kończy kolejne nadania kolejkowe porażką.

**Tryb TX one-shot:** `hal_can_create()` domyślnie włącza tryb one-shot MCP2515
(`CANCTRL.OSM = 1`) po inicjalizacji. Można go wyłączyć poprzez
`cfg.mcp2515.one_shot_tx`. W trybie one-shot, gdy wysłana ramka nie otrzyma
ACK (np. brak innego węzła na magistrali), sprzęt natychmiast zwalnia bufor
TX zamiast retransmitować w nieskończoność. Zapobiega to wyczerpaniu buforów TX:
bez one-shot już 3 kolejne ramki bez ACK trwale blokują wszystkie 3 bufory
TX, powodując, że każde kolejne `hal_can_send()` zawodzi z
`HAL_EBUSY`.

Tryb one-shot jest przydatny przy okresowym rozgłaszaniu, gdy kolejna aktualizacja może zastąpić utraconą ramkę. Nie gwarantuje jednak dostarczenia każdej aktualizacji. Jeśli aplikacja wysyła dane tylko po zmianie, powinna ponawiać nieudaną transmisję, okresowo wysyłać aktualny stan albo wyłączyć `one_shot_tx`; inaczej odbiorca może pozostać z nieaktualnymi danymi.

Jeżeli pierwsza próba transmisji się powiedzie, tryb one-shot i tryb normalny dają ten sam wynik - ponowienie nie jest potrzebne. W trybie one-shot brak ACK, utrata arbitrażu, przerwanie transmisji lub błąd magistrali powodują zwrócenie `HAL_EIO` przez `hal_can_send()`. Błąd jest logowany z nazwą statusu przez `hal_derr_limited("can", ...)`, aby ograniczyć liczbę komunikatów na porcie szeregowym. Tryb normalny kontynuuje sprzętowe ponawianie transmisji i zgłasza sukces, gdy kolejna próba się powiedzie.

**Statusy wyników (MCP2515):** `hal_can_send()` i `hal_can_send_frame()` zwracają `HAL_EBUSY`, gdy żaden bufor nadawczy nie zwolnił się w 2,5 ms, `HAL_ETIMEOUT`, gdy ramka nie wyszła w 2,5 ms (jest przerywana i nigdy nie wychodzi później), oraz `HAL_EIO` dla nieudanej próby one-shot. Funkcje odbioru zwracają `HAL_EAGAIN`, gdy żadna ramka nie czeka. Zmiana trybu, `hal_can_stop()` i każde wywołanie filtrów zwracają `HAL_ETIMEOUT`, gdy kontroler nie wszedł w żądany tryb albo z niego nie wyszedł w 200 ms; wywołania filtrów kończą się na pierwszym takim błędzie. `hal_can_create()` zwraca `HAL_EUNSUPPORTED` dla prędkości albo kwarcu bez tabeli timingu i `HAL_EIO`, gdy kontroler nie odpowiada albo nie przyjmuje konfiguracji. Kontroler nie wykrywa bus-off w trakcie nadawania; zgłasza go `hal_can_get_state()`. Każdy kanał: `HAL_EINVAL` dla błędnego uchwytu albo argumentu i `HAL_EBUSY` dla nadania na zatrzymanym albo uśpionym kanale.

---

<a id="hal_hd44780---wyświetlacz-znakowy-lcd-hd44780--opcjonalny---hal_enable_hd44780"></a>

## `hal_hd44780` - wyświetlacze znakowe LCD  *(opcjonalny - `HAL_ENABLE_HD44780`)*

Wyświetlanie tekstu na równoległych LCD zgodnych z HD44780. Sterownik obsługuje 4- i 8-bitową transmisję przez GPIO, opcjonalną linię `RW`, własne znaki w CGRAM, kursor, włączanie i wyłączanie wyświetlania, przewijanie ręczne i automatyczne oraz konfigurowalne przesunięcia wierszy. Zakres funkcji odpowiada oryginalnej bibliotece LiquidCrystal.

### API C

`hal_hd44780_t` jest uchwytem do instancji wyświetlacza. Podaj piny i
szerokość magistrali, utwórz uchwyt, a następnie wybierz geometrię wyświetlacza
przez `hal_hd44780_begin()`:

```c
#include <hal/display/hal_hd44780.h>

hal_status_t show_lcd_message(void) {
    hal_hd44780_config_t config = {0};
    config.rs_pin = 2;
    config.rw_pin = HAL_HD44780_PIN_NONE;
    config.enable_pin = 3;
    config.data_pins[0] = 4; /* D4 */
    config.data_pins[1] = 5; /* D5 */
    config.data_pins[2] = 6; /* D6 */
    config.data_pins[3] = 7; /* D7 */
    config.bus_width = HAL_HD44780_BUS_4_BIT;

    hal_hd44780_t lcd = NULL;
    hal_status_t status = hal_hd44780_create(&config, &lcd);
    if (status != HAL_OK) return status;

    status = hal_hd44780_begin(lcd, 16u, 2u, HAL_HD44780_FONT_5X8);
    if (status == HAL_OK) status = hal_hd44780_print(lcd, "JaszczurHAL", NULL);
    if (status == HAL_OK) status = hal_hd44780_set_cursor(lcd, 0u, 1u);
    if (status == HAL_OK) status = hal_hd44780_print(lcd, "ready", NULL);

    hal_status_t close_status = hal_hd44780_destroy(lcd);
    return status != HAL_OK ? status : close_status;
}
```

`hal_hd44780_create()` odrzuca nieprawidłowe lub powtórzone numery wymaganych
pinów, a po zapełnieniu statycznej puli zwraca `HAL_ENOMEM`. Tryb 4-bitowy
korzysta z `data_pins[0..3]` jako D4..D7, natomiast tryb 8-bitowy używa
wszystkich ośmiu pól. Ustaw `rw_pin` na `HAL_HD44780_PIN_NONE`, gdy linia `RW`
jest połączona z masą. Pula mieści domyślnie `HAL_HD44780_MAX_INSTANCES`
wyświetlaczy.

Po `hal_hd44780_begin()` zapisuj dane przez `hal_hd44780_write()`,
`hal_hd44780_write_byte()` albo `hal_hd44780_print()`. Kursor, wyświetlanie,
miganie, przewijanie, kierunek tekstu, autoscroll, przesunięcia wierszy, surowe
polecenia i znaki CGRAM mają osobne operacje zwracające status. Po ostatnim
użyciu zwolnij uchwyt przez `hal_hd44780_destroy()`. Za utworzenie i
zniszczenie danej instancji powinno odpowiadać jedno zadanie.

- **Implementacja:** `hal/display/hal_hd44780.cpp` udostępnia API uchwytów i
  używa `hal/display/hd44780/hd44780.*` na platformach RP, STM32G474 oraz w testach
  hostowych.
- **Zakres:** Jest to sterownik znakowego LCD. Do grafiki bitmapowej na
  wyświetlaczach TFT/OLED służy `hal_display`.
- **Czasowanie:** Inicjalizacja, czyszczenie/home, impuls na linii enable i
  opóźnienie wykonania polecenia zachowują przyjętą sekwencję HD44780: 50 ms
  oczekiwania na zasilanie, próby inicjalizacji 4,5 ms/150 us, 2 ms dla
  czyszczenia/home oraz fazy impulsu enable 1/1/100 us.

**Współbieżność:** Wywołania podczas działania dla jednego wyświetlacza są
serializowane przez jego muteks HAL. Nie używaj tego API z procedury obsługi
przerwania. Nie niszcz uchwytu, gdy korzysta z niego inne zadanie.

### API C++ zachowane dla zgodności

Ten sam publiczny nagłówek nadal udostępnia klasę `HD44780` w kodzie
kompilowanym jako C++. Istniejące aplikacje mogą nadal jej używać; migracja
źródeł nie jest wymagana.

```cpp
#include <hal/display/hal_hd44780.h>

HD44780 lcd(2, 3, 4, 5, 6, 7);

void show_lcd_message_cpp(void) {
    lcd.begin(16, 2);
    lcd.clear();
    lcd.print("JaszczurHAL");
    lcd.setCursor(0, 1);
    lcd.print("ready");
}
```

Klasa zachowuje konstruktory i metody w stylu LiquidCrystal. W nowym kodzie,
który potrzebuje jawnej diagnostyki, preferuj opisany wyżej interfejs C.

---

<a id="hal_display---wyświetlacz-tft--oled--lcd--epd--opcjonalny---hal_enable_display"></a>

## `hal_display` - wyświetlacze graficzne  *(opcjonalny - `HAL_ENABLE_DISPLAY`)*

Obsługuje wyświetlacze TFT SPI (ILI9341, ST7789, ST7735, ST7796S, GC9A01),
OLED RGB SSD1331/SSD135x, OLED z rodziny SSD1306 (`SSD1306`, `SSD1309`,
`SSD1315`, `SH1106`, `CH1115`), monochromatyczne LCD ST7567 oraz
monochromatyczne kontrolery e-papieru SSD16xx/UC81xx przez I2C/SPI/GPIO.

```c
// Zdefiniuj JEDNO z tych przed dołączeniem hal_display.h (lub we flagach buildu):
#define HAL_DISPLAY_ILI9341
#define HAL_DISPLAY_ST7789
#define HAL_DISPLAY_ST7735
#define HAL_DISPLAY_ST7796S
#define HAL_DISPLAY_GC9A01

// Opcjonalne wykluczenia per driver:
// #define HAL_ENABLE_ILI9341
// #define HAL_ENABLE_ST7789
// #define HAL_ENABLE_ST7735
// #define HAL_ENABLE_ST7796S
// #define HAL_ENABLE_GC9A01
```

```c
#include <hal/display/hal_display.h>

// --- Popularne kolory RGB565 ---
#define HAL_COLOR_BLACK   0x0000
#define HAL_COLOR_WHITE   0xFFFF
#define HAL_COLOR_RED     0xF800
#define HAL_COLOR_GREEN   0x07E0
#define HAL_COLOR_BLUE    0x001F
#define HAL_COLOR_ORANGE  0xFD20
#define HAL_COLOR_PURPLE  0x780F
#define HAL_COLOR_YELLOW  0xFFE0
#define HAL_COLOR_CYAN    0x07FF

// Selektor pomocniczy: HAL_COLOR(RED) -> HAL_COLOR_RED
#define HAL_COLOR(name) HAL_COLOR_##name

// --- Pomocnicy orientacji / trybu wyświetlacza ---
typedef enum {
    HAL_DISPLAY_ROTATION_0   = 0,
    HAL_DISPLAY_ROTATION_90  = 1,
    HAL_DISPLAY_ROTATION_180 = 2,
    HAL_DISPLAY_ROTATION_270 = 3,
} hal_display_rotation_t;

// --- Opis surowego bufora ---
typedef enum {
    HAL_DISPLAY_PIXEL_FORMAT_NONE = 0u,
    HAL_DISPLAY_PIXEL_FORMAT_MONO01 = (1u << 0),      // 0=czarny, 1=biały
    HAL_DISPLAY_PIXEL_FORMAT_MONO10 = (1u << 1),      // 1=czarny, 0=biały
    HAL_DISPLAY_PIXEL_FORMAT_RGB565_BE = (1u << 2),   // starszy bajt pierwszy
    HAL_DISPLAY_PIXEL_FORMAT_RGB565_NATIVE = (1u << 3),
    HAL_DISPLAY_PIXEL_FORMAT_RGB888 = (1u << 4),
    HAL_DISPLAY_PIXEL_FORMAT_BGR888 = (1u << 5),
    HAL_DISPLAY_PIXEL_FORMAT_L8 = (1u << 6),
} hal_display_pixel_format_t;

typedef struct {
    hal_display_pixel_format_t pixel_format;
    uint16_t pitch;           // liczba pikseli między kolejnymi wierszami źródłowymi
    uint16_t width;           // szerokość prostokąta w pikselach
    uint16_t height;          // wysokość prostokąta w pikselach
    size_t buf_size;          // dostępne bajty bufora źródłowego
    bool frame_incomplete;    // EPD: załaduj RAM teraz i odłóż fizyczne odświeżenie
} hal_display_buffer_desc_t;

typedef struct {
    uint16_t width, height;
    uint32_t supported_pixel_formats;
    hal_display_pixel_format_t current_pixel_format;
    uint8_t current_rotation, supported_rotations;
    uint16_t x_alignment, y_alignment;
    uint16_t width_alignment, height_alignment;
    uint32_t screen_info, flags;
} hal_display_capabilities_t;

#define HAL_DISPLAY_ROTATION(deg) \
    ((uint8_t)( \
        ((deg) == 0)   ? HAL_DISPLAY_ROTATION_0 : \
        ((deg) == 90)  ? HAL_DISPLAY_ROTATION_90 : \
        ((deg) == 180) ? HAL_DISPLAY_ROTATION_180 : \
        ((deg) == 270) ? HAL_DISPLAY_ROTATION_270 : \
                        HAL_DISPLAY_ROTATION_0))

#define HAL_DISPLAY_INVERT_OFF false
#define HAL_DISPLAY_INVERT_ON  true
#define HAL_DISPLAY_COLOR_ORDER_RGB false
#define HAL_DISPLAY_COLOR_ORDER_BGR true

// Tryb zasilania SSD1306
#define HAL_DISPLAY_VCC_EXTERNAL  0x01
#define HAL_DISPLAY_VCC_SWITCHCAP 0x02

typedef enum {
    HAL_DISPLAY_OLED_CONTROLLER_SSD1306 = 0,
    HAL_DISPLAY_OLED_CONTROLLER_SSD1309,
    HAL_DISPLAY_OLED_CONTROLLER_SSD1315,
    HAL_DISPLAY_OLED_CONTROLLER_SH1106,
    HAL_DISPLAY_OLED_CONTROLLER_CH1115,
} hal_display_oled_controller_t;

typedef enum {
    HAL_DISPLAY_OLED_BUS_I2C = 0,
    HAL_DISPLAY_OLED_BUS_SPI,
} hal_display_oled_bus_t;

typedef enum {
    HAL_DISPLAY_OLED_ORIENTATION_NATIVE = 0,
    HAL_DISPLAY_OLED_ORIENTATION_ROTATED_180,
} hal_display_oled_orientation_t;

typedef struct {
    hal_display_oled_controller_t controller;
    hal_display_oled_bus_t bus_type;
    int width, height;
    uint8_t bus, i2c_addr;
    int16_t rst_pin, spi_dc_pin, spi_cs_pin;
    uint8_t switchvcc, spi_mode;
    uint32_t clock_hz;
    uint8_t segment_offset, page_offset, display_offset;
    hal_display_oled_orientation_t orientation;
    bool internal_iref;
    bool periphBegin;
} hal_display_ssd1306_family_config_t;

typedef enum {
    HAL_FONT_DEFAULT = 0,
    HAL_FONT_SANS_BOLD_9PT,
    HAL_FONT_SERIF_9PT,
} hal_font_id_t;

// --- Inicjalizacja / sterowanie ---

// Tworzy obiekt wyświetlacza i uruchamia driver SPI.
// Dla ILI9341: wywołuje też begin(). Dla innych driverów: inicjalizacja jest odłożona do configure().
hal_status_t hal_display_init(uint8_t cs, uint8_t dc, uint8_t rst);

// Tworzy i inicjalizuje OLED SSD1306 podłączony przez I2C.
bool hal_display_init_ssd1306_i2c(int width, int height, uint8_t i2c_addr,
                                  int8_t rst_pin, uint8_t switchvcc,
                                  bool periphBegin);

// Konfigurowalna inicjalizacja rodziny SSD1306 zwracająca status.
hal_status_t hal_display_init_ssd1306_family_ex(
    const hal_display_ssd1306_family_config_t *config);

// Struktury konfiguracyjne deklarowane są warunkowo, przez odpowiednią flagę HAL_ENABLE.
hal_status_t hal_display_init_rgb_oled_ex(
    const hal_display_rgb_oled_config_t *config);
hal_status_t hal_display_init_st7567_ex(
    const hal_display_st7567_config_t *config);
hal_status_t hal_display_init_ssd16xx_ex(
    const hal_display_ssd16xx_config_t *config);
hal_status_t hal_display_init_uc81xx_ex(
    const hal_display_uc81xx_config_t *config);

// Konfiguruje wymiary, rotację, kolejność kolorów. Musi być wywołane po init().
bool hal_display_configure(int width, int height, uint8_t rotation, bool invert, bool bgr);

// Ponownie wysyła sekwencję inicjalizacji rejestrów backendu, gdy wybrany driver ją obsługuje.
hal_status_t hal_display_soft_init(int delay_ms);
hal_status_t hal_display_suspend_ex(void);
hal_status_t hal_display_resume_ex(void);

bool hal_display_set_rotation(uint8_t r);
bool hal_display_invert(bool invert);
int  hal_display_get_width(void);
int  hal_display_get_height(void);
hal_status_t hal_display_get_capabilities_ex(hal_display_capabilities_t *caps);
hal_status_t hal_display_set_pixel_format_ex(hal_display_pixel_format_t format);
hal_status_t hal_display_write_raw_ex(uint16_t x, uint16_t y,
                                      const hal_display_buffer_desc_t *desc,
                                      const void *buffer);
hal_status_t hal_display_epd_refresh_ex(
    hal_display_epd_refresh_mode_t refresh_mode);

// --- Ekran ---
bool hal_display_fill_screen(uint16_t color);
bool hal_display_flush(void); // SSD1306: wysyła bufor ramki; EPD: odświeża oczekujące dane
bool hal_display_draw_image(int x, int y, int w, int h, uint16_t background, uint16_t *data);

// --- Geometria ---
bool hal_display_fill_rect(int x, int y, int w, int h, uint16_t color);
bool hal_display_draw_rect(int x, int y, int w, int h, uint16_t color);
bool hal_display_fill_circle(int x, int y, int r, uint16_t color);
bool hal_display_draw_circle(int x, int y, int r, uint16_t color);
bool hal_display_fill_round_rect(int x, int y, int w, int h, int r, uint16_t color);
bool hal_display_draw_line(int x0, int y0, int x1, int y1, uint16_t color);

// --- Bitmapa ---
bool hal_display_draw_rgb_bitmap(int x, int y, uint16_t *data, int w, int h);

// --- Strumieniowanie TFT ---
bool hal_display_begin_write(int x, int y, int w, int h);
bool hal_display_write_pixels_fast(const uint16_t *pixels, size_t count);
bool hal_display_write_pixels_be(const uint8_t *pixels_be, size_t byte_count);
bool hal_display_write_pixels_dma(const uint8_t *pixels_be, size_t byte_count);
bool hal_display_write_pixels_dma_async_start(const uint8_t *pixels_be,
                                              size_t byte_count);
bool hal_display_write_pixels_dma_async_busy(void);
bool hal_display_write_pixels_dma_async_wait(void);
bool hal_display_end_write(void);

// --- Tekst ---
bool hal_display_set_font(hal_font_id_t font);
bool hal_display_set_text_color(uint16_t color);
bool hal_display_set_text_size(uint8_t size);
bool hal_display_set_cursor(int x, int y);
bool hal_display_print(const char *s);
bool hal_display_println(const char *s);
bool hal_display_print_at(int x, int y, const char *s);
bool hal_display_get_text_bounds(const char *s, int *w, int *h);
int  hal_display_text_width(const char *text);
int  hal_display_text_height(const char *text);

// --- Pomocnicy linii tekstu ---
bool hal_display_clear_text_line(int line_index, int line_height, uint16_t bg_color);
bool hal_display_print_line(int line_index, int line_height, const char *text,
                            bool clear_first, uint16_t fg_color, uint16_t bg_color);
bool hal_display_draw_text_centered(const char *text, uint16_t fg_color,
                                    uint16_t bg_color, bool clear_first,
                                    bool flush_after);

// --- Predefiniowane czcionki / style ---
bool hal_display_println_prepared_text(char *text);
bool hal_display_set_default_font(void);
bool hal_display_set_default_font_with_pos_and_color(int x, int y, uint16_t color);
bool hal_display_set_text_size_one_with_color(uint16_t color);
bool hal_display_set_sans_bold_with_pos_and_color(int x, int y, uint16_t color);
bool hal_display_set_serif9pt_with_color(uint16_t color);

// --- Tekst formatowany ---
int  hal_display_prepare_text(char *display_txt, size_t display_txt_size,
                              const char *format, ...);
int  hal_display_prepare_text_v(char *display_txt, size_t display_txt_size,
                                const char *format, va_list args);
```

- **Kolory:** RGB565 `uint16_t`. Użyj predefiniowanych stałych (`HAL_COLOR_BLACK`, `HAL_COLOR_WHITE`, `HAL_COLOR_RED`, ...)
  lub selektora `HAL_COLOR(name)`, na przykład `HAL_COLOR(ORANGE)`.
- **Pomocnicy trybu wyświetlacza:** `HAL_DISPLAY_ROTATION_*`, `HAL_DISPLAY_ROTATION(deg)`,
  `HAL_DISPLAY_INVERT_ON/OFF`, `HAL_DISPLAY_COLOR_ORDER_RGB/BGR`.
- **Obsługiwane formaty i bezpośredni zapis bufora:** Pobierz właściwości aktywnego
  backendu przez `hal_display_get_capabilities_ex()`, a w
  `hal_display_write_raw_ex()` używaj wyłącznie zadeklarowanych formatów i wyrównań.
  `pitch` jest podawany w pikselach. Backendy TFT i RGB OLED oraz mock akceptują
  `pitch > width`: każdy wiersz źródłowy jest przesyłany osobno w ramach
  jednego okna adresowania, więc bufor wywołującego musi zawierać dane tylko
  do `width` pikseli ostatniego wiersza - pamięć nie musi obejmować paddingu
  występującego za nimi. Backendy o układzie stronicowym albo
  rekonfigurujące profil panelu przy każdym wywołaniu nadal wymagają
  `pitch == width` i w przeciwnym razie zwracają `HAL_EUNSUPPORTED`: ST7567
  zawsze, ponieważ jeden bajt koduje osiem pionowo ułożonych pikseli, więc
  poszczególne wiersze pikseli nie mają własnych granic bajtowych; SSD16xx
  przy rotacji 0/180, ponieważ rotacja zmienia kierunek układu; oraz UC81xx,
  ponieważ każde wywołanie zapisu ponownie stosuje profil panelu, a podział
  danych na wiersze powtarzałby ten efekt dla każdego z nich.
  ST7567 akceptuje `MONO01`/`MONO10`, zwraca `HAL_DISPLAY_SCREEN_INFO_MONO_VTILED`
  i wymaga, aby `y` oraz `height` były wyrównane do 8 pikseli. Użyj
  `hal_display_set_pixel_format_ex()` przed zmianą polaryzacji monochromatycznej
  ST7567.

**E-papier SSD16xx / UC81xx:** Włącz `HAL_ENABLE_SSD16XX` lub
`HAL_ENABLE_UC81XX`; obie flagi dodają DISPLAY i SPI. Wspólny transport
korzysta z SPI plus CS/DC, opcjonalnego resetu i opcjonalnego GPIO BUSY.
Skonfigurowany pin BUSY jest odpytywany z `busy_timeout_ms`, zwracając
`HAL_ETIMEOUT` zamiast blokować się w nieskończoność. SSD16xx obsługuje
rotacje 0/90/180/270 i używa pionowego pakowania 8-pikselowego przy
rotacjach 0/180. UC81xx używa poziomego pakowania MSB-first i wymaga, aby
`x` oraz `width` były wyrównane do 8 pikseli. Oba akceptują wyłącznie
`MONO10`.

Ustaw `frame_incomplete=true`, aby załadować jeden lub więcej obszarów bez
natychmiastowego odświeżania panelu. Zakończ serię wywołaniem
`hal_display_flush_ex()`. Odłożone zapisy korzystają z pełnego odświeżenia,
dzięki czemu stara i nowa pamięć obrazu kontrolera pozostają spójne, a wspólna
warstwa nie musi zachowywać buforów przekazanych przez wywołującego. Przy
`frame_incomplete=false` skonfigurowany profil częściowy jest wybierany przed
zapisem obszaru i jego odświeżeniem. Cykl można również wybrać jawnie za pomocą
`hal_display_epd_refresh_ex(HAL_DISPLAY_EPD_REFRESH_FULL/PARTIAL)`; żądanie
odświeżenia częściowego dla oczekującej partii pełnego odświeżenia zwraca
`HAL_ESTATE`, natomiast brakujący profil częściowy zwraca
`HAL_EUNSUPPORTED`. Bajty LUT i profilu pochodzą od producenta panelu, a
zawierające je tablice muszą pozostać dostępne przez cały runtime backendu.
Nie używaj tej samej LUT tylko dlatego, że dwa moduły zawierają ten sam
kontroler.

```c
hal_display_ssd16xx_config_t cfg = {0};
cfg.controller = HAL_DISPLAY_SSD16XX_SSD1681;
cfg.transport.bus = 0;
cfg.transport.cs_pin = 17;
cfg.transport.dc_pin = 20;
cfg.transport.rst_pin = 21;
cfg.transport.busy_pin = 22;
cfg.transport.busy_active_high = true;
cfg.transport.busy_timeout_ms = 30000;
cfg.width = 200;
cfg.height = 200;
cfg.rotation = HAL_DISPLAY_ROTATION_0;

hal_status_t status = hal_display_init_ssd16xx_ex(&cfg);
if (status == HAL_OK) {
    hal_display_buffer_desc_t frame = {
        HAL_DISPLAY_PIXEL_FORMAT_MONO10, 200, 200, 200, 5000, false
    };
    status = hal_display_write_raw_ex(0, 0, &frame, framebuffer);
}
```

- **Strumieniowanie TFT:** Backendy TFT działające w trybie bezpośrednim
  obsługują jawną sekwencję strumieniowania dużych, ciągłych obszarów:
  `hal_display_begin_write(x, y, w, h)`, jeden lub więcej zapisów pikseli, a
  następnie `hal_display_end_write()`. `hal_display_write_pixels_fast()`
  przyjmuje natywne słowa `uint16_t` RGB565 i zamienia je wewnętrznie na
  kolejność bajtów kontrolera; `hal_display_write_pixels_be()` przyjmuje już
  bajty RGB565 w kolejności big-endian; `hal_display_write_pixels_dma()` jest blokującym
  funkcją zapisu strumienia przez DMA.
  Wariant asynchroniczny,
  `hal_display_write_pixels_dma_async_start()` / `_busy()` / `_wait()`, korzysta z
  `hal_spi_write_dma_async_*()` w sterownikach ILI9341 i ST77xx. Gdy backend rzeczywiście
  działa asynchronicznie, bufor `pixels_be` musi pozostać dostępny i niezmieniony, a
  strumień zapisu wyświetlacza otwarty do zakończenia `_wait()`.
  `hal_display_end_write()` czeka na aktywne asynchroniczne DMA pikseli przed
  zamknięciem transakcji TFT.
- **Uwagi ST77xx/GC9A01:** `HAL_DISPLAY_ST7735`, `HAL_DISPLAY_ST7789`,
  `HAL_DISPLAY_ST7796S` i `HAL_DISPLAY_GC9A01` korzystają ze wspólnego backendu
  zgodnego z rodziną ST77xx. `JH_ST77XX_SPI_DEFAULT_HZ` można nadpisać przed
  dołączeniem lub kompilacją sterownika, aby dostroić domyślny zegar SPI TFT dla
  danej płytki. ST7796S zachowuje udokumentowane domyślne ustawienia w kolejności
  BGR bez wymuszania zamienionych komend inwersji. GC9A01 używa lokalnej sekwencji
  komend Zephyr GC9x01x i domyślnie ustawia 240x240. SSD1331/SSD135x to
  backendy RGB565 w trybie bezpośrednim. Obsługują bezpośredni zapis bufora, dotychczasowe
  strumieniowanie oraz prymitywy GFX. Bezpośredni zapis i GFX obsługują obecnie wyłącznie
  natywną orientację. ST7567 celowo udostępnia tylko bezpośredni, stronicowy zapis;
  jego właściwości nie deklarują dotychczasowego GFX,
  strumieniowania ani DMA.
- **impl/rp2040:** Korzysta ze wspólnego stosu HAL display. ILI9341 i ST77xx
  używają wspólnych sterowników HAL SPI/GPIO; OLED-y z rodziny SSD1306
  używają wspólnego sterownika HAL I2C/SPI; geometria, bitmapy i renderowanie
  tekstu działają przez wspólny silnik `jh_gfx`.
- **impl/stm32g474:** Korzysta z tego samego wspólnego stosu HAL display co RP2040.
- **impl/.mock:** deterministyczny mock hosta, którego stan można sprawdzać w testach.

**Współbieżność:** Backendy sprzętowe chronią operacje wyświetlacza wewnętrznym
`hal_mutex_t`. Podczas strumieniowania TFT muteks pozostaje zablokowany między
`hal_display_begin_write()` a
`hal_display_end_write()`, w tym podczas oczekiwania na asynchroniczne DMA.
Backend mock jest niezsynchronizowany i przeznaczony do testów jednowątkowych.

**Funkcje testowe implementacji mock:**
```c
void         hal_mock_display_reset(void);
void         hal_mock_display_fail_next_io(void);
const char  *hal_mock_display_last_print(void);
const char  *hal_mock_display_last_println(void);
hal_font_id_t hal_mock_display_get_font(void);
uint16_t     hal_mock_display_get_text_color(void);
uint8_t      hal_mock_display_get_text_size(void);
void         hal_mock_display_get_cursor(int *x, int *y);
void         hal_mock_display_get_last_fill_rect(int *x, int *y, int *w, int *h, uint16_t *color);
void         hal_mock_display_get_last_bitmap(int *x, int *y, uint16_t **data, int *w, int *h);
```

**API zwracające status:** mock i wspólne backendy sprzętowe sprawdzają argumenty oraz
przekształcają błędy na `hal_status_t`. Dotychczasowe funkcje `bool` pozostają wrapperami
zgodności wywołującymi operacje `_ex`. Funkcje inicjalizacji i soft-init, które wcześniej
zwracały `void`, teraz w tym samym miejscu zwracają `hal_status_t`; istniejący kod może
nadal ignorować wynik. Gettery zwracające wartość zachowują pierwotną sygnaturę, a ich
warianty `_ex` przekazują dokładny błąd i zapisują wynik przez parametr wyjściowy.

Backend może zwrócić `HAL_EINVAL`, `HAL_EUNINIT`, `HAL_EUNSUPPORTED`, `HAL_ESTATE`,
`HAL_EBUSY`, `HAL_EOVERFLOW` lub `HAL_EIO` bez sprowadzania tych informacji do dawnego
wyniku typu `bool`.

```c
// Konfiguracja + rysowanie z typowaną diagnostyką
hal_status_t st = hal_display_configure_ex(240, 320, 0, false, false);
// HAL_EINVAL -> błędna szerokość/wysokość, HAL_EIO -> inicjalizacja backendu nieudana

st = hal_display_fill_rect_ex(0, 0, 240, 40, HAL_COLOR(BLUE));
// HAL_EINVAL -> nie-dodatnia szerokość/wysokość, HAL_EUNINIT -> jeszcze nieskonfigurowany

int width = 0;
if (hal_display_get_width_ex(&width) == HAL_OK) {
    // width poprawny tylko, gdy wywołanie zwróciło HAL_OK
}

// Strumieniowanie rozróżnia brak strumienia od już otwartego.
if (hal_display_begin_write_ex(0, 0, 240, 320) == HAL_OK) {
    hal_display_write_pixels_be_ex(pixels_be, byte_count); // HAL_EINVAL przy nieparzystej liczbie
    hal_display_end_write_ex();
}
```

Uwaga dotycząca nazewnictwa: ponieważ `hal_display_init_ssd1306_i2c_ex()` już
istnieje jako inicjalizator wybierający magistralę, funkcją zwracającą status
dla SSD1306 jest `hal_display_init_ssd1306_i2c_status_ex()`. W nowym kodzie dla
rodziny OLED preferuj `hal_display_init_ssd1306_family_ex()`: wybiera
kontroler, transport I2C/SPI, przesunięcia segmentu/strony/wyświetlacza,
orientację sprzętową oraz właściwy dla danego wariantu sposób ustawiania prądu
odniesienia w jednej strukturze konfiguracyjnej zwracającej status.
`HAL_ENABLE_SSD1306` nadal
automatycznie włącza I2C dla wcześniejszej funkcji pomocniczej; transport SPI OLED
wymaga też `HAL_ENABLE_SPI`.

---


---

*Dalej: [Czujniki](11_sensors.md)*
