# Bezpieczeństwo wielordzeniowe, drivery i logowanie

*Dostępne również [po angielsku](../en/04_multicore_drivers_migration.md).*

> **Część [dokumentacji referencyjnej API JaszczurHAL](../../pl/JaszczurHAL_API.md)**

## Zasady bezpieczeństwa wielordzeniowego

JaszczurHAL obsługuje dwurdzeniowe systemy RP2040/RP2350 oraz ESP32-S3,
wykorzystując, tam gdzie to możliwe, zarówno rdzeń 0, jak i rdzeń 1. Obsługiwany
jest również STM32G474, a ogólna ochrona mutexami jest dostępna poprzez ścieżkę
z włączonym FreeRTOS.

Obowiązują następujące zasady projektowe:

### Przenośny punkt wejścia aplikacji

Gdy włączona jest flaga `HAL_PROVIDE_APP_ENTRY`, funkcję `main()` przejmuje na
własność natywny runtime. Funkcja `app_start()` uruchamiana jest jednokrotnie
przed rozpoczęciem dispatchu zadań. Na RP w wersji bare-metal
`app_task0()` działa jako pętla nadrzędna (super-loop) rdzenia 0; flaga
`HAL_ENABLE_APP_TASK1` uruchamia rdzeń 1 poprzez `multicore_launch_core1()`
jeszcze przed `app_start()`. Bootstrap rdzenia 1 dołącza do koordynatora
transakcji flash i czeka - dispatch aplikacji rozpoczyna się dopiero,
gdy rdzeń 0 zakończy `app_start()`. Dzięki temu inicjalizacja EEPROM/KV we
flashu jest bezpieczna podczas startu i nie eksponuje częściowo
zainicjalizowanego stanu aplikacji dla `app_task1()`. W RP FreeRTOS SMP te same
haki stają się zadaniami przypiętymi do rdzeni 0 i 1. Na STM32G474
dispatch bare-metal jest kooperacyjny w jednej pętli nadrzędnej,
natomiast FreeRTOS tworzy niezależne zadania task0 oraz opcjonalne task1.
ESP-IDF ma już własny scheduler; HAL domyślnie tworzy task0 na rdzeniu 0 oraz
opcjonalne task1 na rdzeniu 1, z możliwością jawnego wskazania rdzenia lub
wartości `-1` oznaczającej brak przypisania (affinity).

Koordynator serializuje natywne modyfikacje flasha, zabezpiecza drugi rdzeń,
wstrzymuje TinyUSB, odrzuca aktywne DMA oraz operacje wykonywane z XIP,
maskuje lokalne przerwania i przywraca przejęty stan runtime na każdej ścieżce
sprzątania (cleanup). W firmware bare-metal wykorzystuje pomocnik multicore z
Pico SDK, a pod FreeRTOS SMP - jego odpowiednik świadomy schedulera. Natywne
zatwierdzenia (commit) EEPROM oraz wszystkie callbacki program/erase LittleFS
korzystają wyłącznie z tej wspólnej ścieżki transakcyjnej.

### Inicjalizacja: tylko jeden rdzeń

Wszystkie funkcje `*_init()`, `*_create()` oraz `*_deinit()` / `*_destroy()`
muszą być wywoływane **tylko z jednego rdzenia** (zazwyczaj z rdzenia 0, w
trakcie `app_start()`). Funkcje te alokują pamięć ze statycznych pul,
konfigurują peryferia sprzętowe i ustanawiają stan wewnętrzny. **Nie** są
chronione mutexami, ponieważ:

- alokacja z puli z natury odbywa się jednorazowo (podczas rozruchu),
- konfiguracja peryferiów sprzętowych musi zakończyć się przed ich użyciem,
- dodanie narzutu mutexów do ścieżek inicjalizacyjnych nie daje żadnej
  praktycznej korzyści, o ile respektowana jest udokumentowana gwarancja.

### Czas działania: współbieżne backendy sprzętowe

Po inicjalizacji większość API runtime HAL obsługuje współbieżnych
wywołujących w firmware dwurdzeniowym RP2040/RP2350 oraz w obsługiwanych
buildach FreeRTOS, w tym na STM32G474 i w dostarczonym zestawie backendów
ESP32-S3. Dokładna gwarancja thread safety każdego modułu opisana
jest w poświęconej mu sekcji poniżej. Ogólny wzorzec jest następujący:

- **Mutexy na instancję** chronią API oparte na uchwytach (`hal_can`, `hal_thermocouple`, `hal_rtc`, `SmartTimers`).
- **Mutexy na magistralę** chronią współdzielone magistrale komunikacyjne (`hal_spi`, `hal_i2c`).
- **Mutexy singletonowe** chronią moduły globalne (`hal_eeprom`, `hal_display`, `hal_gps`, `hal_external_adc`, `hal_wifi`, `hal_udp`, `hal_wireguard`, `hal_mqtt`, `hal_kv`, debug serial).
- **Bezstanowe funkcje pomocnicze** (`hal_bits`, `hal_math`, czyste funkcje
  pomocnicze `hal_time`, `hal_crypto`, `hal_constrain`, `hal_map`) są z natury
  thread-safe.

Mutexy singletonowe i mutexy na magistralę wykorzystują wewnętrzny atomowy
mechanizm typu create-once, dzięki czemu dwa zadania FreeRTOS lub dwa rdzenie
sprzętowe nie mogą opublikować różnych blokad dla tego samego modułu. Mimo to
wywołania init/begin modułu pozostają preferowanym miejscem do tworzenia tych
blokad, zanim rozpocznie się zwykłe współdzielenie w runtime.

Moduły oznaczone jako **„not thread-safe" (Not thread-safe)**
(`hal_uart`, opcjonalne API NTP/czasu systemowego w `hal_time`,
`pidController`) muszą być serializowane przez wywołującego lub używane tylko
z jednego rdzenia.

### Backend mock

Implementacje mock (`impl/.mock/`) są przeznaczone do deterministycznych,
jednowątkowych testów jednostkowych i nie zapewniają synchronizacji między
wątkami równoważnej sprzętowej. Opcjonalny test runtime FreeRTOS POSIX osobno
sprawdza na hoście integrację schedulera, mutexów, opóźnień (delay) i
mechanizmu create-once.

---

## Drivery i frameworki

Dołączone lub przeniesione (ported) drivery niskopoziomowe znajdują się w
`src/hal/impl/rp2040/drivers/` lub w odpowiednim katalogu tematycznym pod
`src/hal/`. Dołączone wysokopoziomowe frameworki integracyjne znajdują się w
odpowiednim katalogu tematycznym pod `src/hal/`. Wsparcie specyficzne dla
danego targetu pozostaje pod `src/hal/impl/`. Te źródła są wewnętrznymi
szczegółami implementacyjnymi HAL (nie są częścią publicznego API).

### Zestawienie, autorzy i ścieżki licencji

| Katalog drivera | Zastosowanie w HAL | Autor(zy) źródłowi | Licencja | Ścieżka licencji w repozytorium |
|---|---|---|---|---|
| Silnik GFX (przeniesiony) | Renderowanie `hal_display` (geometria, tekst, mapy bitowe) przeniesione do `hal/display/drivers/jh_gfx.*` | Limor Fried (Ladyada) i współtwórcy (Adafruit GFX) | BSD-2-Clause (adnotacja autorska w nagłówkach źródłowych; biblioteka nie jest już dołączana/linkowana) | `src/hal/display/drivers/jh_gfx.h` |
| Driver ILI9341 (przeniesiony) | Backend TFT (`HAL_DISPLAY_ILI9341`) przeniesiony do `hal/display/drivers/ili9341_driver.*` | Limor Fried (Ladyada) (Adafruit ILI9341) | BSD-2-Clause (adnotacja autorska w nagłówkach źródłowych) | `src/hal/display/drivers/ili9341_driver.h` |
| Driver ST77xx/GC9A01 (przeniesiony) | Backendy ST7735/ST7789/ST7796S wraz ze wsparciem dla okrągłego TFT GC9A01 opartym na Zephyr, przeniesione do `hal/display/drivers/st77xx_driver.*` | Limor Fried (Ladyada) (Adafruit ST7735/ST7789), driver Zephyr GC9x01x jako lista kontrolna referencyjna dla GC9A01 | BSD-2-Clause dla oryginalnej ścieżki ST77xx; notatki dot. przeniesienia zachowania GC9A01 odwołują się do źródeł Zephyr na licencji Apache-2.0 | `src/hal/display/drivers/st77xx_driver.h` |
| Driver z rodziny SSD1306 (przeniesiony) | Backend OLED (`HAL_ENABLE_SSD1306`) przeniesiony do `hal/display/drivers/ssd1306_driver.*` i rozszerzony o warianty SSD1309/SSD1315/SH1106/CH1115 | Limor Fried (Ladyada) i współtwórcy (Adafruit SSD1306), zachowanie drivera wyświetlacza Zephyr jako lista kontrolna referencyjna | BSD-2-Clause (adnotacja autorska w nagłówkach źródłowych) | `src/hal/display/drivers/ssd1306_driver.h` |
| Drivery RGB OLED SSD1331/SSD135x (przeniesione) | Współdzielone drivery OLED RGB565 poprzez HAL SPI/GPIO (`HAL_ENABLE_SSD1331`, `HAL_ENABLE_SSD135X`) | Zachowanie `display_ssd1331.c` i `display_ssd135x.c` z Zephyr jako lokalna lista kontrolna referencyjna | Zachowanie referencyjne Apache-2.0, zaimplementowane w drzewie repozytorium względem transportu HAL | `src/hal/display/drivers/rgb_oled_driver.h` |
| Driver LCD ST7567 (przeniesiony) | Współdzielony monochromatyczny driver LCD poprzez HAL I2C lub SPI/GPIO (`HAL_ENABLE_ST7567`) | Zachowanie `display_st7567.c` i `display_st7567_regs.h` z Zephyr jako lokalna lista kontrolna referencyjna | Zachowanie referencyjne Apache-2.0, zaimplementowane w drzewie repozytorium względem transportu HAL | `src/hal/display/drivers/st7567_driver.h` |
| Drivery EPD SSD16xx/UC81xx (przeniesione) | Współdzielone monochromatyczne drivery e-papieru poprzez HAL SPI/GPIO (`HAL_ENABLE_SSD16XX`, `HAL_ENABLE_UC81XX`) | `ssd16xx.c`, `ssd16xx_regs.h`, `uc81xx.c` i `uc81xx_regs.h` z Zephyr jako lokalne odniesienie protokołu/maszyny stanów | Apache-2.0, dostosowane do transportu HAL opartego na statusach (status-first) i publicznych konfiguracji | `src/hal/display/drivers/ssd16xx_driver.h`, `src/hal/display/drivers/uc81xx_driver.h` |
| Rdzeń NeoPixel (przeniesiony) | `hal_rgb_led` | Phil „Paint Your Dragon" Burgess i współtwórcy (Adafruit_NeoPixel) | LGPL (adnotacja autorska w nagłówkach źródłowych) | `src/hal/gpio/neopixel/COPYING`, `src/hal/gpio/neopixel/jh_neopixel.h` |
| `DS3231` | Backend RTC DS3231 (`hal_rtc`) | Eric Ayars, Andrew Wickert, Jean-Claude Wippler, współtwórcy Northern Widget | Deklaracje domeny publicznej w nagłówkach źródłowych | `src/hal/rtc/ds3231/ds3231.h`, `src/hal/rtc/ds3231/ds3231.cpp` |
| Driver DHT11/DHT22 (przeniesiony) | `hal_dht` | Bonezegei (Jofel Batutay) | Adnotacja autorska w nagłówku źródłowym | `src/hal/temperature/dht/hal_dht.cpp` |
| `MCP2515` | Backend `hal_can` | Seeed Technology (Loovee), Cory J. Fowler | LGPL (dołączony plik `license.txt`) | `src/hal/can/mcp2515/license.txt` i `src/hal/can/mcp2515/mcp2515_driver.h` |
| Współdzielony silnik WireGuard/lwIP | Backend `hal_wireguard` | Kenta Ida (oryginalne API), Daniel Hope (rdzeń), Marcin Kielesiński (porty RP2040/Pico W i współdzielony HAL) | BSD-3-Clause | `src/hal/network/wireguard/core/LICENSE` |
| `PubSubClient` | Backend `hal_mqtt` | Nick O'Leary | MIT | `src/hal/network/mqtt/PubSubClient/LICENSE.txt` |
| TinyGPS++ (przeniesiony) | Logika parsowania NMEA dla `hal_gps` przeniesiona do `gps_nmea_parser` | Mikal Hart | LGPL-2.1+ (adnotacja autorska w nagłówkach źródłowych; biblioteka nie jest już dołączana/linkowana) | `src/hal/gps/gps_nmea_parser.cpp` |

Uwaga: `hal/display/drivers/Fonts/` zawiera dodatkowe informacje licencyjne dla
poszczególnych czcionek w nagłówkach czcionek (np. `TomThumb.h`,
`Tiny3x3a2pt7b.h`).

### Zmiany integracyjne i ich uzasadnienie

| Obszar | Co się zmieniło | Dlaczego |
|---|---|---|
| Okablowanie include'ów | Moduły HAL dołączają wbudowane zależności z lokalnych ścieżek `drivers/` i `frameworks/`; include'y wewnątrzmodułowe zostały przepięte na lokalne ścieżki względne. | Utrzymuje kod firm trzecich zamknięty wewnątrz wnętrza HAL i zapobiega wyciekom globalnej przestrzeni nazw include. |
| Build warunkowy | Pliki `.cpp` driverów są opakowane strażnikami `HAL_ENABLE_*` na poziomie modułu. | Moduły domyślnie wyłączone usuwają z buildu zarówno wrappery HAL, jak i kod backendu firm trzecich. |
| Synchronizacja SPI | Drivery wykonujące transakcje SPI integrują tam, gdzie to potrzebne, `hal_spi_lock`/`hal_spi_unlock` (CAN, współdzielone drivery paneli TFT). | Zapobiega przeplataniu się transakcji SPI między wątkami/rdzeniami. |
| Synchronizacja I2C | Drivery wykonujące ruch I2C integrują tam, gdzie to potrzebne, `hal_i2c_lock_bus`/`hal_i2c_unlock_bus` oraz mapowanie magistrali. | Zapobiega mieszaniu transakcji magistrali 0/1 i poprawia determinizm przy współbieżności. |
| Mutexy per driver | Wybrane drivery/wrappery posiadają teraz własne mutexy dla operacji wieloetapowych (`MCP2515`, `MAX6675`, `MCP9600`, wrappery HAL). | Zmniejsza ryzyko race condition w sekwencjach odczyt/modyfikacja/zapis oraz w wieloetapowych sekwencjach poleceń. |
| Współdzielona fasada RTC | `hal_rtc.cpp` odpowiada za pulę uchwytów, walidację, mutexy, konwersję epoki, mapowanie statusów i wrappery zgodności; providerzy dowiązywani na etapie linkowania (link-time) udostępniają wspólne zachowanie I2C PCF8563/DS3231, RTC domeny backup STM32G474, timer AON RP lub pamięć mock. | Utrzymuje protokół układu, rejestry targetu i wstrzykiwanie testowe za operacjami providera, a providerom I2C i natywnym pozwala współdzielić jedną fasadę. |
| Osobne API niskiego poboru mocy | `hal_power.h` odpowiada za przenośne stany, polityki, capabilities, przyczyny wybudzenia oraz callbacki prepare/resume; backendy targetu odpowiadają za szczegóły WFI/STOP/Standby i ponownie wykorzystują wewnętrzny interfejs względnego wybudzenia RTC. | Zapobiega temu, by własność urządzenia RTC przejmowała politykę procesora, drzewa zegarów, wstrzymywania peryferiów, resetu czy schedulera. |
| Współdzielona fasada GPS | `hal_gps.cpp` wybiera na etapie buildu HAL UART lub SoftwareSerial i odpowiada za inicjalizację transportu, polling, framing fallback i dostępność. Współdzielony silnik GPS odpowiada za parsowanie, blokady, diagnostykę i wszystkie gettery fixów, łącznie ze ścieżką wstrzykiwania mock. | Eliminuje identyczne fasady transportu RP2040/STM32G474 oraz duplikat getterów mock, zachowując przy tym wybór transportu i deterministyczne wstrzykiwanie. |
| Współdzielony rdzeń serial/debug | `hal_serial.cpp` odpowiada za formatowanie, prefiksy, znaczniki czasu, stan wyciszenia/ograniczania częstotliwości (mute/rate-limit), pierścień ISR SPSC, mirroring na konsolę sieciową, leniwe mutexy create-once oraz publiczne punkty wejścia serial/debug. Porty dowiązywane na etapie linkowania odpowiadają wyłącznie za RP USB CDC, ESP32-S3 USB Serial/JTAG VFS, STM32 USART2/stdout lub transport przechwytywania/RX mock. | Utrzymuje jedną implementację formatowania/stanu, zachowując przy tym docelowe zakończenia linii, zachowanie flush specyficzne dla transportu i atomowe granice komunikatów między zadaniami/rdzeniami. |
| Obsługa drugiego kontrolera I2C | API HAL I2C oraz adaptery driverów używają indeksu magistrali 0/1 dla pierwszego i drugiego sprzętowego kontrolera danego targetu. | Umożliwia korzystanie z drugiego kontrolera bez omijania thread safety HAL. |
| Współdzielony stos wyświetlaczy | Dołączone biblioteki Adafruit GFX/ILI9341/ST77xx/SSD1306/BusIO zostały zastąpione przenośnym stosem wyświetlaczy w drzewie repozytorium (`hal/display/drivers/`), zbudowanym wyłącznie na HAL SPI/I2C/GPIO. Publiczna fasada obejmuje wyświetlacze ILI9341, ST77xx/GC9A01, z rodziny SSD1306, SSD1331/SSD135x oraz ST7567 poprzez GFX i surowe zapisy zgłaszane przez capability. | Jedna współdzielona implementacja steruje identycznie RP2040 i STM32G474 i jest całkowicie usuwana z buildu, gdy moduł wyświetlacza jest wyłączony. |
| Przenośny silnik NMEA | `hal_gps` używa parsera NMEA w drzewie repozytorium (`hal/gps/gps_nmea_parser.cpp`), z logiką parsowania przeniesioną z TinyGPS++ (LGPL); sama biblioteka TinyGPS++ nie jest już dołączana ani linkowana. | Ten sam silnik parsera/getterów działa na RP2040, STM32G474 i mock i jest usuwany z buildu przy wyłączonym module GPS. |
| Transport UDP | `hal_udp` korzysta ze współdzielonego silnika raw lwIP i jest włączany podczas buildu flagą `HAL_ENABLE_UDP`. | Obsługa UDP pozostaje opcjonalna i przy wyłączeniu nie dodaje żadnego rozmiaru kodu. |
| Dołączenie WireGuard | `hal_wireguard` korzysta ze współdzielonego silnika lwIP bramkowanego flagą `HAL_ENABLE_WIREGUARD`; hooki targetu dostarczają netif warstwy spodniej (underlay), kontekst stosu, entropię i czas. | Utrzymuje WireGuard deterministyczny i offline, jednocześnie współdzieląc zachowanie tras, timerów i zamykania (teardown) pomiędzy obsługiwanymi targetami z lwIP na hoście. |
| Dołączenie PubSubClient | `hal_mqtt` korzysta z dołączonych źródeł PubSubClient, bramkowanych flagą `HAL_ENABLE_MQTT` w jednostce translacji drivera. | Obsługa MQTT jest opcjonalna i przy wyłączeniu nie dodaje żadnego rozmiaru kodu. |

---

## Hak znacznika czasu logowania

Moduł serial debug obsługuje opcjonalne poprzedzanie logów błędów znacznikiem
czasu.

API:

- `typedef bool (*hal_debug_timestamp_hook_t)(char *out, size_t out_size, void *user);`
- `void hal_debug_set_timestamp_hook(hal_debug_timestamp_hook_t hook, void *user);`

Zachowanie:

- jeśli hak zwróci `true` i zapisze niepusty tekst, `hal_derr()` /
    `hal_derr_limited()` poprzedzają komunikat `[`znacznik czasu`]` przed
    `ERROR! ...`
- jeśli hak nie jest ustawiony lub zwróci `false`, logowanie zachowuje się
    dokładnie tak jak wcześniej

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
odróżnić te przypadki. Ten sam rdzeń waliduje i konwertuje daty RTC, PCF8563
oraz DS3231 w buildach RP2040, STM32G474 i mock.

Zawsze dostępne API `hal_time` zawiera również dawne algorytmy czasu
z `tools.cpp`:

- `hal_time_is_daylight_saving_time(...)` stosuje regułę „ostatnia niedziela"
  CET/CEST opartą wyłącznie na dacie i odrzuca nieprawidłowe daty gregoriańskie
- `hal_time_adjust_cet_cest(...)` stosuje odpowiednie przesunięcie i
  normalizuje przejście przez granicę doby
- `hal_time_is_in_range(...)` sprawdza półotwarty przedział `[start, end)`
- `hal_time_extract_minutes(...)` rozbija liczbę minut, z opcjonalnymi
  wyjściami

Ugruntowane funkcje pomocnicze `isDaylightSavingTime()`, `adjustTime()`,
`is_time_in_range()` oraz `extract_time()` pozostają wrapperami zgodnymi na
poziomie źródeł i nie zawierają żadnej logiki kalendarza ani przedziałów.

---

## Przykłady

Po przykłady szybkiego startu sięgnij do przykładów w pliku
[README.pl.md](../../../README.pl.md).

Typowe scenariusze tam opisane:

- GPIO wraz z odmierzaniem czasu
- I2C wraz z EEPROM
- WiFi wraz z NTP/czasem systemowym
- WiFi wraz z datagramami UDP
- nieblokujący przepływ request/poll/read dla DS18B20
- inicjalizacja wyświetlacza

Ten plik zawiera niskopoziomową dokumentację referencyjną API oraz mapę
przenośnego API.

---

## Zakres testów hosta

Testy hosta/mock są budowane przez CMake i weryfikują backend mock zwrócony w
stronę desktopu wraz z wybranymi modułami narzędziowymi.

Testy obejmują następujące targety:

- `test_hal_gpio`, `test_hal_adc`, `test_hal_pwm`, `test_hal_spi`,
  `test_hal_timer`, `test_hal_onewire`, `test_hal_ds18b20`, `test_hal_dht`,
  `test_hal_pga2311`
- `test_stm32_hal_timer` weryfikuje rzeczywisty backend timera STM32G474 w
  buildu sterowanego z hosta, łącznie z przeplanowywaniem callbacków i
  zarządzanymi timerami.
- `test_hal_i2c`, `test_hal_i2c_slave`, `test_hal_rgb_led`, `test_hal_external_adc`, `test_ads1x15_driver`, `test_bh1750_driver`, `test_hal_gps`, `test_hal_system`, `test_hal_bits`
- `test_hal_serial`, `test_hal_sc_auth`, `test_hal_serial_session`,
  `test_hal_serial_session_vocabulary`, `test_jh_security_primitives`,
  `test_security_architecture`, `test_serial_architecture`, `test_hal_uart`,
  `test_hal_swserial`; `test_freertos_posix_runtime` dodatkowo miesza
  współbieżne nadajniki serial/debug i weryfikuje kompletność granic
  komunikatów.
- `test_hal_can`, `test_hal_thermocouple`, `test_hal_display`
- `test_hal_eeprom`, `test_hal_kv`, `test_hal_wifi`, `test_hal_littlefs`, `test_hal_sdlogger`, `test_hal_udp`, `test_hal_wireguard`, `test_hal_mqtt`, `test_hal_ota`, `test_hal_time`, `test_hal_crypto`
- `test_SmartTimers`, `test_pidController`, `test_multicoreWatchdog`, `test_tools`
- `hal_soft_timer_*` oraz `hal_pid_controller_*` to cienkie wrappery nad tymi
  rdzeniami narzędziowymi.

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
| Bajty trwałe (persistent) | `hal_eeprom_*()` |
| Trwałość klucz/wartość | `hal_kv_*()` |
| System plików i logowanie na SD | `hal_littlefs_*()`, `hal_sdlogger_*()` |
| Funkcje pomocnicze matematyczne i bitowe | `hal_constrain()`, `hal_map()`, `hal_min()`, `hal_max()`, `bitSet()`, `bitClear()`, `bitRead()` |
| Wyświetlacze | `hal_display_*()` |

---


---

*Dalej: [GPIO, ADC i PWM](05_gpio_adc_pwm.md)*
