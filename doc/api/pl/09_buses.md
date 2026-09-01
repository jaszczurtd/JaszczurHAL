# Magistrale komunikacyjne

*Dostępne również [po angielsku](../en/09_buses.md).*

> **Część [Dokumentacji API JaszczurHAL](../../pl/JaszczurHAL_API.md)**

Obejmuje: `hal_spi`, `hal_spi_device`, `hal_i2c`, `hal_i2c_slave`, `hal_uart`,
`hal_swserial`, `hal_onewire`.

## `hal_spi` - magistrala SPI i API transferu

```c
#include <hal/spi/hal_spi.h>

// Operacje, które mogą się nie udać, zwracają teraz status zamiast void.
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

// Funkcjom zgodności zwracającym wartość lub bool odpowiadają warianty _ex ze statusem.
hal_status_t hal_spi_transfer_ex(uint8_t bus, uint8_t data,
                                 uint8_t *out_received);
hal_status_t hal_spi_transfer16_ex(uint8_t bus, uint16_t data,
                                   uint16_t *out_received);
hal_status_t hal_spi_write_dma_ex(uint8_t bus, const uint8_t *data, size_t len);
hal_status_t hal_spi_write_dma_async_start_ex(uint8_t bus,
                                              const uint8_t *data, size_t len);
hal_status_t hal_spi_write_dma_async_wait_ex(uint8_t bus);

// Skonfiguruj piny i uruchom magistralę SPI w trybie master.
// Magistrale 0/1 odpowiadają RP SPI0/1, STM32G474 SPI1/2 lub ESP32-S3 SPI2/3.
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

Obsługiwane są tylko magistrale 0 i 1. Dla innych wartości funkcje statusowe
zwracają `HAL_EINVAL`. Niskopoziomowe funkcje synchronizacji i zwalniania
zasobów nadal wywołują asercję w buildach z włączonymi kontrolami.

API zwraca `HAL_EINVAL` dla nieprawidłowej magistrali, ustawień lub wskaźnika
wyjściowego, a także dla wskaźnika `NULL` przy niezerowym rozmiarze bufora.
Próba uruchomienia asynchronicznego DMA podczas aktywnego transferu zwraca
`HAL_EBUSY`. Operacje o zerowej długości kończą się powodzeniem i nie wymagają
bufora.
Na STM32G474 polling oraz oczekiwanie na DMA mogą dodatkowo zwrócić
`HAL_ETIMEOUT`, a błąd transferu DMA powoduje zwrot `HAL_EIO`. Blokujące
wywołania SDK na RP2040 zwracają `HAL_EIO`, jeśli przesłały mniej elementów,
niż żądano. Każdy backend zwraca tylko błędy, które potrafi wiarygodnie
rozpoznać.

**Zapisy DMA:** `hal_spi_write_dma()` jest wygodnym wariantem blokującym. Uruchamia
najszybszy mechanizm zapisu dostępny w backendzie i wraca dopiero wtedy, gdy backend
przyjmie lub prześle bufor. Funkcje `hal_spi_write_dma_async_*()` udostępniają wariant
nieblokujący, jeśli backend go obsługuje. Po udanym `_async_start()` bufor `data` musi
pozostać dostępny i niezmieniony, dopóki `_async_busy()` nie zwróci `false` albo nie
zakończy się `_async_wait()`. Nie wolno też rozpoczynać drugiego
asynchronicznego zapisu na tej samej magistrali, dopóki poprzedni jest
aktywny. Backendy bez asynchronicznego DMA wykonują zapis wewnątrz
`_async_start()`, zwracają `_async_busy() == false`, a `_async_wait()`
zwraca sterowanie natychmiast.

- **impl/rp2040:** natywne `hardware/spi.h` z Pico SDK na SPI0/SPI1 wraz
  z konfiguracją funkcji pinów przez `hardware/gpio.h`.
  `hal_spi_write_dma_async_start()` używa SPI TX DMA dla strumieni bajtów
  w kolejności MSB-first i wraca, zanim magistrala przejdzie w stan
  bezczynności. Przed zakończeniem transakcji lub zwolnieniem kanału
  `hal_spi_end_transaction()` i `hal_spi_deinit()` czekają na zakończenie
  aktywnego asynchronicznego TX DMA.
- **impl/stm32g474:** SPI1/SPI2 master na poziomie rejestrów, 8-bitowy
  full-duplex, programowe NSS, transfer w trybie pollingu, konfiguracja pinów
  AF5 i sterowany przerwaniami TX DMA. SPI1 jest zasilane z 170 MHz PCLK2, a
  SPI2 z 170 MHz PCLK1; backend wybiera najszybszy preskaler będący potęgą
  dwójki, który nie przekracza żądanego zegara.
  Domyślne piny: magistrala SPI 0 = PA6/PA7/PA5, magistrala 1 = PB14/PB15/PB13.
  TX SPI1 rezerwuje DMA1 Channel7, a TX SPI2 rezerwuje DMA1 Channel8.
  Odpowiadające im żądania DMAMUX i przerwania DMA pozwalają dzielić bufory większe
  niż limit sprzętowego licznika 65 535 bajtów na kolejne transfery.
  Wywołanie asynchroniczne
  zwraca sterowanie, zanim magistrala stanie się bezczynna, natomiast zakończenie
  transakcji i deinit czekają na zakończenie. Bufory w SRAM CCM dostępnym
  tylko dla CPU używają synchronicznej ścieżki pollingu, ponieważ DMA1 nie ma
  dostępu do tej pamięci.
- **impl/esp32:** ESP-IDF SPI master na SPI2/SPI3 dla magistrali HAL 0/1.
  Maski płytki i targetu walidują piny MISO/MOSI/SCK; chip select pozostaje pod
  przenośną warstwą GPIO `hal_spi_device`. Magistrala żąda automatycznego
  kanału DMA IDF i przechodzi na pracę bez DMA, jeśli kanał jest niedostępny.
  API asynchronicznego DMA w HAL używa udokumentowanego synchronicznego fallbacku:
  `_async_start()` kończy zapis przed powrotem, `_async_busy()` jest zawsze
  `false`, a `_async_wait()` zwraca sterowanie natychmiast. Transfery są
  dzielone na fragmenty po 64 bajty.
- **impl/.mock:** przechowuje stan inicjalizacji i ustawienia, głębokość
  blokady, zaprogramowane bajty RX oraz log TX używany w testach.

**Thread safety:** `hal_spi_begin_transaction()` ustawia parametry magistrali,
ale jej nie blokuje. Wieloetapowe operacje drivera na wspólnej
magistrali należy otoczyć wywołaniami `hal_spi_lock()` i `hal_spi_unlock()`.
Asynchroniczny transfer DMA należy do tej samej transakcji i sekcji krytycznej:
chip-select musi pozostać aktywny, a magistrala zablokowana aż do zakończenia
`_async_wait()`.

---

## `hal_spi_device` - przenośny deskryptor urządzenia SPI

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

Deskryptor wiąże magistralę, pin chip-select aktywny stanem niskim oraz
rzeczywiście używane ustawienia SPI. `HAL_SPI_DEVICE_CS_NONE` oznacza
urządzenie bez sygnału CS zarządzanego przez HAL. Podczas inicjalizacji pin CS
jest konfigurowany jako nieaktywne wyjście w stanie wysokim. Deskryptor jest
niezależny od platformy i korzysta z istniejących API backendów SPI i GPIO.
Funkcje `hal_spi_device_acquire()` i `hal_spi_device_release()` pozwalają
ręcznie zarządzać wieloetapowym transferem i zwracają status każdej operacji.
`hal_spi_device_transaction()` wykonuje operacje READ, WRITE, TRANSFER
i TRANSFER_IN_PLACE w ramach jednego przejęcia magistrali.

Po ręcznej operacji wejścia/wyjścia `hal_spi_device_finish()` zawsze zwalnia
zasoby. Jeśli wcześniej wystąpił błąd operacji, późniejszy błąd kończenia go
nie zastępuje. Po udanym przejęciu magistrali każda droga zakończenia
transakcji wywołuje funkcję kończącą backendu, dezaktywuje CS i odblokowuje
magistralę.

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

// Operacje, które mogą się nie udać, zwracają teraz status zamiast void.
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

// Warianty statusowe dotychczasowych operacji zwracających wartość lub bool.
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

// Ręczne lock/unlock - użyj z biblioteką zewnętrzną, która bezpośrednio
// korzysta z natywnego obiektu magistrali backendu.
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

Obsługiwane są tylko wartości magistrali 0 i 1. Inne wartości są błędami
programisty i wywołują `HAL_ASSERT` w buildach z włączonymi kontrolami.

Warianty `_ex` zwracają `HAL_OK` przy powodzeniu, a w pozostałych przypadkach
przekazują nowemu kodowi dokładniejszą diagnostykę przez `hal_status_t`.
Nieprawidłowa magistrala lub bufor powoduje zwrot `HAL_EINVAL`. Jeśli backend
potrafi wykryć użycie niezainicjalizowanej magistrali, zwraca `HAL_EUNINIT`.
Dotychczasowy `HAL_I2C_ERROR_TIMEOUT` odpowiada `HAL_ETIMEOUT`, ogólny błąd
NACK lub magistrali - `HAL_EBUS`, a pozostałe błędy backendu - `HAL_EIO`.
Dla zgodności źródłowej dotychczasowe funkcje nadal zwracają `void`, `uint8_t`
lub `bool`.

`hal_i2c_scan()` zastępuje dawny helper `i2cScanner()` z `tools.cpp`.
Wykonuje pojedynczy skan zamiast nieskończonej pętli print/delay, pomija
zarezerwowane adresy 7-bitowe, nie zależy od wyjścia szeregowego i działa na
obu kontrolerach. Gdy bufor wynikowy jest za mały, zwraca `HAL_EOVERFLOW`.
Opcjonalny callback pozwala aplikacji jawnie odświeżać watchdog. To aplikacja
odpowiada za prezentację wyników i harmonogram skanowania:

```c
uint8_t addresses[HAL_I2C_SCAN_ADDRESS_COUNT];
size_t found = 0;
hal_status_t status =
    hal_i2c_scan(addresses, HAL_I2C_SCAN_ADDRESS_COUNT, &found,
                 hal_watchdog_feed);
```

**Inicjalizacja:** `hal_i2c_init*()` tworzy mutex magistrali, konfiguruje
SDA/SCL i zegar oraz uruchamia kontroler backendu. Funkcję należy wywołać
podczas konfiguracji, przed rozpoczęciem zwykłej komunikacji I2C. Jeżeli API
zostanie użyte wcześniej, mechanizm fallback tworzy mutex atomowo przy
pierwszym wywołaniu. Do zmiany częstotliwości skonfigurowanej magistrali służą
`hal_i2c_set_clock()` i `hal_i2c_set_clock_bus()`. Zmiana odbywa się pod
ochroną mutexu HAL tej magistrali.

**Tryby zegara:** Nazwane stałe zegara odpowiadają trybom ze specyfikacji
magistrali I2C: Standard-mode (100 kHz), Fast-mode (400 kHz), Fast-mode
Plus / Fm+ (1 MHz) i High-speed mode / Hs-mode (3,4 MHz). 1 MHz i 3,4 MHz to
rzeczywiste przypadki użycia: Fm+ jest powszechne dla szybszych peryferiów
na poziomie płytki i buforów magistrali, podczas gdy Hs-mode występuje w
czujnikach o wysokiej szybkości, takich jak niektóre czujniki środowiskowe
Bosch i czujniki ruchu ST. Zawsze sprawdź kontroler, prowadzenie ścieżek na płytce,
podciągnięcia (pull-up), pojemność magistrali i arkusz danych każdego
urządzenia przed wyborem tych prędkości. W szczególności Hs-mode ma
wymagania dotyczące protokołu i czasów sygnałów, których nie spełni samo ustawienie
wyższej częstotliwości. Przed użyciem trzeba więc sprawdzić, czy dany backend i kontroler
obsługują ten tryb na wybranej platformie.

**Odniesienie:** NXP UM10204, "I2C-bus specification and user manual",
definiuje Standard-mode, Fast-mode, Fast-mode Plus i High-speed mode.

**Adresowanie 10-bitowe (`HAL_ENABLE_I2C_10BIT`, opcjonalne):** Szerokość
adresowania jest właściwością zainicjalizowanego kontrolera, nigdy
pojedynczego wywołania ani pojedynczej wartości adresu: `hal_i2c_init()`/
`hal_i2c_init_bus()` ustawiają magistralę w trybie 7-bitowym (`0x000..0x07F`),
`hal_i2c_init_10bit()`/`hal_i2c_init_bus_10bit()` ustawiają tryb 10-bitowy
(`0x000..0x3FF`). Ta sama wartość liczbowa adresu jest interpretowana inaczej
w zależności od użytego wariantu inicjalizacji, a
kontroler nigdy nie miesza urządzeń 7- i 10-bitowych naraz.

We wszystkich dotychczasowych funkcjach przyjmujących adres typ parametru
zmieniono z `uint8_t` na nowy `hal_i2c_address_t` (`uint16_t`). Jest to
świadoma niezgodność na poziomie typu. Typowe wywołania przekazujące literał
lub zmienną `uint8_t` po ponownym buildzie nadal nie wymagają zmian w kodzie.

`hal_i2c_scan()` i `hal_i2c_scan_bus()` zawsze obsługują wyłącznie adresy
7-bitowe. Wywołane na magistrali 10-bitowej zwracają `HAL_EUNSUPPORTED`.
Przełączenie magistrali między trybami
wymaga jawnej ponownej inicjalizacji, która przechodzi normalny cykl
zatrzymania/resetu i unieważnia stan związany z poprzednim trybem.

- **impl/rp2040:** natywne `hardware/i2c.h` z Pico SDK obsługuje I2C0/I2C1,
  a `hardware/gpio.h` konfiguruje funkcje pinów. Każda magistrala ma własny
  mutex chroniący wszystkie transakcje. Żądana częstotliwość powyżej
  Fast-mode Plus jest ograniczana do 1 MHz, ponieważ kontroler I2C w RP2040
  nie obsługuje Hs-mode. Przed przywróceniem pinom funkcji I2C
  `hal_i2c_bus_clear()` próbuje odblokować linie SCL/SDA w trybie GPIO.
  W trybie 10-bitowym inicjalizacja ustawia bit DesignWare
  `IC_CON.IC_10BITADDR_MASTER`, który pozostaje aktywny przez cały runtime
  kontrolera. Transfer korzysta wtedy z osobnej, niskopoziomowej obsługi FIFO
  i timeoutu. Rejestr `IC_TAR` zawiera pełny adres 10-bitowy. Nie można użyć
  standardowych funkcji Pico SDK `i2c_write_timeout_us()` ani
  `i2c_read_timeout_us()`, ponieważ wymagają adresu 7-bitowego.
- **impl/stm32g474:** kontroler I2C v2 na I2C1/I2C2 działa w trybie master
  i jest obsługiwany bezpośrednio przez rejestry. Oba kontrolery wybierają
  HSI16 jako źródło zegara, dlatego sprawdzone ustawienia `TIMINGR` dla 16 MHz
  nie zależą od zegara APB 170 MHz. Backend sprawdza funkcje alternatywne pinów
  SDA/SCL i konfiguruje je jako open-drain z podciągnięciem. Na obu
  magistralach obsługuje wszystkie poziomy częstotliwości HAL oraz operacje
  zapisu, odczytu, zapisu z odczytem i sprawdzania zajętości. Przed
  inicjalizacją może również odblokować magistralę w trybie GPIO, używając
  mikrosekundowych opóźnień niezależnych od częstotliwości zegara.
  Tryb 10-bitowy ustawia `CR2.ADD10` i zapisuje adres bez przesunięcia
  w `CR2.SADD[9:0]`; tryb 7-bitowy zachowuje dotychczasowe przesunięcie do
  `SADD[7:1]`. Bit `CR2.HEAD10R` celowo pozostaje wyzerowany. Driver
  `i2c-stm32f7` z mainline kernela Linux działa tak samo dla tego kontrolera
  I2C v2: w każdej fazie wysyła pełny nagłówek 10-bitowy.
- **impl/esp32:** API master/controller ESP-IDF na I2C0/I2C1 z generowaną
  walidacją pinów płytki, wewnętrznymi podciągnięciami, buforowanymi
  uchwytami urządzeń, timeoutem transferu/sondy 100 ms i resetem
  kontrolera po wystąpieniu timeoutu. Zmiany zegara odrzucają buforowane uchwyty urządzeń,
  więc kolejne transfery odtwarzają je z nową szybkością. Czyszczenie
  magistrali na poziomie GPIO generuje do dziewięciu impulsów SCL i jest
  akceptowane tylko, gdy wybrany kontroler jest zdeinicjalizowany. Tryb
  10-bitowy konfiguruje każdy buforowany uchwyt urządzenia z
  `I2C_ADDR_BIT_LEN_10`; bufor uchwytów urządzeń poszerza się z 128 do 1024
  wpisów tylko wtedy, gdy włączono `HAL_ENABLE_I2C_10BIT`, więc buildy
  wyłącznie 7-bitowe zajmują mniej pamięci. `i2c_master_probe()` z
  ESP-IDF zawsze sonduje jako adres 7-bitowy niezależnie od przekazanej
  wartości. Dlatego na magistrali 10-bitowej `hal_i2c_is_busy_bus()` zamiast
  prawdziwej, zero-bajtowej sondy wykonuje odczyt jednego bajtu przez poprawnie
  skonfigurowany uchwyt urządzenia.
- **impl/.mock:** bufor pierścieniowy z funkcjami do podawania danych w testach.
  Kolejne transakcje `request`/`read` pobierają po kolei bajty RX, co pozwala testowi
  odtworzyć sekwencję operacji na wielu rejestrach.
  `hal_i2c_end_transmission()` zwraca `HAL_I2C_ERROR_GENERIC`, gdy
  w implementacji testowej ustawiono flagę zajętości, a w przeciwnym razie
  `HAL_I2C_RESULT_OK`. `hal_i2c_bus_clear()` zwiększa wewnętrzny licznik
  dostępny przez `hal_mock_i2c_get_bus_clear_count()`, natomiast
  `hal_i2c_init()` go zeruje.

**Thread safety:** Backendy sprzętowe chronią transfery wewnętrznym
`hal_mutex_t` przypisanym do magistrali. Użyj `hal_i2c_lock()`
i `hal_i2c_unlock()`, jeśli sekcja krytyczna ma objąć również bezpośrednie
wywołania backendu lub zewnętrznej biblioteki. `hal_i2c_init*()`
i `hal_i2c_deinit*()` rekonfigurują wspólny obiekt magistrali, dlatego podczas
konfiguracji i zwalniania zasobów aplikacja musi serializować te wywołania.
Implementacja testowa nie synchronizuje dostępu współbieżnego.

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

PCF8574 nie ma mapy rejestrów, więc w transakcji podaje się tylko jego adres
I2C. Pojedynczy bajt zapisu steruje wszystkimi 8 zatrzaskami wyjściowymi,
a pojedynczy bajt odczytu zwraca bieżącą wartość portu. Dzięki
`hal_i2c_write_byte()` i `hal_i2c_read_byte()` driver nie musi jawnie wykonywać
sekwencji begin/write/end ani request/read.

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

Uwaga: helpery korzystają z *wewnętrznego* mutexu HAL osobnego dla każdej
magistrali, który obejmuje pojedynczą sekwencję `begin`/`end`. Kod, który
przeplata zapis i następujący po nim odczyt z inną wieloetapową transakcją na
tej samej magistrali (np. ustaw wskaźnik rejestru -> odczytaj N bajtów), musi
dodatkowo zserializować obie sekwencje
mutexem należącym do kodu wywołującego, ponieważ mutex HAL jest zwalniany przy
każdym `end_transmission`.

---

## `hal_i2c_slave` - slave/target I2C z mapą rejestrów  *(opcjonalnie - `HAL_ENABLE_I2C_SLAVE`)*

Udostępnia mapę rejestrów o stałym rozmiarze w trybie slave I2C. Zdalny
master zapisuje jednobajtowy wskaźnik rejestru, a następnie odczytuje N
bajtów od wskazanego adresu. Wskaźnik przesuwa się z każdym bajtem taktowanym
przez mastera i zachowuje wartość po warunku STOP. Późniejszy odczyt bez
poprzedzającego zapisu adresu jest więc kontynuowany od ostatniej pozycji.
Odczyt nie może wyjść poza skonfigurowaną mapę rejestrów.

API trybu target w ESP-IDF podaje liczbę bajtów przyjętych do FIFO lub bufora
pierścieniowego TX, ale nie informuje, ile z nich kontroler rzeczywiście
wysłał przez magistralę. Backend ESP32 przechowuje więc programowy wskaźnik
pierwszego rejestru, którego danych ESP-IDF jeszcze nie przyjęło. Bajty
oczekujące na wysłanie pozostają w FIFO lub buforze pierścieniowym także po
warunku STOP, dlatego pozycja widziana na magistrali się nie zmienia.

Zapis nowego wskaźnika rejestru usuwa oczekującą końcówkę i kolejkuje aktualne
dane od wybranego rejestru. Lokalne wywołania `reg_write*()` nie zmieniają
bajtów już umieszczonych w kolejce. Aby je odświeżyć, master musi ponownie
wybrać rejestr.

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

Obsługiwane są tylko wartości magistrali 0 i 1. Inne wartości są błędami
programisty i wywołują `HAL_ASSERT` w buildach z włączonymi kontrolami.

**Protokół mapy rejestrów (I2C):**
1. Master zapisuje `[reg_address]`, ustawiając wskaźnik rejestru.
2. Master odczytuje N bajtów, a slave odpowiada
   `regs[ptr], regs[ptr+1], ...`.
3. Master zapisuje `[reg_address, data0, data1, ...]`, ustawiając wskaźnik,
   a następnie kolejno zapisując dane.

- **impl/rp2040:** natywny tryb peryferyjny `hardware/i2c.h` z Pico SDK na
  I2C0/I2C1 wraz z obsługą zdarzeń przez `hardware/irq.h`. Przerwania RX FIFO,
  read-request, START i STOP/TX-abort bezpośrednio sterują protokołem mapy
  rejestrów.
- **impl/stm32g474:** tryb target kontrolera I2C v2 na I2C1/I2C2 jest
  obsługiwany bezpośrednio przez rejestry. Backend wybiera HSI16 jako źródło
  zegara, konfiguruje funkcje alternatywne SDA/SCL, własny adres oraz
  zachowawcze ustawienie `TIMINGR`. Włącza przerwania RX, TX, ADDR, STOP, NACK
  i błędów oraz opróżnia TXDR po NACK lub STOP. Protokół mapy
  rejestrów wykonują handlery przerwań I2C EV/ER.
- **impl/esp32:** urządzenia I2C w trybie target obsługiwane przez ESP-IDF na
  kontrolerze 0/1. Callbacki `receive`/`request` wykonują w ISR tylko ograniczony
  zakres pracy i sygnalizują po jednym wątku FreeRTOS dla każdej aktywnej
  magistrali. Wątek resetuje FIFO
  TX albo zapisuje bieżącą zawartość mapy rejestrów przez oficjalny driver
  trybu target. Zapis nowego wskaźnika rejestru usuwa nieaktualne dane TX
  z kolejki przed dodaniem kolejnego zestawu. Jeśli ESP-IDF przyjmie tylko
  część danych, programowy wskaźnik przesuwa się o liczbę przyjętych bajtów;
  pozycja pozostałych nie zmienia się nawet po STOP. Sprawdzenie numeru
  generacji zabezpiecza ten stan przed równoczesnym zapisem nowego wskaźnika.
  Licznik transakcji obejmuje zakończone zapisy i przyjęte żądania odczytu.
  Podczas deinicjalizacji backend najpierw przestaje przyjmować zdarzenia
  z ISR, wyrejestrowuje callbacki i czeka na zakończenie pracy wątku. Następnie
  usuwa driver, zamykając okres, w którym mogą nadejść przerwania. Dopiero
  wtedy zwalnia obiekty synchronizacji wątku.
- **impl/.mock:** udostępnia bezpośredni dostęp do mapy rejestrów oraz funkcje
  symulujące zapis i odczyt mastera.

**Thread safety:** W backendach sprzętowych `reg_write*()`
i `reg_read*()` można bezpiecznie wywoływać z różnych zadań lub rdzeni. Mapę
rejestrów chroni krótko utrzymywana blokada backendu, używana również przez
callbacki magistrali i ISR. Dzięki temu handlery w buildach FreeRTOS nie
przejmują mutexów HAL. Podczas konfiguracji i zwalniania zasobów aplikacja
musi serializować `init` oraz `deinit`. Implementacja testowa nie synchronizuje
dostępu współbieżnego.

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

Nowy kod powinien preferować warianty zwracające status. `create_ex()` zwraca
`HAL_EINVAL` dla nieprawidłowych/nakładających się pinów i `HAL_ENOMEM`,
gdy instancja lub natywne zasoby PIO/DMA są wyczerpane. Zmiany pinów po
`begin()` zwracają `HAL_ESTATE`; operacja we/wy przed `begin()` zwraca
`HAL_EUNINIT`, a pusty `read_ex()` - `HAL_EAGAIN`. Zapis `write_ex()`
o zerowej długości jest ważny nawet ze wskaźnikiem `data` równym `NULL`.
Opcjonalny `out_written` z `println_ex()` liczy tylko bajty payloadu, bez
CRLF, aby zachować historyczną wartość zwracaną.

`begin()` i `flush()` historycznie zwracały `void`; teraz zwracają
bezpośrednio `hal_status_t`, więc istniejący wywołujący mogą nadal
ignorować wynik. Funkcje zwracające uchwyt, `bool` lub inną wartość pozostają
warstwą zgodności wywołującą odpowiednie funkcje statusowe. `available()`
jest lekkim getterem stanu opartym na odpytywaniu i zwraca 0 dla nieprawidłowego lub
nieuruchomionego uchwytu. `destroy()` zawsze kończy się powodzeniem.

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

**impl/rp2040:** natywny backend Pico SDK. Dwie maszyny stanów PIO taktują
bity RX i TX, a DMA przenosi odebrane ramki do bufora cyklicznego. Backend nie
używa callbacku GPIO trwającego przez cały bajt, pętli opóźniającej ani sekcji
krytycznej. Każdy uchwyt wymaga dwóch wolnych maszyn stanów PIO i jednego
wolnego kanału DMA. Programy PIO są wspólne dla uchwytów korzystających z tego
samego bloku PIO.

**impl/stm32g474 / impl/.mock:** wspólna implementacja oparta na GPIO,
pomiarze czasu i synchronizacji HAL. Wariant testowy udostępnia również
deterministyczne funkcje RX/TX używane w testach hostowych.

Publiczne operacje są serializowane przez osobny mutex HAL każdej instancji.
Wywołania `create` i `destroy` należy serializować zgodnie z przyjętą
w projekcie zasadą konfiguracji i zwalniania zasobów.

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

`hal_uart_begin()` i `hal_uart_flush()` zwracają status; zastąpiły dawną parę
`void` + `_ex`. Funkcje zwracające `bool` lub wartość (`set_rx`, `set_tx`, `read`,
`write`, `println`, `get_error_counters`) zachowują dotychczasowe sygnatury.
Każdej odpowiada wariant `_ex` zwracający status.

- **impl/rp2040:** UART SDK RP2040 (`uart0` / `uart1`) ze sterowanym
  przerwaniami RX. `hal_uart_begin()` instaluje i włącza `UARTx_IRQ` w NVIC
  rdzenia wywołującego, dlatego przerwanie RX UART zostaje do niego przypisane.
  Powtórne `hal_uart_begin()` oraz wywołanie
  `hal_uart_destroy()` muszą wystąpić na tym samym rdzeniu, który wykonał
  udane `begin()`. Obecne API UART nie zapisuje numeru tego rdzenia do celów
  diagnostycznych i nie zwraca `HAL_ESTATE` dla operacji cyklu życia wykonanej z
  niewłaściwego rdzenia; sama serializacja międzyrdzeniowa nie zmienia tego
  wymogu przypisania przerwania. W buildach FreeRTOS/SMP wykonuj operacje
  cyklu życia UART z zadania przypiętego do zamierzonego rdzenia i nie
  migruj tego zadania, gdy UART jest aktywny.
- **impl/stm32g474:** USART1/USART2 są obsługiwane bezpośrednio przez rejestry
  i taktowane odpowiednio z PCLK2/PCLK1 170 MHz. Dane RX są odbierane przez
  polling. Backend zlicza ORE, PE, FE i NE oraz jawne flagi LIN-break, jeśli
  udostępnia je `USART_ISR`.
- **impl/esp32:** Porty HAL 1/2 odpowiadają ESP-IDF UART1/UART2; UART0
  pozostaje zarezerwowany poza tym API HAL. Każdy aktywny uchwyt
  posiada 512-bajtowy bufor RX IDF i 32-wpisową kolejkę zdarzeń używaną do
  zliczania błędów overrun i przepełnienia bufora, sygnałów break oraz błędów
  ramkowania i parzystości.
  Publiczne operacje wejścia/wyjścia chroni osobny mutex każdej instancji.
  Rdzeń, który pomyślnie wywoła `hal_uart_begin()`, musi także wykonywać
  operacje kończące cykl życia. Ponowne `begin()` z innego rdzenia zwraca
  `HAL_ESTATE`, natomiast `destroy()` wywołane z innego rdzenia powoduje
  asercję i pozostawia uchwyt aktywny. Piny można zmieniać tylko przed
  `begin()`, chyba że nowa wartość pinu jest taka sama jak dotychczasowa.
- **impl/.mock:** bufor pierścieniowy i rejestrowanie ostatniego zapisu;
  funkcje testowe pozwalają podawać dane wejściowe.
- **Liczniki błędów:** kumulatywne od `hal_uart_begin()`; reset mocka też je czyści.

**Thread safety:** Kod przenośny powinien serializować operacje cyklu
życia oraz wspólny dostęp do uchwytu. Na ESP32-S3 operacje wejścia/wyjścia
w runtime chroni osobny mutex każdej instancji. Nadal jednak wszystkie
operacje cyklu życia muszą być wykonywane na tym samym rdzeniu. Na RP2040 za
serializację odpowiada wywołujący; tam również obowiązuje reguła jednego
rdzenia dla całego cyklu życia.

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

Warstwa thread-safe do obsługi pojedynczej magistrali 1-Wire podłączonej do
jednego pinu GPIO. Buildy sprzętowe korzystają ze
wspólnego drivera bit-bang opartego wyłącznie na HAL, znajdującego się
w `src/hal/onewire/`. Implementacja testowa udostępnia deterministyczne,
programowalne odpowiedzi do testów hostowych.

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

> **Helpery CRC przeniesiono.** Rutyny Dallas/Maxim CRC-8 i CRC-16 znajdowały
> się wcześniej tutaj, ale są teraz ogólnymi helperami w `hal_crc.h`
> (`hal_crc8_maxim`, `hal_crc16_maxim`, `hal_crc16_maxim_check`). Zobacz
> [Narzędzia -> `hal_crc`](16_utilities.md).

- **impl/rp2040 + impl/stm32g474:** oba korzystają z tego samego drivera.
  Implementacja przełącza GPIO HAL między wejściem i wyjściem, odmierza sloty
  przez `hal_delay_us()` i używa sekcji krytycznych HAL wokół części slotu,
  które wymagają precyzyjnego czasu. Zgodnie z modelem elektrycznym OneWire
  magistrala nadal wymaga zewnętrznego rezystora podciągającego.
- **impl/.mock:** programowalne odpowiedzi wykrywania obecności, odczytu
  i wyszukiwania.

**Thread safety:** Buildy sprzętowe chronią operacje publiczne
mutexem każdego uchwytu oraz wspólnym mutexem magistrali. DS18B20 używa
własnej niskopoziomowej instancji drivera, dlatego wieloetapowe transakcje
scratchpad pozostają atomowe pod ochroną mutexu DS18B20.

---


---

*Dalej: [CAN i wyświetlacz](10_can_display.md)*
