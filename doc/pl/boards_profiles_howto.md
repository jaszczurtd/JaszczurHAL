<a id="profile-targetów-i-płytek"></a>

# Profile platform i płytek

*Dostępne również [po angielsku](../en/boards_profiles_howto.md).*

JaszczurHAL wybiera platformę i płytkę za pomocą dwóch stałych identyfikatorów:

```json
{
  "target": "rp2040",
  "board": "rp2040-zero"
}
```

Platforma docelowa (target) określa mikrokontroler, architekturę zestawu instrukcji (ISA), zestaw narzędzi i sposób kompilacji. Profil płytki opisuje konkretny sprzęt: pamięć flash, wyprowadzone i zarezerwowane piny, wbudowane urządzenia, możliwości sprzętowe oraz komponenty dobierane podczas kompilacji. Funkcje aplikacji nadal trzeba włączać przez `HAL_ENABLE_*`. Sama obecność sprzętu nie włącza jego obsługi.

Dostępne profile są zapisane w `boards/profiles/*.json`. Ich identyfikatory wyświetla polecenie `python3 scripts/generate_board_config.py --boards-root boards --list boards`. ESP32-S3 udostępnia zaimplementowane moduły podstawowe i peryferia oraz funkcje sieciowe i usługi określone w dokumentacji jako faza 3.

Przed załadowaniem narzędzi kompilacji generator sprawdza zgodność platformy, rozmiar pamięci flash, piny, komponenty i zależności funkcji. Z tych samych deskryptorów tworzy również konfigurację zastępczą do bezpośredniego wyboru płytki w kodzie. Nazwy płytek i ustawienia kompilacji pozostają dzięki temu jednakowe także bez konfiguracji wygenerowanej dla konkretnej kompilacji.

## Pliki źródłowe

Źródłowe dane przechowywane w systemie kontroli wersji znajdują się w `boards/`:

- `targets/<id>.json` opisuje target MCU/ISA;
- `profiles/<id>.json` opisuje fizyczną płytkę;
- `capabilities.json` przypisuje stałe bity cech sprzętowych;
- `board.schema.json` zawiera wyłącznie informacje pomocnicze dla edytora;
- `scripts/generate_board_config.py` odpowiada za sprawdzanie poprawności
  strukturalnej i semantycznej.

Identyfikatory deskryptorów muszą mieć zapis kebab-case i odpowiadać nazwom plików. Błędem są nieznane pola, powtórzone identyfikatory, niezgodne pary platforma-płytka, nieprawidłowe punkty połączeń, nieznane cechy lub komponenty oraz zapis wyników poza `.build`.

## Model deskryptora

Każdy deskryptor zawiera `schemaVersion`, `kind`, `id`, `displayName`,
`description` i `status`.

Deskryptor platformy określa ponadto:

- `architecture`: producenta, rodzinę, SoC, ISA, liczbę rdzeni, publiczne
  nazwy MCU/podtypu/CPU, obecność FPU oraz nazwę backendu runtime;
- `hal.targetSelector`;
- `build`: identyfikator systemu budowania (`provider`), jego ściśle określoną
  recepturę oraz platformę lub `idfTarget`, gdy są wymagane;
- `gpio`: format identyfikatora pinu, pełną listę prawidłowych pinów, opcjonalne
  cechy pinów oraz kodowanie HAL;
- `memory.regions` oraz `memory.ramUsableBytes`; całkowity rozmiar RAM jest
  obliczany ze wszystkich regionów RAM, natomiast użyteczna pamięć RAM opisuje
  obszar standardowo udostępniany przez domyślny skrypt linkera aplikacji;
- `defaultBoard`;
- opcjonalny `sourceFallbackBoard`, używany tylko wtedy, gdy płytkę można
  bezpiecznie wybrać w kodzie źródłowym bez udziału generatora kompilacji;
- identyfikatory komponentów definiowanych przez target;
- opcjonalny `requiredFeatures`, dodawany do wynikowego zestawu przed
  obliczeniem jego skrótu i wartości `featureHash`;
- opcjonalny `supportedFeatures`, czyli zamkniętą listę funkcji dozwolonych
  dla danego targetu, sprawdzaną przez skrypty kompilacji produkcyjnych po
  rozwiązaniu zależności przechodnich. Lista ta musi zawierać wszystkie
  funkcje wymagane.

Wygenerowany `jh_board_config.h` zapisuje dane platformy w makrach `HAL_TARGET_*`, a dane płytki w `HAL_BOARD_*`. Korzysta z nich `hal_system_get_current_architecture()`, więc implementacje nie muszą utrzymywać osobnej tabeli mikrokontrolerów, ISA i pamięci. Całkowity rozmiar flash pozostaje właściwością płytki: płytki z tym samym mikrokontrolerem mogą mieć różne układy pamięci.

Deskryptory płytki dodatkowo definiują:

- `compatibleTargets` i `build.provider`;
- identyfikator płytki przekazywany systemowi budowania, gdy jest wymagany;
- stabilny `hal.profileId`, selektor, aliasy zgodności oraz opcjonalne
  selektory, za pomocą których system budowania automatycznie wykrywa płytkę;
  nazwą używaną podczas działania jest zawsze `id` płytki;
- źródło informacji o fizycznej pamięci flash i jej oczekiwany rozmiar, a
  także fizycznie zamontowaną pamięć PSRAM, jeśli występuje;
- opcjonalny transport `programming`, stały USB VID/PID programatora oraz
  mechanizm resetowania i przechodzenia w tryb rozruchowy, służący do
  bezpiecznego wyboru urządzenia po stronie hosta;
- wyprowadzone piny, grupy złączy, rezerwacje i aliasy;
- cechy sprzętowe, urządzenia zdefiniowane przez płytkę, domyślne ustawienia
  peryferiów oraz komponenty.

W profilu Waveshare ESP32-S3-Zero wbudowany programator USB Serial/JTAG jest
opisany następująco:

```json
"programming": {
  "transport": "usb-serial-jtag",
  "usb": { "vid": 12346, "pid": 4097 },
  "reset": "usb-serial-jtag-control-lines",
  "boot": "usb-serial-jtag-control-lines"
}
```

Dziesiętne wartości USB to `303a:1001` w zwykłym zapisie szesnastkowym.
Na podstawie tych danych `jh-vscode` tworzy mechanizm sprawdzania tożsamości
urządzenia; nie trzeba powielać ich w manifestach. Końcowe testy sprzętowe
fazy 1 potwierdziły tożsamość interfejsu programowania, trzy pełne operacje
wgrania kompletu trzech obrazów, wykrywanie ESP32-S3 i dwóch rdzeni, 4 MiB
fizycznej pamięci flash, zainicjalizowane 2 MiB pamięci Quad PSRAM oraz
ponowne połączenie monitora szeregowego na płytce SKU 25081. W fazie 2 dodano
wygenerowane maski dostępności i rezerwacji GPIO, używane przez backendy GPIO,
ADC, UART, I2C i SPI dla ESP32-S3. Na fizycznym stanowisku testowym sprawdzono
następnie oba rdzenie aplikacji, GPIO/IRQ, ADC, pętlę zwrotną UART, tryb master
I2C i SPI, GPTimer, odbiór i nadawanie przez USB Serial/JTAG oraz działanie
systemu i synchronizacji. Dlatego target i profil płytki mają status
`supported`.

Każdy punkt połączenia GPIO ma jawnie wskazaną domenę:

```json
{ "domain": "soc-gpio", "id": 16 }
```

Piny STM32 mają identyfikatory symboliczne, np. `PA5`. Linie GPIO udostępniane przez inny układ są oznaczane jako `component-gpio`, dzięki czemu nie rozszerzają przestrzeni nazw GPIO samego SoC.

Rezerwacja `hard` wyklucza użycie pinu przez aplikację. Rezerwacja `soft` oznacza funkcję przypisaną płytce, z możliwością świadomego sterowania pinem przez aplikację. Deskryptor płytki nie określa połączeń aplikacji, układu partycji, tożsamości produktu USB nadawanej przez firmware, wyboru zegara, sekretów ani kolejności kolorów WS2812. Stały identyfikator USB interfejsu programowania jest natomiast cechą sprzętu i należy do `programming.usb`.

Profil złożony z płytki bazowej i dodatkowego modułu musi zachować fizyczne urządzenia płytki, aliasy i publiczne definicje HAL. Nie usuwaj np. `HAL_LED_BUILTIN` tylko po to, aby przeznaczyć jego pin dla modułu: dioda nadal jest elektrycznie podłączona i może obciążać lub przełączać wspólną linię. Wybierz połączenia bez konfliktów. Modyfikacja PCB, np. rozwarcie mostka lutowniczego, wymaga osobnego profilu z jednoznacznym opisem przeróbki.

## Urządzenia zdefiniowane w profilu płytki

Każdy wpis w sekcji `devices` ma identyfikator w formacie camelCase i określa
`kind`. Urządzenia korzystające z jednej linii - `gpio`, `component-gpio` i
`addressable` - mają pojedynczy `endpoint`.

Urządzenie używające kilku sygnałów magistrali ma `kind: "bus-device"` i pole `role` z rejestru ról generatora. Rola określa wymagane sygnały i typy atrybutów, co zapobiega zapisaniu niepełnego opisu. Poniższy skrócony przykład pokazuje nazewnictwo. Pełnym wzorcem konfiguracji SX1262 jest profil `rp2040-lora-lf` w repozytorium:

```json
"loraRadio": {
  "kind": "bus-device",
  "role": "sx1262-radio",
  "bus": { "kind": "spi", "index": 1 },
  "signals": {
    "sck": { "domain": "soc-gpio", "id": 14 },
    "cs": { "domain": "soc-gpio", "id": 13 },
    "busy": { "domain": "soc-gpio", "id": 18 },
    "dio1": { "domain": "soc-gpio", "id": 16 }
  },
  "attributes": {
    "maxSpiClockHz": 16000000,
    "regulator": "dcdc",
    "rfSwitchMode": "dio2"
  }
}
```

Generator sprawdza, czy każda rola występuje w profilu najwyżej raz, sygnały urządzenia nie współdzielą pinów, każdy sygnał `soc-gpio` ma rezerwację `hard`, a wartości liczbowe mieszczą się w zakresach typów i spełniają wymagane relacje kolejności. Wymagane lub zabronione sygnały i atrybuty mogą zależeć od wartości pola wyliczeniowego.

Dlatego `rfSwitchMode: "dio2"` wyklucza linie GPIO i poziomy logiczne do sterowania przełącznikiem. `rfSwitchMode: "dio2-single-gpio"` oznacza sterowanie przełącznikiem RF przez DIO2 układu SX1262 oraz jedną zewnętrzną linię sterującą torem radiowym.

Każda rola generuje w `jh_board_config.h` stały zestaw makr z własnym
prefiksem, a także `HAL_BOARD_DEVICE_PIN_NONE` dla brakujących sygnałów
opcjonalnych:

```c
#define HAL_BOARD_LORA_RADIO_PRESENT 1
#define HAL_BOARD_LORA_RADIO_SPI_BUS 1u
#define HAL_BOARD_LORA_RADIO_PIN_CS 13u
#define HAL_BOARD_LORA_RADIO_PIN_RF_SWITCH_A HAL_BOARD_DEVICE_PIN_NONE
#define HAL_BOARD_LORA_RADIO_MAX_SPI_CLOCK_HZ UINT32_C(16000000)
#define HAL_BOARD_LORA_RADIO_REGULATOR_IS_DCDC 1
```

Płytki bez danego urządzenia nadal definiują `<PREFIX>_PRESENT 0`, dzięki
czemu moduł HAL może na etapie kompilacji ustalić konfigurację pochodzącą z
profilu płytki. Dla atrybutów wyliczeniowych generowana jest jedna flaga
`_IS_<VALUE>` dla każdej dozwolonej wartości oraz łańcuch znaków `_NAME`; symboliczne
piny STM32 są kodowane jako te same całkowitoliczbowe identyfikatory pinów,
których używa HAL. Pełny deskryptor trafia również bez zmian do
`jh_board_resolved.json`, gdzie jest dostępny dla narzędzi.

Identyfikatory komponentów, obsługujące je systemy kompilacji i grupy wzajemnie wykluczających się komponentów są zdefiniowane w `config/tooling/board_components.json`. Generator płytki odczytuje ten plik i tworzy dane CMake dołączane przez `cmake/jh_board_components.cmake`.

Każda oficjalna konfiguracja sprawdza wynikową listę komponentów. Nieznany komponent, niezgodność z systemem kompilacji albo dwa komponenty z tej samej grupy wykluczającej kończą konfigurację błędem. Skrypty kompilacji mogą dobierać źródła na podstawie flag `JH_BOARD_COMPONENT_<ID>`.

## Generowanie

Sprawdź wszystkie deskryptory przechowywane w repozytorium:

```bash
python3 scripts/generate_board_config.py \
  --boards-root boards \
  --validate-only
```

Wygeneruj końcową konfigurację jednego profilu:

```bash
python3 scripts/generate_board_config.py \
  --boards-root boards \
  --target rp2040 \
  --board rp2040-zero \
  --output-dir .build/generated/boards/rp2040/rp2040-zero \
  --requested-feature HAL_ENABLE_RGB_LED
```

`--feature` pozostaje aliasem `--requested-feature` zachowanym dla zgodności
wstecznej.

Odśwież lub sprawdź wszystkie pliki generowane przechowywane w repozytorium, łącznie z konfiguracją zastępczą używaną bezpośrednio przez źródła:

```bash
python3 scripts/sync_generated.py --write
python3 scripts/sync_generated.py --check
```

Polecenia tworzą z deskryptorów publiczny typ wyliczeniowy profili, rejestr cech sprzętowych i pełną konfigurację zastępczą. Z `config/tooling/board_components.json` powstaje rejestr komponentów dla CMake. Nagłówek przechowywany w repozytorium jest jedyną kopią `jh_board_registry.h`; wyniki poszczególnych kompilacji go nie powielają.

Deterministycznie wygenerowany zestaw plików obejmuje:

- `jh_board_config.cmake`;
- `jh_board_config.h`;
- `jh_board_resolved.json`;
- `jh_link_contract.h`;
- jednostki translacji definiujące sygnaturę linkowania oraz odwołujące się
  do niej;
- `generation.d`.

Firmware nie analizuje JSON. CMake uruchamia generator przed importem Pico SDK, a następnie używa wygenerowanych ustawień platformy i płytki. `hal_board.h` zawsze korzysta z rejestru w repozytorium. Konfigurację płytki odczytuje z plików wygenerowanych dla kompilacji, a przy ich braku - z zapisanej konfiguracji zastępczej.

`jh_board_resolved.json` zawiera żądane funkcje w `requestedFeatures`, pełny zestaw po uwzględnieniu zależności w `resolvedFeatures`, informacje `featureProvenance`, skrót `resolvedFeaturesDigest` oraz definicje płytki i systemu kompilacji w `boardCompileDefinitions`. Pole `features` pozostaje aliasem `resolvedFeatures`.

Wygenerowany CMake eksportuje te dane jako `JH_BOARD_REQUESTED_FEATURES`, `JH_BOARD_RESOLVED_FEATURES`, `JH_BOARD_RESOLVED_FEATURES_DIGEST` i `JH_BOARD_COMPILE_DEFINITIONS`. Nagłówek `jh_board_config.h` udostępnia definicje jako makra preprocesora. Dzięki temu projekt kompilowany bezpośrednio, bez CMake i Pythona, otrzymuje taką samą konfigurację implementacji, magistrali i pinów.

## Biblioteki statyczne dla poszczególnych płytek

Biblioteki statyczne mają osobne katalogi dla każdej platformy i płytki:

```text
.build/static/<target>/<board>/
  libJaszczurHAL.a
  include/generated/
```

Przykładowe polecenia kompilacji:

```bash
./scripts/build_link_library.sh --target rp2040 --board rp2040-plus-4mb
./scripts/build_link_library.sh --target stm32g474 --board nucleo-g474re
./scripts/build_link_library.sh --target stm32g474 --board nucleo-g474re-pim730
./scripts/build_link_library.sh --target esp32s3 --board waveshare-esp32-s3-zero
```

`nucleo-g474re` opisuje samą płytkę Nucleo. Projekty używające zewnętrznego
radia PIM730/RM2 muszą wybrać obsługiwany profil `nucleo-g474re-pim730`;
profil określa stałe piny gSPI CYW43 i udostępnia cechy oraz komponenty
radiowe wymagane przez kompilacje sieciowe. Wygenerowany nagłówek płytki zawiera
też definicje backendu CYW43, magistrali gSPI, stosu i pinów; projekty
korzystające bezpośrednio z kompilatora nie mogą duplikować tych
definicji opcjami `-D` z wiersza poleceń. Okablowanie i ograniczenia
elektryczne są udokumentowane w
[Łączności](../api/pl/15_connectivity.md#konfiguracja-i-cykl-życia-backendu-cyw43).
Profile `picow`, `pico2w`, `pico-rm2` oraz `nucleo-g474re-pim730` deklarują
też cechę `bluetooth-controller` obsługiwaną przez mechanizm cyklu życia oraz
komponent `btstack-host`, włączany po wybraniu odpowiedniej funkcji HAL. Włączenie
`HAL_ENABLE_BLE` powoduje skompilowanie tego komponentu; sama cecha fizyczna
nigdy nie włącza
Bluetooth. Zobacz [API Bluetooth](../api/pl/20_bluetooth.md).

Eksperymentalny profil `rp2040-lora-lf` opisuje Waveshare SKU 26592. Używa
istniejącego targetu `rp2040` i definicji płytki `pico` z Pico SDK,
rezerwuje zintegrowane okablowanie SX1262, eksportuje `sx126x-radio` jako
komponent włączany po wybraniu odpowiedniej funkcji HAL i deklaruje
`HAL_BOARD_CAP_SX1262_RADIO`. Dane elektryczne tego profilu śledzone w
repozytorium obejmują SPI1 z bezpieczną domyślną
wartością 8 MHz, ścisły limit poniżej 18 MHz, konserwatywny zakres LF
410-450 MHz z wiki producenta, regulację DCDC, tryb oscylatora XTAL oraz
połączone sterowanie ścieżką antenową przez DIO2 i GPIO17. Podczas działania
mechanizm cyklu życia `hal_lora_radio` udostępnia informację o zadeklarowanej
obsłudze radia.

Eksperymentalne profile `pico-core1262-hf` oraz
`nucleo-g474re-core1262-hf` opisują stałe konfiguracje sprzętowe projektów
testowych, złożone z płytki bazowej i zewnętrznego modułu Waveshare
Core1262-HF. Rezerwują kompletne
okablowanie SPI/sterujące/przełącznika RF, deklarują `loraRadio` i
eksportują zarówno `external-radio-frontend`, jak i `sx1262-radio`. Profil
Nucleo używa SPI2 na PB13/PB14/PB15 i celowo zachowuje LD2 plus
`HAL_LED_BUILTIN` na PA5. Obie konfiguracje przeszły testy CAD/RSSI/kalibracji
bez transmisji oraz dwukierunkowe testy OTA, ale pozostają eksperymentalne,
ponieważ montaż na przewodach zworkowych i po jednym przetestowanym egzemplarzu
płytki bazowej każdego typu nie są równoważne stabilnemu projektowi płytki
nośnej.

Przy innym połączeniu modułu Core1262 użyj podstawowego profilu `pico` lub `nucleo-g474re` i jawnego deskryptora aplikacji. Nie wybieraj profilu złożonego, którego stałe przypisanie pinów nie odpowiada rzeczywistym połączeniom.

Archiwum definiuje:

```text
jh_board_contract_<target>_<board>_<featureHash>
```

`featureHash` obejmuje pierwsze 12 znaków szesnastkowych SHA-256 obliczonego z `hal.profileId` i następującej po nim posortowanej listy `resolvedFeatures`, zapisanej jako `HAL_ENABLE_*=1` lub `HAL_DISABLE_*=1`. Sama nazwa funkcji i zapis z `=1` dają ten sam skrót. Generator odrzuca `=0`, nieznane funkcje, żądania funkcji pochodnych i inne jawne wartości.

Różne listy żądanych funkcji dają ten sam `featureHash` i sygnaturę linkowania, jeżeli po rozwiązaniu zależności prowadzą do tego samego zestawu. Pole `requestedFeatures` nadal zachowuje pierwotne żądania do celów diagnostycznych.

Oficjalna kompilacja firmware zawsze dołącza wygenerowaną jednostkę translacji odwołującą się do sygnatury. Próba użycia biblioteki dla innej platformy, płytki lub innego wynikowego zestawu funkcji kończy się więc błędem niezdefiniowanego symbolu zgodności.

W GCC i Clang odwołanie jest zachowywane przez wygenerowaną funkcję z atrybutami `constructor, used`. Obsługiwane skrypty linkera zachowują tablicę konstruktorów, dlatego kontrola działa również z osobnymi sekcjami funkcji i danych oraz `--gc-sections`.

Biblioteka statyczna i jej wygenerowane nagłówki stanowią jeden pakiet. Nie kopiuj ani nie linkuj `libJaszczurHAL.a` bez pasującego `include/generated/` i jednostki translacji odwołującej się do sygnatury linkowania.

Dwie warunkowe reguły zgodności pozostają poza zestawem zależności rejestru v1: EEPROM AT24C256 może włączyć I2C, a GPS może wybrać UART, gdy nie wskazano transportu szeregowego. Reguły działają w `hal_config.h` i nie wpływają na równoważność `featureHash`. Skrót obejmuje zależności rozwiązane przez rejestr, nie każde makro dodane później.

## Zainstalowany pakiet

Zainstaluj skonfigurowaną bibliotekę statyczną dla RP lub STM32 przez CMake:

```bash
cmake --install .build/static/<target>/<board> \
  --prefix .build/install/<target>/<board>
```

Zainstalowany pakiet zawiera:

```text
include/
  hal/generated/
    jh_hal_features.h
    jh_board_registry.h
    jh_board_fallback_config.h
  generated/
    jh_board_config.h
    jh_link_contract.h
lib/
  libJaszczurHAL.a
share/JaszczurHAL/generated/
  jh_link_contract_reference.c
  jh_board_resolved.json
```

Pozostałe nagłówki publiczne są instalowane w `include/`. Po instalacji można kompilować aplikację odpowiednim kompilatorem, korzystając z żądań zapisanych w `jh_board_resolved.json`; `hal_config.h` uwzględnia zależności zapisane w wygenerowanych plikach repozytorium. Dołącz `jh_link_contract_reference.c` do aplikacji i zlinkuj ją z pasującą biblioteką. Ten sposób kompilacji nie wymaga Pythona. Nadal potrzebne są biblioteki SDK platformy, pliki startowe, skrypty linkera i standardowe opcje narzędzi.

## Dodawanie RP2040-Zero

Istniejący profil `rp2040-zero` pokazuje całą procedurę:

1. Zweryfikuj dane producenta i nagłówek płytki Pico SDK w wersji wskazanej
   przez repozytorium.
2. Dodaj `boards/profiles/rp2040-zero.json`.
3. Wybierz target `rp2040`, identyfikator płytki systemu budowania
   `waveshare_rp2040_zero` i potwierdź pamięć flash o pojemności 2 MB.
4. Opisz wyprowadzenia na złączach i polach lutowniczych oraz dodaj rezerwację
   `soft` GPIO16 dla diody statusu.
5. Opisz diodę jako adresowalny WS2812; kolejność RGB/GRB określa projekt.
6. Uruchom sprawdzanie rejestru i generowanie z katalogiem wyjściowym wewnątrz
   `.build`.
7. Sprawdź wygenerowany plik CMake, nagłówek i wynikowy JSON.
8. Dodaj testy wzorcowe, testy błędnych konfiguracji, testy par target/płytka,
   pamięci flash i sygnatury linkowania.
9. Wybierz `target: rp2040` oraz `board: rp2040-zero` w manifeście projektu.

Wygenerowany profil udostępnia definicje GPIO16 i WS2812, ale celowo nie
definiuje `HAL_LED_BUILTIN` ani domyślnej kolejności pikseli.
