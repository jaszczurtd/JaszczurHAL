# Czujniki

*Dostępne również [po angielsku](../en/11_sensors.md).*

> **Część [Dokumentacji API JaszczurHAL](../../pl/JaszczurHAL_API.md)**

Obejmuje: `hal_thermocouple`, `hal_ds18b20`, `hal_dht`, `hal_bh1750`, `hal_adp5360`, `hal_mcp3221`, `hal_tsc2007`, `hal_stmpe610`, `hal_irsmall_decoder`, `hal_rtc`, `hal_external_adc`, `hal_gps`.

## `hal_thermocouple` - Wzmacniacz termopary  *(opcjonalny - `HAL_ENABLE_THERMOCOUPLE`)*

Obsługuje MCP9600/MCP9601 (współdzielony driver HAL I2C) oraz MAX6675
(współdzielony bit-bang SPI po HAL GPIO). Jedna fasada niezależna od targetu
zarządza statyczną pulą uchwytów, walidacją, blokadami per instancja oraz
doborem operacji na podstawie capabilities układu. Dzięki temu providerzy
sprzętowi i deterministyczny provider mock hosta korzystają z
tego samego publicznego cyklu życia. Funkcje niedostępne dla wybranego chipu
zwracają `HAL_EUNSUPPORTED`; historyczne wrappery zwracające wartość zwracają
bezpieczną wartość domyślną (`NAN` / `0` / `false`) i wypisują błąd.

```c
#include <hal/temperature/hal_thermocouple.h>

// Selektor chipu
typedef enum {
    HAL_THERMOCOUPLE_CHIP_MCP9600,  // MCP9600/MCP9601 przez I2C
    HAL_THERMOCOUPLE_CHIP_MAX6675,  // MAX6675 przez bit-bang SPI po HAL GPIO (tylko typ K)
} hal_thermocouple_chip_t;

// Typ przewodu (MCP9600 obsługuje wszystkie; MAX6675 ma stały typ K)
typedef enum {
    HAL_THERMOCOUPLE_TYPE_K = 0, HAL_THERMOCOUPLE_TYPE_J, HAL_THERMOCOUPLE_TYPE_T,
    HAL_THERMOCOUPLE_TYPE_N,     HAL_THERMOCOUPLE_TYPE_S, HAL_THERMOCOUPLE_TYPE_E,
    HAL_THERMOCOUPLE_TYPE_B,     HAL_THERMOCOUPLE_TYPE_R,
} hal_thermocouple_type_t;

// Rozdzielczość ADC (tylko MCP9600)
typedef enum {
    HAL_THERMOCOUPLE_ADC_RES_18 = 0,  // 18-bitowa, ~320 ms/konwersję
    HAL_THERMOCOUPLE_ADC_RES_16,      // 16-bitowa, ~80 ms/konwersję
    HAL_THERMOCOUPLE_ADC_RES_14,      // 14-bitowa, ~20 ms/konwersję
    HAL_THERMOCOUPLE_ADC_RES_12,      // 12-bitowa, ~5 ms/konwersję
} hal_thermocouple_adc_res_t;

// Rozdzielczość otoczenia (złącze zimne) (tylko MCP9600)
typedef enum {
    HAL_THERMOCOUPLE_AMBIENT_RES_0_25    = 0,  // 0,25 °C/LSB
    HAL_THERMOCOUPLE_AMBIENT_RES_0_125,         // 0,125 °C/LSB
    HAL_THERMOCOUPLE_AMBIENT_RES_0_0625,        // 0,0625 °C/LSB
    HAL_THERMOCOUPLE_AMBIENT_RES_0_03125,       // 0,03125 °C/LSB
} hal_thermocouple_ambient_res_t;

// Struktura konfiguracji - wyzeruj ją, następnie wypełnij chip i pasujący element bus
typedef struct {
    hal_thermocouple_chip_t chip;
    union {
        struct {
            uint8_t sda_pin;
            uint8_t scl_pin;
            uint32_t clock_hz;
            uint8_t i2c_bus;   // 0 = główna, 1 = druga
            uint8_t i2c_addr;
        } i2c;
        struct { uint8_t sclk_pin; uint8_t cs_pin; uint8_t miso_pin; } spi;
    } bus;
} hal_thermocouple_config_t;

typedef hal_thermocouple_impl_t *hal_thermocouple_t;  // nieprzezroczysty uchwyt

// Cykl życia
hal_thermocouple_t hal_thermocouple_init(const hal_thermocouple_config_t *cfg);
hal_status_t hal_thermocouple_init_ex(const hal_thermocouple_config_t *cfg,
                                      hal_thermocouple_t *out);
void               hal_thermocouple_deinit(hal_thermocouple_t h);  // bezpieczne dla NULL

// Odczyty
float   hal_thermocouple_read(hal_thermocouple_t h);          // złącze gorące °C, NAN przy błędzie
hal_status_t hal_thermocouple_read_ex(hal_thermocouple_t h, float *out_c);
float   hal_thermocouple_read_ambient(hal_thermocouple_t h);  // złącze zimne °C (tylko MCP9600)
hal_status_t hal_thermocouple_read_ambient_ex(hal_thermocouple_t h, float *out_c);
int32_t hal_thermocouple_read_adc_raw(hal_thermocouple_t h);  // surowe µV (tylko MCP9600); 0, gdy nieobsługiwane
hal_status_t hal_thermocouple_read_adc_raw_ex(hal_thermocouple_t h, int32_t *out_raw);

// Konfiguracja (tylko MCP9600, jeśli nie zaznaczono inaczej)
hal_status_t hal_thermocouple_set_type(hal_thermocouple_t h, hal_thermocouple_type_t type);
hal_thermocouple_type_t hal_thermocouple_get_type(hal_thermocouple_t h);  // MAX6675 zawsze zwraca K
hal_status_t hal_thermocouple_get_type_ex(hal_thermocouple_t h,
                                          hal_thermocouple_type_t *out_type);

hal_status_t hal_thermocouple_set_filter(hal_thermocouple_t h, uint8_t coeff); // współczynnik IIR [0,7]
uint8_t hal_thermocouple_get_filter(hal_thermocouple_t h);
hal_status_t hal_thermocouple_get_filter_ex(hal_thermocouple_t h, uint8_t *out_coeff);

hal_status_t hal_thermocouple_set_adc_resolution(hal_thermocouple_t h,
                                                 hal_thermocouple_adc_res_t res);
hal_thermocouple_adc_res_t hal_thermocouple_get_adc_resolution(hal_thermocouple_t h);
hal_status_t hal_thermocouple_get_adc_resolution_ex(
    hal_thermocouple_t h, hal_thermocouple_adc_res_t *out_res);

hal_status_t hal_thermocouple_set_ambient_resolution(
    hal_thermocouple_t h, hal_thermocouple_ambient_res_t res);

hal_status_t hal_thermocouple_enable(hal_thermocouple_t h, bool enable); // false = uśpienie
bool hal_thermocouple_is_enabled(hal_thermocouple_t h);           // MAX6675 zawsze zwraca true
hal_status_t hal_thermocouple_is_enabled_ex(hal_thermocouple_t h,
                                            bool *out_enabled);

// Kanały alarmowe 1-4 (tylko MCP9600)
typedef struct {
    float temperature; bool rising; bool alert_cold_junction;
    bool active_high;  bool interrupt_mode;
} hal_thermocouple_alert_cfg_t;

hal_status_t hal_thermocouple_set_alert(
    hal_thermocouple_t h, uint8_t alert_num, bool enabled,
    const hal_thermocouple_alert_cfg_t *cfg);
float hal_thermocouple_get_alert_temp(hal_thermocouple_t h, uint8_t alert_num);
hal_status_t hal_thermocouple_get_alert_temp_ex(
    hal_thermocouple_t h, uint8_t alert_num, float *out_c);

uint8_t hal_thermocouple_get_status(hal_thermocouple_t h);  // surowy rejestr statusu
hal_status_t hal_thermocouple_get_status_ex(hal_thermocouple_t h,
                                            uint8_t *out_status);
```

Każde pole używane przez wybrany chip musi zostać zainicjalizowane. Zacznij od
wyzerowanego deskryptora i jawnie ustaw `i2c_bus` dla MCP9600:

```c
hal_thermocouple_config_t cfg = {0};
cfg.chip = HAL_THERMOCOUPLE_CHIP_MCP9600;
cfg.bus.i2c.sda_pin = 4;
cfg.bus.i2c.scl_pin = 5;
cfg.bus.i2c.clock_hz = HAL_I2C_CLOCK_STANDARD_HZ;
cfg.bus.i2c.i2c_bus = 0;
cfg.bus.i2c.i2c_addr = 0x67;

hal_thermocouple_t sensor = hal_thermocouple_init(&cfg);
```

Niezainicjalizowane `i2c_bus` może wybrać nieprawidłowy lub niezamierzony
kontroler. Backendy natywne walidują indeks magistrali, a inicjalizacja wtedy
zawodzi. Dotyczy to również migracji kodu, który wcześniej polegał na
permisywnych wartościach domyślnych I2C.

Wspólna fasada wybiera providera sprzętowego na RP2040/RP2350 oraz STM32G474;
ten provider deleguje operacje MCP9600/MCP9601 i MAX6675 do tych samych
przenośnych driverów dostępnych wyłącznie w HAL. Provider hosta zachowuje
deterministyczne wstrzykiwanie przez `hal_mock_thermocouple_*()` bez
posiadania drugiej fasady.

**Thread safety:** Thread-safe i wielordzeniowo. Przydział
z puli jest chroniony sekcją krytyczną. Każda żyjąca instancja posiada własny
`hal_mutex_t`; odczyt, konfiguracja, wstrzykiwanie mock oraz deinicjalizacja
są serializowane przez ten mutex.

---

## `hal_ds18b20` - cyfrowy czujnik temperatury DS18B20  *(opcjonalny - `HAL_ENABLE_DS18B20`)*

Nieblokujący workflow czujnika:

1. `hal_ds18b20_request()` rozpoczyna konwersję.
2. `hal_ds18b20_poll()` przesuwa maszynę stanów.
3. `hal_ds18b20_take_latest()` odczytuje próbkę z pamięci podręcznej (`fresh=true` tylko raz dla każdej nowej próbki).

```c
#include <hal/temperature/hal_ds18b20.h>

#ifndef HAL_DS18B20_MAX_INSTANCES
#define HAL_DS18B20_MAX_INSTANCES 4
#endif

typedef struct hal_ds18b20_impl_s *hal_ds18b20_t;

typedef enum {
    HAL_DS18B20_RES_9_BIT  = 9,
    HAL_DS18B20_RES_10_BIT = 10,
    HAL_DS18B20_RES_11_BIT = 11,
    HAL_DS18B20_RES_12_BIT = 12,
} hal_ds18b20_resolution_t;

typedef struct {
    uint8_t data_pin;
    bool    use_rom;      // false: Skip ROM (magistrala z jednym urządzeniem), true: Match ROM
    uint8_t rom_code[8];  // ważne, gdy use_rom=true
    hal_ds18b20_resolution_t resolution_hint;
} hal_ds18b20_config_t;

hal_ds18b20_t hal_ds18b20_init(const hal_ds18b20_config_t *cfg);
hal_status_t  hal_ds18b20_init_ex(const hal_ds18b20_config_t *cfg,
                                  hal_ds18b20_t *out);
hal_status_t  hal_ds18b20_deinit(hal_ds18b20_t h);
bool          hal_ds18b20_request(hal_ds18b20_t h);
hal_status_t  hal_ds18b20_request_ex(hal_ds18b20_t h);
hal_status_t  hal_ds18b20_poll(hal_ds18b20_t h);
bool          hal_ds18b20_is_busy(hal_ds18b20_t h);
bool          hal_ds18b20_take_latest(hal_ds18b20_t h, float *temp_c, bool *fresh);
hal_status_t  hal_ds18b20_take_latest_ex(hal_ds18b20_t h, float *temp_c,
                                         bool *fresh);
```

`hal_ds18b20_init()` zachowuje historyczny kształt zwracający uchwyt; użyj
`hal_ds18b20_init_ex()`, gdy wywołujący potrzebuje przyczyny niepowodzenia.
Historyczne funkcje `bool` są cienkimi wrapperami nad ich formami `_ex`.
Historyczne wywołania cyklu życia/maszyny stanów typu `void` zwracają teraz
`hal_status_t`; wywołujący, którzy ignorują wynik, nadal się kompilują.

Mapowanie statusów: nieprawidłowe argumenty zwracają `HAL_EINVAL`,
niepowodzenie przydziału z puli uchwytów lub mutexu zwraca `HAL_ENOMEM`,
brakujący/niepasujący czujnik zwraca `HAL_ENOENT`, żądanie w trakcie
aktywnej konwersji zwraca `HAL_EBUSY`, odpytywanie przed upływem terminu
konwersji zwraca `HAL_EAGAIN`, odpytywanie w stanie bezczynności zwraca
`HAL_ESTATE`, a niepowodzenie scratchpada/CRC/dekodowania zwraca
`HAL_EPROTO`.

**impl/rp2040 + impl/stm32g474:** Oba wykorzystują wspólną implementację
`src/hal/onewire/` dostępną wyłącznie w HAL. Backend wykonuje sondowanie
obecności/adresu DS18B20, weryfikację CRC scratchpada, zapisy rozdzielczości,
nieblokujące planowanie konwersji za pomocą `hal_micros64()` oraz dekodowanie
temperatury poprzez wspólny transport bit-bang 1-Wire.
**impl/.mock:** deterministyczna maszyna stanów konwersji sterowana czasem
mock (`hal_mock_set_micros` / `hal_mock_advance_micros`), z wstrzykiwaną
obecnością/CRC/temperaturą.
**Thread safety:** Backendy sprzętowe używają mutexu per-uchwyt.
Tworzenie/niszczenie powinno nadal przestrzegać projektowej polityki
init/deinit jednordzeniowego. Backend mock jest przeznaczony do testów
jednowątkowych.

**Pomocnicy mock:**
```c
void     hal_mock_ds18b20_set_next_temp(hal_ds18b20_t h, float temp_c);
void     hal_mock_ds18b20_set_presence(hal_ds18b20_t h, bool present);
void     hal_mock_ds18b20_set_crc_ok(hal_ds18b20_t h, bool ok);
uint32_t hal_mock_ds18b20_get_request_count(hal_ds18b20_t h);
```

---

## `hal_dht` - czujnik temperatury i wilgotności DHT11/DHT22  *(opcjonalny - `HAL_ENABLE_DHT`)*

Blokujący odczyt pojedynczej ramki DHT po HAL GPIO.

```c
#include <hal/temperature/hal_dht.h>

#ifndef HAL_DHT_MAX_INSTANCES
#define HAL_DHT_MAX_INSTANCES 4
#endif

typedef enum {
  HAL_DHT_SENSOR_DHT11 = 0,
  HAL_DHT_SENSOR_DHT22 = 1,
} hal_dht_sensor_t;

typedef struct {
  uint8_t data_pin;
  hal_dht_sensor_t sensor;
} hal_dht_config_t;

typedef struct hal_dht_impl_s *hal_dht_t;

typedef struct {
  float temperature_c;
  float temperature_f;
  float humidity;
} hal_dht_sample_t;

hal_dht_config_t hal_dht_default_config(uint8_t data_pin);
hal_status_t     hal_dht_init_ex(const hal_dht_config_t *cfg,
                                 hal_dht_t *out_handle);
hal_dht_t        hal_dht_init(const hal_dht_config_t *cfg);
void             hal_dht_deinit(hal_dht_t h);
hal_status_t     hal_dht_read_ex(hal_dht_t h);
bool             hal_dht_read(hal_dht_t h);
float            hal_dht_get_temperature_c(hal_dht_t h);
float            hal_dht_get_temperature_f(hal_dht_t h);
float            hal_dht_get_humidity(hal_dht_t h);
hal_status_t     hal_dht_get_sample_ex(hal_dht_t h, hal_dht_sample_t *out);
bool             hal_dht_get_sample(hal_dht_t h, hal_dht_sample_t *out);
```

`hal_dht_init_ex()` to inicjalizator zwracający status i raportuje
nieprawidłową konfigurację jako `HAL_EINVAL`, a wyczerpanie puli/mutexu jako
`HAL_ENOMEM`; `hal_dht_init()` zachowuje historyczny kształt zwracający
uchwyt. `hal_dht_read_ex()` wykonuje impuls startowy DHT i odczyt 40-bitowej
ramki, waliduje sumę kontrolną i aktualizuje zbuforowaną próbkę tylko przy
powodzeniu. Zwraca `HAL_EUNINIT` dla nieprawidłowego uchwytu, `HAL_ETIMEOUT`
dla brakującej odpowiedzi/czasowania zbocza oraz `HAL_EPROTO` dla
niezgodności sumy kontrolnej; `hal_dht_read()` to historyczny wrapper `bool`.

Implementacja zachowuje przepływ czasowy Bonezegei DHT: 250 ms ustalania
stanu wysokiego w bezczynności, 18 ms impuls startowy hosta w stanie niskim,
40 us zwolnienia, czasowanie odpowiedzi 80/80 us oraz dyskryminator bitu
30 us. Ramki DHT11 są dekodowane jako bajty całkowitej wilgotności i
dziesiętnej temperatury; ramki DHT22 wykorzystują natywne 16-bitowe pole
wilgotności oraz 16-bitowe ze znakiem pole temperatury z rozdzielczością 0,1
jednostki.

`hal_dht_get_sample_ex()` kopiuje zbuforowaną próbkę z raportowaniem błędów
`HAL_EUNINIT` / `HAL_EINVAL`; `hal_dht_get_sample()` to wrapper zgodności.
Skalarne gettery z pamięci podręcznej zachowują swój historyczny kształt
zwracania wartości z bezpiecznym fallbackiem.

**impl/rp2040 + impl/stm32g474 + impl/.mock:** wszystkie wykorzystują
`hal/temperature/dht/hal_dht.cpp` po prymitywach HAL GPIO/system/sync.
**Thread safety:** tworzenie uchwytu używa mutexu singletonowej puli
utworzonego przez `jh_hal_mutex_create_once`; każdy uchwyt ma własny mutex
dla odczytu/pobierania/deinicjalizacji. Odczyt ramki krytyczny czasowo
maskuje przerwania tylko na czas krótkiego okna bit-bang DHT.

---

## `hal_bh1750` - czujnik natężenia światła otoczenia BH1750  *(opcjonalny - `HAL_ENABLE_BH1750`)*

```c
#include <hal/sensors/hal_bh1750.h>

#define HAL_BH1750_I2C_ADDR_LOW      0x23u
#define HAL_BH1750_I2C_ADDR_HIGH     0x5Cu
#define HAL_BH1750_I2C_ADDR_DEFAULT  HAL_BH1750_I2C_ADDR_HIGH

typedef struct {
  uint8_t i2c_bus;   // 0 = domyślny kontroler, 1 = drugi kontroler
  uint8_t i2c_addr;  // 7-bitowy adres BH1750
} hal_bh1750_config_t;

typedef struct {
  hal_bh1750_config_t cfg;
  bool initialized;
  hal_mutex_t mutex;
} hal_bh1750_t;

hal_bh1750_config_t hal_bh1750_default_config(void);
hal_status_t hal_bh1750_init_ex(hal_bh1750_t *dev,
                                const hal_bh1750_config_t *cfg);
bool  hal_bh1750_init(hal_bh1750_t *dev, const hal_bh1750_config_t *cfg);
void  hal_bh1750_deinit(hal_bh1750_t *dev);
hal_status_t hal_bh1750_light_ex(hal_bh1750_t *dev, float *out_lux);
float hal_bh1750_light(hal_bh1750_t *dev);
```

`hal_bh1750_init_ex()` wysyła komendę `0x10` (tryb ciągły o wysokiej
rozdzielczości - H-resolution), czeka 180 ms na pierwszy pomiar i zwraca
`HAL_OK` tylko, gdy urządzenie potwierdzi (ACK) komendę. `hal_bh1750_light_ex()`
odczytuje dokładnie dwa bajty i zwraca lux jako `raw / 1.2f` przez
`out_lux`; nieudane odczyty zwracają `HAL_EBUS` i ustawiają wyjście na
`-1.0f`. Historyczne wrappery `hal_bh1750_init()` i `hal_bh1750_light()`
zachowują oryginalne zachowanie `bool` / `-1.0f`.

**wspólna implementacja tematyczna:** `hal/sensors/bh1750/hal_bh1750.cpp` jest
używana przez RP2040, STM32G474 oraz testy mock. Domyślny adres to `0x5C`,
aby zachować domyślną wartość konstruktora drivera źródłowego; płytki z
ADDR podpiętym do masy powinny ustawić `0x23`.
**Thread safety:** mutex per-instancja serializuje wywołania
drivera; odczyty bajtów I2C używają `hal_i2c_read_bytes_bus()`, więc
żądanie i kopiowanie próbki pozostają wewnątrz mutexu magistrali.

---

## `hal_adp5360` - PMIC ADP5360  *(opcjonalny - `HAL_ENABLE_ADP5360`)*

```c
#include <hal/power/hal_adp5360.h>

#define HAL_ADP5360_I2C_ADDR_DEFAULT 0x46u
#define HAL_ADP5360_DEVICE_ID        0x10u

hal_adp5360_config_t hal_adp5360_default_config(void);
hal_status_t hal_adp5360_init_ex(hal_adp5360_t *dev,
                                 const hal_adp5360_config_t *cfg);
bool hal_adp5360_init(hal_adp5360_t *dev, const hal_adp5360_config_t *cfg);
void hal_adp5360_deinit(hal_adp5360_t *dev);

hal_status_t hal_adp5360_shipment_mode_enable(hal_adp5360_t *dev);
hal_status_t hal_adp5360_software_reset(hal_adp5360_t *dev);
hal_status_t hal_adp5360_hardware_reset(hal_adp5360_t *dev);

hal_status_t hal_adp5360_charger_enable(hal_adp5360_t *dev, bool enable);
hal_status_t hal_adp5360_fuel_gauge_get_soc_pct(hal_adp5360_t *dev,
                                                uint8_t *out_pct);
hal_status_t hal_adp5360_fuel_gauge_get_voltage_uv(hal_adp5360_t *dev,
                                                   uint32_t *out_uv);
hal_status_t hal_adp5360_regulator_set_voltage(hal_adp5360_t *dev,
                                               hal_adp5360_regulator_t reg,
                                               int32_t min_uv,
                                               int32_t max_uv);
```

Wspólny driver ADP5360 jest wzorowany na działających driverach Zephyr
ADP5360 MFD, ładowarki, fuel-gauge i regulatora, ale zależy wyłącznie od
JaszczurHAL. `hal_adp5360_init_ex()` sonduje ID urządzenia `0x10`, programuje
opcje nadzorczego resetu/watchdoga, czyści rejestry statusu przerwań i
opcjonalnie stosuje sekcje konfiguracyjne ładowarki, fuel-gauge, BUCK i
BUCK-BOOST z `hal_adp5360_config_t`.

API runtime udostępnia zachowanie pochodzące z Zephyr jako wywołania
`hal_status_t`: tryb wysyłkowy (shipment mode), reset programowy/sprzętowy,
sterowanie online/status/kondycją/prądem ładowarki, odczyty i zapisy
SOC/napięcia/pojemności/alarmu fuel-gauge oraz sterowanie
napięciem/prądem/trybem/włączeniem/aktywnym rozładowaniem regulatora.
Niskopoziomowi pomocnicy `hal_adp5360_reg_read/write/burst/update()` są
publiczni na potrzeby uruchamiania płytki (bring-up) i diagnostyki.

**wspólna implementacja tematyczna:** `hal/power/adp5360/hal_adp5360.cpp` jest
używana przez RP2040, STM32G474 oraz testy mock. Wykorzystuje prymitywy HAL
I2C/GPIO/czasu oraz mutex per-urządzenie utworzony przez
`jh_hal_mutex_create_once()`, więc driver jest bezpieczny do wywoływania z
kontekstów zadań wielordzeniowych/FreeRTOS, gdy leżący u podstaw backend HAL
I2C jest zainicjalizowany.

Obecny zakres celowo nie obejmuje rejestracji callbacków przerwań GPIO w
stylu Zephyr dla pinów ADP5360 INT/PGOOD/reset-status.

---

## `hal_tsc2007` - rezystancyjny kontroler dotyku TSC2007  *(opcjonalny - `HAL_ENABLE_TSC2007`)*

```c
#include <hal/input/hal_tsc2007.h>

#define HAL_TSC2007_I2C_ADDR_DEFAULT      0x48u
#define HAL_TSC2007_TOUCH_INVALID         4095u
#define HAL_TSC2007_STABILITY_THRESHOLD   100u

typedef struct {
  uint8_t i2c_bus;   // 0 = domyślny kontroler, 1 = drugi kontroler
  uint8_t i2c_addr;  // 7-bitowy adres TSC2007
} hal_tsc2007_config_t;

typedef struct {
  int16_t x;
  int16_t y;
  int16_t z;         // próbka ciśnienia Z1
} hal_tsc2007_point_t;

typedef struct {
  hal_tsc2007_config_t cfg;
  bool initialized;
  hal_mutex_t mutex;
} hal_tsc2007_t;

hal_tsc2007_config_t hal_tsc2007_default_config(void);
hal_status_t hal_tsc2007_init_ex(hal_tsc2007_t *dev,
                                 const hal_tsc2007_config_t *cfg);
bool hal_tsc2007_init(hal_tsc2007_t *dev, const hal_tsc2007_config_t *cfg);
void hal_tsc2007_deinit(hal_tsc2007_t *dev);

hal_status_t hal_tsc2007_command_ex(hal_tsc2007_t *dev,
                                    hal_tsc2007_function_t func,
                                    hal_tsc2007_power_t pwr,
                                    hal_tsc2007_resolution_t res,
                                    uint16_t *out_value);
uint16_t hal_tsc2007_command(hal_tsc2007_t *dev,
                             hal_tsc2007_function_t func,
                             hal_tsc2007_power_t pwr,
                             hal_tsc2007_resolution_t res);

hal_status_t hal_tsc2007_read_touch_ex(hal_tsc2007_t *dev, uint16_t *x,
                                       uint16_t *y, uint16_t *z1,
                                       uint16_t *z2);
bool hal_tsc2007_read_touch(hal_tsc2007_t *dev, uint16_t *x, uint16_t *y,
                            uint16_t *z1, uint16_t *z2);
hal_tsc2007_point_t hal_tsc2007_get_point(hal_tsc2007_t *dev);
```

`hal_tsc2007_init_ex()` sonduje adres 7-bitowy i wysyła tę samą początkową
komendę `MEASURE_TEMP0` / `POWERDOWN_IRQON` / 12-bitową co driver
źródłowy. `hal_tsc2007_command_ex()` buduje bajt komendy jako
`(function << 4) | (power << 2) | (resolution << 1)`, czeka 500 us,
odczytuje dokładnie dwa bajty i zwraca 12-bitową wartość zdekodowaną z
górnych bitów odpowiedzi przez `out_value`. Historyczny wrapper
`hal_tsc2007_command()` nadal zwraca `0` przy niepowodzeniu.

`hal_tsc2007_read_touch_ex()` wykonuje ustaloną sekwencję: `Z1`, `Z2`, `X`,
`Y`, duplikat `X`, duplikat `Y`, a następnie `MEASURE_TEMP0` z power-down.
Próbka X/Y jest akceptowana tylko wtedy, gdy zduplikowane pomiary mieszczą
się w granicach `HAL_TSC2007_STABILITY_THRESHOLD`, a żadna z zaakceptowanych
współrzędnych nie jest równa `HAL_TSC2007_TOUCH_INVALID`. Odrzucone próbki
zwracają `HAL_ENOENT`, niepowodzenia transakcji I2C zwracają `HAL_EBUS`, a
nieprawidłowe argumenty zwracają `HAL_EINVAL`. `hal_tsc2007_read_touch()`
zachowuje historyczny kształt `bool`, a `hal_tsc2007_get_point()` zwraca
`{x, y, z1}` lub `{0, 0, 0}`, gdy próbka jest odrzucona.

**wspólna implementacja tematyczna:** `hal/input/tsc2007/tsc2007.cpp` jest
używana przez RP2040, STM32G474 oraz testy mock po HAL I2C i czasowaniu
systemowym HAL.
**Thread safety:** mutex per-instancja serializuje publiczne
wywołania drivera i jest tworzony wspólnym pomocnikiem create-once, więc
pierwszy dostęp jest bezpieczny pod FreeRTOS/wielordzeniowością RP2040.
`hal_tsc2007_deinit()` nie powinno być wywoływane współbieżnie z innymi
operacjami na tej samej instancji.

---

## `hal_stmpe610` - rezystancyjny kontroler dotyku STMPE610  *(opcjonalny - `HAL_ENABLE_STMPE610`)*

```c
#include <hal/input/hal_stmpe610.h>

#define HAL_STMPE610_I2C_ADDR_DEFAULT 0x41u
#define HAL_STMPE610_CHIP_ID          0x0811u
#define HAL_STMPE610_SPI_CLOCK_HZ     1000000ul

typedef enum {
  HAL_STMPE610_TRANSPORT_I2C,
  HAL_STMPE610_TRANSPORT_SPI,
  HAL_STMPE610_TRANSPORT_SOFT_SPI,
} hal_stmpe610_transport_t;

typedef struct {
  hal_stmpe610_transport_t transport;
  uint8_t i2c_bus;
  uint8_t i2c_addr;
  uint8_t spi_bus;
  uint8_t cs_pin;
  uint8_t mosi_pin;
  uint8_t miso_pin;
  uint8_t sck_pin;
} hal_stmpe610_config_t;

typedef struct {
  int16_t x;
  int16_t y;
  int16_t z;
} hal_stmpe610_point_t;

hal_stmpe610_config_t hal_stmpe610_default_config(void);
hal_stmpe610_config_t hal_stmpe610_i2c_config(uint8_t bus, uint8_t addr);
hal_stmpe610_config_t hal_stmpe610_spi_config(uint8_t bus, uint8_t cs_pin);
hal_stmpe610_config_t hal_stmpe610_soft_spi_config(uint8_t cs_pin,
                                                   uint8_t mosi_pin,
                                                   uint8_t miso_pin,
                                                   uint8_t sck_pin);

bool hal_stmpe610_init(hal_stmpe610_t *dev, const hal_stmpe610_config_t *cfg);
hal_status_t hal_stmpe610_init_ex(hal_stmpe610_t *dev,
                                  const hal_stmpe610_config_t *cfg);
void hal_stmpe610_deinit(hal_stmpe610_t *dev);

bool hal_stmpe610_touched(hal_stmpe610_t *dev);
bool hal_stmpe610_buffer_empty(hal_stmpe610_t *dev);
uint8_t hal_stmpe610_buffer_size(hal_stmpe610_t *dev);
hal_status_t hal_stmpe610_read_data_ex(hal_stmpe610_t *dev, uint16_t *x,
                                       uint16_t *y, uint8_t *z);
hal_status_t hal_stmpe610_read_data(hal_stmpe610_t *dev, uint16_t *x,
                                    uint16_t *y, uint8_t *z);
hal_stmpe610_point_t hal_stmpe610_get_point(hal_stmpe610_t *dev);

hal_status_t hal_stmpe610_read_register8_ex(hal_stmpe610_t *dev, uint8_t reg,
                                            uint8_t *out_value);
uint8_t hal_stmpe610_read_register8(hal_stmpe610_t *dev, uint8_t reg);
hal_status_t hal_stmpe610_read_register16_ex(hal_stmpe610_t *dev, uint8_t reg,
                                             uint16_t *out_value);
uint16_t hal_stmpe610_read_register16(hal_stmpe610_t *dev, uint8_t reg);
hal_status_t hal_stmpe610_write_register8(hal_stmpe610_t *dev, uint8_t reg,
                                          uint8_t value);
```

`hal_stmpe610_init_ex()` sonduje ID chipu `0x0811`, zachowuje oryginalny
fallback do trybu 1 sprzętowego SPI, gdy tryb 0 nie odpowiada, a następnie
wykonuje ustaloną sekwencję konfiguracji kontrolera dotyku: soft reset,
10 ms oczekiwania, odczyty rejestrów typu flush, włączenie TSC, włączenie
przerwania dotyku, konfigurację czasowania ADC/TSC, próg/reset FIFO, prąd
sterujący 50 mA i czyszczenie statusu przerwań. Raportuje błędne
argumenty/konfigurację jako `HAL_EINVAL`, niepowodzenie alokacji jako
`HAL_ENOMEM`, a niezgodność ID chipu jako `HAL_ENOENT`; `hal_stmpe610_init()`
pozostaje historycznym wrapperem `bool`.

`hal_stmpe610_read_data_ex()` odczytuje cztery bajty z portu danych FIFO i
dekoduje 12-bitowe X/Y oraz 8-bitowe ciśnienie, zwracając `HAL_EUNINIT` dla
niezainicjalizowanej instancji i `HAL_EINVAL` dla błędnych wskaźników
wyjściowych. `hal_stmpe610_read_data()` bezpośrednio zwraca status; wywołujący,
którzy dotąd ignorowali poprzedni wynik `void`, mogą nadal go ignorować.
`hal_stmpe610_read_register8_ex()` i `hal_stmpe610_read_register16_ex()`
raportują niepowodzenia odczytu rejestru przez parametry wyjściowe, podczas
gdy historyczne wrappery zwracające wartość zachowują swój dawny kształt
zero-przy-niepowodzeniu. `hal_stmpe610_write_register8()` zwraca status w
miejscu.
`hal_stmpe610_get_point()` opróżnia FIFO, zwraca ostatnią próbkę i czyści
status przerwań, gdy FIFO jest puste. Ścieżka 16-bitowego odczytu rejestru
I2C jest kierowana wyłącznie przez I2C; unika to błędu przepadania
(fall-through) transportu obecnego w imporcie źródłowym.

**wspólna implementacja tematyczna:** `hal/input/stmpe610/stmpe610.cpp` jest
używana przez RP2040, STM32G474 oraz testy mock. I2C używa transferów HAL z
wyborem magistrali; sprzętowe SPI używa transakcji HAL SPI plus pinu CS
dostarczonego przez wywołującego; miękkie SPI (soft SPI) wykonuje bit-bang
MSB-first po HAL GPIO.
**Thread safety:** mutex per-instancja serializuje publiczne
wywołania drivera i jest tworzony wspólnym pomocnikiem create-once, więc
pierwszy dostęp jest bezpieczny pod FreeRTOS/wielordzeniowością RP2040.
Transakcje sprzętowego SPI dodatkowo blokują magistralę HAL SPI, gdy CS jest
aktywne. `hal_stmpe610_deinit()` nie powinno być wywoływane współbieżnie z
innymi operacjami na tej samej instancji.

---

## `hal_irsmall_decoder` - dekoder odbiornika IR  *(opcjonalny - `HAL_ENABLE_IRSMALL_DECODER`)*

```c
#include <hal/input/hal_irsmall_decoder.h>

typedef enum {
  HAL_IRSMALL_PROTOCOL_NEC,
  HAL_IRSMALL_PROTOCOL_NECX,
  HAL_IRSMALL_PROTOCOL_RC5,
  HAL_IRSMALL_PROTOCOL_SIRC12,
  HAL_IRSMALL_PROTOCOL_SIRC15,
  HAL_IRSMALL_PROTOCOL_SIRC20,
  HAL_IRSMALL_PROTOCOL_SIRC,
  HAL_IRSMALL_PROTOCOL_SAMSUNG,
  HAL_IRSMALL_PROTOCOL_SAMSUNG32,
} hal_irsmall_protocol_t;

typedef struct {
  hal_irsmall_protocol_t protocol;
  uint8_t input_pin;
  bool timeout_enabled;
  hal_irq_priority_t irq_priority;
} hal_irsmall_decoder_config_t;

typedef struct {
  hal_irsmall_protocol_t protocol;
  uint16_t addr;
  uint8_t cmd;
  uint8_t ext;
  bool key_held;
  uint8_t bits;
} hal_irsmall_decoder_data_t;

hal_irsmall_decoder_config_t
hal_irsmall_decoder_default_config(uint8_t input_pin,
                                   hal_irsmall_protocol_t protocol);

bool hal_irsmall_decoder_init(hal_irsmall_decoder_t *dev,
                              const hal_irsmall_decoder_config_t *cfg);
void hal_irsmall_decoder_deinit(hal_irsmall_decoder_t *dev);
void hal_irsmall_decoder_enable(hal_irsmall_decoder_t *dev);
void hal_irsmall_decoder_disable(hal_irsmall_decoder_t *dev);
void hal_irsmall_decoder_reset(hal_irsmall_decoder_t *dev);
bool hal_irsmall_decoder_data_available(hal_irsmall_decoder_t *dev,
                                        hal_irsmall_decoder_data_t *out);
bool hal_irsmall_decoder_has_data(hal_irsmall_decoder_t *dev);
```

`hal_irsmall_decoder_init()` konfiguruje wejście jako pull-up, podłącza
przerwanie GPIO z trybem zbocza używanym przez wybrany protokół i
wykorzystuje interwały `hal_micros()` do dekodowania ramek NEC, NEC
extended, RC5, Sony SIRC 12/15/20-bit, Sony SIRC triple-frame, Samsung
20-bit oraz Samsung 32-bit. `hal_irsmall_decoder_data_available()` kopiuje i
czyści jedną zdekodowaną ramkę; `hal_irsmall_decoder_has_data()` czyści
oczekujące dane bez ich kopiowania.

**wspólna implementacja tematyczna:** `hal/input/irsmall_decoder/irsmall_decoder.cpp`
jest używana przez RP2040, STM32G474 oraz testy mock po przerwaniach HAL
GPIO i czasowaniu systemowym HAL. Wspólna implementacja zachowuje progi
czasowe oraz zachowanie tłumienia powtórzeń (repeat suppression) ze źródła;
bajty adresu rozszerzonego NEC są składane jawnie, aby uniknąć odczytów
type-punned. Dekoder ramek RC5 wykorzystuje maszynę stanów tabeli przejść ze
sprawdzonego na RP2040 drivera `RC5`, ze wspólnym raportowaniem
`key_held` stosowanym po zdekodowaniu poprawnej ramki.
**Thread safety:** publiczne wywołania są serializowane mutexem
instancji utworzonym wspólnym pomocnikiem create-once. Odczyty znacznika
czasu/stanu współdzielone z ISR używają krótkich sekcji krytycznych dla
ścieżek timeout/reset. W tym samym czasie może być podłączonych do
`HAL_IRSMALL_DECODER_MAX_INSTANCES` instancji.

---

## `hal_rtc` - zegar czasu rzeczywistego  *(opcjonalny - `HAL_ENABLE_RTC`)*

Abstrakcja RTC oparta na uchwytach. Backendy obejmują PCF8563 i DS3231 po
I2C, RTC domeny podtrzymywanej (backup-domain) STM32G474 oraz zawsze
aktywny (always-on) timer RP2040/RP2350. API jest neutralne względem
providera i udostępnia generyczne sterowanie alarmem/timerem/wyjściem
zegarowym, diagnostykę źródła oraz zdarzenia/IRQ.

```c
#include <hal/rtc/hal_rtc.h>

#ifndef HAL_RTC_MAX_INSTANCES
#define HAL_RTC_MAX_INSTANCES 4
#endif

#define HAL_RTC_MIN_YEAR 1900u
#define HAL_RTC_MAX_YEAR 2099u

typedef struct hal_rtc_impl_s *hal_rtc_t;

typedef enum {
  HAL_RTC_CHIP_PCF8563 = 0,
  HAL_RTC_CHIP_DS3231,
  HAL_RTC_CHIP_INTERNAL,
} hal_rtc_chip_t;

typedef enum {
  HAL_RTC_CLOCK_SOURCE_AUTO = 0,
  HAL_RTC_CLOCK_SOURCE_EXTERNAL,
  HAL_RTC_CLOCK_SOURCE_LSE,
  HAL_RTC_CLOCK_SOURCE_LSI,
  HAL_RTC_CLOCK_SOURCE_HSE_DIV32,
  HAL_RTC_CLOCK_SOURCE_AON,
} hal_rtc_clock_source_t;

typedef struct {
  uint8_t  sda_pin;
  uint8_t  scl_pin;
  uint32_t clock_hz;
  uint8_t  i2c_bus;   // 0 = domyślna, 1 = drugi kontroler
  uint8_t  i2c_addr;  // 0 = domyślna backendu (0x51 PCF8563, 0x68 DS3231)
} hal_rtc_i2c_cfg_t;

typedef struct {
  hal_rtc_clock_source_t clock_source;
} hal_rtc_internal_cfg_t;

typedef struct {
  hal_rtc_chip_t chip;
  union {
    hal_rtc_i2c_cfg_t i2c;
    hal_rtc_internal_cfg_t internal;
  } bus;
} hal_rtc_config_t;

typedef struct {
  uint8_t  second;    // 0..59
  uint8_t  minute;    // 0..59
  uint8_t  hour;      // 0..23
  uint8_t  day;       // 1..liczba dni w wybranym miesiącu
  uint8_t  weekday;   // 0..6
  uint8_t  month;     // 1..12
  uint16_t year;      // HAL_RTC_MIN_YEAR..HAL_RTC_MAX_YEAR
  bool     clock_integrity;
} hal_rtc_datetime_t;

#define HAL_RTC_FLAG_ALARM (1u << 0)
#define HAL_RTC_FLAG_TIMER (1u << 1)
#define HAL_RTC_FLAG_WAKEUP (1u << 2)

#define HAL_RTC_IRQ_ALARM  (1u << 0)
#define HAL_RTC_IRQ_TIMER  (1u << 1)
#define HAL_RTC_IRQ_WAKEUP (1u << 2)

#define HAL_RTC_WAKEUP_LOW_POWER (1u << 0)

typedef struct {
  bool     armed;
  bool     pending;
  uint64_t requested_timeout_us;
  uint64_t programmed_timeout_us;
  uint64_t resolution_us;
  uint32_t flags;
} hal_rtc_wakeup_state_t;

typedef enum {
  HAL_RTC_CLKOUT_DISABLED = 0,
  HAL_RTC_CLKOUT_1_HZ,
  HAL_RTC_CLKOUT_32_HZ,
  HAL_RTC_CLKOUT_1024_HZ,
  HAL_RTC_CLKOUT_32768_HZ,
} hal_rtc_clkout_mode_t;

typedef enum {
  HAL_RTC_TIMER_DISABLED = 0,
  HAL_RTC_TIMER_1_60_HZ,
  HAL_RTC_TIMER_1_HZ,
  HAL_RTC_TIMER_64_HZ,
  HAL_RTC_TIMER_4096_HZ,
} hal_rtc_timer_clock_t;

typedef struct {
  bool    minute_enabled;
  uint8_t minute;
  bool    hour_enabled;
  uint8_t hour;
  bool    day_enabled;
  uint8_t day;
  bool    weekday_enabled;
  uint8_t weekday;
} hal_rtc_alarm_t;

hal_rtc_t hal_rtc_init(const hal_rtc_config_t *cfg);
void      hal_rtc_deinit(hal_rtc_t h);

bool hal_rtc_get_datetime(hal_rtc_t h, hal_rtc_datetime_t *out_dt);
bool hal_rtc_set_datetime(hal_rtc_t h, const hal_rtc_datetime_t *dt);
bool hal_rtc_get_clock_integrity(hal_rtc_t h, bool *out_ok);
bool hal_rtc_get_clock_source(hal_rtc_t h, hal_rtc_clock_source_t *out_source);

bool hal_rtc_set_interrupt_enable(hal_rtc_t h, uint8_t irq_mask);
bool hal_rtc_get_interrupt_enable(hal_rtc_t h, uint8_t *out_irq_mask);
bool hal_rtc_get_and_clear_flags(hal_rtc_t h, uint8_t *out_flags);

bool hal_rtc_set_clkout_mode(hal_rtc_t h, hal_rtc_clkout_mode_t mode);
bool hal_rtc_get_clkout_mode(hal_rtc_t h, hal_rtc_clkout_mode_t *out_mode);

bool hal_rtc_set_timer(hal_rtc_t h, hal_rtc_timer_clock_t timer_clock, uint8_t count);
bool hal_rtc_get_timer(hal_rtc_t h, hal_rtc_timer_clock_t *out_timer_clock, uint8_t *out_count);

bool hal_rtc_set_alarm(hal_rtc_t h, const hal_rtc_alarm_t *alarm);
bool hal_rtc_get_alarm(hal_rtc_t h, hal_rtc_alarm_t *out_alarm);

hal_status_t hal_rtc_wakeup_arm_ex(hal_rtc_t h, uint64_t timeout_us,
                                   uint32_t flags);
hal_status_t hal_rtc_wakeup_cancel_ex(hal_rtc_t h);
hal_status_t hal_rtc_wakeup_get_state_ex(hal_rtc_t h,
                                         hal_rtc_wakeup_state_t *out_state);

// Pomocnicy epoki uniksowej (sekundy od 1970-01-01 UTC)
bool hal_rtc_get_epoch(hal_rtc_t h, uint64_t *out_epoch);
bool hal_rtc_set_epoch(hal_rtc_t h, uint64_t epoch);

// Temperatura na chipie (tylko DS3231)
bool hal_rtc_get_temperature(hal_rtc_t h, float *out_temperature_c);
```

**Architektura:** `src/hal/rtc/hal_rtc.cpp` jest jedyną publiczną fasadą.
Jest właścicielem statycznej puli uchwytów, walidacji
konfiguracji/daty/alarmu, blokowania per-uchwyt, konwersji epoki,
propagacji statusu oraz historycznych wrapperów `bool`/uchwyt. Wewnętrzne
operacje providera oddzielają ten cykl życia od zachowania backendu:

- **PCF8563:** bezpośredni dostęp do rejestrów przez wspólnego providera I2C
  (data-czas, integralność zegara/bit VL, pola alarmu, tryb+licznik timera,
  tryb CLKOUT, maska włączenia przerwań i odczytywane-i-czyszczone flagi
  zdarzeń).
- **DS3231:** wspólny przenośny driver przez wspólnego providera I2C z
  data-czasem, obsługą integralności zegara OSF, mapowaniem alarmu/IRQ
  wykorzystującym Alarm2, temperaturą oraz częściowym mapowaniem CLKOUT
  (`1 Hz`, `1,024 kHz`, `32,768 kHz`). Zapisy daty aktualizują pełny
  kalendarz i czyszczą OSF dopiero po udanym dostępie I2C; wyłączenie CLKOUT
  pozostawia włączone podtrzymywane bateryjnie śledzenie czasu. Niepowodzenia
  I2C providera propagują jako `HAL_EIO`. Funkcje timera oraz
  `HAL_RTC_CLKOUT_32_HZ` nie są obsługiwane.
- **Wewnętrzny RTC STM32G474:** kalendarz domeny podtrzymywanej (backup
  domain) na poziomie rejestrów, wykorzystujący LSE lub LSI, zachowywaną
  integralność czasu, odpytywanie Alarmu A i dostarczanie IRQ przez linię
  EXTI 18, jednorazowy względny timer wybudzania przez linię EXTI 20 / IRQ 3
  oraz tryby wyjścia kalibracyjnego wyłączony/1 Hz. Względne wartości
  timeout są zaokrąglane w górę do pełnych sekund i akceptują wartości do
  65 536 sekund. Obsługuje lata 2000..2099. Temperatura, generyczny timer
  odliczający oraz pozostałe częstotliwości CLKOUT zwracają
  `HAL_EUNSUPPORTED`; dzień i dzień tygodnia nie mogą być wybrane
  jednocześnie w jednym dopasowaniu Alarmu A.
- **Wewnętrzny RTC RP2040/RP2350:** provider Pico SDK `pico_aon_timer`.
  RP2040 wykorzystuje swój sprzętowy kalendarzowy RTC; RP2350 wykorzystuje
  liniowy zawsze aktywny timer Powman. Backend obsługuje zapytania o
  datę/czas, epokę, integralność oraz źródło i raportuje
  `HAL_RTC_CLOCK_SOURCE_AON`. Jego względny alarm wybudzenia zaokrągla do
  rozdzielczości sprzętu (jedna sekunda na RP2040 i jedna milisekunda na
  RP2350) i może żądać trasowania wybudzenia niskiego poboru mocy.
  Generyczne alarmy kalendarzowe, timery odliczające, temperatura oraz
  aktywne tryby CLKOUT zwracają `HAL_EUNSUPPORTED`.
- **Provider mock:** stan w pamięci z deterministycznym wstrzykiwaniem do
  testów jednostkowych; nie zawiera fasady, walidacji, kalendarza, puli ani
  kopii mutexu.

Względne wybudzenie jest operacją providera natywną dla targetu. Uzbrojenie
zastępuje każde poprzednie zdarzenie względne na tym samym wewnętrznym RTC,
raportuje żądany i zaokrąglony timeout przez `hal_rtc_wakeup_get_state_ex()`
i zachowuje się jako jednorazowe na granicy HAL. `hal_rtc_get_and_clear_flags_ex()`
odczytuje i czyści oczekującą flagę `HAL_RTC_FLAG_WAKEUP`; anulowanie również ją
czyści. Zewnętrzni providerzy I2C zwracają `HAL_EUNSUPPORTED`.
`HAL_RTC_WAKEUP_LOW_POWER` żąda trasowania backendu potrzebnego przez
głębszy stan, ale nie powoduje wejścia w ten stan.

Fasada i providerzy chipów wykorzystują wspólny rdzeń kalendarza
gregoriańskiego, który odrzuca niemożliwe daty, takie jak 31 kwietnia oraz
29 lutego w roku nieprzestępnym. Konwersja uniksowa akceptuje daty od
1970-01-01 do 2099-12-31 i raportuje `HAL_EOVERFLOW` poza tym zakresem.
Sprzętowy kalendarz STM32G474 zawęża akceptowany zakres do 2000-01-01 -
2099-12-31. Liniowa konwersja RP2350 akceptuje daty od epoki uniksowej do
2099-12-31.

Dla wewnętrznego backendu STM32G474, `HAL_RTC_CLOCK_SOURCE_AUTO` zachowuje
już wybrane, obsługiwane źródło LSE/LSI w domenie podtrzymywanej. Na świeżej
domenie czeka na LSE i wykorzystuje LSI jako ograniczony w czasie fallback.
Jawne żądania `LSE` i `LSI` nigdy nie resetują domeny podtrzymywanej;
żądanie sprzeczne z zachowanym źródłem zwraca `HAL_EBUSY`. `HSE_DIV32`
należy do przenośnej enumeracji źródeł, ale nie jest obsługiwane przez
obecnego providera STM32G474. Targety RP akceptują `AUTO` lub `AON`; oba
rozwiązują się do `AON` bez I2C.

Provider STM32G474 rezerwuje rejestr podtrzymywany (backup) TAMP nr 31 dla
swojego znacznika ważności zegara `JHRT`. Nadpisz
`HAL_STM32_RTC_BACKUP_REGISTER_INDEX`, gdy aplikacja jest właścicielem tego
rejestru. `HAL_STM32_RTC_LSE_STARTUP_TIMEOUT_MS` oraz
`HAL_STM32_RTC_LSI_STARTUP_TIMEOUT_MS` konfigurują oczekiwania na oscylator.
API względnego wybudzenia konfiguruje wyłącznie źródło RTC; nigdy nie
zmienia stanu zasilania MCU. Użyj oddzielnie bramkowanego API `hal_power`
dla pełnego przejścia Sleep/STOP/Standby.

Provider RP pozostawia działający zegar AON nietkniętym przy
deinicjalizacji i przywraca go po resecie, gdy sprzęt nadal raportuje
działający, możliwy do zdekodowania kalendarz. Płytki RP nie mają zasilania
RTC podtrzymywanego baterią, więc jest to zachowanie ciepłego resetu (warm
reset), gdy zasilanie pozostaje dostępne, a nie gwarantowane śledzenie
czasu w przypadku utraty zasilania. Ustawienie pierwszej ważnej daty
uruchamia timer AON.

```c
const hal_rtc_config_t cfg = {
    .chip = HAL_RTC_CHIP_INTERNAL,
    .bus.internal.clock_source = HAL_RTC_CLOCK_SOURCE_AUTO,
};

hal_rtc_t rtc = NULL;
if (hal_rtc_init_ex(&cfg, &rtc) == HAL_OK) {
    hal_rtc_clock_source_t source;
    (void)hal_rtc_get_clock_source_ex(rtc, &source);
}
```

**Thread safety:** wspólna fasada serializuje każde wywołanie
providera w runtime mutexem per uchwyt; ruch I2C jest dodatkowo
chroniony mutexem magistrali `hal_i2c`. Tworzenie/niszczenie przestrzega
projektowej polityki init/deinit jednordzeniowego. Każdy obsługiwany MCU
udostępnia jeden wewnętrzny zasób RTC/AON, więc drugi jednoczesny uchwyt
wewnętrzny zwraca `HAL_EBUSY`. Provider mock pozostaje przeznaczony do
deterministycznych testów jednowątkowych.

**Pomocnicy mock:**
```c
void hal_mock_rtc_set_datetime(hal_rtc_t h, const hal_rtc_datetime_t *dt);
void hal_mock_rtc_set_clock_integrity(hal_rtc_t h, bool ok);
void hal_mock_rtc_set_flags(hal_rtc_t h, uint8_t flags);
void hal_mock_rtc_fire_wakeup(hal_rtc_t h);
```

**Warianty `_ex` zwracające status:** każda operacja mogąca zakończyć się błędem
uchwyt/`bool` powyżej ma dodatkowy odpowiednik `_ex` zwracający
`hal_status_t` (zobacz [API statusów](01_status_api.md)). `hal_rtc_init_ex()`
produkuje uchwyt przez parametr wyjściowy. `hal_rtc_deinit()` to niezawodne
(infallible) czyszczenie, pozostaje `void` i celowo nie ma towarzyszącej
funkcji `_ex`.

Wspólna fasada raportuje nieprawidłowe argumenty lub konfigurację
(`HAL_EINVAL`), wyczerpanie puli/mutexu (`HAL_ENOMEM`) oraz konwersję epoki
uniksowej poza zakresem 1970..2099 (`HAL_EOVERFLOW`). Providerzy raportują
nieobsługiwane chipy lub funkcje chipu (`HAL_EUNSUPPORTED`), konflikty
zachowanego źródła (`HAL_EBUSY`), niekompatybilny zachowany kalendarz
12-godzinny (`HAL_ECONFIG`), ograniczone czasowo oczekiwania na
uruchomienie/rejestr (`HAL_ETIMEOUT`) oraz niepowodzenia backendu
(`HAL_EIO`). Status inicjalizacji magistrali I2C jest propagowany bez zmian.

```c
hal_rtc_t rtc = NULL;
hal_status_t st = hal_rtc_init_ex(&cfg, &rtc);
// HAL_OK -> uchwyt gotowy, HAL_EINVAL -> błędne argumenty,
// HAL_ENOMEM -> pula/mutex wyczerpane, HAL_EIO -> niepowodzenie sondowania/magistrali
if (st != HAL_OK) {
    return;
}

uint64_t epoch = 0;
if (hal_rtc_get_epoch_ex(rtc, &epoch) == HAL_OK) {
    use(epoch);              // HAL_EOVERFLOW, jeśli data RTC jest poza zakresem uniksowym
}
```

---


## `hal_external_adc` - zewnętrzny ADC ADS1115  *(opcjonalny - `HAL_ENABLE_EXTERNAL_ADC`)*

```c
#include <hal/analog/hal_external_adc.h>

// Inicjalizuje ADS1115 pod podanym 7-bitowym adresem I2C.
// adc_range: rozmiar LSB w miliwoltach (np. 0.1875 dla zakresu pełnoskalowego ±6,144 V).
//            Zapisywany wewnętrznie dla hal_ext_adc_read_scaled().
void    hal_ext_adc_init(uint8_t address, float adc_range);
void    hal_ext_adc_init_bus(uint8_t i2c_bus, uint8_t address, float adc_range); // i2c_bus: 0=domyślny, 1=drugi kontroler

// Odczytuje surową wartość 16-bitową ze znakiem z kanału 0-3.
// Ustawia wzmocnienie na 0 (±6,144 V) przed każdą konwersją; blokuje do gotowości wyniku.
int16_t hal_ext_adc_read(uint8_t channel);

// Odczytuje kanał i zwraca (raw * adc_range) / 1000.0f.
// Nałóż dalsze korekty specyficzne dla projektu (dzielnik napięcia itd.) na wynik.
float   hal_ext_adc_read_scaled(uint8_t channel);
```

**wspólna implementacja tematyczna:** driver ADS1X15/ADS1115 dostępny
wyłącznie w HAL po HAL I2C, używany przez RP2040 i STM32G474.
**Thread safety:** RP2040/STM32G474: thread-safe i
wielordzeniowo tam, gdzie zapewnia to implementacja mutexu backendu.
Dedykowany wewnętrzny `hal_mutex_t` serializuje wybór kanału ADC i dostęp do
zakresu; transakcje HAL I2C chronią magistralę. `hal_ext_adc_init()` /
`hal_ext_adc_init_bus()` modyfikują globalny stan singletonowy i powinny być
wywoływane podczas inicjalizacji. Backend mock nie synchronizuje
współbieżnego dostępu.

**Pomocnicy mock:**
```c
void  hal_mock_ext_adc_inject_raw(uint8_t channel, int16_t value);   // wstrzykuje surowy wynik 16-bitowy dla kanału 0-3
void  hal_mock_ext_adc_inject_scaled(uint8_t channel, float value);  // wstrzykuje przeskalowaną wartość float dla kanału 0-3
float hal_mock_ext_adc_get_range(void);                               // zwraca adc_range ustawiony przez hal_ext_adc_init()
```

---

## `hal_gps` - odbiornik GPS NMEA  *(opcjonalny - `HAL_ENABLE_GPS`)*

Singletonowy podsystem GPS. Jedna fasada niezależna od targetu zasila
przenośny wewnętrzny silnik NMEA z HAL UART lub SoftwareSerial, wybierany w
czasie buildu. Mock obsługuje dokładne wstrzykiwanie pól oraz surowe
wejście NMEA przez ten sam silnik i publiczne gettery.

**Automatyczne wykrywanie ramkowania SoftwareSerial:** Po odebraniu ~500
znaków, jeśli każda ramka NMEA nie przeszła sumy kontrolnej, ścieżka
SoftwareSerial przełącza się między 8N1 i 7N1 oraz ponownie inicjalizuje
port jednorazowo. Rozwiązuje to problem z prawdziwymi modułami u-blox (8N1)
oraz klonowanymi płytkami NEO-6M, które są dostarczane jako 7N1.

```c
#include <hal/gps/hal_gps.h>

// Inicjalizuje podsystem GPS (pierwsze udane wywołanie sprzętowe zaczyna obowiązywać).
// config: format ramki UART - HAL_UART_CFG_8N1 (zalecana wartość domyślna) lub
//         HAL_UART_CFG_7N1. Transport SoftwareSerial może wypróbować alternatywne
//         ramkowanie po powtarzających się niepowodzeniach sumy kontrolnej.
void hal_gps_init(uint8_t rx_pin, uint8_t tx_pin, uint32_t baud, uint16_t config);

// Tymczasowo zwalnia transport GPS, zachowując sparsowany fix.
// Obie operacje są idempotentne i zwracają wynik hal_status_t.
hal_status_t hal_gps_pause(void);
hal_status_t hal_gps_resume(void);

// Odsącza dostępne bajty szeregowe do parsera.
// Musi być wywoływana często (zwykle w każdej iteracji pętli głównej), aby
// przenosić zbuforowane dane transportu do parsera NMEA.
// No-op w buildzie mock - użyj bezpośrednio pomocników wstrzykujących.
void hal_gps_update(void);

// Podaje jeden surowy bajt NMEA do parsera ręcznie (alternatywa dla hal_gps_update).
// Testy mock mogą użyć tej ścieżki, aby przećwiczyć cały wspólny silnik.
void hal_gps_encode(char c);

// Stan fix
bool     hal_gps_location_is_valid(void);    // true, gdy dostępny jest ważny fix
bool     hal_gps_location_is_updated(void);  // true, gdy od ostatniego zapytania przyszły nowe dane
uint32_t hal_gps_location_age(void);         // ms od ostatniego ważnego fix; UINT32_MAX, jeśli brak fix

// Pozycja
double hal_gps_latitude(void);   // stopnie, ujemne = południe
double hal_gps_longitude(void);  // stopnie, ujemne = zachód

// Prędkość
double hal_gps_speed_kmph(void); // prędkość względem podłoża w km/h; 0,0, gdy brak fix

// Rozszerzone dane fix (z ramek GGA/GSA/GSV)
double   hal_gps_altitude_m(void);              // wysokość nad MSL, metry
double   hal_gps_course_deg(void);              // kurs nad podłożem, stopnie
uint32_t hal_gps_satellites_used(void);         // satelity użyte w fix
uint8_t  hal_gps_satellites_in_view(void);      // satelity w zasięgu widoczności
double   hal_gps_hdop(void);                    // pozioma dylucja precyzji (HDOP)
double   hal_gps_vdop(void);                    // pionowa dylucja precyzji (VDOP)
double   hal_gps_pdop(void);                    // dylucja precyzji pozycji (PDOP)
uint8_t  hal_gps_fix_quality(void);             // wskaźnik jakości fix z GGA
uint8_t  hal_gps_fix_mode(void);                // tryb fix z GSA (1=brak,2=2D,3=3D)
double   hal_gps_horizontal_accuracy_m(void);   // szacowana dokładność pozioma, metry

// Data UTC
int hal_gps_date_year(void);   // czterocyfrowy rok
int hal_gps_date_month(void);  // 1-12
int hal_gps_date_day(void);    // 1-31

// Czas UTC
int hal_gps_time_hour(void);   // 0-23
int hal_gps_time_minute(void); // 0-59
int hal_gps_time_second(void); // 0-59

// Diagnostyka
uint32_t hal_gps_chars_processed(void);    // łączna liczba bajtów podanych do parsera
uint32_t hal_gps_passed_checksum(void);    // ramki NMEA, które przeszły sumę kontrolną
uint32_t hal_gps_failed_checksum(void);    // ramki NMEA, które nie przeszły sumy kontrolnej
uint32_t hal_gps_sentences_with_fix(void); // poprawne ramki zawierające fix lokalizacji
int      hal_gps_serial_available(void);   // bajty oczekujące w buforze RX portu szeregowego
```

**Architektura:** `src/hal/gps/hal_gps.cpp` jest jedyną fasadą transportu.
Jest właścicielem inicjalizacji, odpytywania, fallbacku ramkowania
SoftwareSerial, dostępności portu szeregowego oraz dokonywanego podczas
buildu wyboru między `hal_uart` a `hal_swserial`. RP2040 i STM32G474
wykorzystują ten sam plik, a tylko wybrany transport HAL dostarcza
zachowanie specyficzne dla targetu.

Wspólny `hal/gps/hal_gps_core.cpp` jest właścicielem mutexu, podawania
bajtów, wieku fix, diagnostyki oraz każdego publicznego gettera danych
wokół `gps_nmea_parser.cpp`. Logika parsera jest przeniesiona z TinyGPS++
(LGPL), z obsługą GSA/GSV/GST opartą na układach pól minmea. Iniektory mock
aktualizują stan deterministycznego silnika bez ponownej implementacji
publicznych getterów.

**Thread safety:** jeden wewnętrzny `hal_mutex_t` chroni stan
parsera, wstrzykiwanie mock, podawanie bajtów oraz wszystkie akcesory.
Inicjalizacja pozostaje operacją init singletona na sprzęcie; inicjalizacja
mock resetuje stan dla każdego testu. Pause i resume to operacje cyklu
życia: wywołuj je z właściciela transportu i nie nakładaj ich na
`hal_gps_update()`.

**Transport RP2040 i powinowactwo rdzenia:**

- Z `HAL_GPS_TRANSPORT_SWSERIAL` odbiór działa w maszynach stanów PIO, a
  DMA zapisuje surowy pierścień. Nie ma ISR RX GPS na żadnym z dwóch
  rdzeni; `hal_gps_update()` opróżnia bufor obsługiwany przez DMA w
  kontekście zadania wywołującego. `hal_gps_pause()` zatrzymuje to DMA i
  zwalnia transport PIO; `hal_gps_resume()` odtwarza go z zapisanymi
  pinami, prędkością transmisji i ramkowaniem.
- Z `HAL_GPS_TRANSPORT_UART` `hal_gps_init()` wywołuje `hal_uart_begin()`.
  Sprzętowe IRQ RX UART jest więc instalowane na, i niejawnie posiadane
  przez, rdzeń wykonujący `hal_gps_init()`.
- Obecne API GPS/UART nie udostępnia tego niejawnego właściciela UART i nie
  waliduje rdzenia wywołującego. Inicjalizuj GPS/UART z zamierzonego rdzenia
  i trzymaj reinicjalizację lub demontaż na tym samym rdzeniu. W kodzie
  FreeRTOS/SMP wywołuj `hal_gps_init()` z zadania usługi GPS po przypięciu
  tego zadania do wybranego rdzenia; trzymanie `hal_gps_update()` w tym
  samym zadaniu czyni całą własność transportu/parsowania jednoznaczną.

Na przykład aplikacja, która przypisuje GPS do rdzenia 0, musi wywoływać
zarówno `hal_gps_init()`, jak i regularne odpytywanie `hal_gps_update()` z
zadania usługi rdzenia 0. Samo chronienie wywołań mutexem nie przenosi już
zainstalowanego IRQ UART RP2040 między rdzeniami.

**Domyślna konfiguracja UART:**

`HAL_GPS_DEFAULT_UART_CONFIG` (zdefiniowana w `hal/core/hal_config.h`)
domyślnie ustawia `HAL_UART_CFG_8N1` (standard NMEA 0183). Ścieżka
SoftwareSerial może automatycznie przełączyć się na 7N1 po powtarzających
się niepowodzeniach sumy kontrolnej.

```c
// Wartość domyślna hal/core/hal_config.h (można nadpisać we flagach buildu):
#define HAL_GPS_DEFAULT_UART_CONFIG  HAL_UART_CFG_8N1

// Użycie:
hal_gps_init(GPS_RX_PIN, GPS_TX_PIN, 9600, HAL_UART_CFG_8N1);
```

**Pomocnicy mock:**
```c
void hal_mock_gps_set_location(double lat, double lng);        // wstrzykuje szerokość i długość geograficzną
void hal_mock_gps_set_valid(bool valid);                       // kontroluje hal_gps_location_is_valid()
void hal_mock_gps_set_updated(bool updated);                   // kontroluje hal_gps_location_is_updated()
void hal_mock_gps_set_age(uint32_t age_ms);                    // kontroluje hal_gps_location_age()
void hal_mock_gps_set_speed(double kmph);                      // kontroluje hal_gps_speed_kmph()
void hal_mock_gps_set_date(int year, int month, int day);      // kontroluje akcesory daty
void hal_mock_gps_set_time(int hour, int minute, int second);  // kontroluje akcesory czasu
void hal_mock_gps_set_altitude_m(double altitude_m);
void hal_mock_gps_set_course_deg(double course_deg);
void hal_mock_gps_set_dop(double hdop, double vdop, double pdop);
void hal_mock_gps_set_satellites(uint32_t used, uint8_t in_view);
void hal_mock_gps_set_fix(uint8_t quality, uint8_t mode);
void hal_mock_gps_set_horizontal_accuracy_m(double accuracy_m);
void hal_mock_gps_reset(void);                                 // zeruje cały stan
```

---


---

## `hal_mcp3221` - 12-bitowy ADC MCP3221  *(opcjonalny - `HAL_ENABLE_MCP3221`)*

```c
#include <hal/analog/hal_mcp3221.h>

hal_i2c_init(sda_pin, scl_pin, HAL_I2C_CLOCK_STANDARD_HZ);

hal_mcp3221_t adc = {0};
hal_status_t status = hal_mcp3221_init_ex(&adc, NULL);
if (status == HAL_OK) {
  uint16_t raw = 0;
  status = hal_mcp3221_read_ex(&adc, &raw);
}
```

Domyślna konfiguracja używa magistrali 0 i adresu
`HAL_MCP3221_I2C_ADDR_DEFAULT` (`0x4D`, zgodnie z domyślną wartością wtyczki
grblHAL `(0x9A >> 1)`). `hal_mcp3221_read_ex()` żąda dokładnie dwóch bajtów i
dekoduje je jako surową wartość big-endian, zachowując zachowanie drivera
źródłowego.

**Thread safety:** mutex per-instancja serializuje odczyty;
transakcje I2C wykorzystują blokadę magistrali HAL I2C. Wywołania cyklu
życia pozostają jednowłaścicielskie.

Przykład: `examples/23_io_pmic`.

---

*Dalej: [Modem komórkowy](12_modem.md)*
