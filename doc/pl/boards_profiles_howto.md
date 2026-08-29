# Profile targetów i płytek

*Dostępne również [po angielsku](../en/boards_profiles_howto.md).*

JaszczurHAL wybiera sprzęt za pomocą dwóch stabilnych identyfikatorów:

```json
{
  "target": "rp2040",
  "board": "rp2040-zero"
}
```

Target identyfikuje MCU, ISA, toolchain i recepturę buildu. Profil płytki
identyfikuje fizyczną płytkę, jej flash, eksponowane i zarezerwowane piny,
urządzenia pokładowe, capabilities oraz kontrolowane komponenty buildu.
Funkcje aplikacji pozostają opt-in przez `HAL_ENABLE_*`; możliwość sprzętowa
nigdy sama w sobie nie włącza funkcji.

Bieżąca inwentaryzacja profili pochodzi z `boards/profiles/*.json`; wylistuj
jej stabilne identyfikatory poleceniem
`python3 scripts/generate_board_config.py --boards-root boards --list boards`.
Target ESP32-S3 udostępnia zestaw backendów
rdzenia/peryferiów oraz graf natywnej łączności/usług Fazy 3. Generator
buildu waliduje zgodność targetu, rozmiar flash, piny, komponenty i
reguły funkcji przed importem toolchainu. Te same deskryptory generują
fallback źródłowy, więc nazwy płytek i fakty czasu buildu pozostają
identyczne bez konfiguracji wygenerowanej przez build.

## Pliki źródłowe

Wersjonowanym źródłem prawdy jest `boards/`:

- `targets/<id>.json` opisuje target MCU/ISA;
- `profiles/<id>.json` opisuje fizyczną płytkę;
- `capabilities.json` przypisuje stabilne bity możliwości;
- `board.schema.json` dostarcza wyłącznie wsparcie edytora;
- `scripts/generate_board_config.py` wykonuje walidację strukturalną i
  semantyczną.

Identyfikatory deskryptorów używają kebab-case i muszą odpowiadać swoim
nazwom plików. Nieznane pola, zduplikowane identyfikatory, niezgodne pary
target/płytka, nieprawidłowe punkty końcowe, nieznane możliwości lub
komponenty oraz wyjście poza `.build` to twarde błędy.

## Model deskryptora

Każdy deskryptor zawiera `schemaVersion`, `kind`, `id`, `displayName`,
`description` i `status`.

Deskryptory targetu dodatkowo definiują:

- `architecture`: providera, rodzinę, SoC, ISA, liczbę rdzeni, publiczne
  nazwy MCU/podtypu/CPU, obecność FPU oraz nazwę backendu runtime;
- `hal.targetSelector`;
- `build`: providera, kontrolowaną recepturę oraz platformę providera lub
  `idfTarget`, gdy wymagane;
- `gpio`: format identyfikatora pinu, dokładne prawidłowe piny, opcjonalne
  cechy pinów oraz kodowanie HAL;
- `memory.regions` plus `memory.ramUsableBytes`; całkowita RAM jest
  generowana z każdego regionu RAM, podczas gdy użyteczna RAM opisuje
  region normalnie eksponowany przez domyślny linker aplikacji;
- `defaultBoard`;
- opcjonalny `sourceFallbackBoard`, używany tylko wtedy, gdy wybór na
  poziomie źródła może bezpiecznie wybrać płytkę bez generatora buildu;
- identyfikatory komponentów należące do targetu.
- opcjonalny `requiredFeatures`, dodawany do zestawu efektywnego przed
  obliczeniem jego skrótu i hasha funkcji;
- opcjonalny `supportedFeatures`, zamknięta, specyficzna dla targetu
  dozwolona lista egzekwowana przez runnery produkcyjne po przechodnim
  rozwiązaniu. Musi zawierać każdą wymaganą funkcję.

Rozwiązany `jh_board_config.h` rzutuje deskryptory targetu na fakty
`HAL_TARGET_*`, a deskryptory płytki na fakty `HAL_BOARD_*`.
`hal_system_get_current_architecture()` odczytuje te wygenerowane fakty
targetu zamiast utrzymywać drugą tabelę MCU/ISA/pamięci w źródle backendu.
Całkowity flash pozostaje faktem płytki, ponieważ płytki dla jednego
targetu mogą nosić różne urządzenia flash.

Deskryptory płytki dodatkowo definiują:

- `compatibleTargets` i `build.provider`;
- identyfikator płytki providera tam, gdzie wymagany;
- stabilny `hal.profileId`, selektor, aliasy zgodności oraz opcjonalne
  selektory autodetekcji providera; nazwa runtime to
  zawsze `id` płytki;
- źródło fizycznego flash i oczekiwany rozmiar, plus zamontowany PSRAM,
  gdy obecny;
- opcjonalny transport `programming`, stały USB VID/PID programatora oraz
  mechanizm reset/boot używany do bezpiecznego wyboru urządzenia po
  stronie hosta;
- eksponowane piny, grupy złączy, rezerwacje i aliasy;
- możliwości, urządzenia należące do płytki, domyślne ustawienia
  peryferiów oraz komponenty.

Waveshare ESP32-S3-Zero opisuje swój natywny programator USB Serial/JTAG
jako:

```json
"programming": {
  "transport": "usb-serial-jtag",
  "usb": { "vid": 12346, "pid": 4097 },
  "reset": "usb-serial-jtag-control-lines",
  "boot": "usb-serial-jtag-control-lines"
}
```

Dziesiętne wartości USB to `303a:1001` w zwykłym zapisie szesnastkowym.
`jh-vscode` wyprowadza swój weryfikator tożsamości z tych faktów płytki;
manifesty ich nie duplikują. Domknięcie sprzętowe Fazy 1 zweryfikowało tę
tożsamość programowania, trzy kompletne trzyobrazowe flashowania,
wykrywanie ESP32-S3/dwóch rdzeni, 4 MiB fizycznego flash, zainicjalizowany
2 MiB Quad PSRAM oraz ponowne połączenie monitora szeregowego na płytce
SKU 25081. Faza 2 dodaje wygenerowane maski dostępności/rezerwacji GPIO
używane przez backendy GPIO, ADC, UART, I2C i SPI ESP32-S3. Fizyczny
fixture następnie przeszedł oba rdzenie aplikacji, GPIO/IRQ, ADC, pętlę
zwrotną UART, master I2C, master SPI, GPTimer, RX/TX USB Serial/JTAG oraz
sprawdzenia systemu/synchronizacji na tej płytce. Target i profil płytki są
więc oznaczone jako `supported`.

Punkty końcowe GPIO używają jawnej domeny:

```json
{ "domain": "soc-gpio", "id": 16 }
```

Punkty końcowe STM32 używają identyfikatorów symbolicznych, takich jak
`PA5`. GPIO dostarczane przez inny chip używa `component-gpio`, więc nie
zawyża przestrzeni nazw GPIO SoC.

Rezerwacje są `hard`, gdy aplikacja nie może użyć pinu, oraz `soft`, gdy pin
ma funkcję należącą do płytki, którą aplikacja może celowo sterować.
Okablowanie aplikacji, układ partycji, tożsamość produktu USB zdefiniowana
przez firmware, wybór zegara, sekrety oraz kolejność pikseli WS2812 nie
należą do deskryptora płytki. Stała tożsamość USB transportu programowania
płytki jest faktem fizycznej płytki i należy pod `programming.usb`.

Profil kompozytowy musi zachować fizyczne urządzenia bazowej płytki,
aliasy oraz publiczne definicje HAL. Nie usuwaj wbudowanego urządzenia,
takiego jak `HAL_LED_BUILTIN`, tylko po to, aby ponownie użyć jego pinu dla
podłączonego modułu: oryginalne urządzenie pozostaje elektrycznie
podłączone i może obciążać lub przełączać współdzieloną linię, nawet gdy
nakładanie się wygląda nieszkodliwie. Zamiast tego wybierz niekolidujące
okablowanie. Celowa przeróbka PCB, taka jak otwarcie mostka lutowniczego,
wymaga odrębnego profilu, którego opis stwierdza fizyczną modyfikację.

## Urządzenia należące do płytki

Każdy wpis pod `devices` używa identyfikatora camelCase i deklaruje `kind`.
Urządzenia z jedną linią - `gpio`, `component-gpio` i `addressable` - mają
pojedynczy `endpoint`.

Urządzenie okablowane na kilku pinach magistrali używa `kind: "bus-device"`
i nazywa `role` z rejestru ról urządzeń generatora. Rola deklaruje, które
sygnały i które typowane atrybuty musi dostarczyć deskryptor, więc profil
nie może zawierać częściowo opisanego urządzenia. Ten skrócony przykład
ilustruje nazewnictwo; użyj kompletnego, śledzonego profilu
`rp2040-lora-lf` jako autorytatywnego przykładu SX1262:

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

Generator egzekwuje, że każda rola pojawia się co najwyżej raz na płytkę,
że żadne dwa sygnały jednego urządzenia nie współdzielą pinu, że każdy
sygnał `soc-gpio` jest pokryty rezerwacją `hard`, oraz że atrybuty
liczbowe pozostają wewnątrz zadeklarowanego typu i porządku. Sygnały i
atrybuty mogą być bramkowane atrybutem enum: stają się wymagane, gdy
bramka je wybiera, i odrzucane w przeciwnym razie, więc
`rfSwitchMode: "dio2"` zabrania linii i poziomów przełącznika GPIO.
`rfSwitchMode: "dio2-single-gpio"` modeluje płytki, które włączają
sterowanie przełącznikiem RF DIO2 SX1262 i dodatkowo wymagają jednej
zewnętrznej linii sterującej front-endu.

Każda rola generuje stały zestaw makr w `jh_board_config.h` pod
swoim własnym prefiksem, plus `HAL_BOARD_DEVICE_PIN_NONE` dla brakujących
sygnałów opcjonalnych:

```c
#define HAL_BOARD_LORA_RADIO_PRESENT 1
#define HAL_BOARD_LORA_RADIO_SPI_BUS 1u
#define HAL_BOARD_LORA_RADIO_PIN_CS 13u
#define HAL_BOARD_LORA_RADIO_PIN_RF_SWITCH_A HAL_BOARD_DEVICE_PIN_NONE
#define HAL_BOARD_LORA_RADIO_MAX_SPI_CLOCK_HZ UINT32_C(16000000)
#define HAL_BOARD_LORA_RADIO_REGULATOR_IS_DCDC 1
```

Płytki bez urządzenia nadal definiują `<PREFIX>_PRESENT 0`, więc moduł HAL
może rozwiązać konfigurację dostarczoną przez płytkę podczas buildu.
Atrybuty enum emitują jedną flagę `_IS_<VALUE>` na każdą dozwoloną wartość
oraz string `_NAME`; symboliczne piny STM32 są kodowane jako te same
całkowitoliczbowe identyfikatory pinów, których używa HAL. Kompletny
deskryptor dociera też niezmieniony do `jh_board_resolved.json` dla
narzędzi.

Identyfikatory komponentów, providera i wyłączne sloty pochodzą z
autorytatywnego modelu `config/tooling/board_components.json`. Generator
płytki odczytuje go bezpośrednio i zapisuje projekcję CMake dołączaną
przez `cmake/jh_board_components.cmake`. Każdy oficjalny build
waliduje rozwiązaną listę komponentów względem tego rejestru: nieznany
komponent, komponent niepasujący do providera buildu lub dwa komponenty
zajmujące ten sam wyłączny slot kończą etap konfiguracji błędem.
Receptury mogą warunkować integrację źródeł od wyeksportowanych flag
`JH_BOARD_COMPONENT_<ID>`.

## Generowanie

Zwaliduj wszystkie śledzone deskryptory:

```bash
python3 scripts/generate_board_config.py \
  --boards-root boards \
  --validate-only
```

Wygeneruj jeden rozwiązany profil:

```bash
python3 scripts/generate_board_config.py \
  --boards-root boards \
  --target rp2040 \
  --board rp2040-zero \
  --output-dir .build/generated/boards/rp2040/rp2040-zero \
  --requested-feature HAL_ENABLE_RGB_LED
```

`--feature` pozostaje pisownią kompatybilności wstecznej dla
`--requested-feature`.

Odśwież lub zweryfikuj wszystkie śledzone wygenerowane artefakty, w tym
projekcje płytki na poziomie źródła:

```bash
python3 scripts/sync_generated.py --write
python3 scripts/sync_generated.py --check
```

Te komendy generują publiczny rejestr enum/capabilities oraz kompletną
konfigurację fallback bezpośrednio z deskryptorów, plus rejestr komponentów
płytki CMake z `config/tooling/board_components.json`. Śledzony nagłówek C
jest jedynym fizycznym `jh_board_registry.h`; wyjście per-build nigdy go
nie duplikuje.

Deterministyczne wyjście zawiera:

- `jh_board_config.cmake`;
- `jh_board_config.h`;
- `jh_board_resolved.json`;
- `jh_link_contract.h`;
- definicję sygnatury linkowania oraz jednostki translacji referencyjnej;
- `generation.d`.

Firmware nigdy nie parsuje JSON. CMake uruchamia generator przed importem
Pico SDK i używa wygenerowanej platformy providera oraz płytki. `hal_board.h`
zawsze używa śledzonego rejestru, a następnie odczytuje konfigurację płytki
wygenerowaną przez build, gdy jest dostępna, albo śledzony wygenerowany
fallback w przeciwnym razie.
`jh_board_resolved.json` zapisuje bezpośrednie `requestedFeatures`,
przechodni rejestr `resolvedFeatures`, ich `featureProvenance`,
`resolvedFeaturesDigest` oraz `boardCompileDefinitions` płytki/providera.
Zachowane pole `features` jest aliasem `resolvedFeatures`. Wygenerowany
CMake eksportuje te same wartości funkcji jako
`JH_BOARD_REQUESTED_FEATURES`, `JH_BOARD_RESOLVED_FEATURES` oraz
`JH_BOARD_RESOLVED_FEATURES_DIGEST`, a definicje providera eksportuje jako
`JH_BOARD_COMPILE_DEFINITIONS`. `jh_board_config.h` zapisuje te definicje
providera jako makra preprocesora, więc kod korzystający bezpośrednio z
kompilatora otrzymuje tę samą konfigurację backendu, magistrali i pinów
bez uruchamiania CMake lub Pythona.

## Biblioteki statyczne świadome płytki

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
radia PIM730/RM2 muszą wybrać wspierany profil `nucleo-g474re-pim730`;
profil określa stałe piny gSPI CYW43 i eksportuje capabilities oraz komponenty
radiowe wymagane przez buildy sieciowe. Wygenerowany nagłówek płytki zawiera
też definicje backendu CYW43, magistrali gSPI, stosu i pinów; projekty
korzystające bezpośrednio z kompilatora nie mogą duplikować tych
definicji opcjami `-D` z wiersza poleceń. Okablowanie i ograniczenia
elektryczne są udokumentowane w
[Łączności](../api/pl/15_connectivity.md#konfiguracja-i-cykl-życia-backendu-cyw43).
Profile `picow`, `pico2w`, `pico-rm2` oraz `nucleo-g474re-pim730` deklarują
też możliwość `bluetooth-controller` należącą do cyklu życia oraz
bramkowany funkcją komponent `btstack-host`. Włączenie `HAL_ENABLE_BLE`
kompiluje ten komponent; sama fizyczna możliwość nigdy nie włącza
Bluetooth. Zobacz [API Bluetooth](../api/pl/20_bluetooth.md).

Eksperymentalny profil `rp2040-lora-lf` opisuje Waveshare SKU 26592. Używa
istniejącego targetu `rp2040` i definicji płytki `pico` z Pico SDK,
rezerwuje zintegrowane okablowanie SX1262, eksportuje `sx126x-radio` jako
komponent bramkowany funkcją i deklaruje `HAL_BOARD_CAP_SX1262_RADIO`. Jego
śledzone fakty elektryczne obejmują SPI1 przy bezpiecznej domyślnej
wartości 8 MHz, ścisły limit poniżej 18 MHz, konserwatywny zakres LF
410-450 MHz z wiki producenta, regulację DCDC, tryb oscylatora XTAL oraz
połączone sterowanie ścieżką antenową przez DIO2 plus GPIO17. Cykl życia
`hal_lora_radio` publikuje zadeklarowaną capability radia w runtime.

Eksperymentalne profile `pico-core1262-hf` oraz
`nucleo-g474re-core1262-hf` opisują ustalone konfiguracje testowe zbudowane
z bazowej płytki i zewnętrznego Waveshare Core1262-HF. Rezerwują kompletne
okablowanie SPI/sterujące/przełącznika RF, deklarują `loraRadio` i
eksportują zarówno `external-radio-frontend`, jak i `sx1262-radio`. Profil
Nucleo używa SPI2 na PB13/PB14/PB15 i celowo zachowuje LD2 plus
`HAL_LED_BUILTIN` na PA5. Obie konfiguracje przeszły testy CAD/RSSI/kalibracji
bez transmisji oraz dwukierunkowe testy OTA, ale pozostają eksperymentalne,
ponieważ montaż na przewodach zworkowych i jeden przetestowany host każdego
typu nie są równoważne stabilnemu projektowi nośnika.

Inne okablowanie Core1262 używa zwykłego profilu `pico` lub `nucleo-g474re`
oraz jawnego deskryptora aplikacji. Nie może wybierać profilu
kompozytowego, którego stała konfiguracja pinów nie pasuje do fizycznego
montażu.

Archiwum definiuje:

```text
jh_board_contract_<target>_<board>_<featureHash>
```

`featureHash` to pierwsze 12 znaków szesnastkowych SHA-256 nad
`hal.profileId`, po którym następuje posortowane `resolvedFeatures`
rejestru, serializowane jako `HAL_ENABLE_*=1` lub `HAL_DISABLE_*=1`. Nagie
nazwy funkcji i `=1` produkują więc ten sam hash; generator odrzuca `=0`,
nieznane funkcje, żądania funkcji pochodnych oraz inne jawne wartości
funkcji. Dwa różne żądane zestawy, które produkują to samo domknięcie, mają
ten sam hash funkcji i sygnaturę linkowania, podczas gdy
`requestedFeatures` nadal zachowuje ich różnicę diagnostyczną.

Oficjalne buildy firmware zawsze kompilują wygenerowaną referencyjną
jednostkę translacji. Linkowanie archiwum dla innego targetu, płytki lub
rozwiązanego zestawu funkcji zawodzi więc z niezdefiniowanym symbolem
zgodności. Dla GCC i Clang referencja jest zakorzeniona przez wygenerowaną
funkcję `constructor, used`. Tablica konstruktorów jest zachowywana przez
wspierane skrypty linkera, więc sygnatura pozostaje efektywna, gdy
sekcje funkcji/danych oraz `--gc-sections` są włączone.

Archiwum i jego wygenerowane nagłówki to jedna jednostka. Nigdy nie kopiuj
ani nie linkuj `libJaszczurHAL.a` bez pasującego katalogu
`include/generated/` oraz referencyjnej jednostki translacji sygnatury
linkowania.

Dwie warunkowe reguły zgodności pozostają poza domknięciem rejestru v1:
EEPROM AT24C256 może dodać I2C, a GPS może wybrać UART, gdy nie zażądano
żadnego transportu szeregowego. Działają one w `hal_config.h` i nie
uczestniczą w równoważności featureHash. Hash porównuje zestaw rozwiązany
przez rejestr, nie każde makro dodane później przez te resztkowe reguły.

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

Reszta publicznych nagłówków jest instalowana poniżej `include/` jak
zwykle. Po instalacji pasujący kompilator może skompilować źródła projektu,
używając bezpośrednich żądań z `jh_board_resolved.json`; `hal_config.h`
stosuje śledzone wygenerowane domknięcie. Skompiluj
`jh_link_contract_reference.c` do aplikacji i zlinkuj go z pasującym
archiwum. Ta ścieżka buildu i linkowania nie wywołuje
Pythona. Biblioteki SDK targetu, pliki startowe, skrypty linkera oraz
normalne flagi toolchainu są nadal wymagane przez wybraną platformę.

## Dodawanie RP2040-Zero

Istniejący profil `rp2040-zero` demonstruje kompletną procedurę:

1. Zweryfikuj dane producenta i przypięty nagłówek płytki Pico SDK.
2. Dodaj `boards/profiles/rp2040-zero.json`.
3. Wybierz target `rp2040`, płytkę providera `waveshare_rp2040_zero` i
   potwierdź 2 MB flash.
4. Opisz eksponowane nagłówki/pola oraz zarezerwuj miękko GPIO16 dla diody
   statusu.
5. Opisz diodę jako adresowalny WS2812 z kolejnością RGB/GRB należącą do
   projektu.
6. Uruchom walidację rejestru i generowanie poniżej `.build`.
7. Sprawdź wygenerowany CMake, nagłówek i rozwiązany JSON.
8. Dodaj testy golden, negatywne, target/płytka, flash i sygnatury
   linkowania.
9. Wybierz `target: rp2040` oraz `board: rp2040-zero` w manifeście projektu.

Wygenerowany profil eksponuje fakty GPIO16 i WS2812, ale celowo nie
definiuje `HAL_LED_BUILTIN` ani domyślnej kolejności pikseli.
