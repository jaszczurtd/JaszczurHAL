# GPIO, ADC i PWM

*Dostępne również [po angielsku](../en/05_gpio_adc_pwm.md).*

> **Część [Dokumentacji API JaszczurHAL](../../pl/JaszczurHAL_API.md)**

## `hal_gpio` - GPIO

```c
#include <hal/gpio/hal_gpio.h>

typedef enum {
    HAL_GPIO_INPUT                  = 0,
    HAL_GPIO_OUTPUT                 = 1,
    HAL_GPIO_INPUT_PULLUP           = 2,
    HAL_GPIO_INPUT_PULLDOWN         = 3,
    HAL_GPIO_OUTPUT_LOW             = 4,
    HAL_GPIO_OUTPUT_HIGH            = 5,
    HAL_GPIO_OUTPUT_OPEN_DRAIN      = 6,
    HAL_GPIO_OUTPUT_OPEN_DRAIN_LOW  = 7,
    HAL_GPIO_OUTPUT_OPEN_DRAIN_HIGH = 8,
} hal_gpio_mode_t;

typedef enum {
    HAL_GPIO_IRQ_FALLING = 0,
    HAL_GPIO_IRQ_RISING  = 1,
    HAL_GPIO_IRQ_CHANGE  = 2,
} hal_gpio_irq_mode_t;

void hal_gpio_set_mode(uint8_t pin, hal_gpio_mode_t mode);
void hal_gpio_write(uint8_t pin, bool high);
bool hal_gpio_read(uint8_t pin);
void hal_gpio_attach_interrupt(uint8_t pin, void (*callback)(void), hal_gpio_irq_mode_t mode);
void hal_gpio_detach_interrupt(uint8_t pin);

#define HAL_GPIO_IRQ_CORE_NONE UINT8_MAX

hal_status_t hal_gpio_attach_interrupt_ex(uint8_t pin,
                                          void (*callback)(void),
                                          hal_gpio_irq_mode_t mode,
                                          uint8_t owner_core);
hal_status_t hal_gpio_detach_interrupt_ex(uint8_t pin);
hal_status_t hal_gpio_get_interrupt_owner_ex(uint8_t pin,
                                             uint8_t *out_owner_core);

typedef enum {
    HAL_IRQ_PRIORITY_HIGHEST = 0,
    HAL_IRQ_PRIORITY_HIGH    = 1,
    HAL_IRQ_PRIORITY_DEFAULT = 2,
    HAL_IRQ_PRIORITY_LOW     = 3,
} hal_irq_priority_t;

void hal_gpio_set_irq_priority(hal_irq_priority_t priority);
```

**Uwaga:** Callback przekazany do `hal_gpio_attach_interrupt` działa w
kontekście ISR. Nie wywołuj w nim `printf`, `malloc` ani żadnych funkcji
blokujących.

**Walidacja:** Nieprawidłowe argumenty przekazane do starszych operacji `void`
wyzwalają `HAL_ASSERT` w buildach z włączonym sprawdzaniem. Operacje IRQ
zwracające status kończą się kodem `HAL_EINVAL` albo `HAL_EUNSUPPORTED` dla
nieobsługiwanego backendu lub pinu i nie konfigurują sprzętu.

**Początkowy stan wyjścia:** Tryby `HAL_GPIO_OUTPUT_LOW/HIGH` oraz
`HAL_GPIO_OUTPUT_OPEN_DRAIN_LOW/HIGH` jawnie określają początkowy stan wyjścia.
Dla zgodności `HAL_GPIO_OUTPUT` nadal oznacza wyjście push-pull rozpoczynające
pracę w stanie niskim.

**Wyjście typu otwarty dren:** STM32G474 i ESP32-S3 używają sprzętowego trybu
open-drain. Na RP2040 (natywny Pico SDK) jest on emulowany przez wymuszenie
stanu LOW dla `false` oraz przełączenie pinu w wejście o wysokiej impedancji
dla `true`.

**Wielowątkowość:** `hal_gpio_write` i `hal_gpio_read` wywołują backend
bezpośrednio. Różne rdzenie mogą bezpiecznie korzystać z różnych pinów.
Współbieżny dostęp do tego samego pinu wymaga synchronizacji po stronie
aplikacji.

**Przypisanie IRQ do rdzenia:** W kodzie wielordzeniowym używaj
`hal_gpio_attach_interrupt_ex`. Jeśli funkcję wywołano z rdzenia innego niż
`owner_core`, zwraca ona `HAL_ESTATE`. W przeciwnym razie atomowo przypisuje
przerwanie pinu do tego rdzenia. Rekonfigurację i
`hal_gpio_detach_interrupt_ex` można wykonać wyłącznie z przypisanego rdzenia.
`hal_gpio_get_interrupt_owner_ex` służy tylko do odczytu informacji
diagnostycznych i może być wywoływana z dowolnego rdzenia.
Odłączony pin zwraca `HAL_ENOENT` i zapisuje `HAL_GPIO_IRQ_CORE_NONE`.
Starszy adapter `attach` wiąże IRQ z rdzeniem, z którego go wywołano,
natomiast adapter `detach` wyzwala asercję, jeśli zasada przypisania zostanie
naruszona.

Na jednordzeniowym backendzie STM32G474 wszystkie te operacje muszą być
wykonywane na rdzeniu 0. ESP32-S3 alokuje jedną usługę ISR GPIO ESP-IDF dla
wszystkich callbacków HAL GPIO na rdzeniu, z którego wykonano pierwsze udane
dołączenie. W konsekwencji każde aktywne przerwanie HAL GPIO musi być
przypisane do tego samego rdzenia, dopóki ostatni callback nie zostanie
odłączony, a usługa zwolniona. Próba przypisania innego rdzenia zwraca
`HAL_ESTATE`. Funkcje zwracające status służą do diagnostyki
inicjalizacji/zadań, nie do kontekstu ISR.

To API jawnego przypisania obejmuje wyłącznie przerwania GPIO. Przerwania
peryferyjne podlegają odrębnym zasadom backendu. W szczególności przerwanie RX
sprzętowego UART RP2040 jest obecnie wiązane niejawnie z rdzeniem, który
wywołuje `hal_uart_begin()`; GPS dziedziczy to zachowanie, gdy zbudowany jest
z `HAL_GPS_TRANSPORT_UART`. API UART nie udostępnia informacji o przypisanym
rdzeniu. Na RP operacje `begin`, rekonfiguracji i `destroy` trzeba
serializować i wykonywać na tym samym rdzeniu. ESP32-S3 dodatkowo zwraca
`HAL_ESTATE`, jeśli rekonfiguracja
zostanie wywołana z niewłaściwego rdzenia. Wywołanie zgodnościowej operacji
`destroy` z innego rdzenia nie zwalnia uchwytu. Zobacz
[dokumentację magistrali `hal_uart`](09_buses.md) i
[dokumentację czujnika `hal_gps`](11_sensors.md).

RP2040 SoftwareSerial odbiera zamiast tego przez PIO/DMA i nie instaluje
przerwania RX CPU.

**Routing STM32G474:** Identyfikator pinu ma postać `port * 16 + pin`
(`PA0=0`, `PB0=16`, ...). EXTI działa według numeru linii
(`line == pin_number`), dlatego w danej chwili tylko jeden port może używać
konkretnej linii. Dołączenie pinu o tym samym numerze z innego portu zmienia
przypisanie tej linii EXTI.

**Priorytet IRQ:** `hal_gpio_set_irq_priority` ustawia priorytet przerwania
GPIO. Na RP2040 wszystkie piny GPIO współdzielą `IO_IRQ_BANK0`. Na STM32G474
przerwania GPIO są podzielone między `EXTI0..EXTI4`, `EXTI9_5` i
`EXTI15_10`; ten sam priorytet HAL jest stosowany do wszystkich tych wpisów
NVIC. ESP32-S3 odtwarza swoją współdzieloną usługę ISR na rdzeniu-właścicielu
usługi. Poziomom HAL `highest`, `high` oraz `default`/`low` odpowiadają poziomy
przerwań ESP-IDF 3, 2 i 1.

**impl/esp32:** Piny są sprawdzane przy użyciu wygenerowanych masek: poprawne
dla targetu, tylko wejściowe, wyprowadzone na płytce, zarezerwowane na stałe i
zarezerwowane programowo. Piny USB i pamięci zarezerwowane na stałe są
odrzucane. Aplikacja może świadomie użyć pinu zarezerwowanego programowo przez
profil płytki. Callbacki GPIO działają w kontekście ISR przez wspólną usługę
ESP-IDF.

**Odłączanie przerwania:** `hal_gpio_detach_interrupt` usuwa zarejestrowany
callback i maskuje źródło pin/EXTI, jeśli backend obsługuje sprzętowe
maskowanie przerwań.

Na przykład obsługa sygnału RPM przeznaczona dla rdzenia 1 RP2040 może zgłosić
błąd już podczas inicjalizacji, zamiast niejawnie zarejestrować przerwanie na
niewłaściwym rdzeniu:

```c
hal_status_t status = hal_gpio_attach_interrupt_ex(
    rpm_pin, rpm_edge_isr, HAL_GPIO_IRQ_RISING, 1u);
if (status != HAL_OK) {
    /* Abort ECU startup or report a core-affinity configuration fault. */
}
```

---

## `hal_pwm` - PWM

```c
#include <hal/gpio/hal_pwm.h>

void hal_pwm_set_resolution(uint8_t bits);
bool hal_pwm_is_pin_supported(uint8_t pin);
void hal_pwm_write(uint8_t pin, uint32_t value);
```

`hal_pwm` to proste, przenośne API PWM. Celowo udostępnia niewielki zestaw
zachowań.
Rozdzielczość wynosi od 1 do 16 bitów, `hal_pwm_write()` ogranicza wartości do
bieżącego maksimum, a nieobsługiwane piny są ignorowane i wyzwalają `HAL_ASSERT`
w buildach z włączonym sprawdzaniem. Przed dynamicznym wyborem pinu wywołaj
`hal_pwm_is_pin_supported()`.

Nie gwarantuje częstotliwości wybranej przez wywołującego ani niezależnej
alokacji kanału. Użyj `hal_pwm_freq`, gdy liczy się częstotliwość, okres
(`wrap`) oraz cykl życia kanału. Domyślna rozdzielczość to 8 bitów.

**impl/rp2040:** natywny Pico SDK `hardware/pwm.h` (`pwm_init`,
`pwm_config_set_wrap`, `pwm_set_gpio_level`, `pwm_set_enabled`). Publiczny
zakres wypełnienia wynosi `0..2^bits-1`. Przy niskiej rozdzielczości backend
może wewnętrznie zwiększyć wartość `wrap` slice'a, aby zachować domyślną
częstotliwość zbliżoną do 1 kHz, gdy `clkdiv` przekroczyłby limit sprzętowy.
Dwa GPIO należące do tego samego sprzętowego slice'a (`gpio/2 mod 8`) mają
wspólną częstotliwość i wartość `wrap`, ale niezależne wypełnienie. Użyj
`hal_pwm_freq`, gdy liczy się dokładna częstotliwość.

**impl/stm32g474:** PWM TIM zaimplementowany na poziomie rejestrów dla
przypisanych kanałów timera. Prosty PWM ustawia w miarę możliwości 1 kHz w
granicach możliwości sprzętu. Backend używa jawnych stałych
`JH_G474_TIMCLK1_HZ` /
`JH_G474_TIMCLK2_HZ`. Obie wynoszą 170 MHz w bieżącym drzewie zegarów,
ponieważ APB1 i APB2 działają bez preskalera; przyszłe zmiany APB muszą
aktualizować stałe zegara wejściowego timera zgodnie z regułą mnożenia zegara
timera STM32 przez 2.

**impl/esp32:** wyjście ESP-IDF LEDC o częstotliwości 1 kHz. Backend przy
pierwszym użyciu przydziela logiczny kanał LEDC każdemu pinowi wyjściowemu i
przelicza wybrany zakres 1..16 bitów na sprzętowy zakres wypełnienia. Zmiana
globalnej rozdzielczości zwalnia wszystkie aktywne kanały prostego PWM.
Maksymalna wartość logiczna korzysta ze stanu zatrzymania LEDC z wyjściem
wysokim, co daje dokładne wypełnienie 100%. Kolejny zapis niższej wartości
ponownie uruchamia przebieg. Jeśli zatrzymanie lub dekonfiguracja kanału się
nie powiedzie, kanał wewnętrzny pozostaje zajęty, a globalna rozdzielczość się
nie zmienia. Zapobiega to ponownemu przydzieleniu nadal aktywnego kanału
sprzętowego.

**Wielowątkowość:** Na RP2040 piny są przypisane do sprzętowych slice'ów PWM,
a na STM32G474 do kanałów TIM. Kanały tego samego timera mają wspólną
częstotliwość i rozdzielczość, natomiast piny przypisane do jednego kanału TIM
nie działają niezależnie. Wywołuj `hal_pwm_set_resolution` podczas
inicjalizacji, a nie równolegle z zapisem.

---

## `hal_dac` - Prawdziwe wyjście DAC *(opcjonalny - `HAL_ENABLE_DAC`)*

```c
#include <hal/analog/hal_dac.h>

bool hal_dac_is_supported(void);
uint8_t hal_dac_resolution_bits(void);
uint16_t hal_dac_max_value(void);

hal_status_t hal_dac_init_ex(uint8_t channel);
bool hal_dac_init(uint8_t channel);

hal_status_t hal_dac_write(uint8_t channel, uint16_t value);
hal_status_t hal_dac_write_millivolts(uint8_t channel, uint16_t millivolts);
```

API statusowe zwraca `HAL_OK`, `HAL_EUNSUPPORTED` na targetach bez
prawdziwego DAC (RP2040), `HAL_EINVAL` dla nieprawidłowych kanałów oraz
`HAL_EUNINIT` dla zapisów przed inicjalizacją kanału. `hal_dac_init()`
pozostaje adapterem zgodności zwracającym `bool` i wywołującym
`hal_dac_init_ex()`. Starsze funkcje zapisu zwracają teraz bezpośrednio
`hal_status_t`; dotychczasowy kod może nadal ignorować wynik.

**impl/stm32g474:** prawdziwy DAC1, 12-bitowy, kanał 0 -> PA4 i kanał 1 -> PA5.

**impl/rp2040:** brak prawdziwego peryferium DAC; API statusów zwraca
`HAL_EUNSUPPORTED`.

**impl/.mock:** dwa kanały 12-bitowe z funkcjami pomocniczymi mock do odczytu
zapisanych wartości.

---

## `hal_pcnt` - Licznik impulsów / zboczy *(opcjonalny - `HAL_ENABLE_PCNT`)*

```c
#include <hal/analog/hal_pcnt.h>

bool hal_pcnt_is_supported(void);
uint8_t hal_pcnt_channel_count(void);

hal_status_t hal_pcnt_init_ex(uint8_t channel, uint8_t pin,
                              hal_pcnt_edge_t edge);
bool hal_pcnt_init(uint8_t channel, uint8_t pin, hal_pcnt_edge_t edge);

hal_status_t hal_pcnt_read_ex(uint8_t channel, uint32_t *out_count);
uint32_t hal_pcnt_read(uint8_t channel);

hal_status_t hal_pcnt_reset(uint8_t channel);

hal_status_t hal_pcnt_read_and_reset_ex(uint8_t channel, uint32_t *out_count);
uint32_t hal_pcnt_read_and_reset(uint8_t channel);
```

API statusowe zwraca `HAL_OK`, `HAL_EINVAL` dla nieprawidłowych kanałów,
pinów, zboczy lub wskaźników wyjściowych, oraz `HAL_EUNINIT` przy
odczycie/resecie prawidłowego kanału, który nie został zainicjalizowany.
Starsza funkcja `void hal_pcnt_reset()` zwraca teraz bezpośrednio
`hal_status_t`; dotychczasowy kod może nadal ignorować wynik.
Starsze adaptery `init`, `read` i `read-and-reset` zachowują wyniki typu
`bool`/`uint32_t`.

**impl/rp2040:** liczniki programowe oparte na przerwaniu GPIO.

**impl/stm32g474:** tryb zegara zewnętrznego TIM2 dla kanału 0.

**impl/esp32:** cztery liczniki logiczne oparte na jednostkach ESP-IDF PCNT.
Każde wejście wybiera zbocze narastające, opadające lub oba. Tryb zliczania
skumulowanego oraz punkty progowe ze znakiem rozszerzają zakres
sprzętowego licznika do publicznego wyniku `uint32_t`. Ponowna inicjalizacja
kanału logicznego najpierw usuwa jego poprzednią jednostkę PCNT.

**impl/.mock:** liczniki w pamięci oraz funkcje pomocnicze do ustawiania
impulsów w testach.


---

## `hal_pwm_freq` - PWM ze sterowaniem częstotliwością *(opcjonalny - `HAL_ENABLE_PWM_FREQ`)*

Użyj tego zamiast `hal_pwm`, gdy potrzebujesz konkretnej częstotliwości PWM (np. 160 Hz, 300 Hz).

```c
#include <hal/gpio/hal_pwm_freq.h>

// Opaque handle
typedef hal_pwm_freq_channel_impl_t *hal_pwm_freq_channel_t;

// Create a channel: pin, frequency in Hz, resolution (wrap value, e.g. 2047 for 11-bit = 2^11-1)
hal_pwm_freq_channel_t hal_pwm_freq_create(uint8_t pin,
                                           uint32_t frequency_hz,
                                           uint32_t resolution);

// Write value in [0, resolution] - values outside range are clamped automatically
void hal_pwm_freq_write(hal_pwm_freq_channel_t ch, int value);

// Stop output without releasing the channel; the next write starts it again
void hal_pwm_freq_stop(hal_pwm_freq_channel_t ch);

// Free resources
void hal_pwm_freq_destroy(hal_pwm_freq_channel_t ch);
```

**impl/rp2040:** Pico SDK `hardware/pwm.h` + `hardware/clocks.h` - oblicza
`clkdiv` i `wrap`, aby dokładnie uzyskać żądaną częstotliwość. Dla przypadków
brzegowych stosuje korekcję pseudo/slow-scale.

**impl/stm32g474:** PWM TIM na poziomie rejestrów na zmapowanych kanałach
TIM2/TIM3/TIM4/TIM15/TIM16/TIM17. Częstotliwość jest zasobem na poziomie
timera, więc wiele kanałów na tym samym TIM współdzieli tę samą częstotliwość
i efektywny okres. Podobnie jak `hal_pwm`, wykorzystuje jawne stałe TIMCLK
170 MHz, dzięki czemu częstotliwość próbkowania podawana przez DACless jest
zgodna z konfiguracją timera. Wyjście PWM jest konfigurowane podczas
`hal_pwm_freq_create()`, ale **nie jest uruchamiane** - skonfigurowanie funkcji
GPIO lub włączenie kanału TIM jest odroczone do pierwszego wywołania
`hal_pwm_freq_write()`. Zapobiega to krótkim, niepożądanym impulsom po
włączeniu zasilania na pinach z logiką odwróconą (0% wypełnienia = element wykonawczy
włączony).

**impl/esp32:** ESP-IDF LEDC korzysta z tego samego alokatora targetu co
prosty PWM. Utworzenie kanału rezerwuje jeden uchwyt z puli o stałym rozmiarze
logicznych oraz zgodny timer i kanał LEDC dla wybranego pinu, częstotliwości i
maksimum logicznego. Zapisy są ograniczane do tego maksimum. Operacja `stop`
zachowuje uchwyt, a `destroy` zwalnia zasoby logiczne i LEDC. Wspólny alokator
zapewnia dokładne 100% wypełnienia przez stan `idle-high` i ponownie uruchamia
LEDC po zapisie wartości częściowej. Jeśli ESP-IDF odrzuci usunięcie kanału,
uchwyt logiczny i kanał LEDC pozostają zarezerwowane do ponowienia próby. W buildach z
włączonym sprawdzaniem nieudane `hal_pwm_freq_destroy()` wyzwala `HAL_ASSERT`.

**impl/.mock:** przechowuje ostatnio zapisaną wartość, którą można odczytać
funkcjami pomocniczymi mock.

**Funkcje pomocnicze mock:**
```c
int      hal_mock_pwm_freq_get_value(hal_pwm_freq_channel_t ch);
uint32_t hal_mock_pwm_freq_get_frequency(hal_pwm_freq_channel_t ch);
uint8_t  hal_mock_pwm_freq_get_pin(hal_pwm_freq_channel_t ch);
bool     hal_mock_pwm_freq_is_running(hal_pwm_freq_channel_t ch);
```

**Wielowątkowość:** Backendy RP2040, STM32G474 i ESP32-S3 chronią
`hal_pwm_freq_create()`, `hal_pwm_freq_write()`, `hal_pwm_freq_stop()` i
`hal_pwm_freq_destroy()` wewnętrznym mutexem. Kod wywołujący nadal odpowiada
za cykl życia uchwytu kanału i nie może używać go po
`hal_pwm_freq_destroy()`. Backend mock nie zapewnia synchronizacji
współbieżnego dostępu.

---

## `DAClessAudio` - Silnik audio PWM *(opcjonalny - `HAL_ENABLE_DACLESS`)*

```cpp
#include <hal/audio/hal_dacless.h>

struct DAClessConfig {
    uint8_t  pinPWM;      // default 6
    uint16_t pwmBits;     // default 12
    uint16_t blockSize;   // default 128, capped by DACLESS_MAX_BLOCK_SIZE
    uint8_t  nAdcInputs;  // capped by DACLESS_MAX_ADC_INPUTS
    bool     useDma;      // default true; set false for cooperative service()
    uint8_t  adcPins[DACLESS_MAX_ADC_INPUTS];
};

class DAClessAudio {
public:
    using SampleCallback = uint16_t (*)(void *);
    using BlockCallback  = void (*)(void *, uint16_t *);

    explicit DAClessAudio(const DAClessConfig &cfg = DAClessConfig());
    bool begin();
    void service();
    void mute();
    void unmute();
    void setSampleCallback(SampleCallback cb, void *userdata = nullptr);
    void setBlockCallback(BlockCallback cb, void *userdata = nullptr);
    uint16_t getADC(uint8_t channel) const;
    float getSampleRate() const;
    const volatile uint16_t *getOutBufPtr() const;
    const volatile uint16_t *getAdcBuffer() const;
    bool isDmaActive() const;
};

uint16_t interpolate(uint16_t x, uint16_t y, uint16_t mu_scaled);
```

`HAL_ENABLE_DACLESS` propaguje `HAL_ENABLE_DMA_PWM_AUDIO` i
`HAL_ENABLE_PWM_FREQ`. Wspólny driver bazuje na silniku DACless autorstwa
Briana Varrena. Zachowuje jego konfigurację, podwójnie buforowany przepływ
bloków, callbacki próbek i bloków, bufor wyników ADC, globalne zmienne
zgodności (`audio_rate`, `out_buf_ptr`, `adc_results_buf`) oraz działanie
funkcji interpolującej 8-bitowy współczynnik mieszania z wersji RP2040.

Domyślnie (`cfg.useDma = true`) `hal_dma_pwm_audio` zasila PWM przez podwójnie
buforowane DMA i na bieżąco aktualizuje bufor wyników ADC. Na RP2040 zachowany
jest oryginalny łańcuch DMA: kanały A/B dla PWM oraz osobne DMA sterujące i
próbkujące ADC. Na STM32G474 DMA aktualizacji TIM zapisuje aktywny rejestr CCR,
callbacki `half-transfer`/`transfer-complete` obsługują dwie połowy bufora
audio, a ADC1 cyklicznie skanuje skonfigurowane piny przez DMA.

`begin()` zwraca `true`, gdy backend PWM/DMA został utworzony i uruchomiony.
Gdy zwraca `false`, instancja pozostaje zatrzymana i wyciszona; może się to
zdarzyć, gdy backend targetu wyczerpał stały zasób sprzętowy.

Ustaw `cfg.useDma = false`, aby pracować kooperacyjnie. Wywołuj wtedy często
`service()` z `app_task0()` albo zadania FreeRTOS. Funkcja zapisuje zaległe
próbki przez `hal_pwm_freq_write()` i uzupełnia gotowy bufor za pomocą
callbacku blokowego. Jeśli go nie ustawiono, używa callbacku próbkowego, a w
ostateczności wpisuje ciszę odpowiadającą środkowi zakresu. Gdy `service()`
zostanie wywołana z opóźnieniem, odtwarzanie przez polling nadrabia najwyżej
`DACLESS_MAX_POLLING_CATCHUP_SAMPLES` próbek przed ponowną synchronizacją z
bieżącym czasem. Callbacki są wywoływane poza mutexem instancji, więc mogą
odczytywać `getADC()` bez ryzyka zakleszczenia.

Domyślne piny ADC to GPIO 26..29 na RP2040 i w backendzie mock oraz PA0..PA3 na STM32G474
(`port * 16 + pin`). Nadpisz `cfg.adcPins[]` dla niestandardowego
okablowania.

**Wielowątkowość:** Publiczne metody chronią stan każdej instancji osobnym
mutexem HAL utworzonym przez `jh_hal_mutex_create_once`.
Callbacki bufora DMA działają z poziomu przerwania DMA backendu i nie
przejmują mutexu instancji. Rejestracja callbacków, `begin()`, `service()`,
`mute()`, `unmute()` i `getADC()` są bezpieczne do wywołania z normalnego
kontekstu zadania/rdzenia. Nie wywołuj `service()` z ISR.

---

## `hal_adc` - Wejście analogowe

```c
#include <hal/analog/hal_adc.h>

void hal_adc_set_resolution(uint8_t bits);
int  hal_adc_read(uint8_t pin);
```

Domyślna rozdzielczość to 12 bitów (spójna we wszystkich backendach RP2040,
STM32G474, ESP32-S3 i mock).

- **impl/rp2040:** natywny pico-sdk `hardware/adc.h` (`adc_init`, `adc_gpio_init`,
  `adc_select_input`, `adc_read`). Prawidłowe piny ADC to GPIO 26-29 (kanały 0-3);
  12-bitowa próbka sprzętowa jest przeskalowywana do skonfigurowanej rozdzielczości.
- **impl/stm32g474:** regularne konwersje niesymetryczne ADC1 z uruchamianiem
  regulatora dopiero przy pierwszym użyciu, kalibracją i walidacją mapowania
  pinu na kanał.
  ADC12 jest taktowane synchronicznie z HCLK/4, czyli 42,5 MHz przy bieżącym
  drzewie zegarów 170 MHz.
- **impl/esp32:** konwersja jednorazowa (oneshot) ESP-IDF ADC z leniwą
  konfiguracją jednostki/kanału i tłumieniem 12 dB. Akceptowane są wyłącznie
  piny obsługujące ADC, które profil płytki oznacza jako wyprowadzone lub
  zarezerwowane programowo. 12-bitowy wynik sprzętowy jest skalowany do
  skonfigurowanego zakresu 1..16-bitowego; nieprawidłowy pin lub nieudana
  konwersja zwraca
  wartość kompatybilności `0`.
- **impl/.mock:** wartości poszczególnych pinów można ustawić przez
  `hal_mock_adc_inject(pin, value)`.

**Wielowątkowość:** API może być bezpiecznie używane z wielu wątków i rdzeni.
Wewnętrzny mutex chroni wspólny stan ADC na RP2040, STM32G474 i ESP32-S3,
dlatego współbieżne odczyty są automatycznie wykonywane kolejno.

---

*Dalej: [Timery i system](06_timers_system.md)*
