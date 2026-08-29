# Przechowywanie danych

*Dostępne również [po angielsku](../en/14_storage.md).*

> **Część [Dokumentacji API JaszczurHAL](../../pl/JaszczurHAL_API.md)**

Obejmuje: `hal_eeprom`, `hal_kv`, `hal_littlefs`, `hal_sdlogger`.

Układy pamięci flash wewnętrznej rezerwują regiony aplikacji, OTA, LittleFS
i EEPROM w czasie konsolidacji (link time). Operacje kasowania/programowania
RP współdzielą koordynator transakcji flash, który zabezpiecza drugi rdzeń,
wstrzymuje pracę USB, odrzuca aktywne konflikty DMA, maskuje lokalne
przerwania i przywraca stan runtime na wyjściu. STM32G474
używa rezerwacji linkera wyrównanych do stron oraz swojej docelowej usługi
flash. Zobacz [mapę pamięci RP](../../../rp_native_lib/MEMORY_MAP.md) oraz
[mapę pamięci STM32G474](../../../stm32_lib/MEMORY_MAP.md).

## `hal_eeprom` - ujednolicony EEPROM  *(opcjonalny - `HAL_ENABLE_EEPROM`)*

Jednolite API dla trwałego, adresowanego bajtowo przechowywania danych.
Backend jest wybierany w runtime przez `hal_eeprom_init()`.

Publiczne API, przycinanie zakresu (range clipping), kodowanie liczb
całkowitych, blokady, własność callbacków i wybór providera znajdują się w
jednej fasadzie niezależnej od targetu. Jeden przenośny provider AT24C256
korzysta z HAL I2C; providerzy flash RP, flash STM32G474 i pamięci hosta
zawierają wyłącznie swoje mechanizmy przechowywania.

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
// HAL_EEPROM_FLASH / natywny flash: zapisuje lustro RAM do flash.
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

**Semantyka zatwierdzania (commit):** Dla backendów opartych na flash,
`hal_eeprom_write_byte`, `hal_eeprom_write_int` i `hal_eeprom_write_bytes`
najpierw aktualizują bufor RAM. Wywołaj `hal_eeprom_commit()` raz po grupie
zapisów, aby utrwalić je we flash. Dla `HAL_EEPROM_AT24C256` zapisy są
zatwierdzane synchronicznie na układzie w porcjach wielkości strony;
`hal_eeprom_commit()` jest wtedy no-opem.

**Natywna implementacja RP:** `HAL_EEPROM_FLASH` używa ostatnich
`HAL_RP_FLASH_EEPROM_SIZE` bajtów fizycznej pamięci flash (domyślnie 4096
bajtów). Zapisy aktualizują lustro RAM. Zatwierdzenie "brudnego" stanu
wykonuje pełne kasowanie i programowanie partycji w jednej operacji
`jh_rp_flash_transaction_execute()`, więc rdzeń 1, przerwania, DMA i
TinyUSB podlegają tej samej polityce bezpieczeństwa co każda inna natywna
mutacja flash. Wygenerowany region linkera wyklucza rezerwację z firmware'u.

**Implementacja STM32G474:** `HAL_EEPROM_FLASH` i `HAL_EEPROM_STM32_FLASH`
używają ostatnich stron wewnętrznej pamięci flash zarezerwowanych przez
skrypt linkera STM32. Domyślna rezerwacja to `HAL_STM32_FLASH_EEPROM_SIZE =
4096` bajtów, przy `HAL_STM32_FLASH_PAGE_SIZE = 2048` bajtów. Zmniejsza to
pamięć flash dostępną dla kodu aplikacji o 4 KB. Jeśli rozmiar rezerwacji
zostanie zmieniony, utrzymuj synchronizację definicji buildu i symbolu
linkera oraz użyj wielokrotności rozmiaru strony flash STM32.

Linker STM32 obsługuje też osobną rezerwację LittleFS przed EEPROM. Utrzymuj
`HAL_STM32_FLASH_EEPROM_SIZE` i `HAL_STM32_FLASH_LITTLEFS_SIZE`
nienakładające się; EEPROM/KV i LittleFS nie współdzielą stron.

**Implementacja AT24C256:** jeden provider niezależny od targetu steruje
zewnętrznym układem poprzez prymitywy `hal_i2c_*` na obu targetach
sprzętowych. Zapisy są dzielone na granicach stron po 64 bajty i odpytywane
o ACK z ograniczonym czasowo timeoutem (`HAL_AT24C256_WRITE_TIMEOUT_US`,
domyślnie 20000 us). Zapisy poza zakresem są przycinane; odczyty poza
zakresem zwracają bajty wypełnione zerami. Adres I2C AT24C256 to
`EEPROM_I2C_ADDRESS` (domyślnie `0x50`, zdefiniowane w `hal_config.h`), o
ile do `hal_eeprom_init()` nie przekazano jawnego adresu.

**Postęp długich operacji:** EEPROM nigdy niejawnie nie karmi watchdoga.
Użyj `hal_eeprom_set_progress_callback()` przed długimi zapisami, resetem
lub operacjami zatwierdzania flash, jeśli aplikacja chce nakarmić własny
watchdog lub raportować postęp. Pełny reset AT24C256 dotyka 512 stron i
może zająć kilka sekund.

**impl/.mock:** ta sama publiczna fasada rozdziela wywołania do providera
w pamięci (`MOCK_EEPROM_BUF_SIZE`, domyślnie 32768); mock nie powiela
zachowania `hal_eeprom_*`.

**Thread safety:** Thread-safe i wielordzeniowo dla obu
rodzin backendów. Wspólny mutex fasady chroni wybór providera, aktywny
rozmiar, callbacki, przycinanie zakresu i każdą operację.
`HAL_EEPROM_AT24C256` transfery używają dodatkowo mutexu magistrali
`hal_i2c`, podczas gdy natywni providerzy flash zachowują własną
platformową koordynację flash. Skonfiguruj raportowanie postępu przed
dostępem współbieżnym; callback działa pod mutexem fasady i nie może
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
`hal_eeprom` jest **modułem referencyjnym** dla zmienionej migracji statusu.
Historycznie `void` punkty wejścia (`init`, `write_byte`, `write_int`,
`write_bytes`, `read_bytes`, `commit`, `reset`, `set_progress_callback`)
**teraz zwracają `hal_status_t` bezpośrednio** - jest to zgodne źródłowo,
więc istniejący wywołujący, którzy ignorują wartość zwracaną, pozostają
niezmienieni, a nowy kod może ją sprawdzać. Na backendzie AT24C256
przywraca to rzeczywiste błędy I2C (`HAL_EIO`), które stare API `void`
odrzucało. Trzy gettery zwracające wartość zachowują swoją sygnaturę i
zyskują towarzyszące warianty `_ex` (`hal_eeprom_read_byte_ex`,
`hal_eeprom_read_int_ex`, `hal_eeprom_size_ex`), które raportują wartość
przez parametr wyjściowy. Dostęp poza zakresem nadal przycina dokładnie
jak wcześniej; nowy status po prostu zgłasza to jako `HAL_EOVERFLOW`.

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

Thread-safe, dopisywane wyłącznie (append-only) przechowywanie
rekordów KV na bazie `hal_eeprom`. Używa układu dwóch banków (dual-bank) z
nagłówkami i rekordami chronionymi CRC16. Automatyczne odśmiecanie (garbage
collection, GC) kompaktuje żywe rekordy do banku alternatywnego, gdy
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

**Zależności:** `hal_eeprom`, `hal_sync`, `hal_serial`.
**Thread safety:** Thread-safe i wielordzeniowo. Wewnętrzny
mutex singletonowy, utworzony przy pomocy atomowego pomocnika HAL
create-once, chroni wszystkie operacje. `hal_kv_init()` musi być wywołane
po `hal_eeprom_init()`.

**Deduplikacja:** `hal_kv_set_u32` / `hal_kv_set_blob` pomijają zapis do
EEPROM, gdy wartość jest niezmieniona, unikając niepotrzebnego zużycia
(wear) pamięci flash.

**Polityka zatwierdzania (commit):** auto-commit jest domyślnie włączony
(zachowanie historyczne). Użyj `hal_kv_set_auto_commit(false)`, aby odłożyć
fizyczne zatwierdzenie EEPROM/flash i połączyć wiele zapisów, a następnie
opróżnij je jednorazowo przez `hal_kv_commit()`.

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

**Status API:** operacje `_ex` są właścicielami walidacji i wejścia/wyjścia
EEPROM; powyższe historyczne funkcje bool to cienkie wrappery zgodności.
Historycznie `void` funkcja `hal_kv_set_auto_commit()` teraz zwraca
bezpośrednio `hal_status_t`. Brak trafienia przy odczycie mapuje się na
`HAL_ENOENT`, użycie przed udaną inicjalizacją na `HAL_EUNINIT`, nieprawidłowy
zakres EEPROM na `HAL_EOVERFLOW`, niewystarczająca pojemność banku na
`HAL_ENOMEM`, a leżące u podstaw błędy EEPROM są propagowane.
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

Thread-safe wrapper montowania/formatowania LittleFS oraz lekcy
pomocnicy ścieżek.

```c
#include <hal/storage/hal_littlefs.h>

hal_status_t hal_littlefs_set_progress_callback(
    hal_littlefs_progress_callback_t callback, void *ctx);
bool         hal_littlefs_begin(void);
hal_status_t hal_littlefs_end(void);
bool         hal_littlefs_format(void);
bool         hal_littlefs_is_mounted(void);
bool         hal_littlefs_exists(const char *path);
bool         hal_littlefs_remove(const char *path);
size_t       hal_littlefs_total_bytes(void);
size_t       hal_littlefs_used_bytes(void);
```

**Uwagi dotyczące zachowania:**
- Moduł jest dostępny tylko, gdy zdefiniowano `HAL_ENABLE_LITTLEFS`.
- `hal_littlefs_begin()` montuje system plików; `hal_littlefs_end()` go odmontowuje.
- `hal_littlefs_format()` formatuje partycję LittleFS.
- Gdy `hal_littlefs_format()` zawiedzie, stan montowania/ścieżki/stat pozostaje niezmieniony.
- Pomocnicy ścieżek wymagają zamontowanego systemu plików i walidują niepuste ścieżki.
- Publiczne API HAL obecnie udostępnia wyłącznie cykl życia, usuwanie/istnienie
  ścieżki oraz statystyki rozmiaru. Nie zapewnia przenośnych wrapperów
  otwierania/odczytu/zapisu plików.

**Natywna implementacja RP:** używa przypiętego komponentu upstream
checkoutu littlefs v2.11.3 pod `third_party/littlefs/` oraz wewnętrznej
partycji flash kontrolowanej przez `HAL_RP_FLASH_LITTLEFS_SIZE`. Natywna
receptura CMake rezerwuje 64 KiB, gdy `HAL_ENABLE_LITTLEFS` jest włączone
bez jawnego rozmiaru. Partycja znajduje się bezpośrednio przed ostatnim
4 KiB sektorem EEPROM. Każdy 256-bajtowy program i 4096-bajtowy callback
kasowania przechodzi przez natywny koordynator transakcji flash RP; odczyty
używają mapowania XIP. Linker uniemożliwia obrazowi firmware'u nakładanie
się na którąkolwiek z partycji.

**Implementacja STM32G474:** używa tego samego zarządzanego checkoutu
littlefs pod `third_party/littlefs/` oraz wewnętrznej rezerwacji flash STM32
udostępnianej przez skrypt linkera. `HAL_STM32_FLASH_LITTLEFS_SIZE`
kontroluje rozmiar rezerwacji i musi być wielokrotnością
`HAL_STM32_FLASH_PAGE_SIZE` (2048 bajtów). Rozmiar może wynosić zero, gdy
backend jest skompilowany, ale nieużywany; montowanie wtedy zawodzi
bezpiecznie. Pomocnicy CMake dla STM32 automatycznie rezerwują 64 KB, gdy
`HAL_ENABLE_LITTLEFS` jest przekazane przez ich listy definicji i nie podano
jawnego rozmiaru.

Rozmiar bloku kasowania LittleFS to jedna strona flash STM32; granularność
programowania to jedno podwójne słowo (doubleword, 8 bajtów) STM32.
`hal_littlefs_total_bytes()` raportuje zarezerwowany rozmiar partycji po
zamontowaniu. `hal_littlefs_used_bytes()` raportuje przydzielone bloki
littlefs pomnożone przez rozmiar strony flash.

LittleFS nigdy niejawnie nie karmi watchdoga. Użyj
`hal_littlefs_set_progress_callback()` przed długimi operacjami, takimi jak
formatowanie lub duże serie odśmiecania (garbage-collection)/zapisu, jeśli
aplikacja chce nakarmić własny watchdog lub raportować postęp.

**Przykład: montowanie, formatowanie przy pierwszym użyciu, inspekcja i usunięcie ścieżki**
```c
#include <hal/storage/hal_littlefs.h>
#include <tools_c.h>

void example_littlefs(void) {
    if (!hal_littlefs_begin()) {
        derr("LittleFS mount failed; formatting");
        if (!hal_littlefs_format() || !hal_littlefs_begin()) {
            derr("LittleFS unavailable");
            return;
        }
    }

    deb("LittleFS mounted: %lu/%lu bytes used",
        (unsigned long)hal_littlefs_used_bytes(),
        (unsigned long)hal_littlefs_total_bytes());

    if (hal_littlefs_exists("/data.txt")) {
        (void)hal_littlefs_remove("/data.txt");
    }

    hal_littlefs_end();
}
```

---
**impl/.mock:** deterministyczny test double z wstrzykiwalnym wynikiem
montowania/formatowania, obecnością ścieżki i statystykami rozmiaru
wolumenu.
**Thread safety:** Backendy RP2040 i STM32G474 są thread-safe dla publicznego
API. Singletonowy `hal_mutex_t` serializuje
wszystkie wywołania wrappera.

**Pomocnicy mock:**
```c
void hal_mock_littlefs_reset(void);
void hal_mock_littlefs_set_begin_result(bool result);
void hal_mock_littlefs_set_format_result(bool result);
void hal_mock_littlefs_set_total_bytes(size_t total_bytes);
void hal_mock_littlefs_set_used_bytes(size_t used_bytes);
void hal_mock_littlefs_set_exists(const char *path, bool exists);
```

**Status API:** operacje `_ex` dla cyklu życia, ścieżek i rozmiaru są
właścicielami walidacji i wejścia/wyjścia backendu; historyczne funkcje
bool/wartościowe to cienkie wrappery zgodności. Historycznie `void` setter
callbacku i funkcja odmontowania teraz zwracają bezpośrednio
`hal_status_t`. Zwykłe zapytanie o stan `hal_littlefs_is_mounted()` nie ma
formy `_ex`. Nieprawidłowa ścieżka/wyjście mapuje się na `HAL_EINVAL`,
użycie podczas odmontowania na `HAL_EUNINIT`, brakująca ścieżka na
`HAL_ENOENT`, nieskonfigurowana partycja STM32 na `HAL_ECONFIG`, a natywne
błędy montowania/formatowania/stat/odmontowania na `HAL_EIO`.

```c
if (hal_littlefs_begin_ex() != HAL_OK) {
    return;                 // HAL_EIO: montowanie nieudane
}
hal_status_t st = hal_littlefs_exists_ex("/config.json");
// HAL_OK -> obecny, HAL_ENOENT -> nieobecny,
// HAL_EUNINIT -> niezamontowany, HAL_EINVAL -> ścieżka NULL/pusta

size_t used = 0;
hal_littlefs_used_bytes_ex(&used);   // HAL_EUNINIT (used=0) podczas odmontowania
```

---

## `hal_sdlogger` - logger karty SD  *(opt-in - `HAL_ENABLE_SDLOGGER`)*

Okresowy logger karty SD wraz z loggerem raportów awaryjnych (crash). Moduł
przechowuje liczniki plików log/crash w `hal_eeprom` i zapisuje pliki
poprzez wspólną warstwę FatFs SD-over-SPI, więc jego włączenie dołącza
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
- `hal_sdlogger_init(cs)` otwiera `logNNNNN.txt` i zwiększa licznik logów w
  EEPROM; `hal_sdlogger_init_ex(cs)` to forma zwracająca status, a
  starsza funkcja `bool` jest cienkim wrapperem.
- `hal_sdlogger_append()` buforuje linie i opróżnia bufor co
  `HAL_SDLOGGER_WRITE_INTERVAL_MS`; `hal_sdlogger_close()` opróżnia resztki.
  Te funkcje teraz zwracają `hal_status_t`, więc starsi wywołujący nadal
  mogą ignorować wynik, a nowy kod może sprawdzać niepowodzenia.
- `hal_sdlogger_crash_init(add_to_name, cs)` otwiera `wdNNNNNN.txt` i
  zapisuje w nim opcjonalny tag awarii wraz z odpowiadającą nazwą pliku
  logu. Generowane nazwy plików celowo pozostają w formie 8.3 FatFs,
  ponieważ LFN jest wyłączone.
- Liczniki loggera SD są zwiększane dopiero po zamontowaniu karty SD i
  pomyślnym otwarciu pliku docelowego.
- `hal_sdlogger_crash_append()` i `hal_sdlogger_crash_report()` opróżniają
  wpisy awaryjne natychmiast.
- Mapowanie statusu: niepowodzenie montowania SD zwraca `HAL_EBUS`; zapisy
  plików, opróżnienia, zamknięcia i niepowodzenia aktualizacji EEPROM
  zwracają status backendu lub `HAL_EIO`; append/close przed init zwracają
  `HAL_EUNINIT`; zbyt duża zbuforowana linia logu zwraca `HAL_EOVERFLOW`;
  `hal_sdlogger_crash_report(NULL)` zwraca `HAL_EINVAL`.

Możliwy do zbudowania przykład: `examples/10_storage`.

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
**hal/storage/filesystem:** pomocnicy plików SD oraz przenośna implementacja
loggera SD używana przez RP2040 i STM32G474. Niezmieniony rdzeń FatFs R0.16
jest ładowany z checkoutu dokładnego commitu projektowego mirrora
`jaszczurtd/ff16` w `third_party/FatFs`; śledzone wrappery udostępniają
bramkę funkcji oraz projektowy plik `ffconf.h`.
**impl/.mock:** deterministyczny test double z wstrzykiwalnymi wynikami
SD/otwarcia, przechwyconymi nazwami plików/treścią, licznikami opróżnień
i flagami zamknięcia.
**Thread safety:** wspólny backend serializuje publiczne wywołania
singletonowym `hal_mutex_t`; init/close nadal powinny być traktowane jako
praca cyklu życia jednordzeniowa.

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
