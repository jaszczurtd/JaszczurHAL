<a id="api-statusów-hal_status_t"></a>

# Wyniki operacji i obsługa błędów (`hal_status_t`)

*Dostępne również [po angielsku](../en/01_status_api.md).*

> **Część [Dokumentacji API JaszczurHAL](../../pl/JaszczurHAL_API.md)**

Typ `hal_status_t` pozwala sprawdzić wynik operacji i rozpoznać przyczynę błędu. Ten rozdział opisuje kody, funkcje pomocnicze z [`hal_status.h`](../../../src/hal/core/hal_status.h) oraz zasady przechodzenia ze starszego API na funkcje zwracające status, w tym warianty `_ex`.

<a id="dlaczego-istnieje"></a>

## Dlaczego warto używać kodów statusu

Starsze funkcje HAL zwracają `bool`, liczbę typu `int` lub `size_t`, uchwyt albo `void`. Takie interfejsy nie zapewniają jednolitego sposobu rozpoznania błędu i jego przyczyny. `hal_status_t` pozwala odróżnić m.in. nieprawidłowy argument, brak inicjalizacji, błąd magistrali, brak obiektu i przepełnienie. Aplikacja nie musi wprowadzać osobnej konwencji dla każdego modułu.

To funkcja zwracająca status sprawdza argumenty, wykonuje operację i przypisuje błędom odpowiednie kody. Starsze funkcje zgodności korzystają z niej, a nie odwrotnie.

## Kody statusów

W tym API powodzenie ma wartość dodatnią, a błędy - ujemne. `status == HAL_OK` sprawdza pomyślne zakończenie operacji; `status < 0` lub `hal_status_is_error()` wykrywa błąd. Wartość `0` oznacza `HAL_NONE`, nie `HAL_OK`.

| Kod | Znaczenie |
|---|---|
| `HAL_OK` | Operacja zakończona pomyślnie. |
| `HAL_NONE` | Brak statusu / stan niezainicjalizowany / stan nieprawidłowy (wartość `0`). |
| `HAL_EINVAL` | Nieprawidłowy argument lub nieobsługiwana wartość parametru. |
| `HAL_EBUSY` | Zasób lub magistrala są zajęte. |
| `HAL_ETIMEOUT` | Przekroczono limit czasu operacji. |
| `HAL_EIO` | Ogólny błąd I/O urządzenia, magistrali lub backendu. |
| `HAL_EUNSUPPORTED` | Operacja nie jest obsługiwana przez wybraną platformę lub implementację. |
| `HAL_ENOENT` | Żądany obiekt, urządzenie lub wpis nie został znaleziony. |
| `HAL_EAGAIN` | Ponów próbę później; operacja nieblokująca wymagałaby teraz oczekiwania. |
| `HAL_EOVERFLOW` | Operacja przepełniłaby bufor lub zasób. |
| `HAL_ENOMEM` | Brak pamięci lub wolnego miejsca w puli zasobów. |
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

Funkcje są zdefiniowane jako `static inline` w [`hal_status.h`](../../../src/hal/core/hal_status.h) i dostępne w C oraz C++:

```c
bool        hal_status_is_ok(hal_status_t status);        // status == HAL_OK
bool        hal_status_is_error(hal_status_t status);     // status < HAL_NONE
hal_status_t hal_status_from_bool(bool ok, hal_status_t error_status);
bool        hal_status_to_bool(hal_status_t status);      // stara postać bool
const char *hal_status_to_string(hal_status_t status);    // np. "HAL_EINVAL"
```

Do zapisu statusu w logu użyj `hal_status_to_string()`. Funkcja zwraca stabilną nazwę symboliczną albo `"HAL_STATUS_UNKNOWN"`:

```c
hal_status_t st = hal_spi_init(0, rx, tx, sck);
if (hal_status_is_error(st)) {
    hal_derr("SPI init failed: %s", hal_status_to_string(st));
}
```

<a id="konwencja-nazewnictwa-i-migracji-statusów"></a>

## Nazwy funkcji i zgodność ze starszym API

Nowe funkcje, które mogą zakończyć się błędem, zwracają `hal_status_t`. Przy zmianie istniejącej funkcji sposób zachowania zgodności zależy od jej dotychczasowego typu wyniku:

- Funkcja zwracająca wcześniej `void` może zacząć zwracać `hal_status_t` pod tą samą nazwą. Dotychczasowe wywołania nadal mogą ignorować wynik, więc dodatkowy wariant `_ex` nie jest potrzebny. Tak zmieniono m.in. `hal_eeprom_commit()`, `hal_display_init()`, `hal_dac_write()` i `hal_i2c_init()`.

- Funkcja zwracająca `bool` pozostaje adapterem zgodności. Nie można po prostu zastąpić jej wyniku statusem: ujemny kod błędu jest w C wartością prawdziwą. Sprawdzenie argumentów i wykonanie operacji przejmuje odpowiadająca jej funkcja `_ex`.

- Funkcja zwracająca dane przekazuje je przez **parametr wyjściowy**, a jako wynik wywołania zwraca status:

  ```c
  int  w = hal_display_get_width();              // wersja starsza: 0, gdy nieskonfigurowano
  hal_status_t st = hal_display_get_width_ex(&w); // _ex: status + wartość w *w
  ```

- Funkcja tworząca uchwyt zapisuje go w parametrze wyjściowym. Niepowodzenie, wcześniej sygnalizowane przez `NULL`, opisuje kodem błędu:

  ```c
  hal_rtc_t rtc = NULL;
  hal_status_t st = hal_rtc_init_ex(&cfg, &rtc);  // HAL_OK lub precyzyjny kod błędu
  ```

- Gdy nazwa `hal_foo_bar_ex()` jest już zajęta przez starszą funkcję, wariant statusowy otrzymuje `_status` przed `_ex`. Dotyczy to `hal_wifi_ping_status_ex()` obok starszego `hal_wifi_ping_ex()` zwracającego int oraz `hal_display_init_ssd1306_i2c_status_ex()` obok `hal_display_init_ssd1306_i2c_ex()`, który pozwala wybrać magistralę.

- Proste zapytania o stan, które nie mogą zakończyć się błędem, nie potrzebują wariantu `_ex`. Przykłady to `hal_littlefs_is_mounted()` i `hal_spi_write_dma_async_busy()`.

<a id="gdzie-udokumentowane-są-warianty-statusu"></a>

## Gdzie szukać opisu poszczególnych funkcji

Funkcje zwracające status i ich starsze odpowiedniki są opisane obok siebie w dokumentacji modułów, wraz z przykładami:

| Obszar | Sekcja |
|---|---|
| Magistrale (`hal_spi`, `hal_i2c`, `hal_swserial`) | [Magistrale komunikacyjne](09_buses.md) |
| GPIO/peryferia (`hal_dac`, `hal_pcnt`) | [GPIO, ADC i PWM](05_gpio_adc_pwm.md) |
| Wyświetlacz (`hal_display`) | [Magistrala CAN i wyświetlacz](10_can_display.md) |
| Urządzenia wyjściowe (`hal_dac`, `hal_rgb_led`, `hal_pga2311`) | [Urządzenia wyjściowe](13_output_devices.md) |
| RTC (`hal_rtc`) | [Czujniki](11_sensors.md) |
| Pamięć masowa (`hal_eeprom`, `hal_kv`, `hal_littlefs`) | [Pamięć masowa](14_storage.md) |
| Sieć (`hal_wifi`, `hal_tcp`, `hal_udp`, `hal_mqtt`, `hal_notify`, `hal_wireguard`) | [Łączność sieciowa](15_connectivity.md) |

<a id="wytyczne-migracji"></a>

## Jak przejść na API zwracające status

- W nowym kodzie wybieraj funkcję zwracającą `hal_status_t`: podstawową albo wariant `_ex`, jeśli trzeba zachować starszy interfejs zwracający dane, uchwyt lub `bool`.
- Do ogólnego wykrywania błędów używaj `hal_status_is_error(st)`. Rozróżniaj konkretne kody wtedy, gdy aplikacja reaguje na nie w różny sposób.
- Błędy, których starsza funkcja `bool` nie rozróżniała, otrzymują kody właściwe dla danego modułu. Ich znaczenie opisują nagłówek i dokumentacja modułu. Funkcja statusowa musi sama sprawdzać argumenty, wykonać operację i odwzorować błąd; nie może jedynie wywoływać starszego adaptera.
