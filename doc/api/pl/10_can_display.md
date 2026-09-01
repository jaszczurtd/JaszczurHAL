# Magistrala CAN i wyświetlacz

*Dostępne również [po angielsku](../en/10_can_display.md).*

> **Część [Dokumentacji API JaszczurHAL](../../pl/JaszczurHAL_API.md)**

Obejmuje: `hal_can`, `hal_hd44780`, `hal_display`.

## `hal_can` - magistrala CAN  *(opcjonalny - `HAL_ENABLE_CAN`, backendy `HAL_ENABLE_MCP2515` / `HAL_ENABLE_MCP251XFD` / `HAL_ENABLE_STM32G474_FDCAN`)*

```c
#include <hal/can/hal_can.h>

#define HAL_CAN_MAX_DATA_LEN 8
#define HAL_CAN_FD_MAX_DATA_LEN 64
#define HAL_CAN_DLC_INVALID 0xFFu
#define HAL_CAN_STD_ID_MASK 0x7FFu
#define HAL_CAN_EXT_ID_MASK 0x1FFFFFFFu
#define HAL_CAN_MAX_FILTERS 6u
#define HAL_CAN_NO_INT_PIN   0xFF

// Nieprzezroczysty uchwyt - jeden na fizyczną instancję kontrolera/backendu CAN
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
    HAL_CAN_MODE_SLEEP       = 0x10u
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
    bool enable_fd;
    bool one_shot_tx;
    bool sleep_wakeup;
} hal_can_mcp251xfd_config_t;

typedef struct {
    uint8_t rx_pin;
    uint8_t tx_pin;
    uint32_t arbitration_bitrate_hz;
    uint32_t data_bitrate_hz;
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
// STM32G474 FDCAN: PA11/PA12, arbitraż 500 kbit/s, dane 2 Mbit/s.
hal_can_config_t hal_can_default_config(void);

// Tworzy i inicjalizuje kanał CAN na podstawie konfiguracji. NULL używa
// konfiguracji domyślnej.
// Zwraca NULL w razie niepowodzenia (układ nie odpowiada lub pula wyczerpana)
hal_can_t hal_can_create(const hal_can_config_t *cfg);

// Zwalnia wszystkie zasoby; uchwyt nie może być używany po tym wywołaniu
void hal_can_destroy(hal_can_t h);

// Wysyła ramkę CAN
bool hal_can_send(hal_can_t h, uint32_t id, uint8_t len, const uint8_t *data);

// Wysyła ramkę CAN/CAN FD. MCP2515 akceptuje wyłącznie klasyczne ramki CAN;
// MCP251XFD i STM32G474 FDCAN akceptują CAN FD, gdy enable_fd=true.
bool hal_can_send_frame(hal_can_t h, const hal_can_frame_t *frame);

// Odczytuje kolejną dostępną ramkę (zwraca false, jeśli żadna ramka nie jest gotowa)
bool hal_can_receive(hal_can_t h, uint32_t *id, uint8_t *len, uint8_t *data);

// Odczytuje kolejną dostępną ramkę CAN/CAN FD.
bool hal_can_receive_frame(hal_can_t h, hal_can_frame_t *frame);

// Start/stop oraz tryby kontrolera. Nowe uchwyty są domyślnie uruchomione.
bool hal_can_start(hal_can_t h);
bool hal_can_stop(hal_can_t h);
bool hal_can_set_mode(hal_can_t h, hal_can_mode_t mode);
bool hal_can_get_mode(hal_can_t h, hal_can_mode_t *mode);

// Stan kontrolera i diagnostyka.
bool hal_can_get_state(hal_can_t h, hal_can_state_t *state);
bool hal_can_get_error_counters(hal_can_t h,
                                hal_can_error_counters_t *counters);

// Sprawdzenie nieblokujące: true, jeśli czeka co najmniej jedna ramka
bool hal_can_available(hal_can_t h);

// Konfiguruje sprzętowe filtry RX dla dwóch akceptowanych standardowych
// 11-bitowych ID.
// Niepasujące ID są odrzucane przez backendy ze sprzętowym wsparciem filtrów.
// Zwraca false, jeśli programowanie maski/filtra backendu się nie powiedzie.
bool hal_can_set_std_filters(hal_can_t h, uint32_t id0, uint32_t id1);

// Konfiguruje jeden slot filtra akceptacji z id/maską/flagami.
bool hal_can_set_filter(hal_can_t h, uint8_t index,
                        const hal_can_filter_t *filter);

// Pomocnik tworzenia z ponawianiem prób, z opcjonalną konfiguracją pinu IRQ.
hal_can_t hal_can_create_with_retry(const hal_can_config_t *cfg,
                                    uint8_t int_pin,
                                    void (*isr)(void),
                                    int max_retries,
                                    void (*retry_idle)(void));

// Opróżnia oczekujące ramki RX i wywołuje callback dla każdej poprawnej.
int hal_can_process_all(hal_can_t h, hal_can_frame_cb_t cb);

// Pomocnicy DLC dla CAN/CAN FD. bytes_to_dlc() zaokrągla w górę do
// najbliższej reprezentowalnej długości CAN FD i zwraca HAL_CAN_DLC_INVALID
// dla wartości >64 bajtów.
uint8_t hal_can_dlc_to_bytes(uint8_t dlc);
uint8_t hal_can_bytes_to_dlc(uint8_t bytes);
bool hal_can_validate_frame(const hal_can_frame_t *frame);
bool hal_can_validate_filter(const hal_can_filter_t *filter);
bool hal_can_frame_matches_filter(const hal_can_frame_t *frame,
                                  const hal_can_filter_t *filter);

// Koduje temperaturę w °C jako bajt payloadu CAN typu signed int8.
// Obcina w stronę zera, saturuje do zakresu [-128, 127], zwraca bajt w
// zapisie uzupełnienia do dwóch (two's complement).
uint8_t hal_can_encode_temp_i8(float temp_c);
```

- **Wspólna implementacja modułu:** Pliki `hal_can.cpp` właściwe dla poszczególnych
  targetów zawierają publiczną warstwę CAN, zarządzają cyklem życia uchwytów i mutexami
  oraz kierują wywołania do backendu. Operacje specyficzne dla MCP2515 znajdują się w
  `hal/can/mcp2515/hal_can_mcp2515.*` i korzystają z dostępnego wyłącznie w HAL
  drivera rejestrów/SPI MCP2515 z `hal/can/mcp2515/mcp2515_driver.*`.
  Operacje MCP251XFD znajdują się w `hal/can/mcp251xfd/hal_can_mcp251xfd.*`
  i korzystają z dostępnego wyłącznie w HAL pollingowego drivera rejestrów/SPI
  z `hal/can/mcp251xfd/mcp251xfd_driver.*`.
  Natywne operacje FDCAN dla STM32G474 znajdują się w
  `impl/stm32g474/hal_can_stm32g474_fdcan.*` i programują bezpośrednio rejestry
  FDCAN1 oraz stały układ pamięci komunikatów (message RAM) STM32G4.
- **Wybór backendu:** API CAN przyjmuje `hal_can_config_t`. Włącz
  `HAL_ENABLE_MCP2515` dla klasycznego backendu MCP2515 lub
  `HAL_ENABLE_MCP251XFD` dla wsparcia CAN FD w MCP2517FD/MCP2518FD. Obie flagi
  kontrolerów zewnętrznych dołączają publiczną warstwę CAN oraz zależność SPI. Włącz
  `HAL_ENABLE_STM32G474_FDCAN` dla natywnego FDCAN1 na STM32G474; ta flaga
  dołącza wyłącznie publiczną warstwę CAN i powoduje błąd buildu na innych
  targetach. Sama flaga `HAL_ENABLE_CAN` nie dołącza już SPI: włącza wspólne API
  i wymaga wskazania backendu.

**Thread safety:** API jest thread-safe i może być używane z wielu rdzeni. Każdy kanał
ma własny mutex `hal_mutex_t`. `hal_can_receive()` utrzymuje blokadę od sprawdzenia
dostępności do zakończenia odczytu ramki, co eliminuje race condition typu TOCTOU.

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
  obsługuje dodatkowo `HAL_CAN_MODE_FD` na uchwytach z obsługą FD; STM32G474
  FDCAN obsługuje tryb FD, loopback, listen-only i one-shot oraz przejścia do
  trybów sleep i konfiguracji za pośrednictwem CCCR/TEST.
  API stanu i liczników błędów przekształca zawartość rejestrów kontrolera do
  `hal_can_state_t` i `hal_can_error_counters_t`.
- **Filtry:** `hal_can_set_filter()` programuje jeden slot id/maska.
  `HAL_CAN_MAX_FILTERS` (6) to *minimalna* liczba sprzętowych filtrów akceptacji
  gwarantowana przez każdy backend, a zatem liczba slotów, na której może polegać
  przenośny kod. W MCP2515 odpowiadają one sześciu filtrom sprzętowym. MCP251XFD
  i STM32G474 FDCAN używają pierwszych sześciu sprzętowych obiektów filtrów
  kierowanych do RX FIFO 0, choć sprzęt może udostępniać ich więcej.
  `hal_can_set_std_filters()` pozostaje wygodnym pomocnikiem dla dwóch
  dokładnych 11-bitowych ID. Zaprogramowanie filtra MCP2515 czyści też tryb
  odbioru dowolnego (receive-any) na obu sprzętowych buforach odbiorczych, więc
  niepasujące ramki są odrzucane, zanim zajmą którykolwiek bufor.
  `hal_can_create_with_retry()` ponawia inicjalizację do `max_retries + 1` prób
  i może automatycznie podłączyć handler IRQ, gdy `int_pin != HAL_CAN_NO_INT_PIN`.
  `hal_can_process_all()` wielokrotnie wywołuje `hal_can_receive()` i przekazuje
  dalej tylko ramki z `id != 0` i `len > 0`.
  `hal_can_encode_temp_i8()` to niewielki helper wspólnego formatu danych dla
  jednobajtowych pól temperatury ze znakiem w ramkach CAN. Obcina wejściową
  wartość typu `float` w stronę zera, nasyca ją do zakresu `int8_t` i zwraca
  odpowiadający bajt payloadu w zapisie uzupełnienia do dwóch.

**Tryb TX one-shot:** `hal_can_create()` domyślnie włącza tryb one-shot MCP2515
(`CANCTRL.OSM = 1`) po inicjalizacji. Można go wyłączyć poprzez
`cfg.mcp2515.one_shot_tx`. W trybie one-shot, gdy wysłana ramka nie otrzyma
ACK (np. brak innego węzła na magistrali), sprzęt natychmiast zwalnia bufor
TX zamiast retransmitować w nieskończoność. Zapobiega to wyczerpaniu buforów TX:
bez one-shot już 3 kolejne ramki bez ACK trwale blokują wszystkie 3 bufory
TX, powodując, że każde kolejne `hal_can_send()` zawodzi z
`CAN_GETTXBFTIMEOUT`.

Dla aplikacji z okresowym rozgłaszaniem, w których świeże dane i tak zostaną
wysłane przy następnym tyknięciu timera, one-shot nie ma praktycznych wad -
pojedynczą utraconą ramkę zastąpi kolejna aktualizacja. Nadawcy wysyłający dane
tylko po zmianie muszą natomiast ponawiać nieudane wysłanie, dodać okresowy
heartbeat albo wyłączyć `one_shot_tx`. W przeciwnym razie jedna utracona ramka
może pozostawić u odbiorcy nieaktualne dane.

Gdy magistrala jest sprawna, a wszyscy odbiorcy
obecni, zachowanie one-shot jest identyczne jak w trybie normalnym:
pierwsza próba się udaje i ponowienie nie jest potrzebne. W trybie one-shot
brak ACK, utrata arbitrażu, przerwana transmisja lub błąd magistrali są
zgłaszane przez `hal_can_send()` jako `false` i logowane przez
`hal_derr_limited("can", ...)`, aby uniknąć zalewania portu szeregowego.
Tryb normalny kontynuuje retransmisję sprzętową i zgłasza sukces, gdy
późniejsza próba się powiedzie.

---

## `hal_hd44780` - wyświetlacz znakowy LCD HD44780  *(opcjonalny - `HAL_ENABLE_HD44780`)*

Driver równoległych wyświetlaczy znakowych LCD zgodnych z HD44780.
Obsługuje te same 4- i 8-bitowe tryby transferu GPIO co oryginalna
biblioteka LiquidCrystal, wraz z opcjonalnym `RW`, niestandardowymi znakami
CGRAM, sterowaniem kursorem/wyświetlaczem, przewijaniem, autoscrollem oraz
konfigurowalnymi przesunięciami wierszy.

```cpp
#include <hal/display/hal_hd44780.h>

// Tryb 4-bitowy, RW podpięte do GND:
HD44780 lcd(rs_pin, enable_pin, d4_pin, d5_pin, d6_pin, d7_pin);

// Tryb 4-bitowy z pinem RW:
HD44780 lcd_rw(rs_pin, rw_pin, enable_pin, d4_pin, d5_pin, d6_pin, d7_pin);

// Tryb 8-bitowy:
HD44780 lcd8(rs_pin, enable_pin,
             d0_pin, d1_pin, d2_pin, d3_pin,
             d4_pin, d5_pin, d6_pin, d7_pin);

lcd.begin(16, 2);
lcd.clear();
lcd.print("JaszczurHAL");
lcd.setCursor(0, 1);
lcd.print(hal_millis() / 1000u);

uint8_t glyph[8] = {0x00, 0x04, 0x0E, 0x15, 0x04, 0x04, 0x04, 0x00};
lcd.createChar(0, glyph);
lcd.write((uint8_t)0);
```

- **Wspólna implementacja modułu:** `hal/display/hd44780/hd44780.*` jest używana
  przez RP2040, STM32G474 oraz testy hostowe. Driver
  korzysta z HAL GPIO, `hal_delay_us()` oraz mutexu `hal_mutex_t` instancji.
- **Zakres klasy:** Jest to driver znakowego LCD. Do grafiki bitmapowej na
  wyświetlaczach TFT/OLED przez SPI służy `hal_display`.
- **Czasowanie:** Opóźnienia inicjalizacji, czyszczenia/home, impulsu enable i
  wykonania komendy odpowiadają sprawdzonej sekwencji HD44780: 50 ms
  oczekiwania na zasilanie, próby inicjalizacji 4,5 ms/150 us, 2 ms opóźnienia
  czyszczenia/home oraz fazy impulsu enable 1/1/100 us.

**Thread safety:** Każda instancja `HD44780` ma mutex HAL chroniący metody publiczne.
Dzięki temu zadania działające na wielu rdzeniach lub pod FreeRTOS nie mogą przeplatać
sekwencji komend i danych GPIO kierowanych do tego samego wyświetlacza.
API nie jest przeznaczone do wywoływania z ISR, ponieważ `hal_mutex_lock`
nie jest ISR-safe.

---

## `hal_display` - wyświetlacz TFT / OLED / LCD / EPD  *(opcjonalny - `HAL_ENABLE_DISPLAY`)*

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
  helperem zapisu strumienia przez DMA.
  Wariant asynchroniczny,
  `hal_display_write_pixels_dma_async_start()` / `_busy()` / `_wait()`, korzysta z
  `hal_spi_write_dma_async_*()` w driverach ILI9341 i ST77xx. Gdy backend rzeczywiście
  działa asynchronicznie, bufor `pixels_be` musi pozostać dostępny i niezmieniony, a
  strumień zapisu wyświetlacza otwarty do zakończenia `_wait()`.
  `hal_display_end_write()` czeka na aktywne asynchroniczne DMA pikseli przed
  zamknięciem transakcji TFT.
- **Uwagi ST77xx/GC9A01:** `HAL_DISPLAY_ST7735`, `HAL_DISPLAY_ST7789`,
  `HAL_DISPLAY_ST7796S` i `HAL_DISPLAY_GC9A01` korzystają ze wspólnego backendu
  zgodnego z rodziną ST77xx. `JH_ST77XX_SPI_DEFAULT_HZ` można nadpisać przed
  dołączeniem lub buildem drivera, aby dostroić domyślny zegar SPI TFT dla
  danej płytki. ST7796S zachowuje udokumentowane domyślne ustawienia w kolejności
  BGR bez wymuszania zamienionych komend inwersji. GC9A01 używa lokalnej sekwencji
  komend Zephyr GC9x01x i domyślnie ustawia 240x240. SSD1331/SSD135x to
  backendy RGB565 w trybie bezpośrednim. Obsługują bezpośredni zapis bufora, dotychczasowe
  strumieniowanie oraz prymitywy GFX. Bezpośredni zapis i GFX obsługują obecnie wyłącznie
  natywną orientację. ST7567 celowo udostępnia tylko bezpośredni, stronicowy zapis;
  jego właściwości nie deklarują dotychczasowego GFX,
  strumieniowania ani DMA.
- **impl/rp2040:** Korzysta ze wspólnego stosu HAL display. ILI9341 i ST77xx
  używają wspólnych driverów HAL SPI/GPIO; OLED-y z rodziny SSD1306
  używają wspólnego drivera HAL I2C/SPI; geometria, bitmapy i renderowanie
  tekstu działają przez wspólny silnik `jh_gfx`.
- **impl/stm32g474:** Korzysta z tego samego wspólnego stosu HAL display co RP2040.
- **impl/.mock:** deterministyczny mock hosta, którego stan można sprawdzać w testach.

**Thread safety:** Backendy sprzętowe chronią operacje wyświetlacza wewnętrznym
`hal_mutex_t`. Podczas strumieniowania TFT mutex pozostaje zablokowany między
`hal_display_begin_write()` a
`hal_display_end_write()`, w tym podczas oczekiwania na asynchroniczne DMA.
Backend mock jest niezsynchronizowany i przeznaczony do testów jednowątkowych.

**Pomocnicy mock:**
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
automatycznie włącza I2C dla historycznego pomocnika; transport SPI OLED
wymaga też `HAL_ENABLE_SPI`.

---


---

*Dalej: [Czujniki](11_sensors.md)*
