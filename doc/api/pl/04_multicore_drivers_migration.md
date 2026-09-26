<a id="bezpieczeństwo-wielordzeniowe-drivery-i-logowanie"></a>

# Współbieżność, sterowniki i logowanie

*Dostępne również [po angielsku](../en/04_multicore_drivers_migration.md).*

> **Część [dokumentacji referencyjnej API JaszczurHAL](../../pl/JaszczurHAL_API.md)**

<a id="zasady-bezpieczeństwa-wielordzeniowego"></a>

## Zasady pracy z wieloma rdzeniami i zadaniami

JaszczurHAL obsługuje pracę na dwóch rdzeniach RP2040/RP2350 i ESP32-S3. Na jednordzeniowym STM32G474 synchronizacja zadań za pomocą muteksów jest dostępna w konfiguracji FreeRTOS. Bezpieczne współdzielenie modułu zależy od jego zasad inicjalizacji i dostępu opisanych poniżej.

Przestrzegaj następujących zasad:

### Przenośny punkt wejścia aplikacji

Gdy włączona jest flaga `HAL_PROVIDE_APP_ENTRY`, funkcję `main()` dostarcza
natywny runtime. `app_start()` jest wywoływana raz, przed uruchomieniem zadań
aplikacji. W wersji bare metal dla RP funkcja `app_task0()` działa jako główna
pętla rdzenia 0, natomiast flaga
`HAL_ENABLE_APP_TASK1` uruchamia rdzeń 1 poprzez `multicore_launch_core1()`
jeszcze przed `app_start()`. Kod startowy rdzenia 1 dołącza do koordynatora
transakcji flash i czeka. `app_task1()` zaczyna działać dopiero po zakończeniu
`app_start()` na rdzeniu 0. Dzięki temu podczas uruchamiania można bezpiecznie
zainicjalizować EEPROM/KV we flashu, zanim drugi rdzeń uzyska dostęp do stanu
aplikacji.

W konfiguracji RP z FreeRTOS SMP funkcje aplikacji działają jako zadania przypisane do rdzeni 0 i 1. Na STM32G474 bez systemu operacyjnego obie funkcje są wywoływane kooperacyjnie w jednej pętli, natomiast FreeRTOS uruchamia niezależne `task0` i opcjonalne `task1`. Na ESP32-S3 scheduler FreeRTOS jest już uruchomiony przez ESP-IDF. HAL domyślnie tworzy `task0` na rdzeniu 0 i opcjonalne `task1` na rdzeniu 1. Konfiguracja może wskazać inny rdzeń lub `-1`, czyli brak przypisania.

Wspólny koordynator szereguje operacje modyfikujące flash: zatrzymuje drugi rdzeń w bezpiecznym miejscu, wstrzymuje TinyUSB, odrzuca operacje wykonywane z XIP i zajęte DMA sięgające do flash (pierścienie peryferium->RAM pracują dalej) i maskuje lokalne przerwania. Po zakończeniu przywraca tymczasowo zmieniony stan. W konfiguracji bare-metal korzysta z mechanizmu wielordzeniowego Pico SDK, a w FreeRTOS SMP - z jego odpowiednika współpracującego ze schedulerem. Wszystkie zapisy EEPROM i funkcje zwrotne LittleFS `program`/`erase` przechodzą przez ten mechanizm.

<a id="inicjalizacja-tylko-jeden-rdzeń"></a>

### Inicjalizacja i zwalnianie zasobów na jednym rdzeniu

Domyślnie wykonuj `*_init()`, `*_create()`, `*_deinit()` i `*_destroy()` z jednego rdzenia; inicjalizację najlepiej zakończyć w `app_start()` na rdzeniu 0, zanim uruchomi się współbieżny dostęp. Nie zakładaj, że ogólna deklaracja bezpieczeństwa wielowątkowego obejmuje tworzenie i niszczenie zasobów. Zakres ochrony sprawdź w dokumentacji konkretnego modułu.

Przygotowanie zasobów może obejmować przydzielenie miejsca w statycznej puli, konfigurację peryferiów i utworzenie stanu wewnętrznego. Wszystkie te czynności muszą zakończyć się przed użyciem zasobu. Przed jego zniszczeniem zatrzymaj użytkowników i callbacki zgodnie z wymaganiami danego API; sama obecność muteksu nie rozwiązuje problemu czasu życia obiektu.

<a id="czas-działania-współbieżne-backendy-sprzętowe"></a>

### Współbieżne wywołania po inicjalizacji

Po inicjalizacji większość funkcji HAL można współdzielić między rdzeniami RP2040/RP2350 oraz zadaniami w obsługiwanych konfiguracjach FreeRTOS, w tym na STM32G474 i w dostarczonych implementacjach ESP32-S3. Nie jest to jednak gwarancja dla każdej funkcji. Sprawdź ograniczenia konkretnego modułu; stosowane mechanizmy ochrony dzielą się na:

- **Muteksy instancji** chronią API oparte na uchwytach, np. `hal_can`, `hal_thermocouple`, `hal_rtc` i `SmartTimers`.
- **Muteksy magistral** chronią współdzielone zasoby `hal_spi` i `hal_i2c`.
- **Muteksy modułów globalnych** chronią `hal_eeprom`, `hal_display`, `hal_gps`, `hal_external_adc`, `hal_wifi`, `hal_udp`, `hal_wireguard`, `hal_mqtt`, `hal_kv`, zegar systemowy i NTP w `hal_time` oraz wyjście diagnostyczne portu szeregowego.
- **Funkcje bez stanu współdzielonego**, takie jak `hal_bits`, `hal_math`, czyste funkcje `hal_time`, `hal_crypto`, `hal_constrain` i `hal_map`, nie wymagają takiej synchronizacji.

Muteksy modułów globalnych i magistral są tworzone atomowo tylko raz (`create-once`). Dwa zadania lub rdzenie nie utworzą więc różnych blokad dla tego samego zasobu. Najlepiej jednak utworzyć blokady podczas `init`/`begin`, zanim rozpocznie się współbieżny dostęp.

Moduły `hal_uart` i `pidController` wymagają synchronizacji po stronie aplikacji lub użycia z jednego rdzenia.

**Czas systemowy i NTP:** przy włączonym `HAL_ENABLE_TIME` funkcje zegara, NTP i statusu z `hal_time` można wywoływać współbieżnie z wielu zadań i rdzeni, ale nie z procedury obsługi przerwania (ISR). Trzy wywołania należą do konfiguracji: `hal_time_set_timezone()`, `hal_time_attach_rtc_ex()` i `hal_time_detach_rtc_ex()`. Na platformach sprzętowych zmiana strefy czasowej nie jest synchronizowana z `hal_time_get_local()` ani `hal_time_format_local()`, dlatego ustaw strefę, zanim inne zadania zaczną odczytywać czas lokalny. [Opis `hal_time`](15_connectivity.md#hal_time---funkcje-pomocnicze-kalendarza-oraz-opcjonalny-czas-systemowyntp) wyjaśnia, jak wywołujący dzielą się obsługą NTP.

<a id="backend-mock"></a>

### Implementacja testowa (mock)

Implementacje w `impl/.mock/` służą do deterministycznych, jednowątkowych testów jednostkowych. Nie odwzorowują synchronizacji między wątkami zapewnianej przez implementacje sprzętowe. Osobny, opcjonalny test FreeRTOS POSIX sprawdza na hoście współpracę schedulera, muteksów, opóźnień i mechanizmu `create-once`.

---

<a id="drivery-i-frameworki"></a>

## Sterowniki i biblioteki zewnętrzne

Niskopoziomowe sterowniki dołączone lub dostosowane do HAL znajdują się w `src/hal/impl/rp2040/drivers/` albo we właściwym katalogu tematycznym `src/hal/`. Integracje wyższego poziomu również są pogrupowane tematycznie w `src/hal/`, a kod zależny od platformy trafia do `src/hal/impl/`. Są to szczegóły implementacji; aplikacja powinna korzystać z publicznego API HAL.

### Zestawienie, autorzy i ścieżki licencji

| Katalog sterownika | Zastosowanie w HAL | Autor(zy) źródłowi | Licencja | Ścieżka licencji w repozytorium |
|---|---|---|---|---|
| Silnik GFX (port) | Renderowanie `hal_display` (geometria, tekst, mapy bitowe) umieszczone w `hal/display/drivers/jh_gfx.*` | Limor Fried (Ladyada) i współtwórcy (Adafruit GFX) | BSD-2-Clause (adnotacja autorska w nagłówkach źródłowych; biblioteka nie jest już dołączana/linkowana) | `src/hal/display/drivers/jh_gfx.h` |
| Sterownik ILI9341 (port) | Backend TFT (`HAL_DISPLAY_ILI9341`) umieszczony w `hal/display/drivers/ili9341_driver.*` | Limor Fried (Ladyada) (Adafruit ILI9341) | BSD-2-Clause (adnotacja autorska w nagłówkach źródłowych) | `src/hal/display/drivers/ili9341_driver.h` |
| Sterownik ST77xx/GC9A01 (port) | Backendy ST7735/ST7789/ST7796S wraz ze wsparciem dla okrągłego TFT GC9A01 opartym na Zephyr, umieszczone w `hal/display/drivers/st77xx_driver.*` | Limor Fried (Ladyada) (Adafruit ST7735/ST7789), sterownik Zephyr GC9x01x jako wzorzec zgodności dla GC9A01 | BSD-2-Clause dla oryginalnej ścieżki ST77xx; notatki dot. przeniesienia zachowania GC9A01 odwołują się do źródeł Zephyr na licencji Apache-2.0 | `src/hal/display/drivers/st77xx_driver.h` |
| Sterownik z rodziny SSD1306 (port) | Backend OLED (`HAL_ENABLE_SSD1306`) umieszczony w `hal/display/drivers/ssd1306_driver.*` i rozszerzony o warianty SSD1309/SSD1315/SH1106/CH1115 | Limor Fried (Ladyada) i współtwórcy (Adafruit SSD1306), zachowanie sterownika wyświetlacza oparte o logikę zapożyczoną z projektu Zephyr | BSD-2-Clause (adnotacja autorska w nagłówkach źródłowych) | `src/hal/display/drivers/ssd1306_driver.h` |
| Sterowniki RGB OLED SSD1331/SSD135x (porty) | Współdzielone sterowniki OLED RGB565 poprzez HAL SPI/GPIO (`HAL_ENABLE_SSD1331`, `HAL_ENABLE_SSD135X`) | `display_ssd1331.c` i `display_ssd135x.c` z logiką zapożyczoną z projektu Zephyr | Kod referencyjny na licencji Apache-2.0; implementacja w repozytorium została dostosowana do transportu HAL | `src/hal/display/drivers/rgb_oled_driver.h` |
| Sterownik LCD ST7567 (port) | Współdzielony monochromatyczny sterownik LCD poprzez HAL I2C lub SPI/GPIO (`HAL_ENABLE_ST7567`) | `display_st7567.c` i `display_st7567_regs.h` z logiką zapożyczoną z projektu Zephyr | Kod referencyjny na licencji Apache-2.0; implementacja w repozytorium została dostosowana do transportu HAL | `src/hal/display/drivers/st7567_driver.h` |
| Sterowniki EPD SSD16xx/UC81xx (porty) | Współdzielone monochromatyczne sterowniki e-papieru poprzez HAL SPI/GPIO (`HAL_ENABLE_SSD16XX`, `HAL_ENABLE_UC81XX`) | `ssd16xx.c`, `ssd16xx_regs.h`, `uc81xx.c` i `uc81xx_regs.h` Kod sterowników Zephyr stanowi inspirację dla protokołu/maszyny stanów | Apache-2.0; kod dostosowany do publicznej konfiguracji i transportu HAL, w którym podstawowym wynikiem jest wartość `hal_status_t` | `src/hal/display/drivers/ssd16xx_driver.h`, `src/hal/display/drivers/uc81xx_driver.h` |
| Rdzeń NeoPixel (port) | `hal_rgb_led` | Phil „Paint Your Dragon" Burgess i współtwórcy (Adafruit_NeoPixel) | LGPL (adnotacja autorska w nagłówkach źródłowych) | `src/hal/gpio/neopixel/COPYING`, `src/hal/gpio/neopixel/jh_neopixel.h` |
| `DS3231` | Backend RTC DS3231 (`hal_rtc`) | Eric Ayars, Andrew Wickert, Jean-Claude Wippler, współtwórcy Northern Widget | Deklaracje domeny publicznej w nagłówkach źródłowych | `src/hal/rtc/ds3231/ds3231.h`, `src/hal/rtc/ds3231/ds3231.cpp` |
| Sterownik DHT11/DHT22 (port) | `hal_dht` | Bonezegei (Jofel Batutay) | Adnotacja autorska w nagłówku źródłowym | `src/hal/temperature/dht/hal_dht.cpp` |
| `MCP2515` | Backend `hal_can` | Seeed Technology (Loovee), Cory J. Fowler | LGPL (dołączony plik `license.txt`) | `src/hal/can/mcp2515/license.txt` i `src/hal/can/mcp2515/mcp2515_driver.h` |
| Współdzielony silnik WireGuard/lwIP | Backend `hal_wireguard` | Kenta Ida (oryginalne API), Daniel Hope (rdzeń), Marcin Kielesiński (porty RP2040/Pico W i współdzielony HAL) | BSD-3-Clause | `src/hal/network/wireguard/core/LICENSE` |
| `PubSubClient` | Backend `hal_mqtt` | Nick O'Leary | MIT | `src/hal/network/mqtt/PubSubClient/LICENSE.txt` |
| TinyGPS++ (port) | Logika parsowania NMEA dla `hal_gps` przeniesiona do `gps_nmea_parser` | Mikal Hart | LGPL-2.1+ (adnotacja autorska w nagłówkach źródłowych; biblioteka nie jest już dołączana/linkowana) | `src/hal/gps/gps_nmea_parser.cpp` |

Uwaga: `hal/display/drivers/Fonts/` zawiera dodatkowe informacje licencyjne dla
poszczególnych czcionek w nagłówkach czcionek (np. `TomThumb.h`,
`Tiny3x3a2pt7b.h`).

### Zmiany integracyjne i ich uzasadnienie

| Obszar | Co się zmieniło | Dlaczego |
|---|---|---|
| Ścieżki plików nagłówkowych | Moduły HAL dołączają wbudowane zależności z lokalnych katalogów `drivers/` i `frameworks/`; odwołania wewnątrz modułów używają lokalnych ścieżek względnych. | Kod firm trzecich pozostaje wewnątrz HAL i nie zanieczyszcza globalnej przestrzeni nazw nagłówków. |
| Kompilacja warunkowa | Pliki `.cpp` sterowników są opakowane strażnikami `HAL_ENABLE_*` na poziomie modułu. | Po wyłączeniu modułu do kompilacji nie trafiają ani adaptery HAL, ani kod backendu firm trzecich. |
| Synchronizacja SPI | Sterowniki wykonujące transakcje SPI wywołują w razie potrzeby `hal_spi_lock`/`hal_spi_unlock` (CAN, współdzielone sterowniki paneli TFT). | Zapobiega przeplataniu się transakcji SPI między wątkami/rdzeniami. |
| Synchronizacja I2C | Sterowniki wykonujące transakcje I2C wywołują w razie potrzeby `hal_i2c_lock_bus`/`hal_i2c_unlock_bus` i odwzorowują numery magistral. | Zapobiega mieszaniu transakcji magistrali 0/1 i zapewnia przewidywalne działanie przy współbieżności. |
| Muteksy poszczególnych sterowników | Wybrane sterowniki i adaptery mają własne muteksy dla operacji wieloetapowych (`MCP2515`, `MAX6675`, `MCP9600`, adaptery HAL). | Ogranicza to ryzyko wyścigów w sekwencjach odczyt/modyfikacja/zapis i poleceniach wymagających kilku wywołań. |
| Współdzielona fasada RTC | `hal_rtc.cpp` zarządza pulą uchwytów, walidacją, muteksami, konwersją czasu Unix, odwzorowaniem statusów i adapterami zgodności. Providery dołączane na etapie linkowania implementują obsługę układów I2C PCF8563/DS3231, RTC w domenie podtrzymania STM32G474, timera AON RP albo pamięci mock. | Protokół układu, rejestry targetu i funkcje testowe pozostają za interfejsem providera, a implementacje I2C i natywne korzystają z jednej fasady. |
| Osobne API niskiego poboru mocy | `hal_power.h` definiuje przenośne stany, zasady działania, obsługiwane funkcje, przyczyny wybudzenia i callbacki `prepare`/`resume`. Backendy targetów implementują szczegóły WFI/STOP/Standby i korzystają z wewnętrznego interfejsu względnego wybudzenia RTC. | Dzięki temu moduł RTC nie przejmuje zasad dotyczących procesora, drzewa zegarów, wstrzymywania peryferiów, resetu ani schedulera. |
| Współdzielona fasada GPS | `hal_gps.cpp` wybiera podczas kompilacji HAL UART albo SoftwareSerial i zarządza inicjalizacją transportu, odpytywaniem, zapasowym mechanizmem składania ramek i dostępnością. Wspólny mechanizm GPS zajmuje się parsowaniem, blokadami, diagnostyką i wszystkimi funkcjami odczytującymi pozycję, w tym funkcjami mock do ustawiania danych testowych. | Usuwa to powielone fasady transportu RP2040/STM32G474 oraz kopię funkcji odczytowych mock, a jednocześnie zachowuje wybór transportu i deterministyczne dane testowe. |
| Współdzielony rdzeń serial/debug | `hal_serial.cpp` odpowiada za formatowanie, prefiksy, znaczniki czasu, wyciszanie i ograniczanie częstotliwości komunikatów, pierścień ISR SPSC, przekazywanie komunikatów do konsoli sieciowej, leniwe tworzenie muteksów metodą `create-once` oraz publiczne punkty wejścia serial/debug. Porty dowiązywane na etapie linkowania odpowiadają wyłącznie za RP USB CDC, ESP32-S3 USB Serial/JTAG VFS, STM32 USART2/stdout albo przechwytywanie wyjścia i wstrzykiwanie danych RX w mocku. | Zapewnia jedną implementację formatowania i stanu, zachowując zakończenia linii właściwe dla targetu, zależne od transportu działanie funkcji `flush` i atomowe granice komunikatów między zadaniami/rdzeniami. |
| Obsługa drugiego kontrolera I2C | API HAL I2C oraz adaptery sterowników używają indeksu magistrali 0/1 dla pierwszego i drugiego sprzętowego kontrolera danego targetu. | Pozwala to korzystać z drugiego kontrolera bez pomijania synchronizacji zapewnianej przez HAL. |
| Współdzielony stos wyświetlaczy | Dołączone biblioteki Adafruit GFX/ILI9341/ST77xx/SSD1306/BusIO zostały zastąpione przenośnym stosem wyświetlaczy umieszczonym w repozytorium (`hal/display/drivers/`) i opartym wyłącznie na HAL SPI/I2C/GPIO. Publiczna fasada obsługuje wyświetlacze ILI9341, ST77xx/GC9A01, z rodziny SSD1306, SSD1331/SSD135x oraz ST7567 przez GFX i bezpośredni zapis, jeśli sterownik deklaruje taką możliwość. | Ta sama implementacja steruje wyświetlaczami na RP2040 i STM32G474, a po wyłączeniu modułu nie trafia do kompilacji. |
| Przenośny silnik NMEA | `hal_gps` używa parsera NMEA w drzewie repozytorium (`hal/gps/gps_nmea_parser.cpp`), z logiką parsowania przeniesioną z TinyGPS++ (LGPL); sama biblioteka TinyGPS++ nie jest już dołączana ani linkowana. | Ten sam silnik parsera/getterów działa na RP2040, STM32G474 i mock i jest usuwany z kompilacji przy wyłączonym module GPS. |
| Transport UDP | `hal_udp` korzysta ze współdzielonego silnika raw lwIP i jest włączany podczas kompilacji flagą `HAL_ENABLE_UDP`. | Obsługa UDP pozostaje opcjonalna i po wyłączeniu nie zwiększa rozmiaru kodu. |
| Dołączenie WireGuard | `hal_wireguard` korzysta ze wspólnej implementacji lwIP włączanej flagą `HAL_ENABLE_WIREGUARD`; funkcje targetu dostarczają bazowy `netif`, kontekst stosu, entropię i czas. | WireGuard działa deterministycznie i bez dostępu do sieci podczas testów, a obsługa tras, timerów i zamykania jest wspólna dla obsługiwanych targetów z lwIP. |
| Dołączenie PubSubClient | `hal_mqtt` korzysta z dołączonych źródeł PubSubClient, bramkowanych flagą `HAL_ENABLE_MQTT` w jednostce translacji sterownika. | Obsługa MQTT jest opcjonalna i po wyłączeniu nie zwiększa rozmiaru kodu. |

---

<a id="hak-znacznika-czasu-logowania"></a>

## Dodawanie znacznika czasu do logów

Do komunikatów błędów wysyłanych przez port szeregowy można dołączyć znacznik czasu. Jego tekst przygotowuje opcjonalna funkcja zwrotna aplikacji.

API:

- `typedef bool (*hal_debug_timestamp_hook_t)(char *out, size_t out_size, void *user);`
- `void hal_debug_set_timestamp_hook(hal_debug_timestamp_hook_t hook, void *user);`

Zachowanie:

- Jeżeli funkcja zwrotna zwróci `true` i zapisze niepusty tekst, `hal_derr()` oraz `hal_derr_limited()` dodadzą prefiks `` `[znacznik czasu]` `` przed `ERROR! ...`.
- Bez funkcji zwrotnej albo po wyniku `false` format logowania pozostaje bez zmian.

Typowe użycie:

```c
static bool app_ts_hook(char *out, size_t out_size, void *user) {
        (void)user;
        unsigned long ms = hal_millis();
        snprintf(out, out_size, "t+%lu.%03lus", ms / 1000UL, ms % 1000UL);
        return true;
}

void app_start(void) {
        hal_debug_init(115200, NULL);
        hal_debug_set_timestamp_hook(app_ts_hook, NULL);
}
```

---

<a id="funkcja-pomocnicza-konwersji-czasu"></a>

## Konwersja daty i czasu

`hal_time_from_components(int year, int month, int day, int hour, int minute, int second)`
konwertuje składowe daty/czasu na sekundy epoki Unix.

Walidacja:

- zwraca `0` dla wartości spoza zakresu (rok < 1970, nieprawidłowy
  miesiąc/dzień/czas lub epoka Unix przekraczająca `UINT32_MAX`)
- obsługuje reguły lat przestępnych (łącznie z wyjątkami stuletnimi)

Interfejs zgodności zwraca `0` zarówno przy błędzie, jak i dla poprawnej daty początku epoki Unix. Wewnętrzny moduł `hal/time/jh_calendar` używa `hal_status_t` oraz czasu 64-bitowego, dzięki czemu rozróżnia te przypadki. Ten sam kod sprawdza i przelicza daty RTC, PCF8563 i DS3231 w konfiguracjach RP2040, STM32G474 i mock.

Zawsze dostępne API `hal_time` zawiera również wcześniejsze funkcje czasu
z `tools.cpp`:

- `hal_time_is_daylight_saving_time(...)` stosuje regułę „ostatnia niedziela"
  CET/CEST opartą wyłącznie na dacie i odrzuca nieprawidłowe daty gregoriańskie
- `hal_time_adjust_cet_cest(...)` stosuje odpowiednie przesunięcie i
  normalizuje przejście przez granicę doby
- `hal_time_is_in_range(...)` sprawdza półotwarty przedział `[start, end)`
- `hal_time_extract_minutes(...)` rozbija liczbę minut, z opcjonalnymi
  wyjściami

Są to publiczne implementacje modułu `hal_time`; biblioteka nie eksportuje
drugiego zestawu aliasów funkcji pomocniczych.

---

## Przykłady

Gotowe przykłady znajdują się w katalogu [examples](../../../examples).

Typowe scenariusze tam opisane:

- GPIO wraz z odmierzaniem czasu
- I2C wraz z EEPROM
- WiFi wraz z NTP/czasem systemowym
- WiFi wraz z datagramami UDP
- nieblokujący cykl żądania, odpytywania i odczytu dla DS18B20
- inicjalizacja wyświetlacza

Szczegóły funkcji opisują rozdziały API poszczególnych modułów; poniższe zestawienie pomaga wybrać przenośny interfejs.

---

## Zakres testów hosta

Testy uruchamiane na komputerze są budowane przez CMake. Sprawdzają implementację mock oraz wybrane moduły narzędziowe; nie zastępują testów na urządzeniu.

Testy obejmują następujące cele testowe:

- `test_hal_gpio`, `test_hal_adc`, `test_hal_pwm`, `test_hal_spi`,
  `test_hal_timer`, `test_hal_onewire`, `test_hal_ds18b20`, `test_hal_dht`,
  `test_hal_pga2311`
- `test_stm32_hal_timer` weryfikuje rzeczywisty backend timera STM32G474 w
  kompilacji sterowanej z komputera, łącznie z przeplanowywaniem callbacków i
  zarządzanymi timerami.
- `test_hal_i2c`, `test_hal_i2c_slave`, `test_hal_rgb_led`, `test_hal_external_adc`, `test_ads1x15_driver`, `test_bh1750_driver`, `test_hal_gps`, `test_hal_system`, `test_hal_bits`
- `test_hal_serial`, `test_hal_sc_auth`, `test_hal_serial_session`,
  `test_hal_serial_session_vocabulary`, `test_jh_security_primitives`,
  `test_security_architecture`, `test_serial_architecture`, `test_hal_uart`,
  `test_hal_swserial`; `test_freertos_posix_runtime` dodatkowo uruchamia
  równocześnie wiele źródeł komunikatów serial/debug i sprawdza, czy granice
  komunikatów pozostają nienaruszone.
- `test_hal_can`, `test_hal_thermocouple`, `test_hal_display`
- `test_hal_eeprom`, `test_hal_kv`, `test_hal_wifi`, `test_hal_littlefs`, `test_hal_sdlogger`, `test_hal_udp`, `test_hal_wireguard`, `test_hal_mqtt`, `test_hal_ota`, `test_hal_time`, `test_hal_crypto`
- `test_SmartTimers`, `test_pidController`, `test_multicoreWatchdog`, `test_tools`
- `hal_soft_timer_*` oraz `hal_pid_controller_*` to proste adaptery tych
  modułów pomocniczych.

Aby skompilować i uruchomić testy, użyj:

```bash
cmake -S . -B .build/host
cmake --build .build/host
ctest --test-dir .build/host --output-on-failure
```

---

## Mapa przenośnego API

| Obszar | Publiczne API |
|---|---|
| Czas monotoniczny i opóźnienia | `hal_millis()`, `hal_micros()`, `hal_micros64()`, `hal_delay_ms()`, `hal_delay_us()` |
| Stan systemu | `hal_get_free_heap()`, `hal_read_chip_temp()`, `hal_watchdog_*()` |
| GPIO i przerwania | `hal_gpio_set_mode()`, `hal_gpio_write()`, `hal_gpio_read()`, `hal_gpio_attach_interrupt()` |
| ADC i PWM | `hal_adc_*()`, `hal_pwm_*()`, `hal_pwm_freq_*()` |
| Synchronizacja | `hal_mutex_*()`, `hal_critical_section_enter()`, `hal_critical_section_exit()` |
| USB serial i debug | `hal_serial_*()`, `hal_debug_init()`, `hal_deb()`, `hal_derr()`, `hal_derr_limited()` |
| Trwała pamięć bajtowa | `hal_eeprom_*()` |
| Trwała pamięć klucz-wartość | `hal_kv_*()` |
| System plików i logowanie na SD | `hal_littlefs_*()`, `hal_sdlogger_*()` |
| Funkcje pomocnicze matematyczne i bitowe | `hal_constrain()`, `hal_map()`, `hal_min()`, `hal_max()`, `bitSet()`, `bitClear()`, `bitRead()` |
| Wyświetlacze | `hal_display_*()` |

---

*Dalej: [GPIO, ADC i PWM](05_gpio_adc_pwm.md)*
