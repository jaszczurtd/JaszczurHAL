# Narzędzia

*Dostępne również [po angielsku](../en/16_utilities.md).*

> **Część [Dokumentacji API JaszczurHAL](../../pl/JaszczurHAL_API.md)**

Obejmuje: tematyczne helpery HAL, zgodnościowe nagłówki narzędziowe,
`hal_soft_timer`, `hal_pid_controller`, `SmartTimers`, `pidController`,
`multicoreWatchdog` i `draw7Segment`.

## `hal_soft_timer` - interfejs C dla `SmartTimers`

```c
#include <hal/timers/hal_soft_timer.h>

typedef struct hal_soft_timer_impl_s *hal_soft_timer_t;
typedef void (*hal_soft_timer_callback_t)(void);

hal_soft_timer_t hal_soft_timer_create(void);
void             hal_soft_timer_destroy(hal_soft_timer_t timer);

bool     hal_soft_timer_begin(hal_soft_timer_t timer, hal_soft_timer_callback_t callback, uint32_t interval_ms);
void     hal_soft_timer_restart(hal_soft_timer_t timer);
bool     hal_soft_timer_available(hal_soft_timer_t timer);
uint32_t hal_soft_timer_time_left(hal_soft_timer_t timer);
void     hal_soft_timer_set_interval(hal_soft_timer_t timer, uint32_t interval_ms);
void     hal_soft_timer_tick(hal_soft_timer_t timer);
void     hal_soft_timer_abort(hal_soft_timer_t timer);

typedef struct {
    hal_soft_timer_t         *timer;
    hal_soft_timer_callback_t callback;
    uint32_t                  intervalMs;
} hal_soft_timer_table_entry_t;

bool hal_soft_timer_setup_table(const hal_soft_timer_table_entry_t *table,
                                uint32_t count,
                                void (*idle_cb)(void),
                                uint32_t delay_ms);
bool hal_soft_timer_tick_table(const hal_soft_timer_table_entry_t *table,
                               uint32_t count);
```

- **Przeznaczenie:** pozwala modułom napisanym w C korzystać z timerów bez
  bezpośredniej zależności od klasy `SmartTimers`.
- **Implementacja:** korzysta wewnętrznie z `SmartTimers`, więc w runtime działa
  tak samo jak ta klasa.

**Thread safety:** Metody można bezpiecznie wywoływać z wielu wątków i rdzeni.
Każdą instancję chroni mutex utworzony przez `SmartTimers`.

- **Funkcje obsługujące tablicę timerów:** `hal_soft_timer_setup_table(...)`
  tworzy i konfiguruje timery na podstawie tablicy deskryptorów. Opcjonalnie
  wywołuje `idle_cb` i wprowadza opóźnienie między kolejnymi wpisami.
  `hal_soft_timer_tick_table(...)` wykonuje `tick` dla wszystkich wpisów z tej
  samej tablicy.
- **Walidacja:** obie funkcje wymagają `table != NULL` oraz `count > 0`.
  Dla nieprawidłowych argumentów zapisują błąd przez `hal_derr(...)`
  i zwracają `false`.

**Przykład: cykliczna funkcja zwrotna przez interfejs C**

```c
#include <hal/timers/hal_soft_timer.h>

static hal_soft_timer_t status_timer;

static void check_connection(void) {
    if (!hal_wifi_is_connected()) {
        hal_deb("WiFi disconnected, reconnecting...");
        hal_wifi_begin_station("MySSID", "password", true);
    }
}

void app_start(void) {
    hal_debug_init(115200);
    hal_wifi_begin_station("MySSID", "password", false);

    status_timer = hal_soft_timer_create();
    hal_soft_timer_begin(status_timer, check_connection, 5000);
}

void app_task0(void) {
    hal_soft_timer_tick(status_timer);
}
```

---

## `hal_pid_controller` - interfejs C dla `pidController`

```c
#include <hal/control/hal_pid_controller.h>

typedef struct hal_pid_controller_impl_s *hal_pid_controller_t;
typedef enum {
  HAL_PID_DIRECTION_FORWARD = 0,
  HAL_PID_DIRECTION_BACKWARD = 1
} hal_pid_direction_t;

hal_pid_controller_t hal_pid_controller_create(void);
hal_pid_controller_t hal_pid_controller_create_with_gains(float kp, float ki, float kd, float max_integral);
void                 hal_pid_controller_destroy(hal_pid_controller_t controller);

void  hal_pid_controller_set_kp(hal_pid_controller_t controller, float kp);
void  hal_pid_controller_set_ki(hal_pid_controller_t controller, float ki);
void  hal_pid_controller_set_kd(hal_pid_controller_t controller, float kd);
void  hal_pid_controller_set_tf(hal_pid_controller_t controller, float tf);
void  hal_pid_controller_set_max_integral(hal_pid_controller_t controller, float max_integral);

float hal_pid_controller_get_kp(hal_pid_controller_t controller);
float hal_pid_controller_get_ki(hal_pid_controller_t controller);
float hal_pid_controller_get_kd(hal_pid_controller_t controller);
float hal_pid_controller_get_tf(hal_pid_controller_t controller);

void  hal_pid_controller_update_time(hal_pid_controller_t controller, float time_divider);
float hal_pid_controller_update(hal_pid_controller_t controller, float error);
void  hal_pid_controller_set_output_limits(hal_pid_controller_t controller, float min_output, float max_output);
void  hal_pid_controller_reset(hal_pid_controller_t controller);
void  hal_pid_controller_set_direction(hal_pid_controller_t controller, hal_pid_direction_t direction);
bool  hal_pid_controller_is_error_stable(hal_pid_controller_t controller, float error, float tolerance, int stability_threshold);
bool  hal_pid_controller_is_oscillating(hal_pid_controller_t controller, float current_error, int window_size);
```

- **Przeznaczenie:** udostępnia regulator PID jako funkcje C operujące na
  uchwytach ukrywających szczegóły implementacji. Pozwala dzięki temu stopniowo
  przechodzić z API opartego na klasach.
- **Implementacja:** korzysta wewnętrznie z `PIDController`, więc w runtime
  działa tak samo jak ta klasa.

**Thread safety:** Regulator nie synchronizuje dostępu. Każda pętla sterowania
powinna mieć własną instancję; dostęp współbieżny trzeba zabezpieczyć po stronie
aplikacji.

**Ochrona przed nasyceniem całki (anti-windup):** regulator stosuje dwa
uzupełniające się mechanizmy:

1. **Sztywne ograniczenie całki** - `setMaxIntegral()` /
   `hal_pid_controller_set_max_integral()` ogranicza `|integral|`.
2. **Wstrzymanie całkowania przy nasyceniu** - regulator nie zwiększa całki,
   gdy wyjście osiągnęło ograniczenie w kierunku uchybu (tj. wyjście ≥ max
   i uchyb > 0 albo wyjście ≤ min i uchyb < 0). Zapobiega to dalszemu
   narastaniu całki po osiągnięciu limitu wyjścia bez konieczności ręcznego
   dobierania jej ograniczenia.

**Przykład: sterowanie prędkością silnika**

```c
#include <hal/control/hal_pid_controller.h>

static hal_pid_controller_t speed_pid;
static float target_rpm = 1000.0f;
static float current_rpm = 0.0f;

void app_start(void) {
    speed_pid = hal_pid_controller_create_with_gains(
        50.0f,    // Kp
        10.0f,    // Ki
        5.0f,     // Kd
        200.0f    // max_integral
    );
    hal_pid_controller_set_output_limits(speed_pid, 0.0f, 255.0f);
    hal_pid_controller_update_time(speed_pid, 1.0f / 10.0f);
}

void app_task0(void) {
    current_rpm = read_encoder_rpm();
    float error = target_rpm - current_rpm;
    float pwm = hal_pid_controller_update(speed_pid, error);

    hal_pwm_write(MOTOR_PWM_PIN, (uint32_t)pwm);

    if (hal_pid_controller_is_error_stable(speed_pid, error, 10.0f, 5)) {
        hal_deb("Speed stable at %.1f RPM", current_rpm);
    }

    hal_delay_ms(10);
}
```

---

## Tematyczne moduły narzędziowe

Dotychczasowa zbiorcza implementacja została rozdzielona według
odpowiedzialności. Nowy kod dołącza nagłówek właściwej domeny HAL i korzysta z
API z prefiksem. `tools.h`, `tools_c.h` oraz `utils/tools_api.h` pozostają
agregatorami nagłówków bez własnych deklaracji funkcji. Nie istnieje już
jednostka `tools.cpp`.

Zalecane sposoby dołączania nagłówków:

- `#include <JaszczurHAL.h>` jako stabilny agregat publiczny;
- bezpośredni nagłówek domeny dla wąskiej zależności;
- `#include <tools.h>` albo `#include <tools_c.h>` tylko podczas utrzymywania
  starszego kodu.

| Domena | Nagłówek | Główne API |
|---|---|---|
| Tablice | `hal/core/hal_array.h` | `COUNTOF()` |
| Serializacja liczb/endian | `hal/core/jh_endian.h` | `jh_load_*`, `jh_store_*`, `jh_bswap*`, `MSB()`, `LSB()` |
| Obliczenia i sygnały | `hal/core/hal_math.h` | `hal_math_*` |
| Ograniczone operacje tekstowe | `hal/core/hal_text.h` | `hal_text_*` |
| Parsowanie wartości NMEA | `hal/gps/hal_gps_nmea_utils.h` | `hal_gps_nmea_*` |
| Próbkowanie i konwersja ADC | `hal/analog/hal_adc_utils.h` | `hal_adc_*_ex` |
| Konwersja NTC | `hal/temperature/hal_ntc.h` | `hal_ntc_*_ex` |
| Piksele | `hal/display/hal_pixel.h` | `hal_pixel_*` |
| Adaptery PNG/JPEG w pamięci | `hal/codecs/hal_image.h` | `hal_image_*` |
| MAC i skanowanie WiFi | `hal/network/hal_network_utils.h` | `hal_network_*`, `hal_wifi_scan_*` |
| Kalendarz i upływ czasu | `hal/time/hal_time.h` | `hal_time_*`, `hal_get_seconds()` |
| Okresowo odświeżane wartości losowe | `hal/system/hal_periodic_random.h` | `hal_periodic_random_*` |

Funkcje `_ex` zwracają `hal_status_t`, używają jawnych buforów/stanu i
walidują argumenty. Zachowanie ADC, takie jak liczba próbek, pusty odczyt,
opóźnienie i korekcja charakterystyki, wybiera
`hal_adc_average_config_t`, a nie ukryte ustawienia ogólne projektu.

### Funkcje pomocnicze do manipulacji bitami (`hal_bits`)

Każdą definicję aliasu bitowego chroni `#ifndef`, dzięki czemu aplikacja może
dostarczyć własną, równoważną definicję przed dołączeniem tego nagłówka.

```c
#include <hal/core/hal_bits.h>

#define is_set(x, mask)      // true, gdy dowolny bit z maski jest ustawiony w x
#define set_bit(var, mask)   // ustaw bity wskazane przez maskę w var
#define clr_bit(var, mask)   // wyczyść bity wskazane przez maskę w var

// Warianty operujące na indeksie bitu (utrwalone nazwy publiczne)
#define bitSet(var, bit)     // ustaw bit o numerze 'bit' w var
#define bitClear(var, bit)   // wyczyść bit o numerze 'bit' w var
#define bitRead(var, bit)    // odczytaj bit o numerze 'bit' z var (zwraca 0 lub 1)

// Pomocnicze funkcje dla rejestrów volatile (forma wskaźnikowa makra)
#define set_bit_v(reg, mask) // ustaw maskę w *reg
#define clr_bit_v(reg, mask) // wyczyść maskę w *reg

```

Unikaj przekazywania wyrażeń z efektami ubocznymi (na przykład `i++` lub
wywołań funkcji modyfikujących stan), ponieważ argumenty makra mogą zostać
obliczone więcej niż raz.

### Publiczne funkcje pomocnicze

Nazwy funkcji wskazują moduł HAL, do którego należą. `tools.h`, `tools_c.h`
i `tools_api.h` jedynie zbierają właściwe nagłówki i nie publikują drugiego
zestawu nazw funkcji.

```c
void hal_debug_init_default(void);
void hal_debug_set_module_prefix(const char *module_name);

static inline float hal_math_round_to_n(float value, int decimals);
void  hal_math_split_decimal_tenths(float value, int *whole, int *tenths);
float hal_math_join_decimal_tenths(int whole, int tenths);
int   hal_math_percent_to_value(float percent, int maximum);
int   hal_math_percent_from_value(int value, int maximum);
float hal_math_low_pass(float alpha, float input, float previous_output);
float hal_math_blend(float current_value, float new_value, float alpha);
hal_status_t hal_math_rolling_average_f32_ex(size_t *index, size_t *count,
                                              float value, float *table,
                                              size_t table_count,
                                              float *out_average);
hal_status_t hal_math_average_i32_ex(const int *values, size_t count,
                                      int *out_average);
hal_status_t hal_math_min_i32_ex(const int *values, size_t count,
                                  int *out_minimum);
hal_status_t hal_math_midpoint_min_max_i32_ex(const int *values, size_t count,
                                               int *out_midpoint);
float hal_math_rolling_average_default_f32(int *index, int *count,
                                            float value, float *table);
int   hal_math_nonnegative_average_i32(const int *values, int count);
int   hal_math_min_i32(const int *values, int count);
int   hal_math_midpoint_min_max_i32(const int *values, int count);
float hal_math_map_f32(float value, float input_min, float input_max,
                       float output_min, float output_max);
float hal_math_round_tenth(float value);
float hal_math_round_precision(float value, int precision);
static inline uint32_t hal_math_float_to_u32(float value);
static inline float    hal_math_u32_to_float(uint32_t value);

hal_status_t hal_adc_raw_to_voltage_ex(int raw, float reference_voltage,
                                        uint8_t resolution_bits,
                                        float high_side_resistance,
                                        float low_side_resistance,
                                        float *out_voltage);
int hal_adc_compensate_rp2040_12bit(int sample);
hal_status_t hal_adc_read_average_ex(const hal_adc_average_config_t *config,
                                      float *out_average);
float hal_adc_raw_to_voltage(int raw, float high_side_resistance,
                             float low_side_resistance);
float hal_adc_read_average(uint8_t pin);
hal_status_t hal_ntc_temperature_from_adc_ex(
    float adc_average, float adc_full_scale,
    const hal_ntc_beta_config_t *config, float *out_celsius);
hal_status_t hal_ntc_read_temperature_ex(
    const hal_adc_average_config_t *adc_config, float adc_full_scale,
    const hal_ntc_beta_config_t *ntc_config, float *out_celsius);
float hal_ntc_steinhart(float divider_ratio, float nominal_resistance,
                        int resistance, bool characteristic);
float hal_ntc_read_temperature(uint8_t pin, int nominal_resistance,
                               int series_resistance);
unsigned long hal_get_seconds(void);
bool  hal_time_is_daylight_saving_time(int year, int month, int day);
void  hal_time_adjust_cet_cest(int *year, int *month, int *day, int *hour, int *minute);
bool  hal_time_is_in_range(long now, long start, long end);
void  hal_time_extract_minutes(long time_in_minutes, int *hours, int *minutes);
uint16_t jh_load_le16(const uint8_t *input);
uint32_t jh_load_le32(const uint8_t *input);
uint64_t jh_load_le64(const uint8_t *input);
void jh_store_le16(uint8_t *output, uint16_t value);
void jh_store_le32(uint8_t *output, uint32_t value);
void jh_store_le64(uint8_t *output, uint64_t value);
uint16_t jh_load_be16(const uint8_t *input);
uint32_t jh_load_be32(const uint8_t *input);
uint64_t jh_load_be64(const uint8_t *input);
void jh_store_be16(uint8_t *output, uint16_t value);
void jh_store_be32(uint8_t *output, uint32_t value);
void jh_store_be64(uint8_t *output, uint64_t value);
uint16_t jh_bswap16(uint16_t value);
uint32_t jh_bswap32(uint32_t value);
uint64_t jh_bswap64(uint64_t value);
uint8_t  jh_u16_msb(uint16_t value);
uint8_t  jh_u16_lsb(uint16_t value);
uint8_t  MSB(unsigned short value);
uint8_t  LSB(unsigned short value);
uint16_t jh_u16_from_bytes(uint8_t msb, uint8_t lsb);
char *hal_text_format_binary_int(int value, char *buffer, size_t buffer_size);
hal_status_t hal_text_concat_ex(char *destination, size_t destination_size,
                                 const char *first, const char *second);
bool hal_text_concat(char *destination, size_t destination_size,
                     const char *first, const char *second);
bool hal_text_is_printable(const char *text, size_t maximum_size);
hal_status_t hal_text_hex_pair_to_byte_ex(char high, char low,
                                           uint8_t *out_value);
uint8_t hal_text_hex_pair_to_byte(char high, char low);
hal_status_t hal_text_url_decode_ex(const char *source, char *destination,
                                     size_t destination_size,
                                     size_t *out_length);
void  hal_text_url_decode(const char *source, char *destination);
void  hal_text_remove_whitespace(char *text);
bool  hal_text_starts_with(const char *text, const char *prefix);
int   hal_text_parse_number(const char **text);
hal_status_t hal_text_transliterate_ascii_ex(const char *input, char *output,
                                              size_t output_size);
void hal_text_transliterate_ascii(const char *input, char *output,
                                  size_t output_size);
hal_status_t hal_text_pack_field_ex(uint8_t *buffer, size_t width,
                                     const char *text, uint8_t padding);
void hal_text_pack_field_pad(uint8_t *buffer, const char *text, int width,
                             uint8_t padding);
void hal_text_pack_field(uint8_t *buffer, const char *text, int width);
uint16_t hal_pixel_rgb888_to_rgb565(uint8_t red, uint8_t green, uint8_t blue);
hal_status_t hal_pixel_rgb888_buffer_to_rgb565_ex(const uint8_t *rgb,
                                                   uint16_t *rgb565,
                                                   size_t pixel_count);
hal_status_t hal_pixel_rgba8888_buffer_to_rgb565_ex(const uint8_t *rgba,
                                                     uint16_t *rgb565,
                                                     size_t pixel_count);
bool hal_image_png_base64_decoded_size(const char *base64, size_t base64_len,
                                        size_t *png_size);
bool hal_image_png_base64_decode_rgba8888(
    uint8_t **rgba, unsigned *width, unsigned *height, const char *base64,
    size_t base64_len, uint8_t *png_work, size_t png_work_size,
    unsigned *png_error);
bool hal_image_png_base64_decode_rgb565(
    const char *base64, size_t base64_len, uint8_t *png_work,
    size_t png_work_size, uint16_t *rgb565, size_t rgb565_pixels,
    unsigned *width, unsigned *height, unsigned *png_error);
bool hal_image_jpeg_decode_rgb565(const uint8_t *jpeg, size_t jpeg_size,
                                  uint16_t *rgb565, size_t rgb565_pixels,
                                  unsigned *width, unsigned *height);
bool hal_image_jpeg_base64_decoded_size(const char *base64, size_t base64_len,
                                        size_t *jpeg_size);
bool hal_image_jpeg_base64_decode_rgb565(const char *base64, size_t base64_len,
                                         uint8_t *jpeg_work,
                                         size_t jpeg_work_size,
                                         uint16_t *rgb565, size_t rgb565_pixels,
                                         unsigned *width, unsigned *height);
int     hal_gps_nmea_hex_value(char value);
int32_t hal_gps_nmea_decimal_x100(const char *text);
void    hal_gps_nmea_degrees(const char *text, int16_t *degrees,
                             uint32_t *billionths);
hal_status_t hal_network_format_mac_ex(const uint8_t mac[6], char *buffer,
                                        size_t buffer_size);
const char *hal_wifi_encryption_to_string(hal_wifi_encryption_t encryption);
hal_status_t hal_wifi_scan_for_ssid_ex(const char *ssid_prefix,
                                        bool log_results, bool *out_found);
bool hal_wifi_scan_for_ssid(const char *ssid_prefix);
void hal_periodic_random_int_init(hal_periodic_random_int_t *random,
                                  uint32_t seed);
void hal_periodic_random_float_init(hal_periodic_random_float_t *random,
                                    uint32_t seed);
hal_status_t hal_periodic_random_int_get_ex(hal_periodic_random_int_t *random,
                                             uint32_t now_ms,
                                             uint32_t interval_ms,
                                             int maximum, int *out_value);
hal_status_t hal_periodic_random_float_get_ex(
    hal_periodic_random_float_t *random, uint32_t now_ms, uint32_t interval_ms,
    float maximum, float *out_value);
int   hal_periodic_random_int_get(uint32_t interval_ms, int maximum);
float hal_periodic_random_float_get(uint32_t interval_ms, float maximum);
```

Nagłówki domen opisują parametry, jednostki, opcjonalne wyjścia, zachowanie na
granicach i błędy każdej funkcji. Jeśli dostępny jest wariant `_ex`, warto go
wybrać, gdy kod potrzebuje dokładnego statusu albo jawnie przekazywanego stanu.

Funkcje czasu deklaruje `<hal/time/hal_time.h>`, a implementuje
`hal_time.cpp`: `hal_get_seconds()`, `hal_time_is_daylight_saving_time()`,
`hal_time_adjust_cet_cest()`, `hal_time_is_in_range()` oraz
`hal_time_extract_minutes()`. Zobacz
[Funkcje pomocnicze kalendarza oraz opcjonalny czas systemowy/NTP](15_connectivity.md)
na temat zachowania na granicach zakresów, walidacji i zawijania wartości.

---

## `hal_crc` - sumy kontrolne CRC

```c
#include <hal/security/hal_crc.h>
```

Moduł udostępnia niewymagające tablic funkcje CRC, niezależne od backendu.
Służą one do kontroli integralności danych, **nie** do zastosowań
kryptograficznych; funkcje skrótu i HMAC opisano w
[`hal_crypto`](07_crypto.md). Moduł jest opcjonalny i włącza go
`HAL_ENABLE_CRC`. Włączenie `HAL_ENABLE_ONEWIRE` lub
`HAL_ENABLE_DS18B20` także aktywuje CRC, ponieważ komunikacja 1-Wire wymaga
weryfikacji ROM-u i scratchpada.

Wariant CRC określają szerokość, wielomian, wartość początkowa, odbicie bitów
i końcowa operacja XOR. Nazwa każdej funkcji wskazuje więc dokładnie obliczany
wariant. Celowo nie ma ogólnej funkcji `hal_crc16`, która niejawnie narzucałaby
jeden wielomian.

### Funkcje

```c
uint8_t  hal_crc8_maxim(const uint8_t *data, size_t len);   // CRC-8/MAXIM-DOW (1-Wire)
uint16_t hal_crc16_maxim(const uint8_t *data, size_t len, uint16_t crc); // Maxim CRC-16 1-Wire
bool     hal_crc16_maxim_check(const uint8_t *data, size_t len,
                               const uint8_t inverted_crc[2], uint16_t crc);
uint16_t hal_crc16_ccitt(const uint8_t *data, size_t len, uint16_t crc); // CRC-16/CCITT-FALSE, wielomian 0x1021
uint32_t hal_crc32(const uint8_t *data, size_t len);        // CRC-32/ISO-HDLC (zlib/Ethernet)
```

- Wartość początkowa `crc` pozwala funkcjom `hal_crc16_*` kontynuować obliczenia
  dla kolejnych części bufora. Aby rozpocząć nowe obliczenie, przekaż `0` dla
  wariantu Maxim albo `HAL_CRC16_CCITT_INIT` (`0xFFFF`) dla CCITT.
  `hal_crc8_maxim` i `hal_crc32` obliczają CRC jednorazowo dla całego bufora.
- `hal_crc16_maxim_check` odtwarza sposób transmisji używany przez urządzenia
  1-Wire, które przesyłają CRC-16 zanegowane bitowo (dawne
  `hal_onewire_check_crc16`).
- Referencyjne wartości kontrolne dla `"123456789"`: CRC-8/MAXIM `0xA1`,
  CRC-16/CCITT-FALSE `0x29B1`, CRC-32 `0xCBF43926` - potwierdzone przez
  `tests/test_hal_crc.cpp`.

Nowy wariant, na przykład `hal_crc16_ccitt` lub `hal_crc32c`, można dodać
niezależnie, bez zmieniania istniejących funkcji.

---

## SmartTimers

```c
#include <hal/timers/smart_timers/SmartTimers.h>

SmartTimers timer;

// Konfiguracja i start - sygnatura callbacku: void f(void)
timer.begin(callback, interval_ms);

// Krok timera; wywołuje callback po upłynięciu interwału, po czym automatycznie restartuje.
// Wywołuj w głównej pętli.
timer.tick();

// true, jeśli interwał upłynął od ostatniego restartu (nieblokujące)
bool ready = timer.available();

// Pozostałe milisekundy do następnego callbacku.
// Zwraca 0, gdy interwał już upłynął lub timer jest zatrzymany. NIE zwraca skonfigurowanego interwału.
uint32_t remaining = timer.time();

// Zmień interwał bez resetowania znacznika czasu.
// Wywołaj potem restart(), jeśli chcesz, aby liczenie zaczęło się od teraz.
timer.time(new_interval_ms);

// Zresetuj wewnętrzny znacznik czasu do teraz (interwał zaczyna liczenie od tego momentu)
timer.restart();

// Zatrzymaj timer
timer.abort();
```

> **Uwaga:** Makra `SECOND`, `SECS()`, `MINS()`, `HOURS()` są zdefiniowane w
> `hal/system/hal_system.h`
> (dołączanym automatycznie przez `hal/timers/smart_timers/SmartTimers.h`).

**Thread safety:** Po utworzeniu obiektu wszystkie metody można bezpiecznie
wywoływać z wielu wątków i rdzeni. Każda instancja od razu tworzy własny
`hal_mutex_t`, który chroni wywołania metod. Funkcje zwrotne przekazane do
`begin()` są wywoływane poza sekcją chronioną mutexem, aby nie powodować
deadlocka.

**Przykład: tabela wielu timerów**

```c
#include <hal/timers/smart_timers/SmartTimers.h>

SmartTimers heartbeat_timer;
SmartTimers status_timer;
SmartTimers led_timer;

static void on_heartbeat(void) {
    hal_deb("[heartbeat] alive");
}

static void on_status(void) {
    hal_deb("[status] free_heap=%lu bytes", hal_get_free_heap());
}

static void on_led_blink(void) {
    hal_gpio_write(LED_PIN, !hal_gpio_read(LED_PIN));
}

void app_start(void) {
    hal_debug_init(115200);
    hal_gpio_set_mode(LED_PIN, HAL_GPIO_OUTPUT);

    // Uruchom trzy niezależne timery
    heartbeat_timer.begin(on_heartbeat, 1000);  // 1 sekunda
    status_timer.begin(on_status, 5000);        // 5 sekund
    led_timer.begin(on_led_blink, 500);         // 500 ms
}

void app_task0(void) {
    // Odpytaj (tick) wszystkie timery
    heartbeat_timer.tick();
    status_timer.tick();
    led_timer.tick();

    if (heartbeat_timer.available()) {
        hal_deb("Heartbeat ready");
    }

    hal_delay_ms(10);
}
```

---

## pidController

```c
#include <utils/pidController.h>

// Struktura parametrów strojenia
typedef struct { float kP; float kI; float kD; float Tf; } PIDValues;

enum Direction { FORWARD, BACKWARD };

// Konstrukcja z początkowymi wzmocnieniami i limitem całki anti-windup
PIDController pid(float kp, float ki, float kd, float mi);
PIDController pid;  // konstruktor domyślny - wszystkie wzmocnienia 0, limity niezainicjalizowane

// Zmiana i odczyt parametrów regulatora
pid.setKp(kp);  pid.getKp();
pid.setKi(ki);  pid.getKi();
pid.setKd(kd);  pid.getKd();
pid.setTf(tf);  pid.getTf();   // stała czasowa filtru dolnoprzepustowego członu różniczkującego
pid.setMaxIntegral(mi);        // anti-windup: maksymalna wartość |integral|

// Ograniczenie wyjścia
pid.setOutputLimits(float min, float max);

// Krok czasowy - wywołaj to, gdy zmienia się dt (timeDivider = 1/dt w ms)
pid.updatePIDtime(float timeDivider);

// Wykonaj jedną iterację PID; error = wartość zadana - pomiar
float out = pid.updatePIDcontroller(float error);

// Kierunek
pid.setDirection(FORWARD);   // lub BACKWARD

// Analiza
bool stable = pid.isErrorStable(error, tolerance, stabilityThreshold);
bool osc    = pid.isOscillating(currentError, windowSize);  // domyślny windowSize = 20

// Reset całki, różniczki, historii
pid.reset();

// Alias kompatybilności wstecznej dla ograniczania (w nowym kodzie użyj hal_constrain())
template<typename T> T pid_clamp(T v, T lo, T hi);
```

**Thread safety:** `PIDController` nie synchronizuje dostępu. Każda instancja
powinna być używana tylko z jednego rdzenia lub wątku.

**Przykład: regulacja temperatury (styl klasy C++)**

```cpp
#include <utils/pidController.h>

class ThermostatController {
    PIDController pid_ctrl;
    float target_temp;
    float current_temp;

public:
    ThermostatController() : target_temp(25.0f) {
        // Strojenie dla temperatury: powolne zmiany (dominacja członu P)
        pid_ctrl.setKp(2.0f);
        pid_ctrl.setKi(0.1f);
        pid_ctrl.setKd(0.5f);
        pid_ctrl.setMaxIntegral(50.0f);
        pid_ctrl.setOutputLimits(-100.0f, 100.0f);  // -100 do +100 %
        pid_ctrl.updatePIDtime(1.0f / 30.0f);  // pętla sterowania 30 ms
    }

    float compute(float measured_temp) {
        current_temp = measured_temp;
        float error = target_temp - current_temp;
        return pid_ctrl.updatePIDcontroller(error);
    }

    void set_target(float temp) { target_temp = temp; }
    float get_current(void) const { return current_temp; }
};

ThermostatController thermo;

extern "C" void app_start(void) {
    thermo.set_target(22.5f);
}

extern "C" void app_task0(void) {
    float adc_reading = (float)hal_adc_read(TEMP_SENSOR_PIN);
    float temp_c = (adc_reading * 3.3f / 4096.0f - 0.5f) * 100.0f;

    float heater_pwm = thermo.compute(temp_c);
    hal_pwm_write(HEATER_PIN, (uint32_t)(127 + heater_pwm * 127 / 100));

    hal_delay_ms(30);
}
```

---

## multicoreWatchdog

```c
#include <utils/multicoreWatchdog.h>

// Inicjalizuje watchdog sprzętowy (timeout = time ms; sprawdzanie aktywności co time/10 ms).
// Zwraca true, jeśli system został zresetowany przez watchdog (false = czysty rozruch).
// Callback 'function' jest wywoływany raz po restarcie przez watchdog z WATCHDOG_VALUES_AMOUNT
// liczbami diagnostycznymi (flagi aktywności rdzeni NOINIT). Może być NULL.
bool setupWatchdog(void(*function)(int *values, int size), unsigned int time);

// Wywołaj z głównej pętli rdzenia 0, aby zasygnalizować aktywność i odpytać wewnętrzny timer
void updateWatchdogCore0(void);

// Wywołaj z głównej pętli rdzenia 1, aby zasygnalizować aktywność i odpytać wewnętrzny timer
void updateWatchdogCore1(void);

// Wywołaj raz na końcu funkcji setup każdego rdzenia
void setStartedCore0(void);
void setStartedCore1(void);

// Zwraca true, gdy zarówno setStartedCore0(), jak i setStartedCore1() zostały wywołane
bool isEnvironmentStarted(void);

// Zaplanuj natychmiastowy reset systemu
void triggerSystemReset(void);

// Nakarm watchdog sprzętowy (wywołuje hal_watchdog_feed; można użyć przed setupWatchdog)
void watchdog_feed(void);
```

- **Implementacja:** `hal_watchdog_enable` / `hal_watchdog_feed` /
  `hal_watchdog_caused_reboot`.

> **Uwaga:** Wewnętrzny timer korzysta z `SmartTimers` i jest chroniony mutexem
> HAL, aby nie wywołać funkcji zwrotnej dwukrotnie po równoczesnym wywołaniu
> `updateWatchdogCore0/1`.

**Przykład: watchdog dwurdzeniowy**

```c
#include <utils/multicoreWatchdog.h>

void on_watchdog_reboot(int *values, int size) {
    hal_derr("Watchdog triggered! Core flags: %d %d", values[0], values[1]);
}

void app_start(void) {
    hal_debug_init(115200);

    bool was_watchdog = setupWatchdog(on_watchdog_reboot, 10000);  // 10 sek
    if (was_watchdog) {
        hal_derr("REBOOT: Previous boot triggered watchdog!");
    }

    setStartedCore0();
}

void app_task0(void) {
    // Wykonaj zadania rdzenia 0
    do_core0_tasks();

    // Zasygnalizuj aktywność watchdogowi
    updateWatchdogCore0();

    if (isEnvironmentStarted()) {
        hal_deb("Both cores running");
    }
}

char pad0[128];  // zapobiega umieszczeniu rdzenia 1 w tej samej ramce stosu

void app_task1(void) {
    // Konfiguracja rdzenia 1
    setStartedCore1();

    while (true) {
        // Wykonaj zadania rdzenia 1
        do_core1_tasks();

        // Zasygnalizuj aktywność z rdzenia 1
        updateWatchdogCore1();
    }
}
```

---

## draw7Segment - renderowanie wyświetlacza w stylu 7-segmentowym

```c
#include <utils/draw7Segment.h>

// Narysuj 7-segmentowy ciąg znaków w (x, y).
// str          - zakończony zerem ciąg znaków do wyrenderowania.
// digitWidth   - szerokość pojedynczej komórki cyfry w pikselach.
// digitHeight  - wysokość pojedynczej komórki cyfry w pikselach.
// thickness    - grubość segmentu w pikselach.
// color        - kolor RGB565.
void draw7SegString(const char* str, int x, int y, int digitWidth, int digitHeight, float thickness, uint16_t color);

// Oblicz szerokość w pikselach ciągu 7-segmentowego bez jego rysowania.
// Zwraca całkowitą szerokość w pikselach.
int get7SegStringWidth(const char* str, int digitWidth, float thickness);
```

**Obsługiwane znaki:** `0`-`9`, szesnastkowe `A`-`F`, spacja, `+`, `-`, `.`,
`:`, `%`, `^`.

Znaki mają proporcjonalne szerokości: `1` i spacja są węższe, `^` nieco szerszy.

**Zależności:** wyłącznie `hal_display.h`. Interfejs nie używa typów
specyficznych dla platformy; wszystkie parametry tekstowe to `const char*`.

**Thread safety:** Funkcje można bezpiecznie wywoływać współbieżnie, jeśli
pozwala na to `hal_display` (dotyczy backendów z rodziny RP). Rysowanie odbywa
się przez funkcje `hal_display_*`, których stan jest chroniony mutexem.

**Przykład: wyświetlacz zegara cyfrowego**

```c
#include <utils/draw7Segment.h>
#include <hal/display/hal_display.h>

void app_start(void) {
    // Zakładamy, że wyświetlacz jest już zainicjalizowany przez hal_display_init(...)
    hal_display_fill_screen(HAL_COLOR_BLACK);
}

void draw_time_display(int hours, int minutes, int seconds) {
    char time_str[16];
    snprintf(time_str, sizeof(time_str), "%02d:%02d:%02d", hours, minutes, seconds);

    // Oblicz szerokość ciągu znaków
    int width = get7SegStringWidth(time_str, 32, 3.0f);  // cyfry 32-pikselowe
    int x = (320 - width) / 2;  // wyśrodkuj poziomo

    // Narysuj wyświetlacz 7-segmentowy w kolorze czerwonym
    draw7SegString(time_str, x, 50, 32, 48, 3.0f, HAL_COLOR_RED);
}

void app_task0(void) {
    uint32_t ms = hal_millis();
    uint32_t total_secs = ms / 1000;

    int hours = (total_secs / 3600) % 24;
    int minutes = (total_secs / 60) % 60;
    int seconds = total_secs % 60;

    draw_time_display(hours, minutes, seconds);

    hal_delay_ms(100);
}
```

**Przykład: licznik na diodzie LED statusu**

```c
#include <utils/draw7Segment.h>
#include <hal/gpio/hal_gpio.h>

void show_counter_7seg(int value) {
    char counter_str[16];
    snprintf(counter_str, sizeof(counter_str), "%d", value);

    // Rysuj na wyświetlaczu (2x rozmiar, biały, na pozycji 10,100)
    draw7SegString(counter_str, 10, 100, 24, 36, 2.0f, HAL_COLOR_WHITE);
}

void app_task0(void) {
    static int counter = 0;

    if (hal_gpio_read(BUTTON_PIN)) {  // wciśnięty przycisk
        counter++;
        if (counter > 9999) counter = 0;
        show_counter_7seg(counter);
        hal_delay_ms(500);
    }
}
```

---


---

*Powrót do [Dokumentacji API JaszczurHAL](../../pl/JaszczurHAL_API.md)*
