# Bezpieczeństwo wielordzeniowe, drivery i logowanie

*Dostępne również [po angielsku](../en/04_multicore_drivers_migration.md).*

> **Część [dokumentacji referencyjnej API JaszczurHAL](../../pl/JaszczurHAL_API.md)**

## Zasady bezpieczeństwa wielordzeniowego

JaszczurHAL obsługuje dwurdzeniowe układy RP2040/RP2350 oraz ESP32-S3 i w miarę
możliwości korzysta z obu rdzeni. Obsługiwany jest również STM32G474, dla którego
ogólna synchronizacja za pomocą mutexów jest dostępna w buildach z FreeRTOS.

Obowiązują następujące zasady projektowe:

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

W buildach RP z FreeRTOS SMP te same funkcje działają jako zadania przypięte do
rdzeni 0 i 1. Na STM32G474 w trybie bare metal oba zadania są wykonywane
kooperacyjnie w jednej pętli głównej, a FreeRTOS uruchamia niezależne `task0` i
opcjonalne `task1`. ESP-IDF ma własny kernel. HAL domyślnie tworzy `task0`
na rdzeniu 0 i opcjonalne `task1` na rdzeniu 1; konfiguracja może wskazać inny
rdzeń albo wartość `-1`, która oznacza brak przypisania.

Koordynator szereguje natywne operacje modyfikujące flash, wprowadza drugi
rdzeń w bezpieczny stan, wstrzymuje TinyUSB, odrzuca operacje przy aktywnym DMA
lub wykonywane z XIP, maskuje lokalne przerwania i zawsze przywraca tymczasowo
zmieniony stan runtime. W firmware bare metal korzysta z funkcji multicore Pico
SDK, a w FreeRTOS SMP z jej odpowiednika współpracującego z planistą. Zapisy
EEPROM oraz wszystkie
callbacki `program`/`erase` LittleFS przechodzą wyłącznie przez ten wspólny
mechanizm transakcji.

### Inicjalizacja: tylko jeden rdzeń

Wszystkie funkcje `*_init()`, `*_create()` oraz `*_deinit()` / `*_destroy()`
muszą być wywoływane **tylko z jednego rdzenia** (zazwyczaj z rdzenia 0, w
trakcie `app_start()`). Funkcje te alokują pamięć ze statycznych pul,
konfigurują peryferia sprzętowe i inicjalizują stan wewnętrzny. **Nie** są
chronione mutexami, ponieważ:

- alokacja z puli z natury odbywa się jednorazowo (podczas rozruchu),
- konfiguracja peryferiów sprzętowych musi zakończyć się przed ich użyciem,
- dodanie narzutu mutexów do ścieżek inicjalizacyjnych nie daje żadnej
  praktycznej korzyści, o ile przestrzegana jest ta udokumentowana zasada.

### Czas działania: współbieżne backendy sprzętowe

Po inicjalizacji większość funkcji HAL używanych podczas działania programu
można wywoływać współbieżnie w dwurdzeniowym firmware RP2040/RP2350 oraz w
obsługiwanych buildach FreeRTOS, w tym na STM32G474 i w dostarczonych backendach
ESP32-S3. Dokładne zasady thread safety opisano osobno dla każdego
modułu. Ogólnie obowiązuje następujący podział:

- **Mutexy na instancję** chronią API oparte na uchwytach (`hal_can`, `hal_thermocouple`, `hal_rtc`, `SmartTimers`).
- **Mutexy na magistralę** chronią współdzielone magistrale komunikacyjne (`hal_spi`, `hal_i2c`).
- **Mutexy singletonowe** chronią moduły globalne (`hal_eeprom`, `hal_display`, `hal_gps`, `hal_external_adc`, `hal_wifi`, `hal_udp`, `hal_wireguard`, `hal_mqtt`, `hal_kv`, debug serial).
- **Funkcje pomocnicze bez stanu** (`hal_bits`, `hal_math`, czyste funkcje
  pomocnicze `hal_time`, `hal_crypto`, `hal_constrain`, `hal_map`) są z natury
  bezpieczne dla wielu wątków.

Mutexy singletonów i magistral korzystają z wewnętrznego, atomowego mechanizmu
`create-once`. Dwa zadania FreeRTOS ani dwa rdzenie nie mogą więc utworzyć
różnych blokad dla tego samego modułu. Blokady najlepiej jednak tworzyć podczas
wywołania `init`/`begin`, zanim moduł zacznie być współdzielony.

Moduły **nieprzystosowane do współbieżnego użycia**
(`hal_uart`, opcjonalne API NTP/czasu systemowego w `hal_time`,
`pidController`) muszą być serializowane przez wywołującego lub używane tylko
z jednego rdzenia.

### Backend mock

Implementacje mock (`impl/.mock/`) służą do deterministycznych,
jednowątkowych testów jednostkowych. Nie zapewniają synchronizacji między
wątkami odpowiadającej backendom sprzętowym. Opcjonalny test runtime FreeRTOS
POSIX osobno sprawdza na hoście współpracę schedulera, mutexów, opóźnień i
mechanizmu `create-once`.

---

## Drivery i frameworki

Dołączone lub przeportowane drivery niskopoziomowe znajdują się w
`src/hal/impl/rp2040/drivers/` lub w odpowiednim katalogu tematycznym pod
`src/hal/`. Dołączone komponenty integracyjne wyższego poziomu znajdują się w
odpowiednim katalogu tematycznym pod `src/hal/`. Wsparcie specyficzne dla
danego targetu znajduje się w `src/hal/impl/`. Są to wewnętrzne szczegóły
implementacji HAL, a nie część publicznego API.

### Zestawienie, autorzy i ścieżki licencji

| Katalog drivera | Zastosowanie w HAL | Autor(zy) źródłowi | Licencja | Ścieżka licencji w repozytorium |
|---|---|---|---|---|
| Silnik GFX (port) | Renderowanie `hal_display` (geometria, tekst, mapy bitowe) umieszczone w `hal/display/drivers/jh_gfx.*` | Limor Fried (Ladyada) i współtwórcy (Adafruit GFX) | BSD-2-Clause (adnotacja autorska w nagłówkach źródłowych; biblioteka nie jest już dołączana/linkowana) | `src/hal/display/drivers/jh_gfx.h` |
| Driver ILI9341 (port) | Backend TFT (`HAL_DISPLAY_ILI9341`) umieszczony w `hal/display/drivers/ili9341_driver.*` | Limor Fried (Ladyada) (Adafruit ILI9341) | BSD-2-Clause (adnotacja autorska w nagłówkach źródłowych) | `src/hal/display/drivers/ili9341_driver.h` |
| Driver ST77xx/GC9A01 (port) | Backendy ST7735/ST7789/ST7796S wraz ze wsparciem dla okrągłego TFT GC9A01 opartym na Zephyr, umieszczone w `hal/display/drivers/st77xx_driver.*` | Limor Fried (Ladyada) (Adafruit ST7735/ST7789), driver Zephyr GC9x01x jako wzorzec zgodności dla GC9A01 | BSD-2-Clause dla oryginalnej ścieżki ST77xx; notatki dot. przeniesienia zachowania GC9A01 odwołują się do źródeł Zephyr na licencji Apache-2.0 | `src/hal/display/drivers/st77xx_driver.h` |
| Driver z rodziny SSD1306 (port) | Backend OLED (`HAL_ENABLE_SSD1306`) umieszczony w `hal/display/drivers/ssd1306_driver.*` i rozszerzony o warianty SSD1309/SSD1315/SH1106/CH1115 | Limor Fried (Ladyada) i współtwórcy (Adafruit SSD1306), zachowanie drivera wyświetlacza oparte o logikę zapożyczoną z projektu Zephyr | BSD-2-Clause (adnotacja autorska w nagłówkach źródłowych) | `src/hal/display/drivers/ssd1306_driver.h` |
| Drivery RGB OLED SSD1331/SSD135x (porty) | Współdzielone drivery OLED RGB565 poprzez HAL SPI/GPIO (`HAL_ENABLE_SSD1331`, `HAL_ENABLE_SSD135X`) | `display_ssd1331.c` i `display_ssd135x.c` z logiką zapożyczoną z projektu Zephyr | Kod referencyjny na licencji Apache-2.0; implementacja w repozytorium została dostosowana do transportu HAL | `src/hal/display/drivers/rgb_oled_driver.h` |
| Driver LCD ST7567 (port) | Współdzielony monochromatyczny driver LCD poprzez HAL I2C lub SPI/GPIO (`HAL_ENABLE_ST7567`) | `display_st7567.c` i `display_st7567_regs.h` z logiką zapożyczoną z projektu Zephyr | Kod referencyjny na licencji Apache-2.0; implementacja w repozytorium została dostosowana do transportu HAL | `src/hal/display/drivers/st7567_driver.h` |
| Drivery EPD SSD16xx/UC81xx (porty) | Współdzielone monochromatyczne drivery e-papieru poprzez HAL SPI/GPIO (`HAL_ENABLE_SSD16XX`, `HAL_ENABLE_UC81XX`) | `ssd16xx.c`, `ssd16xx_regs.h`, `uc81xx.c` i `uc81xx_regs.h` Kod driverów Zephyr stanowi inspirację dla protokołu/maszyny stanów | Apache-2.0; kod dostosowany do publicznej konfiguracji i transportu HAL, w którym podstawowym wynikiem jest wartość `hal_status_t` | `src/hal/display/drivers/ssd16xx_driver.h`, `src/hal/display/drivers/uc81xx_driver.h` |
| Rdzeń NeoPixel (port) | `hal_rgb_led` | Phil „Paint Your Dragon" Burgess i współtwórcy (Adafruit_NeoPixel) | LGPL (adnotacja autorska w nagłówkach źródłowych) | `src/hal/gpio/neopixel/COPYING`, `src/hal/gpio/neopixel/jh_neopixel.h` |
| `DS3231` | Backend RTC DS3231 (`hal_rtc`) | Eric Ayars, Andrew Wickert, Jean-Claude Wippler, współtwórcy Northern Widget | Deklaracje domeny publicznej w nagłówkach źródłowych | `src/hal/rtc/ds3231/ds3231.h`, `src/hal/rtc/ds3231/ds3231.cpp` |
| Driver DHT11/DHT22 (port) | `hal_dht` | Bonezegei (Jofel Batutay) | Adnotacja autorska w nagłówku źródłowym | `src/hal/temperature/dht/hal_dht.cpp` |
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
| Build warunkowy | Pliki `.cpp` driverów są opakowane strażnikami `HAL_ENABLE_*` na poziomie modułu. | Po wyłączeniu modułu do buildu nie trafiają ani adaptery HAL, ani kod backendu firm trzecich. |
| Synchronizacja SPI | Drivery wykonujące transakcje SPI wywołują w razie potrzeby `hal_spi_lock`/`hal_spi_unlock` (CAN, współdzielone drivery paneli TFT). | Zapobiega przeplataniu się transakcji SPI między wątkami/rdzeniami. |
| Synchronizacja I2C | Drivery wykonujące transakcje I2C wywołują w razie potrzeby `hal_i2c_lock_bus`/`hal_i2c_unlock_bus` i odwzorowują numery magistral. | Zapobiega mieszaniu transakcji magistrali 0/1 i zapewnia przewidywalne działanie przy współbieżności. |
| Mutexy poszczególnych driverów | Wybrane drivery i adaptery mają własne mutexy dla operacji wieloetapowych (`MCP2515`, `MAX6675`, `MCP9600`, adaptery HAL). | Ogranicza to ryzyko wyścigów w sekwencjach odczyt/modyfikacja/zapis i poleceniach wymagających kilku wywołań. |
| Współdzielona fasada RTC | `hal_rtc.cpp` zarządza pulą uchwytów, walidacją, mutexami, konwersją czasu Unix, odwzorowaniem statusów i adapterami zgodności. Providery dołączane na etapie linkowania implementują obsługę układów I2C PCF8563/DS3231, RTC w domenie podtrzymania STM32G474, timera AON RP albo pamięci mock. | Protokół układu, rejestry targetu i funkcje testowe pozostają za interfejsem providera, a implementacje I2C i natywne korzystają z jednej fasady. |
| Osobne API niskiego poboru mocy | `hal_power.h` definiuje przenośne stany, zasady działania, obsługiwane funkcje, przyczyny wybudzenia i callbacki `prepare`/`resume`. Backendy targetów implementują szczegóły WFI/STOP/Standby i korzystają z wewnętrznego interfejsu względnego wybudzenia RTC. | Dzięki temu moduł RTC nie przejmuje zasad dotyczących procesora, drzewa zegarów, wstrzymywania peryferiów, resetu ani planisty. |
| Współdzielona fasada GPS | `hal_gps.cpp` wybiera podczas buildu HAL UART albo SoftwareSerial i zarządza inicjalizacją transportu, odpytywaniem, zapasowym mechanizmem składania ramek i dostępnością. Wspólny mechanizm GPS zajmuje się parsowaniem, blokadami, diagnostyką i wszystkimi funkcjami odczytującymi pozycję, w tym funkcjami mock do ustawiania danych testowych. | Usuwa to powielone fasady transportu RP2040/STM32G474 oraz kopię funkcji odczytowych mock, a jednocześnie zachowuje wybór transportu i deterministyczne dane testowe. |
| Współdzielony rdzeń serial/debug | `hal_serial.cpp` odpowiada za formatowanie, prefiksy, znaczniki czasu, wyciszanie i ograniczanie częstotliwości komunikatów, pierścień ISR SPSC, przekazywanie komunikatów do konsoli sieciowej, leniwe tworzenie mutexów metodą `create-once` oraz publiczne punkty wejścia serial/debug. Porty dowiązywane na etapie linkowania odpowiadają wyłącznie za RP USB CDC, ESP32-S3 USB Serial/JTAG VFS, STM32 USART2/stdout albo przechwytywanie wyjścia i wstrzykiwanie danych RX w mocku. | Zapewnia jedną implementację formatowania i stanu, zachowując zakończenia linii właściwe dla targetu, zależne od transportu działanie funkcji `flush` i atomowe granice komunikatów między zadaniami/rdzeniami. |
| Obsługa drugiego kontrolera I2C | API HAL I2C oraz adaptery driverów używają indeksu magistrali 0/1 dla pierwszego i drugiego sprzętowego kontrolera danego targetu. | Pozwala to korzystać z drugiego kontrolera bez pomijania synchronizacji zapewnianej przez HAL. |
| Współdzielony stos wyświetlaczy | Dołączone biblioteki Adafruit GFX/ILI9341/ST77xx/SSD1306/BusIO zostały zastąpione przenośnym stosem wyświetlaczy umieszczonym w repozytorium (`hal/display/drivers/`) i opartym wyłącznie na HAL SPI/I2C/GPIO. Publiczna fasada obsługuje wyświetlacze ILI9341, ST77xx/GC9A01, z rodziny SSD1306, SSD1331/SSD135x oraz ST7567 przez GFX i bezpośredni zapis, jeśli driver deklaruje taką możliwość. | Ta sama implementacja steruje wyświetlaczami na RP2040 i STM32G474, a po wyłączeniu modułu nie trafia do buildu. |
| Przenośny silnik NMEA | `hal_gps` używa parsera NMEA w drzewie repozytorium (`hal/gps/gps_nmea_parser.cpp`), z logiką parsowania przeniesioną z TinyGPS++ (LGPL); sama biblioteka TinyGPS++ nie jest już dołączana ani linkowana. | Ten sam silnik parsera/getterów działa na RP2040, STM32G474 i mock i jest usuwany z buildu przy wyłączonym module GPS. |
| Transport UDP | `hal_udp` korzysta ze współdzielonego silnika raw lwIP i jest włączany podczas buildu flagą `HAL_ENABLE_UDP`. | Obsługa UDP pozostaje opcjonalna i po wyłączeniu nie zwiększa rozmiaru kodu. |
| Dołączenie WireGuard | `hal_wireguard` korzysta ze wspólnej implementacji lwIP włączanej flagą `HAL_ENABLE_WIREGUARD`; funkcje targetu dostarczają bazowy `netif`, kontekst stosu, entropię i czas. | WireGuard działa deterministycznie i bez dostępu do sieci podczas testów, a obsługa tras, timerów i zamykania jest wspólna dla obsługiwanych targetów z lwIP. |
| Dołączenie PubSubClient | `hal_mqtt` korzysta z dołączonych źródeł PubSubClient, bramkowanych flagą `HAL_ENABLE_MQTT` w jednostce translacji drivera. | Obsługa MQTT jest opcjonalna i po wyłączeniu nie zwiększa rozmiaru kodu. |

---

## Hak znacznika czasu logowania

Moduł serial debug obsługuje opcjonalne poprzedzanie logów błędów znacznikiem
czasu.

API:

- `typedef bool (*hal_debug_timestamp_hook_t)(char *out, size_t out_size, void *user);`
- `void hal_debug_set_timestamp_hook(hal_debug_timestamp_hook_t hook, void *user);`

Zachowanie:

- jeśli hook zwróci `true` i zapisze niepusty tekst, `hal_derr()` /
  `hal_derr_limited()` dodają prefiks `` `[znacznik czasu]` `` przed
  `ERROR! ...`;
- jeśli hook nie jest ustawiony lub zwróci `false`, logowanie zachowuje się
  dokładnie tak jak wcześniej.

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

## Funkcja pomocnicza konwersji czasu

`hal_time_from_components(int year, int month, int day, int hour, int minute, int second)`
konwertuje składowe daty/czasu na sekundy epoki Unix.

Walidacja:

- zwraca `0` dla wartości spoza zakresu (rok < 1970, nieprawidłowy
  miesiąc/dzień/czas lub epoka Unix przekraczająca `UINT32_MAX`)
- obsługuje reguły lat przestępnych (łącznie z wyjątkami stuletnimi)

API zgodności zwraca `0` zarówno w przypadku błędu, jak i dla prawidłowego
początku epoki Unix. Wewnętrznie współdzielony rdzeń `hal/time/jh_calendar`
używa `hal_status_t` oraz 64-bitowej epoki, dzięki czemu wywołujący może
odróżnić te przypadki. Ten sam kod sprawdza i przelicza daty RTC, PCF8563
oraz DS3231 w buildach RP2040, STM32G474 i mock.

Zawsze dostępne API `hal_time` zawiera również wcześniejsze funkcje czasu
z `tools.cpp`:

- `hal_time_is_daylight_saving_time(...)` stosuje regułę „ostatnia niedziela"
  CET/CEST opartą wyłącznie na dacie i odrzuca nieprawidłowe daty gregoriańskie
- `hal_time_adjust_cet_cest(...)` stosuje odpowiednie przesunięcie i
  normalizuje przejście przez granicę doby
- `hal_time_is_in_range(...)` sprawdza półotwarty przedział `[start, end)`
- `hal_time_extract_minutes(...)` rozbija liczbę minut, z opcjonalnymi
  wyjściami

Dotychczasowe funkcje pomocnicze `isDaylightSavingTime()`, `adjustTime()`,
`is_time_in_range()` oraz `extract_time()` pozostają zgodnymi źródłowo
adapterami i nie zawierają własnej logiki kalendarza ani przedziałów.

---

## Przykłady

Po przykłady szybkiego startu sięgnij do przykładów w folderze
[examples](../../../examples).

Typowe scenariusze tam opisane:

- GPIO wraz z odmierzaniem czasu
- I2C wraz z EEPROM
- WiFi wraz z NTP/czasem systemowym
- WiFi wraz z datagramami UDP
- nieblokujący cykl żądania, odpytywania i odczytu dla DS18B20
- inicjalizacja wyświetlacza

Ten plik zawiera niskopoziomową dokumentację referencyjną API oraz mapę
przenośnego API.

---

## Zakres testów hosta

Testy hostowe korzystające z backendu mock są budowane przez CMake. Sprawdzają
ten backend na komputerze oraz wybrane moduły pomocnicze.

Testy obejmują następujące cele testowe:

- `test_hal_gpio`, `test_hal_adc`, `test_hal_pwm`, `test_hal_spi`,
  `test_hal_timer`, `test_hal_onewire`, `test_hal_ds18b20`, `test_hal_dht`,
  `test_hal_pga2311`
- `test_stm32_hal_timer` weryfikuje rzeczywisty backend timera STM32G474 w
  buildzie sterowanym z hosta, łącznie z przeplanowywaniem callbacków i
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

Punkt wejścia do buildu i uruchamiania:

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
