# Skrypty obsługi repozytorium JaszczurHAL

*Dostępne również [po angielsku](../en/00_scripts.md).*

Ten dokument zawiera zbiorczy wykaz skryptów służących do konfiguracji,
kompilowania, sprawdzania, pakowania i obsługi JaszczurHAL. Obejmuje wszystkie
skrypty z `scripts/` oraz główne punkty wejścia znajdujące się w pozostałych
częściach repozytorium.

Uruchamiaj polecenia z katalogu głównego repozytorium, chyba że dana sekcja
mówi inaczej. W razie rozbieżności z tym dokumentem rozstrzygające są
implementacja skryptu i jego komunikat `--help`.

## Główne punkty wejścia

| Cel | Polecenie | Rezultat |
|---|---|---|
| Przygotowanie stacji roboczej Debian/Ubuntu | `./runmefirst.sh` | Instaluje wymagane narzędzia hostowe, toolchain ARM, narzędzia analizy i bezpieczeństwa, obsługę USB oraz integrację z VS Code; synchronizuje zarządzane komponenty i konfiguruje hooki Git. |
| Przygotowanie natywnej stacji roboczej Windows | `powershell -NoProfile -ExecutionPolicy Bypass -File .\runmefirst.ps1` | Przygotowuje zarządzane środowisko Pythona w wersji wskazanej przez repozytorium, natywne toolchainy, komponenty źródłowe i ścieżki użytkownika Cortex-Debug, a następnie sprawdza konfigurację hosta Windows. |
| Synchronizacja zarządzanych zależności | `./third_party/update_components.sh` | Pobiera brakujące komponenty i zastępuje zarządzane instalacje niezgodne z wersjami zapisanymi w repozytorium. |
| Weryfikacja zależności bez ich zmiany | `./third_party/update_components.sh --verify-only` | Sprawdza wersje wszystkich zarządzanych komponentów, commity, wymagane pliki, stan archiwum PMD, zbudowany picotool oraz stempel łańcucha narzędzi RISC-V. |
| Odświeżenie wszystkich wersjonowanych plików generowanych | `python3 scripts/sync_generated.py --write` | Uruchamia generatory funkcji, płytek, przykładów, głównego VS Code oraz SBOM i wypisuje każdy plik zmieniony podczas synchronizacji. |
| Weryfikacja wszystkich wersjonowanych plików generowanych | `python3 scripts/sync_generated.py --check` | Uruchamia każdy generator w trybie weryfikacji tylko do odczytu i kończy się niepowodzeniem przy brakującym lub nieaktualnym wyjściu. |
| Uruchomienie pełnej bramki repozytorium | `./runalltests.sh` | Czyści katalogi robocze bramki i uruchamia testy, kontrole Clang ASan/UBSan/libFuzzer, Valgrind, analizę statyczną, CPD, buildy targetów oraz buildy przykładów. |
| Uruchomienie bramki sanitizerów/fuzz | `scripts/run_sanitizer_fuzz.sh` | Odtwarza build hosta instrumentowany przez Clang, uruchamia wszystkie testy pod ASan/UBSan i wykonuje krótkie fuzzowanie parserów sieciowych. |
| Obsługa projektu firmware | `vscode/entry/jh-vscode <action> --project <dir>` w Uniksie lub `vscode/entry/jh-vscode.cmd ...` w Windows | Dostarcza stabilny CLI buildu, wgrywania, monitorowania, wyboru płytki, IntelliSense oraz czyszczenia używany przez projekty VS Code. |
| Build lub flashowanie projektu ESP-IDF | `python3 scripts/build_esp_idf.py <action> --project <dir>` | Uruchamia akcję `build`, `artifacts` lub `flash`; ustala metadane targetu i płytki ESP, w razie potrzeby przygotowuje SDK w wersji wskazanej przez repozytorium oraz sprawdza przenośny manifest zawierający wiele obrazów. |
| Build przykładów przechowywanych w repozytorium | `scripts/examples_dispatcher.py build --target <target>` | Kompiluje manifesty przykładów za pomocą tego samego mechanizmu `jh-vscode` i CMake, którego używają projekty firmware. |
| Build natywnych testów parytetu RP | `scripts/build_rp_native_parity_fixtures.sh` | Kompiluje testy USB wielordzeniowego i SDLogger dla wszystkich obsługiwanych natywnych kombinacji target/runtime. |

### Polityka artefaktów

Artefakty buildu generowane przez repozytorium trafiają do `.build/`, a
zarządzane instalacje komponentów do `third_party/`. Układ katalogów,
oddzielenie cache dla poszczególnych targetów i płytek oraz zasady utrzymania
plików generowanych opisano w
[Katalogi buildu i pliki generowane](../../pl/FwProjectWorkflow.md#katalogi-budowania-i-pliki-generowane).

## Interfejsy narzędziowe

`config/tooling/` zawiera wersjonowane dane repozytorium używane wspólnie przez
skrypty, pliki generowane, CMake i kod przygotowujący host. Każdy dokument JSON
ma `schemaVersion: 1` i należy do jednego obszaru:

| Plik danych | Znaczenie |
|---|---|
| `artifacts.json` | Określa nazwy plików metadanych archiwów oraz wersjonowanych plików generowanych. |
| `board_components.json` | Definiuje prawidłowe komponenty płytek, providerów i wzajemnie wykluczające się grupy. |
| `examples.json` | Definiuje rejestr aktywnych przykładów przechowywanych w repozytorium. |
| `managed_components.json` | Definiuje zarządzane komponenty źródłowe/narzędziowe, metadane walidacji, domyślną kolejność oraz skrypty startowe zgodności. |

Kod Pythona wczytuje te dokumenty przez `scripts/tooling_contract.py`.
Nazwane ścieżki artefaktów rozwiązuje `scripts/repository_layout.py`.
CMake nie parsuje JSON podczas zwykłej konfiguracji: generator płytek zapisuje
`cmake/generated/jh_board_components_registry.cmake` na podstawie
`board_components.json`.

Po zmianie danych komponentów płytki lub innego wejścia generatora, odśwież i
sprawdź wszystkie wersjonowane pliki generowane za pomocą wspólnego skryptu:

```bash
python3 scripts/sync_generated.py --write
python3 scripts/sync_generated.py --check
```

Stałe tekstowe protokołu i formatu przechowuj blisko operacji, których dotyczą. W
szczególności jawne argumenty `encoding="utf-8"` dokumentują format tekstu na
dysku i celowo nie są zastępowane globalną stałą tekstową. Komunikaty
skierowane do użytkownika oraz tokeny składniowe używane tylko raz również
pozostają przy kodzie, który je obsługuje.

## Orkiestratorzy na poziomie repozytorium

Te skrypty celowo znajdują się poza `scripts/`, ponieważ stanowią główne
punkty wejścia procesów obejmujących całe repozytorium.

### `runmefirst.sh`

Jednorazowa, idempotentna konfiguracja dla systemów zgodnych z Debian/Ubuntu. Skrypt:

- usuwa drzewo `.build/` repozytorium przed konfiguracją;
- instaluje kompilatory, CMake, Ninja, Python, Java, Valgrind, narzędzia Clang
  do sanitizerów i fuzzowania, clang-tidy, cppcheck, OpenOCD,
  `gdb-multiarch`, obsługę portu szeregowego, libusb oraz inne pakiety hosta;
- wywołuje `third_party/update_components.sh`;
- instaluje `osv-scanner` oraz `cve-bin-tool`;
- instaluje regułę udev dla dostępu USB BOOTSEL/picotool do RP2040/RP2350
  oraz portu `/dev/ttyACM*` w trybie aplikacji używanego przez automatyczny
  reset przy 1200 bps;
- sprawdza obecność trwałej reguły dla połączeń zwrotnych OTA przez TCP/8266,
  ograniczonej do sieci LAN, i pyta przed zmianą zapory sieciowej lub instalacją
  `iptables-persistent`;
- konfiguruje hooki Git repozytorium;
- weryfikuje, że każde wymagane narzędzie jest dostępne.

Skrypt używa `sudo` dla pakietów systemowych, `/usr/local/bin`, reguły udev
oraz zaakceptowanej przez użytkownika zmiany zapory sieciowej. Pobiera narzędzia i
zależności, więc wymaga dostępu do sieci. Dedykowany pomocnik zapory
sieciowej to `scripts/configure_ota_firewall.py`; obsługuje `--check`, jawne
`--interface` / `--network`; zmiany wymagają potwierdzenia lub opcji `--yes`.

### `runmefirst.ps1`

Idempotentna konfiguracja natywnego środowiska Windows. Przed wprowadzeniem
zmian skrypt wyświetla pełny plan. Używa krótkich katalogów narzędzi i buildu
w profilu użytkownika, tworzy środowisko Python 3.12 w wersji wskazanej przez
repozytorium, ze sprawdzonym
skrótem pakietu pyserial, synchronizuje komponenty źródłowe i odnajduje CMake,
Ninja, GNU Arm, GNU RISC-V, OpenOCD oraz picotool. Zgodne narzędzia systemowe
są używane ponownie, chyba że podano `-Force`. Systemowy OpenOCD jest używany
tylko wtedy, gdy dostępne są także wymagane skrypty interfejsu i targetu. W
przeciwnym razie instalowane jest zarządzane archiwum o zweryfikowanej
autentyczności.

Zapisuje zweryfikowany zestaw plików wykonywalnych, zarządzany Python oraz
krótki katalog główny buildu w `.build/windows/host-environment.json` dla
wspólnych narzędzi firmware. Tryb edytora dodatkowo
zachowuje i aktualizuje standardowy `settings.json` użytkownika VS Code o
specyficzne dla Windows ścieżki OpenOCD i GNU Arm dla Cortex-Debug; tworzy
odzyskiwalny plik `.jaszczurhal.bak` przed zmianą istniejących ustawień.

`-VerifyOnly` jest tylko do odczytu. `-ConfigureHost` jawnie zezwala na
naprawę udokumentowanych ustawień długich ścieżek, a `-InstallExtensions`
jawnie zezwala na zmiany profilu VS Code. `-FirmwareOnly` nadal pokazuje
wyniki sprawdzania edytora, lecz traktuje je jako opcjonalne w środowiskach
bez interfejsu graficznego i w CI. Pomija również konfigurację profilu
Cortex-Debug.
`-VerifyOnly` sprawdza skonfigurowane ścieżki debuggera bez zapisywania.
Skrypt nigdy się sam nie podnosi do uprawnień administratora. Zobacz
[Natywna konfiguracja dla Windows](../../pl/windows_setup.md), gdzie opisano
wymagania hosta, polecenia, ścieżki i obecnie obsługiwany zakres.

### `scripts/windows_host_inventory.ps1`

Skrypt diagnostyczny PowerShell 5.1 dla Windows - nie wprowadza zmian.
`runmefirst.ps1` używa go do końcowego sprawdzenia wymagań hosta. Podaje
wersję buildu i architekturę Windows, ustawienia długich ścieżek, Git, Python,
CMake, Ninja, GNU Arm, GNU RISC-V, OpenOCD, picotool, rozszerzenia VS Code
oraz opcjonalne sprawdzenia zakończeń linii w repozytorium. Niepowodzenie
obowiązkowej kontroli daje niezerowy kod wyjścia. `-Json` zwraca
ustrukturyzowane rekordy, `-RepoPath` włącza sprawdzenia checkoutu, a
`-FirmwareOnly`
pokazuje pozycje dotyczące edytora, ale traktuje je jako opcjonalne. Skrypt nie
zmienia konfiguracji hosta i może służyć jako samodzielne narzędzie
diagnostyczne.

### `third_party/update_components.sh`

Główny punkt wejścia do zarządzania zależnościami. Jest to adapter zgodności
dla `scripts/component_manager.py all`, który przetwarza piętnaście
komponentów bazowych w kolejności zależności zadeklarowanej przez
`config/tooling/managed_components.json`:

1. BearSSL
2. cJSON
3. LodePNG
4. TJpg_Decoder
5. FatFs
6. Unity
7. lwIP
8. littlefs
9. BTstack
10. Driver Semtech SX126x
11. FreeRTOS-Kernel
12. Pico SDK
13. PMD CPD
14. picotool
15. Łańcuch narzędzi RISC-V

ESP-IDF jest szesnastym zarządzanym komponentem, lecz jest instalowany tylko
na żądanie,
ponieważ jego checkout, rekurencyjne submoduły i narzędzia targetu są duże.
Skrypt obsługi ESP-IDF przygotowuje go przy pierwszym użyciu; dedykowana
konfiguracja jest dostępna przez `scripts/ensure_esp_idf.sh --enable` lub
`JH_ENABLE_ESP_IDF=1`.

Tryb normalny doprowadza każdą zarządzaną instalację do wersji zapisanej w
konfiguracji. `--verify-only` nie wykonuje pobierania, ekstrakcji, zastąpienia
checkoutu ani buildu. Weryfikacja picotool obejmuje jego wymagane
polecenia oraz możliwości USB/podpisywania włączone przez aktualnie dostępne
zależności.
Układ wersji zapisanych w repozytorium i katalogów opisano w dokumencie
[Zarządzane komponenty zewnętrzne](../../../third_party/README.pl.md).

### `runalltests.sh`

Pełna lokalna kontrola jakości. Przed uruchomieniem dziewięciu etapów skrypt
wywołuje `scripts/sync_generated.py --write` dla wersjonowanych plików dotyczących
modułów, płytek, przykładów, głównej konfiguracji VS Code i SBOM. Lokalne
uruchomienie odświeża więc deterministycznie generowane pliki, a w podsumowaniu
ponownie wymienia zmienione artefakty. Opcja `--check-generated` przełącza ten
krok w tryb tylko do odczytu. CI korzysta z tego samego skryptu w trybie
sprawdzania, dzięki czemu lista generatorów jest utrzymywana w jednym miejscu.
Opcje `-j N`, `--jobs N` i `-jN` określają liczbę równoległych zadań buildu.
Kontrola obejmuje:

1. weryfikacja wymaganych narzędzi i zarządzanych komponentów;
2. testy hosta, w tym opcjonalny zestaw FreeRTOS POSIX;
3. testy Clang ASan/UBSan i krótkie kontrole libFuzzer przez ten sam skrypt,
   którego używa CI;
4. Valgrind memcheck;
5. cppcheck;
6. clang-tidy dla kodu hosta/współdzielonego oraz backendu STM32, używający
   zarówno bazy danych `JH_STM32_HOST_SANITY` kompilatora hosta, jak i
   prawdziwej bazy danych ARM;
7. wykrywanie duplikatów PMD CPD w implementacjach C/C++ utrzymywanych w
   repozytorium oraz w skryptach Python;
8. buildy STM32, RP2040/RP2350, natywnego FreeRTOS, profilu funkcji RP
   oraz czyste buildy ESP32-S3/ESP-IDF z walidacją artefaktów;
9. każdy zadeklarowany przykład RP, buildy natywnych testów parytetu
   oraz przykłady STM32.

Skrypt na starcie usuwa tylko swoje zarządzane drzewa `.build/gate`,
`.build/examples` oraz `.build/tests`. Kończy działanie przy pierwszej
nieudanej bramce.
Etap 4 uruchamia każdy bezpośrednio zarejestrowany natywny test wykonywalny
C/C++ oznaczony jako `memcheck`. `MEMCHECK_REQUIRED_TESTS` zawiera obowiązkowy,
krytyczny podzbiór i zapobiega niezauważonemu pominięciu tych testów. Testy
skryptów w Pythonie, CMake i shellu są
wykluczone: opakowanie ich interpretera nadrzędnego mierzyłoby to narzędzie
hosta, a nie skompilowane krzyżowo firmware lub procesy potomne.
Valgrind korzysta ze sprawiedliwego planowania wątków, dzięki
czemu natywne testy planisty FreeRTOS POSIX są uwzględnione bez zawieszania
się. Postęp CTest jest wyświetlany bez filtrowania zarówno w terminalu,
jak i do `.build/gate/logs/jh_memcheck.log`.

### `scripts/run_sanitizer_fuzz.sh`

Wspólny skrypt sanitizerów dla Linuksa, używany przez lokalny etap 3 i job CI
`sanitizer-fuzz`. Wyszukuje dostępny toolchain Clang, z numerem wersji w nazwie
lub bez niego, odtwarza build w `.build/`, włącza ASan, UBSan i libFuzzer oraz uruchamia
kompletny zestaw CTest hosta z wykrywaniem wycieków i natychmiastowym
zatrzymaniem po wykryciu niezdefiniowanego zachowania, a następnie wykonuje
krótkie testy fuzz dla parserów HTTP, WebSocket i multipart.
`--build-dir`, `--jobs` oraz `--fuzz-runs` wybierają zarządzane wyjście i
obciążenie; `--check-tools` tylko sprawdza dostępność Clanga.

### `vscode/entry/jh-vscode` oraz `jh-vscode.cmd`

Skrypty startowe dla Uniksa i Windows uruchamiają ten sam publiczny punkt
wejścia w Pythonie i wspólny interfejs CLI projektu firmware. Wersja dla
Windows sprawdza Python 3 wraz z pyserial i przekazuje argumenty CLI oraz kod
wyjścia. Konfiguracja firmware domyślnie używa Ninja, przekazuje
aktywny interpreter Python, eksportuje bazę poleceń kompilacji oraz ustala
ścieżki picotool i toolchainu właściwe dla platformy. Natywne drzewa CMake na
Windows używają krótkiego katalogu buildu przygotowanego podczas konfiguracji
hosta, a artefakty końcowe zachowują ścieżki zapisane w manifeście.

`debug-tools` podaje zweryfikowany OpenOCD, GDB obsługujący ARM,
katalog główny skryptów oraz konfigurację targetu używaną przez
Cortex-Debug. Generowane ustawienia dla Linuksa wybierają `gdb-multiarch`;
Windows używa GDB GNU Arm zainstalowanego przez skrypt konfigurujący host.

Akcje, opcje,
zabezpieczenia urządzenia oraz zachowanie monitora są udokumentowane
wyłącznie w
[Wejście JaszczurHAL do VS Code](../../../vscode/README.pl.md). Zasady dotyczące
manifestu, wykrywania źródeł, targetu, płytki, cache i artefaktów opisuje
[Proces obsługi projektu firmware](../../pl/FwProjectWorkflow.md).

## Skrypty buildu

### `scripts/build_rp_native_lib.sh`

Kompiluje JaszczurHAL przy użyciu oficjalnego Pico SDK. Obsługiwane targety
to:

| Target skryptu | Platforma Pico SDK | Domyślna płytka |
|---|---|---|
| `rp2040` | `rp2040` | `pico` |
| `rp2350-arm` | `rp2350-arm-s` | `pico2` |
| `rp2350-riscv` | `rp2350-riscv` | `pico2` |

Skrypt przygotowuje Pico SDK i picotool. Dodatkowo przygotowuje
FreeRTOS-Kernel dla `--freertos` oraz łańcucha narzędzi RISC-V dla
`rp2350-riscv`. Może skompilować przenośną aplikację przy pomocy
`--example <directory>`.

Domyślnie każdy build sprawdza bibliotekę statyczną, artefakty ELF/BIN/UF2,
symbole punktu wejścia rdzenia oraz opcjonalny firmware przykładu.
`--library-only` kompiluje wyłącznie target CMake `JaszczurHAL` i sprawdza, czy
archiwum `libJaszczurHAL.a` nadaje się do linkowania. Domyślny katalog wyjściowy to
`.build/static/<target>/<board>/`.

Ważne opcje to `--target`, `--platform`, `--board`, `--sdk-dir`,
`--toolchain`, `--picotool-dir`, `--picotool-build-dir`, `--example`,
`--freertos`, `--library-only`, `--project-config`, powtarzalne `-D`,
`--output`, `--clean` oraz `--jobs`.
Oba katalogi wyjścia buildu muszą pozostać poniżej `.build/`.

### `scripts/build_stm32_lib.sh`

Kompiluje statyczną bibliotekę STM32G474 przy użyciu wbudowanego łańcucha
narzędzi GNU Arm. Akceptuje konfigurację projektu, powtarzalne definicje HAL,
niestandardowy plik toolchain CMake oraz opcjonalną ścieżkę do
FreeRTOS-Kernel.

Domyślne wyjście:

```text
.build/static/stm32g474/nucleo-g474re/libJaszczurHAL.a
```

`--freertos` lub jawna definicja `HAL_ENABLE_FREERTOS` wywołuje
`ensure_freertos_kernel.sh` przed konfiguracją CMake. Ważne opcje to
`--project-config`, powtarzalne `-D`, `--freertos`, `--freertos-kernel`,
`--output`, `--toolchain`, `--clean` oraz `--jobs`.

### `scripts/build_esp_idf.py`

Skrypt obsługujący projekty dla targetów, których deskryptor płytki wybiera
providera `esp-idf`. Udostępnia trzy akcje:

| Akcja | Zachowanie |
|---|---|
| `build` | Opcjonalnie usuwa wybrane wyjście przy `--clean`, generuje wejścia projektu/płytki/SDK, kompiluje przy użyciu ESP-IDF w wersji wskazanej przez repozytorium, zapisuje informacje o pochodzeniu toolchainu oraz waliduje artefakty. |
| `artifacts` | Ponownie waliduje istniejący build i zapisuje deterministyczny manifest `jh_esp_idf_artifacts.json` bez wywoływania kompilatora. |
| `flash` | Ponownie sprawdza istniejący build, wymaga `--port` i uruchamia flashowanie ESP-IDF z pełnym zestawem obrazów i przesunięć. Następnie sprawdza log flashowania i manifest. |

`--project` jest wymagane. `--target` domyślnie to `esp32s3`; jego deskryptor
targetu wybiera `waveshare-esp32-s3-zero`, gdy pominięto `--board`.
`--output` musi pozostać poniżej katalogu głównego `.build` projektu lub
repozytorium. Powtarzalne argumenty `--source` zastępują automatyczne
wykrywanie; w przeciwnym razie skrypt uwzględnia obsługiwane pliki w
katalogu głównym projektu i rekurencyjnie pod `src/`. Powtarzalne argumenty
`--feature` i `--define` rozszerzają konfigurację projektu. `--idf-dir` lub
`JH_ESP_IDF_DIR` wybiera dokładny, zgodny zewnętrzny checkout.

Skrypt odczytuje bezpośrednio `boards/` i rejestr modułów. Moduły wymagane
przez target są dodawane do ostatecznego zestawu. Moduł żądany bezpośrednio
lub jako zależność, którego nie ma w `supportedFeatures`, powoduje błąd
`[JH-CFG-UNSUPPORTED]`. Lista dozwolonych funkcji ESP32-S3 zawiera wymagany
FreeRTOS, dostarczone flagi peryferiów Fazy 2 oraz graf sieci/usług Fazy 3.
Podstawę tworzą źródła systemu, synchronizacji, GPIO, ADC, prostego PWM,
portu szeregowego/debug oraz timera. Skrypt odpowiada też za ustawienie
`HAL_PROVIDE_APP_ENTRY`, dokładne selektory targetu i płytki, domyślne
wartości generowanego `sdkconfig` oraz kontrolowany graf komponentów.
Zapisuje rozwiązane listy źródeł i zależności do generowanego wejścia CMake,
którego używa komponent ESP-IDF.

Manifest wyjściowy używa wyłącznie ścieżek względnych do buildu.
Zapisuje uporządkowane obrazy flash i skróty; artefakty buildu; dane
targetu, płytki, funkcji, partycji i `sdkconfig`; wersję/commit ESP-IDF;
rzeczywiste wersje kompilatora, CMake, Ninja, IDF Python i esptool; oraz
skrót pliku `tools.json` właściwy dla wybranej wersji.
`scripts/build_esp_idf_phase0.py` jest adapterem zgodności, który przekazuje
głównemu skryptowi starsze argumenty projektów testowych.

### `scripts/build_rp_native_parity_fixtures.sh`

Kompiluje `tests/hardware/rp_usb_multicore` oraz `tests/hardware/rp_sdlogger`
przez standardowy proces `jh-vscode` dla:

- RP2040/Pico;
- RP2350 ARM/Pico 2;
- RP2350 RISC-V/Pico 2;
- bare-metal i FreeRTOS na każdym targecie.

Czyści wyłącznie dwa zarządzane drzewa buildu testów poniżej
`.build/hardware/`. `--jobs N` kontroluje równoległość CMake. Skrypt jest
bramką buildu; uruchomienie odpowiadających mu weryfikatorów Python nadal
wymaga fizycznych płytek, a dla SDLogger - karty SD SPI.

### `scripts/lib/build_artifacts.sh`

Wewnętrzny moduł shell dołączany przez wszystkie trzy pomocniki buildu.
Definiuje:

- `jh_build_root <repo>` do normalizacji `<repo>/.build`;
- `jh_resolve_build_output <repo> <requested> <default-relative>` do
  normalizacji ścieżki wyjściowej i odrzucania wszystkiego poza `.build/`.

Jest to biblioteka, nie samodzielne polecenie.

Po pełne wymagania targetu, opcje, wyjścia oraz ręczne odpowiedniki CMake
zobacz [Build biblioteki JaszczurHAL](../../pl/lib_compilation.md).

## Skrypty komponentów zarządzanych

`scripts/component_manager.py` zawiera wieloplatformową implementację operacji
Git `clone`/`fetch` oraz sprawdzania ref, origin i submodułów. Obsługuje również
pobieranie i weryfikację SHA-256 archiwów, rozpakowywanie ZIP/`tar.gz`, atomową
wymianę instalacji, manifesty zawartości i znaczniki wersji. Pliki
`ensure_*.sh` są uniksowymi adapterami zgodności, które przekazują dotychczasowe
argumenty CLI do tego menedżera w Pythonie. Metadane walidacji komponentów,
domyślna kolejność i przypisanie adapterów znajdują się w wersjonowanym modelu
`config/tooling/managed_components.json`.

Dedykowane pomocniki wczytują wersje zapisane w
`third_party/*_version.conf`. Normalnie używaj
`third_party/update_components.sh`; wywołuj pojedynczego pomocnika tylko dla
konkretnego buildu lub do diagnostyki.

### Wspólne zasady dotyczące kopii roboczych

Katalog zarządzany przez Git zawsze odpowiada dokładnie wskazanemu commitowi.
Jeśli go brakuje, repozytorium jest klonowane ze wskazanego ref. Katalog z innym
commitem albo katalog niebędący repozytorium Git w zarządzanej lokalizacji jest
zastępowany. `--verify-only` zgłasza niezgodność bez wprowadzania zmian.

Zarządzane katalogi oparte na archiwach używają dokładnie określonego skrótu
SHA-256 oraz deterministycznego manifestu wyodrębnionych plików.
Brakująca lub zmodyfikowana instalacja jest zastępowana w trybie normalnym i
odrzucana przez `--verify-only`.

Ścieżki FreeRTOS, Pico SDK i picotool dostarczone przez użytkownika są
traktowane jako zarządzane zewnętrznie. Są weryfikowane i nie są zastępowane
przez ich dedykowane pomocniki.

### `scripts/ensure_bearssl.sh`

Synchronizuje `third_party/BearSSL` z
`third_party/bearssl_version.conf`, weryfikuje dokładny commit oraz wymagane
nagłówki/źródła i obsługuje `--verify-only`, `--repo-root` oraz `--dir`.
`--enable` i `--force` są akceptowane dla jednolitego interfejsu updatera.

### `scripts/ensure_cjson.sh`

Synchronizuje `third_party/cJSON` z `third_party/cjson_version.conf` i
weryfikuje dokładny commit, licencję, źródła rdzenia oraz źródła narzędziowe.
Opcje odzwierciedlają pomocnika BearSSL.

### `scripts/ensure_lodepng.sh`

Synchronizuje `third_party/lodepng` z `third_party/lodepng_version.conf` i
weryfikuje czysty, dokładny commit, licencję, nagłówek oraz implementację.
Opcje odzwierciedlają pomocnika BearSSL.

### `scripts/ensure_jpeg.sh`

Synchronizuje `third_party/TJpg_Decoder` z `third_party/jpeg_version.conf` i
weryfikuje czysty, dokładny commit, licencję oraz rdzeń Tiny JPEG
Decompressor. Opcje odzwierciedlają pomocnika BearSSL.

### `scripts/ensure_fatfs.sh`

Synchronizuje `third_party/FatFs` z dokładnego commita repozytorium projektu
`jaszczurtd/ff16`, zapisanego w
`third_party/fatfs_version.conf`. Repozytorium zawiera niezmienione
archiwum R0.16 autorstwa ChaN. Pomocnik weryfikuje pochodzenie repozytorium,
dokładny commit, wymagane pliki źródłowe oraz licencyjne i obsługuje
`--verify-only`, `--repo-root` oraz `--dir`.

### `scripts/ensure_unity.sh`

Synchronizuje `third_party/Unity` z `third_party/unity_version.conf` i
weryfikuje czysty, dokładny commit, pochodzenie repozytorium, licencję oraz
źródła rdzenia frameworka. Opcje odzwierciedlają pomocnika BearSSL.

### `scripts/ensure_lwip.sh`

Synchronizuje `third_party/lwip` z `third_party/lwip_version.conf`. Oprócz
dokładnego commita i wymaganych ścieżek, weryfikuje makra
major/minor/revision lwIP względem skonfigurowanej wersji. Opcje
odzwierciedlają pomocnika BearSSL.

### `scripts/ensure_littlefs.sh`

Synchronizuje `third_party/littlefs` z
`third_party/littlefs_version.conf`. Weryfikuje dokładny commit, wymagane
źródła rdzenia i licencję oraz skonfigurowaną główną i poboczną wersję API
littlefs. Natywne buildy RP i STM32G474 kompilują ten zarządzany
checkout bezpośrednio. Opcje odzwierciedlają pomocnika BearSSL.

### `scripts/ensure_btstack.sh`

Synchronizuje `third_party/BTstack` z `third_party/btstack_version.conf`
przez `python3 scripts/component_manager.py component btstack`. Opcje
odzwierciedlają pomocnika BearSSL.

### `scripts/ensure_sx126x.sh`

Synchronizuje `third_party/sx126x_driver` z
`third_party/sx126x_driver_version.conf` przez
`python3 scripts/component_manager.py component sx126x`. Weryfikuje czysty,
dokładny commit, licencję Clear BSD, zestaw źródeł bazowego drivera i
wersji oraz nagłówki HAL/status/rejestry. Opcje odzwierciedlają pomocnika
BearSSL.

### `scripts/ensure_freertos_kernel.sh`

Synchronizuje lub weryfikuje FreeRTOS-Kernel oraz jego wymagane porty
RP/STM32. Jeśli nie wystąpi żaden z poniższych warunków, skrypt nic nie robi.
Uruchamia się, gdy:

- podano `--enable`, `--freertos`, `--force` lub `--verify-only`;
- `EXTRA_HAL_DEFINES` zawiera `HAL_ENABLE_FREERTOS`; lub
- `HAL_ENABLE_FREERTOS` jest obecne w środowisku.

`--kernel-dir` oraz `JH_FREERTOS_KERNEL_DIR` wybierają zewnętrzny checkout,
który jest weryfikowany, ale nie zastępowany. Zarządzane submoduły oraz
wersja jądra są również sprawdzane. Natywne, bezpośrednie integracje CMake
RP i STM32G474 wywołują `scripts/component_manager.py` bezpośrednio; ten
skrypt shell jest punktem wejścia zgodności używanym przez skrypty
buildu biblioteki statycznej.

### `scripts/ensure_pico_sdk.sh`

Synchronizuje lub weryfikuje oficjalny Pico SDK i inicjalizuje submoduły
wymienione w `third_party/pico_sdk_version.conf`. Jest włączany przez
`--enable`, `--native`, `--force`, `--verify-only` lub
`JH_ENABLE_PICO_SDK`.

`--sdk-dir` oraz `JH_PICO_SDK_DIR` wybierają zewnętrzny checkout.
`--no-submodules` pomija skonfigurowaną inicjalizację submodułów, natomiast
`--with-submodules "A B"` nadpisuje listę dla tego wywołania.

### `scripts/ensure_picotool.sh`

Synchronizuje źródła picotool i kompiluje plik wykonywalny względem
wybranego Pico SDK. Źródło znajduje się pod `third_party/picotool`; pliki
generowane oraz plik wykonywalny domyślnie trafiają do
`.build/tools/picotool/`.

Jest kompilowany ponownie, gdy zmieni się checkout źródeł, wersja podawana
przez picotool jest błędna, obsługa USB staje się dostępna lub SDK zapewnia
obsługę podpisywania, której brakowało w starszym buildzie. `--rebuild`
wymusza czysty, ponowny build. `--verify-only` sprawdza zarówno źródło, jak i
plik wykonywalny bez ich zmiany.

Pomocnik jest włączany przez `--enable`, `--build`, `--force`,
`--verify-only`, `--rebuild` lub `JH_ENABLE_PICOTOOL`. Jego katalog
buildu musi pozostać poniżej `.build/`.

### `scripts/ensure_pmd.sh`

Instaluje lub weryfikuje binarną dystrybucję PMD 7.26.0 wskazaną w
`third_party/pmd_version.conf`. Menedżer weryfikuje plik ZIP na podstawie
SHA-256,
śledzi pełny manifest rozpakowanych plików, wybiera skrypt startowy dla danej
platformy i sprawdza wersję podaną przez PMD. Wymagane jest środowisko Java;
`runmefirst.sh` dla Linuksa instaluje domyślny runtime bez interfejsu
graficznego.

### `scripts/ensure_riscv_toolchain.sh`

Instaluje gotowy łańcuch narzędzi `riscv32-unknown-elf` w wersji wskazanej
przez repozytorium,
firmy Raspberry Pi dla natywnego targetu `rp2350-riscv`. Dobiera archiwum
wydania do architektury hosta i rozpakowuje je do
`third_party/riscv-toolchain`, zapisuje stempel komponentu oraz weryfikuje
główną wersję GCC.

Jeśli plik wykonywalny, manifest zawartości, tożsamość archiwum lub stempel
różnią się od konfiguracji zapisanej w repozytorium, tryb normalny zastępuje instalację.
`--verify-only` nie wykonuje pobierania ani ekstrakcji. Archiwa o
zweryfikowanej autentyczności są dostępne dla Linuksa x86-64 i AArch64 oraz
natywnego Windows AMD64.

## Skrypty przykładów i wsparcia VS Code

### `scripts/examples_dispatcher.py`

Używa wersjonowanego rejestru przykładów z `config/tooling/examples.json` i
udostępnia pięć subpoleceń:

| Polecenie | Zachowanie |
|---|---|
| `generate` | Regeneruje manifest, ustawienia VS Code, zadania, konfigurację uruchamiania oraz referencję skrótów klawiszowych każdego przykładu. |
| `generate-template` | Regeneruje współdzielone fragmenty ustawień, zadań, rozszerzeń oraz skrótów klawiszowych pod `vscode/examples`. |
| `check-template` | Kończy się niepowodzeniem, gdy współdzielone fragmenty lub plik `.vscode` któregokolwiek przykładu w rejestrze różni się od wyniku współdzielonych generatorów. |
| `list` | Wypisuje każdy zarejestrowany projekt z rozwiniętymi `targets` i `gateTargets`. |
| `build` | Kompiluje obsługiwane przykłady i warianty przez `vscode/entry/jh-vscode`. |

`build` wymaga `--target` z jedną z wartości `rp2040`, `rp2350-arm`,
`rp2350-riscv` lub `stm32g474`. Powtarzalne `--example` ogranicza
uruchomienie, `--gate` ogranicza je do konfiguracji bazowych/wariantów,
których generowane `gateTargets` zawierają żądany target, `--jobs`
kontroluje równoległe projekty przykładów, a `--verbose` zapisuje wywołane
polecenia w zarządzanych logach na przykład poniżej `.build/examples`.

Polecenie `generate` korzysta z rejestru JSON, natomiast `build` odczytuje
wygenerowane manifesty. Akcja `list` wyświetla aktualne pełne
macierze oraz macierze domyślnej bramki bez utrzymywania tutaj duplikatów
liczników.
Przykłady RISC-V WiFi pozostają wykluczone, dopóki RP2350 RISC-V + CYW43 jest
nieobsługiwane.

Macierz targetów, interfejs aplikacji i polecenia buildu opisano w dokumencie
[Przykłady JaszczurHAL](../../../examples/README.pl.md).

### `scripts/sync_generated.py`

Jeden skrypt obsługuje wszystkie wersjonowane pliki generowane. `--write`
odświeża rejestr funkcji, statyczny rejestr płytek,
pliki VS Code przykładów, główne pliki VS Code oraz SBOM repozytorium.
`--check` wywołuje ich tryby weryfikacji tylko do odczytu
i kończy się niepowodzeniem, jeśli brakuje pliku wyjściowego albo jest on
nieaktualny. Przed uruchomieniem skrypt zapisuje stan plików wersjonowanych oraz
nieignorowanych, a po zakończeniu wypisuje wykryte zmiany.
`--report-file <path>` zapisuje tę końcową listę również do pliku dla skryptów,
takich jak `runalltests.sh`.

### `scripts/generate_board_config.py`

Sprawdza deskryptory JSON targetów, płytek i ich cech sprzętowych w `boards/`, a
następnie na podstawie wybranej pary target/płytka generuje konfigurację CMake
oraz metadane do odczytu maszynowego. CMake i testy płytek wywołują skrypt
bezpośrednio. `--validate-only` sprawdza kompletny rejestr.
`--list targets|boards` oraz `--default-board` udostępniają listy dostępnych
pozycji,
natomiast `--feature` i `--define` dodają sprawdzoną warstwę konfiguracji
buildu używaną dla generowanego wyjścia. `--output-dir` i `--output-root`
muszą pozostać wewnątrz drzewa buildu wskazanego przez kod wywołujący.
Definicje providera i backendu są zapisywane spójnie w
`jh_board_resolved.json.boardCompileDefinitions`, generowanego
`JH_BOARD_COMPILE_DEFINITIONS` oraz makr `jh_board_config.h` do
bezpośredniego użycia przez kompilator. Generowana referencja sygnatury
linkowania GCC/Clang używa symbolu głównego oznaczonego atrybutami
`constructor, used`, dzięki czemu
niezgodność targetu, płytki lub modułów powoduje błąd linkowania także przy
włączonym usuwaniu nieużywanych sekcji.

Generowany nagłówek udostępnia również identyfikator wybranego deskryptora
targetu, backend, nazwy MCU i podtypu, opis CPU i liczbę rdzeni, obecność
FPU oraz całkowity i użyteczny rozmiar RAM przez makra `HAL_TARGET_*`.
Informacje o bieżącej konfiguracji systemu są odczytywane bezpośrednio z tych
makr. Pojemność flash przeznaczonego na program, właściwa dla danej płytki,
jest dostępna jako
`HAL_BOARD_EXPECTED_FLASH_BYTES`.

`--write-static` odświeża wersjonowane `jh_board_registry.h`,
`jh_board_fallback_config.h` oraz rejestr CMake komponentów płytki. Dwa
pierwsze pochodzą z `boards/`; projekcja CMake pochodzi z
`config/tooling/board_components.json`. `--check-static` odrzuca brakujące
lub nieaktualne pliki. CI uruchamia sprawdzenie niezależnie od generowania
płytki dla każdego buildu.

### `scripts/generate_hal_features.py`

Waliduje zamkniętą przestrzeń nazw `HAL_ENABLE_*` / `HAL_DISABLE_*` oraz
niezależny od targetu graf zależności pod `config/features/`. `--write`
atomowo odświeża wersjonowany, produkcyjny nagłówek C oraz mechanizm
rozwiązywania zależności w CMake,
natomiast `--check` porównuje je bez zapisywania. `--lint` akceptuje
powtarzalne argumenty `--input-root` i sprawdza bezpośrednio pliki
`hal_project_config.h` oraz manifesty projektów pod kątem nieznanych
symboli, nieobsługiwanych wartości `=0` oraz bezpośrednich żądań symboli
pochodnych. Odrzuca również warunkowe definicje modułów poza pasującym
strażnikiem `#ifndef` oraz nieskalarne listy definicji CMake. Wykryte problemy
domyślnie kończą polecenie niepowodzeniem; `--report-only` jest jawnym,
ręcznym trybem diagnostycznym.

`--effective` korzysta z mechanizmu `jh-vscode`, aby ustalić zadeklarowane
targetów, profili targetu i wariantów bez odczytywania ignorowanego przez
git, lokalnego stanu płytki. Sprawdza ograniczenia oraz aktywne żądania
powtórzone po uwzględnieniu pierwszeństwa warstw. Standardowy plik
`.vscode/jaszczurhal.project.json` tworzy zadeklarowane osie; niesparowany
`hal_project_config.h` z co najmniej jednym żądaniem modułu HAL tworzy
pojedynczą konfigurację bez osi. Samodzielne nagłówki bez żądanych modułów i
manifesty referencyjne są analizowane tylko bez rozwiązywania konfiguracji.

`--resolution-output <path>` zapisuje deterministyczne `requestedFeatures`,
`resolvedFeatures`, skróty pełnego zbioru zależności oraz pochodzenie żądań
bezpośrednich dla każdej efektywnej konfiguracji. Test rejestru zamraża skrót macierzy w
`config/effective-features-baseline.json`. Każdy rekord opisuje sprawdzoną,
unikalną krotkę target/płytka/żądanie dla preprocesorów C oraz sprawdzony,
unikalny zestaw żądań dla niezależnego od targetu mechanizmu CMake.

Generowany nagłówek C jest dołączany przez `hal_config.h` i rozwija każdą
niezależną od targetu, przechodnią implikację. Jego generowana sekcja
`HAL_CONFIG_VERBOSE` wypisuje każdy aktywny, zarejestrowany moduł po
uruchomieniu pozostałych reguł konfiguracji. Generowany mechanizm CMake
udostępnia ten sam pełny zbiór do wyboru źródeł i zależności RP oraz
STM32G474. Skrypt ESP-IDF również rozwiązuje wszystkie zależności, a następnie
sprawdza listę dozwolonych `supportedFeatures` targetu przed skonfigurowaniem
minimalnego grafu komponentów określonego przez konfigurację.

Generowanie płytki
używa rozwiązanego zestawu dla `featureHash` i sygnatury linkowania,
zachowując przy tym zestaw bezpośredni jako `requestedFeatures`.

`jh-vscode` rozwiązuje rejestr po nałożeniu profilu manifestu i wariantu,
udostępnia wynik przez `featureResolution` i używa ostatecznego zestawu podczas
wstępnej kontroli oraz podejmowania decyzji OTA. Bezpośrednie żądania przekazuje do
CMake.

Warunkowe wartości domyślne, wybór providera, sprawdzanie cech płytki
oraz ograniczenia targetu pozostają w `hal_config.h`. CI uruchamia `--check`
oraz ścisłą analizę konfiguracji źródłowej i wynikowej, po czym publikuje
deterministyczny raport. Zainstalowane pakiety RP i STM32G474 zawierają
generowane nagłówki modułów i płytki, rozwiązany JSON płytki, nagłówek sygnatury
linkowania oraz źródło referencyjne; projekt korzystający bezpośrednio z
kompilatora może skompilować i zlinkować te artefakty bez wywoływania Pythona.

### `scripts/board_registry.py`

Moduł importowany przez inne skrypty, który przekształca sprawdzone deskryptory
`boards/` w model targetu i płytki używany przez `jh-vscode`, generatory
projektów oraz mechanizm obsługi przykładów. Celowo nie zawiera niezależnego
rejestru ani interfejsu wiersza poleceń; pliki deskryptorów pozostają
miarodajnym źródłem danych.

### `scripts/tooling_contract.py` oraz `scripts/repository_layout.py`

Moduły do wczytywania wersjonowanych modeli danych z
`config/tooling/`. `tooling_contract.py` waliduje wspólny schemat i pola o
zadeklarowanych typach; `repository_layout.py` udostępnia nazwane metadane
archiwów oraz wersjonowane ścieżki plików generowanych. Katalogi domenowe pozostają
oddzielnymi dokumentami JSON zamiast jednego globalnego modułu tekstowego.
Wykaz danych, polecenia generujące pliki oraz zasady utrzymania formatu są
zdefiniowane w [Interfejsach narzędziowych](#interfejsy-narzędziowe).

### `scripts/vscode_task_config.py`

Moduł źródłowy używany do generowania rozszerzeń VS Code, opisu
skrótów klawiszowych, definicji zadań, pola wyboru płytki oraz
zarządzanych profili Cortex-Debug. Dostarcza również pomocników migracji i
synchronizacji używanych przez `sync-board-picker`. Generowane projekty,
samodzielny generator projektów oraz testy dryfu importują te funkcje
zamiast utrzymywać osobne szablony JSON. Zachowanie widoczne dla użytkownika
każdego generowanego zadania jest udokumentowane w
[Generowanych zadaniach VS Code](../../../vscode/README.pl.md#generowane-zadania-vs-code).

### `vscode/tools/create-vscode-example.py`

Generuje samodzielny projekt firmware obsługiwany przez wspólny mechanizm
buildu, wraz z manifestem,
aplikacją blink, konfiguracją projektu HAL, konfiguracją uruchamiania,
współdzielonymi poleceniami zadań dla Uniksa/Windows, rekomendacjami
rozszerzeń oraz opisem skrótów klawiszowych. Generowane ustawienia VS
Code obejmują `cmake.configureSettings` dla początkowego targetu i płytki,
co pozwala CMake Tools bezpośrednio skonfigurować ten mechanizm.
`--target` i `--board` wybierają początkowy profil. `--force`
zastępuje wyłącznie pliki zarządzane przez generator w żądanym
katalogu projektu; `--dry-run` wypisuje ścieżki bez ich zapisywania.

### `vscode/tools/manage_vscode_extensions.py`

Odczytuje współdzieloną listę rekomendacji rozszerzeń i sprawdza wybrany
profil VS Code przy pomocy `code --list-extensions`. Tryb domyślny jest
tylko do odczytu i kończy się niezerowym kodem, gdy brakuje rekomendacji.
`--install` wypisuje brakującą listę i prosi o potwierdzenie przed
wywołaniem `code --install-extension`. `--install --yes` zapisuje jawną,
nieinteraktywną zgodę. Polecenie weryfikuje kompletną listę po instalacji.
`--code` oraz `JH_VSCODE_CODE` wybierają konkretne polecenie VS Code.

### `vscode/tools/configure_cortex_debug.py`

Odczytuje zweryfikowany `host-environment.json` dla Windows, sprawdza, czy
istnieją pliki wykonywalne OpenOCD, GNU Arm GCC oraz sąsiadującego GDB, i
łączy ich ścieżki bezwzględne ze standardowym profilem użytkownika VS Code jako
ustawienia Cortex-Debug właściwe dla Windows. Podczas aktualizacji zachowywane są
niepowiązane ustawienia JSONC, komentarze, zagnieżdżenie oraz przecinki
końcowe. Tworzy `settings.json.jaszczurhal.bak` przed zmianą istniejącego
profilu i zastępuje plik ustawień atomowo. `--check` jest tylko do odczytu,
`--yes` potwierdza nieinteraktywną aktualizację, a `--settings` obsługuje
jawnie wybrany profil VS Code lub fixture testowy. `runmefirst.ps1` wywołuje
tego pomocnika automatycznie poza trybem `-FirmwareOnly`.

### `scripts/configure_ota_firewall.py`

W sposób idempotentny sprawdza i konfiguruje trwały dostęp przychodzący TCP dla
połączenia zwrotnego OTA po stronie hosta. Współdzielony punkt wejścia wybiera backend
Linux lub Windows, znajduje sieć RFC1918 na domyślnym interfejsie IPv4,
zawęża regułę do tego interfejsu i podsieci, i domyślnie wybiera TCP/8266.
Aktywne instalacje UFW i firewalld używają swojej natywnej, trwałej
konfiguracji. Jeśli nie są dostępne, Linux używa `iptables-nft`/`iptables` z
`iptables-save` oraz loadera rozruchowego `netfilter-persistent`,
włączanego przez systemd, gdy jest dostępny.
Niefiltrowana polityka `INPUT` już zezwala na połączenie zwrotne i nie wymaga
dodatkowego pakietu ani reguły.

W Windows backend akceptuje wyłącznie aktywną sieć, której profil
połączenia to `Private`. Zarządza jedną nazwaną, przychodzącą regułą
Windows Defender Firewall ograniczoną do tego profilu, aliasu interfejsu,
podsieci źródłowej RFC1918, TCP oraz wybranego portu lokalnego. Inspekcja i
planowanie działają bez podniesionych uprawnień; zastosowanie reguły wymaga,
by wywołujący ponownie uruchomił polecenie w już podniesionym PowerShell.
Pomocnik nigdy nie uruchamia procesu z podniesionymi uprawnieniami ani nie
zmienia profilu sieciowego.

Tryb interaktywny wypisuje pełny zakres reguły i pyta przed wprowadzeniem
zmiany. `--check` jest tylko do odczytu, `--dry-run` wypisuje kompletny
plan, `--interface` oraz `--network` nadpisują automatyczne wykrywanie
trasy, `--port` wybiera inny, stały port połączenia zwrotnego, a `--yes` obsługuje
świadome, nieinteraktywne wprowadzenie zmian. Akceptowane są wyłącznie sieci
IPv4 RFC1918, a konfiguracja odmawia wystawienia portu już używanego przez
proces nasłuchujący.

### `scripts/ota_firewall_common.py`

Wspólny moduł importowany przez backendy zapory OTA dla Linuksa i Windows.
Przechowuje sprawdzoną parę interfejs/podsieć, funkcje sprawdzające RFC1918
oraz wspólny typ błędu
`SetupError`. Bezpośredni wywołujący powinni używać
`scripts/configure_ota_firewall.py`, aby wybór platformy, zgoda oraz kody
wyjścia pozostały spójne.

### `scripts/ota_firewall_windows.py`

Wewnętrzny backend Windows Defender Firewall wybierany przez
`scripts/configure_ota_firewall.py`. Wykrywa aktywne prywatne sieci IPv4
przez NetTCPIP, parsuje istniejące reguły NetSecurity, waliduje zakres
interfejs/podsieć/port oraz tworzy lub weryfikuje trwałą regułę
przychodzącą wyłącznie po uzyskaniu zgody przez główny skrypt. Mechanizm
uruchamiania poleceń można zastąpić w testach, a cały moduł pozostaje wewnętrzny
względem publicznego punktu wejścia.

### `scripts/rp_ota_artifacts.py`

Wewnętrzny pomocnik pakowania natywnego firmware RP używany przez CMake.
`package` dodaje do pliku BIN aplikacji wersjonowany nagłówek OTA JaszczurHAL z
targetem, przesunięciem ładowania, generacją, wersją i skrótem SHA-256 danych.
Pole HMAC pozostaje niepodpisane, dopóki akcja wgrywania VS Code nie
zastosuje hasła projektu. `merge-uf2` łączy UF2 aplikatora rozruchu
kopiującego do RAM z UF2 aplikacji, odrzucając konfliktujące bloki adresów i
porządkując numerację bloków. Artefakty buildu pozostają w wyznaczonym
katalogu `.build/`. Zobacz
[Natywny proces OTA](../../pl/OTAWorkflow.md), gdzie opisano pełny sposób pracy
z tymi artefaktami.

### `scripts/vscode_library_workspace.py`

Obsługuje bibliotekę statyczną z poziomu głównego katalogu repozytorium w VS
Code. Akcja `select` sprawdza pary target/płytka
względem `boards/` i zapisuje aktywny profil w lokalnym stanie ignorowanym
przez git. `build`, `refresh-intellisense`, `install`, `clean` oraz
`config-dump` używają następnie tych samych ścieżek buildu i instalacji z
tego profilu.

Buildy RP wywołują `build_rp_native_lib.sh --library-only`,
buildy STM32G474 - `build_stm32_lib.sh`, a buildy mock
wybierają główny target CMake `hal_mock`. Każdy build eksportuje
`compile_commands.json`; akcje IntelliSense zapisują lokalny
`.vscode/c_cpp_properties.json` bez zmiany wersjonowanych ustawień. Clean usuwa
wyłącznie zarządzane drzewa buildu/instalacji aktywnego profilu.

`sync-vscode` deterministycznie zapisuje wersjonowane zadania, ustawienia,
rekomendacje rozszerzeń oraz opis skrótów klawiszowych katalogu
głównego z rejestru płytek. Użyj `sync-vscode --check`, by odrzucić dryf bez
zmiany plików.

Projekty firmware są obsługiwane osobno przez
`jh-vscode <action> --project <dir>`.

### `scripts/vscode_refresh_intellisense.sh`

Adapter zgodności dla wcześniejszego punktu wejścia IntelliSense
repozytorium. Przypisuje `mock`, `rp2040`, `rp2350-arm`, `rp2350-riscv`, `stm32`
lub `stm32g474` do domyślnej płytki, wybiera odpowiedni profil biblioteki i przekazuje obsługę
do `vscode_library_workspace.py refresh-intellisense`.

### `scripts/vscode_clear_build_artifacts.sh`

Ręczny pomocnik pełnego czyszczenia. Usuwa całe drzewo `.build/`
repozytorium i nic poza nim. Nie ma opcji. Usuwa to również zapisane w cache
buildy targetów, przykłady, testy, dane IntelliSense oraz
skompilowany plik wykonywalny picotool; ignorowane źródła komponentów pod
`third_party/` pozostają bez zmian. Główne zadanie VS Code `Project: Clean`
celowo używa zawężonej akcji dla przestrzeni roboczej biblioteki zamiast tego.

## Skrypty analizy statycznej i bezpieczeństwa

### `scripts/run_cpd.py`

Uruchamia zarządzany PMD Copy/Paste Detector dla źródeł C/C++ utrzymywanych w
repozytorium oraz plików Pythona w
`scripts/`. Każda grupa duplikatów C/C++ produkcyjnych, testowych lub
przykładowych od 100 tokenów oraz każda grupa skryptów Python od 50 tokenów
blokuje bramkę; nie ma listy bazowej ani akceptowanego długu. Implementacje
generowane i dostarczone przez firmy trzecie są wykluczone. Raport podaje również udział
zduplikowanych tokenów łącznie oraz dla zakresów mock, RP2040, STM32G474,
wspólnego kodu, pozostałego kodu przenośnego oraz skryptów Pythona. Nakładające
się zakresy tokenów są liczone tylko raz. Deterministyczne listy źródeł oraz
raporty XML są zapisywane do żądanego katalogu wyjściowego poniżej
`.build/`.

### `scripts/clang_tidy_files.py`

Odczytuje plik CMake `compile_commands.json`, wybiera źródła JaszczurHAL, usuwa
powtórzone wpisy buildu i
wypisuje zakotwiczone wyrażenia regularne plików dla `run-clang-tidy`.

Wymagane opcje to `--build-dir` oraz `--profile host|stm32`.
`--repo-root` kontroluje klasyfikację ścieżek. `--output-compile-db` zapisuje
przefiltrowaną, deterministyczną bazę danych. Dla wpisów STM32 skrypt ustawia
w Clangu target `arm-none-eabi` oraz dodaje zgłaszane przez kompilator systemowe
ścieżki nagłówków systemowych.

Jest to wewnętrzny pomocnik bramki jakości wywoływany przez
`runalltests.sh`, a nie ogólny formatter.

### `scripts/check_documentation_links.py`

Sprawdza znajdujące się w repozytorium cele i kotwice odnośników Markdown w
utrzymywanej dokumentacji. Zestaw testów CTest hosta rejestruje go jako
`test_documentation_links`, więc zwykłe lokalne bramki i bramki CI odrzucają
uszkodzone linki dokumentacji.

Uruchom go bezpośrednio przy pomocy:

```bash
python3 scripts/check_documentation_links.py .
```

Opcjonalny argument pozycyjny wybiera inny katalog główny repozytorium.

### `scripts/check_documentation_i18n_parity.py`

Sprawdza układ dwujęzycznej dokumentacji oraz każdą parę angielską/polską
w `doc/en/`, `doc/pl/`, `doc/api/en/` i `doc/api/pl/`, a także główne pliki
README i spisy dokumentacji. Odrzuca brakujące lub osierocone pliki,
placeholdery tłumaczeń, niewiarygodne różnice rozmiaru oraz rozbieżną strukturę
nagłówków, bloków kodu i symboli HAL/JH. `doc/HAL_FLAGS.txt` jest celowo
używany wspólnie przez obie wersje językowe i nie jest tłumaczony. Zestaw testów CTest hosta
rejestruje ten test jako
`test_documentation_i18n_parity`. Nagłówek angielskiego głównego README
dotyczący wymowy nazwy jest specyficzny dla tego języka i nie wymaga pustego
odpowiednika w polskiej wersji.

Uruchom go bezpośrednio przy pomocy:

```bash
python3 scripts/check_documentation_i18n_parity.py .
```

Opcjonalny argument pozycyjny wybiera inny katalog główny repozytorium.

### `scripts/generate_sbom.py`

Generuje deterministyczny SBOM CycloneDX 1.5. Odczytuje
`security/third_party.json`, rozwiązuje wersję JaszczurHAL z pliku
`VERSION` i zapisuje `security/sbom.cdx.json`.

`--inventory` oraz `--output` zastępują domyślne ścieżki wejścia i wyjścia.
`--check` generuje tymczasowy plik wynikowy, porównuje go z wybranym wyjściem
i kończy się niepowodzeniem bez modyfikowania wersjonowanego pliku, gdy brakuje
go lub jest nieaktualny. Generator używa wyłącznie standardowej biblioteki
Python.

### `scripts/check_sbom.sh`

Adapter zgodności wywołujący `scripts/generate_sbom.py --check`.
Wspólny skrypt `scripts/sync_generated.py --check` sprawdza aktualność tych
danych lokalnie i w CI.

### `scripts/check_vulnerabilities.sh`

Regeneruje wersjonowany SBOM, a następnie uruchamia skanery, które są już
zainstalowane:

- `osv-scanner` skanuje rekurencyjnie źródło repozytorium;
- gdy `JH_SECURITY_SCAN_SOURCE=1`, `cve-bin-tool` skanuje generowany SBOM
  CycloneDX.

Skrypt przeszukuje zarówno `PATH`, jak i `~/.local/bin`, nie instaluje
skanerów i ostrzega zamiast kończyć się niepowodzeniem wyłącznie z powodu
braku dostępnego skanera. Wykryte podatności i błędy działania skanerów nadal
powodują niepowodzenie polecenia.

Inwentarz, SBOM, CI, triage i zasady aktualizacji komponentów opisano w
[Łańcuchu dostaw bezpieczeństwa](../../pl/security_supply_chain.md).

## Skrypt zasobów

### `scripts/image_to_base64.py`

Odczytuje dowolny plik obrazu, koduje go w Base64 i generuje prawidłową
deklarację C `static const char[]`. Argument pozycyjny to obraz wejściowy.
Przydatne opcje to:

- `--output` / `-o` do zapisu do pliku zamiast standardowego wyjścia;
- `--name` / `-n` do wyboru prawidłowego identyfikatora C;
- `--line-width` do kontroli zawijania generowanego literału tekstowego.

Błędnie napisana opcja `--otput` pozostaje aliasem zgodności; nowe
polecenia powinny używać `--output`.

Użycie PNG jest udokumentowane w
[API LodePNG](18_LodePNG.md#skrypt-zasobów-png-do-base64).
Użycie JPEG jest udokumentowane w
[API JPEG](19_JPEG.md#skrypt-zasobów-jpeg-do-base64).

## Powiązana dokumentacja

- [Kompilacja biblioteki JaszczurHAL](../../pl/lib_compilation.md) opisuje
  wymagania, opcje, wyjścia oraz ręczne odpowiedniki CMake dla biblioteki
  statycznej oraz natywnego buildu RP.
- [Proces obsługi projektu firmware](../../pl/FwProjectWorkflow.md) opisuje
  manifesty firmware obsługiwane przez wspólny mechanizm buildu, wykrywanie źródeł, wybór
  targetu i płytki, zasady przechowywania cache, wgrywanie oraz pliki generowane.
- [Integracja JaszczurHAL z VS Code](../../../vscode/README.pl.md) opisuje
  interfejs wiersza poleceń `jh-vscode` oraz zadania VS Code.
- [Profile targetów i płytek](../../pl/boards_profiles_howto.md) opisują
  pola deskryptora oraz sposób łączenia domyślnych wartości rejestru z
  manifestami projektu.
- [Dziennik zmian wejścia VS Code](../../../vscode/CHANGELOG.md) rejestruje
  zaimplementowane możliwości procesu oraz decyzje dotyczące
  zgodności.
- [Środowisko wykonawcze Windows](../../../vscode/windows/runtime/README.pl.md)
  opisuje obecny zakres natywnego runtime Windows oraz
  pozostałą pracę nad adapterem urządzenia.
- [Natywny neutralny firmware RP](../../../vscode/neutral_fw/rp_native/README.pl.md)
  wyjaśnia obraz domyślnej tożsamości używany przez
  `jh-vscode clear-identity`.
- [Przykłady JaszczurHAL](../../../examples/README.pl.md) dokumentują rejestr
  przykładów, pokrycie targetów, interfejs wejścia aplikacji, warianty oraz
  polecenia buildu.
- [Zarządzane komponenty zewnętrzne](../../../third_party/README.pl.md)
  opisują wersje zapisane w repozytorium, ignorowane instalacje, działanie
  mechanizmu aktualizacji oraz zasady korzystania z zewnętrznego checkoutu.
- [Bezpieczeństwo łańcucha dostaw](../../pl/security_supply_chain.md)
  opisuje generowanie SBOM, skanery podatności, politykę CI oraz zasady
  aktualizacji/triage.
