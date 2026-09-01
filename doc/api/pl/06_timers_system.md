# Timery, system, bity, matematyka

*Dostępne również [po angielsku](../en/06_timers_system.md).*

> **Część [Dokumentacji API JaszczurHAL](../../pl/JaszczurHAL_API.md)**

## `hal_status` - Współdzielone kody statusu

```c
#include <hal/core/hal_status.h>

typedef enum {
    HAL_NONE = 0,
    HAL_OK = 1,
    HAL_EINVAL = -1,
    HAL_EBUSY = -2,
    HAL_ETIMEOUT = -3,
    HAL_EIO = -4,
    HAL_EUNSUPPORTED = -5,
    HAL_ENOENT = -6,
    HAL_EAGAIN = -7,
    HAL_EOVERFLOW = -8,
    HAL_ENOMEM = -9,
    HAL_IGNORED = -10,
    HAL_EEXIST = -11,
    HAL_EPERM = -12,
    HAL_EINTERNAL = -13,
    HAL_ECANCELED = -14,
    HAL_EPROTO = -15,
    HAL_EAUTH = -16,
    HAL_EBUS = -17,
    HAL_EHW = -18,
    HAL_ECONFIG = -19,
    HAL_ESTATE = -20,
    HAL_EUNINIT = -21,
    HAL_EDEPRECATED = -22,
    HAL_EUNKNOWN = -23,
} hal_status_t;

static inline const char *hal_status_to_string(hal_status_t status);
```

Jest to wspólny zestaw statusów dla nowych publicznych API. Po migracji
dotychczasowe funkcje zwracające wartość, uchwyt lub `bool` pozostają
adapterami zgodności. Starsze operacje `void`, które mogą się nie powieść,
mogą zacząć bezpośrednio zwracać `hal_status_t`, ponieważ istniejący kod nadal
może ignorować wynik.

Używaj `HAL_OK` do oznaczania powodzenia, `status < 0` do wykrywania dowolnego
błędu, a konkretnych kodów do diagnostyki pomiędzy modułami i backendami.
`hal_status_to_string()` zwraca stabilne nazwy symboliczne, takie jak
`"HAL_EIO"` i `"HAL_STATUS_UNKNOWN"` dla nierozpoznanych wartości liczbowych.
Prefiks `HAL_` unika kolizji z nazwami POSIX `errno` używanymi przez warstwę
zgodności z gniazdami BSD.

---

## `hal_timer` - Alarmy sprzętowe

```c
#include <hal/timers/hal_timer.h>

typedef int32_t hal_alarm_id_t;
#define HAL_ALARM_INVALID (-1)
typedef int64_t (*hal_alarm_callback_t)(hal_alarm_id_t id, void *user_data);
typedef enum { ... } hal_timer_result_t;
typedef enum { HAL_TIMER_STATE_STOPPED, HAL_TIMER_STATE_RUNNING, HAL_TIMER_STATE_PAUSED } hal_timer_state_t;

// Layer 1: low-level alarms (one-shot + cancel)
hal_alarm_id_t hal_timer_add_alarm_us(uint32_t delay_us,
                                      hal_alarm_callback_t callback,
                                      void *user_data,
                                      bool fire_if_past);
hal_alarm_id_t hal_timer_add_alarm_us_ex(uint32_t delay_us,
                                         hal_alarm_callback_t callback,
                                         void *user_data,
                                         bool fire_if_past,
                                         hal_timer_result_t *out_result);
bool hal_timer_cancel_alarm(hal_alarm_id_t alarm_id);

// Layer 1 advanced: alarm pools (scale logical alarms beyond default pool)
typedef struct hal_timer_pool_impl_s *hal_timer_pool_t;
#define HAL_TIMER_POOL_DEFAULT ((hal_timer_pool_t)0)
hal_timer_pool_t hal_timer_pool_create(uint8_t hardware_alarm_num, uint16_t max_timers);
hal_timer_pool_t hal_timer_pool_create_auto(uint16_t max_timers);
void hal_timer_pool_destroy(hal_timer_pool_t pool);
hal_alarm_id_t hal_timer_pool_add_alarm_us(hal_timer_pool_t pool,
                                           uint32_t delay_us,
                                           hal_alarm_callback_t callback,
                                           void *user_data,
                                           bool fire_if_past);
hal_alarm_id_t hal_timer_pool_add_alarm_us_ex(hal_timer_pool_t pool,
                                              uint32_t delay_us,
                                              hal_alarm_callback_t callback,
                                              void *user_data,
                                              bool fire_if_past,
                                              hal_timer_result_t *out_result);
bool hal_timer_pool_cancel_alarm(hal_timer_pool_t pool, hal_alarm_id_t alarm_id);

// Layer 2: managed timers (one-shot or periodic)
typedef struct hal_timer_impl_s *hal_timer_t;
typedef void (*hal_timer_callback_t)(hal_timer_t timer, void *user_data);
hal_timer_result_t hal_timer_create(hal_timer_pool_t pool, uint32_t period_us,
                                    bool periodic, hal_timer_callback_t callback,
                                    void *user_data, hal_timer_t *out_timer);
hal_timer_result_t hal_timer_destroy(hal_timer_t timer);
hal_timer_result_t hal_timer_start(hal_timer_t timer);
hal_timer_result_t hal_timer_stop(hal_timer_t timer);
hal_timer_result_t hal_timer_pause(hal_timer_t timer);
hal_timer_result_t hal_timer_resume(hal_timer_t timer);
hal_timer_result_t hal_timer_set_period_us(hal_timer_t timer, uint32_t period_us,
                                           bool restart_if_running);
hal_timer_result_t hal_timer_get_period_us(hal_timer_t timer, uint32_t *out_period_us);
hal_timer_state_t  hal_timer_get_state(hal_timer_t timer);
hal_timer_result_t hal_timer_get_remaining_us(hal_timer_t timer, int64_t *out_remaining_us);
```

Przed wywołaniem `hal_timer_pool_destroy()` zatrzymaj i zniszcz każdy
zarządzany timer oraz anuluj każdy alarm niskiego poziomu powiązany z tą
pulą, a następnie upewnij się, że wszystkie jej callbacki zakończyły
działanie. Sama zewnętrzna synchronizacja z innymi wywołującymi nie
wystarcza; niszczenie puli z poziomu callbacku alarmu/ISR nie jest
obsługiwane.

- **Podział na warstwy:** alarmy niskiego poziomu służą do prostego planowania
  w ISR. Timerów zarządzanych używaj, gdy potrzebujesz operacji
  start/stop/pause/resume, odczytu stanu albo pracy okresowej.
- **Obsługa błędów:** funkcje `_ex` zwracają szczegółowy wynik typu
  `hal_timer_result_t` (`INVALID_ARG`, `TIME_PASSED`, `POOL_FULL`,
  `NO_RESOURCE` itd.). Starsze warianty bez `_ex` zachowują zgodność przez
  `HAL_ALARM_INVALID`.
- **impl/rp2040:** Pule alarmów Pico SDK (`pico/time.h`) oraz planowanie
  callbacków (`alarm_pool_add_alarm_in_us`, API anulowania). Wyniki
  `add_alarm_in_us()` mniejsze lub równe `0` są uznawane za błędne i zamieniane
  na jawny kod wyniku w wariancie `_ex`. Rekord operacji o stałym rozmiarze
  przechowuje stan alarmu między przydzieleniem go przez SDK a udostępnieniem
  drugiemu rdzeniowi. Przed ponownym użyciem identyfikatora alarmu Pico usuwane
  są nieaktualne znaczniki anulowania. Sekwencja przypisana do danego slotu
  powtarza się po 32767 alokacjach.
- **impl/stm32g474:** TIM6 działa jako planista alarmów jednorazowych
  1 MHz, taktowany z jawnie określonego zegara timera APB1 o częstotliwości
  170 MHz. Długie opóźnienia są dzielone na fragmenty mieszczące się w
  16-bitowych okresach TIM6, wartości zwracane z callbacku większe od zera
  ponownie planują ten sam alarm, a pule programowe zapewniają takie samo
  publiczne zachowanie pul/anulowania jak RP2040.
- **impl/esp32:** jeden 1 MHz GPTimer z ESP-IDF stanowi zaplecze domyślnej
  logicznej puli alarmów, która domyślnie pomieści do 16 jednoczesnych alarmów.
  Dodatnie wartości zwracane z callbacku ponownie planują ten sam alarm.
  `hal_timer_pool_create()` wybiera jeden z czterech slotów selektora targetu,
  natomiast `hal_timer_pool_create_auto()` zajmuje pierwszy dostępny slot;
  każda pomyślnie utworzona pula posiada osobny GPTimer 1 MHz oraz tablicę
  logicznych alarmów o rozmiarze podanym przez wywołującego. Tworzenie zwraca
  `NULL`, gdy selektor, pamięć lub zasób GPTimer są niedostępne. Niszczenie
  najpierw rozbraja, zatrzymuje, wyłącza i usuwa GPTimer. Jeśli ESP-IDF odmówi
  zwolnienia zasobu, kontekst i selektor nie są zwalniane, ponieważ ISR może
  nadal odwoływać się do stanu callbacku.
  Zarządzane timery działają zarówno na puli domyślnej, jak i dedykowanej,
  poprzez wspólną warstwę zarządzanych timerów.

**Wielowątkowość:** Backendy z rodziny RP oraz ESP32-S3 mogą bezpiecznie
planować i anulować alarmy oraz zmieniać stan timerów zarządzanych z wielu
wątków i rdzeni. STM32G474 chroni mechanizm przydzielania slotów alarmów
krótkimi sekcjami krytycznymi PRIMASK. Callbacki GPTimer ESP32-S3 oraz TIM6
STM32G474 wykonują się w kontekście ISR; utrzymuj callbacki krótkie,
nieblokujące i bezpieczne dla ISR. Mock jest deterministyczny dla testów, ale
nie jest zsynchronizowany dla współbieżnych wątków hosta.

---

### Przykłady

**Przykład: alarm jednorazowy (niski poziom)**
```c
#include <hal/timers/hal_timer.h>
#include <hal/system/hal_system.h>

static bool alarm_fired = false;

static int64_t on_timeout(hal_alarm_id_t id, void *user_data) {
    alarm_fired = true;
    hal_deb("Alarm %d fired after timeout", id);
    return 0;  // do not reschedule
}

void example_alarm(void) {
    // Schedule a one-shot alarm for 500 ms from now
    hal_alarm_id_t alarm = hal_timer_add_alarm_us(500000,
                                                    on_timeout,
                                                    NULL,
                                                    false);

    if (alarm != HAL_ALARM_INVALID) {
        hal_deb("Alarm scheduled with ID: %d", alarm);
    }

    // Wait for it to fire
    while (!alarm_fired) {
        hal_delay_ms(10);
    }

    hal_deb("Alarm execution complete");
}
```

**Przykład: zarządzany timer okresowy**
```c
#include <hal/timers/hal_timer.h>

static uint32_t tick_count = 0;

static void periodic_callback(hal_timer_t timer, void *user_data) {
    tick_count++;
    if (tick_count % 10 == 0) {
        hal_deb("Timer fired %lu times", tick_count);
    }
}

void example_managed_timer(void) {
    hal_timer_t my_timer;

    // Create a periodic timer that fires every 1 second
    hal_timer_result_t result = hal_timer_create(
        HAL_TIMER_POOL_DEFAULT,
        1000000,           // 1,000,000 microseconds = 1 second
        true,              // periodic
        periodic_callback,
        NULL,
        &my_timer
    );

    if (result == HAL_TIMER_OK) {
        hal_deb("Timer created successfully");

        // Start the timer
        hal_timer_start(my_timer);

        // Let it run for 5 seconds
        hal_delay_ms(5000);

        // Pause it temporarily
        hal_timer_pause(my_timer);
        hal_deb("Timer paused, ticks: %lu", tick_count);

        // Resume after 2 seconds
        hal_delay_ms(2000);
        hal_timer_resume(my_timer);

        // Stop and destroy
        hal_timer_stop(my_timer);
        hal_timer_destroy(my_timer);

        hal_deb("Final tick count: %lu", tick_count);
    } else {
        hal_derr("Failed to create timer: %d", result);
    }
}
```

**Przykład: pula alarmów dla wielu timerów**
```c
#include <hal/timers/hal_timer.h>

static int64_t pool_callback(hal_alarm_id_t id, void *user_data) {
    uint32_t timer_num = (uint32_t)(uintptr_t)user_data;
    hal_deb("Pool alarm %d (user data: %lu) fired", id, timer_num);
    return 0;
}

void example_alarm_pool(void) {
    // Create a pool supporting up to 10 timers on a dedicated hardware alarm
    hal_timer_pool_t pool = hal_timer_pool_create_auto(10);

    if (pool == NULL) {
        hal_derr("Failed to create timer pool");
        return;
    }

    // Schedule multiple alarms using the pool
    hal_alarm_id_t alarms[5];
    for (int i = 0; i < 5; i++) {
        alarms[i] = hal_timer_pool_add_alarm_us(
            pool,
            (i + 1) * 300000,  // 300ms, 600ms, 900ms, 1200ms, 1500ms
            pool_callback,
            (void *)(uintptr_t)i,
            false
        );

        if (alarms[i] != HAL_ALARM_INVALID) {
            hal_deb("Scheduled alarm %d (user %d) for %d ms", alarms[i], i, (i+1)*300);
        }
    }

    // Wait for all to fire
    hal_delay_ms(2000);

    // Cleanup
    hal_timer_pool_destroy(pool);
}
```

---

## `hal_system` - Czas, watchdog i informacje systemowe

```c
#include <hal/system/hal_system.h>

// Time-conversion macros (also included by SmartTimers.h)
#define SECOND      1000UL
#define SECS(t)     ((unsigned long)((t) * SECOND))
#define MINS(t)     (SECS(t) * 60UL)
#define HOURS(t)    (MINS(t) * 60UL)

uint32_t hal_millis(void);
typedef void (*hal_millis_interval_callback_t)(void *user_data);
bool hal_millis_interval_elapsed(uint32_t now_ms, uint32_t *last_ms,
                                 uint32_t interval_ms);
bool hal_millis_interval_elapsed_now(uint32_t *last_ms, uint32_t interval_ms);
bool hal_millis_interval_call(uint32_t now_ms, uint32_t *last_ms,
                              uint32_t interval_ms,
                              hal_millis_interval_callback_t callback,
                              void *user_data);
bool hal_millis_interval_call_now(uint32_t *last_ms, uint32_t interval_ms,
                                  hal_millis_interval_callback_t callback,
                                  void *user_data);
uint32_t hal_micros(void);
uint64_t hal_micros64(void);          // 64-bit timestamp, no overflow
void     hal_delay_ms(uint32_t ms);
void     hal_delay_us(uint32_t us);
void     hal_watchdog_feed(void);
hal_status_t hal_watchdog_enable(uint32_t ms, bool pause_on_debug);
bool     hal_watchdog_caused_reboot(void);
void     hal_idle(void);
bool     hal_in_isr(void);            // true when called from an exception/IRQ handler
uint32_t hal_get_free_heap(void);     // available heap in bytes
hal_status_t hal_read_chip_temp_ex(float *out_celsius);
float    hal_read_chip_temp(void);    // approximate on-die temperature in °C
hal_status_t hal_enter_bootloader(void); // does not return on supported hardware
hal_status_t hal_u32_to_bytes_be(uint32_t val, uint8_t *buf);

typedef struct {
    const char *target_name;
    const char *backend_name;
    const char *mcu;
    const char *mcu_subtype;
    const char *cpu_arch;
    const char *rtos_name;
    uint8_t cpu_cores;
    bool is_hardware;
    bool has_fpu;
    bool has_rtos;
    uint32_t cpu_clock_hz;
    uint32_t peripheral_clock_hz;
    uint32_t flash_total_bytes;
    uint32_t flash_usable_bytes;
    uint32_t flash_reserved_bytes;
    uint32_t ram_total_bytes;
    uint32_t ram_usable_bytes;
    uint32_t heap_total_bytes;
    uint32_t heap_free_bytes;
    uint32_t stack_total_bytes;
    uint32_t uid_bytes;
} hal_system_architecture_t;

hal_status_t hal_system_get_current_architecture(hal_system_architecture_t *out);

// Device unique identifier (RP2040 flash unique id).
#define HAL_DEVICE_UID_BYTES        8u
#define HAL_DEVICE_UID_HEX_BUF_SIZE 17u  // 16 hex chars + NUL

hal_status_t hal_get_device_uid(uint8_t uid[HAL_DEVICE_UID_BYTES]);
hal_status_t hal_get_device_uid_hex_ex(char *buf, size_t buflen);
bool hal_get_device_uid_hex(char *buf, size_t buflen);

// Crash / fault diagnostics (full reference in the "Crash / fault diagnostics"
// block below).
void               hal_fault_subsystem_init(void);
hal_reset_reason_t hal_get_reset_reason(void);
const char        *hal_reset_reason_str(hal_reset_reason_t reason);
hal_status_t        hal_get_last_fault_ex(hal_fault_info_t *out);
bool               hal_get_last_fault(hal_fault_info_t *out);
void               hal_clear_last_fault(void);
bool               hal_last_boot_was_brownout(void);
void               hal_alive_mark(void);
hal_status_t        hal_stack_guard_init_ex(void);
bool               hal_stack_guard_init(void);
void               hal_stack_guard_check(void);

// Type-independent math helpers (macros)
#define hal_constrain(v, lo, hi) ...
#define hal_map(x, in_min, in_max, out_min, out_max) ...

// NONULL helper macro: if pointer is null, jump to local `error:` label
#define NONULL(x) do { if ((x) == NULL) { goto error; } } while (0)
// COUNTOF helper macro: calculating the `C-array` size
#define COUNTOF(arr) (sizeof(arr) / sizeof((arr)[0]))

```

`hal_system_get_current_architecture()` kopiuje opis bieżącej konfiguracji
systemu do struktury wskazanej przez kod wywołujący. Tożsamość
targetu, backend, MCU i jego podtyp, opis CPU, liczba rdzeni, obecność FPU oraz
całkowity i dostępny rozmiar RAM pochodzą bezpośrednio z wygenerowanego
deskryptora targetu. Pojemność flash przeznaczonego na program pochodzi z
deskryptora wybranej płytki. Wartości runtime, takie jak częstotliwości zegarów
i ilość wolnego heapu, są pobierane z backendu. Pola tekstowe wskazują
statyczne napisy przechowywane w danych wygenerowanych podczas buildu lub w
backendzie. Pola liczbowe mają wartość `0`, jeśli dana informacja
nie dotyczy bieżącego targetu. API nie alokuje pamięci, dlatego kod wywołujący
nie może zwalniać zwróconych napisów.

Systemowe funkcje statusowe rozróżniają nieprawidłowy wskaźnik wyjściowy
(`HAL_EINVAL`), brak zapisanych danych o awarii (`HAL_ENOENT`) i usługę
niezaimplementowaną przez aktywny backend (`HAL_EUNSUPPORTED`). Starsze
funkcje `hal_read_chip_temp()`, `hal_get_last_fault()` i
`hal_stack_guard_init()` są adapterami zgodności wywołującymi odpowiednie
warianty `_ex`.

Funkcje interwału milisekundowego pozwalają wykonywać nieblokujące operacje
okresowe w firmware opartym na pętli, bez uruchamiania timera sprzętowego.
Sprawdzają warunek `now_ms - last_ms >= interval_ms`, który działa prawidłowo
także po zawinięciu licznika, i aktualizują `last_ms` tylko po upływie
interwału. Warianty `*_now` same pobierają `now_ms` przez `hal_millis()`.
`hal_millis_interval_call*()` po upływie interwału wywołuje callback, jeśli nie
jest równy `NULL`, i zwraca `true` dla bieżącego obiegu pętli.

- **impl/rp2040:** `hal_millis()` używa `to_ms_since_boot(get_absolute_time())`,
  a `hal_micros()` i `hal_micros64()` - `time_us_64()`. Obsługa SoC dla
  watchdoga, bezczynności, odczytu wolnego heapu, temperatury układu, resetu
  BOOTSEL, tożsamości urządzenia, wykrywania ISR i pozostałych usług systemowych
  znajduje się w
  `src/hal/impl/rp2040/drivers/rp2040/rp2040_system.{h,cpp}`. Przyczyna resetu
  jest dekodowana, a ARM HardFault przechwytywany w
  `rp2040_fault.{h,cpp}`. W buildach FreeRTOS funkcja opóźniająca oddaje
  sterowanie tylko w prawidłowym kontekście zadania. Przed uruchomieniem
  planisty, w ISR i na krytycznych ścieżkach HAL
  stosowane jest ograniczone aktywne oczekiwanie SDK. Opóźnienie
  mikrosekundowe zawsze używa `busy_wait_us()`. Informacje o konfiguracji
  systemu łączą wygenerowane dane targetu i płytki z obszarami flash
  zarezerwowanymi przez wybrany skrypt linkera oraz, przy aktywnym FreeRTOS,
  rozmiarem jego heapu. Kod zapisuje bit resetu watchdoga przed uruchomieniem
  aplikacji, więc późniejsze włączenie watchdoga nie usuwa informacji o
  przyczynie poprzedniego restartu.
- **impl/stm32g474:** Podczas uruchamiania PLL generuje SYSCLK 170 MHz z HSI16.
  Magistrale AHB, APB1 i APB2 pracują bez preskalera. W trybie bare metal
  64-bitowy licznik milisekund jest zwiększany przez SysTick. W buildach z
  FreeRTOS każdy tick przekazany przez kernel zwiększa ten licznik. Stan
  licznika jest zapisywany w dwóch buforach. W trybie
  bare metal odczyt czasu uwzględnia bieżącą część mikrosekundową SysTick oraz
  przepełnienie, które nastąpiło, ale nie zostało jeszcze obsłużone. Dzięki
  temu `hal_micros()` zachowuje zgodne z wcześniejszym API zawijanie po 32
  bitach, natomiast `hal_micros64()` pozostaje monotoniczny po przekroczeniu
  tej wartości. DWT służy jako fallback dla funkcji opóźniających. Kod
  obsługujący DWT oraz watchdog, bezczynność, temperatura
  układu, tożsamość urządzenia, wykrywanie ISR i pozostałe usługi systemowe są
  podzielone między
  `src/hal/impl/stm32g474/port/system_stm32g474.c` oraz
  `src/hal/impl/stm32g474/drivers/stm32g474/stm32g474_system.{h,cpp}`.
  W kontekście zadania FreeRTOS funkcja opóźniająca oddaje sterowanie
  planiście. Przed jego uruchomieniem, w ISR i w sekcjach krytycznych
  stosowane jest aktywne oczekiwanie oparte na DWT. Informacje o konfiguracji
  systemu łączą pojemności zapisane w deskryptorach targetu i płytki z
  zakresami heapu, stosu, EEPROM i LittleFS wynikającymi z wybranego runtime i
  skryptu linkera. Częstotliwość CPU i głównego zegara peryferiów wynosi 170
  MHz. Sprzętowy watchdog korzysta z IWDG taktowanego nominalnym zegarem LSI
  32 kHz. Wybiera najkrótszy preskaler mieszczący żądany czas i obsługuje
  timeouty od 1 do 32768 ms. `pause_on_debug` steruje bitem zamrażającym IWDG w
  DBGMCU. Przy określaniu, czy restart wywołał watchdog, używana jest flaga
  `RCC_CSR_IWDGRSTF` zapisana podczas uruchamiania systemu.
- **impl/esp32:** `esp_timer_get_time()` zwraca monotoniczny czas w
  mikrosekundach. `hal_delay_ms()` używa `vTaskDelay()` tylko w prawidłowym
  kontekście zadania i wykonuje aktywne oczekiwanie przed
  uruchomieniem planisty, w kontekście ISR lub wewnątrz sekcji krytycznej HAL.
  Usługi systemowe używają API ESP-IDF dla watchdoga zadań, sterty, drzewa
  zegarów, czujnika temperatury, przyczyny resetu, uruchomionej partycji oraz
  eFuse MAC. Informacje o konfiguracji systemu łączą wygenerowane dane o
  flashu, PSRAM i CPU z bieżącymi wartościami zegara, partycji i heapu.
  API watchdoga przyjmuje opcję `pause_on_debug`, ale runtime nie pozwala
  zastosować jej osobno do pojedynczego TWDT. Kontrola debuggera ESP32 pozostaje
  po stronie OpenOCD. `hal_enter_bootloader()` przełącza układ w tryb pobierania
  wbudowany w ROM i wykonuje restart. Zwraca `HAL_EUNSUPPORTED`, gdy
  polityka eFuse wyłącza tryby pobierania.
- **impl/.mock:** czas sterowany funkcjami pomocniczymi mocka;
  `hal_watchdog_caused_reboot`, `hal_get_free_heap`, temperatura układu oraz UID
  urządzenia można ustawiać przez funkcje pomocnicze testów.
  `hal_enter_bootloader()` ustawia
  obserwowalną flagę zamiast wykonywać restart. `hal_in_isr()` zwraca wartość
  ustawioną przez `hal_mock_set_in_isr(bool)`.

**Wielowątkowość:** API czasu i watchdoga w backendach z rodziny RP oraz ESP32-S3
można bezpiecznie wywoływać z obu rdzeni. Karmienie watchdoga na STM32G474 to
atomowe zapisy do rejestru; wywołujący muszą serializować rekonfigurację
watchdoga. W trybach FreeRTOS na RP, STM32G474 oraz ESP32-S3 funkcja
`hal_delay_ms()` korzysta z planisty tylko w prawidłowym kontekście i blokuje
wyłącznie wywołujące ją zadanie. Przed uruchomieniem planisty, w ISR i w
sekcjach krytycznych HAL stosuje aktywne oczekiwanie. `hal_delay_us()` blokuje
tylko wywołujący rdzeń. Stan mocka jest przeznaczony do testów jednowątkowych.

> **Uwaga:** `COUNTOF(arr)` działa wyłącznie z tablicami alokowanymi statycznie
> (nie ze wskaźnikami).

> **Uwaga:** `NONULL(x)` sprawdza wskaźnik w funkcjach, które w razie błędu
> przechodzą do wspólnej sekcji `error:`. Makro używa `NULL`, dlatego działa
> zarówno w jednostkach translacji C, jak i C++. Jeśli
> `x == NULL`, wykonuje `goto error;`. Funkcja używająca makra musi definiować
> etykietę `error:`.

### Przykłady

**Przykład: informacje o konfiguracji systemu**
```c
#include <hal/system/hal_system.h>

void example_architecture_snapshot(void) {
    hal_system_architecture_t arch = {0};
    hal_status_t status = hal_system_get_current_architecture(&arch);
    if (status != HAL_OK) {
        hal_derr("arch snapshot failed: %s", hal_status_to_string(status));
        return;
    }

    hal_deb("target=%s backend=%s mcu=%s cpu=%s rtos=%s",
            arch.target_name,
            arch.backend_name,
            arch.mcu,
            arch.cpu_arch,
            arch.rtos_name);
    hal_deb("flash total=%lu usable=%lu reserved=%lu ram=%lu heap_free=%lu",
            (unsigned long)arch.flash_total_bytes,
            (unsigned long)arch.flash_usable_bytes,
            (unsigned long)arch.flash_reserved_bytes,
            (unsigned long)arch.ram_total_bytes,
            (unsigned long)arch.heap_free_bytes);
}
```

**Przykład: czas systemowy i watchdog**
```c
#include <hal/system/hal_system.h>
#include <hal/serial/hal_serial.h>

void example_system_timing(void) {
    // Get current time
    uint32_t start_ms = hal_millis();
    uint32_t start_us = hal_micros();

    // Busy-wait for 500 ms with microsecond precision
    hal_delay_us(500000);

    uint32_t elapsed_ms = hal_millis() - start_ms;
    uint32_t elapsed_us = hal_micros() - start_us;

    hal_deb("Elapsed: %lu ms, %lu us", elapsed_ms, elapsed_us);

    // Use time conversion macros
    uint32_t one_minute = MINS(1);   // 60000 ms
    uint32_t five_secs = SECS(5);    // 5000 ms
    uint32_t one_hour = HOURS(1);    // 3600000 ms

    // Setup watchdog: reset if not fed for 5 seconds
    hal_status_t watchdog_status = hal_watchdog_enable(5000, false);
    if (watchdog_status != HAL_OK) {
        hal_derr("watchdog unavailable: %s",
                 hal_status_to_string(watchdog_status));
        return;
    }

    // Main loop with watchdog feeding
    uint32_t loop_count = 0;
    while (loop_count < 100) {
        // Do work...
        hal_delay_ms(100);

        // Feed watchdog every 1 second
        if (loop_count % 10 == 0) {
            hal_watchdog_feed();
        }

        loop_count++;
    }

    hal_deb("Watchdog feeding complete");
}
```

**Przykład: nieblokujący callback interwału w pętli**
```c
#include <hal/system/hal_system.h>

static uint32_t last_publish_ms = 0;

static void publish_cb(void *user_data) {
    (void)user_data;
    callback();
}

void app_task0(void) {
    uint32_t now = hal_millis();

    // Wrap-safe non-blocking interval pattern.
    (void)hal_millis_interval_call(now,
                                   &last_publish_ms,
                                   PUBLISH_INTERVAL,
                                   publish_cb,
                                   NULL);
}
```

Równoważna, jawna forma:

```c
uint32_t now = hal_millis();
if (hal_millis_interval_elapsed(now, &last_publish_ms, PUBLISH_INTERVAL)) {
    callback();
}
```

**Przykład: UID urządzenia i diagnostyka resetu**
```c
#include <hal/system/hal_system.h>
#include <hal/serial/hal_serial.h>

void example_device_uid_and_reset(void) {
    // Very first: initialize fault diagnostics
    hal_fault_subsystem_init();
    hal_stack_guard_init();

    // Get device unique identifier
    uint8_t uid[HAL_DEVICE_UID_BYTES];
    if (hal_get_device_uid(uid) != HAL_OK) {
        return;
    }

    char uid_hex[HAL_DEVICE_UID_HEX_BUF_SIZE];
    if (hal_get_device_uid_hex(uid_hex, sizeof(uid_hex))) {
        hal_deb("Device UID: %s", uid_hex);
    }

    // Check reset reason
    hal_reset_reason_t reset_reason = hal_get_reset_reason();
    hal_deb("Reset reason: %s", hal_reset_reason_str(reset_reason));

    // Check for previous fault
    hal_fault_info_t fault_info;
    if (hal_get_last_fault(&fault_info) && fault_info.valid) {
        hal_deb("Previous fault detected:");
        hal_deb("  PC:  0x%08lx", fault_info.pc);
        hal_deb("  LR:  0x%08lx", fault_info.lr);
        hal_deb("  PSR: 0x%08lx", fault_info.psr);
        hal_deb("  CFSR:  0x%08lx", fault_info.cfsr);
        hal_deb("  HFSR:  0x%08lx", fault_info.hfsr);
        hal_deb("  MMFAR: 0x%08lx", fault_info.mmfar);
        hal_deb("  BFAR:  0x%08lx", fault_info.bfar);
        hal_clear_last_fault();  // Clear for next boot
    }

    // Check for brownout
    if (hal_last_boot_was_brownout()) {
        hal_derr("Brown-out suspected on previous boot!");
    }

    // System info
    uint32_t free_heap = hal_get_free_heap();
    float chip_temp = 0.0f;
    hal_status_t temp_status = hal_read_chip_temp_ex(&chip_temp);
    if (temp_status == HAL_OK) {
        hal_deb("Free heap: %lu bytes, Chip temp: %.1f°C",
                free_heap, chip_temp);
    }

    // Mark alive for brownout detection
    hal_alive_mark();
}
```

**Przykład: sprawdzanie, czy kod działa w przerwaniu**
```c
#include <hal/system/hal_system.h>

static volatile uint32_t isr_counter = 0;

static void my_isr_callback(void) {
    isr_counter++;

    // This is running in ISR context
    if (hal_in_isr()) {
        hal_deb("ISR callback #%lu", isr_counter);
    } else {
        hal_deb("ERROR: Expected ISR context but not in ISR!");
    }
}

void example_isr_detection(void) {
    // In normal task/main context, this is false
    if (!hal_in_isr()) {
        hal_deb("Running in task context (not ISR)");
    }

    // Simulate interrupt trigger (will call my_isr_callback)
    // In real code, this would be triggered by actual hardware interrupt
    my_isr_callback();
}
```

---
```c
void hal_mock_set_millis(uint32_t ms);
void hal_mock_advance_millis(uint32_t ms);
void hal_mock_set_micros(uint32_t us);
void hal_mock_advance_micros(uint32_t us);
bool hal_mock_watchdog_was_fed(void);
void hal_mock_watchdog_reset_flag(void);
void hal_mock_set_caused_reboot(bool val);
void hal_mock_set_free_heap(uint32_t bytes);  // default: 256 KB
void hal_mock_set_chip_temp(float celsius);   // default: 25.0 °C
bool hal_mock_bootloader_was_requested(void);
void hal_mock_bootloader_reset_flag(void);
void hal_mock_set_device_uid(const uint8_t uid[8]);  // override UID
void hal_mock_reset_device_uid(void);                // restore default E661A4D1234567AB
void hal_mock_set_in_isr(bool in_isr);               // forces hal_in_isr() return value for tests
```

**Szczegóły UID urządzenia:**
- `hal_get_device_uid(uid)` wypełnia dokładnie 8-bajtowy bufor wyjściowy i
  zwraca `HAL_EINVAL` dla `NULL`.
- `hal_get_device_uid_hex_ex(buf, buflen)` zapisuje 16 wielkich znaków
  szesnastkowych, po których następuje terminator NUL (17 bajtów łącznie).
  Zgłasza `HAL_EINVAL` dla buforów `NULL` oraz `HAL_EOVERFLOW`, gdy
  `buflen < HAL_DEVICE_UID_HEX_BUF_SIZE`.
- `hal_get_device_uid_hex(buf, buflen)` to starszy adapter zgodności zwracający
  `bool` i korzystający z API zwracającego status.
- Na sprzęcie RP2040 źródłem jest 64-bitowy unikalny identyfikator
  przechowywany w zewnętrznym układzie pamięci flash QSPI, odczytywany poprzez
  `pico_get_unique_board_id()`. Ten identyfikator jest trwały pomiędzy
  restartami, unikalny dla każdego urządzenia i jest używany jako numer
  seryjny USB dla RP.
- Na ESP32-S3 źródłem jest fabryczny eFuse MAC. HAL rozszerza zerami
  48-bitową wartość do publicznej szerokości UID wynoszącej 8 bajtów, bez
  zapisu eFuse'ów.
- W backendzie mock wartość domyślna jest deterministyczna
  (`0xE6 0x61 0xA4 0xD1 0x23 0x45 0x67 0xAB` -> `"E661A4D1234567AB"`), dzięki
  czemu testy porównujące tekstowy UID mogą mieć zaszytą oczekiwaną wartość na
  stałe. Użyj `hal_mock_set_device_uid()`, aby zasymulować drugą płytkę.

**Diagnostyka awarii / usterek (crash / fault):**
```c
typedef enum {
    HAL_RESET_REASON_UNKNOWN = 0,
    HAL_RESET_REASON_POWER_ON,
    HAL_RESET_REASON_RUN_PIN,
    HAL_RESET_REASON_SOFT,
    HAL_RESET_REASON_WATCHDOG,
    HAL_RESET_REASON_DEBUG,
    HAL_RESET_REASON_GLITCH,
    HAL_RESET_REASON_BROWNOUT,
    HAL_RESET_REASON_HARDFAULT,
    HAL_RESET_REASON_STACK_OVERFLOW
} hal_reset_reason_t;

typedef struct {
    bool     valid;   // true if pc/lr/psr below are meaningful
    uint32_t pc;      // stacked PC at fault
    uint32_t lr;      // stacked LR at fault
    uint32_t psr;     // stacked xPSR; mcause on RP2350 RISC-V
    uint32_t cfsr;    // Cortex-M CFSR; zero when unavailable
    uint32_t hfsr;    // Cortex-M HFSR; zero when unavailable
    uint32_t mmfar;   // Cortex-M MMFAR; zero/invalid when unavailable
    uint32_t bfar;    // Cortex-M BFAR; zero/invalid when unavailable
} hal_fault_info_t;

void               hal_fault_subsystem_init(void);
hal_reset_reason_t hal_get_reset_reason(void);
const char        *hal_reset_reason_str(hal_reset_reason_t reason);
hal_status_t        hal_get_last_fault_ex(hal_fault_info_t *out);
bool               hal_get_last_fault(hal_fault_info_t *out);
void               hal_clear_last_fault(void);
bool               hal_last_boot_was_brownout(void);
void               hal_alive_mark(void);
hal_status_t        hal_stack_guard_init_ex(void);
bool               hal_stack_guard_init(void);
void               hal_stack_guard_check(void);
```

`hal_fault_subsystem_init()` należy wywołać raz, możliwie wcześnie podczas
uruchamiania systemu. Punkt wejścia dostarczany przez HAL robi to przed
`app_start()`. Aplikacja z własnym punktem wejścia musi wywołać tę funkcję
samodzielnie. Backendy zapisujące informacje na czas następnego restartu
odczytują i zapamiętują sprzętowe flagi przyczyny resetu, kopiują dane o
awarii do RAM, a następnie usuwają znaczniki ulotne. Dzięki temu następne
zdarzenie zostanie rozpoznane niezależnie.

Zdefiniuj `HAL_ENABLE_STACK_GUARD`, aby włączyć ochronę sprzętową. Rozruch
platformy konfiguruje ją przed uruchomieniem kodu aplikacji. Publiczna funkcja
inicjalizująca sprawdza stan MPU/MSPLIM/PMP targetu albo konfigurację punktu
kontrolnego końca stosu zadania w ESP-IDF. Zwraca `HAL_EHW`, jeśli moduł został
wkompilowany, ale wymagana ochrona sprzętowa nie jest aktywna.

**Typowe użycie podczas konfiguracji i w pętli aplikacji:**
```c
hal_fault_subsystem_init();                   // very first call in setup
if (hal_stack_guard_init_ex() != HAL_OK) {    // verify configured protection
    log("stack guard unavailable");
}
log("reset: %s", hal_reset_reason_str(hal_get_reset_reason()));
hal_fault_info_t f;
if (hal_get_last_fault(&f) && f.valid) {
    log("previous fault: PC=0x%08lx LR=0x%08lx PSR=0x%08lx", f.pc, f.lr, f.psr);
    log("fault status: CFSR=0x%08lx HFSR=0x%08lx MMFAR=0x%08lx BFAR=0x%08lx",
        f.cfsr, f.hfsr, f.mmfar, f.bfar);
}
if (hal_last_boot_was_brownout()) {
    log("suspected brown-out on previous boot");
}
// ... in main loop:
hal_alive_mark();                             // refresh brown-out heuristic marker
```

`hal_stack_guard_check()` pozostaje dostępna dla zgodności źródłowej ze
starszym kodem, który okresowo sprawdzał ochronę stosu. Funkcja nic nie robi,
niezależnie od targetu. Sprzęt i FreeRTOS zgłaszają naruszenia synchronicznie,
dlatego nowe aplikacje nie powinny jej wywoływać.

`HAL_ENABLE_STACK_PROTECTOR` niezależnie włącza zabezpieczenia generowane przez
kompilator. Obsługiwane konfiguracje GCC/Clang kompilują jednostki translacji
HAL i aplikacji z opcją `-fstack-protector-strong`. Uszkodzenie kanarka ramki
funkcji uruchamia ten sam mechanizm resetu co przepełnienie stosu, a informacja
o zdarzeniu pozostaje dostępna po restarcie. `HAL_ENABLE_STACK_GUARD` nadal
odpowiada za sprzętową kontrolę granicy stosu i sprawdzenia FreeRTOS. Obie flagi
mogą być używane osobno.

**impl/rp2040 (rodzina RP):** Obsługę właściwą dla SoC implementuje
`src/hal/impl/rp2040/drivers/rp2040/rp2040_fault.{h,cpp}`, a warstwa HAL
wywołuje funkcje `rp2040_fault_*`. Stan, który ma przetrwać restart, jest
zapisywany w `watchdog_hw->scratch[0..3]`. Rejestry `[4..7]` są zarezerwowane
przez Pico SDK dla argumentów `WATCHDOG_NON_REBOOT_MAGIC` /
`watchdog_reboot()`. Handler HardFault przełącza się na awaryjny stos
danego rdzenia, zapisuje dostępne informacje o awarii w rejestrach scratch z
sygnaturą `'JHD'`, a następnie wywołuje `watchdog_reboot(0, 0, 0)`.
Flaga przepełnienia stosu ma pierwszeństwo przed ogólnym znacznikiem awarii,
dlatego podczas następnego startu zwracane jest
`HAL_RESET_REASON_STACK_OVERFLOW`.

RP2350 ARM korzysta ze sprzętowego statusu `STKOF`. RP2040 nie ma rejestrów
CFSR/MMFAR, dlatego kod uznaje błąd MPU za naruszenie ochrony stosu tylko
wtedy, gdy adres znajduje się bardzo blisko ramki wyjątku. Na RP2350 RISC-V
główny handler trap przełącza stos i dekoduje instrukcję, która wywołała błąd
pamięci, ponieważ Hazard3 nie zapisuje jej adresu w `mtval`.

Przy `HAL_ENABLE_STACK_GUARD` CMake ustawia `PICO_USE_STACK_GUARDS=1`: Pico
SDK używa ochrony MPU na RP2040 oraz właściwego dla architektury RP2350
mechanizmu limitu stosu/PMP dla każdego uruchomionego
rdzenia.

Zapisywanie informacji o awarii na RP zakłada, że standardowe mapowanie XIP
pozwala wykonywać kod. Gdy koordynowana operacja na flash chwilowo wyłączy
XIP, nie ma gwarancji zachowania danych diagnostycznych po ewentualnej awarii.
Wprawdzie początkowy kod handlera RISC-V znajduje się w SRAM, ale dalsza część
odpowiedzialna za rozpoznanie błędu i reset już nie w całości. Nie wpływa to na
sprzętową ochronę stosu podczas normalnej pracy. Aplikacja nie może jednak
zakładać, że po awarii w trakcie operacji na flash z wyłączonym XIP otrzyma
zapis diagnostyczny.

Sprzęt nie zgłasza bezpośrednio `HAL_RESET_REASON_BROWNOUT`, ponieważ POR i BOR
używają jednej flagi. `hal_last_boot_was_brownout()` stosuje więc heurystykę:
zwraca `true`, gdy układ zgłosił POR, ale znacznik działania zapisany w
rejestrze scratch przetrwał. Sugeruje to spadek V<sub>DD</sub> poniżej progu BOR
bez utraty zawartości tych rejestrów.

**impl/stm32g474:** Implementacja korzysta z takiego samego schematu podziału
obsługi jak rodzina RP. Przyczyna resetu jest określana na
podstawie `RCC->CSR`, a zapisany stan pochodzi z rekordu wyjątku Cortex-M4,
który przetrwał restart. Przy `HAL_ENABLE_STACK_GUARD` `SystemInit()`
rezerwuje region MPU nr 7 jako 32-bajtowy obszar bez prawa dostępu i wykonywania,
pod adresem `JH_StackLimit`. Handler błędu przełącza się na
osobny stos w CCMRAM, sprawdza podstawowe i rozszerzone ramki wyjątku oraz
nigdy nie czeka bezterminowo na UART debugowania. Gdy aplikacja później
zainicjalizuje konsolę szeregową, pełny zapis
PC/LR/xPSR/CFSR/HFSR/MMFAR/BFAR jest wypisywany jednokrotnie. Usterka
ochrony MPU, zgłoszenie przepełnienia stosu zadania FreeRTOS lub błąd kanarka
kompilatora są zapisywane i przy następnym uruchomieniu zwracane jako
`HAL_RESET_REASON_STACK_OVERFLOW`.

**impl/esp32:** Przyczyny zwracane przez `esp_reset_reason()` są odwzorowywane
na wartości HAL. Obejmuje to
watchdog, brownout, panika/zawieszenie CPU (lockup), debugger, glitch oraz
resety programowe. Wczesna inicjalizacja instaluje na obu rdzeniach handlery
krytycznych wyjątków Xtensa, które po zapisaniu danych wywołują poprzedni
handler. Zapis obejmuje PC, adres
powrotu, stan procesora, przyczynę wyjątku, adres, wersję i sumę kontrolną w
pamięci RTC `no-init`. Podczas następnego uruchomienia rekord jest sprawdzany i odczytywany;
`hal_get_last_fault_ex()` udostępnia przenośny podzbiór PC/LR/PSR i zwraca
`HAL_ENOENT`, gdy nie ma prawidłowego rekordu. Brownout jest wykrywany
bezpośrednio na podstawie sprzętowej przyczyny resetu, a `hal_alive_mark()`
nic nie robi.

Nieudana instalacja IPC między rdzeniami pozostawia
inicjalizację niekompletną, więc kolejne wywołanie
`hal_fault_subsystem_init()` ponawia próbę dla brakującego rdzenia, zamiast
udostępnić niekompletny stan. Przy `HAL_ENABLE_STACK_GUARD`
wygenerowana konfiguracja ESP-IDF włącza watchpoint końca stosu FreeRTOS, a
`hal_stack_guard_init_ex()` sprawdza tę konfigurację podczas działania.

Kod startowy, ochrona stosu oraz zachowywanie informacji o awarii po restarcie
są na ESP32-S3 zaimplementowane i objęte kontrolą kompilacji oraz linkowania.
Testy sprzętowe nadal muszą potwierdzić zachowanie po wymuszonych awariach i
zachowanie danych po resecie.

Po naruszeniu stosu HAL nie wraca do kodu aplikacji: dane stosu i adresy
powrotu nie są już wiarygodne. Najpierw zapisywany jest rekord diagnostyczny,
a następnie system musi zostać zresetowany.

Kod awaryjny próbuje następnie wypisać krótki komunikat
`STACK OVERFLOW; resetting` przez już aktywny sprzętowy UART. Pomija ten krok,
jeśli żaden bezczynny UART nadający się do użycia w stanie awarii nie jest
dostępny.

Domyślną konsolą RP jest USB CDC, do której celowo nie
odwołuje się handler awarii. Dlatego RP wyświetla komunikat przed resetem tylko
wtedy, gdy aplikacja ma aktywny bezczynny UART sprzętowy. Pełny rekord jest
odczytywany dopiero po restarcie przez zwykłe API diagnostyczne.

**impl/.mock:** Cały stan można ustawiać funkcjami pomocniczymi opisanymi
poniżej. W mocku `hal_fault_subsystem_init()` NIE usuwa przygotowanej przyczyny
resetu ani informacji o awarii. Test może więc ustawić je przed inicjalizacją i
sprawdzić zachowanie podczas jej wykonywania. Aby jawnie wyczyścić ten stan,
użyj `hal_mock_fault_diagnostics_reset()`.

**Funkcje pomocnicze mocka:**
```c
void hal_mock_set_reset_reason(hal_reset_reason_t reason);
void hal_mock_set_last_fault(const hal_fault_info_t *info);  // NULL clears
void hal_mock_set_brownout_suspected(bool v);
bool hal_mock_alive_was_marked(void);
void hal_mock_alive_reset_flag(void);
bool hal_mock_fault_subsystem_was_inited(void);
bool hal_mock_stack_guard_is_armed(void);
void hal_mock_fault_diagnostics_reset(void);
```

---

<a id="halpower-low-power-transitions-optional-halenablepowermanagement"></a>

## `hal_power` - Przejścia niskiego poboru mocy *(opcjonalny - `HAL_ENABLE_POWER_MANAGEMENT`)*

API zasilania działa niezależnie od zarządzania urządzeniem RTC.
`hal_rtc_wakeup_arm_ex()` może ustawić sprzętowe wybudzenie po określonym
czasie. `hal_power_enter_ex()` przeprowadza natomiast całe przejście procesora
w tryb uśpienia, przywraca zegary, koryguje czas monotoniczny, wywołuje
callbacki i określa przyczynę wybudzenia. Włączenie zarządzania zasilaniem dodaje
`HAL_ENABLE_INTERNAL_RTC` oraz `HAL_ENABLE_RTC`.

```c
#include <hal/power/hal_power.h>

typedef enum {
  HAL_POWER_STATE_SLEEP = 0,
  HAL_POWER_STATE_DEEP_SLEEP,
  HAL_POWER_STATE_POWER_DOWN,
} hal_power_state_t;

typedef enum {
  HAL_POWER_POLICY_FAST_WAKE = 0,
  HAL_POWER_POLICY_LOWEST_POWER,
} hal_power_policy_t;

#define HAL_POWER_WAKE_SOURCE_RTC       (1u << 0)
#define HAL_POWER_WAKE_SOURCE_INTERRUPT (1u << 1)

typedef enum {
  HAL_POWER_WAKE_REASON_UNKNOWN = 0,
  HAL_POWER_WAKE_REASON_RTC,
  HAL_POWER_WAKE_REASON_INTERRUPT,
} hal_power_wake_reason_t;

typedef struct {
  bool supported;
  bool resumes_execution;
  bool retains_ram;
  bool can_compensate_monotonic_time;
  uint32_t supported_policies;
  uint32_t wake_sources;
  uint64_t minimum_rtc_timeout_us;
  uint64_t maximum_rtc_timeout_us;
  uint64_t rtc_resolution_us;
} hal_power_capabilities_t;

typedef struct hal_power_result_s {
  hal_power_state_t state;
  hal_power_wake_reason_t reason;
  uint32_t wake_sources;
  uint64_t elapsed_us;
  bool resumed_from_reset;
} hal_power_result_t;

typedef struct {
  hal_power_state_t state;
  hal_power_policy_t policy;
  uint32_t wake_sources;
  hal_rtc_t rtc;
  uint64_t rtc_timeout_us;
  hal_power_prepare_callback_t prepare;
  hal_power_resume_callback_t resume;
  void *user_data;
} hal_power_request_t;

hal_status_t hal_power_get_capabilities_ex(
    hal_power_state_t state, hal_power_capabilities_t *out_capabilities);
hal_status_t hal_power_enter_ex(const hal_power_request_t *request,
                                hal_power_result_t *out_result);
hal_status_t hal_power_get_last_wake_ex(hal_power_result_t *out_result);
```

Przed wyborem stanu zawsze sprawdź jego możliwości. Poprawnie wykonane
zapytanie może zwrócić `supported=false`; przenośny program może wtedy wybrać
płytszy tryb. Wybudzenie RTC przyjmuje dowolny dodatni timeout nieprzekraczający
podanego maksimum i zaokrągla go w górę do wielokrotności
`rtc_resolution_us`. Uchwyt RTC musi wskazywać wewnętrzny, natywny provider
targetu. Dla zewnętrznych układów PCF8563/DS3231 próba ustawienia względnego
wybudzenia zwraca `HAL_EUNSUPPORTED`.

| Target/runtime | `SLEEP` | `DEEP_SLEEP` | `POWER_DOWN` |
|---|---|---|---|
| STM32G474 bare metal | Cortex-M4 Sleep / WFI, polityka szybkiego wybudzenia | STOP0 dla szybkiego wybudzenia, STOP1 dla najniższego poboru mocy | Standby, polityka najniższego poboru mocy, wybudzenie RTC w stylu resetu |
| RP2040/RP2350 bare metal | CPU WFI z RTC AON lub już włączonym przerwaniem | nieobsługiwane przy integracji z Pico SDK w wersji wskazanej przez repozytorium | nieobsługiwane przy integracji z Pico SDK w wersji wskazanej przez repozytorium |
| Mock | deterministyczna symulacja wznowienia | deterministyczna symulacja wznowienia | deterministyczny wynik w stylu resetu; brak callbacku `resume` |
| FreeRTOS | nieobsługiwane, dopóki zarządzanie zasilaniem nie zostanie zintegrowane z planistą i trybem tickless idle | nieobsługiwane | nieobsługiwane |

`HAL_POWER_WAKE_SOURCE_INTERRUPT` oznacza źródło przerwania skonfigurowane już
przez moduł, do którego należy, na przykład GPIO/EXTI. API
zasilania nie konfiguruje pinów, polaryzacji przerwania, trybu wybudzenia
peryferium ani aktywnych transferów. Opcjonalny callback `prepare` jest
wywoływany po ustawieniu zdarzenia wybudzenia RTC i powinien wstrzymać
wyświetlacz, radio, DMA, USB oraz peryferia aplikacji. Gdy wykonanie jest
wznawiane po wybudzeniu, zegary i systemowa baza czasu są przywracane przed
wywołaniem `resume`. Przed zakończeniem `prepare` trzeba także wysłać
zbuforowane komunikaty diagnostyczne; `hal_serial_set_flush(true)` sprawia, że port
USART2 na STM32G474 czeka na fizyczne zakończenie transmisji. Callback
`resume` nie jest wywoływany po `POWER_DOWN`, ponieważ wykonanie zaczyna się od nowa od
resetu.

Na STM32G474 przejścia Sleep/STOP z wybudzeniem RTC zachowują monotoniczność
`hal_micros64()` i `hal_millis()`, dodając zaprogramowany interwał RTC,
podczas gdy SysTick jest zatrzymany. Drzewo PLL 170 MHz oraz SysTick są
przywracane przed powrotem do wywołującego.
`can_compensate_monotonic_time` informuje o dostępności tej korekty na
podstawie RTC. Jeśli tryb STOP zakończy się wyłącznie przez przerwanie, nie ma
precyzyjnego źródła informacji o upływie czasu, więc wyniku nie można traktować
jak zmierzonego czasu uśpienia.

Tryb Standby na STM32G474 domyślnie zapisuje swój znacznik i ustawiony timeout
w rejestrach podtrzymania TAMP nr 30 i 29. Podczas następnego uruchomienia
`SystemInit()` przechwytuje i kasuje ten znacznik, zanim inicjalizacja RTC
zdąży wyczyścić flagi sprzętowe. `hal_power_get_last_wake_ex()` ustawia
`resumed_from_reset=true` tylko wtedy, gdy obecne były flaga Standby i
znacznik. RTC jest podawane jako przyczyna tylko wtedy, gdy ustawiona była również
flaga wybudzenia RTC. Wczesne przechwycenie wyłącza też sprzętowy timer
wybudzenia pozostawiony w domenie podtrzymania. Zmień
`HAL_STM32_POWER_BACKUP_REGISTER_INDEX` oraz
`HAL_STM32_POWER_TIMEOUT_BACKUP_REGISTER_INDEX`, gdy aplikacja używa tych
rejestrów do własnych celów. Oba indeksy i indeks znacznika
integralności RTC muszą być różne.

Przejście jest synchroniczne i może być wykonywane tylko przez jeden kontekst
naraz. Współbieżne przejście
zwraca `HAL_EBUSY`. `prepare` może przerwać operację, zwracając błąd; w takim
wypadku uzbrojone zdarzenie RTC jest anulowane. Przejście w stylu resetu
zwraca `HAL_EAGAIN` bez wejścia w Standby, jeśli jego jednorazowe zdarzenie
RTC wygaśnie w trakcie `prepare`. `out_result` jest opcjonalny dla przejść
kończących się wznowieniem wykonywania, ponieważ ten sam wynik można później odczytać przez
`hal_power_get_last_wake_ex()`.

---

## `hal_bits` - Funkcje pomocnicze dla bitów

```c
#include <hal/core/hal_bits.h>

#define is_set(x, mask)      ...
#define set_bit(var, mask)   ...
#define clr_bit(var, mask)   ...
#define bitSet(var, bit)     ...
#define bitClear(var, bit)   ...
#define bitRead(var, bit)    ...
#define set_bit_v(reg, mask) ...
#define clr_bit_v(reg, mask) ...
```

> **Uwaga:** Wszystkie funkcje pomocnicze to makra (niezależne od szerokości
> typu). Unikaj przekazywania wyrażeń z efektami ubocznymi (`i++`, wywołania
> funkcji ze stanem), ponieważ argumenty mogą zostać obliczone więcej niż raz.
> `bitSet/bitClear/bitRead` pozostają zabezpieczone przez `#ifndef`, dzięki
> czemu istniejące definicje mają pierwszeństwo.

**Wielowątkowość:** Funkcje nie przechowują stanu i same są bezpieczne dla
wielu wątków. Jeśli kilka kontekstów korzysta z tej samej zmiennej lub
rejestru, kod wywołujący musi zapewnić synchronizację.

---

## `hal_compiler` - Atrybuty i funkcje wbudowane kompilatora

```c
#include <hal/core/hal_compiler.h>

#define HAL_COMPILER_IS_GNU_LIKE  0 or 1
#define HAL_COMPILER_IS_MSVC      0 or 1

#define HAL_NORETURN          ...  // function never returns
#define HAL_FORCE_INLINE      ...  // inline specifier plus a forced-inline request
#define HAL_TRAP()            ...  // stop immediately at an unrecoverable point
#define HAL_UNREACHABLE()     ...  // path the program must never take
#define HAL_PACKED            ...  // structure suffix, empty on MSVC
#define HAL_PACKED_BEGIN      ...  // pragma pack(push, 1) on MSVC
#define HAL_PACKED_END        ...  // pragma pack(pop) on MSVC

uint32_t hal_clz32(uint32_t value);  // leading zero count, value must be non-zero
```

Firmware jest zawsze budowany toolchainem GNU, natomiast targety hostowe
obsługują również Clang i MSVC. Wszystkie różnice między tymi kompilatorami są
zamknięte w tym nagłówku, więc obsługę nowego kompilatora hostowego dodaje się
w jednym miejscu. Nagłówek nie zależy od pozostałych części HAL i można go
dołączać bezpośrednio z jednostek translacji runtime, portów i testów.
`hal_config.h` dołącza go automatycznie, dlatego większość źródeł ma już do
niego dostęp.

Umiejscowienie ma znaczenie dla obu kompilatorów:

```c
static HAL_NORETURN void fatal(int code);        // storage class first
static HAL_FORCE_INLINE uint32_t span(uint32_t); // no separate inline keyword

HAL_PACKED_BEGIN
struct wire_header {
  uint8_t kind;
  uint32_t length;
} HAL_PACKED;
HAL_PACKED_END
```

Napisanie `inline` obok `HAL_FORCE_INLINE` powoduje zduplikowanie
specyfikatora na GNU i podnosi ostrzeżenie C4141 na MSVC, dlatego to makro
zawiera ten specyfikator.

Obydwa makra tożsamości można wstępnie zdefiniować jako `0`, co wybiera
przenośny wariant awaryjny: `HAL_TRAP()` staje się `abort()`,
`hal_clz32()` używa pętli, a makra atrybutów rozwijają się do niczego. Test
kompilatora hostowego buduje w ten sposób jedną jednostkę translacji i
porównuje wynik `hal_clz32()` z wariantem korzystającym z funkcji wbudowanej.
Dzięki temu test obejmuje także kod, którego nie wybiera żaden obecnie
obsługiwany kompilator.
Egzotyczny port może użyć tego samego przełącznika, zanim powstanie jego
własne mapowanie.

**Celowo poza zakresem:** atrybuty wpływające na linker (`section`,
`naked`, `constructor`) i asembler inline są zapisane bezpośrednio w kodzie
właściwym dla targetu. Ich błędne odwzorowanie mogłoby niezauważenie uszkodzić
układ pamięci. Źródła firm trzecich zachowują oryginalną postać. Operacje
atomowe pozostają bezpośrednimi wywołaniami `__atomic_*`,
ponieważ każda jednostka translacji, która ich używa, jest kompilowana przez
toolchain GNU.

**Wielowątkowość:** Makra oraz `hal_clz32()` nie przechowują stanu i są
bezpieczne w dowolnym kontekście.

### Przykłady

**Przykład: manipulacja bitami przy pomocy masek**
```c
#include <hal/core/hal_bits.h>

void example_bit_manipulation(void) {
    uint8_t status_reg = 0x00;
    uint8_t mode_mask = 0x0F;      // Lower 4 bits
    uint8_t enabled_mask = 0x80;   // Bit 7

    // Check if bits are set
    if (is_set(status_reg, enabled_mask)) {
        hal_deb("Enabled bit is set");
    }

    // Set multiple bits
    set_bit(status_reg, enabled_mask);  // status_reg |= 0x80
    hal_deb("After set: 0x%02x", status_reg);

    // Set individual bits by index
    bitSet(status_reg, 3);  // Set bit 3
    bitSet(status_reg, 2);  // Set bit 2
    hal_deb("After bitSet: 0x%02x", status_reg);

    // Read individual bit
    uint8_t bit_value = bitRead(status_reg, 7);
    hal_deb("Bit 7 value: %u", bit_value);

    // Clear specific bits
    clr_bit(status_reg, enabled_mask);
    hal_deb("After clear: 0x%02x", status_reg);

    // Clear by bit index
    bitClear(status_reg, 3);
    bitClear(status_reg, 2);
    hal_deb("After bitClear: 0x%02x", status_reg);
}
```

**Przykład: manipulacja bitami rejestru (volatile)**
```c
#include <hal/core/hal_bits.h>

// Simulated hardware register (volatile)
static volatile uint32_t *hw_control_reg = NULL;  // Would be: (uint32_t*)0x40000000

void example_register_bits(void) {
    if (hw_control_reg == NULL) return;

    // Set control bits in volatile register
    uint32_t enable_bit = 0x00000001;
    uint32_t mode_mask = 0x00000030;

    // Set bit in register (atomic operation)
    set_bit_v(hw_control_reg, enable_bit);

    // Clear bits in register
    clr_bit_v(hw_control_reg, mode_mask);

    hal_deb("Register updated");
}
```

---


## `hal_math` - Funkcje pomocnicze matematyczne niezależne od platformy

```c
#include <hal/core/hal_math.h>

// Clamp to [lo, hi] - type-independent macro
#define hal_constrain(v, lo, hi) ...
// Re-map value from one range to another - type-independent macro
// When in_min == in_max, returns out_min (safe: no division by zero).
#define hal_map(x, in_min, in_max, out_min, out_max) ...
```

> **Uwaga:** Makra są dostępne zarówno w C, jak i C++ i są ponownie
> eksportowane poprzez `hal/system/hal_system.h`. `hal_constrain` jest również
> ponownie eksportowany jako `pid_clamp` dla zgodności wstecznej.
> Argumenty makr mogą zostać obliczone więcej niż raz, więc unikaj efektów
> ubocznych w argumentach (na przykład `i++` lub wywołań funkcji zmieniających
> stan).

> **Uwaga:** `hal_map` zwraca `out_min`, gdy `in_min == in_max`, aby uniknąć
> dzielenia całkowitoliczbowego przez zero. Odpowiada to zachowaniu
> `mapfloat()`.

**Wielowątkowość:** Funkcje pomocnicze są czystymi wyrażeniami i nie korzystają
ze współdzielonego stanu, dlatego są bezpieczne dla wielu wątków.

### Przykłady

**Przykład: ograniczanie wartości (clamping)**
```c
#include <hal/core/hal_math.h>
#include <hal/system/hal_system.h>

void example_constrain(void) {
    // Clamp integer to range
    int speed = 150;
    int clamped_speed = hal_constrain(speed, 0, 100);
    hal_deb("Speed %d clamped to %d", speed, clamped_speed);  // Output: 100

    // Clamp float value
    float voltage = -0.5f;
    float safe_voltage = hal_constrain(voltage, 0.0f, 3.3f);
    hal_deb("Voltage %.1f clamped to %.1f", voltage, safe_voltage);  // Output: 0.0

    // ADC reading clamping
    uint16_t raw_adc = 4100;  // 12-bit ADC max is 4095
    uint16_t clamped_adc = hal_constrain(raw_adc, 0, 4095);
    hal_deb("ADC %u clamped to %u", raw_adc, clamped_adc);  // Output: 4095
}
```

**Przykład: przemapowanie/skalowanie wartości**
```c
#include <hal/core/hal_math.h>

void example_map(void) {
    // Map ADC reading (0-4095) to voltage (0.0-3.3V)
    uint16_t adc_value = 2048;  // Midpoint
    int voltage_mv = hal_map(adc_value, 0, 4095, 0, 3300);  // Millivolts
    hal_deb("ADC %u -> %d mV", adc_value, voltage_mv);  // Output: 1650 mV

    // Map 0-255 PWM range to 0-100% duty cycle
    uint8_t pwm = 200;
    uint8_t percent = hal_map(pwm, 0, 255, 0, 100);
    hal_deb("PWM %u -> %u%%", pwm, percent);  // Output: 78%

    // Map temperature sensor reading to usable range
    uint16_t temp_raw = 512;
    int temp_celsius = hal_map(temp_raw, 0, 1023, -40, 125);  // -40 to +125°C
    hal_deb("Raw temp %u -> %d°C", temp_raw, temp_celsius);  // Output: 10°C

    // Inverse mapping: map 100-0% to PWM 0-255
    uint8_t brightness_percent = 75;
    uint8_t pwm_value = hal_map(brightness_percent, 0, 100, 0, 255);
    hal_deb("Brightness %u%% -> PWM %u", brightness_percent, pwm_value);  // Output: 191
}
```

**Przykład: martwa strefa joysticka/potencjometru**
```c
#include <hal/core/hal_math.h>

void example_joystick_with_deadzone(void) {
    uint16_t joystick_x = 500;  // Raw ADC reading (0-1023)
    uint16_t deadzone_low = 450;
    uint16_t deadzone_high = 550;

    // If in deadzone, return center; otherwise map to -100 to +100
    int16_t mapped_x;
    if (joystick_x >= deadzone_low && joystick_x <= deadzone_high) {
        mapped_x = 0;  // Deadzone
    } else if (joystick_x < deadzone_low) {
        // Left side: 0 to deadzone_low -> -100 to 0
        mapped_x = hal_map(joystick_x, 0, deadzone_low, -100, 0);
    } else {
        // Right side: deadzone_high to 1023 -> 0 to +100
        mapped_x = hal_map(joystick_x, deadzone_high, 1023, 0, 100);
    }

    hal_deb("Joystick raw %u -> mapped %d", joystick_x, mapped_x);
}
```

---


---

*Dalej: [Kryptografia](07_crypto.md)*
