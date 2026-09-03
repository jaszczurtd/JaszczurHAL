# Synchronizacja, USB, wyjście szeregowe, ramkowanie i uwierzytelnianie

*Dostępne również [po angielsku](../en/08_sync_serial.md).*

> **Część [Dokumentacji API JaszczurHAL](../../pl/JaszczurHAL_API.md)**

## `hal_sync` - mutex

```c
#include <hal/system/hal_sync.h>

typedef hal_mutex_impl_t* hal_mutex_t;   // nieprzezroczysty

hal_mutex_t hal_mutex_create(void);
void        hal_mutex_lock(hal_mutex_t mutex);
bool        hal_mutex_try_lock(hal_mutex_t mutex);
void        hal_mutex_unlock(hal_mutex_t mutex);
void        hal_mutex_destroy(hal_mutex_t mutex);
```

- **impl/rp2040:** `mutex_t` z Pico SDK w zwykłych buildach RP2040 albo mutex
  FreeRTOS (`xSemaphoreCreateMutex`) w buildach
  `HAL_ENABLE_FREERTOS + __FREERTOS`. Oba warianty są nierekurencyjne
  i synchronizują zadania działające na rdzeniach 0 i 1.
- **impl/stm32g474:** atomowy spinlock dla jednego rdzenia w buildach bez
  FreeRTOS albo mutex FreeRTOS (`xSemaphoreCreateMutex`) w buildach
  `HAL_ENABLE_FREERTOS`. Oba warianty są nierekurencyjne.
- **impl/esp32:** mutex FreeRTOS z ESP-IDF (`xSemaphoreCreateMutex`), nierekurencyjny
  i wyłącznie w kontekście zadania.
- **impl/.mock:** `std::mutex`.
- **Uwaga o FreeRTOS:** na RP2040/RP2350 i STM32G474 implementacja
  `hal_mutex_*` korzysta z FreeRTOS, gdy `HAL_ENABLE_FREERTOS` wybiera
  ustaloną w projekcie wersję kernela. Na ESP32-S3 używa schedulera
  dostarczanego przez wersję ESP-IDF określoną w zależnościach projektu.
  `hal_mutex_*` można wywoływać wyłącznie z kontekstu zadania; nie jest to API dla ISR.
  Mutexy singletonów i magistral korzystają z wewnętrznego, atomowego mechanizmu
  jednokrotnego tworzenia, jeśli nadal potrzebny jest defensywny fallback z
  inicjalizacją przy pierwszym użyciu.
  `hal_mutex_try_lock()` nigdy nie czeka. Mogą go używać procedury obsługi
  przerwań w trybie bare metal; backendy FreeRTOS zwracają `false`, gdy funkcja
  zostanie wywołana z kontekstu przerwania.

### Starsze makra mutexów (`tools.h`)

```c
m_mutex_def(name)            // static hal_mutex_t name = NULL
m_mutex_init(name)           // name = hal_mutex_create()
m_mutex_enter_blocking(name) // hal_mutex_lock(name)
m_mutex_exit(name)           // hal_mutex_unlock(name)
```

### Sekcja krytyczna oparta na maskowaniu przerwań

```c
void hal_critical_section_enter(void);  // zapisz i wyłącz przerwania
void hal_critical_section_exit(void);   // przywróć poprzedni stan przerwań
```

- **impl/rp2040:** bezpieczne przy zagnieżdżaniu i osobne dla każdego rdzenia
  `save_and_disable_interrupts()` / `restore_interrupts()` z Pico SDK, także
  w buildach FreeRTOS.
- **impl/stm32g474:** bezpieczna przy zagnieżdżaniu pełna maska przerwań PRIMASK, łącznie z
  buildami FreeRTOS.
- **impl/esp32:** bezpieczna przy zagnieżdżaniu sekcja krytyczna `portMUX_TYPE`
  z ESP-IDF, wspólna dla obu rdzeni i ze śledzeniem głębokości osobno dla
  każdego z nich.
- **impl/.mock:** funkcje nic nie robią (`no-op`).

> **Uwaga:** Ten mechanizm bezpośrednio maskuje przerwania i jest przeznaczony
> do krótkich sekcji wrażliwych na czas lub współdzielonych z ISR. Na ESP32-S3
> wspólny `portMUX` dodatkowo serializuje oba rdzenie, natomiast RP2040 maskuje
> przerwania tylko na rdzeniu wywołującym. Nie jest to blokada planisty
> FreeRTOS; do wzajemnego wykluczania zadań użyj `hal_mutex_t`.

### Przykłady

**Przykład: Ochrona współdzielonego stanu za pomocą `hal_mutex_t`**
```c
#include <hal/system/hal_sync.h>

static hal_mutex_t s_stats_mutex;
static uint32_t s_success_count = 0;

void stats_init(void) {
  s_stats_mutex = hal_mutex_create();
}

void stats_note_success(void) {
  hal_mutex_lock(s_stats_mutex);
  s_success_count++;
  hal_mutex_unlock(s_stats_mutex);
}

uint32_t stats_snapshot(void) {
  uint32_t copy;

  hal_mutex_lock(s_stats_mutex);
  copy = s_success_count;
  hal_mutex_unlock(s_stats_mutex);

  return copy;
}
```

**Przykład: Krótka sekcja z maskowaniem przerwań dla flag współdzielonych z ISR**
```c
#include <hal/system/hal_sync.h>

static volatile bool s_alarm_fired = false;

void alarm_isr_hook(void) {
  hal_critical_section_enter();
  s_alarm_fired = true;
  hal_critical_section_exit();
}

bool consume_alarm_flag(void) {
  bool fired;

  hal_critical_section_enter();
  fired = s_alarm_fired;
  s_alarm_fired = false;
  hal_critical_section_exit();

  return fired;
}
```

---

## `hal_usb` - cykl życia urządzenia USB i CDC

```c
#include <hal/usb/hal_usb.h>

hal_status_t hal_usb_init(void);
hal_status_t hal_usb_deinit(void);
hal_status_t hal_usb_task(void);
hal_status_t hal_usb_cdc_is_connected(bool *out_connected);
hal_status_t hal_usb_cdc_available(size_t *out_available);
hal_status_t hal_usb_cdc_read(uint8_t *data, size_t capacity,
                              size_t *out_read);
hal_status_t hal_usb_cdc_write(const uint8_t *data, size_t length,
                               uint32_t timeout_ms, size_t *out_written);
hal_status_t hal_usb_cdc_flush(uint32_t timeout_ms);
hal_status_t hal_usb_reset_to_bootloader(void);

typedef void (*hal_usb_bootloader_reset_hook_t)(void *user);
hal_status_t hal_usb_set_bootloader_reset_hook(
    hal_usb_bootloader_reset_hook_t hook, void *user);
```

Natywny backend RP jako jedyny zarządza TinyUSB. Udostępnia deskryptory CDC i numer
seryjny urządzenia. Obsługa na pierwszym planie jest chroniona mutexem, a praca w tle
odbywa się przez IRQ lub timer o niskim priorytecie. Backend ogranicza oczekiwanie
spowodowane zapełnieniem bufora TX i obsługuje reset do BOOTSEL wyzwalany sygnałem DTR
przy 1200 bps. `hal_usb_init()` udostępnia w runtime informację, czy płytka obsługuje USB.

`hal_usb_set_bootloader_reset_hook()` rejestruje opcjonalny callback wywoływany
bezpośrednio przed resetem do bootloadera (zarówno przy wyzwalaczu DTR 1200 bps, jak i przy
`hal_usb_reset_to_bootloader()`). Służy do wykonania czynności związanych z zamykaniem
aplikacji oraz do testów hostowych i mocków; callback nie może blokować. Przekazanie
`NULL` usuwa rejestrację.

STM32G474 obecnie zwraca `HAL_EUNSUPPORTED`; mock hosta dostarcza deterministyczne
bufory RX/TX oraz obserwację resetu na potrzeby testów jednostkowych.
ESP32-S3 nie udostępnia tego publicznego cyklu życia USB. Jego konsola diagnostyczna
korzysta z opisanego poniżej VFS USB Serial/JTAG skonfigurowanego podczas startupu.

---

## `hal_serial` - wyjście szeregowe i debugowanie

```c
#include <hal/serial/hal_serial.h>

// Konfigurowalne rozmiary buforów debug (w razie potrzeby zdefiniuj przed dołączeniem):
// #define HAL_DEBUG_BUF_SIZE    1024   // bufor pomocniczy przechwytywania/RX mocka (legacy)
// #define HAL_DEBUG_PREFIX_SIZE  16
// #define HAL_DEBUG_DEFAULT_BAUD 9600   // używane przez leniwą inicjalizację
// #define HAL_DEBUG_RATE_LIMIT_SOURCES_MAX 16
// #define HAL_DEBUG_RATE_LIMIT_SOURCE_NAME_MAX 24
// #define HAL_DEBUG_ISR_SLOT_COUNT 64u  // sloty pierścienia SPSC dla logów odroczonych z ISR (>= 2)
// #define HAL_DEBUG_ISR_TEXT_MAX  160u  // ładunek na rekord (wraz z terminatorem NUL)

typedef struct {
    uint16_t full_logs_limit;   // domyślnie 5
    uint32_t min_gap_ms;        // domyślnie 1000 ms
    uint32_t summary_every_ms;  // domyślnie 30000 ms
} hal_debug_rate_limit_t;

void hal_serial_begin(uint32_t baud);
void hal_serial_set_flush(bool enabled);
void hal_serial_print(const char *s);
void hal_serial_println(const char *s);
int  hal_serial_available(void);   // liczba bajtów oczekujących w buforze RX
int  hal_serial_read(void);        // odczytaj jeden bajt lub -1, gdy pusto

hal_debug_rate_limit_t hal_debug_rate_limit_defaults(void);
const hal_debug_rate_limit_t *hal_debug_get_rate_limit(void);

void hal_debug_init(uint32_t baud, const hal_debug_rate_limit_t *cfg = 0);
// cfg == 0 -> wartości domyślne

bool hal_deb_is_initialized(void);        // odpytaj stan inicjalizacji
void hal_deb_set_prefix(const char *prefix);
void hal_deb(const char *format, ...);    // w stylu printf, strumieniowe, thread-safe
void hal_derr(const char *format, ...);   // to samo, ale z prefiksem "ERROR! "
void hal_derr_limited(const char *source, const char *format, ...);
// source to dowolny znacznik zdefiniowany przez wywołującego (np. "gps", "can"); 0 -> "global"
void hal_deb_hex(const char *prefix, const uint8_t *buf, int len, int maxBytes);
// loguje: "<prefix> len=<n> bytes: XX XX ...", maxBytes jest przycinane do 1..48

void hal_debug_loop(void);  // opróżnij rekordy debug odroczone z ISR (wywołuj z pętli głównej)
```

### Formatowanie debugowania w kontekście zadania

W kontekście zadania `hal_deb()`, `hal_derr()` oraz ścieżka pełnego komunikatu
`hal_derr_limited()` nie budują już całej sformatowanej linii logu w buforze
o stałym rozmiarze `HAL_DEBUG_BUF_SIZE`. Wspólna implementacja wyjścia
szeregowego i debugowania przesyła dane strumieniowo bezpośrednio do chronionej
mutexem funkcji zapisującej w transporcie:

- stałe fragmenty tekstu i dane `%s` są emitowane w częściach bez bufora
  roboczego dla całej linii
- konwersje liczbowe, zmiennoprzecinkowe i wskaźnikowe używają małego lokalnego bufora;
  jeśli pojedynczy wynik się w nim nie mieści, tymczasowo używany jest bufor o dokładnie
  wymaganym rozmiarze
- prefiksy (`hal_deb_set_prefix()`, `ERROR!`, znaczniki czasu i tagi źródła limitu
  szybkości) są emitowane jako oddzielne fragmenty strumienia pod tym samym mutexem TX,
  więc logiczna linia logu nadal nie może przeplatać się z danymi od innego
  nadawcy korzystającego z wyjścia szeregowego

`HAL_DEBUG_BUF_SIZE` nie ogranicza już długości logu zapisywanego z zadania.
Parametr pozostaje dostępny dla zgodności wstecznej z helperami przechwytywania
i odbioru danych w mocku.
Rekordy odroczone z ISR są nadal celowo ograniczone przez `HAL_DEBUG_ISR_TEXT_MAX`,
ponieważ ścieżka ISR nie może alokować, blokować ani korzystać z transportu
szeregowego.

### Logowanie debug odroczone z ISR

`hal_deb()`, `hal_derr()` i `hal_derr_limited()` można teraz wywoływać z
kontekstu przerwania, ale mimo to **należy tego unikać**. Funkcje wykrywają ISR
przez `hal_in_isr()`. W kodzie wykonywanym bezpośrednio w przerwaniu **nie**
przejmują mutexu, **nie** przeprowadzają inicjalizacji przy pierwszym użyciu,
**nie** wywołują hooka znacznika czasu, **nie** przeszukują tabeli limitera
szybkości i **nie** wykonują operacji wejścia/wyjścia UART.

Sformatowane dane trafiają do wspólnego, bezblokadowego (lock-free) pierścienia
SPSC z jednym producentem i jednym konsumentem (single-producer / single-consumer).
Pierścień ma `HAL_DEBUG_ISR_SLOT_COUNT` slotów po `HAL_DEBUG_ISR_TEXT_MAX`
bajtów każdy - domyślnie 64 × 160 B - i korzysta z operacji atomowych
release/acquire. W przypadku `hal_derr_limited()` tag `[source]` jest od razu
umieszczany w tekście dodawanym do kolejki, ponieważ w ISR globalny limiter
jest pomijany.

`hal_debug_loop()` opróżnia pierścień z kontekstu zadania i emituje każdy
rekord przez zwykłą, chronioną mutexem ścieżkę szeregową. Każda opróżniona linia jest
opatrzona adnotacją `[ISR ts=<micros>]` (oryginalny czas zdarzenia, nie "teraz") i
respektuje bieżący `hal_deb_set_prefix()` dla rekordów debug oraz standardowy znacznik
`ERROR! ` dla rekordów błędów. Gdy producent przepełni pierścień, zwiększa wewnętrzny
licznik, a następne opróżnienie emituje jedną linię podsumowującą
`"ERROR! [ISR] dropped N debug message(s)"` przed wyzerowaniem licznika.
Po wywołaniu `hal_debug_set_muted(true)` producent po cichu odrzuca nowe
rekordy, nie zapisując ich w pierścieniu ani nie zwiększając licznika.
Konsument usuwa rekordy już oczekujące i zeruje licznik odrzuceń. Żadna z tych
ścieżek nie wykonuje dalszej pracy.

`hal_debug_loop()` można bezpiecznie wywoływać już od pierwszej iteracji pętli
głównej, nawet jeśli wcześniej nie uruchomiono `hal_debug_init()`, `hal_deb()`
ani `hal_derr()`. Przy wysyłaniu danych funkcja przechodzi tę samą inicjalizację
przy pierwszym użyciu co `hal_deb()`. Ścieżki szybkiego wyjścia w ISR i po
wyciszeniu opierają się natomiast wyłącznie na statycznych zmiennych
wyzerowanych podczas startu. Wywołanie z ISR nie wykonuje żadnej operacji, dzięki czemu nie
może ponownie wejść w opróżnianie kolejki chronione mutexem UART.

**Pomocnicy introspekcji pierścienia tylko dla mocka** (zadeklarowane w `hal_mock.h`):

```c
size_t   hal_mock_debug_isr_used_slots(void);            // oczekujące rekordy
size_t   hal_mock_debug_isr_capacity(void);              // bieżąca pojemność pierścienia
uint32_t hal_mock_debug_isr_dropped(void);                // licznik przepełnień (podgląd)
void     hal_mock_debug_isr_reset(void);                  // wyczyść head/tail/dropped
void     hal_mock_debug_isr_set_test_capacity(size_t);    // przełącz na mały pierścień testowy
void     hal_mock_debug_isr_restore_default_ring(void);   // przywróć pierścień produkcyjny
```

### Leniwa inicjalizacja

`hal_deb()` i `hal_derr()` inicjalizują się przy pierwszym użyciu. Jeśli przed
pierwszym komunikatem debug nie wywołano `hal_debug_init()`, funkcja zostanie
uruchomiona automatycznie z `HAL_DEBUG_DEFAULT_BAUD` (domyślnie 9600, z
możliwością nadpisania przez `-D`). Na RP2040/RP2350, STM32G474, ESP32-S3
i w implementacji testowej zarówno inicjalizacja, jak i udostępnienie mutexu
singletonu są chronione atomowo. Dwa zadania lub rdzenie nie zresetują więc
równocześnie stanu debug ani nie utworzą konkurencyjnych instancji mutexu,
z których jedna zostałaby porzucona. Jawne wywołanie `hal_debug_init_default()` nie jest już
obowiązkowe.

`hal_derr_limited()` korzysta z tego samego mechanizmu inicjalizacji. Globalna
konfiguracja limitu częstotliwości jest stosowana osobno dla każdego znacznika
źródła błędu (`source`), aby błędy z różnych modułów nie tłumiły się wzajemnie.

### Serializacja TX (R1.8)

`hal_serial_print()` i `hal_serial_println()` przejmują wspólny, globalny mutex
TX na czas zapisu do konsoli debug. Port RP wybrany podczas linkowania zapisuje
przez CDC modułu `hal_usb`, a ESP32-S3, STM32 i implementacja testowa korzystają
z odpowiednich dla siebie transportów. Dzięki temu dane ze wszystkich źródeł
trafiających do tego samego łącza są wysyłane kolejno. Dotyczy to funkcji debug
(`hal_deb`, `hal_derr`, `hal_derr_limited`), obsługi sesji ramkowanej
(`hal_serial_session_println`) oraz bezpośrednich wywołań API.

Bez tej blokady `hal_deb` uruchomione na rdzeniu 1 RP2040 mogłoby wstawić
swoje bajty w środek odpowiedzi sesji wysyłanej przez rdzeń 0. Utrata choćby
jednego bajtu CDC naruszałaby wtedy CRC ramki `$SC,...*<crc>` i zmuszała hosta
do ponowienia polecenia. Mutexy poszczególnych funkcji
(`s_deb_mutex`, `s_derr_mutex`) serializują stan funkcji debug, ale same nie
chronią transportu przed jednoczesnym zapisem z innych miejsc.

Mutex TX jest tworzony przez ten sam atomowy mechanizm jednokrotnej
inicjalizacji co blokady innych singletonów. Jeśli `hal_debug_init()` zostanie
wywołane jawnie, mutex powstaje już podczas tej inicjalizacji. Jest więc
dostępny także dla komunikatów wysyłanych na bardzo wczesnym etapie rozruchu.
Blokada TX jest zawsze przejmowana **wewnątrz** `s_deb_mutex`, `s_derr_mutex`
lub `s_rl_mutex`, nigdy w odwrotnej kolejności. Dzięki temu deadlock jest
niemożliwy.

W backendach RP USB-CDC okres utrzymywania mutexu może dodatkowo objąć
`hal_usb_cdc_flush()` po każdym `hal_serial_print()` /
`hal_serial_println()`. Jest to domyślnie wyłączone i można to zmienić w runtime
za pomocą `hal_serial_set_flush(bool enabled)`. Pętla zapisu RP2040
samodzielnie uruchamia obsługę FIFO CDC, dlatego transmisja krótkich pakietów
rozpoczyna się także bez tej opcji. Dodatkowy `flush` jest przeznaczony dla
aplikacji, które chcą jeszcze raz odpytać transport przed zwolnieniem mutexu TX.

Ustawienie `hal_serial_set_flush(false)` pozostawia backend RP w trybie
domyślnym. Dodatkowe odpytywanie i `flush` są wtedy pomijane, ale mutex TX
nadal chroni zapis. Nie wyłącza to ograniczonej liczby ponowień w pętli
zapisu, gdy FIFO CDC jest pełne. Na ESP32-S3 włączenie tej opcji wywołuje
`fsync(stdout)` dla konsoli VFS skonfigurowanej podczas startu. Na STM32G474
kod czeka po każdym komunikacie na flagę zakończenia transmisji USART2. Jest
to przydatne przed zmianą zegara peryferiów przez STOP lub przed wyłączeniem
konsoli przez aplikację. Implementacja testowa przyjmuje to ustawienie, ale
nie symuluje zależności czasowych platformy docelowej.

### Wspólna implementacja i porty transportowe wybierane podczas linkowania

Cała wspólna implementacja wyjścia szeregowego i debugowania znajduje się
w `src/hal/serial/hal_serial.cpp`. Obejmuje publiczne funkcje obu API,
formatowanie strumieniowe, prefiksy, hooki znacznika czasu, stan wyciszenia,
sloty limitu częstotliwości, pierścień SPSC dla ISR, kopiowanie komunikatów do
konsoli sieciowej, inicjalizację przy pierwszym użyciu oraz wszystkie wspólne
mutexy. Wewnętrzny interfejs `jh_serial_port.h` jest wybierany podczas
linkowania. Celowo udostępnia tylko operacje transportowe: uruchamianie
i konfigurację, oznaczanie końca logicznego komunikatu, zapis i odczyt bajtu
oraz właściwe dla platformy zakończenie linii i `flush`.

Porty produkcyjne są celowo niewielkie:

- `impl/rp2040/hal_serial.cpp` obsługuje uruchamianie USB CDC, TX/RX oraz
  opcjonalny `flush`;
- `impl/esp32/hal_serial.cpp` ponownie wykorzystuje konsolę VFS USB Serial/JTAG
  skonfigurowaną podczas startu ESP-IDF. Korzysta z oficjalnego, buforowanego
  `usb_serial_jtag_driver`. Jeśli go brakuje, instaluje jedną instancję
  drivera i nigdy nie rejestruje drugiego właściciela VFS. Argument `baud`
  ma znaczenie informacyjne. Odbiór jest nieblokujący i używa 256-bajtowego
  bufora HAL, a opcjonalny `flush` wywołuje `fsync(stdout)`;
- `impl/stm32g474/hal_serial.cpp` obsługuje sprzętowy USART2 oraz `stdout`
  hosta w buildach sprawdzających target; dla RX zwraca obecnie informację
  o braku obsługi;
- `impl/.mock/hal_serial.cpp` deterministycznie przechwytuje ostatni komunikat
  i obserwuje `stdout`; pozwala też podawać binarne dane RX w testach.

Zakończenie linii zależy od transportu. Sprzęt RP i STM32 wysyła `\r\n`,
natomiast ESP32-S3, hostowy wariant STM32 i implementacja testowa używają
`\n`. Zgodnie z dotychczasowym API testowym przechwycony komunikat nie zawiera
zakończenia linii.

Ścieżka asercji RP korzysta z tego samego surowego transportu i kopiuje dane
do konsoli sieciowej, ale nie przejmuje mutexu TX. Dzięki temu komunikat
o błędzie krytycznym nie może zablokować się na mutexie utrzymywanym przez
kontekst, w którym wystąpił błąd.

Szczegóły implementacji limitera:

- źródło jest dopasowywane na podstawie `hash + tekst źródła`, z dodatkowym
  sprawdzeniem zabezpieczającym przed kolizją;
- stan limitera chroni wewnętrzny mutex, więc dostęp jest thread-safe;
- po wyczerpaniu `HAL_DEBUG_RATE_LIMIT_SOURCES_MAX` nowe źródła trafiają do
  wspólnej puli `overflow`, zamiast przejmować stan niezwiązanego źródła.

**Publiczne helpery debug w `hal/serial/hal_serial.h`:**
```c
void hal_debug_init_default(void);
void hal_debug_set_module_prefix(const char *module_name);

#define deb            hal_deb
#define derr           hal_derr
#define derr_limited   hal_derr_limited
```

Krótkie nazwy `deb` i `derr` są stabilnymi, wspieranymi aliasami publicznymi;
nie są przeznaczone do usunięcia. `hal_debug_set_module_prefix(...)` w razie
potrzeby przycina nazwę modułu, tak aby
wygenerowany prefiks `<module>:` zawsze mieścił się w `HAL_DEBUG_PREFIX_SIZE`.

Architekturę i zachowanie współbieżne sprawdzają `test_serial_architecture`,
`test_hal_serial` oraz test runtime'u FreeRTOS POSIX. Testy chronią przed
ponownym wprowadzeniem osobnych implementacji rdzenia debug dla poszczególnych
platform. Weryfikują też, czy mutex jest bezpiecznie udostępniany podczas inicjalizacji,
niepodzielność komunikatów, podsumowania przepełnień FIFO ISR, wyciszanie,
zakończenia linii właściwe dla transportu oraz binarne RX implementacji
testowej.

### Polityka obsługi błędów

- `HAL_ASSERT(...)` jest używane dla krytycznych błędów programistycznych
  w podstawowych mechanizmach, na przykład dla mutexu `NULL` podczas
  synchronizacji.
- Przy niekrytycznym, nieprawidłowym użyciu API peryferiów w runtime walidacja
  zwraca błąd i zapisuje komunikat w logu, a program może działać dalej.
- `hal_derr(...)` drukuje każdy błąd (bez tłumienia).
- `hal_derr_limited(source, ...)` jest zalecane dla potencjalnie powtarzalnych,
  niekrytycznych błędów, aby uniknąć zalewania logów.

### Przykłady

**Przykład: Inicjalizacja wyjścia debug z niestandardowym limitem szybkości**
```c
#include <hal/serial/hal_serial.h>

void debug_setup(void) {
  hal_debug_rate_limit_t cfg = hal_debug_rate_limit_defaults();

  cfg.full_logs_limit = 3;
  cfg.min_gap_ms = 2000;
  cfg.summary_every_ms = 10000;

  hal_debug_init(115200, &cfg);
  hal_deb_set_prefix("net");
  // Opcjonalne na RP2040, gdy pożądany jest dodatkowy flush/odpytanie zadania USB CDC:
  // hal_serial_set_flush(true);
  hal_deb("debug channel ready");
}
```

**Przykład: Pętla główna z odpytywaniem RX i odroczonym opróżnianiem logu ISR**
```c
#include <hal/serial/hal_serial.h>

void app_loop(void) {
  hal_debug_loop();

  while (hal_serial_available() > 0) {
    int ch = hal_serial_read();
    if (ch == 'r') {
      hal_deb("received restart request");
    }
  }
}
```

---

## `hal_serial_session` - pomocnik ramkowanej sesji szeregowej

```c
#include <hal/serial/hal_serial_session.h>

#define HAL_SERIAL_SESSION_PROTOCOL_VERSION 1u
#define HAL_SERIAL_SESSION_MAX_LINE         128u
#define HAL_SERIAL_SESSION_UNKNOWN          "unknown"

typedef void (*hal_serial_session_unknown_cb_t)(const char *line, void *user);

typedef struct {
    bool        active;
    uint32_t    session_id;
    uint32_t    hello_counter;
    uint32_t    last_activity_ms;
    uint8_t     line_len;
    char        line[HAL_SERIAL_SESSION_MAX_LINE + 1u];
    const char *module_tag;   // wiązane przy init
    const char *fw_version;   // wiązane przy init (może być NULL -> "unknown")
    const char *build_id;     // wiązane przy init (może być NULL -> "unknown")
    uint8_t     uid_bytes[HAL_DEVICE_UID_BYTES];       // przechwycone przy init (auth)
    char        uid_hex[HAL_DEVICE_UID_HEX_BUF_SIZE];  // przechwycone przy init
    hal_serial_session_unknown_cb_t unknown_handler;   // opcjonalny odbiornik
    void       *unknown_user;
    bool        in_request;   // zezwala na `hal_serial_session_println` tylko w żądaniu
    uint16_t    request_seq;  // seq echowane w ramkowanych odpowiedziach
    const hal_serial_session_vocabulary_t *vocab;
    // Stan uwierzytelniania (Faza 3)
    bool        authenticated;
    bool        challenge_pending;
    uint8_t     challenge[HAL_SC_AUTH_CHALLENGE_BYTES];
    uint32_t    auth_counter; // pomyślnie wydane losowe wyzwania
    uint32_t    auth_failures;
} hal_serial_session_t;

void     hal_serial_session_init(hal_serial_session_t *session,
                                 const char *module_tag,
                                 const char *fw_version,
                                 const char *build_id);
void     hal_serial_session_init_with_vocabulary(
                                 hal_serial_session_t *session,
                                 const char *module_tag,
                                 const char *fw_version,
                                 const char *build_id,
                                 const hal_serial_session_vocabulary_t *vocab);
void     hal_serial_session_set_unknown_handler(hal_serial_session_t *s,
                                                hal_serial_session_unknown_cb_t cb,
                                                void *user);
hal_status_t hal_serial_session_attach_unknown_handler(
                                 hal_serial_session_t *session,
                                 hal_serial_session_unknown_cb_t cb,
                                 void *user);
hal_status_t hal_serial_session_detach_unknown_handler(
                                 hal_serial_session_t *session,
                                 hal_serial_session_unknown_cb_t cb,
                                 void *user);
bool     hal_serial_session_is_active(const hal_serial_session_t *session);
bool     hal_serial_session_is_authenticated(const hal_serial_session_t *session);
uint32_t hal_serial_session_id(const hal_serial_session_t *session);
hal_status_t hal_serial_session_current_request_seq(
                                 const hal_serial_session_t *session,
                                 uint16_t *out_seq);
void     hal_serial_session_poll(hal_serial_session_t *session);
void     hal_serial_session_println(hal_serial_session_t *session,
                                    const char *payload);
hal_status_t hal_serial_session_println_ex(hal_serial_session_t *session,
                                           const char *payload);
```

Protokół łącza (w obu kierunkach):

    $SC,<seq>,<inner>*<crc8>\n

Kodek ramki opisano w [`hal_serial_frame`](#halserialframe-pomocnicy-ramkowania-na-łączu).

Wbudowane polecenie strukturalne, rozpoznawane zawsze:
- `HELLO` - aktywuje sesję, generuje nowy `session_id` i wysyła odpowiedź z danymi
  identyfikacyjnymi urządzenia.

Odpowiedź HELLO jest jedyną odpowiedzią o stałej strukturze. Każdy host
analizuje ją w postaci
`module=... proto=... session=... fw=... build=... uid=...`:

    OK HELLO module=<name> proto=1 session=<id> fw=<ver> build=<id> uid=<hex>

Polecenia sterowane słownikiem (R1.0 + R1.6 + R1.7):
- `cmd_bye` - zamyka ramkowaną sesję. Odpowiada `reply_bye_ok` (gdy
  ustawione), czyści `active` i zeruje stan uwierzytelnienia, gdy włączono
  CRYPTO. Polecenie zawsze kończy się powodzeniem; nieaktywna sesja po prostu
  ponownie odpowiada OK. Obsługa BYE nie zależy od `HAL_ENABLE_CRYPTO`, dlatego
  każdą sesję można poprawnie zamknąć niezależnie od tego, czy kod AUTH został
  skompilowany.
- `cmd_auth_begin` - generuje świeże 16-bajtowe wyzwanie dla aktywnej sesji;
  pomocnik formatuje wyzwanie przez `reply_auth_challenge_fmt` (musi
  zawierać `%s` dla bajtów hex).
- `cmd_auth_prove <64 hex chars>` - potwierdza, że host zna `K_device` dla
  tego UID. Wyniki przechodzą przez tokeny odpowiedzi słownika
  (`reply_auth_ok` przy sukcesie, jeden z rodziny `reply_auth_failed_*` przy
  niepowodzeniu, `reply_not_ready_hello_required`, jeśli HELLO nie zostało jeszcze widziane).
- `cmd_reboot_bootloader` - wymaga uwierzytelnienia. Po powodzeniu wysyła
  `reply_reboot_ok`, przez około 50 ms opróżnia bufory, a następnie przechodzi do ROM-u
  rozruchowego w trybie pamięci masowej BOOTSEL/UF2. Bez uwierzytelnienia wysyła
  `reply_not_authorized`.

Od R1.6 te tokeny NIE są zapisane na sztywno w JaszczurHAL. Pochodzą z
instancji `hal_serial_session_vocabulary_t` przekazanej przez projekt do
`hal_serial_session_init_with_vocabulary`. Jeśli pole ma wartość `NULL` albo
sesję zainicjalizowano przez klasyczne `hal_serial_session_init`, odpowiadające
mu polecenie nie jest rozpoznawane. Wewnętrzny payload trafia wtedy do handlera
nierozpoznanych linii. Słownik projektu Fiesta znajduje się w
`Fiesta/src/common/scDefinitions/sc_session_vocabulary.h`
(`fiesta_default_vocabulary`); zobacz poniższą sekcję o konfiguracji słownika.

Nierozpoznane wewnętrzne payloady są obsługiwane następująco:
- jeśli callback użytkownika jest zarejestrowany przez
  `hal_serial_session_set_unknown_handler`, otrzymuje rozpakowaną zawartość ramki
  i odpowiada za wysłanie ewentualnej odpowiedzi (użyj
  `hal_serial_session_println`, aby odpowiedź odziedziczyła `<seq>` żądania).
- `hal_serial_session_attach_unknown_handler()` rejestruje callback w wolnym slocie,
  nie zastępując callbacku projektu. Odpowiadająca jej funkcja
  `hal_serial_session_detach_unknown_handler()` usuwa wyłącznie tę samą parę
  callback/user. Adaptery transportu używają tych funkcji zwracających status,
  aby bezpiecznie rezerwować i zwalniać slot.
- w przeciwnym razie kod wysyła `reply_unknown_cmd` ze słownika (odpowiedź
  nadal jest ramkowana). Przy klasycznej inicjalizacji to pole jest NULL, więc nieznana linia
  jest po cichu porzucana - zarejestruj callback, aby ją obserwować.

Dane wejściowe bez ramek są po cichu odrzucane. Nie ma ścieżki fallback dla
zwykłego tekstu. Narzędzia hostowe muszą ramkować żądania; usunięcie dawnej
obsługi zapobiega błędnemu rozpoznawaniu fragmentów logu jako poleceń.

Zasady przechowywania danych identyfikacyjnych:
- `module_tag` nie może mieć wartości `NULL` i musi wskazywać tekst o statycznym
  czasie życia (zwykle stałą modułu `MODULE_NAME` ustalaną podczas buildu).
- `fw_version` i `build_id` mogą podczas inicjalizacji mieć wartość `NULL` lub
  wskazywać pusty tekst; w takim przypadku oba przyjmują wartość `unknown`.
  W przeciwnym razie sesja przechowuje przekazane wskaźniki, które muszą
  pozostać ważne przez cały czas jej życia.
- Podczas inicjalizacji `hal_get_device_uid_hex()` pobiera UID urządzenia w zapisie
  szesnastkowym, a sesja przechowuje jego kopię w swojej strukturze.
- Po inicjalizacji dane identyfikacyjne nie mogą się zmienić;
  `hal_serial_session_poll()` nie przyjmuje ich jako argumentów.

Ograniczenie wysyłania odpowiedzi:
- `hal_serial_session_println` nic nie robi poza okresem obsługi żądania
  (`session->in_request == false`). Dzięki temu moduły nie mogą przypadkowo umieścić
  niezamówionych bajtów w strumieniu ramek. Stan asynchroniczny można wysłać z callbacku
  obsługującego nierozpoznaną linię, w odpowiedzi na żądanie.
- `hal_serial_session_println_ex()` zwraca przez `hal_status_t` błędy
  nieprawidłowych argumentów, wywołania poza okresem obsługi żądania, zbyt
  dużego payloadu oraz niedozwolonych znaków ramki.
  `hal_serial_session_current_request_seq()` zwraca aktywne
  `<seq>` wyłącznie podczas obsługi tego samego żądania.

Uwierzytelnianie (Faza 3) - opcjonalne (opt-in):
- Cała obsługa AUTH jest dołączana do buildu wyłącznie po zdefiniowaniu
  `HAL_ENABLE_CRYPTO`. Bez tej flagi struktura sesji nie zawiera pól uwierzytelniania,
  handlery AUTH nie są wywoływane, a
  `hal_serial_session_is_authenticated()` zawsze zwraca `false`. Pozostała
  część sesji ramkowanej działa bez zmian, w tym HELLO i polecenia projektu
  kierowane do handlera nierozpoznanych linii.
- Rzeczywiste tokeny poleceń (`cmd_auth_begin`, `cmd_auth_prove`) pochodzą
  z instancji słownika - Fiesta dostarcza `"SC_AUTH_BEGIN"` /
  `"SC_AUTH_PROVE"`; inny projekt może dostarczyć inne nazwy. Pole tokenu
  równe NULL wyłącza dane polecenie i kieruje wewnętrzną linię do
  handlera nierozpoznanych linii.
- Prymitywy soli i wyprowadzania klucza opisano w [`hal_sc_auth`](#halscauth-pomocnik-uzgadniania-uwierzytelniania-handshake-opt-in-halenablecrypto).
- Handler AUTH_BEGIN wymaga aktywnej sesji, wcześniej potwierdzonej przez HELLO.
  Każde nowe 16-bajtowe wyzwanie pobiera wyłącznie z providera
  `jh_secure_random_bytes()` właściwego dla targetu. Jeśli bezpieczna entropia
  jest niedostępna, handshake kończy się błędem zgodnie z zasadą fail-closed. Poprzedni stan
  uwierzytelnienia i oczekujące wyzwanie są wtedy czyszczone, a jeśli słownik zawiera
  `reply_auth_failed_entropy`, odpowiedź ta zostaje wysłana. Nie ma deterministycznego
  fallback.
- Handler AUTH_PROVE jest jednorazowy na wyzwanie: zarówno sukces, jak i niepowodzenie
  unieważniają oczekujące wyzwanie, więc przechwycona prawidłowa odpowiedź
  nie może zostać ponownie użyta z tym samym wyzwaniem.
- Nowe HELLO generuje nowy `session_id` i czyści `authenticated` /
  `challenge_pending`. Kod modułu chroniący wrażliwe operacje musi
  ponownie sprawdzać `hal_serial_session_is_authenticated(session)` po każdym
  poleceniu, a nie tylko raz.
- `auth_failures` zlicza nieudane próby `SC_AUTH_PROVE`; oparte na tym ograniczanie
  częstotliwości prób i czasowe blokady zaplanowano na Fazę 7.

Konfiguracja słownika (R1.0 + R1.6 + R1.7):
- Wejściowe tokeny poleceń (`cmd_bye`, `cmd_auth_begin`, `cmd_auth_prove`,
  `cmd_reboot_bootloader`) oraz payloady odpowiedzi są zapisane w
  `hal_serial_session_vocabulary_t`. Przekaż wypełnioną instancję do
  `hal_serial_session_init_with_vocabulary()`, aby włączyć obsługę BYE, AUTH i
  REBOOT_BOOTLOADER w preferowanym dialekcie projektu.
- W R1.6 usunięto historyczne wartości domyślne SC_* z JaszczurHAL.
  `hal_serial_session_vocabulary_default` jest teraz pustą strukturą
  zastępczą, której każde pole ma wartość `NULL`. Klasyczne
  `hal_serial_session_init()` nadal działa
  dla sesji obsługujących tylko HELLO: HELLO jest strukturalne i nie jest sterowane
  słownikiem, ale polecenia AUTH i REBOOT przechodzą do
  handlera nierozpoznanych linii, gdy nie podano słownika.
- Wartość `NULL` w danym polu oznacza, że polecenie nie jest rozpoznawane albo
  odpowiedź nie jest wysyłana. Przy częściowej obsłudze AUTH można pozostawić
  pola wybranych poleceń jako `NULL`. Kod pominie odpowiadające im przypadki,
  a pozostała część słownika nadal będzie działać.
- HELLO oraz odpowiedź `OK HELLO module=... proto=... session=... fw=... build=...
  uid=...` są celowo NIEKONFIGUROWALNE: ich struktura jest parsowana przez każdego
  hosta i stanowi część specyfikacji protokołu.
- Ciągi odpowiedzi kończące się na `_fmt` (obecnie tylko `reply_auth_challenge_fmt`)
  są przekazywane do formaterów z rodziny `printf`; nadpisania MUSZĄ zachować
  placeholder `%s` dla bajtów hex wyzwania.
- `reply_auth_failed_entropy` jest dodatkowym polem i opisuje niepowodzenie przed wydaniem
  wyzwania. Istniejące formaty ramek poleceń i odpowiedzi sukcesu pozostają
  niezmienione.

Uwagi:
- parser jest oparty na liniach (`\r` / `\n` kończą ramkę),
- publiczne typy, konfiguracja i deklaracje znajdują się w
  `hal_serial_session.h`; parser, kierowanie żądań i uwierzytelnianie mają
  jedną implementację w `hal_serial_session.cpp`,
- identyfikator sesji jest niekryptograficzny i służy wyłącznie do śledzenia
  na etapie zestawiania sesji,
- bufor wewnętrznego payloadu HELLO mieści sześć obowiązkowych pól i dodatkowy zapas;
  implementacja używa bufora o rozmiarze 192 bajtów.

Typowa konfiguracja modułu firmware z HELLO i poleceniami projektu
przekazywanymi do handlera nierozpoznanych linii, bez AUTH/REBOOT:
```c
#include <hal/serial/hal_serial_session.h>

static hal_serial_session_t s_session;

static void on_unknown(const char *inner, void *user) {
    (void)user;
    if (strcmp(inner, "SC_GET_META") == 0) {
        hal_serial_session_println(&s_session, "SC_OK META ...");
    }
}

void configSessionInit(void) {
    hal_serial_session_init(&s_session, MODULE_NAME, FW_VERSION, BUILD_ID);
    hal_serial_session_set_unknown_handler(&s_session, on_unknown, NULL);
}

void configSessionTick(void) {
    hal_serial_session_poll(&s_session);
}
```

Dla modułów obsługujących AUTH/REBOOT zamień init na
`hal_serial_session_init_with_vocabulary(&s_session, MODULE_NAME,
FW_VERSION, BUILD_ID, &my_vocab)`, gdzie `my_vocab` jest wypełnioną instancją
`hal_serial_session_vocabulary_t` projektu. Zobacz sekcję
"Konfiguracja słownika" poniżej.

Aplikacje udostępniające te payloady przez `hal_command_router` powinny
dołączyć
[`hal_serial_commands`](23_commands.md#adapter-ramkowanej-sesji-szeregowej-framed-serial-session)
zamiast dodawać kolejne drzewo kierowania poleceń w callbacku nierozpoznanych linii.
Bezpośredni callback pozostaje przydatny w małych protokołach oraz jako opcjonalny
fallback adaptera dla danych, które nie są poleceniami.

Weryfikacja w testach (backend mock):
- Zbuduj ramkowane żądanie za pomocą `hal_serial_frame_encode(seq, "HELLO", buf,
  sizeof(buf), NULL)`, dołącz `\n` i wstrzyknij je przez
  `hal_mock_serial_inject_rx(buf, -1)`.
- Odczytaj `hal_mock_serial_last_line()` i zdekoduj wynik za pomocą
  `hal_serial_frame_decode(...)`, aby sprawdzić pola odpowiedzi HELLO
  (`module=`, `proto=`, `session=`, `fw=`, `build=`, `uid=`) oraz to, że
  seq odpowiedzi odpowiada seq żądania.
- Użyj `hal_mock_set_device_uid(...)`, aby symulować inną fizyczną płytkę
  przy sprawdzaniu wartości `uid=`.
- Użyj `hal_mock_secure_random_set_seed(...)` ze statusem `HAL_OK` dla stabilnych
  wektorów wyzwań lub ustaw status błędu, aby przećwiczyć niepowodzenie entropii i
  zweryfikować, że sesja pozostaje nieuwierzytelniona z wyzerowanym wyzwaniem.

### Przykłady

**Przykład: HELLO + niestandardowy handler poleceń**
```c
#include <hal/serial/hal_serial_session.h>
#include <string.h>

static hal_serial_session_t s_session;

static void on_unknown(const char *inner, void *user) {
  (void)user;

  if (strcmp(inner, "SC_GET_STATUS") == 0) {
    hal_serial_session_println(&s_session, "SC_OK STATUS ready=1");
    return;
  }

  hal_serial_session_println(&s_session, "SC_ERR unknown");
}

void sc_init(void) {
  hal_serial_session_init(&s_session, "cfg", "1.2.3", "dev-build");
  hal_serial_session_set_unknown_handler(&s_session, on_unknown, NULL);
}

void sc_tick(void) {
  hal_serial_session_poll(&s_session);
}
```

**Przykład: Włączenie obsługi AUTH / BYE / REBOOT sterowanej słownikiem**
```c
#include <hal/serial/hal_serial_session.h>
#include <hal/serial/hal_serial_session_vocabulary.h>

static const hal_serial_session_vocabulary_t s_vocab = {
  .cmd_bye = "SC_BYE",
  .reply_bye_ok = "SC_OK BYE",
  .cmd_auth_begin = "SC_AUTH_BEGIN",
  .reply_auth_challenge_fmt = "SC_OK AUTH_CHALLENGE %s",
  .cmd_auth_prove = "SC_AUTH_PROVE",
  .reply_auth_ok = "SC_OK AUTH",
  .reply_auth_failed_no_challenge = "SC_ERR AUTH_NO_CHALLENGE",
  .reply_auth_failed_bad_length = "SC_ERR AUTH_BAD_LENGTH",
  .reply_auth_failed_bad_hex = "SC_ERR AUTH_BAD_HEX",
  .reply_auth_failed_key_derivation = "SC_ERR AUTH_KEY_DERIVATION",
  .reply_auth_failed_mac_compute = "SC_ERR AUTH_MAC_COMPUTE",
  .reply_auth_failed_bad_mac = "SC_ERR AUTH_BAD_MAC",
  .reply_auth_failed_entropy = "SC_ERR AUTH_ENTROPY",
  .cmd_reboot_bootloader = "SC_REBOOT_BOOTLOADER",
  .reply_reboot_ok = "SC_OK REBOOT",
  .reply_not_ready_hello_required = "SC_ERR HELLO_REQUIRED",
  .reply_not_authorized = "SC_ERR NOT_AUTHORIZED",
  .reply_unknown_cmd = "SC_ERR unknown",
};

static hal_serial_session_t s_secure_session;

void secure_sc_init(void) {
  hal_serial_session_init_with_vocabulary(&s_secure_session,
                      "boot",
                      "1.2.3",
                      "dev-build",
                      &s_vocab);
}
```

---

## `hal_serial_frame` - pomocnicy ramkowania na łączu

```c
#include <hal/serial/hal_serial_frame.h>

#define HAL_SERIAL_FRAME_PREFIX        "$SC,"
#define HAL_SERIAL_FRAME_PREFIX_LEN    4u
#define HAL_SERIAL_FRAME_PAYLOAD_MAX   256u
#define HAL_SERIAL_FRAME_LINE_MAX      (HAL_SERIAL_FRAME_PAYLOAD_MAX + 32u)

uint8_t hal_serial_frame_crc8(const uint8_t *data, size_t len);

bool    hal_serial_frame_encode(uint16_t seq,
                                const char *payload,
                                char *out, size_t out_size,
                                size_t *out_len);

bool    hal_serial_frame_decode(const char *line,
                                uint16_t *seq_out,
                                char *payload_out,
                                size_t payload_out_size);
```

Format ramki (w obu kierunkach):

    $SC,<seq>,<payload>*<crc8>\n

- Stały znacznik początku ramki `$SC,`.
- `<seq>`: dziesiętna liczba bez znaku zapisana w ASCII, z zakresu `[0..65535]`.
  Odpowiedź powtarza `seq` żądania, aby host mógł je ze sobą powiązać.
- `<payload>`: dowolny tekst ASCII. Nie może zawierać `*`, `\r` ani `\n`.
- `<crc8>`: dwie wielkie cyfry szesnastkowe. CRC-8/CCITT (wielomian `0x07`,
  wartość początkowa `0x00`, bez refleksji, bez xor-out) obliczane dla bajtów
  między początkowym `$` a separatorem `*`, z pominięciem obu tych znaków.
  Wektor referencyjny:
  `"123456789" -> 0xF4`.
- Terminator linii `\n` (pomocnicy kodujący **nie** dołączają go; użyj funkcji
  `hal_serial_println()`, która już to robi).

Firmware i towarzyszące narzędzia hostowe powinny dołączać ten przenośny nagłówek C
bezpośrednio z JaszczurHAL. Nie utrzymuj innego kodeka ramki ani skopiowanych
stałych. Testy po obu stronach powinny sprawdzać ten sam wektor referencyjny CRC.

### Przykłady

**Przykład: Kodowanie ramkowanego żądania**
```c
#include <hal/serial/hal_serial_frame.h>
#include <hal/serial/hal_serial.h>

void send_hello(void) {
  char line[HAL_SERIAL_FRAME_LINE_MAX];
  size_t line_len = 0;

  if (hal_serial_frame_encode(7, "HELLO", line, sizeof(line), &line_len)) {
    hal_serial_println(line);
  }
}
```

**Przykład: Dekodowanie odebranej ramki**
```c
#include <hal/serial/hal_serial_frame.h>
#include <hal/serial/hal_serial.h>

void inspect_line(const char *line) {
  uint16_t seq = 0;
  char payload[HAL_SERIAL_FRAME_PAYLOAD_MAX + 1u];

  if (hal_serial_frame_decode(line, &seq, payload, sizeof(payload))) {
    hal_deb("frame seq=%u payload=%s", (unsigned)seq, payload);
  } else {
    hal_derr("invalid frame: %s", line);
  }
}
```

---

## `hal_sc_auth` - pomocnik uzgadniania uwierzytelniania (handshake)  *(opt-in - `HAL_ENABLE_CRYPTO`)*

Moduł jest włączany przez tę samą flagę `HAL_ENABLE_CRYPTO` co `hal_crypto`.
Zależy od `hal_hmac_sha256`, dlatego włączenie uwierzytelniania bez obsługi
kryptografii nie jest poprawną konfiguracją. Po wyłączeniu flagi
`hal_serial_session` nadal działa, ale handlery AUTH i REBOOT nie są
dołączane do buildu. Odpowiadające im tokeny poleceń nie zostaną wtedy rozpoznane
niezależnie od zawartości słownika, a
`hal_serial_session_is_authenticated()` zwróci `false`.

```c
#include <hal/security/hal_sc_auth.h>

#define HAL_SC_AUTH_SCHEME_TAG          "FIESTA-SC-AUTH-v1"
#define HAL_SC_AUTH_SCHEME_TAG_LEN      17u
#define HAL_SC_AUTH_SALT                ((const uint8_t *)HAL_SC_AUTH_SCHEME_TAG)
#define HAL_SC_AUTH_SALT_LEN            HAL_SC_AUTH_SCHEME_TAG_LEN
#define HAL_SC_AUTH_KEY_BYTES           HAL_SHA256_DIGEST_BYTES   // 32
#define HAL_SC_AUTH_CHALLENGE_BYTES     16u
#define HAL_SC_AUTH_CHALLENGE_HEX_BUF_SIZE  33u                   // 32 znaki hex + NUL
#define HAL_SC_AUTH_RESPONSE_BYTES      HAL_SHA256_DIGEST_BYTES   // 32
#define HAL_SC_AUTH_RESPONSE_HEX_BUF_SIZE   HAL_SHA256_HEX_BUF_SIZE

bool hal_sc_auth_derive_device_key(
    const uint8_t *uid, size_t uid_len,
    uint8_t out_key[HAL_SC_AUTH_KEY_BYTES]);

bool hal_sc_auth_compute_response(
    const uint8_t device_key[HAL_SC_AUTH_KEY_BYTES],
    const uint8_t *challenge, size_t challenge_len,
    uint32_t session_id,
    uint8_t out_response[HAL_SC_AUTH_RESPONSE_BYTES]);

bool hal_sc_auth_macs_equal(const uint8_t *a, const uint8_t *b, size_t len);
```

Konstrukcje:

- `K_device  = HMAC-SHA256(key=salt, message=uid_bytes)`
- `response  = HMAC-SHA256(key=K_device, message=challenge || session_id_be32)`

`hal_u32_to_bytes_be` zapisuje identyfikator sesji w kolejności big-endian.
Dzięki temu firmware i host obliczają MAC z dokładnie tej samej sekwencji
bajtów, niezależnie od własnej kolejności bajtów.

`hal_sc_auth_macs_equal` korzysta ze wspólnej wewnętrznej funkcji
`jh_constant_time_compare`. Przed powrotem z funkcji `jh_secure_zeroize`
zeruje bufory komunikatów uwierzytelniania oraz bufory wyjściowe po błędzie.

Sól jest publiczną stałą ustalaną podczas buildu i wspólną dla całego
projektu. Bezpieczeństwo schematu opiera się na połączeniu HMAC-SHA256
z unikatowym UID urządzenia, **nie** na poufności soli. Traktowanie jej jako
sekretu jedynie zaciemniałoby założenia projektu.

Jeśli stos hosta zawiera odpowiednik tego helpera, obie implementacje muszą
pozostać zgodne. Po obu stronach należy testować wektory wyprowadzania klucza
oraz MAC odpowiedzi. Ich porównanie pozwala wcześnie wykryć rozbieżności
i uniknąć błędów AUTH_FAILED podczas integracji w runtime.

Sam handshake jest obsługiwany przez
[`hal_serial_session`](#halserialsession-pomocnik-ramkowanej-sesji-szeregowej)
za pośrednictwem pól `cmd_auth_begin` / `cmd_auth_prove` słownika
(Fiesta nazywa je `SC_AUTH_BEGIN` / `SC_AUTH_PROVE`). Moduły odczytują
stan uwierzytelnienia przez `hal_serial_session_is_authenticated(...)`
i nie muszą wywoływać poniższych pomocników bezpośrednio.

## Przykłady

**Przykład: Wyprowadzenie klucza urządzenia i obliczenie odpowiedzi AUTH**
```c
#include <hal/security/hal_sc_auth.h>

bool build_auth_response(const uint8_t *uid,
             size_t uid_len,
             const uint8_t challenge[HAL_SC_AUTH_CHALLENGE_BYTES],
             uint32_t session_id,
             uint8_t out_mac[HAL_SC_AUTH_RESPONSE_BYTES]) {
  uint8_t device_key[HAL_SC_AUTH_KEY_BYTES];

  if (!hal_sc_auth_derive_device_key(uid, uid_len, device_key)) {
    return false;
  }

  return hal_sc_auth_compute_response(device_key,
                    challenge,
                    HAL_SC_AUTH_CHALLENGE_BYTES,
                    session_id,
                    out_mac);
}
```

**Przykład: Weryfikacja odpowiedzi hosta w stałym czasie**
```c
#include <hal/security/hal_sc_auth.h>

bool auth_response_matches(const uint8_t expected[HAL_SC_AUTH_RESPONSE_BYTES],
               const uint8_t actual[HAL_SC_AUTH_RESPONSE_BYTES]) {
  return hal_sc_auth_macs_equal(expected,
                  actual,
                  HAL_SC_AUTH_RESPONSE_BYTES);
}
```

---


---

*Dalej: [Magistrale komunikacyjne](09_buses.md)*
