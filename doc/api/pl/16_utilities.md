# Narzędzia

*Dostępne również [po angielsku](../en/16_utilities.md).*

> **Część [Dokumentacji API JaszczurHAL](../../pl/JaszczurHAL_API.md)**

Obejmuje: `hal_soft_timer`, `hal_pid_controller`, `tools.h/cpp`, `SmartTimers`, `pidController`, `multicoreWatchdog`, `draw7Segment`.

## `hal_soft_timer` - opakowanie C nad `SmartTimers`

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

**Przeznaczenie:** pozwala modułom w stylu C korzystać z funkcjonalności timera bez bezpośredniego powiązania z klasą `SmartTimers`.
**Implementacja:** wewnętrznie deleguje do `SmartTimers` (ta sama semantyka runtime).
**Thread safety:** Thread-safe i wielordzeniowo (dziedziczy ochronę mutexem per-instancja z `SmartTimers`).
**Pomocnicze funkcje tablicowe:** `hal_soft_timer_setup_table(...)` tworzy/konfiguruje timery na podstawie tablicy deskryptorów i opcjonalnie wywołuje `idle_cb` oraz opóźnienie między wpisami. `hal_soft_timer_tick_table(...)` odpytuje (tick) wszystkie wpisy z tej samej tablicy.
**Reguły walidacji:** funkcje pomocnicze do tablic walidują `table != NULL` oraz `count > 0`. Dla nieprawidłowych danych wejściowych logują przez `hal_derr(...)` i zwracają `false`.

**Przykład: cykliczny callback z opakowaniem C**
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

## `hal_pid_controller` - opakowanie C nad `pidController`

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

**Przeznaczenie:** udostępnia sterowanie PID poprzez funkcje C i nieprzezroczyste uchwyty (handles), umożliwiając stopniową migrację z użycia opartego na klasach.
**Implementacja:** wewnętrznie deleguje do `PIDController` (ta sama semantyka runtime).
**Thread safety:** Not thread-safe. Używaj jednej instancji kontrolera na jedną pętlę sterowania albo serializuj dostęp samodzielnie.

**Anti-windup:** dwa uzupełniające się mechanizmy:
1. **Twarde ograniczenie całki (hard clamp)** - `setMaxIntegral()` / `hal_pid_controller_set_max_integral()` ogranicza `|integral|`.
2. **Anti-windup przez clamping** - akumulacja całki jest pomijana, gdy wyjście
   jest nasycone w kierunku uchybu (tj. wyjście ≥ max i uchyb > 0,
   lub wyjście ≤ min i uchyb < 0). Zapobiega to nawijaniu się całki (windup)
   przy granicach wyjścia bez konieczności ręcznego strojenia limitu całki.

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

## Narzędzia wyższego poziomu (tools.h / tools.cpp)

Lokalizacja fizyczna: podstawowe funkcje pomocnicze narzędziowe znajdują się w `src/utils/*`;
wspólne narzędzia frameworku, takie jak SmartTimers, znajdują się w swojej domenie pod `src/hal/timers/*`.

Zalecane opcje dołączania:
- `#include <tools.h>` (nagłówek agregujący w `src/`)
- `#include <tools_c.h>` (deklaracje narzędziowe kompatybilne z C, z `src/`)
- bezpośrednie dołączenie ze ścieżki komponentu, na przykład
  `#include <hal/timers/smart_timers/SmartTimers.h>`

Narzędzia wewnętrznie zależą od HAL.

### Funkcje pomocnicze do manipulacji bitami (`hal_bits`)

Aliasy bitowe są definiowane z ochroną `#ifndef`, dzięki czemu aplikacja może
dostarczyć własne, równoważne definicje przed dołączeniem tego nagłówka.

```c
#include <hal/core/hal_bits.h>

#define is_set(x, mask)      // true, gdy dowolny bit z maski jest ustawiony w x
#define set_bit(var, mask)   // OR maski w var
#define clr_bit(var, mask)   // AND ~maski w var

// Warianty operujące na indeksie bitu (utrwalone nazwy publiczne)
#define bitSet(var, bit)     // ustaw bit o numerze 'bit' w var
#define bitClear(var, bit)   // wyczyść bit o numerze 'bit' w var
#define bitRead(var, bit)    // odczytaj bit o numerze 'bit' z var (zwraca 0 lub 1)

// Pomocnicze funkcje dla rejestrów volatile (forma wskaźnikowa makra)
#define set_bit_v(reg, mask) // ustaw maskę w *reg
#define clr_bit_v(reg, mask) // wyczyść maskę w *reg

```

Unikaj przekazywania wyrażeń z efektami ubocznymi (na przykład `i++` lub wywołań funkcji
modyfikujących stan), ponieważ argumenty makra mogą zostać obliczone więcej niż raz.

### Funkcje

```c
void  debugInit(void);                  // opcjonalne - hal_deb() inicjalizuje się leniwie automatycznie
void  setDebugPrefixWithColon(const char *moduleName); // buduje "<module>:" w granicach HAL_DEBUG_PREFIX_SIZE i przekazuje do hal_deb_set_prefix
void  floatToDec(float val, int *hi, int *lo);
float decToFloat(int hi, int lo);
float adcToVolt(int adc, float r1, float r2);
float ntcToTemp(int tpin, int thermistor, int r);
float steinhart(float val, float thermistor, int r, bool characteristic);
int   percentToGivenVal(float percent, int maxWidth);
int   percentFrom(int givenVal, int maxVal);
float getAverageValueFrom(int tpin);  // zawiera odczyt "na pusto" (poprawka na przesłuch multipleksera ADC RP2040)
float filter(float alpha, float input, float previous_output);
float filterValue(float currentValue, float newValue, float alpha);
int   adcCompe(int x);
float getAverageForTable(int *idx, int *overall, float val, float *table);
int   getAverageFrom(int *table, int size);
int   getMinimumFrom(int *table, int size);
int   getHalfwayBetweenMinMax(int *array, int n);
float mapfloat(float x, float in_min, float in_max, float out_min, float out_max);
static inline uint32_t float_to_u32(float f);   // bitcast float -> uint32_t (memcpy)
static inline float    u32_to_float(uint32_t u); // bitcast uint32_t -> float (memcpy)
unsigned long getSeconds(void);
bool  isDaylightSavingTime(int year, int month, int day);
void  adjustTime(int *year, int *month, int *day, int *hour, int *minute);
bool  is_time_in_range(long now, long start, long end);
void  extract_time(long timeInMinutes, int *hours, int *minutes);
unsigned short byteArrayToWord(unsigned char *bytes);
void     wordToByteArray(unsigned short word, unsigned char *bytes);
uint8_t  MSB(unsigned short value);
uint8_t  LSB(unsigned short value);
int      MsbLsbToInt(uint8_t msb, uint8_t lsb);
float rroundf(float val);
float roundfWithPrecisionTo(float value, int precision);
char *printBinaryAndSize(int number, char *buf, size_t bufSize);  // zapisuje ciąg binarny do buf
bool  concatStrings(char *dest, size_t destSize, const char *src1, const char *src2); // false przy argumentach NULL lub zbyt małym dest
bool  isValidString(const char *s, int maxBufSize);
char  hexToChar(char high, char low);
void  urlDecode(const char *src, char *dst);
void  removeSpaces(char *str);
bool  startsWith(const char *str, const char *prefix);
void  remove_non_ascii(const char *input, char *output, size_t outputSize);  // zastępuje UTF-8 znakami ASCII
void  hal_pack_field_pad(uint8_t *buf, const char *str, int width, uint8_t pad);
void  hal_pack_field(uint8_t *buf, const char *str, int width); // dopełnienie zerami
unsigned short rgbToRgb565(unsigned char r, unsigned char g, unsigned char b);
bool rgb888ToRgb565(const unsigned char *rgb, unsigned short *rgb565, size_t pixelCount);
bool rgba8888ToRgb565(const unsigned char *rgba, unsigned short *rgb565, size_t pixelCount); // ignoruje kanał alfa
bool pngBase64DecodedSize(const char *base64, size_t base64Len,
                          size_t *pngSize); // wymaga HAL_ENABLE_PNG_AS_BASE64
bool pngBase64Decode32(unsigned char **rgba, unsigned *width, unsigned *height,
                       const char *base64, size_t base64Len,
                       uint8_t *pngWork, size_t pngWorkSize,
                       unsigned *pngError); // wymaga HAL_ENABLE_PNG_AS_BASE64
bool pngBase64DecodeRgb565(const char *base64, size_t base64Len,
                           uint8_t *pngWork, size_t pngWorkSize,
                           unsigned short *rgb565, size_t rgb565Pixels,
                           unsigned *width, unsigned *height,
                           unsigned *pngError); // wymaga HAL_ENABLE_PNG_AS_BASE64
const char *macToString(uint8_t mac[6], char *buf, size_t bufSize);
const char *encToString(uint8_t enc);       // etykieta szyfrowania WiFi HAL
bool  scanNetworks(const char *networkToFind);  // wymaga HAL_ENABLE_WIFI
int   getRandomEverySomeMillis(uint32_t time, int maxValue);
float getRandomFloatEverySomeMillis(uint32_t time, float maxValue);
```

Powyższe cztery przestarzałe funkcje pomocnicze do obsługi czasu są wyłącznie
wrapperami kompatybilności. Ich implementacje delegują bezpośrednio do
`hal_time_is_daylight_saving_time()`, `hal_time_adjust_cet_cest()`,
`hal_time_is_in_range()` oraz `hal_time_extract_minutes()` odpowiednio. Nowy
kod powinien dołączać `<hal/time/hal_time.h>` i wywoływać API HAL bezpośrednio; zobacz
[Funkcje pomocnicze kalendarza oraz opcjonalny czas systemowy/NTP](15_connectivity.md)
w kwestii semantyki granic, walidacji i przewijania (rollover).

---

## `hal_crc` - sumy kontrolne CRC

```c
#include <hal/security/hal_crc.h>
```

Niezależne od backendu, beztablicowe sumy kontrolne CRC dla integralności danych (**nie**
kryptografia - do hashowania/HMAC zobacz [`hal_crypto`](07_crypto.md)). Opcjonalne, włączane przez
`HAL_ENABLE_CRC`; włączenie `HAL_ENABLE_ONEWIRE` (lub `HAL_ENABLE_DS18B20`) również
włącza tę funkcję, ponieważ ścieżka 1-Wire potrzebuje CRC do walidacji ROM/scratchpad. CRC
to rodzina parametryzowana szerokością, wielomianem, wartością początkową, odbiciem bitów i końcowym XOR-em,
dlatego każda procedura jest nazwana po konkretnym wariancie, który oblicza - celowo
nie ma niekwalifikowanego `hal_crc16`, który po cichu wiązałby się z jednym
wielomianem.

### Funkcje

```c
uint8_t  hal_crc8_maxim(const uint8_t *data, size_t len);   // CRC-8/MAXIM-DOW (1-Wire)
uint16_t hal_crc16_maxim(const uint8_t *data, size_t len, uint16_t crc); // Maxim CRC-16 1-Wire
bool     hal_crc16_maxim_check(const uint8_t *data, size_t len,
                               const uint8_t inverted_crc[2], uint16_t crc);
uint16_t hal_crc16_ccitt(const uint8_t *data, size_t len, uint16_t crc); // CRC-16/CCITT-FALSE, wielomian 0x1021
uint32_t hal_crc32(const uint8_t *data, size_t len);        // CRC-32/ISO-HDLC (zlib/Ethernet)
```

- Ziarno (seed) `crc` pozwala `hal_crc16_*` kontynuować obliczenia nad podzielonym buforem; przekaż `0`
  dla wariantu Maxim oraz `HAL_CRC16_CCITT_INIT` (`0xFFFF`) dla CCITT, aby zacząć
  od nowa. `hal_crc8_maxim` i `hal_crc32` są jednorazowe (one-shot).
- `hal_crc16_maxim_check` odtwarza konwencję magistrali 1-Wire, w której urządzenia
  transmitują CRC-16 zanegowane bitowo (dawne `hal_onewire_check_crc16`).
- Katalogowe wartości kontrolne dla `"123456789"`: CRC-8/MAXIM `0xA1`,
  CRC-16/CCITT-FALSE `0x29B1`, CRC-32 `0xCBF43926` - potwierdzone przez
  `tests/test_hal_crc.cpp`.

Dodanie nowego wariantu (np. `hal_crc16_ccitt`, `hal_crc32c`) jest dodatkiem samodzielnym w tym miejscu; nigdy nie
narusza istniejących wpisów.

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

**Uwaga:** Makra `SECOND`, `SECS()`, `MINS()`, `HOURS()` są zdefiniowane w `hal/system/hal_system.h`
(dołączanym automatycznie przez `hal/timers/smart_timers/SmartTimers.h`).
**Thread safety:** Z założenia thread-safe i bezpieczne wielordzeniowo. Każda
instancja od razu (eagerly) tworzy własny `hal_mutex_t`, który serializuje
wszystkie wywołania metod. Callbacki przekazane do `begin()` są wywoływane
poza muteksem, aby zapobiec deadlockowi.

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

// Ustawiacze/pobieracze strojenia
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

**Thread safety:** Not thread-safe. Każda instancja `PIDController` powinna być używana z jednego rdzenia/wątku.

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

// Nakarm watchdog sprzętowy (opakowuje hal_watchdog_feed; bezpieczne do wywołania przed setupWatchdog)
void watchdog_feed(void);
```

**impl:** `hal_watchdog_enable` / `hal_watchdog_feed` / `hal_watchdog_caused_reboot`.
**Uwaga:** Wewnętrzny timer korzysta z `SmartTimers` i jest chroniony mutexem HAL, aby zapobiec
podwójnemu wywołaniu przy równoczesnych wywołaniach `updateWatchdogCore0/1`.

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

**Obsługiwane znaki:** `0`-`9`, hex `A`-`F`, spacja, `+`, `-`, `.`, `:`, `%`, `^`.

Znaki mają proporcjonalne szerokości: `1` i spacja są węższe, `^` nieco szerszy.

**Zależności:** wyłącznie `hal_display.h`. Interfejs nie używa typów specyficznych dla platformy; wszystkie parametry tekstowe to `const char*`.

**Thread safety:** Thread-safe, gdy `hal_display` jest thread-safe (backend z rodziny RP). Deleguje całe rysowanie do funkcji `hal_display_*`, które są chronione mutexem.

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
