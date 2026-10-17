# JaszczurHAL

*Dostępne również [po angielsku](README.md).*

Autor: Marcin 'Jaszczur' Kielesinski

JaszczurHAL to warstwa abstrakcji sprzętowej (HAL) i biblioteka narzędziowa dla
systemów wbudowanych opartych na RP2040, RP2350, STM32 i ESP32.
[Przegląd możliwości](doc/pl/features.md) pokazuje, jakie moduły i funkcje są
dostępne. Pełną dokumentację znajdziesz w
[polskim spisie treści](doc/table_of_contents.pl.md).

## Po co to powstało?

No właśnie - przecież jest już Arduino, które w pewnym sensie umożliwia tworzenie
i kompilowanie tego samego kodu, szybko i prosto na wiele platform. Ale nie
wchodząc zbyt głęboko w szczegóły - im bardziej zaawansowane projekty, tym
bardziej dają się we znaki ograniczenia tej - mimo wszystko wartościowej
platformy do nauki. Biblioteki Arduino są też bardzo nierówne - zaczynając od
słabego kodu nadużywajacego `delay()`, po prawdziwe perełki funkcjonalne.
Większość jednak ma podstawowy problem - wielowątkowość. No ale jest przecież
Zephyr - profesjonalny framework, uznany w branży, obsługujący masę sprzętu,
oferujący wielowątkowość jako standard. No ale nie obsługuje on zbyt dobrze mojej
ulubionej rodziny MCU - RP2040/2350. Są też inne platformy i środowiska, z
pewnością... Życia by nie starczyło by sprawdzić je wszystkie. Ale z pewnością ;)
starczy by w pewnym sensie wynaleźć swoje własne koło, i zrobić wszystko "pod
siebie" - z zachowaniem najwyższych standardów jakości (rygorystyczne bramki
testowe, również na sprzęcie), i z wieloma innymi rzeczami, o których możesz
przeczytać dalej, w dokumentacji projektu. :)

Spośród wielu funkcjonalności jakie oferuje JaszczurHAL, warto odnotować
następujące:

- obsługę FreeRTOS (V11.3.0);
- spójne API, które pozwala przenosić kod aplikacji między obsługiwanymi
  platformami;
- implementację testową (mock) do deterministycznych testów na komputerze;
- sterowniki wspólne dla obsługiwanych platform, z zabezpieczeniami do pracy
  wielowątkowej;
- opcjonalne moduły włączane flagami kompilacji `HAL_ENABLE_*`;
- obsługę łączności, zabezpieczeń i pamięci masowej, przydatną m.in.
  w urządzeniach podłączonych do sieci;
- narzędzia do typowych zadań: timery, regulator PID, watchdog i funkcje
  pomocnicze.

Jest oczywistym że nie da się zatrzeć wszystkich różnic między wszystkimi MCU,
i stworzyć w pełni uniwersalnego API. Ale można ukryć zdecydowaną większość
z tych różnic, i stworzyć stabilną i mocną podstawę, dzięki której sporo z tych
różnic traci na znaczeniu. Nawet w czasach AI tłumaczącego kod między
platformami stanowi to niezaprzeczlną wartość - bo tokeny też warto oszczędzać. ;)

Projekt już sprawdza się w praktyce, choć niektóre obszary wciąż wymagają
pracy (WIP). To projekt hobbystyczny - poza jego rozwijaniem trzeba jeszcze
zarabiać na życie i znaleźć czas na samo życie. :)

## Czy to jest gdzieś faktycznie używane?

Tak - w kilku moich bardziej wymagających projektach.

Dobrym przykładem jest
[Fiesta](https://github.com/jaszczurtd/Fiesta), mój prywatny projekt modernizacji
samochodu. Składa się z kilku ściśle zintegrowanych modułów. Najbardziej
wymagający jest chyba moduł ECU: korzysta z JaszczurHAL na dwóch rdzeniach,
steruje pompą wtryskową VP37, komunikuje się z resztą systemu przez CAN
i obsługuje diagnostykę OBD oraz inne funkcje niskopoziomowe.

Są też mniejsze, ale nietrywialne projekty:

- [doomConsole](https://github.com/jaszczurtd/doomConsole) - port gry Doom
  z dźwiękiem, wyświetlaczem TFT i obsługą gamepada Bluetooth 8BitDo;
- [Ford-Mondeo-MK-DPF-Tracker](https://github.com/jaszczurtd/Ford-Mondeo-MK-DPF-Tracker)- urządzenie śledzące cykle regeneracji filtra DPF;
- [lights-timer](https://github.com/jaszczurtd/lights-timer) - sterowanie
  oświetleniem akwarium za pomocą aplikacji na Androida.

## Szybki start

Aby poznać [API HAL](doc/pl/JaszczurHAL_API.md) i zobaczyć, jak ten sam kod
działa na różnych platformach, zacznij od
[gotowych przykładów](examples/README.pl.md).

Aby rozpocząć własny projekt w VS Code, użyj generatora:

```bash
libraries/JaszczurHAL/vscode/tools/create-vscode-example.py \
  --output your-example-project-name
```

Domyślnie projekt jest skonfigurowany dla `rp2040/pico`. Platformę i płytkę
możesz zmienić opcjami `--target` i `--board` albo zadaniem
`Project: Select board` w VS Code. Projekt zawiera gotowe zadania do
kompilacji, wgrywania firmware i monitorowania portu szeregowego.

Opcje generatora, pierwsze wgranie firmware na nową płytkę oraz opis wszystkich
zadań znajdziesz w [instrukcji VS Code](vscode/README.pl.md).

## Przykłady

[Przewodnik po przykładach](examples/README.pl.md) zawiera polecenia kompilacji,
montaże i ograniczenia platform. Aplikacje używają `app_start()`, `app_task0()`
i opcjonalnie `app_task1()`; sposób ich wykonywania na każdej platformie
opisuje [API aplikacji](src/hal/core/hal_app.h).

<a id="obsługiwane-targety-i-moduły-szybki-przegląd"></a>

## Obsługiwane platformy

RP2040 i RP2350 korzystają z Pico SDK, STM32G474 z portu bare metal
w repozytorium, a ESP32-S3 z ESP-IDF. Obsługa klasycznego ESP32 jest
eksperymentalna. Backend mock uruchamia deterministyczne testy na komputerze.
Moduły i ich ograniczenia opisuje [przegląd funkcji](doc/pl/features.md).

<a id="wybór-modułów-skrótowo"></a>

## Włączanie modułów

Włącz potrzebne moduły w `hal_project_config.h` aplikacji:

```c
#pragma once
#define HAL_ENABLE_WIFI
#define HAL_ENABLE_TIME
#define HAL_ENABLE_GPS
```

Konfiguracja sprzętu musi obsługiwać wybrane funkcje. Błędne flagi, parametry
lub zależności zatrzymują kompilację. Szczegóły opisują
[flagi modułów](doc/api/pl/02_module_flags.md) i
[wykaz parametrów kompilacji](doc/HAL_FLAGS.txt).

<a id="przykład-wyboru-targetu-dla-wielu-platform"></a>

## Wybór platformy i płytki

Narzędzia kompilacji wybierają platformę i płytkę. Projekt z `device_tree.json`
wybiera montaż opisujący płytkę i podłączone urządzenia. W VS Code wybór
otwiera skrót `Ctrl+Shift+Alt+1`.
[Poradnik projektu firmware](doc/pl/FwProjectWorkflow.md) wyjaśnia konfigurację,
a [poradnik płytek](doc/pl/boards_profiles_howto.md) opisuje profile i migrację
z usuniętych profili złożonych.

Przy bezpośrednim użyciu kompilatora zdefiniuj dokładnie jeden selektor
`HAL_TARGET_*`; bez niego HAL wykrywa platformę na podstawie narzędzi.
Dostępne są `HAL_TARGET_RP2040`, `HAL_TARGET_RP2350_ARM`,
`HAL_TARGET_RP2350_RISCV`, `HAL_TARGET_STM32G474`, `HAL_TARGET_ESP32_S3`,
`HAL_TARGET_ESP32` i `HAL_TARGET_MOCK`. Kompilowana jest tylko wybrana
implementacja.

<a id="opcjonalny-freertos-opt-in"></a>

## Obsługa FreeRTOS

Na RP i STM32G474 flaga `HAL_ENABLE_FREERTOS` włącza ustaloną wersję jądra
FreeRTOS; RP obsługuje SMP, a STM32 korzysta z portu Cortex-M4F. Na rodzinie
ESP32 jądro dostarcza i uruchamia ESP-IDF. Aplikacje używają standardowego
API FreeRTOS. Przydział zadań, uruchamianie i zasady współbieżności opisuje
[poradnik wielordzeniowości i FreeRTOS](doc/api/pl/04_multicore_drivers_migration.md).

<a id="bezpieczeństwo-wielowątkowe-przegląd"></a>

## Praca wielowątkowa i wielordzeniowa

Moduły sprzętowe obsługują współbieżne wywołania w zakresie opisanym w API.
Twórz, inicjalizuj i zwalniaj zasoby na rdzeniu, do którego należą. W
[dokumentacji modułu](doc/pl/JaszczurHAL_API.md) sprawdź blokady, ograniczenia
ISR i wyjątki. Testy mock są jednowątkowe; opcjonalne testy FreeRTOS POSIX
sprawdzają zachowanie schedulera na komputerze.

<a id="kompilacja-jako-biblioteka-statyczna-a"></a>

## Kompilacja biblioteki statycznej (.a)

[Poradnik kompilacji](doc/pl/lib_compilation.md) opisuje budowanie dla
poszczególnych platform i instalację. Pakiety RP i STM32 zawierają
`libJaszczurHAL.a`, pasujące wygenerowane nagłówki, metadane i odwołanie do
sygnatury linkowania. Zachowuj te pliki razem. Kompilacja z użyciem gotowego
pakietu nie wymaga Pythona; nadal potrzebne są SDK platformy, kod startowy
i konfiguracja linkera.

<a id="testy-i-bramki-jakości"></a>

## Testy i kontrole jakości

Zainstaluj zależności przez `./runmefirst.sh` na Linuksie albo
`./runmefirst.ps1` w PowerShell na Windows. Dostępne sposoby kompilacji
i debugowania w Windows opisuje [poradnik konfiguracji](doc/pl/windows_setup.md).

Pełna bramka jakości działa na Linuksie:

```bash
./runalltests.sh
```

Obejmuje testy hostowe, sanitizery, Valgrind, analizę statyczną, kontrole
dokumentacji i kompilację dla platform docelowych. CI Linuksa używa tych
samych etapów poza kompilacją przykładów wykonywaną tylko lokalnie;
Windows ma osobne kontrole natywne. Przed pushem `./runalltests.sh --commit`
sprawdza czysty checkout ostatniego commita. Testy sprzętowe wymagają osobnej,
jawnie wybranej procedury. Wymagania i zakres opisuje
[poradnik kompilacji i testów](doc/api/pl/03_build_tests.md).

<a id="bezpieczeństwo-i-sbom"></a>

## Bezpieczeństwo zależności i narzędzi

Zgłaszanie podatności opisuje [SECURITY.md](SECURITY.md), a kontrole zależności
i utrzymanie SBOM - [poradnik bezpieczeństwa](doc/pl/security_supply_chain.md).
[Wykaz komponentów](security/third_party.json) zapisuje zarządzane zależności
i wskazuje dla każdej z nich plik `*_version.conf`, z którego pochodzą wersja
i commit; [sbom.cdx.json](security/sbom.cdx.json) jest generowany z danych projektu.

<a id="środowisko-programistyczne-vs-code"></a>

## Praca w VS Code

[Poradnik VS Code](vscode/README.pl.md) opisuje tworzenie projektu, kompilację,
wgrywanie, monitor portu, IntelliSense i skróty klawiszowe.
[Poradnik firmware](doc/pl/FwProjectWorkflow.md#dodawanie-plików-źródłowych-projektu)
wyjaśnia dodawanie źródeł. Otwarcie repozytorium HAL udostępnia
[zadania biblioteki statycznej](doc/pl/lib_compilation.md#workspace-repozytorium-i-vs-code).
Aktualizacje przez sieć i ich ograniczenia bezpieczeństwa opisuje
[poradnik OTA](doc/pl/OTAWorkflow.md).

## Debugowanie w VS Code

Generowane profile Cortex-Debug obsługują RP2040/RP2350 Arm przez Debug Probe
lub Picoprobe oraz STM32G474 przez ST-Link Nucleo. Uruchomienie profilu
kompiluje i wgrywa obraz ELF Debug. Zobacz
[konfigurację i połączenia](doc/pl/windows_setup.md). ESP32-S3 nie ma
generowanego profilu debugowania; ograniczenia kompilacji, wgrywania i OTA
opisują poradniki platform.

<a id="zarządzane-zależności"></a>

## Wersje zależności i aktualizacje

Wersje zależności wskazują pliki `third_party/*_version.conf`.
`./third_party/update_components.sh` instaluje te wersje, a `--verify-only`
sprawdza je bez aktualizacji. [Poradnik komponentów](third_party/README.pl.md)
opisuje aktualizacje, wykaz zależności i SBOM, kompilację oraz wymagane kontrole.

## Dokumentacja

Zacznij od [spisu dokumentacji](doc/table_of_contents.pl.md),
[przeglądu funkcji](doc/pl/features.md) albo [API](doc/pl/JaszczurHAL_API.md).
[Poradnik skryptów](doc/api/pl/00_scripts.md) opisuje narzędzia repozytorium.

## Czy ten projekt powstaje z użyciem AI?

To istotne pytanie, zwłaszcza w dzisiejszych czasach. I tak, w tym projekcie korzystam z AI.
Do czego konkretnie?

- Dokumentacja, to oczywiste. Bez AI nie przygotowałbym samodzielnie tak szczegółowej dokumentacji, w tym komentarzy dla Doxygena.
- Buildsystem: zdecydowaną większość buildsystemu opartego na Pythonie napisało AI - to obszar, którego osobiście nie lubię i który z ulgą powierzam agentom.
- Testy jednostkowe: pokrywające główne ścieżki i scenariusze użycia. Sporo jest takich które
pokrywają realne problemy i regresje, jakie napotkałem podczas pracy z projektem.
- Refaktoryzacja: AI wykonało sporo pracy przy refaktoryzacji źródeł oraz dostosowywaniu mojego starego, lub obcego kodu Arduino do wymagań API JaszczurHAL, między innymi w zakresie obsługi wielowątkowości, stabilności, itp, itd.
- Około 60% kodu C/C++ to mój kod pisany z palca, nie liczę kodu API będącego szkieletem HALa, który powstał już z użyciem AI.
- Konfiguracja CI, skrypty zarządzające sanitizerami, fuzzingiem, Valgrindem, analizą statyczną.

To w dalszym ciągu jest tytaniczna praca nad projektem o takiej skali. Jeśli ktoś uważa że takie coś można stworzyć zwykłym promptowaniem, bez wiedzy i bez patrzenia w kod - życzę mu powodzenia, i wszystkiego dobrego. :)
A już ile się nawalczyłem z AI by dokumentację w ogóle dało się czytać, to moje. ;)

## Uwagi i podziękowania

- Moduł SmartTimers powstał na podstawie
  [Nettigo Timers](https://github.com/nettigo/Timers), forka
  [garthoff/Timers](https://github.com/garthoff/Timers).
- Testy korzystają z forka Unity utrzymywanego przez projekt. Używaną wersję
  wskazuje pin Unity w [komponencie jhbase](third_party/jhbase_version.conf).
- Współdzielona obsługa wyświetlaczy (`src/hal/display/drivers/`) została
  zaimplementowana od zera jako przenośny kod korzystający z HAL. Moduł
  graficzny GFX (`jh_gfx.*`) wykorzystuje dostosowane algorytmy rysowania z
  [Adafruit GFX Library](https://github.com/adafruit/Adafruit-GFX-Library).
  Sterowniki `ili9341_driver.*`, `st77xx_driver.*` i `ssd1306_driver.*`
  wykorzystują dostosowane sekwencje poleceń z bibliotek Adafruit ILI9341,
  ST7735-ST7789 i SSD1306 autorstwa Limor Fried (Ladyada) dla Adafruit
  Industries (BSD-2-Clause). Obsługę protokołów i maszyny stanów
  wyświetlaczy e-papierowych SSD16xx oraz UC81xx oparto na logice
  sterowników Zephyr (Apache-2.0-Clause). Informacje o pochodzeniu kodu
  i autorstwie poszczególnych modułów znajdują się w nagłówkach plików.
- Dołączone, przeniesione na obsługiwane platformy lub dostosowane lokalnie
  komponenty zewnętrzne:
  [plik wersji cJSON](third_party/cjson_version.conf),
  [plik wersji LodePNG](third_party/lodepng_version.conf),
  [plik wersji TJpgDec](third_party/jpeg_version.conf),
  [plik wersji FatFs](third_party/fatfs_version.conf),
  [plik wersji Unity przez jhbase](third_party/jhbase_version.conf),
  [plik wersji FreeRTOS-Kernel](third_party/freertos_core_version.conf),
  [plik wersji BearSSL](third_party/bearssl_version.conf),
  [plik wersji lwIP](third_party/lwip_version.conf),
  [plik wersji littlefs](third_party/littlefs_version.conf),
  [wersja sterownika Semtech SX126x](third_party/sx126x_driver_version.conf),
  [PubSubClient](src/hal/network/mqtt/PubSubClient/),
  [współdzielona implementacja WireGuard/lwIP](src/hal/network/wireguard/core/),
  [LiquidCrystal / HD44780](src/hal/display/hd44780/),
  [Brian Varren DACless](src/hal/audio/dacless/),
  [Seeed/Loovee MCP_CAN / MCP2515](src/hal/can/mcp2515/),
  [MCP251XFD](src/hal/can/mcp251xfd/),
  [Adafruit NeoPixel](src/hal/gpio/neopixel/),
  [Adafruit STMPE610](src/hal/input/stmpe610/),
  [Adafruit TSC2007](src/hal/input/tsc2007/),
  [Paul Stoffregen OneWire](src/hal/onewire/),
  [Bonezegei DHT11/DHT22 by Bonezegei (Jofel Batutay)](src/hal/temperature/dht/),
  [Adafruit MAX6675](src/hal/temperature/max6675/),
  [Adafruit MCP9600](src/hal/temperature/mcp9600/),
  [ArtronShop BH1750](src/hal/sensors/bh1750/),
  [Eric Ayars / JeeLabs / RTClib-style DS3231](src/hal/rtc/ds3231/),
  [IRsmallDecoder / autorstwo dekodera RC5](src/hal/input/irsmall_decoder/).
