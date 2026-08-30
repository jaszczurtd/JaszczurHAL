# Magistrale komunikacyjne

*Dostępne również [po angielsku](../en/09_buses.md).*

> **Część [Dokumentacji API JaszczurHAL](../../pl/JaszczurHAL_API.md)**

Obejmuje: `hal_spi`, `hal_spi_device`, `hal_i2c`, `hal_i2c_slave`, `hal_uart`, `hal_swserial`, `hal_onewire`.

## `hal_spi` - magistrala SPI i API transferu

```c
#include <hal/spi/hal_spi.h>

// Dotychczasowe zawodne operacje zwracające void teraz w ich miejsce zwracają status.
hal_status_t hal_spi_init(uint8_t bus, uint8_t rx_pin, uint8_t tx_pin,
                          uint8_t sck_pin);
hal_status_t hal_spi_begin_transaction(
    uint8_t bus, const hal_spi_settings_t *settings);
hal_status_t hal_spi_end_transaction(uint8_t bus);
hal_status_t hal_spi_transfer_buffer(uint8_t bus, uint8_t *buffer,
                                     size_t len);
hal_status_t hal_spi_transfer_txrx(uint8_t bus, const uint8_t *tx,
                                   uint8_t *rx, size_t len);
hal_status_t hal_spi_write(uint8_t bus, const uint8_t *data, size_t len);

// API zgodności value/bool zachowują towarzyszące warianty _ex zwracające status.
hal_status_t hal_spi_transfer_ex(uint8_t bus, uint8_t data,
                                 uint8_t *out_received);
hal_status_t hal_spi_transfer16_ex(uint8_t bus, uint16_t data,
                                   uint16_t *out_received);
hal_status_t hal_spi_write_dma_ex(uint8_t bus, const uint8_t *data, size_t len);
hal_status_t hal_spi_write_dma_async_start_ex(uint8_t bus,
                                              const uint8_t *data, size_t len);
hal_status_t hal_spi_write_dma_async_wait_ex(uint8_t bus);

// Skonfiguruj piny i uruchom magistralę SPI w trybie master.
// magistrale 0/1 mapują się na RP SPI0/1, STM32G474 SPI1/2 lub ESP32-S3 SPI2/3.
void hal_spi_deinit(uint8_t bus);

// Opcjonalna synchronizacja runtime dla współdzielonych magistrali SPI.
void hal_spi_lock(uint8_t bus);
void hal_spi_unlock(uint8_t bus);

// Ustawienia transakcji zgodne z SPISettings oraz prymitywy transferu.
hal_spi_settings_t settings = {4000000u, HAL_SPI_MSBFIRST, HAL_SPI_MODE0};
uint8_t  hal_spi_transfer(uint8_t bus, uint8_t data);
uint16_t hal_spi_transfer16(uint8_t bus, uint16_t data);
bool     hal_spi_write_dma(uint8_t bus, const uint8_t *data, size_t len);

// Asynchroniczna ścieżka zapisu z obsługą DMA. Backendy bez asynchronicznego DMA
// kończą transfer przed powrotem z _start().
bool     hal_spi_write_dma_async_start(uint8_t bus, const uint8_t *data, size_t len);
bool     hal_spi_write_dma_async_busy(uint8_t bus);
bool     hal_spi_write_dma_async_wait(uint8_t bus);
```

Wspierane są tylko wartości magistrali 0 i 1. Operacje zwracające status
raportują `HAL_EINVAL` dla innych wartości; niskopoziomowe pomocnicze
funkcje synchronizacji i sprzątania zachowują swoje asercje z buildów
kontrolowanych (checked-build).

API raportuje `HAL_EINVAL` dla nieprawidłowych magistrali, ustawień,
wskaźników wyjściowych i niepustych buforów NULL; start asynchronicznego
DMA raportuje `HAL_EBUSY`, gdy transfer jest już aktywny. Operacje na
buforze o zerowej długości kończą się powodzeniem bez wymagania bufora.
Ścieżki pollingu i oczekiwania DMA dla STM32G474 dodatkowo raportują
`HAL_ETIMEOUT`, a jej ścieżka DMA raportuje `HAL_EIO` przy błędzie
transferu. Blokujące wywołania SDK dla RP2040 raportują `HAL_EIO`, jeśli
przenoszą mniej elementów niż zażądano. Backendy ujawniają tylko te błędy,
które potrafią uczciwie rozróżnić.

**Zapisy DMA:** `hal_spi_write_dma()` to blokujący wrapper wygody: uruchamia
najszybszą dostępną ścieżkę zapisu backendu i zwraca sterowanie dopiero po
tym, jak bufor został zaakceptowany/przesłany zgodnie z tym backendem.
Trio `hal_spi_write_dma_async_*()` udostępnia formę nieblokującą tam, gdzie
backend ją wspiera. Po udanym `_async_start()` wywołujący musi utrzymać
bufor `data` żywy i niezmieniony do momentu, gdy `_async_busy()` stanie się
false lub `_async_wait()` zwróci sterowanie, i nie może rozpoczynać drugiego
asynchronicznego zapisu na tej samej magistrali, dopóki poprzedni jest
aktywny. Backendy bez asynchronicznego DMA wykonują zapis wewnątrz
`_async_start()`, raportują `_async_busy() == false`, a `_async_wait()`
zwraca sterowanie natychmiast.

**impl/rp2040:** Natywne `hardware/spi.h` z Pico SDK na SPI0/SPI1 wraz z multipleksowaniem pinów `hardware/gpio.h`. `hal_spi_write_dma_async_start()` używa TX DMA SPI dla strumieni bajtów MSB-first i zwraca sterowanie, zanim magistrala jest bezczynna; `hal_spi_end_transaction()` / `hal_spi_deinit()` czekają na dowolne aktywne asynchroniczne TX DMA przed zamknięciem transakcji lub zwolnieniem kanału.
**impl/stm32g474:** SPI1/SPI2 master na poziomie rejestrów, 8-bitowy
full-duplex, programowe NSS, transfer w trybie pollingu, konfiguracja pinów
AF5 i sterowany przerwaniami TX DMA. SPI1 jest zasilane z 170 MHz PCLK2, a
SPI2 z 170 MHz PCLK1; backend wybiera najszybszy preskaler będący potęgą
dwójki, który nie przekracza żądanego zegara.
Domyślne piny: magistrala SPI 0 = PA6/PA7/PA5, magistrala 1 = PB14/PB15/PB13.
TX SPI1 rezerwuje DMA1 Channel7, a TX SPI2 rezerwuje DMA1 Channel8;
odpowiadające żądania DMAMUX i przerwania DMA łączą w łańcuch bufory
większe niż limit sprzętowego licznika 65 535 bajtów. Start asynchroniczny
zwraca sterowanie, zanim magistrala jest bezczynna, podczas gdy zakończenie
transakcji i deinit czekają na zakończenie. Bufory w SRAM CCM dostępnym
tylko dla CPU używają synchronicznej ścieżki pollingu, ponieważ DMA1 nie ma
dostępu do tej pamięci.
**impl/esp32:** ESP-IDF SPI master na SPI2/SPI3 dla magistrali HAL 0/1.
Maski płytki i targetu walidują piny MISO/MOSI/SCK; chip select pozostaje pod
przenośną warstwą GPIO `hal_spi_device`. Magistrala żąda automatycznego
kanału DMA IDF i wraca do magistrali bez DMA, jeśli jest niedostępny.
Asynchroniczne DMA HAL używa udokumentowanego synchronicznego fallbacku:
`_async_start()` kończy zapis przed powrotem, `_async_busy()` jest zawsze
false, a `_async_wait()` zwraca sterowanie natychmiast. Transfery są
dzielone na fragmenty po 64 bajty.
**impl/.mock:** przechowuje init/ustawienia, głębokość blokady, skryptowane bajty RX i log TX dla testów.
**Thread safety:** `hal_spi_begin_transaction()` stosuje ustawienia magistrali, ale nie blokuje. Użyj `hal_spi_lock()` / `hal_spi_unlock()` wokół wieloetapowych operacji drivera na współdzielonych magistralach. Traktuj czas życia asynchronicznego DMA jako część tej samej transakcji/sekcji krytycznej: utrzymuj chip-select i posiadanie magistrali ważne do zakończenia `_async_wait()`.

---

## `hal_spi_device` - neutralny względem targetu deskryptor urządzenia SPI

```c
#include <hal/spi/hal_spi_device.h>

hal_spi_device_t device;
hal_spi_settings_t settings = {
    8000000u, HAL_SPI_MSBFIRST, HAL_SPI_MODE0};
hal_status_t status = hal_spi_device_init(&device, 0u, 5u, &settings);

hal_spi_device_operation_t operations[] = {
    {HAL_SPI_DEVICE_OP_WRITE, command, NULL, command_len},
    {HAL_SPI_DEVICE_OP_READ, NULL, response, response_len},
};
status = hal_spi_device_transaction(&device, operations, 2u);
```

Deskryptor wiąże jedną magistralę, aktywny-niski pin chip-select oraz
efektywne ustawienia SPI. `HAL_SPI_DEVICE_CS_NONE` wybiera urządzenie bez
CS zarządzanego przez HAL. Inicjalizacja konfiguruje podłączony pin CS jako
nieaktywne wyjście wysokie. Deskryptory są neutralne względem targetu i
używają istniejących backendowych API SPI/GPIO. `hal_spi_device_acquire()`
i `hal_spi_device_release()` udostępniają ręczny cykl życia w stylu
status-first dla stanowych transferów. `hal_spi_device_transaction()`
wykonuje bufory READ, WRITE, TRANSFER i TRANSFER_IN_PLACE w ramach jednej
akwizycji. `hal_spi_device_finish()` gwarantuje zwolnienie po ręcznym
we/wy i zachowuje błąd operacji ponad późniejszy błąd zakończenia. Gdy
akwizycja się powiedzie, wszystkie ścieżki transakcji wywołują zakończenie
backendu, dezaktywują CS i odblokowują magistralę.

---

## `hal_i2c` - magistrala I2C  *(opcjonalnie - `HAL_ENABLE_I2C`)*

```c
#include <hal/i2c/hal_i2c.h>

// Adres urządzenia I2C: 0x000..0x07F po inicjalizacji 7-bitowej,
// 0x000..0x3FF po inicjalizacji 10-bitowej (HAL_ENABLE_I2C_10BIT). Tryb
// adresowania jest właściwością zainicjalizowanego kontrolera, a nie
// pojedynczego wywołania - ta sama wartość liczbowa jest interpretowana
// inaczej w zależności od tego, który wariant init skonfigurował magistralę.
typedef uint16_t hal_i2c_address_t;

#ifdef HAL_ENABLE_I2C_10BIT
typedef enum {
  HAL_I2C_ADDR_MODE_7BIT = 0,
  HAL_I2C_ADDR_MODE_10BIT = 1,
} hal_i2c_addr_mode_t;
#endif

// Popularne stałe zegara I2C:
#define HAL_I2C_CLOCK_STANDARD_HZ    100000UL   // Standard-mode, 100 kHz
#define HAL_I2C_CLOCK_FAST_HZ        400000UL   // Fast-mode, 400 kHz
#define HAL_I2C_CLOCK_FAST_PLUS_HZ  1000000UL   // Fast-mode Plus, 1 MHz
#define HAL_I2C_CLOCK_HIGH_SPEED_HZ 3400000UL   // High-speed mode, 3.4 MHz

// Popularne stałe wyniku transakcji I2C:
#define HAL_I2C_RESULT_OK           0u
#define HAL_I2C_ERROR_GENERIC       2u
#define HAL_I2C_ERROR_OTHER         3u
#define HAL_I2C_ERROR_TIMEOUT       4u

// Dotychczasowe zawodne operacje zwracające void teraz w ich miejsce zwracają status.
hal_status_t hal_i2c_init(uint8_t sda_pin, uint8_t scl_pin,
                          uint32_t clock_hz);
hal_status_t hal_i2c_init_bus(uint8_t bus, uint8_t sda_pin,
                              uint8_t scl_pin, uint32_t clock_hz);
#ifdef HAL_ENABLE_I2C_10BIT
// Jak hal_i2c_init(_bus), ale kontroler interpretuje każdy adres przekazany
// do transferu na tej magistrali jako adres 10-bitowy. hal_i2c_scan(_bus)
// pozostaje wyłącznie 7-bitowy i zwraca HAL_EUNSUPPORTED na magistrali 10-bit.
hal_status_t hal_i2c_init_10bit(uint8_t sda_pin, uint8_t scl_pin,
                                uint32_t clock_hz);
hal_status_t hal_i2c_init_bus_10bit(uint8_t bus, uint8_t sda_pin,
                                    uint8_t scl_pin, uint32_t clock_hz);
hal_i2c_addr_mode_t hal_i2c_get_addr_mode(void);
hal_i2c_addr_mode_t hal_i2c_get_addr_mode_bus(uint8_t bus);
#endif
hal_status_t hal_i2c_set_clock(uint32_t clock_hz);
hal_status_t hal_i2c_set_clock_bus(uint8_t bus, uint32_t clock_hz);
// Odczytaj skonfigurowany (znormalizowany żądany, nie efektywny/zmierzony)
// zegar zaakceptowany przez ostatnie wywołanie init()/set_clock().
hal_status_t hal_i2c_get_clock(uint32_t *out_clock_hz);
hal_status_t hal_i2c_get_clock_bus(uint8_t bus, uint32_t *out_clock_hz);
hal_status_t hal_i2c_bus_clear(uint8_t sda_pin, uint8_t scl_pin);
hal_status_t hal_i2c_bus_clear_bus(uint8_t bus, uint8_t sda_pin,
                                   uint8_t scl_pin);

// Jedno ograniczone skanowanie użytecznego zakresu 7-bitowego 0x08..0x77. Callback jest
// opcjonalny i uruchamia się przed każdą próbą; hal_watchdog_feed można przekazać
// bezpośrednio. Wskaźnik addresses równy NULL z capacity 0 wykonuje skanowanie
// tylko liczące. outFound otrzymuje całkowitą liczbę nawet gdy wyjście jest za małe.
// Zawsze wyłącznie 7-bitowy, nawet na magistrali zainicjalizowanej przez
// hal_i2c_init_10bit().
typedef void (*hal_i2c_scan_callback_t)(void);
hal_status_t hal_i2c_scan(uint8_t *addresses, size_t capacity,
                          size_t *outFound,
                          hal_i2c_scan_callback_t callback);
hal_status_t hal_i2c_scan_bus(uint8_t bus, uint8_t *addresses,
                              size_t capacity, size_t *outFound,
                              hal_i2c_scan_callback_t callback);

// Towarzysze statusowe dla dotychczasowych operacji zwracających value/bool.
hal_status_t hal_i2c_end_transmission_ex(void);
hal_status_t hal_i2c_end_transmission_bus_ex(uint8_t bus);
hal_status_t hal_i2c_write_byte_ex(hal_i2c_address_t address, uint8_t data,
                                   bool *outWriteOk);
hal_status_t hal_i2c_write_byte_bus_ex(uint8_t bus, hal_i2c_address_t address,
                                       uint8_t data, bool *outWriteOk);
hal_status_t hal_i2c_read_byte_ex(hal_i2c_address_t address, uint8_t *outValue);
hal_status_t hal_i2c_read_byte_bus_ex(uint8_t bus, hal_i2c_address_t address,
                                      uint8_t *outValue);
hal_status_t hal_i2c_write_read_ex(hal_i2c_address_t address,
                                   const uint8_t *tx, size_t tx_len,
                                   uint8_t *rx, size_t rx_len);
hal_status_t hal_i2c_write_read_bus_ex(uint8_t bus, hal_i2c_address_t address,
                                       const uint8_t *tx, size_t tx_len,
                                       uint8_t *rx, size_t rx_len);
hal_status_t hal_i2c_read_bytes_ex(hal_i2c_address_t address, uint8_t *rx,
                                   size_t rx_len);
hal_status_t hal_i2c_read_bytes_bus_ex(uint8_t bus, hal_i2c_address_t address,
                                       uint8_t *rx, size_t rx_len);
hal_status_t hal_i2c_request_from_ex(hal_i2c_address_t address, uint8_t count,
                                     uint8_t *outReceived);
hal_status_t hal_i2c_request_from_bus_ex(uint8_t bus, hal_i2c_address_t address,
                                         uint8_t count, uint8_t *outReceived);
void    hal_i2c_deinit(void);
void    hal_i2c_deinit_bus(uint8_t bus);

// Ręczne lock/unlock - użyj przy owijaniu zewnętrznej biblioteki, która wywołuje
// obiekt magistrali natywny dla backendu bezpośrednio.
void    hal_i2c_lock(void);
void    hal_i2c_unlock(void);
void    hal_i2c_lock_bus(uint8_t bus);
void    hal_i2c_unlock_bus(uint8_t bus);

// Prymitywy transakcji (begin/write/end automatycznie przejmują/zwalniają mutex)
void    hal_i2c_begin_transmission(hal_i2c_address_t address);
size_t  hal_i2c_write(uint8_t data);        // zwraca 1 przy powodzeniu, 0 przy niepowodzeniu
uint8_t hal_i2c_end_transmission(void);     // zwraca 0 przy powodzeniu, wartość niezerową przy błędzie
void    hal_i2c_begin_transmission_bus(uint8_t bus, hal_i2c_address_t address);
size_t  hal_i2c_write_bus(uint8_t bus, uint8_t data);
uint8_t hal_i2c_end_transmission_bus(uint8_t bus);

// Jednorazowy pomocnik "begin + write jednego bajtu + end" (przejmuje/zwalnia mutex wewnętrznie).
// *outWriteOk (opcjonalny) otrzymuje status kolejkowanych bajtów z hal_i2c_write().
// Wartość zwracana to status end_transmission (0 przy powodzeniu).
uint8_t hal_i2c_write_byte(hal_i2c_address_t address, uint8_t data, bool *outWriteOk);
uint8_t hal_i2c_write_byte_bus(uint8_t bus, hal_i2c_address_t address, uint8_t data, bool *outWriteOk);

// Symetryczny jednorazowy pomocnik "request + read 1 bajt".
// Wewnętrzny mutex jest utrzymywany przez całą sekwencję request+read.
// *outReadOk (opcjonalny) otrzymuje true, gdy odebrano dokładnie jeden bajt.
// Zwraca odczytany bajt lub 0 przy niepowodzeniu - sprawdź *outReadOk, aby odróżnić
// prawdziwe 0x00 od błędu komunikacji.
uint8_t hal_i2c_read_byte(hal_i2c_address_t address, bool *outReadOk);
uint8_t hal_i2c_read_byte_bus(uint8_t bus, hal_i2c_address_t address, bool *outReadOk);

// Połączony pomocnik write-then-read dla czujników z wskaźnikiem rejestru.
// Zapisuje bajty tx, utrzymuje magistralę aktywną dla odczytu z powtórzonym startem
// i odczytuje dokładnie rx_len bajtów. Zwraca true tylko gdy obie fazy się zakończą.
bool    hal_i2c_write_read(hal_i2c_address_t address,
                           const uint8_t *tx, size_t tx_len,
                           uint8_t *rx, size_t rx_len);
bool    hal_i2c_write_read_bus(uint8_t bus, hal_i2c_address_t address,
                               const uint8_t *tx, size_t tx_len,
                               uint8_t *rx, size_t rx_len);

// Bezpośredni pomocnik odczytu dla czujników udostępniających bieżące dane bez fazy
// wskaźnika rejestru. Utrzymuje mutex magistrali przez request+copy.
bool    hal_i2c_read_bytes(hal_i2c_address_t address, uint8_t *rx, size_t rx_len);
bool    hal_i2c_read_bytes_bus(uint8_t bus, hal_i2c_address_t address,
                               uint8_t *rx, size_t rx_len);

// Odziedziczone (legacy) buforowane API odbioru. Nie jest atomową sekwencją odczytu, chyba że
// wywołujący opakuje request+available/read w hal_i2c_lock()/hal_i2c_unlock().
// Preferuj hal_i2c_read_bytes(_bus) lub hal_i2c_write_read(_bus) w driverach.
uint8_t hal_i2c_request_from(hal_i2c_address_t address, uint8_t count);  // zwraca liczbę odebranych bajtów
int     hal_i2c_available(void);    // bajty w buforze odbiorczym
int     hal_i2c_read(void);         // jeden bajt lub -1, gdy pusto
uint8_t hal_i2c_request_from_bus(uint8_t bus, hal_i2c_address_t address, uint8_t count);
int     hal_i2c_available_bus(uint8_t bus);
int     hal_i2c_read_bus(uint8_t bus);

// Licznik transakcji - liczy zakończone transakcje zapisu (end_transmission) i odczytu
// (request_from) od inicjalizacji. Resetuje się przy init. Zawija się przy UINT32_MAX.
uint32_t hal_i2c_get_transaction_count(void);
uint32_t hal_i2c_get_transaction_count_bus(uint8_t bus);

// Sonda zajętości urządzenia - wyślij adres, sprawdź ACK/NACK natychmiast.
// Zwraca true, jeśli urządzenie NIE potwierdziło ACK (zajęte lub nieobecne).
// Typowe użycie: odpytuj po zapisie AT24C256, aż układ będzie gotowy.
bool    hal_i2c_is_busy(hal_i2c_address_t address);
bool    hal_i2c_is_busy_bus(uint8_t bus, hal_i2c_address_t address);

```

Wspierane są tylko wartości magistrali 0 i 1. Inne wartości są błędami
programisty i wywołują `HAL_ASSERT` w buildach kontrolowanych.

Warianty `_ex` zwracają `HAL_OK` przy powodzeniu i diagnostykę
`hal_status_t` dla nowego kodu. Nieprawidłowe magistrale lub bufory zwracają
`HAL_EINVAL`; użycie niezainicjalizowanej magistrali zwraca `HAL_EUNINIT`
tam, gdzie backend potrafi to wykryć; odziedziczony `HAL_I2C_ERROR_TIMEOUT`
mapuje się na `HAL_ETIMEOUT`, ogólne błędy NACK/magistrali mapują się na
`HAL_EBUS`, a niesprecyzowane błędy backendu mapują się na `HAL_EIO`.
Istniejące wrappery zachowują swoje kształty zwracanych wartości `void`,
`uint8_t` i `bool` dla zgodności źródłowej.

`hal_i2c_scan()` zastępuje stary pomocnik `i2cScanner()` z `tools.cpp`.
Skanuje jednorazowo zamiast posiadać nieskończoną pętlę print/delay, pomija
zarezerwowane adresy 7-bitowe, nie ma zależności od serial, wspiera oba
kontrolery, raportuje przepełnienie bufora jako `HAL_EOVERFLOW` i utrzymuje
jawną obsługę watchdoga przez swój opcjonalny callback. Aplikacje same
zarządzają prezentacją i harmonogramowaniem:

```c
uint8_t addresses[HAL_I2C_SCAN_ADDRESS_COUNT];
size_t found = 0;
hal_status_t status =
    hal_i2c_scan(addresses, HAL_I2C_SCAN_ADDRESS_COUNT, &found,
                 hal_watchdog_feed);
```

**Zachowanie init:** `hal_i2c_init*()` tworzy mutex per-magistrala,
konfiguruje SDA/SCL, zegar i uruchamia kontroler backendu; nadal powinien
być wywoływany podczas setup, przed normalnym ruchem I2C. Wywołania w
czasie działania zachowują atomowy fallback tworzenia-przy-pierwszym-użyciu
do defensywnego użycia przed init. Użyj
`hal_i2c_set_clock()` / `hal_i2c_set_clock_bus()`, aby przestroić już
skonfigurowaną magistralę, zachowując zmianę wewnątrz mutexu magistrali HAL.

**Tryby zegara:** Nazwane stałe zegara mapują się na tryby specyfikacji
magistrali I2C: Standard-mode (100 kHz), Fast-mode (400 kHz), Fast-mode
Plus / Fm+ (1 MHz) i High-speed mode / Hs-mode (3,4 MHz). 1 MHz i 3,4 MHz to
rzeczywiste przypadki użycia: Fm+ jest powszechne dla szybszych peryferiów
na poziomie płytki i buforów magistrali, podczas gdy Hs-mode występuje w
czujnikach o wysokiej szybkości, takich jak niektóre czujniki środowiskowe
Bosch i czujniki ruchu ST. Zawsze sprawdź kontroler, trasowanie płytki,
podciągnięcia (pull-up), pojemność magistrali i arkusz danych każdego
urządzenia przed wyborem tych prędkości. W szczególności Hs-mode ma
wymagania protokołu/czasowania wykraczające poza samo wpisanie większej
wartości zegara, więc wsparcie backendu/kontrolera musi zostać
zweryfikowane na platformie docelowej.

**Odniesienie:** NXP UM10204, "I2C-bus specification and user manual",
definiuje Standard-mode, Fast-mode, Fast-mode Plus i High-speed mode.

**Adresowanie 10-bitowe (`HAL_ENABLE_I2C_10BIT`, opcjonalne):** Szerokość
adresowania jest właściwością zainicjalizowanego kontrolera, nigdy
pojedynczego wywołania ani pojedynczej wartości adresu: `hal_i2c_init()`/
`hal_i2c_init_bus()` ustawiają magistralę w trybie 7-bitowym (`0x000..0x07F`),
`hal_i2c_init_10bit()`/`hal_i2c_init_bus_10bit()` ustawiają tryb 10-bitowy
(`0x000..0x3FF`); ta sama wartość liczbowa adresu jest interpretowana inaczej
w zależności od tego, który wariant init skonfigurował magistralę, a
kontroler nigdy nie miesza urządzeń 7- i 10-bitowych naraz. Każda dotychczasowa
funkcja przyjmująca adres poszerzyła swój parametr z `uint8_t` na nowy
`hal_i2c_address_t` (`uint16_t`) - świadoma, łamiąca zmiana typu; zwykłe
miejsca wywołań przekazujące literał lub zmienną `uint8_t` nadal się
kompilują bez zmian po przebudowie. `hal_i2c_scan()`/`hal_i2c_scan_bus()`
pozostają wyłącznie 7-bitowe na zawsze i zwracają `HAL_EUNSUPPORTED` przy
wywołaniu na magistrali 10-bitowej. Przełączenie magistrali między trybami
wymaga jawnej ponownej inicjalizacji, która przechodzi normalny cykl
zatrzymania/resetu i unieważnia stan związany z poprzednim trybem.

**impl/rp2040:** Natywne `hardware/i2c.h` z Pico SDK na I2C0/I2C1 wraz z multipleksowaniem pinów `hardware/gpio.h`; mutex per-magistrala chroni wszystkie transakcje. Żądania zegara powyżej Fast-mode Plus są przycinane do 1 MHz, ponieważ I2C RP2040 nie implementuje Hs-mode. `hal_i2c_bus_clear()` używa odzyskiwania SCL/SDA na poziomie GPIO przed przywróceniem funkcji pinu I2C. Tryb 10-bitowy ustawia bit `IC_CON.IC_10BITADDR_MASTER` DesignWare na cały czas życia kontrolera przy init i używa dedykowanej niskopoziomowej ścieżki transferu FIFO/timeout (`IC_TAR` niesie pełny adres 10-bitowy; standardowe `i2c_write_timeout_us()`/`i2c_read_timeout_us()` z Pico SDK asertują adres 7-bitowy i nie mogą zostać ponownie użyte).
**impl/stm32g474:** I2C v2 master na poziomie rejestrów na I2C1/I2C2. Oba kontrolery jawnie wybierają HSI16 jako swoje źródło zegara jądra, więc zwalidowane presety TIMINGR dla 16 MHz pozostają niezależne od zegara APB 170 MHz. Backend waliduje mapowania funkcji alternatywnych SDA/SCL, konfiguruje podciągnięcia GPIO w trybie open-drain, wspiera warstwy zegara HAL, obsługuje ścieżki write/read/write-read/is-busy na obu magistralach i wykonuje czyszczenie magistrali na poziomie GPIO z taktowaniem mikrosekundowym niezależnym od zegara przed init. Tryb 10-bitowy ustawia `CR2.ADD10` i wpisuje surowy adres 10-bitowy w `CR2.SADD[9:0]` (tryb 7-bitowy zachowuje dotychczasowe przesunięcie `SADD[7:1]`); `CR2.HEAD10R` celowo nigdy nie jest ustawiany, zgodnie z głównym Linux driverem `i2c-stm32f7` dla tego samego IP I2C v2, który zawsze wysyła kompletny nagłówek 10-bitowy w każdej fazie.
**impl/esp32:** API master/controller ESP-IDF na I2C0/I2C1 z generowaną
walidacją pinów płytki, wewnętrznymi podciągnięciami, buforowanymi
uchwytami urządzeń, timeoutem transferu/sondy 100 ms i resetem
kontrolera po timeout. Zmiany zegara odrzucają buforowane uchwyty urządzeń,
więc kolejne transfery odtwarzają je z nową szybkością. Czyszczenie
magistrali na poziomie GPIO emituje do dziewięciu impulsów SCL i jest
akceptowane tylko, gdy wybrany kontroler jest zdeinicjalizowany. Tryb
10-bitowy konfiguruje każdy buforowany uchwyt urządzenia z
`I2C_ADDR_BIT_LEN_10`; bufor uchwytów urządzeń poszerza się z 128 do 1024
wpisów tylko wtedy, gdy `HAL_ENABLE_I2C_10BIT` jest wkompilowane, więc buildy
wyłącznie 7-bitowe zachowują mniejszy ślad pamięci. `i2c_master_probe()` z
ESP-IDF zawsze sonduje jako adres 7-bitowy niezależnie od przekazanej
wartości, więc `hal_i2c_is_busy_bus()` na magistrali 10-bitowej zastępuje
prawdziwą sondę zero-bajtową odczytem 1 bajtu przez poprawnie
skonfigurowany uchwyt urządzenia.
**impl/.mock:** bufor pierścieniowy; wstrzykiwalny przez pomocnicze funkcje mock. Wstrzyknięte bajty RX są odczytywane kolejno przez transakcje request/read, co pozwala testom skryptować przepływy wielorejestrowe. `hal_i2c_end_transmission()` zwraca `HAL_I2C_ERROR_GENERIC`, gdy ustawiona jest flaga busy mocka, w przeciwnym razie `HAL_I2C_RESULT_OK`. `hal_i2c_bus_clear()` zwiększa wewnętrzny licznik (odpytaj przez `hal_mock_i2c_get_bus_clear_count()`); licznik resetuje się przy `hal_i2c_init()`.
**Thread safety:** Backendy sprzętowe serializują API transferu wewnętrznym `hal_mutex_t` per-magistrala; użyj `hal_i2c_lock` / `hal_i2c_unlock`, aby rozszerzyć sekcje krytyczne wokół bezpośrednich wywołań magistrali backendu/zewnętrznej biblioteki. `hal_i2c_init*()` / `hal_i2c_deinit*()` rekonfigurują współdzielone obiekty magistrali i muszą być serializowane przez aplikację podczas setup/teardown. Backend mock nie synchronizuje jednoczesnego dostępu.

**Pomocnicy mock:**
```c
void    hal_mock_i2c_inject_rx(const uint8_t *data, int len);                    // wstępnie załaduj bufor odbiorczy na magistrali 0
void    hal_mock_i2c_inject_rx_bus(uint8_t bus, const uint8_t *data, int len);   // wstępnie załaduj bufor odbiorczy na wybranej magistrali
hal_i2c_address_t hal_mock_i2c_get_last_addr(void);                              // ostatni adres na magistrali 0
hal_i2c_address_t hal_mock_i2c_get_last_addr_bus(uint8_t bus);                   // ostatni adres na wybranej magistrali
#ifdef HAL_ENABLE_I2C_10BIT
bool    hal_mock_i2c_is_10bit(void);                                             // true, jeśli magistrala 0 jest w trybie 10-bit
bool    hal_mock_i2c_is_10bit_bus(uint8_t bus);                                  // true, jeśli wybrana magistrala jest w trybie 10-bit
#endif
int     hal_mock_i2c_get_lock_depth(void);                                        // bieżąca głębokość blokady na magistrali 0
int     hal_mock_i2c_get_lock_depth_bus(uint8_t bus);                             // bieżąca głębokość blokady na wybranej magistrali
int     hal_mock_i2c_get_read_byte_lock_depth(void);                              // głębokość blokady przechwycona w punkcie odczytu bajtu w hal_i2c_read_byte() na magistrali 0
int     hal_mock_i2c_get_read_byte_lock_depth_bus(uint8_t bus);                   // głębokość blokady przechwycona w punkcie odczytu bajtu w hal_i2c_read_byte_bus() na wybranej magistrali
bool    hal_mock_i2c_is_initialized(void);                                        // stan init dla magistrali 0
bool    hal_mock_i2c_is_initialized_bus(uint8_t bus);                             // stan init dla wybranej magistrali
void    hal_mock_i2c_set_busy(bool busy);                                         // kontroluj hal_i2c_is_busy() + NACK end_transmission na magistrali 0
void    hal_mock_i2c_set_busy_bus(uint8_t bus, bool busy);                        // kontroluj hal_i2c_is_busy() + NACK end_transmission na wybranej magistrali
void    hal_mock_i2c_set_device_present(uint8_t address, bool present);            // skonfiguruj mapę ACK skanowania na magistrali 0
void    hal_mock_i2c_set_device_present_bus(uint8_t bus, uint8_t address, bool present); // skonfiguruj mapę ACK skanowania na wybranej magistrali
uint32_t hal_mock_i2c_get_bus_clear_count(void);                                  // liczba wywołań bus_clear na magistrali 0
uint32_t hal_mock_i2c_get_bus_clear_count_bus(uint8_t bus);                       // liczba wywołań bus_clear na wybranej magistrali
```

**Przykład - ekspander I/O 8-bitowy PCF8574 z użyciem pomocników jednorazowych:**

PCF8574 jest adresowany jednorazowo i nie ma layoutu rejestrów: pojedynczy
bajt zapisu steruje wszystkimi 8 zatrzaskami wyjściowymi; pojedynczy bajt
odczytu zwraca bieżącą wartość portu. Użycie `hal_i2c_write_byte()` i
`hal_i2c_read_byte()` utrzymuje kod drivera wolny od jawnych sekwencji
begin/write/end lub request/read.

```c
#include <hal/i2c/hal_i2c.h>

#define PCF8574_ADDR 0x38   // adres 7-bitowy (A2..A0 = 0)

static uint8_t s_portLatch;  // cień 8 bitów wyjściowych

/** Zainicjalizuj ekspander do wyjść zerowych. */
bool pcf8574_init(void) {
    s_portLatch = 0x00;
    bool writeOk = false;
    uint8_t endTx = hal_i2c_write_byte(PCF8574_ADDR, s_portLatch, &writeOk);
    return writeOk && (endTx == 0);
}

/** Wysteruj jeden pin wyjściowy (0..7). */
bool pcf8574_write_pin(uint8_t pin, bool high) {
    if (pin > 7) return false;
    if (high) s_portLatch |=  (uint8_t)(1u << pin);
    else      s_portLatch &= (uint8_t)~(1u << pin);

    bool writeOk = false;
    uint8_t endTx = hal_i2c_write_byte(PCF8574_ADDR, s_portLatch, &writeOk);
    return writeOk && (endTx == 0);
}

/** Odczytaj jeden pin wejściowy (0..7). Zwraca też false przy błędzie I2C. */
bool pcf8574_read_pin(uint8_t pin) {
    if (pin > 7) return false;
    bool readOk = false;
    uint8_t port = hal_i2c_read_byte(PCF8574_ADDR, &readOk);
    if (!readOk) return false;
    return (port & (uint8_t)(1u << pin)) != 0;
}
```

Uwaga: pomocnicy polegają na *wewnętrznym* mutexie HAL per-magistrala, który
obejmuje pojedynczą parę begin/end. Kod, który przeplata write-then-read z
inną wieloetapową transakcją na tej samej magistrali (np. ustaw wskaźnik
rejestru -> zażądaj N bajtów), musi dodatkowo zserializować obie sekwencje
własnym mutexem wywołującego, ponieważ mutex HAL jest zwalniany przy
każdym `end_transmission`.

---

## `hal_i2c_slave` - slave/target I2C z mapą rejestrów  *(opcjonalnie - `HAL_ENABLE_I2C_SLAVE`)*

Udostępnia mapę rejestrów o stałym rozmiarze w trybie slave I2C. Zdalny
master zapisuje jednobajtowy wskaźnik rejestru, a następnie odczytuje N
bajtów zaczynając od tego adresu. Wskaźnik przesuwa się dla każdego bajtu
taktowanego przez mastera i przetrwa warunki STOP, więc późniejszy goły
odczyt kontynuuje od ostatniej pozycji. Odczyty muszą pozostać w granicach
skonfigurowanej mapy rejestrów.

API targetu ESP-IDF udostępnia bajty zaakceptowane do jego FIFO/bufora
pierścieniowego TX, ale nie to, ile bajtów kontroler wytaktował. Backend
ESP32 używa więc programowego kursora producenta dla pierwszego rejestru
jeszcze niezaakceptowanego przez ESP-IDF. Nieprzetaktowane bajty pozostają
w FIFO/buforze pierścieniowym przez STOP, zachowując kursor na poziomie
linii (wire-level). Nowy zapis wskaźnika rejestru czyści ten oczekujący
sufiks i kolejkuje świeży zrzut od wybranego rejestru. Lokalne wywołania
`reg_write*()` nie zmieniają bajtów już zakolejkowanych; wybierz rejestr
ponownie, aby je odświeżyć.

Jest to niezależne od modułu I2C master (`hal_i2c`) - oba mogą być
wyłączane/włączane osobno, ale nie mogą jednocześnie współdzielić tej samej
magistrali.

```c
#include <hal/i2c/hal_i2c_slave.h>

// Domyślny rozmiar mapy rejestrów (nadpisz w hal_project_config.h)
#ifndef HAL_I2C_SLAVE_REG_MAP_SIZE
#define HAL_I2C_SLAVE_REG_MAP_SIZE 32U
#endif

// Init / deinit
void hal_i2c_slave_init(uint8_t sda_pin, uint8_t scl_pin, uint8_t address);
void hal_i2c_slave_init_bus(uint8_t bus, uint8_t sda_pin, uint8_t scl_pin, uint8_t address);
void hal_i2c_slave_deinit(void);
void hal_i2c_slave_deinit_bus(uint8_t bus);

// Zapis do mapy rejestrów (aplikacja -> bufor slave).
// Rejestry poza zakresem są po cichu ignorowane.
void hal_i2c_slave_reg_write8(uint8_t reg, uint8_t value);
void hal_i2c_slave_reg_write8_bus(uint8_t bus, uint8_t reg, uint8_t value);
void hal_i2c_slave_reg_write16(uint8_t reg, uint16_t value);   // big-endian: MSB przy reg, LSB przy reg+1
void hal_i2c_slave_reg_write16_bus(uint8_t bus, uint8_t reg, uint16_t value);

// Odczyt z mapy rejestrów
uint8_t  hal_i2c_slave_reg_read8(uint8_t reg);
uint8_t  hal_i2c_slave_reg_read8_bus(uint8_t bus, uint8_t reg);
uint16_t hal_i2c_slave_reg_read16(uint8_t reg);
uint16_t hal_i2c_slave_reg_read16_bus(uint8_t bus, uint8_t reg);

// Zapytanie o adres
uint8_t hal_i2c_slave_get_address(void);
uint8_t hal_i2c_slave_get_address_bus(uint8_t bus);

// Licznik aktywności obserwowany przez backend; zobacz uwagi targetu poniżej.
// Przydatny do wykrywania aktywności na żywej magistrali bez odpytywania wartości zwracanych reg_write.
// Resetuje się przy init. Zawija się przy UINT32_MAX. Thread-safe (atomowy).
uint32_t hal_i2c_slave_get_transaction_count(void);
uint32_t hal_i2c_slave_get_transaction_count_bus(uint8_t bus);
```

Wspierane są tylko wartości magistrali 0 i 1. Inne wartości są błędami
programisty i wywołują `HAL_ASSERT` w buildach kontrolowanych.

**Protokół mapy rejestrów (I2C):**
1. Master zapisuje: `[reg_address]` - ustawia wskaźnik rejestru
2. Master odczytuje N bajtów - slave odpowiada `regs[ptr], regs[ptr+1], ...`
3. Master zapisuje: `[reg_address, data0, data1, ...]` - ustawia wskaźnik, następnie zapisuje dane sekwencyjnie

**impl/rp2040:** Natywny tryb peryferyjny `hardware/i2c.h` z Pico SDK na I2C0/I2C1 wraz z obsługą zdarzeń `hardware/irq.h`. Przerwania RX FIFO, read-request, START i STOP/TX-abort sterują bezpośrednio protokołem mapy rejestrów.
**impl/stm32g474:** Tryb target I2C v2 na poziomie rejestrów na I2C1/I2C2. Backend wybiera HSI16 jako zegar jądra i konfiguruje funkcje alternatywne SDA/SCL, dopasowanie własnego adresu, konserwatywny `TIMINGR`, przerwania RX/TX/ADDR/STOP/NACK/error, opróżnianie TXDR przy NACK/STOP i obsługuje ten sam protokół mapy rejestrów z handlerów przerwań I2C EV/ER.
**impl/esp32:** Docelowe urządzenia ESP-IDF I2C na kontrolerze 0/1.
Callbacki receive/request utrzymują pracę ISR w ograniczonym czasie i
sygnalizują jednemu wątkowi roboczemu FreeRTOS na aktywną magistralę;
wątek roboczy resetuje FIFO TX lub zapisuje zrzut mapy rejestrów przez
oficjalny driver trybu target. Nowy zapis wskaźnika rejestru resetuje
nieaktualne zakolejkowane dane TX przed kolejnym zrzutem. Częściowa
akceptacja kolejki przesuwa tylko tę część kursora producenta, którą
zaakceptował ESP-IDF; pozostałe zakolejkowane bajty niosą kursor na
poziomie linii przez STOP. Przerywający zapis wskaźnika jest chroniony
przez sprawdzenie generacji. Ten backend liczy w liczniku transakcji
zakończone zapisy i zaakceptowane żądania obsługi odczytu. Deinit najpierw
zamyka dopuszczanie zdarzeń ISR, wyrejestrowuje callbacki, opróżnia wątek
roboczy, niszczy driver, tworząc barierę czasu życia przerwań, i dopiero
wtedy usuwa obiekty synchronizacji wątku roboczego.
**impl/.mock:** bezpośredni dostęp do mapy rejestrów; pomocnicy symulacji dla zapisu/odczytu mastera.
**Thread safety:** `reg_write*` / `reg_read*` są thread-safe dla normalnych wywołujących z poziomu zadania/rdzenia na backendach sprzętowych. Mapa rejestrów jest chroniona krótką blokadą lokalną dla backendu współdzieloną z callbackami magistrali/ISR, więc handlery nie przejmują mutexów HAL w buildach FreeRTOS. `init` / `deinit` muszą być serializowane przez aplikację podczas setup/teardown. Backend mock nie synchronizuje jednoczesnego dostępu.

**Pomocnicy mock:**
```c
bool    hal_mock_i2c_slave_is_initialized(void);                                       // stan init dla magistrali 0
bool    hal_mock_i2c_slave_is_initialized_bus(uint8_t bus);
uint8_t hal_mock_i2c_slave_get_reg(uint8_t reg);                                       // odczytaj rejestr bezpośrednio (magistrala 0)
uint8_t hal_mock_i2c_slave_get_reg_bus(uint8_t bus, uint8_t reg);
void    hal_mock_i2c_slave_set_reg(uint8_t reg, uint8_t value);                         // zapisz rejestr bezpośrednio (magistrala 0)
void    hal_mock_i2c_slave_set_reg_bus(uint8_t bus, uint8_t reg, uint8_t value);
uint8_t hal_mock_i2c_slave_get_reg_ptr(void);                                          // bieżący wskaźnik (magistrala 0)
uint8_t hal_mock_i2c_slave_get_reg_ptr_bus(uint8_t bus);
void    hal_mock_i2c_slave_simulate_receive(const uint8_t *data, int len);              // symuluj zapis mastera
void    hal_mock_i2c_slave_simulate_receive_bus(uint8_t bus, const uint8_t *data, int len);
int     hal_mock_i2c_slave_simulate_request(uint8_t *out_buf, int max_len);             // symuluj odczyt mastera
int     hal_mock_i2c_slave_simulate_request_bus(uint8_t bus, uint8_t *out_buf, int max_len);
```

---

## `hal_swserial` - UART programowy  *(opcjonalnie - `HAL_ENABLE_SWSERIAL`)*

Stałe formatu ramki UART dla `config` są zdefiniowane w
`hal/serial/hal_uart_config.h`.

```c
#include <hal/serial/hal_uart_config.h>

// 5/6/7/8 bitów danych, parzystość N/E/O, 1/2 bity stopu
HAL_UART_CFG_5N1  HAL_UART_CFG_6N1  HAL_UART_CFG_7N1  HAL_UART_CFG_8N1
HAL_UART_CFG_5N2  HAL_UART_CFG_6N2  HAL_UART_CFG_7N2  HAL_UART_CFG_8N2
HAL_UART_CFG_5E1  HAL_UART_CFG_6E1  HAL_UART_CFG_7E1  HAL_UART_CFG_8E1
HAL_UART_CFG_5E2  HAL_UART_CFG_6E2  HAL_UART_CFG_7E2  HAL_UART_CFG_8E2
HAL_UART_CFG_5O1  HAL_UART_CFG_6O1  HAL_UART_CFG_7O1  HAL_UART_CFG_8O1
HAL_UART_CFG_5O2  HAL_UART_CFG_6O2  HAL_UART_CFG_7O2  HAL_UART_CFG_8O2
```

Wartości liczbowe zachowują swoje ustalone wartości publiczne.

```c
#include <hal/serial/hal_swserial.h>

typedef hal_swserial_impl_t *hal_swserial_t;  // nieprzezroczysty uchwyt

hal_status_t hal_swserial_create_ex(uint8_t rx_pin, uint8_t tx_pin,
                                    hal_swserial_t *out_handle);
hal_swserial_t hal_swserial_create(uint8_t rx_pin, uint8_t tx_pin);

hal_status_t hal_swserial_set_rx_ex(hal_swserial_t h, uint8_t rx_pin);
bool hal_swserial_set_rx(hal_swserial_t h, uint8_t rx_pin);
hal_status_t hal_swserial_set_tx_ex(hal_swserial_t h, uint8_t tx_pin);
bool hal_swserial_set_tx(hal_swserial_t h, uint8_t tx_pin);

hal_status_t hal_swserial_begin(hal_swserial_t h, uint32_t baud,
                                uint16_t config);  // np. HAL_UART_CFG_8N1
int  hal_swserial_available(hal_swserial_t h);

hal_status_t hal_swserial_read_ex(hal_swserial_t h, uint8_t *out_value);
int  hal_swserial_read(hal_swserial_t h);     // zwraca bajt (0-255) lub -1, gdy pusto
hal_status_t hal_swserial_write_ex(hal_swserial_t h, const uint8_t *data,
                                   size_t len, size_t *out_written);
size_t hal_swserial_write(hal_swserial_t h, const uint8_t *data, size_t len);
hal_status_t hal_swserial_println_ex(hal_swserial_t h, const char *s,
                                     size_t *out_written);
size_t hal_swserial_println(hal_swserial_t h, const char *s);
hal_status_t hal_swserial_flush(hal_swserial_t h);  // blokuje do zakończenia TX
void hal_swserial_destroy(hal_swserial_t h);
```

Nowy kod powinien preferować formy statusowe. `create_ex()` raportuje
`HAL_EINVAL` dla nieprawidłowych/nakładających się pinów i `HAL_ENOMEM`,
gdy instancja lub natywne zasoby PIO/DMA są wyczerpane. Zmiany pinów po
`begin()` raportują `HAL_ESTATE`; we/wy przed `begin()` raportuje
`HAL_EUNINIT`; pusty `read_ex()` raportuje `HAL_EAGAIN`. Zapis `write_ex()`
o zerowej długości jest ważny nawet ze wskaźnikiem data równym NULL.
Opcjonalny `out_written` z `println_ex()` liczy tylko bajty payloadu, bez
CRLF, aby zachować historyczną wartość zwracaną.

`begin()` i `flush()` historycznie zwracały `void`; teraz zwracają
bezpośrednio `hal_status_t`, więc istniejący wywołujący mogą nadal
ignorować wynik. Funkcje zwracające uchwyt, bool i wartość pozostają
wrapperami zgodności nad sąsiadującymi implementacjami statusowymi.
`available()` pozostaje tanim getterem odpytującym (0 dla nieprawidłowego
lub nieuruchomionego uchwytu), a `destroy()` pozostaje niezawodnym
sprzątaniem.

```c
hal_swserial_t port = NULL;
hal_status_t st = hal_swserial_create_ex(rx_pin, tx_pin, &port);
if (st == HAL_OK) {
    st = hal_swserial_begin(port, 9600, HAL_UART_CFG_8N1);
}

uint8_t byte = 0;
if (st == HAL_OK && hal_swserial_read_ex(port, &byte) == HAL_OK) {
    // odczytaj bajt
}
```

**impl/rp2040:** natywny backend Pico SDK. Dwie maszyny stanów PIO wykonują taktowanie bitów RX i TX, a DMA przenosi zakończone ramki RX do bufora cyklicznego. Nie wykonuje callbacku GPIO trwającego jeden bajt, pętli opóźniającej ani sekcji krytycznej. Każdy uchwyt wymaga dwóch wolnych maszyn stanów PIO i jednego wolnego kanału DMA; programy PIO są współdzielone między uchwytami na bloku PIO.

**impl/stm32g474 / impl/.mock:** współdzielony backend GPIO/taktowania/synchronizacji HAL. Mock udostępnia też deterministyczne pomocniki RX/TX dla testów hosta.

Publiczne operacje są serializowane przez mutex HAL per-instancja.
Create/destroy powinny stosować się do polityki serializacji setup/teardown
obowiązującej w całym projekcie.

**Pomocnicy mock:**
```c
void        hal_mock_swserial_push(hal_swserial_t h, const uint8_t *data, int len);
void        hal_mock_swserial_reset(hal_swserial_t h);
const char *hal_mock_swserial_last_write(hal_swserial_t h);
```

---

## `hal_uart` - sprzętowy UART  *(opcjonalnie - `HAL_ENABLE_UART`)*

```c
#include <hal/serial/hal_uart.h>

typedef enum {
    HAL_UART_PORT_1 = 1,
    HAL_UART_PORT_2 = 2,
} hal_uart_port_t;

typedef hal_uart_impl_t *hal_uart_t;

typedef struct {
    uint32_t rx_overrun;
    // Błędy ramkowania; błędy szumu STM32 są liczone tutaj też.
    uint32_t rx_framing;
    uint32_t rx_parity;
    // Jawny warunek break, gdy backend udostępnia flagę break.
    uint32_t rx_break;
    uint32_t rx_buffer_overflow;
} hal_uart_error_counters_t;

hal_uart_t hal_uart_create(hal_uart_port_t port, uint8_t rx_pin, uint8_t tx_pin);
bool hal_uart_set_rx(hal_uart_t h, uint8_t rx_pin);
bool hal_uart_set_tx(hal_uart_t h, uint8_t tx_pin);
hal_status_t hal_uart_begin(hal_uart_t h, uint32_t baud, uint16_t config);
int  hal_uart_available(hal_uart_t h);
int  hal_uart_read(hal_uart_t h);
size_t hal_uart_write(hal_uart_t h, const uint8_t *data, size_t len);
size_t hal_uart_println(hal_uart_t h, const char *s);
hal_status_t hal_uart_flush(hal_uart_t h);   // blokuje do zakończenia TX
bool hal_uart_get_error_counters(hal_uart_t h,
                                 hal_uart_error_counters_t *counters);
void hal_uart_destroy(hal_uart_t h);
```

`hal_uart_begin()` i `hal_uart_flush()` są status-first (zastąpiły dawną
parę `void` + `_ex`). Operacje `bool`/wartościowe (`set_rx`, `set_tx`,
`read`, `write`, `println`, `get_error_counters`) zachowują swoje
sygnatury zgodności i każda ma sąsiadujący wariant statusowy `_ex`.

**impl/rp2040:** UART SDK RP2040 (`uart0` / `uart1`) ze sterowanym
przerwaniami RX. `hal_uart_begin()` instaluje i włącza `UARTx_IRQ` w NVIC
wywołującego rdzenia, więc ten rdzeń staje się domyślnym właścicielem
przerwania RX UART. Powtórne `hal_uart_begin()` oraz wywołanie
`hal_uart_destroy()` muszą wystąpić na tym samym rdzeniu, który wykonał
udane begin. Obecne API UART nie zapisuje właściciela do celów
diagnostycznych i nie zwraca `HAL_ESTATE` dla wywołania cyklu życia z
niewłaściwego rdzenia; sama serializacja międzyrdzeniowa nie zmienia tego
wymogu przypisania przerwania. W buildach FreeRTOS/SMP wykonuj operacje
cyklu życia UART z zadania przypiętego do zamierzonego rdzenia i nie
migruj tego zadania, gdy UART jest aktywny.
**impl/stm32g474:** USART1/USART2 na poziomie rejestrów, korzystające z odpowiednich źródeł 170 MHz PCLK2/PCLK1, oraz odpytywane drenowanie RX; liczy ORE, PE, FE, NE i jawne flagi LIN-break, gdy są raportowane przez USART_ISR.
**impl/esp32:** Porty HAL 1/2 mapują się na ESP-IDF UART1/UART2; UART0
pozostaje zarezerwowane poza tym API HAL. Każdy aktywny uchwyt
posiada 512-bajtowy bufor RX IDF i 32-wpisową kolejkę zdarzeń używaną do
akumulowania liczników overrun, przepełnienia bufora, break, framing i
parity. Publiczne we/wy jest serializowane mutexem per-instancja. Rdzeń,
który pomyślnie wywołuje `hal_uart_begin()`, jest właścicielem sprzątania
cyklu życia; ponowne begin z niewłaściwego rdzenia raportuje `HAL_ESTATE`,
podczas gdy destroy z niewłaściwego rdzenia wywołuje asercję i pozostawia
uchwyt aktywny. Przypisanie pinów jest akceptowane tylko przed begin,
chyba że wartość pinu jest niezmieniona.
**impl/.mock:** bufor pierścieniowy plus przechwytywanie ostatniego zapisu; wstrzykiwalny przez pomocnicze funkcje mock.
**Liczniki błędów:** kumulatywne od `hal_uart_begin()`; reset mocka też je czyści.
**Thread safety:** Przenośni wywołujący nadal powinni serializować cykl życia i współdzielone użycie uchwytu. ESP32-S3 serializuje operacje wejścia/wyjścia per instancja w runtime, ale ta blokada nie zastępuje reguły tego samego rdzenia dla cyklu życia. RP2040 pozostaje serializowany przez wywołującego, a jego reguła tego samego rdzenia dla cyklu życia również obowiązuje.

**Pomocnicy mock:**
```c
void        hal_mock_uart_push(hal_uart_t h, const uint8_t *data, int len);
void        hal_mock_uart_reset(hal_uart_t h);
const char *hal_mock_uart_last_write(hal_uart_t h);
uint8_t     hal_mock_uart_get_rx_pin(hal_uart_t h);
uint8_t     hal_mock_uart_get_tx_pin(hal_uart_t h);

typedef void (*hal_mock_uart_write_cb_t)(hal_uart_t h, const char *text, void *user);
void        hal_mock_uart_set_write_callback(hal_uart_t h,
                                             hal_mock_uart_write_cb_t cb,
                                             void *user);
```

---

## `hal_onewire` - magistrala 1-Wire  *(opcjonalnie - `HAL_ENABLE_ONEWIRE`)*

Thread-safe wrapper dla jednej magistrali 1-Wire związanej z
pojedynczym pinem GPIO. Buildy sprzętowe używają współdzielonego drivera
bit-bang tylko-HAL w `src/hal/onewire/`; backend mock utrzymuje
deterministyczne skryptowane odpowiedzi dla testów hosta.

```c
#include <hal/onewire/hal_onewire.h>

typedef struct hal_onewire_impl_s *hal_onewire_t;

hal_onewire_t hal_onewire_init(uint8_t data_pin);
void          hal_onewire_deinit(hal_onewire_t h);

bool    hal_onewire_reset(hal_onewire_t h);
void    hal_onewire_select(hal_onewire_t h, const uint8_t rom[8]);
void    hal_onewire_skip(hal_onewire_t h);
void    hal_onewire_write(hal_onewire_t h, uint8_t value, bool power);
size_t  hal_onewire_write_bytes(hal_onewire_t h, const uint8_t *data,
                                uint16_t len, bool power);
uint8_t hal_onewire_read(hal_onewire_t h);
size_t  hal_onewire_read_bytes(hal_onewire_t h, uint8_t *out, uint16_t len);
void    hal_onewire_write_bit(hal_onewire_t h, uint8_t bit);
uint8_t hal_onewire_read_bit(hal_onewire_t h);
void    hal_onewire_depower(hal_onewire_t h);

void    hal_onewire_reset_search(hal_onewire_t h);
void    hal_onewire_target_search(hal_onewire_t h, uint8_t family_code);
bool    hal_onewire_search(hal_onewire_t h, uint8_t out_rom[8],
                           bool search_mode);
```

> **Pomocniki CRC przeniesione.** Rutyny Dallas/Maxim CRC-8 i CRC-16, które
> kiedyś żyły tutaj, są teraz ogólne i żyją w `hal_crc.h`
> (`hal_crc8_maxim`, `hal_crc16_maxim`, `hal_crc16_maxim_check`). Zobacz
> [Narzędzia -> `hal_crc`](16_utilities.md).

**impl/rp2040 + impl/stm32g474:** Oba delegują do tego samego
współdzielonego drivera. Driver używa przełączania wejście/wyjście
HAL GPIO, taktowania slotów `hal_delay_us()` i sekcji krytycznych HAL
wokół wrażliwych na czas pod-slotów. Zewnętrzne podciągnięcie 1-Wire jest
nadal oczekiwane, zgodnie z oryginalnym modelem elektrycznym OneWire.
**impl/.mock:** Skryptowane odpowiedzi obecności/odczytu/wyszukiwania.
**Thread safety:** Buildy sprzętowe używają mutexu per-uchwyt i
współdzielonego mutexu magistrali wokół operacji publicznych. DS18B20
używa własnej niskopoziomowej instancji drivera, więc wieloetapowe
transakcje scratchpad pozostają atomowe pod mutexem DS18B20.

---


---

*Dalej: [CAN i wyświetlacz](10_can_display.md)*
