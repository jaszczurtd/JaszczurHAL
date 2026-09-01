# Przechowywanie danych

*Dostępne również [po angielsku](../en/14_storage.md).*

> **Część [Dokumentacji API JaszczurHAL](../../pl/JaszczurHAL_API.md)**

Obejmuje: `hal_eeprom`, `hal_kv`, `hal_littlefs`, `hal_sdlogger`.

Regiony aplikacji, OTA, LittleFS i EEPROM w wewnętrznej pamięci flash są rezerwowane
podczas linkowania. Na RP operacje kasowania i programowania korzystają ze
wspólnego koordynatora transakcji flash, który zabezpiecza drugi rdzeń,
wstrzymuje pracę USB, odrzuca aktywne konflikty DMA, maskuje lokalne
przerwania, a po zakończeniu przywraca poprzedni stan runtime. STM32G474
używa rezerwacji linkera wyrównanych do stron oraz usługi flash właściwej dla
tego targetu. Zobacz [mapę pamięci RP](../../../rp_native_lib/MEMORY_MAP.md) oraz
[mapę pamięci STM32G474](../../../stm32_lib/MEMORY_MAP.md).

## `hal_eeprom` - ujednolicony EEPROM  *(opcjonalny - `HAL_ENABLE_EEPROM`)*

Jednolite API dla trwałego, adresowanego bajtowo przechowywania danych.
Backend jest wybierany w runtime przez `hal_eeprom_init()`.

Wspólne, niezależne od targetu API odpowiada za ograniczanie operacji do dostępnego
zakresu, kodowanie liczb całkowitych, blokady, zarządzanie callbackiem i wybór backendu.
Przenośna obsługa AT24C256 korzysta z HAL I2C. Backendy flash RP, flash STM32G474 oraz
pamięci hosta zawierają tylko mechanizmy właściwe dla danego nośnika.

`HAL_EEPROM_FLASH` oznacza "użyj natywnej dla targetu emulacji EEPROM na
wewnętrznej pamięci flash" i jest przenośnym selektorem dla firmware'u RP
oraz STM32G474.

| Selektor backendu | RP2040/RP2350 | STM32G474 |
|---|---|---|
| `HAL_EEPROM_FLASH` | Skoordynowana rezerwacja wewnętrznej pamięci flash | Rezerwacja wewnętrznej pamięci flash |
| `HAL_EEPROM_STM32_FLASH` | Selektor specyficzny dla STM32; dla kodu przenośnego użyj `HAL_EEPROM_FLASH` | Rezerwacja wewnętrznej pamięci flash |
| `HAL_EEPROM_AT24C256` | Zewnętrzny AT24C256 przez HAL I2C | Zewnętrzny AT24C256 przez HAL I2C |

Zarówno RP2040, jak i STM32G474 mogą więc używać albo własnej wewnętrznej
pamięci flash, albo zewnętrznego układu AT24C256 przez to samo API
`hal_eeprom_*`. `hal_kv` opiera się na tym, który backend EEPROM został
wybrany.

```c
#include <hal/storage/hal_eeprom.h>

typedef enum {
    HAL_EEPROM_DEFAULT     = 0, // Domyślne trwałe przechowywanie dla targetu
    HAL_EEPROM_AT24C256    = 1, // Zewnętrzny EEPROM I2C AT24C256 - 32 KB
    HAL_EEPROM_FLASH       = 2, // Natywny dla targetu EEPROM na wewnętrznej pamięci flash
    HAL_EEPROM_STM32_FLASH = 3, // Emulacja EEPROM oparta na wewnętrznej flash STM32G474
} hal_eeprom_type_t;

// Inicjalizuje EEPROM. Wywołaj przed jakąkolwiek inną funkcją hal_eeprom_*.
// size:     używane dla EEPROM opartego na flash; przekaż 0, aby użyć całej
//           rezerwacji targetu. Ignorowane dla HAL_EEPROM_AT24C256 (zawsze 32768 bajtów).
// i2c_addr: 7-bitowy adres I2C układu AT24C256; ignorowane dla flash.
//           Przekaż 0, aby użyć domyślnego EEPROM_I2C_ADDRESS (0x50 z hal_config.h).
hal_status_t hal_eeprom_init(hal_eeprom_type_t type, uint16_t size,
                             uint8_t i2c_addr);

// Dostęp na poziomie bajtu
hal_status_t hal_eeprom_write_byte(uint16_t addr, uint8_t val);
uint8_t hal_eeprom_read_byte(uint16_t addr);

// Dostęp do liczby całkowitej 32-bitowej (little-endian, 4 bajty od addr)
hal_status_t hal_eeprom_write_int(uint16_t addr, int32_t val);
int32_t hal_eeprom_read_int(uint16_t addr);

// Zbiorczy dostęp bajtowy pod jedną wewnętrzną blokadą.
hal_status_t hal_eeprom_write_bytes(uint16_t addr, const uint8_t *data,
                                    uint16_t len);
hal_status_t hal_eeprom_read_bytes(uint16_t addr, uint8_t *out, uint16_t len);

// Zapisuje buforowane dane do pamięci nieulotnej.
// HAL_EEPROM_FLASH / natywny flash: zapisuje kopię z RAM do flash.
// HAL_EEPROM_AT24C256: brak działania (no-op).
hal_status_t hal_eeprom_commit(void);

// Zeruje cały EEPROM (wolne - nie używać w kodzie krytycznym czasowo).
hal_status_t hal_eeprom_reset(void);

// Zwraca rozmiar EEPROM w bajtach.
uint16_t hal_eeprom_size(void);
```

**Kolejność bajtów liczb całkowitych:** `hal_eeprom_write_int` /
`hal_eeprom_read_int` używają kolejności **little-endian** (LSB pod
najniższym adresem).

**Zapisywanie zmian (`commit`):** W backendach opartych na flash
`hal_eeprom_write_byte`, `hal_eeprom_write_int` i `hal_eeprom_write_bytes`
najpierw aktualizują bufor RAM. Wywołaj `hal_eeprom_commit()` raz po grupie
zapisów, aby utrwalić je we flash. Dla `HAL_EEPROM_AT24C256` dane są
zapisywane synchronicznie do układu, stronami; `hal_eeprom_commit()` nic wtedy nie robi.

**Implementacja RP:** `HAL_EEPROM_FLASH` używa ostatnich
`HAL_RP_FLASH_EEPROM_SIZE` bajtów fizycznej pamięci flash (domyślnie 4096
bajtów). Zapisy aktualizują kopię w RAM. Utrwalenie zmienionego stanu
wykonuje pełne kasowanie i programowanie partycji w jednej operacji
`jh_rp_flash_transaction_execute()`, więc rdzeń 1, przerwania, DMA i
TinyUSB podlegają tej samej polityce bezpieczeństwa co każda inna natywna
modyfikacja flash. Wygenerowany region linkera zapobiega zajęciu tej
rezerwacji przez obraz firmware'u.

**Implementacja STM32G474:** `HAL_EEPROM_FLASH` i `HAL_EEPROM_STM32_FLASH`
używają ostatnich stron wewnętrznej pamięci flash zarezerwowanych przez
skrypt linkera STM32. Domyślna rezerwacja ma
`HAL_STM32_FLASH_EEPROM_SIZE = 4096` bajtów, a rozmiar strony
`HAL_STM32_FLASH_PAGE_SIZE = 2048` bajtów. Zmniejsza to
pamięć flash dostępną dla kodu aplikacji o 4 KB. Jeśli rozmiar rezerwacji
zostanie zmieniony, utrzymuj synchronizację definicji buildu i symbolu
linkera oraz użyj wielokrotności rozmiaru strony flash STM32.

Linker STM32 obsługuje też osobną rezerwację LittleFS przed EEPROM. Utrzymuj
`HAL_STM32_FLASH_EEPROM_SIZE` i `HAL_STM32_FLASH_LITTLEFS_SIZE`
nienakładające się; EEPROM/KV i LittleFS nie współdzielą stron.

**Implementacja AT24C256:** jedna implementacja niezależna od targetu steruje
zewnętrznym układem poprzez prymitywy `hal_i2c_*` na obu targetach
sprzętowych. Zapisy są dzielone na granicach stron po 64 bajty, a układ jest
odpytywany o ACK z timeoutem (`HAL_AT24C256_WRITE_TIMEOUT_US`,
domyślnie 20000 us). Zapisy poza zakresem są przycinane; odczyty poza
zakresem zwracają bajty wypełnione zerami. Adres I2C AT24C256 to
`EEPROM_I2C_ADDRESS` (domyślnie `0x50`, zdefiniowane w `hal_config.h`), o
ile do `hal_eeprom_init()` nie przekazano jawnego adresu.

**Postęp długich operacji:** EEPROM nigdy nie karmi watchdoga automatycznie.
Użyj `hal_eeprom_set_progress_callback()` przed długimi zapisami, resetem
lub operacjami zatwierdzania flash, jeśli aplikacja chce odświeżać własny
watchdog lub raportować postęp. Pełny reset AT24C256 dotyka 512 stron i
może zająć kilka sekund.

**impl/.mock:** Wspólne API kieruje wywołania do backendu przechowującego dane w pamięci
(`MOCK_EEPROM_BUF_SIZE`, domyślnie 32768). Mock nie powiela obsługi
`hal_eeprom_*`.

**Thread safety:** Obie rodziny backendów są thread-safe i mogą działać na wielu rdzeniach.
Wspólny mutex chroni wybór backendu, aktywny rozmiar, callbacki, ograniczanie zakresu i
każdą operację. Transfery `HAL_EEPROM_AT24C256` dodatkowo używają mutexu magistrali
`hal_i2c`, a natywne backendy flash stosują mechanizm koordynacji właściwy dla targetu.
Callback postępu należy skonfigurować przed rozpoczęciem współbieżnego dostępu. Jest
wywoływany pod wspólnym mutexem i nie może
ponownie wejść do `hal_eeprom_*`.

### Pomocnicy mock

```c
#include <hal/impl/.mock/hal_mock.h>

// Odczytuje bajt bezpośrednio z zapasowego magazynu mocka.
uint8_t           hal_mock_eeprom_get_byte(uint16_t addr);
// Zwraca typ ustawiony przez hal_eeprom_init().
hal_eeprom_type_t hal_mock_eeprom_get_type(void);
// True, jeśli hal_eeprom_commit() zostało wywołane od ostatniego resetu.
bool              hal_mock_eeprom_was_committed(void);
// Czyści flagę zatwierdzenia (ponownie uzbraja sprawdzenie).
void              hal_mock_eeprom_clear_committed_flag(void);
// Zwraca liczbę zapisów bajtów od ostatniego resetu/czyszczenia licznika.
uint32_t          hal_mock_eeprom_get_write_count(void);
// Czyści licznik zapisów bajtów.
void              hal_mock_eeprom_clear_write_count(void);
// Resetuje cały stan mocka do wartości domyślnych (wyzerowana pamięć, brak typu, niezatwierdzone).
void              hal_mock_eeprom_reset(void);
```

**Przykład użycia:**
```c
// Natywna dla targetu rezerwacja EEPROM na wewnętrznej pamięci flash.
hal_eeprom_init(HAL_EEPROM_FLASH, 512, 0);
hal_eeprom_write_int(0, my_value);
hal_eeprom_commit();

// AT24C256 pod domyślnym adresem 0x50 (I2C musi być już zainicjalizowane przez hal_i2c_init):
hal_eeprom_init(HAL_EEPROM_AT24C256, 0, 0);            // 0 -> użyj EEPROM_I2C_ADDRESS
// lub z jawnym adresem (np. pin A0 podpięty do wysokiego -> 0x51):
hal_eeprom_init(HAL_EEPROM_AT24C256, 0, 0x51);
hal_eeprom_write_byte(0, 0xAB);
// zatwierdzenie niepotrzebne dla AT24C256
```

**Przykład: zapis i odczyt danych konfiguracyjnych**
```c
#include <hal/storage/hal_eeprom.h>

void example_eeprom(void) {
    // Inicjalizuje natywny dla targetu EEPROM flash (512 bajtów)
    hal_eeprom_init(HAL_EEPROM_FLASH, 512, 0);

    // Zapisuje wiele wartości pod różnymi adresami
    hal_eeprom_write_int(0, 12345);           // Zapisz int pod offsetem 0 (4 bajty)
    hal_eeprom_write_byte(4, 0x42);           // Zapisz bajt pod offsetem 4

    // Dla danych strukturalnych użyj zapisów tablic bajtów
    uint8_t config_data[16] = {
        0x01, 0x02, 0x03, 0x04,
        0x05, 0x06, 0x07, 0x08,
        0xFF, 0xFE, 0xFD, 0xFC,
        0x00, 0x00, 0x00, 0x00
    };
    hal_eeprom_write_bytes(8, config_data, sizeof(config_data));

    // Zatwierdza wszystkie buforowane zapisy flash naraz
    hal_eeprom_commit();

    // Odczytuje z powrotem wartości
    int32_t stored_int = hal_eeprom_read_int(0);
    uint8_t stored_byte = hal_eeprom_read_byte(4);

    uint8_t read_buffer[16];
    hal_eeprom_read_bytes(8, read_buffer, sizeof(read_buffer));

    hal_deb("Int: %ld, Byte: 0x%02x", stored_int, stored_byte);
}
```

**API zwracające status** (zobacz [Status API](01_status_api.md)):
`hal_eeprom` jest **modułem wzorcowym** dla migracji na funkcje zwracające status.
Punkty wejścia, które wcześniej zwracały `void` (`init`, `write_byte`, `write_int`,
`write_bytes`, `read_bytes`, `commit`, `reset`, `set_progress_callback`)
**teraz zwracają bezpośrednio `hal_status_t`**. Zmiana zachowuje zgodność źródłową:
istniejący kod nadal może ignorować wynik, a nowy może go sprawdzać. Backend AT24C256
przekazuje dzięki temu rzeczywiste błędy I2C (`HAL_EIO`), które dawne API `void` traciło.
Trzy gettery zwracające wartość zachowują sygnatury i mają dodatkowe warianty `_ex`:
`hal_eeprom_read_byte_ex`, `hal_eeprom_read_int_ex` oraz `hal_eeprom_size_ex`. Warianty te
zapisują wynik przez parametr wyjściowy. Operacja wykraczająca poza zakres nadal jest
przycinana tak samo jak wcześniej, ale dodatkowo zwraca `HAL_EOVERFLOW`.

```c
hal_eeprom_init(HAL_EEPROM_FLASH, 512, 0);   // zwraca HAL_OK / HAL_EINVAL

hal_status_t st = hal_eeprom_write_byte(600, 0x42);
// HAL_EUNINIT   -> hal_eeprom_init() jeszcze nie wywołane
// HAL_EOVERFLOW -> addr poza urządzeniem (zapis przycięty, jak wcześniej)
// HAL_EIO       -> błąd I2C AT24C256
// HAL_OK        -> zbuforowane; utrwal przez hal_eeprom_commit()

uint8_t value = 0;
if (hal_eeprom_read_byte_ex(10, &value) == HAL_OK) {
    use(value);              // HAL_EINVAL, jeśli wskaźnik wyjściowy jest NULL
}
```

---


## `hal_kv` - przechowywanie klucz-wartość na EEPROM  *(opcjonalny - `HAL_ENABLE_KV`)*

Thread-safe magazyn rekordów KV oparty na `hal_eeprom`, do którego dane są
wyłącznie dopisywane (append-only). Używa układu dwóch banków (dual-bank) z
nagłówkami i rekordami chronionymi CRC16. Automatyczne odśmiecanie (garbage
collection, GC) kompaktuje aktualne rekordy do drugiego banku, gdy
aktywnemu bankowi zabraknie miejsca.

```c
#include <hal/storage/hal_kv.h>

typedef struct {
    uint32_t generation;       // licznik generacji banku
    uint16_t used_bytes;       // bajty użyte w aktywnym banku
    uint16_t capacity_bytes;   // pojemność pojedynczego banku
    uint16_t key_count;        // liczba żywych kluczy
    uint32_t next_sequence;    // następny numer sekwencyjny rekordu
} hal_kv_stats_t;

bool hal_kv_init(uint16_t base_addr, uint16_t size_bytes);
bool hal_kv_set_u32(uint16_t key, uint32_t value);
bool hal_kv_get_u32(uint16_t key, uint32_t *out_value);
bool hal_kv_set_blob(uint16_t key, const uint8_t *data, uint16_t len);
bool hal_kv_get_blob(uint16_t key, uint8_t *out, uint16_t out_size, uint16_t *out_len);
bool hal_kv_delete(uint16_t key);
bool hal_kv_gc(void);
bool hal_kv_get_stats(hal_kv_stats_t *out_stats);
hal_status_t hal_kv_set_auto_commit(bool enabled);
bool hal_kv_commit(void);
```

- **Zależności:** `hal_eeprom`, `hal_sync`, `hal_serial`.

**Thread safety:** API jest thread-safe i może być używane z wielu rdzeni. Wszystkie
operacje chroni mutex singletona utworzony przez atomowy mechanizm jednokrotnej
inicjalizacji HAL. `hal_kv_init()` musi być wywołane
po `hal_eeprom_init()`.

**Pomijanie niezmienionych danych:** `hal_kv_set_u32` i `hal_kv_set_blob` nie zapisują
danych do EEPROM, jeśli wartość się nie zmieniła. Ogranicza to niepotrzebne zużycie
pamięci flash.

**Automatyczny `commit`:** Automatyczne utrwalanie zmian jest domyślnie włączone, zgodnie
z dotychczasowym zachowaniem. `hal_kv_set_auto_commit(false)` pozwala odłożyć fizyczny
zapis do EEPROM lub flash i połączyć kilka zmian. Utrwal je potem jednym wywołaniem
`hal_kv_commit()`.

**Przykład: przechowywanie klucz-wartość z liczbami całkowitymi i blobami**
```c
#include <hal/storage/hal_kv.h>
#include <hal/storage/hal_eeprom.h>
#include <string.h>

void example_kv(void) {
    // Najpierw inicjalizuje EEPROM, potem magazyn KV
    hal_eeprom_init(HAL_EEPROM_FLASH, 4096, 0);

    // Magazyn KV używa układu dual-bank zaczynającego się pod adresem 0, 2KB na bank
    hal_kv_init(0, 4096);

    // Zapisuje 32-bitową liczbę całkowitą bez znaku pod kluczem 1
    hal_kv_set_u32(1, 42);
    hal_deb("Stored: key=1, value=42");

    // Zapisuje kilka liczb całkowitych
    hal_kv_set_u32(2, 1000);
    hal_kv_set_u32(3, 999999);

    // Odczytuje wartość
    uint32_t retrieved = 0;
    if (hal_kv_get_u32(1, &retrieved)) {
        hal_deb("Retrieved: key=1, value=%lu", retrieved);
    }

    // Zapisuje dane binarne (blob) - np. adres MAC urządzenia lub konfigurację
    uint8_t mac_address[6] = {0xDE, 0xAD, 0xBE, 0xEF, 0x00, 0x01};
    hal_kv_set_blob(10, mac_address, sizeof(mac_address));

    // Odczytuje dane bloba
    uint8_t retrieved_mac[6];
    uint16_t retrieved_len = 0;
    if (hal_kv_get_blob(10, retrieved_mac, sizeof(retrieved_mac), &retrieved_len)) {
        hal_deb("Retrieved MAC: %02x:%02x:%02x:%02x:%02x:%02x",
                retrieved_mac[0], retrieved_mac[1], retrieved_mac[2],
                retrieved_mac[3], retrieved_mac[4], retrieved_mac[5]);
    }

    // Zapisuje łańcuch konfiguracyjny jako blob
    const char *config = "ssid=MyNetwork&pass=pwd123";
    hal_kv_set_blob(11, (const uint8_t *)config, strlen(config));

    // Odczytuje łańcuch konfiguracyjny
    char config_buf[128];
    uint16_t config_len = 0;
    if (hal_kv_get_blob(11, (uint8_t *)config_buf, sizeof(config_buf), &config_len)) {
        config_buf[config_len] = '\0';  // zakończenie znakiem null
        hal_deb("Retrieved config: %s", config_buf);
    }

    // Usuwa klucz
    hal_kv_delete(2);

    // Pobiera statystyki
    hal_kv_stats_t stats;
    if (hal_kv_get_stats(&stats)) {
        hal_deb("KV stats: %d keys, %d/%d bytes used, gen=%lu",
                stats.key_count, stats.used_bytes, stats.capacity_bytes,
                stats.generation);
    }

    // Ręczne zatwierdzenie (gdy auto-commit był wyłączony)
    hal_kv_set_auto_commit(false);
    hal_kv_set_u32(100, 111);
    hal_kv_set_u32(101, 222);
    hal_kv_commit();  // Opróżnia oba zapisy naraz
    hal_kv_set_auto_commit(true);
}
```

**Status API:** Warianty `_ex` sprawdzają argumenty i wykonują operacje EEPROM;
dotychczasowe funkcje `bool` pozostają wrapperami zgodności. Funkcja
`hal_kv_set_auto_commit()`, która wcześniej zwracała `void`, teraz zwraca bezpośrednio
`hal_status_t`. Brak klucza podczas odczytu odpowiada `HAL_ENOENT`, użycie przed poprawną
inicjalizacją - `HAL_EUNINIT`, nieprawidłowy zakres EEPROM - `HAL_EOVERFLOW`, a zbyt mała
pojemność banku - `HAL_ENOMEM`. Błędy zwracane przez EEPROM są przekazywane bez zmian.
`hal_kv_get_blob_ex()` zgłasza zbyt mały bufor wywołującego jako
`HAL_EOVERFLOW` z wymaganą długością w `*out_len`.

```c
uint8_t  buf[64];
uint16_t len = 0;
hal_status_t st = hal_kv_get_blob_ex(KEY_PROFILE, buf, sizeof(buf), &len);
switch (st) {
case HAL_OK:        use(buf, len);                       break;
case HAL_ENOENT:    /* brak klucza */                     break;
case HAL_EUNINIT:   /* magazyn niezainicjalizowany */          break;
case HAL_EOVERFLOW: /* buf za mały; *len = wymagane */    break;
default:            break;
}
```

---


## `hal_littlefs` - pomocnicy cyklu życia LittleFS  *(opt-in - `HAL_ENABLE_LITTLEFS`)*

Thread-safe, niezależne od targetu API do zarządzania cyklem życia LittleFS,
operacji na ścieżkach i odczytu rozmiaru systemu plików.

```c
#include <hal/storage/hal_littlefs.h>

hal_status_t hal_littlefs_set_progress_callback(
    hal_littlefs_progress_callback_t callback, void *ctx);
hal_status_t hal_littlefs_begin_ex(void);
hal_status_t hal_littlefs_end(void);
hal_status_t hal_littlefs_format_ex(void);
hal_status_t hal_littlefs_exists_ex(const char *path);
hal_status_t hal_littlefs_remove_ex(const char *path);
hal_status_t hal_littlefs_total_bytes_ex(size_t *out_bytes);
hal_status_t hal_littlefs_used_bytes_ex(size_t *out_bytes);

// Historyczne wrappery zgodności.
bool         hal_littlefs_begin(void);
bool         hal_littlefs_format(void);
bool         hal_littlefs_is_mounted(void);
bool         hal_littlefs_exists(const char *path);
bool         hal_littlefs_remove(const char *path);
size_t       hal_littlefs_total_bytes(void);
size_t       hal_littlefs_used_bytes(void);
```

**Uwagi dotyczące zachowania:**

- Moduł jest dostępny tylko, gdy zdefiniowano `HAL_ENABLE_LITTLEFS`.
- `hal_littlefs_begin_ex()` montuje system plików i jest idempotentne, gdy jest
  on już zamontowany.
- `hal_littlefs_end()` jest idempotentne przy odmontowanym systemie. Zawsze
  czyści zapisany stan montowania, także gdy backend zgłosi błąd odmontowania.
- Formatowanie jest destrukcyjne. Udane `hal_littlefs_format_ex()` pozostawia
  system plików odmontowany; zamontuj go jawnie przed użyciem API ścieżek lub
  rozmiaru.
- Jeśli odmontowanie albo formatowanie nie powiedzie się przy zamontowanym systemie plików, API
  podejmuje jedną próbę ponownego montowania i zwraca pierwotny błąd.
  `hal_littlefs_is_mounted()` informuje, czy ponowne zamontowanie się powiodło. Jeśli próba
  formatowania zawiedzie po rozpoczęciu modyfikowania flash, dane mogą być już
  częściowo zmienione i nie ma gwarancji ich zachowania.
- Pomocnicy ścieżek wymagają zamontowanego systemu plików i walidują niepuste ścieżki.
- Wyjście zapytania o rozmiar jest zerowane przed zwróceniem błędu.
- Publiczne API HAL obecnie udostępnia wyłącznie cykl życia, usuwanie/istnienie
  ścieżki oraz statystyki rozmiaru. Nie zapewnia przenośnych wrapperów
  otwierania/odczytu/zapisu plików.

`hal_littlefs.cpp` zawiera publiczne API, przechowuje stan montowania, sprawdza argumenty,
zarządza blokadą i wybiera backend dla każdego targetu, w tym mocka. Jedna wspólna
implementacja littlefs v2 obsługuje montowanie, odmontowywanie, formatowanie,
operacje na ścieżkach i statystyki systemu plików. Backendy sprzętowe dostarczają
jedynie geometrię oraz sprawdzone operacje odczytu, programowania, kasowania
i synchronizacji. Mock pozwala ustawiać ich wyniki w testach.

**Natywna implementacja RP:** używa upstreamowej wersji littlefs v2.11.3
umieszczonej w `third_party/littlefs/` oraz wewnętrznej
partycji flash kontrolowanej przez `HAL_RP_FLASH_LITTLEFS_SIZE`. Natywna
konfiguracja CMake rezerwuje 64 KiB, gdy `HAL_ENABLE_LITTLEFS` jest włączone
bez jawnego rozmiaru. Niezerowa rezerwacja musi zawierać co najmniej dwa
sektory kasowania po 4096 bajtów. Partycja znajduje się bezpośrednio przed
ostatnim 4 KiB sektorem EEPROM. Każda 256-bajtowa operacja programowania i
4096-bajtowa operacja kasowania przechodzi przez natywny koordynator
transakcji flash RP; odczyty używają mapowania XIP. Linker uniemożliwia
obrazowi firmware'u nakładanie się na którąkolwiek z partycji.

**Implementacja STM32G474:** używa tego samego utrzymywanego w repozytorium
checkoutu littlefs z `third_party/littlefs/` oraz wewnętrznej rezerwacji flash STM32
udostępnianej przez skrypt linkera. `HAL_STM32_FLASH_LITTLEFS_SIZE`
kontroluje rozmiar rezerwacji i musi być wielokrotnością
`HAL_STM32_FLASH_PAGE_SIZE` (2048 bajtów). Rozmiar może wynosić zero, gdy
backend jest skompilowany, ale nieużywany; niezerowa rezerwacja musi zawierać
co najmniej dwie strony. Montowanie pustej partycji zawodzi bezpiecznie.
Pomocnicy CMake dla STM32 automatycznie rezerwują 64 KB, gdy
`HAL_ENABLE_LITTLEFS` jest przekazane przez ich listy definicji i nie podano
jawnego rozmiaru.

Rozmiar bloku kasowania LittleFS to jedna strona flash STM32; granularność
programowania to jedno podwójne słowo STM32 (doubleword, 8 bajtów). Operacje
modyfikujące flash EEPROM/KV i LittleFS współdzielą jeden mutex flash STM32, więc ich
sekwencje erase/program nie mogą się nakładać.
Po zamontowaniu `hal_littlefs_total_bytes_ex()` zwraca zarezerwowany rozmiar partycji, a
`hal_littlefs_used_bytes_ex()` - liczbę przydzielonych bloków
LittleFS pomnożoną przez rozmiar bloku kasowania targetu.

LittleFS nigdy nie odświeża watchdoga automatycznie. Użyj
`hal_littlefs_set_progress_callback()` przed długimi operacjami, takimi jak
formatowanie lub duże serie operacji odśmiecania (garbage collection, GC)
i zapisu, jeśli aplikacja chce odświeżać własny watchdog lub raportować postęp.
Skonfiguruj callback przed rozpoczęciem współbieżnego dostępu. Jest on wywoływany
pod wspólnym mutexem i
nie może wywoływać żadnego API `hal_littlefs_*`, w tym settera callbacku ani
`hal_littlefs_is_mounted()`. Na targetach sprzętowych platformowa koordynacja
flash jest już zwolniona, gdy callback jest wykonywany. Liczba wywołań dla
pojedynczej operacji zależy od wybranego backendu. Callback może zostać wywołany podczas
operacji, która później zgłosi błąd; o powodzeniu informuje status zwrotny
operacji.

**Przykład: montowanie z jawnym opt-inem destrukcyjnego formatowania**

Przekaż `true` wyłącznie wtedy, gdy wymazanie zarezerwowanej partycji jest
dopuszczalne. Sam błąd montowania nie rozróżnia pustego nośnika od uszkodzenia lub
przejściowego błędu I/O.

```c
#include <hal/storage/hal_littlefs.h>
#include <tools_c.h>

static hal_status_t mount_littlefs(bool allow_destructive_format) {
    hal_status_t status = hal_littlefs_begin_ex();
    if (status == HAL_OK || !allow_destructive_format) {
        return status;
    }

    status = hal_littlefs_format_ex();
    if (status != HAL_OK) {
        return status;
    }
    return hal_littlefs_begin_ex();
}

void example_littlefs(bool allow_destructive_format) {
    hal_status_t status = mount_littlefs(allow_destructive_format);
    if (status != HAL_OK) {
        derr("LittleFS unavailable: %s", hal_status_to_string(status));
        return;
    }

    size_t total = 0;
    size_t used = 0;
    status = hal_littlefs_total_bytes_ex(&total);
    if (status == HAL_OK) {
        status = hal_littlefs_used_bytes_ex(&used);
    }
    if (status == HAL_OK) {
        deb("LittleFS mounted: %lu/%lu bytes used",
            (unsigned long)used, (unsigned long)total);
    }

    if (hal_littlefs_exists_ex("/data.txt") == HAL_OK) {
        (void)hal_littlefs_remove_ex("/data.txt");
    }

    (void)hal_littlefs_end();
}
```

**Backend mock:** Deterministyczny backend pozwala ustawić wyniki montowania,
odmontowywania i formatowania,
obecność ścieżki oraz statystyki rozmiaru wolumenu. Reset czyści zarówno stan backendu,
jak i zapisany stan montowania wspólnego API.

**Thread safety:** Na wszystkich targetach publiczne wywołania serializuje ten sam mutex
singletona. Mock służy do
deterministycznych testów, a nie do symulacji współbieżności sprzętowej.

**Pomocnicy mock:**

```c
void hal_mock_littlefs_reset(void);
void hal_mock_littlefs_set_begin_result(bool result);
void hal_mock_littlefs_set_begin_status(hal_status_t status);
void hal_mock_littlefs_set_end_result(bool result);
void hal_mock_littlefs_set_format_result(bool result);
void hal_mock_littlefs_set_total_bytes(size_t total_bytes);
void hal_mock_littlefs_set_used_bytes(size_t used_bytes);
void hal_mock_littlefs_set_exists(const char *path, bool exists);
```

**Status API:** Warianty `_ex` cyklu życia, operacji na ścieżkach i odczytu rozmiaru
sprawdzają argumenty oraz wykonują operacje backendu. Dotychczasowe funkcje zwracające
`bool` lub wartość pozostają wrapperami zgodności. Setter callbacku i funkcja odmontowania,
które wcześniej zwracały `void`, teraz zwracają bezpośrednio `hal_status_t`. Zwykłe
zapytanie `hal_littlefs_is_mounted()` nie ma wariantu `_ex`. Nieprawidłowa ścieżka albo
wskaźnik wyjściowy powoduje zwrócenie `HAL_EINVAL`; operacja wymagająca zamontowanego
systemu - `HAL_EUNINIT`; brak ścieżki - `HAL_ENOENT`; brak backendu lub nieprawidłowa albo
pusta geometria partycji - `HAL_ECONFIG`; błąd utworzenia mutexu - `HAL_ENOMEM`;
przepełnienie rozmiaru - `HAL_EOVERFLOW`; a błędy littlefs lub bezpośredniej obsługi
nośnika - `HAL_EIO`.

```c
hal_status_t st = hal_littlefs_exists_ex("/config.json");
// HAL_OK -> obecny, HAL_ENOENT -> nieobecny,
// HAL_EUNINIT -> niezamontowany, HAL_EINVAL -> ścieżka NULL/pusta

size_t used = 0;
hal_littlefs_used_bytes_ex(&used);   // HAL_EUNINIT (used=0) podczas odmontowania
```

---

## `hal_sdlogger` - logger karty SD  *(opt-in - `HAL_ENABLE_SDLOGGER`)*

Okresowy logger karty SD wraz z loggerem raportów awarii (crash). Moduł
przechowuje liczniki plików log/crash w `hal_eeprom` i zapisuje pliki
poprzez wspólną warstwę FatFs SD-over-SPI, dlatego jego włączenie automatycznie włącza
`HAL_ENABLE_FAT`, `HAL_ENABLE_EEPROM` i `HAL_ENABLE_SPI`.

```c
#include <hal/storage/hal_sdlogger.h>

int  hal_sdlogger_get_log_number(void);
int  hal_sdlogger_get_crash_number(void);
hal_status_t hal_sdlogger_init_ex(int cs);
bool hal_sdlogger_init(int cs);
hal_status_t hal_sdlogger_crash_init_ex(const char *add_to_name, int cs);
bool hal_sdlogger_crash_init(const char *add_to_name, int cs);
bool hal_sdlogger_is_initialized(void);
bool hal_sdlogger_crash_is_initialized(void);
hal_status_t hal_sdlogger_append(const char *data);
hal_status_t hal_sdlogger_crash_append(const char *data);
hal_status_t hal_sdlogger_close(void);
hal_status_t hal_sdlogger_crash_close(void);
hal_status_t hal_sdlogger_crash_report(const char *format, ...);
```

**Wartości domyślne konfiguracji:**

```c
HAL_SDLOGGER_WRITE_INTERVAL_MS  2000u
HAL_SDLOGGER_EEPROM_LOGGER_ADDR 0u
HAL_SDLOGGER_EEPROM_CRASH_ADDR  4u
HAL_SDLOGGER_EEPROM_FIRST_ADDR  8u
HAL_SDLOGGER_LOG_BUFFER_SIZE    2048u
HAL_SDLOGGER_NAME_BUFFER_SIZE   128u
HAL_SDLOGGER_SPI_BUS            0u
```

**Uwagi dotyczące zachowania:**
- Aplikacja musi zainicjalizować piny wybranej magistrali SPI przez
  `hal_spi_init()` przed wywołaniem `hal_sdlogger_init()` lub
  `hal_sdlogger_crash_init()`.
- `hal_sdlogger_init(cs)` otwiera `logNNNNN.txt` i zwiększa licznik logów w EEPROM.
  `hal_sdlogger_init_ex(cs)` jest wariantem zwracającym status, a dawna funkcja `bool`
  pozostaje wrapperem zgodności.
- `hal_sdlogger_append()` buforuje linie i opróżnia bufor co
  `HAL_SDLOGGER_WRITE_INTERVAL_MS`; `hal_sdlogger_close()` zapisuje pozostałe dane.
  Te funkcje zwracają teraz `hal_status_t`, więc dotychczasowy kod nadal
  mogą ignorować wynik, a nowy kod może sprawdzać niepowodzenia.
- `hal_sdlogger_crash_init(add_to_name, cs)` otwiera `wdNNNNNN.txt` i
  zapisuje w nim opcjonalny tag awarii wraz z odpowiadającą nazwą pliku
  logu. Generowane nazwy plików celowo pozostają w formie 8.3 FatFs,
  ponieważ LFN jest wyłączone.
- Liczniki loggera SD są zwiększane dopiero po zamontowaniu karty SD i
  pomyślnym otwarciu pliku docelowego.
- `hal_sdlogger_crash_append()` i `hal_sdlogger_crash_report()` opróżniają
  wpisy awaryjne natychmiast.
- Mapowanie statusu: niepowodzenie montowania SD zwraca `HAL_EBUS`; błędy
  zapisu i opróżniania plików, zamknięcia oraz aktualizacji EEPROM
  zwracają status backendu lub `HAL_EIO`; `append`/`close` przed `init` zwracają
  `HAL_EUNINIT`; zbyt duża zbuforowana linia logu zwraca `HAL_EOVERFLOW`;
  `hal_sdlogger_crash_report(NULL)` zwraca `HAL_EINVAL`.

Przykład do zbudowania: `examples/10_storage`.

**Przykład: okresowe logowanie na kartę SD**
```c
#include <hal/storage/hal_sdlogger.h>
#include <hal/storage/hal_eeprom.h>
#include <hal/spi/hal_spi.h>

void setup_sd_logging(void) {
    // Inicjalizuje EEPROM (logger SD przechowuje w nim liczniki)
    hal_eeprom_init(HAL_EEPROM_FLASH, 512, 0);

    // Inicjalizuje magistralę SPI 0 i logger karty SD z pinem CS 17
    hal_spi_init(0, 16, 19, 18);
    int cs_pin = 17;
    if (hal_sdlogger_init(cs_pin)) {
        hal_deb("SD logger initialized, log number: %d", hal_sdlogger_get_log_number());
    } else {
        hal_derr("SD logger init failed!");
        return;
    }
}

void loop_with_logging(void) {
    static uint32_t last_log_ms = 0;
    uint32_t now_ms = hal_millis();

    // Loguj co 2 sekundy (HAL_SDLOGGER_WRITE_INTERVAL_MS)
    if (now_ms - last_log_ms > 2000) {
        last_log_ms = now_ms;

        // Odczytuje przykładowe dane z czujnika
        float temperature = read_temperature();
        int humidity = read_humidity();

        // Dopisuje do pliku logu (buforowane, opróżniane okresowo)
        static char log_line[128];
        snprintf(log_line, sizeof(log_line), "[%lu] T=%.1f°C, H=%d%%\n",
                 now_ms, temperature, humidity);
        hal_sdlogger_append(log_line);
    }
}

void shutdown_logging(void) {
    // Opróżnia wszelkie pozostałe zbuforowane dane na kartę SD
    hal_sdlogger_close();
    hal_deb("Log file closed");
}
```

**Przykład: logger awarii (crash) na karcie SD**
```c
#include <hal/storage/hal_sdlogger.h>
#include <hal/storage/hal_eeprom.h>
#include <hal/spi/hal_spi.h>

void setup_crash_logging(void) {
    // Inicjalizuje EEPROM i logger awarii
    hal_eeprom_init(HAL_EEPROM_FLASH, 512, 0);

    hal_spi_init(0, 16, 19, 18);
    int cs_pin = 17;
    // Tworzy wdNNNNNN.txt i zapisuje w nim "boot" jako tag awarii.
    if (hal_sdlogger_crash_init("boot", cs_pin)) {
        hal_deb("Crash logger initialized, crash log number: %d",
                hal_sdlogger_get_crash_number());
    }
}

void on_critical_error(const char *error_msg) {
    // Loguje raport awaryjny natychmiast (bez buforowania)
    hal_sdlogger_crash_report("[CRITICAL] Error: %s, Free heap: %lu\n",
                              error_msg, hal_get_free_heap());

    // Dopisuje dodatkowe informacje debugowania
    hal_sdlogger_crash_append("[CRITICAL] Stack trace would go here\n");

    // Opróżnia i zamyka plik awarii
    hal_sdlogger_crash_close();
}

void watchdog_reboot_handler(void) {
    hal_sdlogger_crash_report("[WATCHDOG] System reboot triggered\n");
    hal_sdlogger_crash_close();
}
```

---

- **hal/storage/filesystem:** Funkcje pomocnicze do obsługi plików na karcie SD
  oraz przenośna implementacja loggera używana na RP2040 i STM32G474.
  Niezmieniony rdzeń FatFs R0.16 pochodzi z kopii repozytorium utrzymywanej w
  projekcie jako `jaszczurtd/ff16`; w `third_party/FatFs` znajduje się dokładnie
  określony commit tego kodu.
  Utrzymywane w projekcie wrappery udostępniają potrzebny zestaw funkcji i plik
  `ffconf.h`.
- **impl/.mock:** Deterministyczna implementacja testowa z ustawianymi wynikami
  inicjalizacji SD i otwierania plików. Przechwytuje nazwy plików i treść oraz udostępnia
  liczniki wywołań `flush` i flagi zamknięcia.

**Thread safety:** Wspólna implementacja serializuje publiczne wywołania przez
singletonowy `hal_mutex_t`. Operacje init/close nadal należy wykonywać jako część cyklu
życia zarządzanego z jednego rdzenia.

**Pomocnicy mock:**
```c
void        hal_mock_sdlogger_reset(void);
void        hal_mock_sdlogger_set_sd_begin_result(bool result);
void        hal_mock_sdlogger_set_log_open_result(bool result);
void        hal_mock_sdlogger_set_crash_open_result(bool result);
const char *hal_mock_sdlogger_log_filename(void);
const char *hal_mock_sdlogger_crash_filename(void);
const char *hal_mock_sdlogger_log_content(void);
const char *hal_mock_sdlogger_crash_content(void);
uint32_t    hal_mock_sdlogger_log_flush_count(void);
uint32_t    hal_mock_sdlogger_crash_flush_count(void);
uint32_t    hal_mock_sdlogger_sd_begin_count(void);
bool        hal_mock_sdlogger_log_was_closed(void);
bool        hal_mock_sdlogger_crash_was_closed(void);
```

---


---

*Dalej: [Łączność sieciowa](15_connectivity.md)*
