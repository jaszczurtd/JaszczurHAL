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

**impl/rp2040:** `mutex_t` z pico SDK w normalnych buildach RP2040; mutex FreeRTOS (`xSemaphoreCreateMutex`) w buildach `HAL_ENABLE_FREERTOS + __FREERTOS`. Oba warianty są nierekurencyjne i synchronizują wywołujących z zadań core0/core1.
**impl/stm32g474:** jednordzeniowa atomowa spinlock w buildach bez FreeRTOS; mutex FreeRTOS (`xSemaphoreCreateMutex`) w buildach `HAL_ENABLE_FREERTOS`. Oba warianty są nierekurencyjne.
**impl/esp32:** mutex FreeRTOS z ESP-IDF (`xSemaphoreCreateMutex`), nierekurencyjny
i wyłącznie w kontekście zadania.
**impl/.mock:** `std::mutex`.
**Uwaga o FreeRTOS:** `hal_mutex_*` jest świadome FreeRTOS na RP2040/RP2350 i
STM32G474, gdy `HAL_ENABLE_FREERTOS` wybiera przypięty kernel, oraz na ESP32-S3
poprzez scheduler dostarczany przez przypięty ESP-IDF.
`hal_mutex_*` pozostaje wyłącznie funkcją kontekstu zadania; nie jest to API dla ISR.
Mutexy modułów singletonowych/magistrali używają wewnętrznego atomowego pomocnika
create-once tam, gdzie nadal potrzebny jest defensywny, lazy fallback.
`hal_mutex_try_lock()` nigdy nie czeka. Mogą go używać bare-metalowe procedury obsługi
przerwań; backendy FreeRTOS zwracają `false`, gdy zostanie wywołana z kontekstu przerwania.

### Makra (tools.h)

```c
m_mutex_def(name)            // static hal_mutex_t name = NULL
m_mutex_init(name)           // name = hal_mutex_create()
m_mutex_enter_blocking(name) // hal_mutex_lock(name)
m_mutex_exit(name)           // hal_mutex_unlock(name)
```

### Sekcja krytyczna (twarda sekcja przerwań danego targetu)

```c
void hal_critical_section_enter(void);  // zapisz i wyłącz przerwania
void hal_critical_section_exit(void);   // przywróć poprzedni stan przerwań
```

**impl/rp2040:** bezpieczne przy zagnieżdżaniu, per-rdzeniowe `save_and_disable_interrupts()` /
`restore_interrupts()` (pico SDK), łącznie z buildami FreeRTOS.
**impl/stm32g474:** bezpieczna przy zagnieżdżaniu pełna maska przerwań PRIMASK, łącznie z
buildami FreeRTOS.
**impl/esp32:** bezpieczna przy zagnieżdżaniu sekcja krytyczna `portMUX_TYPE` z ESP-IDF,
współdzielona między obydwoma rdzeniami, ze śledzeniem głębokości per-rdzeń.
**impl/.mock:** operacje puste (no-op).
**Uwaga:** To wykorzystuje twardy mechanizm sekcji krytycznej przerwań danego targetu, do
krótkich sekcji wrażliwych na czas lub współdzielonych z ISR. ESP32-S3 dodatkowo serializuje
oba rdzenie za pomocą swojego współdzielonego portMUX; RP2040 maskuje wyłącznie
wywołujący rdzeń. Nie jest to blokada schedulera FreeRTOS; do wzajemnego wykluczania
zadań użyj `hal_mutex_t`.

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

Natywny backend RP jest jedynym właścicielem TinyUSB. Dostarcza deskryptory CDC,
tożsamość seryjną, chronioną mutexem pompę pierwszoplanową (foreground pump), tło pompy
oparte na IRQ/timerze o niskim priorytecie, ograniczone przeciwciśnienie (backpressure)
transmisji oraz reset do BOOTSEL wyzwalany DTR przy 1200 bps. `hal_usb_init()`
publikuje możliwość (capability) płytki dotyczącą runtime USB.

`hal_usb_set_bootloader_reset_hook()` rejestruje opcjonalnego obserwatora wywoływanego
bezpośrednio przed resetem do bootloadera (zarówno przy wyzwalaczu DTR 1200 bps, jak i przy
`hal_usb_reset_to_bootloader()`). Jest przeznaczony do księgowania zamykania aplikacji oraz
testów host/mock; hook nie może blokować. Przekazanie `NULL` czyści rejestrację.

STM32G474 obecnie zwraca `HAL_EUNSUPPORTED`; mock hosta dostarcza deterministyczne
bufory RX/TX oraz obserwację resetu na potrzeby testów jednostkowych.
ESP32-S3 nie udostępnia tego publicznego cyklu życia USB. Jego konsola diagnostyczna
korzysta z opisanego poniżej VFS USB Serial/JTAG należącego do startupu.

---

## `hal_serial` - wyjście szeregowe i debugowanie

```c
#include <hal/serial/hal_serial.h>

// Konfigurowalne pokrętła rozmiaru debug (zdefiniuj przed dołączeniem, jeśli potrzeba):
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
`hal_derr_limited()` nie budują już całej sformatowanej linii logu w buforze o stałym
rozmiarze `HAL_DEBUG_BUF_SIZE`. Współdzielony rdzeń serial/debug strumieniuje wyjście
bezpośrednio do chronionego mutexem writera transportu:

- fragmenty literalne i ładunki `%s` są emitowane w kawałkach bez buforu roboczego dla
  całej linii
- konwersje liczbowe, zmiennoprzecinkowe i wskaźnikowe używają małego lokalnego bufora na
  konwersję, z tymczasowym fallbackiem o dokładnym rozmiarze, tylko gdy pojedyncza
  konwersja się nie mieści
- prefiksy (`hal_deb_set_prefix()`, `ERROR!`, znaczniki czasu i tagi źródła limitu
  szybkości) są emitowane jako oddzielne fragmenty strumienia pod tym samym mutexem TX,
  więc logiczna linia logu nadal nie może przeplatać się z innym emiterem serial

`HAL_DEBUG_BUF_SIZE` nie jest więc już limitem długości logu zadania. Pozostaje
pokrętłem rozmiaru zachowanym dla zgodności wstecznej z pomocnikami przechwytywania/RX
mocka. Rekordy odroczone z ISR są celowo nadal ograniczone przez `HAL_DEBUG_ISR_TEXT_MAX`,
ponieważ ścieżka ISR nie może alokować, blokować ani dotykać transportu serial.

### Logowanie debug odroczone z ISR

`hal_deb()`, `hal_derr()` i `hal_derr_limited()` mogą być teraz wywoływane z kontekstu
przerwania, ale mimo to - **należy tego unikać**. Wykrywają kontekst ISR poprzez
`hal_in_isr()` i na ścieżce krytycznej pod względem czasu **nie** przejmują mutexu,
**nie** wykonują leniwej inicjalizacji, **nie** wywołują hooka znacznika czasu, **nie**
przeszukują tabeli limitera szybkości i **nie** wykonują we/wy UART.
Sformatowany ładunek jest umieszczany w kolejce współdzielonego rdzenia - pierścieniu
jednego producenta/jednego konsumenta (single-producer / single-consumer, SPSC), bez
blokad (lock-free) (`HAL_DEBUG_ISR_SLOT_COUNT`
slotów × `HAL_DEBUG_ISR_TEXT_MAX` bajtów każdy, domyślnie 64 × 160 B), z użyciem
atomików release/acquire. Dla `hal_derr_limited()` tag `[source]` jest wypiekany
w kolejkowanym tekście z góry, ponieważ globalny limiter szybkości jest pomijany w
kontekście ISR.

`hal_debug_loop()` opróżnia pierścień z kontekstu zadania i emituje każdy rekord,
używając normalnej, chronionej mutexem ścieżki serial. Każda opróżniona linia jest
opatrzona adnotacją `[ISR ts=<micros>]` (oryginalny czas zdarzenia, nie "teraz") i
respektuje bieżący `hal_deb_set_prefix()` dla rekordów debug oraz standardowy znacznik
`ERROR! ` dla rekordów błędów. Gdy producent przepełni pierścień, zwiększa wewnętrzny
licznik, a następne opróżnienie emituje jedną linię podsumowującą
`"ERROR! [ISR] dropped N debug message(s)"` przed wyzerowaniem licznika. Gdy aktywne jest
`hal_debug_set_muted(true)`, zarówno producent (po cichu odrzuca, bez zapisu do pierścienia,
bez zwiększania licznika odrzuceń), jak i konsument (odrzuca oczekujące rekordy i zeruje
licznik odrzuceń) zachowują się jako operacje puste (no-op).

`hal_debug_loop()` jest **bezpieczne do wywołania od samej pierwszej iteracji**
pętli głównej, nawet gdy nie wywołano jeszcze `hal_debug_init()` / `hal_deb()` /
`hal_derr()`: ścieżka emisji wykonuje tę samą leniwą inicjalizację co `hal_deb()`, a
skróty (short-circuit) dla stanu w-ISR / wyciszonego używają wyłącznie statycznych
zmiennych zainicjalizowanych zerami. Wywołanie jej z kontekstu ISR samo w sobie jest
operacją pustą (zapobiega ponownemu wejściu w opróżnianie przez leżący u podstaw mutex UART).

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

`hal_deb()` i `hal_derr()` używają **leniwej inicjalizacji** - jeśli `hal_debug_init()`
nie zostało wywołane przed pierwszym wydrukiem debug, jest ono wywoływane automatycznie z
`HAL_DEBUG_DEFAULT_BAUD` (domyślnie 9600, nadpisywalne przez `-D`). Ścieżka leniwej
inicjalizacji i publikacja mutexu singletonowego są atomowo bramkowane na RP2040/RP2350,
STM32G474, ESP32-S3 i mocku, więc dwa zadania lub rdzenie nie resetują jednocześnie stanu
debug ani nie wyciekają konkurujących alokacji mutexu. Wywołanie `debugInit()` nie jest już
obowiązkowe.

`hal_derr_limited()` wykorzystuje ponownie tę samą leniwą inicjalizację i stosuje globalną
konfigurację limitu szybkości per tag źródła błędu (`source`), tak aby błędy z różnych
modułów nie tłumiły się nawzajem.

### Serializacja TX (R1.8)

`hal_serial_print()` i `hal_serial_println()` przejmują wspólny globalny mutex TX
współdzielonego rdzenia wokół leżącej u podstaw ścieżki zapisu konsoli debug. Dowiązany
port RP zapisuje przez CDC `hal_usb`, natomiast ESP32-S3, STM32 i mock wybierają swoje
odpowiednie porty transportowe. Serializuje to każdy emiter docierający do łącza - pomocnicy
debug (`hal_deb`, `hal_derr`,
`hal_derr_limited`), pomocnik ramkowanej sesji
(`hal_serial_session_println`) oraz każdy bezpośredni wywołujący - względem siebie
nawzajem.

Bez tej blokady, na dwurdzeniowym RP2040 `hal_deb` z rdzenia 1 mógłby przeplatać swoje
bajty w środku ramki z odpowiedzią sesji emitowaną przez rdzeń 0, powodując pojedyncze
zrzuty bajtów CDC, które łamały sumy CRC `$SC,...*<crc>`
i zmuszały hosta do ponawiania każdego polecenia. Mutexy per-funkcja
(`s_deb_mutex`, `s_derr_mutex`) serializują stan pomocników debug, ale
same z siebie nie powstrzymują niepowiązanych wywołujących przed ściganiem się o same
zapisy transportu.

Mutex TX jest tworzony przez ten sam atomowy pomocnik create-once, który jest używany
przez inne singletonowe blokady modułów (lub gorliwie w `hal_debug_init()`, gdy wywołane
jawnie), więc wywołujący, którzy emitują podczas bardzo wczesnego rozruchu, nadal widzą
prawidłową blokadę. Jest on ściśle
zagnieżdżony **wewnątrz** `s_deb_mutex` / `s_derr_mutex` / `s_rl_mutex`, nigdy
odwrotnie, więc deadlock jest niemożliwe.

Na backendach RP USB-CDC okno mutexu może dodatkowo obejmować dodatkowe
`hal_usb_cdc_flush()` po każdym `hal_serial_print()` /
`hal_serial_println()`. Jest to domyślnie wyłączone i można to zmienić w czasie
działania za pomocą `hal_serial_set_flush(bool enabled)`. Pętla zapisu RP2040 nadal
kopie FIFO CDC wewnętrznie, więc krótkie pakiety są faktycznie wysyłane; opcjonalny flush
to pokrętło zgodności dla aplikacji, które chcą dodatkowego odpytania transportu przed
zwolnieniem mutexu TX.

Pozostawienie `hal_serial_set_flush(false)` utrzymuje backend RP na jego domyślnej
ścieżce i unika opcjonalnego dodatkowego odpytania/flush, zachowując przy tym mutex TX.
Nie omija to ograniczonych ponowień pętli zapisu, gdy FIFO CDC jest pełne.
Na ESP32-S3 opcja ta mapuje się na `fsync(stdout)` na konsoli VFS startupu. Na
STM32G474 włączenie jej czeka na flagę zakończenia transmisji USART2 po każdym
komunikacie. Jest to przydatne przed zmianą zegara peryferiów przez STOP lub gdy aplikacja
wyłącza konsolę. Backend mock akceptuje ustawianie tej opcji bez semantyki czasowej targetu.

### Współdzielony rdzeń i porty transportowe dowiązywane w czasie linkowania

`src/hal/serial/hal_serial.cpp` jest jedynym rdzeniem serial/debug. Jest właścicielem
publicznych punktów wejścia serial i debug, strumieniowego formatowania, prefiksów,
hooków znacznika czasu, stanu wyciszenia, slotów limitu szybkości, pierścienia SPSC ISR,
mirroringu na konsolę sieciową, leniwej inicjalizacji oraz wszystkich wspólnych mutexów.
Wewnętrzny interfejs `jh_serial_port.h` jest rozwiązywany w czasie linkowania i celowo
udostępnia wyłącznie operacje transportowe: begin/konfigurację, granicę logicznego
komunikatu, zapis bajtu, docelowe zakończenie linii/flush oraz odczyt bajtu (RX).

Porty produkcyjne są celowo niewielkie:

- `impl/rp2040/hal_serial.cpp` jest właścicielem begin/TX/RX CDC USB oraz opcjonalnego flush;
- `impl/esp32/hal_serial.cpp` ponownie wykorzystuje należącą do startupu ESP-IDF konsolę
  VFS USB Serial/JTAG, adaptuje oficjalny buforowany `usb_serial_jtag_driver` lub instaluje
  ten pojedynczy driver, jeśli jest nieobecny, i nigdy nie rejestruje drugiego właściciela
  VFS. Argument baud jest informacyjny, RX jest nieblokujące z 256-bajtowym buforem HAL, a
  opcjonalny flush mapuje się na `fsync(stdout)`;
- `impl/stm32g474/hal_serial.cpp` jest właścicielem USART2 na sprzęcie, stdout hosta dla
  buildów sanity dla targetu (target sanity builds), oraz obecnie nieobsługiwanego wyniku RX;
- `impl/.mock/hal_serial.cpp` jest właścicielem deterministycznego przechwytywania ostatniego
  komunikatu, obserwacji stdout oraz wstrzykiwalnego binarnego RX.

Zakończenia linii pozostają specyficzne dla transportu: sprzęt RP i STM32 emituje `\r\n`,
podczas gdy ESP32-S3, STM32 w stylu hosta oraz wyjście mocka używają `\n`. Przechwytywanie
mocka celowo przechowuje komunikat bez jego zakończenia linii, zgodnie z historycznym API
testów.
Ścieżka asercji RP używa tego samego surowego transportu i mirroringu na konsolę sieciową,
ale nie przejmuje mutexu TX, więc wyjście błędu krytycznego (fault) nie może zablokować się
na blokadzie przetrzymywanej przez kontekst, który zawiódł.

Szczegóły implementacji limitera:
- dopasowanie źródła używa `hash + ciąg source` (wyszukiwanie odporne na kolizje)
- stan limitera jest chroniony wewnętrznym mutexem (thread-safe)
- gdy `HAL_DEBUG_RATE_LIMIT_SOURCES_MAX` zostanie wyczerpane, nowe źródła są grupowane w
    wewnętrznym koszyku `overflow` zamiast ponownego wykorzystania niepowiązanego stanu źródła

**Pomocnicy debug w `tools.h` / `tools_c.h`:**
```c
void  debugInit(void);                          // wrapper wokół hal_debug_init(HAL_DEBUG_DEFAULT_BAUD)
void  setDebugPrefixWithColon(const char *moduleName); // dopisuje ':' i przekazuje do hal_deb_set_prefix()

#define deb            hal_deb
#define derr           hal_derr
#define derr_limited   hal_derr_limited
#define setDebugPrefix hal_deb_set_prefix
```

`setDebugPrefixWithColon(...)` w razie potrzeby przycina nazwę modułu, tak aby
wygenerowany prefiks `<module>:` zawsze mieścił się w `HAL_DEBUG_PREFIX_SIZE`.

Architektura i zachowanie współbieżności są pokryte przez
`test_serial_architecture`, `test_hal_serial` oraz test runtime
FreeRTOS POSIX. Zapobiegają one powrotowi lokalnych dla targetu rdzeni debug i sprawdzają
leniwą publikację mutexu, kompletne granice komunikatów, podsumowania FIFO/przepełnień ISR,
zachowanie wyciszenia, docelowe granice linii oraz binarne RX mocka.

### Polityka obsługi błędów

- `HAL_ASSERT(...)` jest używane dla krytycznych błędów programistycznych w podstawowych
    prymitywach (np. NULL mutex na ścieżkach synchronizacji).
- Miękka walidacja i log błędu są używane dla niekrytycznego nadużycia w runtime
    w API peryferiów, gdzie kontynuacja wykonania jest akceptowalna.
- `hal_derr(...)` drukuje każdy błąd (bez tłumienia).
- `hal_derr_limited(source, ...)` powinno być preferowane dla potencjalnie powtarzalnych
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

Wbudowane polecenie (zawsze rozpoznawane, strukturalne):
- `HELLO` - aktywuje sesję, generuje świeży `session_id` i emituje odpowiedź tożsamościową.

Odpowiedź HELLO jest jedyną strukturalnie stałą odpowiedzią (jej kształt
`module=... proto=... session=... fw=... build=... uid=...` jest parsowany przez każdego
hosta):

    OK HELLO module=<name> proto=1 session=<id> fw=<ver> build=<id> uid=<hex>

Polecenia sterowane słownikiem (R1.0 + R1.6 + R1.7):
- `cmd_bye` - zamyka ramkowaną sesję. Odpowiada `reply_bye_ok` (gdy
  ustawione), czyści `active` i zeruje stan uwierzytelnienia (gdy CRYPTO). Zawsze się
  udaje; nieaktywna sesja po prostu powtarza odpowiedź OK. BYE żyje
  poza `HAL_ENABLE_CRYPTO`, dzięki czemu każda sesja może zostać zamknięta czysto,
  niezależnie od tego, czy ścieżka AUTH jest wkompilowana.
- `cmd_auth_begin` - generuje świeże 16-bajtowe wyzwanie dla aktywnej sesji;
  pomocnik formatuje wyzwanie przez `reply_auth_challenge_fmt` (musi
  zawierać `%s` dla bajtów hex).
- `cmd_auth_prove <64 znaki hex>` - dowodzi, że host zna `K_device` dla
  tego UID. Wyniki przechodzą przez tokeny odpowiedzi słownika
  (`reply_auth_ok` przy sukcesie, jeden z rodziny `reply_auth_failed_*` przy
  niepowodzeniu, `reply_not_ready_hello_required`, jeśli HELLO nie zostało jeszcze widziane).
- `cmd_reboot_bootloader` - bramkowane uwierzytelnieniem. Pomyślna ścieżka emituje
  `reply_reboot_ok`, opróżnia się przez ~50 ms, a następnie skacze do ROM-u rozruchowego
  (tryb pamięci masowej BOOTSEL/UF2); niezuwierzytelniona ścieżka emituje
  `reply_not_authorized`.

Od R1.6 te tokeny NIE są zapisane na sztywno w JaszczurHAL. Pochodzą z
instancji `hal_serial_session_vocabulary_t` przekazanej przez projekt do
`hal_serial_session_init_with_vocabulary`. Każde pole pozostawione NULL (lub dowolna
sesja zainicjalizowana klasycznym `hal_serial_session_init`) sprawia, że
odpowiednie polecenie jest nierozpoznawane - wewnętrzny ładunek przechodzi
do handlera nieznanej linii. Dialekt Fiesta znajduje się w
`Fiesta/src/common/scDefinitions/sc_session_vocabulary.h`
(`fiesta_default_vocabulary`); zobacz poniższą sekcję o konfiguracji słownika.

Nierozpoznane wewnętrzne ładunki:
- jeśli callback użytkownika jest zarejestrowany przez
  `hal_serial_session_set_unknown_handler`, otrzymuje on rozpakowaną
  wewnętrzną linię i jest odpowiedzialny za dowolną odpowiedź (użyj
  `hal_serial_session_println`, aby odpowiedź odziedziczyła `<seq>` żądania).
- `hal_serial_session_attach_unknown_handler()` zajmuje pusty slot callbacku
  bez zastępowania callbacku projektu. Jej odpowiadająca funkcja detach czyści
  wyłącznie tę samą parę callback/user. Adaptery transportu używają tych funkcji
  zwracających status jako pierwszy element do bezpiecznego przejmowania własności.
- w przeciwnym razie pomocnik emituje `reply_unknown_cmd` słownika (nadal
  ramkowane). Przy klasycznej inicjalizacji to pole jest NULL, więc nieznana linia
  jest po cichu porzucana - zarejestruj callback, aby ją obserwować.

Wejście nieramkowane jest po cichu porzucane - nie ma zapasowej ścieżki zwykłego
tekstu. Jest to celowe: narzędzia hostowe mają ramkować żądania, a usunięcie
starszej ścieżki eliminuje niedopasowania podciągów względem linii logu debug.

Model wiązania tożsamości:
- `module_tag` nie może być NULL i musi wskazywać na ciąg o statycznym
  czasie życia (zwykle stałą modułu `MODULE_NAME` ustalaną podczas buildu).
- `fw_version` i `build_id` mogą być NULL lub puste przy init; w takim przypadku oba są
  raportowane jako `unknown`. Gdy nie-NULL, są przechwytywane przez wskaźnik i
  podobnie muszą pozostać ważne przez cały czas życia sesji.
- Szesnastkowy ciąg UID urządzenia jest przechwytywany przez wartość przy init za pomocą
  `hal_get_device_uid_hex()` i przechowywany wewnątrz struktury sesji.
- Cała tożsamość jest niezmienna po init; `hal_serial_session_poll()` nie przyjmuje
  argumentów tożsamości.

Bramkowanie odpowiedzi:
- `hal_serial_session_println` jest operacją pustą poza oknem dispatchu żądania
  (`session->in_request == false`). Moduły nie mogą przypadkowo
  wstrzyknąć niezamówionych bajtów do ramkowanego strumienia; jeśli potrzebujesz wysłać
  stan asynchronicznie, zrób to z callbacku handlera nieznanej linii w
  odpowiedzi na żądanie.
- `hal_serial_session_println_ex()` raportuje nieprawidłowe argumenty, nieaktywne
  okna dispatchu, zbyt duże ładunki oraz zabronione znaki ramki poprzez
  `hal_status_t`. `hal_serial_session_current_request_seq()` zwraca aktywne
  `<seq>` wyłącznie w tym samym oknie dispatchu.

Uwierzytelnianie (Faza 3) - opcjonalne (opt-in):
- Cała ścieżka AUTH jest wkompilowana wyłącznie, gdy zdefiniowane jest
  `HAL_ENABLE_CRYPTO`. Bez tego struktura sesji traci pola auth,
  handlery AUTH nie są wywoływane, a
  `hal_serial_session_is_authenticated()` zawsze zwraca false. Reszta
  ramkowanej sesji (HELLO + polecenia zdefiniowane przez projekt kierowane
  przez handler nieznanej linii) pozostaje niezmieniona.
- Rzeczywiste tokeny poleceń (`cmd_auth_begin`, `cmd_auth_prove`) pochodzą
  z instancji słownika - Fiesta dostarcza `"SC_AUTH_BEGIN"` /
  `"SC_AUTH_PROVE"`; inny projekt może dostarczyć inne nazwy. Pole tokenu
  równe NULL wyłącza dane polecenie i kieruje wewnętrzną linię do
  handlera nieznanego.
- Prymitywy soli i wyprowadzania klucza opisano w [`hal_sc_auth`](#halscauth-pomocnik-uzgadniania-uwierzytelniania-handshake-opt-in-halenablecrypto).
- Handler AUTH_BEGIN wymaga aktywnej (potwierdzonej przez HELLO) sesji
  i pobiera każde świeże 16-bajtowe wyzwanie wyłącznie z providera
  `jh_secure_random_bytes()` targetu. Jeśli bezpieczna entropia jest niedostępna,
  uzgadnianie (handshake) zawodzi w sposób zamknięty (fail closed), wszelki poprzedni
  uwierzytelniony stan i oczekujące wyzwanie są czyszczone, a `reply_auth_failed_entropy`
  jest emitowane, gdy słownik je dostarcza. Nie ma deterministycznego
  fallbacku.
- Handler AUTH_PROVE jest jednorazowy na wyzwanie: zarówno sukces, jak i niepowodzenie
  unieważniają oczekujące wyzwanie, więc przechwycona prawidłowa odpowiedź
  nie może zostać odtworzona (replay) przeciwko temu samemu wyzwaniu.
- Nowe HELLO generuje nowy `session_id` i czyści `authenticated` /
  `challenge_pending`. Kod modułu chroniący wrażliwe operacje musi
  ponownie sprawdzać `hal_serial_session_is_authenticated(session)` po każdym
  poleceniu, a nie tylko raz.
- `auth_failures` liczy nieudane próby `SC_AUTH_PROVE`; polityki ograniczania
  szybkości i blokady na tym oparte są odłożone do Fazy 7.

Konfiguracja słownika (R1.0 + R1.6 + R1.7):
- Wejściowe tokeny poleceń (`cmd_bye`, `cmd_auth_begin`, `cmd_auth_prove`,
  `cmd_reboot_bootloader`) oraz wyjściowe ładunki odpowiedzi są przechwytywane przez
  `hal_serial_session_vocabulary_t`. Przekaż wypełnioną instancję do
  `hal_serial_session_init_with_vocabulary()`, aby włączyć obsługę BYE, AUTH i
  REBOOT_BOOTLOADER w preferowanym dialekcie projektu.
- R1.6 usunęło historyczne wartości domyślne SC_* z JaszczurHAL.
  `hal_serial_session_vocabulary_default` jest teraz pustym placeholderem
  (każde pole NULL). Klasyczne `hal_serial_session_init()` nadal działa
  dla sesji tylko-HELLO: HELLO jest strukturalne i nie jest sterowane
  słownikiem, ale polecenia AUTH i REBOOT przechodzą do
  handlera nieznanej linii, gdy słownik nie jest dostarczony.
- NULL w danym polu oznacza "to polecenie nie jest rozpoznawane" / "ta odpowiedź
  nie jest emitowana". Wywołujący, którzy chcą częściowej obsługi AUTH, mogą pozostawić
  pola poleceń jako NULL; pomocnik pominie te gałęzie, zachowując
  resztę dialektu nienaruszoną.
- HELLO oraz odpowiedź `OK HELLO module=... proto=... session=... fw=... build=...
  uid=...` są celowo NIEKONFIGUROWALNE: ich struktura jest parsowana przez każdego
  hosta i jest traktowana jako część specyfikacji protokołu.
- Ciągi odpowiedzi kończące się na `_fmt` (obecnie tylko `reply_auth_challenge_fmt`)
  są przekazywane do formaterów z rodziny `printf`; nadpisania MUSZĄ zachować
  placeholder `%s` dla bajtów hex wyzwania.
- `reply_auth_failed_entropy` jest dodatkowym polem i opisuje niepowodzenie przed wydaniem
  wyzwania. Istniejące formaty ramek poleceń i odpowiedzi sukcesu pozostają
  niezmienione.

Uwagi:
- parser jest oparty na liniach (`\r` / `\n` kończą ramkę),
- publiczne typy, konfiguracja i deklaracje żyją w
  `hal_serial_session.h`; parsowanie, dispatch i uwierzytelnianie są kompilowane
  raz w `hal_serial_session.cpp`,
- identyfikator sesji jest niekryptograficzny i przeznaczony wyłącznie do śledzenia
  bootstrapu,
- bufor wewnętrznego ładunku HELLO jest rozmiarowany dla sześciu obowiązkowych pól plus
  rozsądny margines; implementacja używa wewnętrznego bufora 192-bajtowego.

Typowe okablowanie (moduł firmware, HELLO + polecenia specyficzne dla projektu przez
handler nieznanej linii - bez AUTH/REBOOT):
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

Aplikacje udostępniające te ładunki przez `hal_command_router` powinny
dołączyć [`hal_serial_commands`](23_commands.md#adapter-ramkowanej-sesji-szeregowej-framed-serial-session)
zamiast dodawać kolejne drzewo dispatchu nazw poleceń do callbacku nieznanego.
Bezpośredni callback pozostaje przydatny dla małych protokołów oraz dla opcjonalnej
fallbacku bez polecenia adaptera.

Obserwowalność testowa (backend mock):
- Zbuduj ramkowane żądanie za pomocą `hal_serial_frame_encode(seq, "HELLO", buf,
  sizeof(buf), NULL)`, dołącz `\n` i wstrzyknij je przez
  `hal_mock_serial_inject_rx(buf, -1)`.
- Zbadaj `hal_mock_serial_last_line()` i zdekoduj ją za pomocą
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

- Literalny sentinel startowy `$SC,`.
- `<seq>`: ASCII, liczba dziesiętna bez znaku w zakresie `[0..65535]`. Odpowiedź zawsze
  echuje seq żądania, aby host mógł je skorelować.
- `<payload>`: dowolny tekst ASCII. Nie może zawierać `*`, `\r` ani `\n`.
- `<crc8>`: dwie wielkie cyfry hex. CRC-8/CCITT (wielomian `0x07`, init
  `0x00`, bez refleksji, bez xor-out) po bajtach pomiędzy (ale z wyłączeniem)
  wiodącym `$` a separatorem `*`. Wektor referencyjny:
  `"123456789" -> 0xF4`.
- Terminator linii `\n` (pomocnicy kodujący **nie** dołączają go; użyj
  `hal_serial_println()`, które już to robi).

Firmware i towarzyszące narzędzia hostowe powinny dołączać ten przenośny nagłówek C
bezpośrednio z JaszczurHAL. Nie utrzymuj innego kodeka ramki ani skopiowanych
stałych. Obie strony powinny asercjonować ten sam wektor referencyjny CRC w testach.

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

Włączane tą samą flagą `HAL_ENABLE_CRYPTO`, co `hal_crypto`. Moduł
zależy od `hal_hmac_sha256`, więc włączanie auth bez crypto
nie jest sensowną konfiguracją. Gdy flaga jest wyłączona,
`hal_serial_session` nadal działa - handlery AUTH / REBOOT są
wykompilowywane (żadne tokeny poleceń nie są rozpoznawane niezależnie od tego, co
dostarcza słownik), a `hal_serial_session_is_authenticated()`
zwraca `false`.

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

Identyfikator sesji jest serializowany big-endian przez `hal_u32_to_bytes_be`, tak aby
firmware i host obliczały MAC dokładnie z tej samej sekwencji bajtów, niezależnie od
endianności hosta.

`hal_sc_auth_macs_equal` deleguje do pojedynczej wewnętrznej implementacji
`jh_constant_time_compare`. Bufory komunikatów uwierzytelniania i nieudane wyjścia są
zerowane przez `jh_secure_zeroize` przed zwróceniem.

Sól jest publiczną stałą ustalaną podczas buildu i obowiązującą w całym projekcie. Bezpieczeństwo
schematu opiera się na HMAC-SHA256 + UID per-urządzenie, **nie** na
poufności soli. Traktowanie soli jako poufnej jedynie zaciemniałoby zamysł
projektowy.

Jeśli stos hosta przenosi lustrzaną kopię tego pomocnika, utrzymuj obie
strony zsynchronizowane i testuj wektory wyprowadzania klucza + MAC odpowiedzi na
obu stronach. Kontrole krzyżowe wektorów wychwytują rozbieżności wcześnie i pozwalają
uniknąć niedopasowań AUTH_FAILED w runtime podczas integracji.

Sam handshake jest okablowany w
[`hal_serial_session`](#halserialsession-pomocnik-ramkowanej-sesji-szeregowej)
za slotami `cmd_auth_begin` / `cmd_auth_prove` słownika
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
