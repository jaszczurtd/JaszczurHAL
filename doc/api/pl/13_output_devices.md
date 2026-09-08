<a id="urządzenia-wyjściowe---rgb-led-cyfrowy-potencjometr-pga2311-proste-układy-io-mfrc522-pn532-funkcje-pomocnicze-matematyczne"></a>

# Diody, regulatory, układy I/O i czytniki RFID/NFC

*Dostępne również [po angielsku](../en/13_output_devices.md).*

> **Część [Dokumentacji API JaszczurHAL](../../pl/JaszczurHAL_API.md)**

Rozdział opisuje sterowanie diodami NeoPixel, potencjometrami cyfrowymi i głośnością, obsługę ekspanderów GPIO i przetwornika DAC oraz komunikację z czytnikami RFID/NFC. Zawiera także opis funkcji numerycznych `hal_math`.

<a id="hal_math---lekkie-funkcje-pomocnicze-numeryczne"></a>

## `hal_math` - funkcje numeryczne

`hal_math.h` udostępnia przenośne funkcje numeryczne dla C i C++.

```c
#include <hal/core/hal_math.h>

#define hal_constrain(v, lo, hi) ...
#define hal_map(x, in_min, in_max, out_min, out_max) ...

static inline float hal_math_round_to_n(float v, int n);
```

`hal_math_round_to_n` zaokrągla do `n` miejsc po przecinku (`n < 0` -> `0`, `n > 6` -> `6`).
Wartości połówkowe są zaokrąglane w kierunku od zera.

---

<a id="hal_rgb_led---dioda-statusowa-neopixel--opcjonalne---hal_enable_rgb_led"></a>

## `hal_rgb_led` - diody NeoPixel  *(opcjonalne - `HAL_ENABLE_RGB_LED`)*

Ustawianie koloru i jasności diod NeoPixel oraz ich wyłączanie. Obsługiwany format pikseli określa konfiguracja inicjalizacji.

```c
#include <hal/gpio/hal_rgb_led.h>

typedef enum {
    HAL_RGB_LED_NONE   = 0,
    HAL_RGB_LED_RED    = 1,
    HAL_RGB_LED_GREEN  = 2,
    HAL_RGB_LED_YELLOW = 3,
    HAL_RGB_LED_WHITE  = 4,
    HAL_RGB_LED_BLUE   = 5,
    HAL_RGB_LED_PURPLE = 6,
} hal_rgb_led_color_t;

typedef enum {
    HAL_RGB_LED_PIXEL_RGB_KHZ800  = 0x0006,  // kolejność bajtów RGB, 800 kHz
    HAL_RGB_LED_PIXEL_GRB_KHZ800  = 0x0052,  // kolejność bajtów GRB, 800 kHz (WS2812B, RP2040-Zero)
    HAL_RGB_LED_PIXEL_RGBW_KHZ800 = 0x0018,  // kolejność bajtów RGBW, 800 kHz
} hal_rgb_led_pixel_type_t;

// Inicjalizacja z domyślną kolejnością bajtów RGB
hal_status_t hal_rgb_led_init(uint8_t pin, uint8_t num_pixels);

// Inicjalizacja z jawnym typem pikseli (użyj HAL_RGB_LED_PIXEL_GRB_KHZ800 dla WS2812B)
hal_status_t hal_rgb_led_init_ex(uint8_t pin, uint8_t num_pixels,
                                 hal_rgb_led_pixel_type_t pixel_type);

// Ustaw jasność [1, 255]; domyślnie 30. Efekt widoczny przy następnym wywołaniu set_color().
void hal_rgb_led_set_brightness(uint8_t brightness);

// Ustaw kolor. Powtórzone wywołania z tym samym kolorem są pomijane (brak ruchu na transporcie LED).
hal_status_t hal_rgb_led_set_color(hal_rgb_led_color_t color);

// Wyłącz diodę (odpowiednik set_color(HAL_RGB_LED_NONE))
hal_status_t hal_rgb_led_off(void);
```

Funkcje inicjalizacji, ustawiania koloru i wyłączania, które wcześniej zwracały
`void`, zwracają teraz status. Dotychczasowy kod może nadal ignorować wynik.
Nieprawidłowa liczba/typ pikseli lub kolory zwracają `HAL_EINVAL`, zapisy
koloru przed inicjalizacją zwracają `HAL_EUNINIT`, błędy alokacji/zasobów
zwracają `HAL_ENOMEM`, a błędy transportu zwracają `HAL_EIO`.
`hal_rgb_led_init_ex()` zachowuje dotychczasową nazwę, ponieważ `_ex` już
oznacza wariant z jawnym typem pikseli.

- **impl/rp2040:** wspólny rdzeń `hal/gpio/neopixel/jh_neopixel.*` + transport PIO RP2040 (`hal/gpio/neopixel/rp2040_pio.h`).
- **impl/stm32g474:** wspólny rdzeń `hal/gpio/neopixel/jh_neopixel.*` + transport GPIO synchronizowany cyklami w `impl/stm32g474/hal_rgb_led.cpp`.
- **impl/esp32:** wspólny rdzeń NeoPixel + kanał TX RMT ESP-IDF i koder bajtów.
  Transport obsługuje publiczne formaty pikseli 800 kHz, oczekuje na
  zakolejkowane zakończenie transmisji i przed zwróceniem sterowania stosuje
  interwał resetu/zatrzasku (latch). Ponowna inicjalizacja wyłącza i usuwa
  poprzedni kanał RMT przed usunięciem jego kodera. Jeśli usuwanie się nie powiedzie,
  odpowiedni uchwyt pozostaje dostępny do kolejnej próby zwolnienia zasobów;
  uchwyty są czyszczone dopiero po potwierdzeniu usunięcia przez ESP-IDF.
- **impl/.mock:** zapisuje parametry inicjalizacji, typ pikseli, jasność i ostatni kolor; funkcje testowe mocka pozwalają sterować jego zachowaniem w testach.

**Współbieżność:** Na RP2040, STM32G474 i ESP32-S3 wywołania HAL są chronione przed równoczesnym dostępem. Muteks HAL
chroni stan singletona paska diod i
dostęp do transportu. Backend mock jest niezsynchronizowany i przeznaczony do
testów jednowątkowych.

**Funkcje pomocnicze mocka:**
```c
bool                hal_mock_rgb_led_is_initialized(void);
hal_rgb_led_color_t hal_mock_rgb_led_get_color(void);
uint8_t             hal_mock_rgb_led_get_brightness(void);
hal_rgb_led_pixel_type_t hal_mock_rgb_led_get_pixel_type(void);
uint8_t             hal_mock_rgb_led_get_pin(void);
uint8_t             hal_mock_rgb_led_get_num_pixels(void);
void                hal_mock_rgb_led_reset(void);
void                hal_mock_rgb_led_fail_next_init(bool fail);
void                hal_mock_rgb_led_fail_next_write(bool fail);
```

---


<a id="hal_digipot---cyfrowe-potencjometry-i2c--opcjonalne---hal_enable_digipot"></a>

## `hal_digipot` - potencjometry cyfrowe  *(opcjonalne - `HAL_ENABLE_DIGIPOT`)*

Ustawianie rezystancji potencjometrów MCP401x i MAX5395 przez I2C. Warianty `_ex` pozwalają odróżnić błędne parametry od problemów z komunikacją.

```c
#include <hal/analog/hal_digipot.h>

hal_status_t hal_digipot_init_ex(const hal_digipot_config_t *cfg,
                                 hal_digipot_t *out);
hal_digipot_t hal_digipot_init(const hal_digipot_config_t *cfg);

hal_status_t hal_digipot_set_resistance_ex(hal_digipot_t h, uint32_t ohms);
bool hal_digipot_set_resistance(hal_digipot_t h, uint32_t ohms);

void hal_digipot_deinit(hal_digipot_t h);
uint16_t hal_digipot_step_count(hal_digipot_t h);
uint32_t hal_digipot_e2e_resistance(hal_digipot_t h);
hal_digipot_mode_t hal_digipot_mode(hal_digipot_t h);
```

`hal_digipot_init_ex()` zwraca `HAL_EINVAL` dla nieprawidłowej konfiguracji, `HAL_ENOMEM` po wyczerpaniu statycznej puli oraz `HAL_EBUS` przy błędzie inicjalizacji układu lub magistrali. `hal_digipot_set_resistance_ex()` zwraca `HAL_EUNINIT` dla nieprawidłowego uchwytu, `HAL_EINVAL` dla nieprawidłowej rezystancji lub trybu, `HAL_EBUS` przy błędzie I2C i `HAL_EIO` przy niezgodności odczytu kontrolnego MCP401x. Dotychczasowe funkcje `hal_digipot_init()` i `hal_digipot_set_resistance()` pozostają dostępne dla zachowania zgodności kodu źródłowego.

**Wspólna implementacja modułu:** `hal_digipot.cpp` zarządza pulą uchwytów, sprawdza
argumenty, wybiera backend i utrzymuje osobny muteks każdej instancji. Obsługa transakcji
właściwa dla układów MCP401x/MAX5395 znajduje się w `hal/analog/digipot/`.

**Współbieżność:** Operacje w runtime są serializowane osobno dla każdej instancji, a
każda transakcja z układem korzysta z funkcji HAL I2C.

---


<a id="hal_pga2311---stereofoniczny-regulator-głośności-pga2311--opcjonalne---hal_enable_pga2311"></a>

## `hal_pga2311` - stereofoniczna regulacja głośności  *(opcjonalne - `HAL_ENABLE_PGA2311`)*

Ustawianie wzmocnienia obu kanałów PGA2311 i wyciszanie dźwięku. Sterownik korzysta z SPI; piny magistrali konfiguruje aplikacja.

```c
#include <hal/audio/hal_pga2311.h>

#define HAL_PGA2311_PIN_NONE            0xFFu
#define HAL_PGA2311_MUTE_PIN_NONE       HAL_PGA2311_PIN_NONE
#define HAL_PGA2311_SPI_DEFAULT_HZ      1000000UL

#define HAL_PGA2311_CODE_MUTE           0x00u
#define HAL_PGA2311_CODE_MIN            0x01u
#define HAL_PGA2311_CODE_0DB            0xC0u
#define HAL_PGA2311_CODE_MAX            0xFFu

#define HAL_PGA2311_GAIN_HALF_DB_MIN   (-191)
#define HAL_PGA2311_GAIN_HALF_DB_MAX   (63)

#define HAL_PGA2311_GAIN_DB_MIN        (-95.5f)
#define HAL_PGA2311_GAIN_DB_MAX        (31.5f)

typedef enum {
  HAL_PGA2311_MUTE_ACTIVE_LOW = 0,
  HAL_PGA2311_MUTE_ACTIVE_HIGH = 1,
} hal_pga2311_mute_polarity_t;

typedef struct {
  uint8_t spi_bus;
  uint8_t cs_pin;
  uint8_t mute_pin;
  hal_pga2311_mute_polarity_t mute_polarity;
  uint32_t spi_clock_hz;
  uint8_t spi_bit_order;
  uint8_t spi_mode;
  bool start_muted;
} hal_pga2311_config_t;

typedef struct hal_pga2311_impl_s hal_pga2311_impl_t;
typedef hal_pga2311_impl_t *hal_pga2311_t;

hal_pga2311_config_t hal_pga2311_default_config(void);
hal_status_t hal_pga2311_init_ex(const hal_pga2311_config_t *cfg,
                                 hal_pga2311_t *out_handle);
hal_pga2311_t hal_pga2311_init(const hal_pga2311_config_t *cfg);
void hal_pga2311_deinit(hal_pga2311_t h);

hal_status_t hal_pga2311_set_raw_ex(hal_pga2311_t h, uint8_t left_code,
                                    uint8_t right_code);
bool hal_pga2311_set_raw(hal_pga2311_t h, uint8_t left_code, uint8_t right_code);
hal_status_t hal_pga2311_set_raw_both_ex(hal_pga2311_t h, uint8_t code);
bool hal_pga2311_set_raw_both(hal_pga2311_t h, uint8_t code);
hal_status_t hal_pga2311_set_gain_half_db_ex(hal_pga2311_t h,
                                             int16_t left_half_db,
                                             int16_t right_half_db);
bool hal_pga2311_set_gain_half_db(hal_pga2311_t h, int16_t left_half_db, int16_t right_half_db);
hal_status_t hal_pga2311_set_gain_db_ex(hal_pga2311_t h, float left_db,
                                        float right_db);
bool hal_pga2311_set_gain_db(hal_pga2311_t h, float left_db, float right_db);
hal_status_t hal_pga2311_set_gain_db_both_ex(hal_pga2311_t h, float db);
bool hal_pga2311_set_gain_db_both(hal_pga2311_t h, float db);

hal_status_t hal_pga2311_set_mute_ex(hal_pga2311_t h, bool mute);
bool hal_pga2311_set_mute(hal_pga2311_t h, bool mute);
bool hal_pga2311_is_muted(hal_pga2311_t h);

bool hal_pga2311_get_target_raw(hal_pga2311_t h, uint8_t *left_code, uint8_t *right_code);
bool hal_pga2311_get_target_gain_half_db(hal_pga2311_t h, int16_t *left_half_db, int16_t *right_half_db);

hal_status_t hal_pga2311_gain_half_db_to_raw_ex(int16_t half_db,
                                                uint8_t *out_code);
bool hal_pga2311_gain_half_db_to_raw(int16_t half_db, uint8_t *out_code);
hal_status_t hal_pga2311_raw_to_gain_half_db_ex(uint8_t code,
                                                int16_t *out_half_db);
bool hal_pga2311_raw_to_gain_half_db(uint8_t code, int16_t *out_half_db);
```

**Uwagi dotyczące zachowania:**
- `HAL_ENABLE_PGA2311` automatycznie włącza `HAL_ENABLE_SPI` poprzez generowany
  rejestr funkcji, dołączany przez `hal_config.h`.
- Moduł nie wywołuje `hal_spi_init()`; piny magistrali SPI konfiguruje aplikacja.
- Status inicjalizacji rozróżnia nieprawidłową konfigurację (`HAL_EINVAL`),
  wyczerpanie puli statycznej lub muteksów (`HAL_ENOMEM`) oraz przekazywane
  dalej błędy konfiguracji/zapisu SPI.
- Funkcje ustawiające i konwertujące, które zwracają status, zgłaszają `HAL_EINVAL` dla
  nieprawidłowych uchwytów, wskaźników wyjściowych lub zakresów wzmocnienia
  oraz przekazują dalej błędy transportu SPI. Istniejące funkcje zwracające uchwyt lub
  `bool` pozostają wrapperami zgodności.
- Przy `mute_pin == HAL_PGA2311_MUTE_PIN_NONE` wyciszenie jest emulowane
  programowo poprzez zapis `HAL_PGA2311_CODE_MUTE` do obu kanałów i
  przywrócenie zbuforowanych kodów docelowych przy wyłączeniu wyciszenia.
- Przy skonfigurowanym sprzętowym pinie mute wyciszenie przełącza wyłącznie
  GPIO i nie wysyła dodatkowych ramek SPI.

**Wspólna implementacja modułu:** `hal/audio/pga2311/pga2311_driver.*` obsługuje
transport przez HAL SPI/GPIO. Plik `hal_pga2311.cpp` udostępnia publiczne API, statyczną
pulę uchwytów oraz osobny muteks każdej instancji.

**Współbieżność:** Osobny muteks każdej instancji serializuje wywołania API, a transakcje
SPI są otoczone `hal_spi_lock()` / `hal_spi_unlock()`.

---

<a id="hal_mfrc522---czytnik-rfid-mfrc522--opcjonalne---hal_enable_mfrc522"></a>

## `hal_mfrc522` - czytnik RFID  *(opcjonalne - `HAL_ENABLE_MFRC522`)*

Komunikacja z czytnikiem MFRC522 przez SPI lub I2C. Aplikacja wybiera transport i inicjalizuje jego magistralę przed użyciem czytnika.

### API C

Interfejs C rozdziela transport magistrali od czytnika. Każdy z nich jest
reprezentowany przez uchwyt do struktury z ukrytymi polami (ang. opaque
handle), dalej nazywany uchwytem do instancji. Uchwyty trzeba zwalniać w
kolejności odwrotnej do tworzenia.

```c
#include <hal/nfc/hal_mfrc522.h>
#include <hal/spi/hal_spi.h>

hal_status_t read_mfrc522_uid(hal_mfrc522_uid_t *out_uid) {
    if (out_uid == NULL) return HAL_EINVAL;

    hal_status_t status = hal_spi_init(0u, 0u, 3u, 2u);
    if (status != HAL_OK) return status;

    hal_mfrc522_spi_config_t config = hal_mfrc522_spi_default_config(5u);
    hal_mfrc522_transport_t transport = NULL;
    hal_mfrc522_t reader = NULL;
    bool present = false;

    status = hal_mfrc522_transport_create_spi(&config, &transport);
    if (status == HAL_OK) status = hal_mfrc522_create(transport, &reader);
    if (status == HAL_OK) status = hal_mfrc522_begin(reader);
    if (status == HAL_OK) status = hal_mfrc522_is_new_card_present(reader, &present);
    if (status == HAL_OK && !present) status = HAL_ENOENT;
    if (status == HAL_OK) status = hal_mfrc522_read_uid(reader, out_uid);

    if (reader != NULL) {
        hal_status_t close_status = hal_mfrc522_destroy(reader);
        if (status == HAL_OK) status = close_status;
    }
    if (transport != NULL) {
        hal_status_t close_status = hal_mfrc522_transport_destroy(transport);
        if (status == HAL_OK) status = close_status;
    }
    return status;
}
```

`hal_mfrc522_transport_create_spi()` przechowuje pin chip-select, opcjonalny
pin resetu, numer magistrali i ustawienia SPI. Po włączeniu `HAL_ENABLE_I2C`
utworzenie transportu I2C zapisuje numer magistrali, 7-bitowy adres i
opcjonalny pin resetu. Zainicjalizowana magistrala HAL nadal należy do
aplikacji.

Do transportu można dołączyć jeden czytnik. Próba zniszczenia używanego
transportu zwraca `HAL_EBUSY`; najpierw wywołaj `hal_mfrc522_destroy()`, a
potem `hal_mfrc522_transport_destroy()`. Domyślne statyczne pule mieszczą
`HAL_MFRC522_MAX_TRANSPORTS` transportów i `HAL_MFRC522_MAX_READERS` czytników.

Wykrywanie karty zwraca `hal_mfrc522_uid_t` z bajtami UID, długością i SAK.
Funkcje `hal_mfrc522_card_type_from_sak()` i
`hal_mfrc522_card_type_name()` określają typ karty. Operacje zwracające status
obejmują też REQA, WUPA, zatrzymanie karty, sterowanie anteną i zasilaniem,
autotest, uwierzytelnianie MIFARE, odczyt i zapis bloków oraz operacje na
wartościach MIFARE Classic, zapis Ultralight, uwierzytelnianie NTAG216, a także
zmianę i naprawę sektora UID.

Implementacja zachowuje logikę protokołu MFRC522 zaczerpniętą ze sterowników
MFRC522-spi-i2c-uart-async i Miguela Balboi, zastępując wywołania transportu i
czasowania funkcjami JaszczurHAL. `StatusCodeToHalStatus()` przekształca wyniki
specyficzne dla sterownika na wspólne wartości `hal_status_t`.

**Współbieżność:** Operacje czytnika są serializowane dla każdej instancji, a
transakcje SPI/I2C korzystają z blokad magistrali HAL. Za utworzenie i
zniszczenie transportu oraz czytnika powinno odpowiadać jedno zadanie. Nie
niszcz ich, gdy inne zadanie korzysta z czytnika.

### API C++ zachowane dla zgodności

Ten sam publiczny nagłówek nadal udostępnia typy `MFRC522_SPI`, `MFRC522_I2C`
i `MFRC522` podczas kompilacji C++. Kod korzystający z tych klas pozostaje
obsługiwany.

```cpp
#include <hal/nfc/hal_mfrc522.h>

MFRC522_SPI bus(5, HAL_MFRC522_PIN_NONE, 0);
MFRC522 reader(&bus);

void start_mfrc522_cpp(void) {
    reader.PCD_Init();
    if (reader.PICC_IsNewCardPresent()) {
        (void)reader.PICC_ReadCardSerial();
    }
}
```

W nowym kodzie, który ma jawnie zarządzać cyklem życia transportu i czytnika
oraz korzystać ze wspólnych kodów statusu, preferuj opisany wyżej interfejs C.

Przykład: `examples/22_rfid_nfc`.

---

<a id="hal_pn532---czytnik-nfcrfid-pn532--opcjonalne---hal_enable_pn532"></a>

## `hal_pn532` - czytnik NFC/RFID  *(opcjonalne - `HAL_ENABLE_PN532`)*

Wykrywanie pasywnych kart i obsługa podstawowych operacji MIFARE przez PN532. Dostępne są transporty SPI oraz, po włączeniu odpowiednich flag, I2C i UART.

### API C

Każdy z typów `hal_pn532_transport_t` i `hal_pn532_t` jest uchwytem do
instancji. Najpierw utwórz transport, a następnie dołącz do niego jeden
czytnik.

```c
#include <hal/nfc/hal_pn532.h>
#include <hal/spi/hal_spi.h>

hal_status_t read_pn532_uid(hal_pn532_uid_t *out_uid) {
    if (out_uid == NULL) return HAL_EINVAL;

    hal_status_t status = hal_spi_init(0u, 0u, 3u, 2u);
    if (status != HAL_OK) return status;

    hal_pn532_spi_config_t config = hal_pn532_spi_default_config(5u);
    hal_pn532_transport_t transport = NULL;
    hal_pn532_t reader = NULL;
    uint32_t firmware = 0u;

    status = hal_pn532_transport_create_spi(&config, &transport);
    if (status == HAL_OK) status = hal_pn532_create(transport, &reader);
    if (status == HAL_OK) status = hal_pn532_begin(reader);
    if (status == HAL_OK) status = hal_pn532_get_firmware_version(reader, &firmware);
    if (status == HAL_OK) status = hal_pn532_sam_configure(reader);
    if (status == HAL_OK) {
        status = hal_pn532_read_passive_target(
            reader, HAL_PN532_MODULATION_ISO14443A,
            HAL_PN532_DEFAULT_TIMEOUT_MS, out_uid);
    }

    if (reader != NULL) {
        hal_status_t close_status = hal_pn532_destroy(reader);
        if (status == HAL_OK) status = close_status;
    }
    if (transport != NULL) {
        hal_status_t close_status = hal_pn532_transport_destroy(transport);
        if (status == HAL_OK) status = close_status;
    }
    return status;
}
```

`hal_pn532_transport_create_spi()` korzysta z magistrali HAL SPI
zainicjalizowanej przez aplikację. Po włączeniu `HAL_ENABLE_I2C` funkcja
`hal_pn532_transport_create_i2c()` używa bezpośrednich transakcji HAL I2C i
bajtu gotowości PN532. Przy `HAL_ENABLE_UART` funkcja
`hal_pn532_transport_create_uart()` tworzy port HAL UART i odpowiada za jego
zwolnienie. Funkcja `hal_pn532_transport_create_uart_handle()` korzysta z
przekazanego przez aplikację uchwytu UART i go nie niszczy. Uchwyt UART musi
pozostać ważny przez cały czas istnienia transportu. Port można przekonfigurować
lub zniszczyć dopiero po zniszczeniu transportu.

Do transportu można dołączyć jeden czytnik. Próba zniszczenia używanego
transportu zwraca `HAL_EBUSY`; najpierw zwolnij czytnik. Domyślne statyczne
pule mieszczą `HAL_PN532_MAX_TRANSPORTS` transportów i
`HAL_PN532_MAX_READERS` czytników.

`hal_pn532_read_passive_target()` zapisuje UID wykrytego celu w strukturze
`hal_pn532_uid_t`. Pozostałe funkcje zwracające status obsługują odczyt
firmware i konfigurację SAM, wybudzenie, surowe polecenia i sprawdzanie
odpowiedzi, wyszukiwanie celów i wymianę danych z nimi, uwierzytelnianie i
operacje na blokach MIFARE Classic oraz stronach Ultralight/NTAG2xx.

Implementacja zachowuje sposób budowy ramek Adafruit_PN532, obsługę ACK,
zapytanie o firmware, konfigurację SAM, skanowanie celów pasywnych oraz
podstawowe funkcje pomocnicze wymiany MIFARE, jednocześnie zastępując wywołania
transportu i czasowania prymitywami JaszczurHAL.

**Współbieżność:** Operacje czytnika są serializowane dla każdej instancji.
Transporty SPI i I2C także korzystają z blokad magistrali HAL. Za utworzenie i
zniszczenie transportu oraz czytnika powinno odpowiadać jedno zadanie. Nie
niszcz ich, gdy inne zadanie korzysta z czytnika.

### API C++ zachowane dla zgodności

Ten sam nagłówek nadal udostępnia typ `PN532` oraz transporty `PN532_SPI`,
`PN532_I2C` i `PN532_UART`, jeśli włączono ich flagi. Kod korzystający z tych
klas pozostaje obsługiwany.

```cpp
#include <hal/nfc/hal_pn532.h>

PN532_SPI bus(5, HAL_PN532_PIN_NONE, 0);
PN532 reader(&bus);

hal_status_t start_pn532_cpp(void) {
    uint32_t firmware = 0u;
    hal_status_t status = reader.begin();
    if (status == HAL_OK) status = reader.getFirmwareVersion(&firmware);
    return status;
}
```

W nowym kodzie, który ma jawnie zarządzać cyklem życia transportu i czytnika,
preferuj opisany wyżej interfejs C.

Przykład: `examples/22_rfid_nfc`.

---

<a id="proste-układy-io--opcjonalne---hal_enable_mcp23017-hal_enable_pca9654e-hal_enable_pcf8574-hal_enable_hc595-hal_enable_mcp4725"></a>

## Ekspandery GPIO, rejestry przesuwne i przetwornik DAC  *(opcjonalne - `HAL_ENABLE_MCP23017`, `HAL_ENABLE_PCA9654E`, `HAL_ENABLE_PCF8574`, `HAL_ENABLE_HC595`, `HAL_ENABLE_MCP4725`)*

```c
#include <hal/gpio/hal_mcp23017.h>
#include <hal/gpio/hal_pca9654e.h>
#include <hal/gpio/hal_pcf8574.h>
#include <hal/gpio/hal_hc595.h>
#include <hal/analog/hal_mcp4725.h>

hal_i2c_init(sda_pin, scl_pin, HAL_I2C_CLOCK_STANDARD_HZ);
hal_spi_init(0, miso_pin, mosi_pin, sck_pin);

/* MCP23017 - 16-bitowy ekspander GPIO I2C. */
hal_mcp23017_t gpio = {0};
hal_mcp23017_init_ex(&gpio, NULL);
hal_mcp23017_write_pin_ex(&gpio, 0, true);

/* PCA9654E - 8-bitowy ekspander wyjść I2C. */
hal_pca9654e_t out8 = {0};
hal_pca9654e_init_ex(&out8, NULL);
hal_pca9654e_write_all_ex(&out8, 0x0Fu);

/* PCF8574 - 8-bitowe quasi-dwukierunkowe GPIO I2C. */
hal_pcf8574_t io8 = {0};
hal_pcf8574_init_ex(&io8, NULL);
hal_pcf8574_write_pin_ex(&io8, 3u, true);
uint8_t input_port = hal_pcf8574_read_all(&io8);

/* 74HC595 - rejestr przesuwny SPI (do 4 połączonych łańcuchowo = 32 wyjścia). */
hal_hc595_config_t sr_cfg = hal_hc595_default_config(cs_pin);
hal_hc595_t sr = {0};
hal_hc595_init_ex(&sr, &sr_cfg);
hal_hc595_write_all_ex(&sr, 0x55u);

/* MCP4725 - 12-bitowy DAC I2C. */
hal_mcp4725_t dac = {0};
hal_mcp4725_init_ex(&dac, NULL);
hal_mcp4725_write_ex(&dac, 2048u); /* ~ pełna skala w połowie */
```

Dostępne moduły:

- `hal_mcp23017`: ekspander GPIO MCP23017 przez I2C. Tryby działania
  odzwierciedlają warianty wtyczki grblHAL: 8 wejść/8 wyjść, 16 wyjść lub
  16 wejść. Inwersja wejść, podciąganie (pull-up) i konfiguracja rejestru
  przerwań MCP są dostępne przez funkcje zwracające `hal_status_t`.
- `hal_pca9654e`: ekspander wyłącznie wyjściowy PCA9654E przez I2C.
  Inicjalizacja zapisuje sekwencję rejestru sterownika źródłowego: wszystkie
  piny jako wyjścia, brak inwersji, wyjścia w stanie niskim.
- `hal_pcf8574`: quasi-dwukierunkowy ekspander GPIO PCF8574 przez I2C.
  Sterownik przechowuje lokalnie ostatnią wartość wyjść, zapisuje pełny
  8-bitowy port w jednej transakcji i odczytuje bieżący stan portu jako
  jeden bajt.
- `hal_hc595`: od jednego do czterech połączonych łańcuchowo rejestrów
  przesuwnych 74HC595 przez HAL SPI oraz pin zatrzasku/chip-select GPIO.
  Bajty są przesuwane od najwyższego rejestru, zgodnie z sterownikiem
  źródłowym.
- `hal_mcp4725`: 12-bitowy DAC MCP4725 przez I2C. Inicjalizacja może wysłać
  sekwencję resetu/wybudzenia general-call, odczytuje bieżącą wartość DAC
  zapisaną w EEPROM i wykonuje aktualizacje DAC w trybie fast-mode.

Przebieg transakcji oparto na działających sterownikach wtyczek grblHAL autorstwa Terje Io.
Implementacja korzysta wyłącznie z funkcji I2C, SPI, GPIO, czasu, statusów i synchronizacji
JaszczurHAL.

Każdy prosty sterownik I/O udostępnia warianty `_ex` transakcji, które mogą zakończyć się
błędem. Dotychczasowe funkcje zwracające `bool` lub wartość pozostają wrapperami
zgodności. Nieprawidłowy wskaźnik urządzenia lub wyniku, pin albo tryb powoduje zwrócenie
`HAL_EINVAL`; użycie przed poprawną inicjalizacją - `HAL_EUNINIT`; błąd utworzenia muteksu
- `HAL_ENOMEM`; a błąd transakcji I2C, SPI lub GPIO - `HAL_EBUS` albo `HAL_EIO`, zależnie
od operacji backendu. Dotychczasowe funkcje odczytu nadal zwracają zero po błędzie. Użyj
wariantu `_ex`, jeśli trzeba odróżnić poprawny zerowy wynik od niepowodzenia.

**Współbieżność:** Każda instancja urządzenia ma własny muteks HAL. Transakcje dodatkowo korzystają z blokad magistral I2C/SPI. Inicjalizacją i zwalnianiem danej instancji powinien zarządzać jeden fragment aplikacji.

Przykład: `examples/23_io_pmic`.

---

*Dalej: [Pamięć masowa](14_storage.md)*
