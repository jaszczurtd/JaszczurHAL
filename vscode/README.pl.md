# Punkt wejścia VS Code JaszczurHAL

Ten katalog zawiera wspólne narzędzia do obsługi projektów firmware JaszczurHAL
w VS Code: buildy w trybach Release i Debug, wgrywanie standardowe, wgrywanie
UF2, flashowanie przez ESP-IDF, monitor portu szeregowego, odświeżanie
IntelliSense, narzędzia do obsługi płytek i portów oraz usuwanie tożsamości USB.

Konfiguracja śledzona w repozytorium i używana po otwarciu jego głównego
katalogu w VS Code służy do osobnego procesu budowania biblioteki statycznej,
opisanego w
[instrukcji kompilacji biblioteki](../doc/pl/lib_compilation.md#workspace-repozytorium-i-vs-code).
Celowo nie traktuje ona głównego katalogu repozytorium jako projektu firmware.

Stabilny interfejs publiczny znajduje się w `entry/`. Pliki
`.vscode/tasks.json` w projektach powinny wywoływać `entry/jh-vscode` w
systemach Unix i `entry/jh-vscode.cmd` w Windows, natomiast zachowanie właściwe
dla projektu należy definiować w konfiguracji.
Przenośna logika interfejsu CLI, konfiguracji, CMake, artefaktów, OTA i trwałego
monitora znajduje się w `runtime/`. Operacje zależne od hosta korzystają z
adaptera platformy ładowanego dopiero wtedy, gdy jest potrzebny. Implementacja
dla Linuksa oraz punkty wejścia zgodności znajdują się w
`linux/runtime/`. Natywny adapter Windows rozpoznaje tożsamość portów COM,
identyfikuje procesy zajmujące zasoby, wykrywa woluminy BOOTSEL, niezawodnie
wgrywa pliki UF2 i blokuje równoczesne buildy. Katalogi runtime są szczegółami
implementacji.
Na każdym poziomie zawierają `__init__.py`, dlatego są zwykłymi pakietami
Pythona i `vscode.runtime` zawsze wskazuje kod z tego repozytorium.
Pełny model projektu firmware opisano w
[`doc/pl/FwProjectWorkflow.md`](../doc/pl/FwProjectWorkflow.md). Komplet wymagań
dotyczących natywnego OTA dla RP i ESP32-S3, w tym zapory sieciowej i procedur
odzyskiwania, znajduje się w
[`doc/pl/OTAWorkflow.md`](../doc/pl/OTAWorkflow.md).

## Skrypty uruchamiające na hostach

Oba skrypty uruchamiające wykonują `entry/jh_vscode.py`, który importuje
wspólny runtime. W systemach Unix używany jest `python3`. W Windows wybierany
jest pierwszy interpreter Python 3, który może zaimportować `pyserial`, według
następującej kolejności:

1. `JH_VSCODE_PYTHON`, jeśli ustawiono tę zmienną jawnie;
2. `.build/windows/venv/Scripts/python.exe` w głównym katalogu JaszczurHAL;
3. `py -3`;
4. `python`.

Jawna wartość `JH_VSCODE_PYTHON` musi wskazywać plik wykonywalny interpretera,
bez dodatkowych argumentów. Jeśli nie uda się znaleźć odpowiedniego
interpretera, program zwróci kod 8 i wyświetli diagnostykę konfiguracji hosta.
Za zarządzane środowisko odpowiada `runmefirst.ps1`; można również wskazać już
przygotowany interpreter za pomocą `JH_VSCODE_PYTHON`.

Generowane pliki `tasks.json` przechowują polecenie uniksowe w `command` oraz
dodają wariant dla Windows, który odczytuje ustawienie
`jaszczurhal.vscodeEntryWindows`. W generowanych plikach `settings.json`
ustawienie to wskazuje sąsiedni plik `jh-vscode.cmd`. Dzięki temu etykiety zadań
i argumenty są identyczne na obu hostach.

Operacje na urządzeniach w Windows korzystają z natywnych interfejsów API dla
portów COM i woluminów. Nie wymagają WSL, Git Bash ani warstwy zgodności z
POSIX.

## Interfejs CLI

```text
jh-vscode <action> [options]
```

Dostępne akcje:

```text
build
build-debug
upload
upload-uf2
upload-ota
ota-discover
monitor
monitor-probe
monitor-any
refresh-intellisense
clean
select-board
sync-board-picker
list-ports
change-port
clear-identity
config-dump
debug-tools
```

Ze względu na zgodność wsteczną `debug` jest akceptowane jako alias
`build-debug`, ale wyłącznie na potrzeby wczesnych migracji. Nowe zadania
powinny używać `build-debug`.

Akcja `change-port` pozwala wybrać port szeregowy interaktywnie lub przez
`--port`, po czym zapisuje go jako lokalne ustawienie użytkownika `uploadPort`
w pliku `.vscode/jaszczurhal.local.json`.

Polecenie `debug-tools --project <path> --json` znajduje zweryfikowane narzędzia:
GDB obsługujący Arm, plik wykonywalny OpenOCD, główny katalog skryptów oraz
skrypty interfejsu i targetu dla wybranej rodziny płytek. W generowanych
ustawieniach dla Linuksa wybierany jest `gdb-multiarch`, instalowany przez
`runmefirst.sh`. Skrypty są sprawdzane oddzielnie dla każdego targetu. Dzięki
temu OpenOCD z dystrybucji może obsłużyć wybrany profil STM32 nawet wtedy, gdy
jest starszy od wersji wymaganej przez inną rodzinę targetów.
W natywnym środowisku Windows ścieżki pochodzą z rekordu środowiska hosta,
utworzonego przez skrypt inicjalizacyjny. `runmefirst.ps1` zapisuje `openocd`
i `armToolchainPath` w ustawieniach Cortex-Debug właściwych dla Windows.
Wynik polecenia pozostaje dostępny do celów diagnostycznych.

Polecenie `list-ports --json` zwraca zachowaną dla zgodności wstecznej listę
ścieżek `bootsel` oraz ustrukturyzowane rekordy `bootselRecords`. Każdy rekord
zawiera punkt montowania, ścieżkę urządzenia, GUID woluminu Windows, etykietę i
system plików, o ile platforma udostępnia te dane.

Akcja `sync-board-picker` odświeża dane wejściowe `boardSelection` oraz
powiązane zadanie uruchamiane automatycznie po otwarciu katalogu, korzystając z
bieżącego rejestru `boards/`. Tworzy też lub naprawia zarządzane profile
Cortex-Debug w `launch.json` dla RP2040, RP2350 ARM oraz STM32G474/ST-Link.
Ścieżka pliku ELF pochodzi ze śledzonego manifestu projektu. Starsze profile
JaszczurHAL są migrowane, a konfiguracje o nazwach należących do użytkownika
pozostają bez zmian. W wygenerowanych projektach synchronizacja uruchamia się
po otwarciu zaufanego obszaru roboczego. VS Code może jednorazowo poprosić o
zgodę za pomocą polecenia `Tasks: Manage Automatic Tasks`. W razie potrzeby
target i płytkę można nadal
wybrać dynamicznie w terminalu przez `Project: Select board`.

## Generowane zadania VS Code

Generator zapisuje te same etykiety zadań i argumenty na obu hostach. W
systemach Unix korzysta z `jaszczurhal.vscodeEntry`, a wariant dla Windows - z
`jaszczurhal.vscodeEntryWindows`. Utrzymywane są następujące zadania:

| Zadanie | Akcja CLI | Działanie |
|---|---|---|
| `Project: Build` | `build` | Buduje aktywny target i płytkę w trybie Release, a następnie publikuje ich stabilne artefakty. Jest to domyślne zadanie budowania w VS Code. |
| `Project: Build (Debug)` | `build-debug` | Korzysta z osobnego cache CMake dla trybu Debug, publikuje plik ELF z symbolami debugowymi i poprzedza uruchomienie każdego zarządzanego profilu Cortex-Debug. |
| `Project: Upload` | `upload` | Buduje projekt i wgrywa go za pośrednictwem backendu aktywnego targetu: dla RP jest to plik UF2 po zweryfikowanym przejściu z CDC do BOOTSEL, dla STM32G474 - OpenOCD, a dla ESP32-S3 - zweryfikowany port USB Serial/JTAG. |
| `Project: Upload (UF2 / BOOTSEL)` | `upload-uf2` | Buduje obraz RP, sprawdza plik UF2 i kopiuje go na jeden zweryfikowany wolumin BOOTSEL. Odmawia działania, jeśli wybór woluminu jest niejednoznaczny. |
| `Project: Upload (OTA)` | `upload-ota --interactive` | Buduje obraz OTA właściwy dla targetu, wykrywa zgodne urządzenia z natywnym RP lub ESP-IDF, przeprowadza uwierzytelnienie, jeśli je skonfigurowano, i pyta o wybór, gdy trzeba jawnie wskazać urządzenie. |
| `Project: Discover OTA devices` | `ota-discover` | Wyświetla zgodne urządzenia OTA wraz z ich adresem, targetem, generacją obrazu, slotem i stanem rozruchu. |
| `Project: List ports` | `list-ports` | Pokazuje rekordy portów szeregowych, urządzenia pasujące do tożsamości projektu i kandydatów BOOTSEL, bez otwierania urządzenia. |
| `Project: Change port` | `change-port` | Pozwala interaktywnie wybrać port szeregowy i zapisuje go w lokalnej konfiguracji projektu ignorowanej przez Git. |
| `Project: Serial Monitor` | `monitor --lock-policy replace-own` | Uruchamia trwały monitor projektu; może zastąpić tylko inny zweryfikowany monitor JaszczurHAL, który zajmuje ten sam port. |
| `Project: Debug Probe Monitor` | `monitor-probe --lock-policy replace-own` | Uruchamia trwały monitor portu szeregowego dla skonfigurowanej tożsamości sondy debugowej. |
| `Project: Serial Monitor (Any)` | `monitor-any --lock-policy wait` | Czeka na dowolny odpowiedni port szeregowy i nigdy nie przejmuje portu zajętego przez inny proces. |
| `Project: Refresh IntelliSense` | `refresh-intellisense` | Uruchamia target generujący bazę poleceń kompilacji i zapisuje poprawioną bazę pod stabilną ścieżką używaną przez cpptools. |
| `Project: Clean` | `clean` | Po sprawdzeniu bezpieczeństwa ścieżek usuwa artefakty projektu i odpowiadające mu zarządzane drzewa CMake. |
| `Project: Clear USB Identity` | `clear-identity` | Buduje neutralny firmware RP i wgrywa go dopiero po zweryfikowaniu bieżącej tożsamości USB lub wybranego woluminu BOOTSEL zgodnie ze standardowymi zasadami bezpieczeństwa. |
| `Project: Config Dump` | `config-dump` | Wyświetla manifest po rozstrzygnięciu wszystkich wartości, lokalne nadpisania, target, płytkę, ścieżki, konfigurację wgrywania i wynik rozwiązywania zależności funkcji HAL. |
| `Project: Select board` | `select-board --interactive` | Pozwala wybrać target i płytkę w terminalu, a następnie zapisuje ten wybór lokalnie. |
| `Project: Select board (GUI)` | `select-board --selection ...` | Korzysta z wygenerowanego selektora VS Code i zapisuje lokalnie wybraną parę target/płytka. |
| `Project: Sync board picker` | `sync-board-picker` | Uruchamia się raz po otwarciu zaufanego katalogu, odświeża wartości selektora i tworzy lub naprawia zarządzane profile debugowe dla RP2040, RP2350 Arm i STM32G474, pozostawiając profile użytkownika bez zmian. |
| `Project: Build variant: <id>` | `build --variant <id>` | Pojawia się tylko przy zadeklarowanych wariantach przykładu i buduje wybrany wariant manifestu za pośrednictwem standardowego procesu publikowania artefaktów. |

Panel Run and Debug udostępnia trzy konfiguracje Cortex-Debug:

- `Project: Debug Firmware` dla RP2040 przez CMSIS-DAP/Picoprobe;
- `Project: Debug Firmware (RP2350 ARM)` przez CMSIS-DAP/Picoprobe;
- `Project: Debug Firmware (STM32G474 / ST-Link)` przez ST-Link.

Każda z nich najpierw uruchamia `Project: Build (Debug)`, a następnie ładuje
powstały plik ELF z ustawieniami sondy, OpenOCD i resetu właściwymi dla profilu.

Wspólne opcje:

```text
--project <path>       Katalog modułu firmware.
--target <id>          Zastąp aktywną rodzinę targetu w tym wywołaniu.
--board <id>           Zastąp aktywną płytkę w obrębie targetu.
--variant <id>         Wybierz wariant przykładu zadeklarowany w manifeście.
--selection <t:b>      Zapisz wybór targetu i płytki; etykiety GUI są dozwolone.
--interactive          Zapytaj o target i płytkę w terminalu.
--port <port>          Zastąp skonfigurowany port wgrywania lub monitora.
--bootsel-volume <id>  Wybierz katalog główny dysku BOOTSEL lub GUID woluminu Windows.
--host <address>       Pomiń wykrywanie OTA i użyj tego adresu urządzenia.
--baud <baud>          Szybkość monitora portu szeregowego, domyślnie 115200.
--lock-policy <mode>   Zasada blokowania monitora: wait, replace-own, replace-any.
--allow-unverified-port
                       Eksperckie wgrywanie przez jawnie wybrany port szeregowy,
                       który nie pasuje do skonfigurowanej tożsamości USB.
--verbose              Włącz szczegółowe komunikaty.
--json                 Zwracaj dane w formacie maszynowym, jeśli jest obsługiwany.
--help                 Wyświetl pomoc.
--version              Wyświetl wersję narzędzia.
```

Dla akcji wykonywanych na module `--project` oznacza katalog modułu firmware,
a nie główny katalog repozytorium. Przykłady:

```text
jh-vscode build --project /home/user/projects/router-reset/reseter
jh-vscode build --project /home/user/projects/Fiesta/src/Clocks
jh-vscode clear-identity --project /home/user/projects/Fiesta/src/ECU
```

Akcje `build`, `build-debug`, `debug-tools`, `upload`, `upload-uf2`, `upload-ota`,
`ota-discover`, `refresh-intellisense`, `clean`, `change-port` i
`clear-identity` wymagają `--project`. Jeśli nie można jednoznacznie ustalić
modułu docelowego, akcje komunikujące się z urządzeniem muszą zakończyć się
błędem, zanim uzyskają dostęp do portów szeregowych, dysków BOOTSEL lub
artefaktów builda.

Projekty firmware dla RP i STM32 korzystają z `toolchain: "cmake"` oraz
wspólnej konfiguracji JaszczurHAL dla wielu targetów. `jh-vscode` rozstrzyga
aktywny target i płytkę, konfiguruje CMake, a następnie uruchamia utrzymywane
targety CMake dla firmware:

```text
firmware
firmware_debug
firmware_upload
firmware_compile_db
```

Domyślnie `jh-vscode` konfiguruje CMake z generatorem Ninja, eksportuje bazę
poleceń kompilacji i jawnie przekazuje aktualnie używany interpreter Pythona.
Wygenerowane i zmigrowane projekty ustawiają `cmake.sourceDir` w
`.vscode/jaszczurhal.project.json` na wspólną konfigurację obsługującą wiele
targetów, na przykład
`${project}/../../libraries/JaszczurHAL/cmake/jh_firmware_project`, a katalog
modułu przekazują jako `JH_PROJECT_DIR`. Ustawienie `cmake.generator` pozwala
jawnie wybrać generator. Samodzielne projekty utworzone przez
`tools/create-vscode-example.py` zapisują też początkowy cache tej konfiguracji
w `.vscode/settings.json` jako `cmake.configureSettings`, dzięki czemu CMake
Tools w VS Code może skonfigurować ją bezpośrednio.

W natywnym środowisku Windows używane są zweryfikowane narzędzia i krótki
katalog bazowy buildów przygotowane przez `runmefirst.ps1`. Cache CMake
pozostaje pod tą krótką ścieżką, natomiast końcowe artefakty firmware trafiają
do katalogu `buildDir` z manifestu. `refresh-intellisense` zapisuje poprawioną
bazę pod stabilną ścieżką artefaktów używaną przez VS Code. Przed rozpoczęciem
builda wcześniejsze pliki firmware są usuwane ze stabilnych ścieżek używanych
do wgrywania. Nowe artefakty są publikowane dopiero po pomyślnym zbudowaniu
wybranego targetu.

Targety `rp2040`, `rp2350-arm` i `rp2350-riscv` są budowane bezpośrednio przy
użyciu oficjalnego Pico SDK. Firmware RP udostępnia interfejs USB CDC
zaimplementowany przez HAL. Jeżeli wybrano port szeregowy, standardowa akcja
`upload` zwalnia monitor projektu, wykonuje sekwencję DTR przy 1200 bps, czeka na pojedynczy
dysk BOOTSEL i kopiuje na niego plik UF2. STM32G474 korzysta z natywnej
definicji targetu w rejestrze, obsługuje warianty firmware bare-metal i FreeRTOS
oraz wgrywanie przez OpenOCD.

Projekty ESP32-S3 używają `toolchain: "esp-idf"`. Rejestr targetów wskazuje
`scripts/build_esp_idf.py`, ścieżkę manifestu wyjściowego, strategię wgrywania
ESP-IDF, wymaganą funkcję FreeRTOS oraz VID/PID interfejsu programującego
wybranej płytki. Akcja `build` uruchamia produkcyjny skrypt, po czym niezależnie
ponownie sprawdza `jh_esp_idf_artifacts.json`. Zachowuje kompletną i
uporządkowaną listę obrazów bootloadera, tabeli partycji i aplikacji, zamiast
sprowadzać ją do pojedynczego stabilnego pliku firmware. Akcja `upload`
ponownie buduje projekt, zwalnia jego monitor i przekazuje zweryfikowany port do
akcji `flash` skryptu. Akcja `monitor` przełącza się na zasady ponownego łączenia
dla ESP i, gdy nie przypięto jawnie portu, łączy się ponownie z jedynym pasującym
urządzeniem USB Serial/JTAG.

Akcja `refresh-intellisense` buduje i sprawdza artefakty ESP-IDF, a następnie
dostosowuje wygenerowaną bazę poleceń kompilacji do cpptools, zachowując
kompilator Xtensa i jego flagi. ESP32-S3 nie udostępnia `build-debug` ani
zarządzanych profili Cortex-Debug.

## Dodawanie plików źródłowych projektu

Wykrywanie źródeł i kompletne reguły `JH_PROJECT_SOURCES` opisano w
[sekcji o dodawaniu plików źródłowych](../doc/pl/FwProjectWorkflow.md#dodawanie-plików-źródłowych-projektu).
Jest to jedyne źródło przykładów układu projektu i manifestu.

## Generator nowego projektu

Skrypt `tools/create-vscode-example.py` tworzy samodzielny projekt firmware dla
VS Code, skonfigurowany przede wszystkim z myślą o CMake. Projekt można umieścić
obok pozostałych repozytoriów firmware:

```bash
libraries/JaszczurHAL/vscode/tools/create-vscode-example.py \
  --output /home/user/projects/jaszczurhal-vscode-example
```

Wygenerowany projekt zawiera niewielką aplikację migającą diodą, lokalne pliki
`.vscode/` oraz `hal_project_config.h` z `HAL_PROVIDE_APP_ENTRY`. Nie zawiera
lokalnego pliku `CMakeLists.txt` dla firmware. W manifeście `cmake.sourceDir`
wskazuje wspólną konfigurację JaszczurHAL dla wielu targetów, a początkowy wybór
`target`/`board` jest zapisany razem z projektem. Ustawienia przekazują ten sam
zestaw początkowych wartości cache do CMake Tools, aby edytor mógł konfigurować projekt
bezpośrednio. Inny początkowy target i płytkę można wybrać za pomocą `--target`
i `--board`:

```bash
libraries/JaszczurHAL/vscode/tools/create-vscode-example.py \
  --output /home/user/projects/jaszczurhal-stm32-example \
  --target stm32g474 \
  --board nucleo-g474re
```

Flagi funkcji w nagłówku projektu, wynikowym profilu targetu i aktywnym
wariancie przykładu mogą mieć postać samego `HAL_ENABLE_X` albo
`HAL_ENABLE_X=1`. Jeszcze przed konfiguracją CMake `jh-vscode` odrzuca wartość
`=0` i wyrażenia generatora CMake, zgłaszając `[JH-CFG-VALUE]`. W danych
wejściowych będących listami definicji każdy wpis `HAL_ENABLE_*` musi być
osobnym prostym tokenem, a kolejne wpisy należy rozdzielać średnikami. Białe
znaki nie rozdzielają wielu definicji funkcji.

Po zastosowaniu aktywnego profilu targetu i wariantu `jh-vscode` sprawdza, czy
nie użyto symboli nieznanych lub pochodnych, oblicza wynikające z nich
zależności przechodnie oraz weryfikuje wymagania i konflikty zapisane w
rejestrze. Wynik jest widoczny w danych `featureResolution` zwracanych przez
`config-dump`. Obejmują one
`registryDigest`, `requestedFeatures`, `resolvedFeatures`,
`resolvedFeaturesDigest` oraz informację `provenance` o pochodzeniu każdego
żądanego wpisu. Funkcje wskazane przez użytkownika pozostają danymi wejściowymi
CMake, natomiast testy wstępne i ocena dostępności OTA korzystają z zestawu po
rozwiązaniu zależności.

Deskryptor `esp32s3` dopuszcza wymagane przez target `HAL_ENABLE_FREERTOS`,
dostarczone flagi peryferiów z fazy 2 oraz graf zależności sieci i usług z fazy 3. Backendy
systemu, synchronizacji, GPIO, ADC, prostego PWM, portu szeregowego, debugowania
i timera są częścią konfiguracji bazowej targetu. Produkcyjny skrypt odrzuca
funkcje wskazane jawnie lub włączone pośrednio w wyniku zależności, jeśli nie
znajdują się na liście dozwolonej przez deskryptor, zgłaszając
`[JH-CFG-UNSUPPORTED]`.

Nagłówek projektu zawiera wyłącznie makra i jest wczytywany przed automatycznym
wykrywaniem targetu. Nie należy dołączać w nim nagłówków JaszczurHAL ani używać
pochodnych selektorów targetu lub płytki. Definicje funkcji wpływające na wybór
plików źródłowych muszą mieć bezwarunkową postać `#define HAL_ENABLE_X` albo
`#define HAL_ENABLE_X 1`. Dozwolony jest jedynie warunek `#ifndef HAL_ENABLE_X`
chroniący definicję tego samego symbolu. Nie należy umieszczać definicji funkcji
w żadnej innej gałęzi warunkowej, ponieważ mechanizm wstępnego zbierania
definicji analizuje plik tekstowo.

Wygenerowany projekt korzysta z `jh-vscode` do tych samych operacji co projekty
po migracji:

```bash
../libraries/JaszczurHAL/vscode/entry/jh-vscode build --project "$PWD"
../libraries/JaszczurHAL/vscode/entry/jh-vscode build-debug --project "$PWD"
../libraries/JaszczurHAL/vscode/entry/jh-vscode refresh-intellisense --project "$PWD"
../libraries/JaszczurHAL/vscode/entry/jh-vscode select-board --project "$PWD" --interactive
```

Akcja `build-debug` ustawia `CMAKE_BUILD_TYPE=Debug` w osobnym cache CMake dla
każdej pary targetu i płytki. Wynik publikuje pod tymi samymi stabilnymi ścieżkami
artefaktów co `build`, dlatego zadania wgrywania zawsze korzystają z ostatniego
udanego builda.

Generowane konfiguracje Cortex-Debug zawierają profile dla RP2040, RP2350 Arm i
STM32G474 z jawnie wskazanymi plikami konfiguracyjnymi OpenOCD. Rozszerzenie
odnajduje GDB na podstawie skonfigurowanej ścieżki toolchaina Arm. Przed
uruchomieniem profilu należy wybrać właściwy target i płytkę przez
`Project: Select board`, ponieważ wspólne zadanie poprzedzające debugowanie
buduje aktualnie wybrany target i płytkę.

Profile RP używają `interface/cmsis-dap.cfg` i odpowiedniego skryptu targetu RP.
Pico w trybie BOOTSEL jest wyłącznie urządzeniem docelowym, a nie sondą, więc
OpenOCD wymaga osobnego CMSIS-DAP/Picoprobe podłączonego do pinów SWD płytki.
Profil STM32G474 korzysta z wbudowanego w NUCLEO-G474RE interfejsu ST-Link za
pośrednictwem
`board/st_nucleo_g4.cfg`, łączy się przy wymuszonym resecie sprzętowym i nie
wymaga zewnętrznego okablowania sondy.

Generowane profile RP ustawiają również sprawdzoną prędkość adaptera: 5 MHz dla
RP2040 i 2 MHz dla RP2350. Bez tego ustawienia OpenOCD obniża prędkość do
domyślnych 100 kHz, a
wykrywanie pamięci flash RP2350 może w Windows przekroczyć domyślny limit czasu
zdalnego połączenia GDB.

Kompletny wygenerowany projekt należy umieścić poza
`libraries/JaszczurHAL/vscode/`. Katalog `vscode/examples/` służy do
niewielkich przykładów konfiguracji, a nie do przechowywania pełnego projektu
firmware w repozytorium.

Wspólne fragmenty konfiguracji i wszystkie przykładowe pliki `.vscode` śledzone
w repozytorium korzystają ze wspólnego narzędzia do generowania artefaktów:

```bash
python3 scripts/sync_generated.py --check
python3 scripts/sync_generated.py --write
```

Pierwsze polecenie nie modyfikuje plików i nadaje się do CI. Drugie odświeża
wszystkie śledzone pliki wygenerowane z rejestru.

## Rozszerzenia VS Code

Aktywny profil VS Code można porównać ze wspólną listą zalecanych rozszerzeń za
pomocą polecenia:

```bash
python3 vscode/tools/manage_vscode_extensions.py
```

Brakujące pozycje można zainstalować interaktywnie:

```bash
python3 vscode/tools/manage_vscode_extensions.py --install
```

Opcje `--install --yes` stanowią jawną zgodę na instalację bez interakcji,
przeznaczoną dla skryptów inicjalizacyjnych i automatyzacji. Po każdej instalacji
wynik jest sprawdzany poleceniem `code --list-extensions`. Jeśli polecenia VS
Code nie ma w `PATH`, należy użyć `--code <path>` albo `JH_VSCODE_CODE`.

## Skróty klawiszowe VS Code

Pliki `.vscode/keybindings.reference.json` w projektach mają wyłącznie charakter
informacyjny. VS Code nie wczytuje ich automatycznie, a repozytorium nie
udostępnia mechanizmu lokalnego włączania skrótów. Działają one tylko wtedy, gdy
odpowiednie wpisy znajdują się w rzeczywistym pliku ustawień użytkownika VS
Code:

```text
~/.config/Code/User/keybindings.json
```

Po migracji projektu należy upewnić się, że skróty globalne wywołują aktualne
etykiety zadań:

```text
Ctrl+Shift+1  Project: Build
Ctrl+Shift+2  Project: Upload
Ctrl+Shift+3  Project: Serial Monitor
Ctrl+Shift+4  Project: Upload (UF2 / BOOTSEL)
Ctrl+Shift+5  Project: Debug Probe Monitor
Ctrl+Shift+6  Project: Refresh IntelliSense
Ctrl+Shift+7  Project: Clean
Ctrl+Shift+8  Project: Upload (OTA)
Ctrl+Shift+9  Project: Config Dump
Ctrl+Shift+Alt+1  Project: Select board (GUI)
Ctrl+Shift+Alt+2  Project: Select board
Ctrl+Shift+Alt+3  Project: Discover OTA devices
```

Starsze przypisania, takie jak `Project: Monitor (persistent)` czy
`Project: Monitor (Debug Probe)`, po usunięciu z projektowego `tasks.json`
aliasów zachowujących zgodność wsteczną otworzą okno „Show all tasks".

Jeśli `Ctrl+Shift+3` nie uruchamia monitora, należy najpierw sprawdzić właściwy
plik użytkownika `keybindings.json`. Poprawne przypisanie wygląda tak:

```json
{
    "key": "ctrl+shift+3",
    "command": "workbench.action.tasks.runTask",
    "args": "Project: Serial Monitor"
}
```

Jeżeli po zmianie pliku ustawień nadal używane jest stare przypisanie zapisane
w cache, należy przeładować okno VS Code.

Po udanym buildzie CMake wykonanym przez `build`, `upload` lub `upload-uf2`
narzędzie `jh-vscode` wyświetla zwięzłe zestawienie mapy pamięci ELF, o ile jest
dostępny artefakt `firmware.elf`. Zestawienie powstaje na podstawie
`arm-none-eabi-objdump -h`. Dzieli zaalokowane sekcje na grupy FLASH/XIP, SRAM,
PSRAM i OTHER oraz pokazuje ich położenie VMA/LMA, rozmiary i krótkie uwagi.
Dodatkowe dane można wyłączyć przez `JH_VSCODE_MEMORY_OVERVIEW=0`.

Monitor portu szeregowego domyślnie używa `--lock-policy wait`. Tryb
`replace-own` może zatrzymać wyłącznie inny monitor JaszczurHAL dla tego samego
projektu po sprawdzeniu wersjonowanego znacznika właściciela portu, PID i
identyfikatora konkretnego uruchomienia procesu. Starszy wariant zapisu
`replace-any` podlega temu samemu ograniczeniu i nigdy nie kończy obcego
procesu.

W projektach Pico z włączoną tożsamością USB monitor korzystający z portu
wybranego niejawnie może połączyć się ponownie z jedynym urządzeniem CDC o
zweryfikowanej tożsamości projektu, gdy zapisana ścieżka zniknie albo host
przydzieli inny numer `ttyACM` lub COM. Obejmuje to również zastąpienie płytki
innym egzemplarzem z tym samym firmware. Przy braku dopasowań lub więcej niż
jednym dopasowaniu monitor czeka zamiast zgadywać. Port podany jawnie przez
`--port` pozostaje przypięty do wskazanej ścieżki.

Przy wgrywaniu szeregowym w projektach z tożsamością USB akcja `upload`
sprawdza wybrany port przed jego otwarciem. W Linuksie łączy metadane pyserial z
aliasami by-id i ustrukturyzowanymi deskryptorami sysfs, także wtedy, gdy
pyserial nie zwraca pasującego rekordu. W Windows porty COM są wyliczane przez
pyserial, a do weryfikacji służą VID, PID, numer seryjny, producent, produkt,
interfejs, lokalizacja i HWID. Każde skonfigurowane stabilne pole musi być
zgodne; niepełne metadane pozostają niezweryfikowane. Pierwsze wgranie na
czystą płytkę obsługiwaną wyłącznie przez port szeregowy, której tożsamość ma
pochodzić z istniejącego firmware, wymaga jawnego polecenia z
`--allow-unverified-port --port <port>`. Domyślne zadania VS Code nie mogą
przekazywać tej flagi.

Tożsamość interfejsu programującego ESP32-S3 pochodzi z deskryptora wybranej
płytki, a nie z kopii przechowywanej w projekcie. Dla
`waveshare-esp32-s3-zero` natywny interfejs USB Serial/JTAG ma identyfikator
`303a:1001`. Jawnie podany port musi wskazywać urządzenie z tym VID/PID;
nieaktualna ścieżka lub niezgodne identyfikatory są odrzucane. Gdy port nie został wskazany,
akcje wgrywania i monitorowania mogą wybrać dokładnie jedno zweryfikowane
dopasowanie, a brak lub wielość dopasowań powoduje błąd.
`--allow-unverified-port` pozostaje opcją ekspercką, dostępną tylko razem z
jawnym `--port`.

## Kolejność rozstrzygania konfiguracji

Reguły wyboru targetu i płytki, nakładek manifestu, wartości zastępczych z
ustawień oraz pierwszeństwa stanu lokalnego opisano wyłącznie w sekcji
[Wybór targetu i konfiguracji](../doc/pl/FwProjectWorkflow.md#wybór-targetu-i-konfiguracji).

## Minimalny manifest projektu

Aktualny przykład manifestu i reguły nakładek znajdują się w sekcji
[Minimalny manifest](../doc/pl/FwProjectWorkflow.md#minimalny-manifest).
Do walidacji maszynowej służy `schema/jh_vscode_project.schema.json`.

## Tożsamość USB

Tożsamość USB oznacza deskryptory firmware widoczne na hoście jako producent i
produkt, na przykład w `lsusb`, `dmesg`, monitorze szeregowym VS Code,
`/dev/serial/by-id` lub metadanych portu COM w Windows. Manifest może dodatkowo
określać `usbVid`, `usbPid`, `usbSerialNumber`, `usbInterface` lub
`usbLocation`. Numer COM jest wyłącznie lokalnym wyborem zapisanym w
`.vscode/jaszczurhal.local.json`; nigdy nie jest traktowany jako stabilna
tożsamość.

Dla płytek ESP-IDF wpis
`boards/profiles/<board>.json.programming.usb` opisuje stałe VID/PID interfejsu
programującego.
`jh-vscode` przekazuje tę informację do wspólnego mechanizmu weryfikacji
wyłącznie w pamięci. Projekty nie mogą powielać jej w swoich manifestach.

W natywnych buildach RP tożsamość jest przekazywana przez zmienne cache CMake:

```text
-DJH_USB_MANUFACTURER="Jaszczur"
-DJH_USB_PRODUCT="Router Reset"
```

Jeśli tożsamość jest wyłączona lub niekompletna, build używa domyślnych
deskryptorów USB JaszczurHAL.

`clear-identity` jest osobną akcją. Wymaga `--project`, buduje neutralny firmware
z `neutral_fw/rp_native/` za pomocą Pico SDK dla wybranego natywnego targetu i
płytki, nie przekazuje zmiennych niestandardowej tożsamości USB do cache, po czym
wgrywa obraz na zweryfikowane urządzenie dostępne przez port szeregowy albo na
jednoznacznie wybrane urządzenie BOOTSEL.

Akcja `upload-uf2` obsługuje wyłącznie rodzinę RP i pliki UF2, a jej ręczny tryb
BOOTSEL jest celowo prosty: buduje projekt, sprawdza każdy 512-bajtowy blok UF2,
wymaga dokładnie jednego dozwolonego woluminu BOOTSEL i nie próbuje zgadywać,
gdy widocznych jest kilka urządzeń. W razie potrzeby Linux montuje przez
`udisksctl` jeden niezamontowany wolumin FAT spełniający kryteria. Windows
odczytuje etykietę dysku i system plików przez WinAPI, a GUID woluminu pozwala
rozpoznać go w stanie zapamiętanym przed operacją. Na obu hostach dozwolone są
wyłącznie etykiety `RPI-RP2`, `RP2350` i `RPI-RP2350`. Dla STM32/OpenOCD należy
używać uniwersalnej akcji `upload`.

Jeśli celowo podłączono kilka woluminów BOOTSEL, należy przekazać
`--bootsel-volume <drive-root-or-volume-guid>`. Tę samą wartość można zapisać
jako `bootselVolume` w ignorowanym przez Git pliku
`.vscode/jaszczurhal.local.json`; katalog główny dysku, na przykład `E:\`, jest
lokalny dla danego hosta Windows. Przed użyciem wskazanego woluminu nadal
sprawdzana jest jego etykieta i system plików FAT.

Dla natywnych targetów RP uniwersalna akcja `upload` korzysta ze
skonfigurowanego i zweryfikowanego portu CDC, aby wykonać reset przy 1200 bps,
a następnie stosuje te same reguły bezpiecznego wyboru pojedynczego dysku UF2.
Przed sekwencją resetu runtime zapamiętuje GUID-y widocznych woluminów i czeka
wyłącznie na nowy dozwolony wolumin albo na wolumin wskazany jawnie. Jeżeli
skonfigurowana ścieżka CDC jest nieaktualna, bo wybrana płytka znajduje się już
w trybie BOOTSEL, `upload` wykorzystuje jedyne widoczne urządzenie BOOTSEL,
zamiast odrzucać brak tożsamości szeregowej. Jeśli zastępcza płytka ma już
zgodny firmware, `upload` może zastąpić nieaktualną zapisaną ścieżkę jedynym
portem CDC o zweryfikowanej tożsamości USB projektu. Brak dopasowań lub kilka
dopasowań pozostają błędem. Jawne `--port` wyłącza oba te mechanizmy. Pierwsze
wgranie wciąż wymaga ręcznego BOOTSEL, ponieważ niezaprogramowana płytka nie
udostępnia interfejsu CDC umożliwiającego reset.

Gdy `upload` wykryje trwały monitor tego projektu na porcie wgrywania, wysyła
mu żądanie zwolnienia konkretnego portu, czeka na zamknięcie uchwytu, utrzymuje
krótkotrwały znacznik projektu podczas wgrywania, a po powrocie płytki pozwala
monitorowi połączyć się ponownie. Ograniczony mechanizm awaryjny może zatrzymać
tylko proces, którego PID i identyfikator uruchomienia nadal odpowiadają danym
w znaczniku właściciela. Stare znaczniki, ponownie wykorzystane PID-y i obce
procesy zajmujące port nigdy nie są kończone. Jeśli system operacyjny nie
potrafi ustalić procesu zajmującego port - jak w przypadku portów COM w Windows
- diagnostyka zajętego portu zawiera PID ze zweryfikowanego znacznika.

W Windows dane UF2 są kopiowane jako zwykły strumień. Przed zgłoszeniem sukcesu
program opróżnia bufor uchwytu docelowego i zamyka ten uchwyt. Nośnik tylko do
odczytu, zniknięcie dysku, niepełny zapis oraz ucięty lub niespójny artefakt UF2
kończą się kodem błędu wgrywania 6. Walidator akceptuje zarówno zwykłe
sekwencje dla jednej rodziny, jak i scalone obrazy OTA, których pojedyncza
globalna sekwencja bloków
obejmuje kilka identyfikatorów rodzin. Testy CI obejmują natywne WinAPI, scalone
obrazy OTA oraz proces kopiowania. Testy typu smoke na rzeczywistych
urządzeniach w Windows zakończyły się powodzeniem dla RP2040/Pico i
RP2350/Pico 2, obejmując
tożsamość woluminu, walidację i kopiowanie UF2, restart oraz zweryfikowane
ponowne połączenie CDC.

## Natywne OTA

`upload-ota` służy do aktualizacji sieciowej natywnych buildów dla płytek WiFi
z targetami `rp2040` i `rp2350-arm` oraz włączonym `HAL_ENABLE_OTA`. Akcja buduje
projekt, odnajduje jego artefakt `.ota`, podpisuje nagłówek obrazu
skonfigurowanym hasłem, przesyła obraz do slotu przeznaczonego na nową wersję i
czeka na potwierdzenie przyjęcia przez urządzenie. Hasło nie jest umieszczane w
niepodpisanym artefakcie builda. Dla targetu `rp2350-riscv` można zbudować
kontener OTA i moduł rozruchowy OTA, jednak bieżący profil płytki RISC-V nie ma
transportu CYW43, dlatego nie udostępnia działającej ścieżki wgrywania przez
sieć.

W projektach ESP-IDF akcja `upload-ota` wykonuje ten sam produkcyjny build i
wybór urządzenia, po czym sprawdza `jh_esp_idf_artifacts.json`, wymaga
`HAL_ENABLE_OTA` w zestawie funkcji po rozwiązaniu zależności oraz weryfikuje
plik BIN aplikacji względem rozmiaru i skrótu SHA-256 z manifestu. Wysyła samą binarną
zawartość aplikacji - nie tworzy ani nie podpisuje kontenera `.ota` dla RP.
ESP32-S3 zapisuje obraz na nieaktywną partycję aplikacji `two-ota-large` i
korzysta z mechanizmu ESP-IDF obejmującego rozruch próbny, zatwierdzenie i
wycofanie obrazu. Kod tej ścieżki jest gotowy i przechodzi kompilację oraz
linkowanie, ale nadal wymaga testów sprzętowych, testów przerwania transmisji,
wycofania obrazu i negatywnych scenariuszy bezpieczeństwa.

`ota-discover` rozsyła żądanie wykrywania JaszczurHAL i wyświetla nazwę hosta,
adres, target, port, rozmiar slotu, generację obrazu oraz tryb rozruchu.
Wgrywanie automatycznie wybiera jedno urządzenie zgodne z aktywnym targetem i
skonfigurowaną nazwą hosta. Jeśli pasuje kilka urządzeń, wygenerowane zadanie
używa `--interactive`. Automatyzacja powinna ustawić `ota.host` w manifeście lub
jawnie przekazać `--host <address>`.

Wspólne ustawienia `ota` oraz pole `artifacts.ota` dotyczące tylko RP opisano w
sekcji [Konfiguracja manifestu OTA](../doc/pl/FwProjectWorkflow.md#konfiguracja-manifestu-ota).
Sekrety należy przechowywać poza śledzonym manifestem, korzystając z
`ota.passwordEnv`. Hasło wpisane bezpośrednio w `ota.password` jest przeznaczone
wyłącznie do przykładów deweloperskich. Puste hasło jest odrzucane, chyba że
jawnie ustawiono
`ota.allowEmptyPassword` na `true`. Przy niepustym haśle host wymaga wymiany
AUTH2 opartej na HMAC-SHA256 i zgodnej ze wspólnym protokołem, a bezpośrednią
odpowiedź `OK` oraz starszą odpowiedź `AUTH` odrzuca. Ustawienie
`allowEmptyPassword` jawnie zezwala na nieuwierzytelnioną transmisję
deweloperską; nie ustawia hasła na urządzeniu. Host
używa jednego połączonego gniazda UDP do zaproszenia i AUTH2, akceptuje linie
sterujące wyłącznie w ściśle określonym formacie i przesyła dane dopiero wtedy, gdy adres
połączenia zwrotnego TCP odpowiada adresowi drugiej strony połączenia UDP,
który wybrał system operacyjny. Domyślnym portem połączenia zwrotnego z hostem
jest TCP/8266. Po jawnej zgodzie
`runmefirst.sh` może dodać trwałą regułę zapory dla tego portu, ograniczoną do
sieci LAN.

Integrację firmware, pierwszą instalację, reguły zapory na hoście, skróty
klawiaturowe, zatwierdzanie rozruchu próbnego, wycofywanie, odzyskiwanie i
diagnostykę opisuje dokument
[Obsługa natywnego OTA](../doc/pl/OTAWorkflow.md).

Dla RP pierwsza instalacja nadal wymaga scalenego pliku UF2 wgranego przez
BOOTSEL. Obraz zawiera moduł rozruchowy OTA i aplikację. Kolejne uruchomienia po
OTA są próbne: kod aplikacji powinien wywołać `hal_ota_confirm_boot_ex()` dopiero
po pomyślnym zakończeniu testów startowych i uruchomieniu wymaganych usług. W
przeciwnym razie moduł rozruchowy wycofa obraz po osiągnięciu ustawionego
limitu prób. Ręczny BOOTSEL pozostaje ścieżką odzyskiwania. W przypadku
ESP32-S3 odzyskiwanie polega natomiast na ponownym wgraniu kompletnego,
zweryfikowanego zestawu obrazów ESP-IDF przez USB Serial/JTAG.

Aplikacją referencyjną jest
[`examples/25_ota`](../examples/25_ota/README.pl.md). Jej wbudowany transport WiFi
obsługuje Pico W/RP2040 i Pico 2 W/RP2350 ARM. Obraz RP2350 RISC-V i moduł
rozruchowy OTA można zbudować, ale repozytorium nie zapewnia jeszcze obsługi
sieciowej CYW43 dla tej architektury.

## Kody wyjścia

```text
0   Powodzenie.
1   Błąd ogólny.
2   Nieprawidłowe użycie CLI.
3   Brak lub nieprawidłowa konfiguracja projektu.
4   Niejednoznaczny lub niebezpieczny wybór urządzenia.
5   Błąd builda.
6   Błąd wgrywania.
7   Błąd monitora, w tym brak pyserial na hoście.
8   Nieobsługiwana akcja lub ścieżka platformy albo niepełne zależności skryptu.
```

Kod 7 obejmuje również brak pyserial na hoście: podstawowy moduł monitora nadal
można zaimportować, a brak zależności jest zgłaszany komunikatem zamiast błędem
importu. Kod 8 obejmuje każdą operację hosta, której nie implementuje aktywny
adapter platformy, oraz skrypt uruchamiający w Windows bez działającego
interpretera Python 3 z pyserial.

## Generowane pliki i cache builda

Katalogi artefaktów, bazy poleceń kompilacji, generowane adaptery, izolację
cache według targetu i płytki, czyszczenie nieaktualnego cache oraz reguły
przypisywania kluczy cache opisano wyłącznie w sekcji
[Katalogi buildu i pliki generowane](../doc/pl/FwProjectWorkflow.md#katalogi-buildu-i-pliki-generowane).
