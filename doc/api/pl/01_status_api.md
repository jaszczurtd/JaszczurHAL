# API statusów (`hal_status_t`)

*Dostępne również [po angielsku](../en/01_status_api.md).*

> **Część [Dokumentacji API JaszczurHAL](../../pl/JaszczurHAL_API.md)**

Opisuje `hal_status_t`, funkcje pomocnicze do obsługi statusów z
[`hal_status.h`](../../../src/hal/core/hal_status.h), migrację istniejących funkcji
do wariantów zwracających status oraz funkcje `_ex` używane w modułach, które
przeszły taką migrację.

## Dlaczego istnieje

Starsze API HAL przekazuje wynik jako `bool` (sukces/porażka), licznik typu
`int`/`size_t`, uchwyt (`NULL` w razie błędu) albo zwykłe `void`. Takie wartości
informują, *że wystąpił* błąd, ale nie podają jego przyczyny. `hal_status_t`
wprowadza jeden, wspólny typ wyniku. Kod wywołujący może dzięki temu rozróżnić
nieprawidłowy argument, niezainicjalizowany backend, błąd magistrali, brak
obiektu czy przepełnienie, bez tworzenia osobnej konwencji błędów dla każdego
modułu.

Obecne podejście do tego problemu to **funkcja zwracająca status**. To ona sprawdza
argumenty, wywołuje backend i odwzorowuje błędy.

## Kody statusów

Wartości `hal_status_t` są dodatnie po powodzeniu i ujemne po wystąpieniu błędu.
Powodzenie można więc sprawdzić przez `status == HAL_OK`, a dowolny błąd przez
`status < 0` lub `hal_status_is_error()`.

| Kod | Znaczenie |
|---|---|
| `HAL_OK` | Operacja zakończona pomyślnie. |
| `HAL_NONE` | Brak statusu / stan niezainicjalizowany / stan nieprawidłowy (wartość `0`). |
| `HAL_EINVAL` | Nieprawidłowy argument lub nieobsługiwana wartość parametru. |
| `HAL_EBUSY` | Zasób lub magistrala są zajęte. |
| `HAL_ETIMEOUT` | Timeout operacji. |
| `HAL_EIO` | Ogólny błąd I/O urządzenia, magistrali lub backendu. |
| `HAL_EUNSUPPORTED` | Operacja nieobsługiwana przez ten target/backend. |
| `HAL_ENOENT` | Żądany obiekt, urządzenie lub wpis nie został znaleziony. |
| `HAL_EAGAIN` | Spróbuj ponownie później / operacja nieblokująca zablokowałaby wywołanie. |
| `HAL_EOVERFLOW` | Operacja przepełniłaby bufor lub zasób. |
| `HAL_ENOMEM` | Brak pamięci lub slotów zasobów. |
| `HAL_IGNORED` | Operacja została zignorowana (niekrytyczna). |
| `HAL_EEXIST` | Obiekt już istnieje. |
| `HAL_EPERM` | Operacja niedozwolona. |
| `HAL_EINTERNAL` | Błąd wewnętrzny / nieoczekiwany stan. |
| `HAL_ECANCELED` | Operacja została anulowana. |
| `HAL_EPROTO` | Błąd protokołu (nieoczekiwana odpowiedź). |
| `HAL_EAUTH` | Błąd uwierzytelniania/autoryzacji. |
| `HAL_EBUS` | Błąd magistrali (niepowodzenie transakcji I2C/SPI). |
| `HAL_EHW` | Błąd sprzętowy (usterka lub błędna konfiguracja peryferium). |
| `HAL_ECONFIG` | Błąd konfiguracji (nieprawidłowe ustawienie/brakująca zależność). |
| `HAL_ESTATE` | Nieprawidłowy stan dla żądanej operacji. |
| `HAL_EUNINIT` | Operacja na niezainicjalizowanym obiekcie/podsystemie. |
| `HAL_EDEPRECATED` | Operacja jest przestarzała. |
| `HAL_EUNKNOWN` | Nieznany błąd. |

Nazwy używają prefiksu `HAL_` zamiast nazw `errno` z POSIX, aby uniknąć
kolizji z `errno.h` oraz warstwą zgodności z gniazdami BSD.

## Funkcje pomocnicze

Wszystkie są `static inline` w [`hal_status.h`](../../../src/hal/core/hal_status.h)
i dostępne zarówno z C, jak i z C++:

```c
bool        hal_status_is_ok(hal_status_t status);        // status == HAL_OK
bool        hal_status_is_error(hal_status_t status);     // status < HAL_NONE
hal_status_t hal_status_from_bool(bool ok, hal_status_t error_status);
bool        hal_status_to_bool(hal_status_t status);      // stara postać bool
const char *hal_status_to_string(hal_status_t status);    // np. "HAL_EINVAL"
```

`hal_status_to_string()` zwraca stabilną nazwę symboliczną (lub
`"HAL_STATUS_UNKNOWN"`), co jest przydatne przy logowaniu:

```c
hal_status_t st = hal_spi_init(0, rx, tx, sck);
if (hal_status_is_error(st)) {
    hal_derr("SPI init failed: %s", hal_status_to_string(st));
}
```

## Konwencja nazewnictwa i migracji statusów

Nowe funkcje, które mogą się nie powieść, zwracają bezpośrednio
`hal_status_t`. Podczas migracji dotychczasowy typ wyniku decyduje, czy
oryginalna nazwa może zostać użyta dla funkcji statusowej, czy potrzebny jest
odpowiednik `_ex`:

- Starsza funkcja `void`, która może zakończyć się błędem, zaczyna
  bezpośrednio zwracać `hal_status_t`.
  Dotychczasowy kod może nadal ignorować zwracaną wartość, a zbędny
  adapter `_ex` nie jest utrzymywany. Dotyczy to między innymi
  `hal_eeprom_commit()`,
  `hal_display_init()`, `hal_dac_write()` i `hal_i2c_init()`.

- Starsza funkcja `bool` pozostaje prostym adapterem zgodności,
  ponieważ ujemne błędy `hal_status_t` są w C traktowane jako prawda. Sąsiadująca
  z nią funkcja `_ex` odpowiada za walidację i wykonanie.

- Funkcje pomocnicze **zwracające wartość** udostępniają swój wynik przez
  **parametr wyjściowy**, pozostawiając zwracaną wartość wolną dla statusu:

  ```c
  int  w = hal_display_get_width();              // wersja starsza: 0, gdy nieskonfigurowano
  hal_status_t st = hal_display_get_width_ex(&w); // _ex: status + wartość w *w
  ```

- Inicjalizatory **zwracające uchwyt** zapisują go w parametrze wyjściowym,
  a wynik `NULL` zamieniają na kod błędu:

  ```c
  hal_rtc_t rtc = NULL;
  hal_status_t st = hal_rtc_init_ex(&cfg, &rtc);  // HAL_OK lub precyzyjny kod błędu
  ```

- **Rozwiązanie kolizji nazw:** gdy `hal_foo_bar_ex()` już istnieje jako starszy
  punkt wejścia (inne znaczenie „ex"), wariant statusu wstawia `_status` przed
  `_ex`. Obecne przypadki:
  `hal_wifi_ping_status_ex()` (starsze `hal_wifi_ping_ex()` zwracające int) oraz
  `hal_display_init_ssd1306_i2c_status_ex()` (starsze, wybierające magistralę
  `hal_display_init_ssd1306_i2c_ex()`).

- **Proste zapytania o stan**, które nie mogą się nie powieść (na przykład
  `hal_littlefs_is_mounted()`, `hal_spi_write_dma_async_busy()`), zwracają stan,
  a nie wynik operacji, która może się nie powieść, dlatego nie mają wariantu
  `_ex`.

## Gdzie udokumentowane są warianty statusu

Funkcje statusowe i ich adaptery zgodności są opisane **obok siebie** w
odpowiednich sekcjach dokumentacji modułów, wraz z przykładami:

| Obszar | Sekcja |
|---|---|
| Magistrale (`hal_spi`, `hal_i2c`, `hal_swserial`) | [Magistrale komunikacyjne](09_buses.md) |
| GPIO/peryferia (`hal_dac`, `hal_pcnt`) | [GPIO, ADC i PWM](05_gpio_adc_pwm.md) |
| Wyświetlacz (`hal_display`) | [Magistrala CAN i wyświetlacz](10_can_display.md) |
| Urządzenia wyjściowe (`hal_dac`, `hal_rgb_led`, `hal_pga2311`) | [Urządzenia wyjściowe](13_output_devices.md) |
| RTC (`hal_rtc`) | [Czujniki](11_sensors.md) |
| Pamięć masowa (`hal_eeprom`, `hal_kv`, `hal_littlefs`) | [Pamięć masowa](14_storage.md) |
| Sieć (`hal_wifi`, `hal_tcp`, `hal_udp`, `hal_mqtt`, `hal_notify`, `hal_wireguard`) | [Łączność sieciowa](15_connectivity.md) |

## Wytyczne migracji

- W nowym kodzie należy używać przede wszystkim funkcji zwracającej status:
  albo głównej
  funkcję zwracającą `hal_status_t`, albo jej towarzyszącą funkcję `_ex`,
  gdy starsza sygnatura wartości/uchwytu/`bool` musi pozostać dostępna.
- Traktuj `hal_status_is_error(st)` jako ogólny warunek wykrywania błędu.
  Sprawdzaj konkretny kod tylko wtedy, gdy wpływa on na dalsze działanie.
- Błąd, którego starsza funkcja `bool` nie potrafi rozróżnić, jest odwzorowywany
  na kod najlepiej pasujący do danego modułu. Kod ten jest opisany w nagłówku i
  sekcji modułu. Funkcja statusowa odpowiada za walidację, wywołanie backendu i
  odwzorowanie błędów; nie może być adapterem wywołującym starszą funkcję.
