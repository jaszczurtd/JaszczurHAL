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
- [Ford-Mondeo-MK-DPF-Tracker](https://github.com/jaszczurtd/Ford-Mondeo-MK-DPF-Tracker)
  - urządzenie śledzące cykle regeneracji filtra DPF;
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

Katalog `examples/` zawiera aplikacje pokazujące, jak używać powiązanych
modułów HAL w jednym programie. Każdy przykład zawiera przenośny plik
`app.c` lub `app.cpp` oraz konfigurację w `hal_project_config.h`.

Wszystkie przykłady korzystają z tych samych funkcji aplikacji:
`app_start()`, `app_task0()` i opcjonalnej `app_task1()`, włączanej flagą
`HAL_ENABLE_APP_TASK1`. Sposób ich uruchamiania zależy od platformy:

- Na RP `app_task1()` działa na drugim rdzeniu.
- Na STM32G474 bez systemu operacyjnego (bare-metal) `app_task1()` jest
  wywoływana kooperacyjnie, a nie na osobnym rdzeniu.
- Na ESP32-S3 funkcje aplikacji działają pod kontrolą schedulera FreeRTOS
  uruchomionego przez ESP-IDF. Domyślnie zadania 0 i 1 są przypisane
  odpowiednio do rdzeni 0 i 1; to przypisanie można zmienić.

Wymagania, zestawienia kompilacji i listę platform obsługiwanych przez każdy
przykład znajdziesz w [opisie przykładów](examples/README.pl.md).
Opisano tam również zasadę rozwijania istniejącego projektu lub jego wariantu,
zanim powstanie kolejny katalog z przykładem.

<a id="obsługiwane-targety-i-moduły-szybki-przegląd"></a>

## Obsługiwane platformy

RP2040 i RP2350 korzystają z oficjalnego Pico SDK. Dla STM32G474 repozytorium
dostarcza implementację działającą bez systemu operacyjnego oraz konfigurację
linkowania. ESP32-S3 korzysta z ESP-IDF.

Na RP i STM32G474 można opcjonalnie włączyć FreeRTOS. Na ESP32-S3 jest on
wymagany przez ESP-IDF. Implementacja mock służy do deterministycznych testów
na komputerze, bez urządzenia docelowego.

Dostępne moduły i funkcje opisuje
[przegląd możliwości](doc/pl/features.md).

<a id="wybór-modułów-skrótowo"></a>

## Włączanie modułów

Domyślnie moduły opcjonalne nie są kompilowane. Włącz tylko te, których
potrzebuje aplikacja, definiując odpowiednie flagi `HAL_ENABLE_*` w pliku
`hal_project_config.h` swojego projektu:

```c
#pragma once
#define HAL_ENABLE_WIFI
#define HAL_ENABLE_TIME
#define HAL_ENABLE_GPS
```

Mechanizmy sprawdzające konfigurację wykrywają nieprawidłowe lub nieobsługiwane
flagi oraz błędne wartości parametrów.

Dostępne flagi, ich parametry i zasady włączania zależności opisano w:

- [dokumentacji API](doc/pl/JaszczurHAL_API.md);
- [rozdziale o flagach modułów](doc/api/pl/02_module_flags.md);
- [zestawieniu flag kompilacji](doc/HAL_FLAGS.txt).

<a id="przykład-wyboru-targetu-dla-wielu-platform"></a>

## Wybór platformy i płytki

Niezależnie od wyboru modułów JaszczurHAL wybiera jedną implementację dla
platformy docelowej. Odpowiada za to plik `src/hal/core/hal_target.h`.
Zdefiniuj **dokładnie jedną** z poniższych flag w `hal_project_config.h`
lub przekaż ją kompilatorowi przez `-D`. Poniższy blok przedstawia dostępne
opcje, a nie gotową konfigurację:

```c
#define HAL_TARGET_RP2040        // RP2040, Cortex-M0+
#define HAL_TARGET_RP2350_ARM    // RP2350, Cortex-M33
#define HAL_TARGET_RP2350_RISCV  // RP2350, Hazard3 RISC-V
#define HAL_TARGET_STM32G474     // STM32G474
#define HAL_TARGET_ESP32_S3      // ESP32-S3, natywny ESP-IDF
#define HAL_TARGET_MOCK          // deterministyczna implementacja testowa dla hosta
```

Jeżeli nie zdefiniujesz żadnej flagi, platforma zostanie **wykryta
automatycznie** na podstawie używanego zestawu narzędzi kompilacyjnych.
Kompilowane są tylko pliki implementacji dla wybranej platformy - nieużywane
warianty nie zwiększają rozmiaru programu.

Standardowe skrypty kompilacji korzystają z generowanego rejestru płytek
oraz stałych identyfikatorów platform i płytek. Zasady wyboru opisano
w [przewodniku po profilach platform i płytek](doc/pl/boards_profiles_howto.md).
Pełne powiązania między platformą, płytką i konfiguracją wyjaśnia
[instrukcja pracy z projektem firmware](doc/pl/FwProjectWorkflow.md).

W codziennej pracy w VS Code nie musisz znać szczegółów tego mechanizmu.
Naciśnij `Ctrl+Shift+Alt+1` i wybierz platformę z menu.
Pozostałe skróty znajdziesz w
[zestawieniu skrótów klawiszowych](vscode/README.pl.md#skróty-klawiszowe-vs-code).

<a id="opcjonalny-freertos-opt-in"></a>

## Obsługa FreeRTOS

Na RP i STM32G474 włącz FreeRTOS za pomocą flagi kompilacji:

```c
#define HAL_ENABLE_FREERTOS
```

Na ESP32-S3 FreeRTOS jest wymagany przez ESP-IDF. Konfiguracja tej platformy
dodaje `HAL_ENABLE_FREERTOS` jako wymaganą flagę.

Aplikacje korzystają bezpośrednio ze standardowych nagłówków i API FreeRTOS
na wszystkich obsługiwanych platformach. JaszczurHAL zajmuje się uruchamianiem
schedulera tam, gdzie jest to potrzebne, oraz opcjonalnym przypisaniem zadań
aplikacji do rdzeni.

RP korzysta z FreeRTOS-Kernel w wersji ustalonej w repozytorium, z obsługą SMP
(pracy na wielu rdzeniach). STM32G474 używa tego samego jądra z portem
Cortex-M4F. ESP32-S3 korzysta z FreeRTOS dostarczonego przez ustaloną wersję
ESP-IDF i obsługuje opcjonalne drugie zadanie aplikacji.

Wersje jądra, używane porty i warianty kompilacji opisano w
[przewodniku po kompilacji biblioteki](doc/pl/lib_compilation.md) oraz
[rozdziale o pracy wielordzeniowej i FreeRTOS](doc/api/pl/04_multicore_drivers_migration.md).

<a id="bezpieczeństwo-wielowątkowe-przegląd"></a>

## Praca wielowątkowa i wielordzeniowa

JaszczurHAL jest projektowany z myślą o pracy wielu wątków i rdzeni.
Inicjalizację i zwalnianie zasobów (`init` / `create` / `destroy` / `deinit`)
wykonuj jednak na tym samym rdzeniu, na którym zostały stworzone.
Muteksy wspólnych instancji modułów i poszczególnych magistral są tworzone atomowo przy
pierwszym użyciu, z zabezpieczeniem przed wyścigiem podczas ich tworzenia.

Implementacja mock służy do deterministycznych testów jednowątkowych.
Opcjonalna flaga `JH_ENABLE_FREERTOS_POSIX_TESTS` rozszerza je o testy
schedulera FreeRTOS uruchamiane na komputerze.

Dokładne zasady współbieżnego dostępu, sygnatury funkcji, zachowanie modułów,
różnice między implementacjami i zakres testów opisuje
[dokumentacja API](doc/pl/JaszczurHAL_API.md).

<a id="kompilacja-jako-biblioteka-statyczna-a"></a>

## Kompilacja biblioteki statycznej (.a)

JaszczurHAL można skompilować jako bibliotekę statyczną `libJaszczurHAL.a`.
[Przewodnik po kompilacji](doc/pl/lib_compilation.md) opisuje jej budowanie,
kompilację przykładowych aplikacji oraz podział na kod biblioteki i kod
uruchamiający aplikację.

Zainstalowane pakiety dla RP i STM32G474 zawierają wygenerowane nagłówki
z konfiguracją funkcji i płytek, metadane wybranej płytki oraz plik źródłowy
potrzebny do sprawdzenia zgodności podczas linkowania. Dzięki temu można
korzystać z biblioteki bezpośrednio z kompilatora. Kompilacja i linkowanie
już zainstalowanego pakietu nie wymagają Pythona.

<a id="testy-i-bramki-jakości"></a>

## Testy i kontrole jakości

Aby uruchomić automatyczne testy i kontrole repozytorium, wykonaj:

```bash
./runalltests.sh
```

Skrypt uruchamia testy jednostkowe na komputerze, w tym testy FreeRTOS POSIX,
kontrole Clang ASan/UBSan/libFuzzer i Valgrind memcheck oraz analizę statyczną.
Sprawdza również duplikaty kodu i dokumentację, a także kompiluje bibliotekę
i firmware w zestawie konfiguracji dla obsługiwanych platform.

Ten skrypt wymaga już zainstalowanego zestawu narzędzi i kompilatorów (standardowe
paczki, dostępne zarówno dla linuxa jak i windows które można pobrać też samodzielnie).
JaszczurHAL posiada już gotowy skrypt, który pobiera i instaluje wszystkie niezbędne
zależności, w wersji zarówno dla windows jak i dla linuxa:

```bash
./runmefirst.sh
```

windows:

```bash
runmefirst.ps1
```

Testy na rzeczywistym sprzęcie wykonuje się osobno, zgodnie z instrukcjami
dla poszczególnych stanowisk.

Wymagania, konfigurację, organizację i zasady rozszerzania testów, procedury
sprzętowe oraz zapisane wyniki znajdziesz w
[rozdziale o kompilacji i testach](doc/api/pl/03_build_tests.md).
Działanie skryptów uruchamiających testy i kontrole opisuje
[rozdział o skryptach repozytorium](doc/api/pl/00_scripts.md).

<a id="bezpieczeństwo-i-sbom"></a>

## Bezpieczeństwo zależności i narzędzi

Projekt prowadzi wykaz używanych komponentów zewnętrznych i ich ustalonych
wersji. Zawiera też instrukcje zgłaszania podatności, sprawdzania zależności
i tworzenia SBOM, czyli zestawienia składników oprogramowania:

- [Zgłaszanie podatności](SECURITY.md) - zasady zgłaszania i oceny podatności
  oraz utrzymywania projektu.
- [Bezpieczeństwo zależności i narzędzi](doc/pl/security_supply_chain.md) -
  tworzenie SBOM, sprawdzanie podatności i zasady działania `security-scan`
  w CI.
- [Wykaz komponentów zewnętrznych](security/third_party.json) - lista
  aktualizowana ręcznie.
- [SBOM w formacie CycloneDX](security/sbom.cdx.json) - zestawienie generowane
  na podstawie danych projektu.

<a id="środowisko-programistyczne-vs-code"></a>

## Praca w VS Code

Narzędzia w katalogu `vscode/` pozwalają kompilować i wgrywać firmware oraz
korzystać z monitora portu szeregowego w VS Code. Projekty wywołują je przez
stały interfejs poleceń:

```text
libraries/JaszczurHAL/vscode/entry/jh-vscode
libraries/JaszczurHAL/vscode/entry/jh-vscode.cmd
```

Narzędzie odczytuje konfigurację projektu i wybiera aktywną platformę oraz
płytkę. Przy kompilacji projektów CMake dobiera sposób budowania do wybranej
platformy. Przed wgraniem przez port szeregowy sprawdza tożsamość urządzenia.
Na RP2040 obsługuje także tryb BOOTSEL i pliki UF2. Programowanie STM32
odbywa się przez OpenOCD, a kompilacja i wgrywanie dla ESP32-S3 - przez
skrypt korzystający z ESP-IDF.

Monitor portu szeregowego pozostaje aktywny do zatrzymania. IntelliSense jest
odświeżany na podstawie bazy poleceń kompilacji z używanego zestawu narzędzi.

Szczegółowe instrukcje:

- [Konfiguracja VS Code](vscode/README.pl.md) - polecenia CLI, nazwy zadań,
  skróty klawiszowe i generator projektów.
- [Praca z projektem firmware](doc/pl/FwProjectWorkflow.md) - organizacja
  i konfiguracja projektu, w tym
  [dodawanie plików źródłowych](doc/pl/FwProjectWorkflow.md#dodawanie-plików-źródłowych-projektu).
- [Aktualizacje OTA](doc/pl/OTAWorkflow.md) - aktualizacje przez sieć na RP
  i ESP32-S3, pierwsze wgranie oraz zakres i ograniczenia zabezpieczeń.
- [Pełna lista skrótów klawiszowych](vscode/README.pl.md#skróty-klawiszowe-vs-code).

Po otwarciu katalogu głównego repozytorium JaszczurHAL w VS Code konfiguracja
`.vscode/` udostępnia osobne zadania do pracy z biblioteką statyczną. Globalne
skróty pozwalają ją kompilować i instalować, usuwać pliki kompilacji oraz
odświeżać IntelliSense dla profilu platformy i płytki wybranego ze wspólnego
rejestru. Pliki wynikowe trafiają do `.build/vscode/library/`.
Szczegóły opisano w
[przewodniku po kompilacji biblioteki](doc/pl/lib_compilation.md#workspace-repozytorium-i-vs-code).

## Debugowanie w VS Code

Generowane profile Cortex-Debug pozwalają debugować RP2040 i RP2350 Arm przez
SWD. Potrzebna jest sonda Raspberry Pi Debug Probe albo Pico z firmware
Debug Probe/Picoprobe. Dla STM32G474 używany jest ST-Link wbudowany w płytkę
NUCLEO-G474RE.

Uruchomienie profilu w widoku Run and Debug kompiluje i wczytuje plik ELF
w konfiguracji Debug. Narzędzia projektu dobierają OpenOCD i GDB z obsługą
Arm w Windows i Linuksie. Sposób podłączenia sondy i konfigurację opisuje
[instrukcja przygotowania środowiska Windows](doc/pl/windows_setup.md).

Praca nad firmware w VS Code jest dostępna zarówno w Linuksie, jak
i bezpośrednio w Windows. Zakres kompilacji, wgrywania, monitorowania,
aktualizacji OTA i debugowania zależy od wybranej platformy RP lub STM
i jest opisany w dokumentacji.

Dla ESP32-S3 dostępne są kompilacja, wgrywanie przez port szeregowy, monitor,
IntelliSense i aktualizacja OTA z użyciem pliku BIN aplikacji. Nie ma jednak
jeszcze profilu debugowania przygotowanego przez narzędzia projektu.
Weryfikacja sprzętowa OTA (faza 3.5) również nie jest jeszcze zakończona; jej
zakres opisuje [dokumentacja OTA](doc/pl/OTAWorkflow.md).

Pełny zestaw testów i kontroli repozytorium działa w Linuksie. Obejmuje
Valgrind, analizę statyczną oraz testy integracyjne na komputerze wymagające
POSIX. Konfigurację, sposób sprawdzenia środowiska i funkcje dostępne
wyłącznie w Linuksie opisuje
[instrukcja przygotowania środowiska Windows](doc/pl/windows_setup.md).

<a id="zarządzane-zależności"></a>

## Wersje zależności i aktualizacje

Pliki `third_party/*_version.conf` określają używane wersje Pico SDK, ESP-IDF,
picotool, PMD CPD, zestawu narzędzi RISC-V dla RP2350, FreeRTOS, BearSSL,
cJSON, LodePNG, TJpgDec, FatFs, Unity, lwIP, littlefs, BTstack i sterownika
Semtech SX126x.

Do aktualizacji lub sprawdzenia komponentów służą polecenia:

```bash
./third_party/update_components.sh
./third_party/update_components.sh --verify-only
```

Pierwsze polecenie aktualizuje komponenty zgodnie z wersjami ustalonymi
w repozytorium. Drugie sprawdza je bez aktualizacji.

Zasady zarządzania zależnościami opisuje
[instrukcja komponentów zewnętrznych](third_party/README.pl.md).
Zawiera również listę czynności przy aktualizacji: uzupełnienie wykazu
komponentów i SBOM, sprawdzenie kompilacji, na które wpływa zmiana,
oraz uruchomienie pełnego zestawu testów i kontroli.

## Dokumentacja

Najważniejsze punkty odniesienia:

- [Pełny spis dokumentacji](doc/table_of_contents.pl.md).
- [Przegląd możliwości](doc/pl/features.md).
- [Skrypty obsługi repozytorium](doc/api/pl/00_scripts.md).
- [Dokumentacja API](doc/pl/JaszczurHAL_API.md).
- [Praca z projektem firmware](doc/pl/FwProjectWorkflow.md).
- [Aktualizacje OTA](doc/pl/OTAWorkflow.md).
- [Profile platform i płytek](doc/pl/boards_profiles_howto.md).
- [Zestawienie flag kompilacji](doc/HAL_FLAGS.txt).
- [Kompilacja biblioteki statycznej](doc/pl/lib_compilation.md).
- [Praca z firmware w VS Code](vscode/README.pl.md).

## Uwagi i podziękowania

- Moduł SmartTimers powstał na podstawie
  [Nettigo Timers](https://github.com/nettigo/Timers), forka
  [garthoff/Timers](https://github.com/garthoff/Timers).
- Testy korzystają z forka Unity utrzymywanego przez projekt. Używaną wersję
  wskazuje [plik wersji Unity](third_party/unity_version.conf).
- Współdzielona obsługa wyświetlaczy (`src/hal/display/drivers/`) została
  zaimplementowana na nowo jako przenośny kod korzystający z HAL. Moduł
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
  [plik wersji Unity](third_party/unity_version.conf),
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
