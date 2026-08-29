# API statusów (`hal_status_t`)

*Dostępne również [po angielsku](../en/01_status_api.md).*

> **Część [Dokumentacji API JaszczurHAL](../../pl/JaszczurHAL_API.md)**

Obejmuje: `hal_status_t`, funkcje pomocnicze statusu w
[`hal_status.h`](../../../src/hal/core/hal_status.h), migrację istniejących funkcji
do wariantów zwracających status oraz towarzyszące funkcje `_ex` używane przez
zmigrowane moduły.

## Dlaczego istnieje

Historyczne API HAL zgłasza wyniki jako `bool` (sukces/porażka), licznik
`int`/`size_t`, uchwyt (`NULL` w razie błędu) lub zwykłe `void`. Te kształty mówią
*że* coś się nie powiodło, ale nie *dlaczego*. `hal_status_t` dodaje jeden,
jednolity typ wyniku, dzięki czemu wywołujący mogą rozgałęziać się na podstawie
przyczyny - nieprawidłowy argument, niezainicjalizowany backend, błąd magistrali,
brak obiektu, przepełnienie - bez wymyślania osobnej konwencji błędów dla
każdego modułu.

Migracja jest **status-first**. Funkcja zwracająca status odpowiada za
walidację, wykonanie w backendzie i mapowanie błędów. Historyczne API typu
`bool`, wartość lub uchwyt pozostaje jako cienki wrapper kompatybilności i
zyskuje towarzyszącą funkcję `_ex` tam, gdzie to potrzebne. Historyczna
funkcja `void`, która może zakończyć się błędem, zaczyna bezpośrednio zwracać `hal_status_t`, co
pozostaje kompatybilne źródłowo z wywołującymi, którzy ignorują wynik;
zbędne adaptery `_ex` są usuwane. Dzięki temu unika się funkcji statusu, które
mogłyby jedynie zgadywać po wywołaniu tracącego informacje starego wrappera.

## Kody statusów

Wartości `hal_status_t` są dodatnie przy sukcesie i ujemne przy porażce, więc
`status == HAL_OK` sprawdza sukces, a `status < 0` (lub `hal_status_is_error()`)
sprawdza ogólną porażkę.

| Kod | Znaczenie |
|---|---|
| `HAL_OK` | Operacja zakończona pomyślnie. |
| `HAL_NONE` | Brak statusu / niezainicjalizowany / stan nieprawidłowy (wartość `0`). |
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
| `HAL_EDEPRECATED` | Operacja jest przestarzała (deprecated). |
| `HAL_EUNKNOWN` | Nieznany błąd. |

Nazwy używają prefiksu `HAL_` zamiast nazw `errno` z POSIX, aby uniknąć
kolizji z `errno.h` oraz warstwą kompatybilności BSD sockets.

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

Nowe zawodne API zwracają bezpośrednio `hal_status_t`. Podczas migracji o tym,
czy oryginalna nazwa może stać się API statusu, czy potrzebuje towarzyszącej
funkcji `_ex`, decyduje historyczny kształt zwracanej wartości:

- Historyczna funkcja `void`, która może zakończyć się błędem, zaczyna
  bezpośrednio zwracać `hal_status_t`.
  Istniejący wywołujący mogą nadal ignorować zwracaną wartość, a zbędny
  adapter `_ex` nie jest zachowywany. Przykłady obejmują `hal_eeprom_commit()`,
  `hal_display_init()`, `hal_dac_write()` i `hal_i2c_init()`.

- Historyczna funkcja `bool` pozostaje cienkim wrapperem kompatybilności,
  ponieważ ujemne błędy `hal_status_t` są prawdziwe (truthy) w C. Sąsiadująca
  z nią funkcja `_ex` odpowiada za walidację i wykonanie.

- Funkcje pomocnicze **zwracające wartość** udostępniają swój wynik przez
  **parametr wyjściowy**, pozostawiając zwracaną wartość wolną dla statusu:

  ```c
  int  w = hal_display_get_width();              // wersja starsza: 0, gdy nieskonfigurowano
  hal_status_t st = hal_display_get_width_ex(&w); // _ex: status + wartość w *w
  ```

- Inicjalizatory **zwracające uchwyt** produkują uchwyt przez parametr
  wyjściowy i mapują wynik `NULL` na kod błędu:

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

- **Czyste zapytania o stan**, które nie mogą się nie powieść (na przykład
  `hal_littlefs_is_mounted()`, `hal_spi_write_dma_async_busy()`), raportują
  stan, a nie wynik zawodnej operacji, więc nie zachowują formy `_ex`.

## Gdzie udokumentowane są warianty statusu

Funkcje statusu i ich wrappery kompatybilności są udokumentowane **w linii,
obok siebie** w odpowiednich sekcjach referencji modułów, wraz z przykładami:

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

- Nowy kod powinien preferować funkcję zwracającą status: albo główną
  funkcję zwracającą `hal_status_t`, albo jej towarzyszącą funkcję `_ex`,
  gdy starsza sygnatura wartości/uchwytu/`bool` musi pozostać dostępna.
- Traktuj `hal_status_is_error(st)` jako ogólną bramkę porażki; rozgałęziaj
  się na konkretne kody tylko tam, gdzie na ich podstawie podejmujesz działanie.
- Resztkowa porażka, której starsze `bool` nie potrafi rozróżnić, jest
  mapowana na najbardziej reprezentatywny kod dla danego modułu (udokumentowany
  w nagłówku i sekcji każdego modułu). Ścieżki statusu odpowiadają za
  walidację, wykonanie w backendzie i mapowanie błędów; nie mogą być
  adapterami wywołującymi starszy wrapper.
