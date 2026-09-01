# Profile targetów i płytek

*Dostępne również [po angielsku](../en/boards_profiles_howto.md).*

JaszczurHAL wybiera sprzęt za pomocą dwóch stabilnych identyfikatorów:

```json
{
  "target": "rp2040",
  "board": "rp2040-zero"
}
```

Target określa MCU, ISA, toolchain i recepturę buildu. Profil opisuje fizyczną
płytkę, jej pamięć flash, wyprowadzone i zarezerwowane piny, wbudowane
urządzenia, cechy sprzętowe oraz komponenty dobierane przez system budowania.
Funkcje aplikacji trzeba jawnie włączać za pomocą `HAL_ENABLE_*`; sama obecność
danej cechy sprzętowej nie włącza odpowiadającej jej funkcji.

Lista dostępnych profili pochodzi z `boards/profiles/*.json`. Ich stabilne
identyfikatory można wyświetlić poleceniem
`python3 scripts/generate_board_config.py --boards-root boards --list boards`.
Target ESP32-S3 udostępnia zestaw backendów podstawowych funkcji i peryferiów
oraz natywny graf zależności funkcji łączności i usług fazy 3.
Generator buildu sprawdza zgodność targetu, rozmiar pamięci flash, piny,
komponenty i reguły funkcji przed importem toolchainu. Te same deskryptory
generują dane konfiguracji zastępczej używane przy wyborze płytki bezpośrednio
w kodzie źródłowym. Dzięki temu nazwy płytek i ustawienia kompilacyjne pozostają
takie same także wtedy, gdy nie ma konfiguracji wygenerowanej podczas buildu.

## Pliki źródłowe

Dane źródłowe śledzone w kontroli wersji znajdują się w katalogu `boards/`:

- `targets/<id>.json` opisuje target MCU/ISA;
- `profiles/<id>.json` opisuje fizyczną płytkę;
- `capabilities.json` przypisuje stałe bity cech sprzętowych;
- `board.schema.json` zawiera wyłącznie informacje pomocnicze dla edytora;
- `scripts/generate_board_config.py` odpowiada za sprawdzanie poprawności
  strukturalnej i semantycznej.

Identyfikatory deskryptorów używają kebab-case i muszą odpowiadać swoim
nazwom plików. Nieznane pola, zduplikowane identyfikatory, niezgodne pary
target/płytka, nieprawidłowe punkty końcowe, nieznane cechy lub komponenty
oraz zapis poza `.build` zawsze powodują błąd.

## Model deskryptora

Każdy deskryptor zawiera `schemaVersion`, `kind`, `id`, `displayName`,
`description` i `status`.

Deskryptory targetu dodatkowo definiują:

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
  bezpiecznie wybrać w kodzie źródłowym bez udziału generatora buildu;
- identyfikatory komponentów definiowanych przez target;
- opcjonalny `requiredFeatures`, dodawany do wynikowego zestawu przed
  obliczeniem jego skrótu i wartości `featureHash`;
- opcjonalny `supportedFeatures`, czyli zamkniętą listę funkcji dozwolonych
  dla danego targetu, sprawdzaną przez skrypty buildów produkcyjnych po
  rozwiązaniu zależności przechodnich. Lista ta musi zawierać wszystkie
  funkcje wymagane.

Wynikowy `jh_board_config.h` przekształca dane z deskryptorów targetu w
definicje `HAL_TARGET_*`, a dane z deskryptorów płytki w definicje
`HAL_BOARD_*`. Funkcja `hal_system_get_current_architecture()` korzysta z
wygenerowanych danych targetu, dzięki czemu źródła backendu nie muszą
utrzymywać osobnej tabeli MCU, ISA i pamięci. Całkowity rozmiar flash pozostaje
daną płytki, ponieważ płytki przeznaczone dla jednego targetu mogą mieć różne
układy pamięci flash.

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

Punkty GPIO mają jawnie określoną domenę:

```json
{ "domain": "soc-gpio", "id": 16 }
```

Punkty STM32 mają identyfikatory symboliczne, na przykład `PA5`. Dla GPIO
dostarczanego przez inny układ stosuje się `component-gpio`, dzięki czemu nie
powiększa ono przestrzeni nazw GPIO SoC.

Rezerwacja ma typ `hard`, gdy aplikacja nie może użyć pinu, albo `soft`, gdy
pin pełni funkcję przypisaną płytce, ale aplikacja może nim świadomie sterować.
Okablowanie aplikacji, układ partycji, tożsamość produktu USB zdefiniowana
przez firmware, wybór zegara, dane poufne oraz kolejność pikseli WS2812 nie
należą do deskryptora płytki. Stały identyfikator USB interfejsu programowania
jest fizyczną cechą płytki i należy go zapisać w `programming.usb`.

Profil kompozytowy musi zachować fizyczne urządzenia bazowej płytki,
aliasy oraz publiczne definicje HAL. Nie usuwaj wbudowanego urządzenia,
takiego jak `HAL_LED_BUILTIN`, tylko po to, aby ponownie użyć jego pinu dla
podłączonego modułu: oryginalne urządzenie pozostaje elektrycznie
podłączone i może obciążać lub przełączać współdzieloną linię, nawet gdy
konflikt może wydawać się niegroźny. Zamiast tego wybierz niekolidujące
okablowanie. Celowa przeróbka PCB, taka jak otwarcie mostka lutowniczego,
wymaga odrębnego profilu, którego opis wyraźnie wskazuje fizyczną modyfikację.

## Urządzenia zdefiniowane w profilu płytki

Każdy wpis w sekcji `devices` ma identyfikator w formacie camelCase i określa
`kind`. Urządzenia korzystające z jednej linii - `gpio`, `component-gpio` i
`addressable` - mają pojedynczy `endpoint`.

Urządzenie podłączone do kilku pinów magistrali używa
`kind: "bus-device"` i wskazuje `role` z rejestru ról znanych generatorowi.
Rola określa sygnały i atrybuty odpowiednich typów, które musi zawierać
deskryptor. Dzięki temu profil nie może zawierać niepełnego opisu urządzenia.
Poniższy skrócony przykład pokazuje nazewnictwo; pełny profil
`rp2040-lora-lf` śledzony w repozytorium jest miarodajnym przykładem dla
SX1262:

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

Generator wymaga, aby każda rola występowała najwyżej raz w profilu płytki,
żadne dwa sygnały jednego urządzenia nie korzystały z tego samego pinu, każdy
sygnał `soc-gpio` miał rezerwację `hard`, a wartości atrybutów liczbowych
mieściły się w zakresie zadeklarowanego typu i spełniały wymagane ograniczenia
dotyczące kolejności. Obecność sygnałów i atrybutów może zależeć od wartości atrybutu
wyliczeniowego: stają się wymagane dla odpowiednich wartości, a przy innych są
niedozwolone.
Dlatego `rfSwitchMode: "dio2"` wyklucza linie i poziomy logiczne sterujące
przełącznikiem przez GPIO. Wartość `rfSwitchMode: "dio2-single-gpio"` opisuje
płytki, które włączają sterowanie przełącznikiem RF przez DIO2 układu SX1262,
a jednocześnie wymagają jednej zewnętrznej linii sterującej front-endu RF.

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

Identyfikatory komponentów, ich przypisanie do systemów budowania oraz grupy
wzajemnie wykluczających się komponentów pochodzą bezpośrednio z modelu danych
`config/tooling/board_components.json`.
Generator płytki odczytuje go bezpośrednio i tworzy na jego podstawie dane dla
CMake, dołączane przez `cmake/jh_board_components.cmake`. Każdy oficjalny
build sprawdza wynikową listę komponentów względem tego rejestru. Etap
konfiguracji kończy się błędem w przypadku nieznanego komponentu, komponentu
niezgodnego z wybranym systemem budowania albo dwóch komponentów należących do
tej samej grupy wyłączności. Receptury mogą uzależniać dołączenie źródeł od
wyeksportowanych flag `JH_BOARD_COMPONENT_<ID>`.

## Generowanie

Sprawdź poprawność wszystkich deskryptorów śledzonych w repozytorium:

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

Odśwież lub zweryfikuj wszystkie wygenerowane pliki śledzone w repozytorium, w
tym konfiguracje płytki dostępne bezpośrednio w źródłach:

```bash
python3 scripts/sync_generated.py --write
python3 scripts/sync_generated.py --check
```

Te polecenia generują bezpośrednio z deskryptorów publiczny typ wyliczeniowy
profili, rejestr cech sprzętowych i pełną konfigurację zastępczą dla kodu
źródłowego, a z `config/tooling/board_components.json` - rejestr komponentów
płytki dla CMake. Nagłówek C śledzony w repozytorium jest jedyną fizyczną kopią
`jh_board_registry.h`; dane tworzone dla konkretnego buildu nie powielają tego
pliku.

Deterministycznie wygenerowany zestaw plików obejmuje:

- `jh_board_config.cmake`;
- `jh_board_config.h`;
- `jh_board_resolved.json`;
- `jh_link_contract.h`;
- jednostki translacji definiujące sygnaturę linkowania oraz odwołujące się
  do niej;
- `generation.d`.

Firmware nigdy nie analizuje plików JSON. CMake uruchamia generator przed
importem Pico SDK i używa wygenerowanej konfiguracji platformy oraz płytki
właściwej dla wybranego systemu budowania. `hal_board.h` zawsze korzysta z
rejestru śledzonego w repozytorium, a następnie odczytuje konfigurację płytki
wygenerowaną przez build, jeśli jest dostępna; w przeciwnym razie używa
śledzonej konfiguracji zastępczej. `jh_board_resolved.json` zapisuje funkcje
wskazane bezpośrednio w `requestedFeatures`, listę `resolvedFeatures` po
rozwiązaniu zależności przechodnich, ich `featureProvenance`,
`resolvedFeaturesDigest` oraz definicje płytki i systemu budowania w
`boardCompileDefinitions`.
Zachowane pole `features` jest aliasem `resolvedFeatures`. Wygenerowany
CMake eksportuje te same wartości funkcji jako
`JH_BOARD_REQUESTED_FEATURES`, `JH_BOARD_RESOLVED_FEATURES` oraz
`JH_BOARD_RESOLVED_FEATURES_DIGEST`, a definicje systemu budowania eksportuje jako
`JH_BOARD_COMPILE_DEFINITIONS`. Plik `jh_board_config.h` przekształca te
definicje w makra preprocesora, dzięki czemu projekt kompilowany
bezpośrednio otrzymuje tę samą konfigurację backendu, magistrali i pinów bez
uruchamiania CMake ani Pythona.

## Biblioteki statyczne dla poszczególnych płytek

Biblioteki statyczne są rozdzielone według targetu i płytki:

```text
.build/static/<target>/<board>/
  libJaszczurHAL.a
  include/generated/
```

Przykłady buildu:

```bash
./scripts/build_rp_native_lib.sh \
  --target rp2040 \
  --board rp2040-plus-4mb

./scripts/build_stm32_lib.sh \
  --board nucleo-g474re

./scripts/build_stm32_lib.sh \
  --board nucleo-g474re-pim730
```

`nucleo-g474re` opisuje samą płytkę Nucleo. Projekty używające zewnętrznego
radia PIM730/RM2 muszą wybrać obsługiwany profil `nucleo-g474re-pim730`;
profil określa stałe piny gSPI CYW43 i udostępnia cechy oraz komponenty
radiowe wymagane przez buildy sieciowe. Wygenerowany nagłówek płytki zawiera
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

Przy innym okablowaniu Core1262 należy użyć zwykłego profilu `pico` lub
`nucleo-g474re` oraz jawnego deskryptora aplikacji. Nie należy wybierać profilu
kompozytowego, którego stała konfiguracja pinów nie pasuje do fizycznego
montażu.

Archiwum definiuje:

```text
jh_board_contract_<target>_<board>_<featureHash>
```

`featureHash` to pierwsze 12 znaków szesnastkowych skrótu SHA-256 obliczonego
dla `hal.profileId`, po którym następuje posortowana lista
`resolvedFeatures` z rejestru, zapisana jako `HAL_ENABLE_*=1` lub
`HAL_DISABLE_*=1`. Nazwa funkcji podana bez wartości oraz z wartością `=1`
dają więc ten sam skrót. Generator odrzuca `=0`, nieznane funkcje, żądania
funkcji pochodnych oraz inne jawne wartości funkcji. Dwa różne zestawy żądań,
które prowadzą do tego samego domknięcia, mają taki sam `featureHash` i
sygnaturę linkowania, choć `requestedFeatures` nadal zachowuje różnicę
potrzebną w diagnostyce.

Oficjalne buildy firmware zawsze kompilują wygenerowaną jednostkę translacji
z odwołaniem do sygnatury. Dlatego próba zlinkowania archiwum przeznaczonego
dla innego targetu, innej płytki lub innego wynikowego zestawu funkcji
kończy się błędem niezdefiniowanego symbolu zgodności. W przypadku GCC i Clang
wygenerowana funkcja z atrybutami `constructor, used` chroni odwołanie przed
usunięciem. Obsługiwane skrypty linkera zachowują tablicę konstruktorów, więc
sprawdzanie sygnatury nadal działa przy włączonych sekcjach funkcji i danych
oraz opcji `--gc-sections`.

Archiwum i jego wygenerowane nagłówki to jedna jednostka. Nigdy nie kopiuj
ani nie linkuj `libJaszczurHAL.a` bez pasującego katalogu
`include/generated/` oraz jednostki translacji zawierającej odwołanie do
sygnatury linkowania.

Dwie warunkowe reguły zgodności pozostają poza domknięciem rejestru v1:
EEPROM AT24C256 może dodać I2C, a GPS może wybrać UART, gdy nie zażądano
żadnego transportu szeregowego. Działają one w `hal_config.h` i nie
uczestniczą w równoważności `featureHash`. Skrót uwzględnia zestaw wynikający
z rozwiązania zależności w rejestrze, a nie każde makro dodane później przez
te dodatkowe reguły.

## Zainstalowany pakiet

Zainstaluj skonfigurowany build statyczny RP lub STM32 za pomocą CMake:

```bash
cmake --install .build/static/<target>/<board> \
  --prefix .build/install/<target>/<board>
```

Zainstalowana jednostka zawiera:

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

Pozostałe publiczne nagłówki są standardowo instalowane w katalogu
`include/`. Po instalacji kompilator właściwy dla danego targetu może
skompilować źródła projektu, korzystając z bezpośrednich żądań funkcji w
`jh_board_resolved.json`; `hal_config.h` stosuje domknięcie zapisane w
śledzonych plikach wygenerowanych. Skompiluj
`jh_link_contract_reference.c` do aplikacji i zlinkuj go z pasującym
archiwum. Ten sposób kompilacji i linkowania nie wymaga uruchamiania Pythona.
Wybrana platforma nadal wymaga bibliotek SDK targetu, plików startowych,
skryptów linkera i standardowych flag toolchainu.

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
