# Skrypty obsługi repozytorium JaszczurHAL

*Dostępne również [po angielsku](../en/00_scripts.md).*

Ten dokument jest centralnym indeksem skryptów, które konfigurują, kompilują,
walidują, pakują i obsługują JaszczurHAL. Obejmuje każdy skrypt pod `scripts/`
oraz główne punkty wejścia procesów znajdujące się w innych miejscach
repozytorium.

Uruchamiaj polecenia z katalogu głównego repozytorium, chyba że dana sekcja
mówi inaczej. Implementacja skryptu oraz jego wyjście `--help` są
rozstrzygające, gdy ten dokument i kod są ze sobą niezgodne.

## Główne punkty wejścia

| Cel | Polecenie | Rezultat |
|---|---|---|
| Przygotowanie stacji roboczej Debian/Ubuntu | `./runmefirst.sh` | Instaluje wymagania hosta, ARM, analizy, bezpieczeństwa, USB oraz workflow VS Code; synchronizuje zarządzane komponenty; konfiguruje hooki Git. |
| Przygotowanie natywnej stacji roboczej Windows | `powershell -NoProfile -ExecutionPolicy Bypass -File .\runmefirst.ps1` | Przygotowuje przypięte, zarządzane środowisko Python, natywne łańcuchy narzędzi, komponenty źródłowe, ścieżki użytkownika Cortex-Debug oraz test poprawności hosta Windows. |
| Synchronizacja zarządzanych zależności | `./third_party/update_components.sh` | Pobiera brakujące komponenty i zastępuje zarządzane instalacje różniące się od śledzonych przypięć. |
| Weryfikacja zależności bez ich zmiany | `./third_party/update_components.sh --verify-only` | Sprawdza wersje wszystkich zarządzanych komponentów, commity, wymagane pliki, stan archiwum PMD, zbudowany picotool oraz stempel łańcucha narzędzi RISC-V. |
| Odświeżenie wszystkich śledzonych plików generowanych | `python3 scripts/sync_generated.py --write` | Uruchamia generatory funkcji, płytek, przykładów, głównego VS Code oraz SBOM i wypisuje każdy plik zmieniony podczas synchronizacji. |
| Weryfikacja wszystkich śledzonych plików generowanych | `python3 scripts/sync_generated.py --check` | Uruchamia każdy generator w trybie weryfikacji tylko do odczytu i kończy się niepowodzeniem przy brakującym lub nieaktualnym wyjściu. |
| Uruchomienie pełnej bramki repozytorium | `./runalltests.sh` | Czyści katalogi robocze bramki i uruchamia testy, Valgrind, analizę statyczną, CPD, buildy targetów oraz buildy przykładów. |
| Obsługa projektu firmware | `vscode/entry/jh-vscode <action> --project <dir>` w Uniksie lub `vscode/entry/jh-vscode.cmd ...` w Windows | Dostarcza stabilny CLI buildu, wgrywania, monitorowania, wyboru płytki, IntelliSense oraz czyszczenia używany przez projekty VS Code. |
| Build lub flashowanie projektu ESP-IDF | `python3 scripts/build_esp_idf.py <action> --project <dir>` | Uruchamia akcję `build`, `artifacts` lub `flash`; ustala metadane targetu/płytki ESP; przygotowuje na żądanie przypięty SDK oraz waliduje relokowalny manifest wieloobrazowy. |
| Build przykładów przechowywanych w repozytorium | `scripts/examples_dispatcher.py build --target <target>` | Kompiluje manifesty przykładów przez tego samego dispatchera `jh-vscode` i CMake, który jest używany przez projekty firmware. |
| Build natywnych testów parytetu RP | `scripts/build_rp_native_parity_fixtures.sh` | Kompiluje testy USB wielordzeniowego i SDLogger dla wszystkich obsługiwanych natywnych kombinacji target/runtime. |

### Polityka artefaktów

Generowane artefakty buildu, których właścicielem jest repozytorium,
należą do `.build/`; zarządzane instalacje komponentów należą do
`third_party/`. Model katalogów, izolacja cache target/płytka oraz własność
plików generowanych są zdefiniowane w
[Katalogi buildu i pliki generowane](../../pl/FwProjectWorkflow.md#katalogi-budowania-i-pliki-generowane).

## Interfejsy narzędziowe

`config/tooling/` zawiera wersjonowane dane, których właścicielem jest
repozytorium, współdzielone przez skrypty, pliki generowane, CMake oraz kod
bootstrapu hosta. Każdy dokument JSON ma `schemaVersion: 1` i jednego
właściciela domeny:

| Plik danych | Własność |
|---|---|
| `artifacts.json` | Nazywa pliki metadanych archiwów oraz śledzone wyjścia generowane. |
| `board_components.json` | Definiuje prawidłowe komponenty płytek, providerów i wzajemnie wykluczające się sloty. |
| `examples.json` | Definiuje rejestr aktywnych przykładów przechowywanych w repozytorium. |
| `managed_components.json` | Definiuje zarządzane komponenty źródłowe/narzędziowe, metadane walidacji, domyślną kolejność oraz launchery zgodności. |

Kod Pythona wczytuje te dokumenty przez `scripts/tooling_contract.py`.
Nazwane ścieżki artefaktów są rzutowane przez `scripts/repository_layout.py`.
CMake nie parsuje JSON podczas zwykłej konfiguracji: generator płytek zapisuje
`cmake/generated/jh_board_components_registry.cmake` na podstawie
`board_components.json`.

Po zmianie danych komponentów płytki lub innego wejścia generatora, odśwież i
zweryfikuj wszystkie śledzone projekcje przez wspólny runner:

```bash
python3 scripts/sync_generated.py --write
python3 scripts/sync_generated.py --check
```

Trzymaj literały protokołu i formatu blisko operacji, których dotyczą. W
szczególności jawne argumenty `encoding="utf-8"` dokumentują format tekstu na
dysku i celowo nie są zastępowane globalną stałą tekstową. Komunikaty
skierowane do użytkownika oraz jednorazowe tokeny składniowe również
pozostają przy kodzie, który jest ich właścicielem.

## Orkiestratorzy na poziomie repozytorium

Te skrypty celowo znajdują się poza `scripts/`, ponieważ są punktami wejścia
workflow najwyższego poziomu.

### `runmefirst.sh`

Jednorazowa, idempotentna konfiguracja dla systemów podobnych do
Debian/Ubuntu. Skrypt:

- usuwa drzewo `.build/` repozytorium przed konfiguracją;
- instaluje kompilator, CMake, Ninja, Python, Java, Valgrind, clang-tidy,
  cppcheck, OpenOCD, `gdb-multiarch`, obsługę portu szeregowego, libusb oraz
  inne pakiety hosta;
- wywołuje `third_party/update_components.sh`;
- instaluje `osv-scanner` oraz `cve-bin-tool`;
- instaluje regułę udev dla dostępu USB BOOTSEL/picotool do RP2040/RP2350
  oraz portu `/dev/ttyACM*` w trybie aplikacji używanego przez automatyczny
  reset przy 1200 bps;
- sprawdza obecność trwałej reguły callbacku TCP/8266 OTA ograniczonej do
  sieci LAN i pyta przed zmianą zapory sieciowej lub instalacją
  `iptables-persistent`;
- konfiguruje hooki Git repozytorium;
- weryfikuje, że każde wymagane narzędzie jest dostępne.

Skrypt używa `sudo` dla pakietów systemowych, `/usr/local/bin`, reguły udev
oraz jawnie zatwierdzonej zmiany zapory sieciowej. Pobiera narzędzia i
zależności, więc wymaga dostępu do sieci. Dedykowany pomocnik zapory
sieciowej to `scripts/configure_ota_firewall.py`; obsługuje `--check`, jawne
`--interface` / `--network` oraz potwierdzone lub `--yes` provisioning.

### `runmefirst.ps1`

Idempotentna natywna konfiguracja dla Windows. Wypisuje swój kompletny plan
przed wprowadzeniem zmiany, używa krótkich, lokalnych dla użytkownika
katalogów głównych narzędzi/buildu, tworzy przypięte środowisko Python
3.12 z zweryfikowanym skrótem pyserial, synchronizuje komponenty źródłowe
oraz rozwiązuje CMake, Ninja, GNU Arm, GNU RISC-V, OpenOCD i picotool. Zgodne
narzędzia systemowe są używane ponownie, chyba że wybrano `-Force`. Systemowy
OpenOCD jest używany ponownie tylko wtedy, gdy jego wymagane skrypty interfejsu i
targetu również da się rozwiązać; w przeciwnym razie konfiguracja przechodzi
na uwierzytelnione zarządzane archiwum.
Zapisuje zweryfikowany zestaw plików wykonywalnych, zarządzany Python oraz
krótki katalog główny buildu w `.build/windows/host-environment.json` dla
współdzielonego runtime firmware. Tryb edytora dodatkowo
zachowuje i aktualizuje standardowy `settings.json` użytkownika VS Code o
specyficzne dla Windows ścieżki OpenOCD i GNU Arm dla Cortex-Debug; tworzy
odzyskiwalny plik `.jaszczurhal.bak` przed zmianą istniejących ustawień.

`-VerifyOnly` jest tylko do odczytu. `-ConfigureHost` jawnie zezwala na
naprawę udokumentowanych ustawień długich ścieżek, a `-InstallExtensions`
jawnie zezwala na zmiany profilu VS Code. `-FirmwareOnly` pozostawia
sprawdzenia edytora widoczne, ale opcjonalne dla bezgłowych (headless)
budowniczych firmware'u i CI, i pomija konfigurację profilu Cortex-Debug.
`-VerifyOnly` sprawdza skonfigurowane ścieżki debuggera bez zapisywania.
Skrypt nigdy się sam nie podnosi do uprawnień administratora. Zobacz
[Natywna konfiguracja dla Windows](../../pl/windows_setup.md) po wymagania
hosta, polecenia, ścieżki oraz aktualną granicę wsparcia.

### `scripts/windows_host_inventory.ps1`

Sonda PowerShell 5.1 dla Windows tylko do odczytu, używana przez
`runmefirst.ps1` do jego finalnego sprawdzenia wymagań hosta. Raportuje
build i architekturę Windows, ustawienia długich ścieżek, Git, Python,
CMake, Ninja, GNU Arm, GNU RISC-V, OpenOCD, picotool, rozszerzenia VS Code
oraz opcjonalne sprawdzenia zakończeń linii w repozytorium. Wymagane
niepowodzenia dają niezerowy kod wyjścia. `-Json` emituje ustrukturyzowane
rekordy, `-RepoPath` włącza sprawdzenia checkoutu, a `-FirmwareOnly`
pozostawia pozycje edytora widoczne, ale opcjonalne. Skrypt nigdy nie zmienia
hosta i jest również przydatny jako samodzielna diagnostyka konfiguracji.

### `third_party/update_components.sh`

Zwykły punkt wejścia zarządzania zależnościami. Jest to launcher zgodności
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

ESP-IDF jest szesnastym zarządzanym komponentem, ale pozostaje opt-in,
ponieważ jego checkout, rekurencyjne submoduły i narzędzia targetu są duże.
Produkcyjny runner ESP-IDF przygotowuje go przy pierwszym użyciu; dedykowana
konfiguracja jest dostępna przez `scripts/ensure_esp_idf.sh --enable` lub
`JH_ENABLE_ESP_IDF=1`.

Tryb normalny dopasowuje każdą zarządzaną instalację do jej śledzonej
konfiguracji. `--verify-only` nie wykonuje pobierania, ekstrakcji, zastąpienia
checkoutu ani buildu. Weryfikacja picotool obejmuje jego wymagane
polecenia oraz możliwości USB/podpisywania włączone przez aktualnie dostępne
zależności.
Zobacz [Zarządzane komponenty zewnętrzne](../../../third_party/README.md) po
układ przypięć i katalogów.

### `runalltests.sh`

Kompletna, lokalna bramka jakości. Przed uruchomieniem swoich ośmiu bramek,
wywołuje `scripts/sync_generated.py --write` dla śledzonych projekcji
funkcji, płytek, przykładów, głównego VS Code oraz SBOM. Lokalne
uruchomienie naprawia więc deterministyczny dryf plików generowanych i
ponownie wypisuje listę zmienionych artefaktów w swoim finalnym
podsumowaniu. `--check-generated` wybiera zamiast tego weryfikację tylko do
odczytu. CI używa tego samego, wspólnego runnera w trybie sprawdzania, więc
lista generatorów jest utrzymywana w jednym miejscu. `-j N`, `--jobs N` oraz
`-jN` wybierają równoległość buildu. Bramki to:

1. weryfikacja wymaganych narzędzi i zarządzanych komponentów;
2. testy hosta, w tym opcjonalny zestaw FreeRTOS POSIX;
3. Valgrind memcheck;
4. cppcheck;
5. clang-tidy dla kodu hosta/współdzielonego oraz backendu STM32, używający
   zarówno bazy danych `JH_STM32_HOST_SANITY` kompilatora hosta, jak i
   prawdziwej bazy danych ARM;
6. wykrywanie duplikatów PMD CPD w implementacjach C/C++, których właścicielem
   jest repozytorium, oraz w skryptach Python;
7. buildy STM32, RP2040/RP2350, natywnego FreeRTOS, profilu funkcji RP
   oraz czyste buildy ESP32-S3/ESP-IDF z walidacją artefaktów;
8. każdy zadeklarowany przykład RP, buildy natywnych testów parytetu
   oraz przykłady STM32.

Skrypt na starcie usuwa tylko swoje zarządzane drzewa `.build/gate`,
`.build/examples` oraz `.build/tests`. Kończy działanie przy pierwszej
nieudanej bramce.
Bramka 3 uruchamia każdy bezpośrednio zarejestrowany, natywny plik
wykonywalny testu C/C++ oznaczony jako `memcheck`. `MEMCHECK_REQUIRED_TESTS`
pozostaje wymaganym, krytycznym podzbiorem i zapobiega ciszemu wypadnięciu
tych zestawów z selekcji. Testy driverów Python, CMake i shell są
wykluczone: opakowanie ich interpretera nadrzędnego mierzyłoby to narzędzie
hosta, a nie skompilowane krzyżowo firmware lub procesy potomne.
Konfiguracja Valgrind używa sprawiedliwego (fair) planowania wątków, dzięki
czemu natywne testy planisty FreeRTOS POSIX są uwzględnione bez zawieszania
się. Postęp CTest jest strumieniowany bez filtrowania zarówno do terminala,
jak i do `.build/gate/logs/jh_memcheck.log`.

### `vscode/entry/jh-vscode` oraz `jh-vscode.cmd`

Launchery dla Uniksa i Windows uruchamiają jeden publiczny punkt wejścia
Python oraz współdzielony CLI projektu firmware. Launcher dla Windows
weryfikuje Python 3 wraz z pyserial i zachowuje argumenty CLI oraz zachowanie
kodu wyjścia. Konfiguracja firmware domyślnie używa Ninja, przekazuje
aktywny interpreter Python, eksportuje polecenia buildu oraz rozwiązuje
ścieżki picotool/łańcucha narzędzi specyficzne dla platformy. Natywne drzewa
CMake dla Windows używają krótkiego katalogu głównego buildu z
bootstrapu, podczas gdy finalne artefakty zachowują swoje ścieżki manifestu.
`debug-tools` raportuje zweryfikowany OpenOCD, GDB zdolny do obsługi ARM,
katalog główny skryptów oraz konfigurację targetu używaną przez
Cortex-Debug. Generowane ustawienia dla Linuksa wybierają `gdb-multiarch`;
Windows używa GDB GNU Arm zarządzanego przez bootstrap. Akcje, opcje,
zabezpieczenia urządzenia oraz zachowanie monitora są udokumentowane
wyłącznie w
[Wejście JaszczurHAL do VS Code](../../../vscode/README.md). Semantyka
manifestu, wykrywania źródeł, targetu, płytki, cache oraz artefaktów należy
do [Workflow projektu firmware](../../pl/FwProjectWorkflow.md).

## Skrypty buildu

### `scripts/build_rp_native_lib.sh`

Kompiluje JaszczurHAL przy użyciu oficjalnego Pico SDK. Obsługiwane targety
to:

| Target skryptu | Platforma Pico SDK | Domyślna płytka |
|---|---|---|
| `rp2040` | `rp2040` | `pico` |
| `rp2350-arm` | `rp2350-arm-s` | `pico2` |
| `rp2350-riscv` | `rp2350-riscv` | `pico2` |

Skrypt zapewnia obecność Pico SDK i picotool. Dodatkowo zapewnia obecność
FreeRTOS-Kernel dla `--freertos` oraz łańcucha narzędzi RISC-V dla
`rp2350-riscv`. Może skompilować przenośną aplikację przy pomocy
`--example <directory>`.

Domyślnie każdy build weryfikuje bibliotekę statyczną, sondy artefaktów
ELF/BIN/UF2, symbole wejścia rdzenia oraz opcjonalne firmware przykładu.
`--library-only` kompiluje wyłącznie target CMake `JaszczurHAL` i weryfikuje
łączalne archiwum `libJaszczurHAL.a`. Domyślne wyjście to
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

Produkcyjny runner projektów dla targetów, których deskryptor płytki wybiera
providera `esp-idf`. Udostępnia trzy akcje:

| Akcja | Zachowanie |
|---|---|
| `build` | Opcjonalnie usuwa wybrane wyjście przy `--clean`, generuje wejścia projektu/płytki/SDK, kompiluje przy użyciu przypiętego ESP-IDF, przechwytuje pochodzenie łańcucha narzędzi oraz waliduje artefakty. |
| `artifacts` | Ponownie waliduje istniejący build i zapisuje deterministyczny manifest `jh_esp_idf_artifacts.json` bez wywoływania kompilatora. |
| `flash` | Ponownie waliduje istniejący build, wymaga `--port` i wywołuje flashowanie ESP-IDF z kompletnym zestawem obrazów/przesunięć przed ponowną walidacją logu flashowania i manifestu. |

`--project` jest wymagane. `--target` domyślnie to `esp32s3`; jego deskryptor
targetu wybiera `waveshare-esp32-s3-zero`, gdy pominięto `--board`.
`--output` musi pozostać poniżej katalogu głównego `.build` projektu lub
repozytorium. Powtarzalne argumenty `--source` zastępują automatyczne
wykrywanie; w przeciwnym razie runner uwzględnia obsługiwane pliki w
katalogu głównym projektu i rekurencyjnie pod `src/`. Powtarzalne argumenty
`--feature` i `--define` rozszerzają konfigurację projektu. `--idf-dir` lub
`JH_ESP_IDF_DIR` wybiera dokładny, zgodny zewnętrzny checkout.

Runner odczytuje bezpośrednio `boards/` i rejestr funkcji. Funkcje wymagane
przez target uczestniczą w rozwiązanym zestawie, podczas gdy żądane lub
przechodnie funkcje spoza `supportedFeatures` kończą się niepowodzeniem z
`[JH-CFG-UNSUPPORTED]`. Lista dozwolonych funkcji ESP32-S3 zawiera wymagany
FreeRTOS, dostarczone flagi peryferiów Fazy 2 oraz graf sieci/usług Fazy 3.
Jej źródła systemu, synchronizacji, GPIO, ADC, prostego PWM,
szeregowe/debug oraz timera tworzą bazę. Runner jest też właścicielem
`HAL_PROVIDE_APP_ENTRY`, dokładnych selektorów targetu/płytki, domyślnych
wartości generowanego `sdkconfig` oraz kontrolowanego grafu komponentów.
Zapisuje rozwiązane listy źródeł i zależności do generowanego wejścia CMake,
którego używa komponent ESP-IDF.

Manifest wyjściowy używa wyłącznie ścieżek względnych do buildu.
Zapisuje uporządkowane obrazy flash i skróty; artefakty buildu; fakty
targetu, płytki, funkcji, partycji i `sdkconfig`; wersję/commit ESP-IDF;
rzeczywiste wersje kompilatora, CMake, Ninja, IDF Python i esptool; oraz
przypięty skrót `tools.json`.
`scripts/build_esp_idf_phase0.py` jest wrapperem kompatybilności, który
dostarcza temu produkcyjnemu runnerowi dawne argumenty projektów testowych.

### `scripts/build_rp_native_parity_fixtures.sh`

Kompiluje `tests/hardware/rp_usb_multicore` oraz `tests/hardware/rp_sdlogger`
przez zwykły workflow `jh-vscode` dla:

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

`scripts/component_manager.py` jest właścicielem wieloplatformowej
implementacji sprawdzeń Git clone/fetch/ref/origin/submodule, pobierania
archiwów i SHA-256, ekstrakcji ZIP/`tar.gz`, atomowego zastępowania,
manifestów zawartości oraz stempli wersji. Dedykowane pliki `ensure_*.sh` są
uniksowymi launcherami zgodności, które przekazują swój istniejący CLI do
tego menedżera Python. Metadane walidacji komponentów, domyślna kolejność
oraz mapowania launcherów żyją w wersjonowanym modelu
`config/tooling/managed_components.json`.

Dedykowane pomocniki wczytują śledzone przypięcia z
`third_party/*_version.conf`. Normalnie używaj
`third_party/update_components.sh`; wywołuj pojedynczego pomocnika tylko dla
dedykowanej buildu lub diagnostyki.

### Wspólne zachowanie checkoutu

Zarządzane katalogi oparte na Git są instalacjami dokładnego commita.
Brakujący katalog jest klonowany przy przypiętym ref. Katalog przy innym
commicie lub katalog nie-Git w zarządzanej lokalizacji jest zastępowany.
`--verify-only` raportuje niezgodność bez jej modyfikowania.

Zarządzane katalogi oparte na archiwach używają dokładnego przypięcia
SHA-256 oraz deterministycznego manifestu wyekstrahowanych plików.
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

Synchronizuje `third_party/FatFs` z dokładnego commita repozytorium
`jaszczurtd/ff16`, którego właścicielem jest projekt, zapisanego w
`third_party/fatfs_version.conf`. To repozytorium odzwierciedla niezmienione
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
źródła rdzenia i licencję oraz skonfigurowaną wersję API major/minor
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
RP/STM32. Bez warunku włączenia jest operacją pustą (no-op). Uruchamia się,
gdy:

- podano `--enable`, `--freertos`, `--force` lub `--verify-only`;
- `EXTRA_HAL_DEFINES` zawiera `HAL_ENABLE_FREERTOS`; lub
- `HAL_ENABLE_FREERTOS` jest obecne w środowisku.

`--kernel-dir` oraz `JH_FREERTOS_KERNEL_DIR` wybierają zewnętrzny checkout,
który jest weryfikowany, ale nie zastępowany. Zarządzane submoduły oraz
wersja jądra są również sprawdzane. Natywne, bezpośrednie integracje CMake
RP i STM32G474 wywołują `scripts/component_manager.py` bezpośrednio; ten
wrapper shell jest punktem wejścia kompatybilności używanym przez pomocniki
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

Rekompiluje się, gdy zmieni się checkout źródeł, zaraportowana wersja
picotool jest błędna, obsługa USB staje się dostępna, lub SDK zapewnia teraz
obsługę podpisywania, której brakowało starszej buildu. `--rebuild`
wymusza czystą rebuild. `--verify-only` sprawdza zarówno źródło, jak i
plik wykonywalny bez ich zmiany.

Pomocnik jest włączany przez `--enable`, `--build`, `--force`,
`--verify-only`, `--rebuild` lub `JH_ENABLE_PICOTOOL`. Jego katalog
buildu musi pozostać poniżej `.build/`.

### `scripts/ensure_pmd.sh`

Instaluje lub weryfikuje binarną dystrybucję PMD 7.26.0 przypiętą w
`third_party/pmd_version.conf`. Menedżer uwierzytelnia SHA-256 pliku ZIP,
śledzi kompletny manifest wyekstrahowanych plików, rozwiązuje launcher
platformy oraz sprawdza zaraportowaną wersję PMD. Wymagane jest środowisko
Java; `runmefirst.sh` dla Linuksa instaluje domyślne, bezgłowe (headless)
runtime.

### `scripts/ensure_riscv_toolchain.sh`

Instaluje przypięty, gotowy łańcuch narzędzi `riscv32-unknown-elf`
firmy Raspberry Pi dla natywnego targetu `rp2350-riscv`. Mapuje architekturę
hosta na pasujący zasób wydania, ekstrahuje archiwum do
`third_party/riscv-toolchain`, zapisuje stempel komponentu oraz weryfikuje
główną wersję GCC.

Jeśli plik wykonywalny, manifest zawartości, tożsamość archiwum lub stempel
różnią się od śledzonej konfiguracji, tryb normalny zastępuje instalację.
`--verify-only` nie wykonuje pobierania ani ekstrakcji. Uwierzytelnione
zasoby obejmują Linuksa x86-64 i AArch64 oraz natywny AMD64 Windows.

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

Rejestr JSON jest źródłem używanym przez `generate`, a generowane manifesty
są wejściem dla `build`. Akcja `list` raportuje aktualne pełne
macierze oraz macierze domyślnej bramki bez utrzymywania tutaj duplikatów
liczników.
Przykłady RISC-V WiFi pozostają wykluczone, dopóki RP2350 RISC-V + CYW43 jest
nieobsługiwane.

Zobacz [Przykłady JaszczurHAL](../../../examples/README.md) po macierz
targetów, interfejs aplikacji oraz polecenia buildu.

### `scripts/sync_generated.py`

Pojedynczy runner na poziomie repozytorium dla każdego śledzonego artefaktu
generowanego. `--write` odświeża rejestr funkcji, statyczny rejestr płytek,
pliki VS Code przykładów, główne pliki VS Code oraz SBOM repozytorium.
`--check` wywołuje ich tryby weryfikacji tylko do odczytu
i kończy się niepowodzeniem przy brakującym lub nieaktualnym wyjściu. Runner
tworzy migawkę śledzonych i nieignorowanych plików przed wykonaniem, a
następnie wypisuje ścieżki zmienione podczas uruchomienia.
`--report-file <path>` również zapisuje tę finalną listę dla wywołujących,
takich jak `runalltests.sh`.

### `scripts/generate_board_config.py`

Waliduje deskryptory JSON targetu, płytki i możliwości pod `boards/` i
rozwiązuje jedną parę target/płytka w generowaną konfigurację CMake oraz
metadane odczytywalne maszynowo. CMake i testy płytek wywołują go
bezpośrednio. `--validate-only` sprawdza kompletny rejestr, `--list
targets|boards` oraz `--default-board` dostarczają odkrywania, natomiast
`--feature` i `--define` dodają zwalidowaną nakładkę buildu używaną dla
generowanego wyjścia. `--output-dir` i `--output-root` muszą pozostać
wewnątrz drzewa buildu, którego właścicielem jest wywołujący.
Definicje providera/backendu są rzutowane spójnie do
`jh_board_resolved.json.boardCompileDefinitions`, generowanego
`JH_BOARD_COMPILE_DEFINITIONS` oraz makr `jh_board_config.h` do
bezpośredniego użycia przez kompilator. Generowana referencja sygnatury
linkowania GCC/Clang używa korzenia `constructor, used`, dzięki czemu
niezgodności target/płytka/funkcja pozostają błędami linkowania przy
włączonym usuwaniu nieużywanych sekcji (section garbage collection).

Generowany nagłówek eksponuje również identyfikator wybranego deskryptora
targetu, backend, nazwy MCU i podtypu, opis CPU i liczbę rdzeni, obecność
FPU oraz całkowitą/użytkową ilość RAM jako fakty `HAL_TARGET_*`. Migawki
architektury systemu odczytują te fakty bezpośrednio. Pojemność flash
programu specyficzna dla płytki pozostaje dostępna jako
`HAL_BOARD_EXPECTED_FLASH_BYTES`.

`--write-static` odświeża śledzone `jh_board_registry.h`,
`jh_board_fallback_config.h` oraz rejestr CMake komponentów płytki. Dwa
pierwsze pochodzą z `boards/`; projekcja CMake pochodzi z
`config/tooling/board_components.json`. `--check-static` odrzuca brakujące
lub nieaktualne kopie. CI uruchamia sprawdzenie niezależnie od generowania
płytki per build.

### `scripts/generate_hal_features.py`

Waliduje zamkniętą przestrzeń nazw `HAL_ENABLE_*` / `HAL_DISABLE_*` oraz
niezależny od targetu graf zależności pod `config/features/`. `--write`
atomowo odświeża śledzony, produkcyjny nagłówek C oraz resolver CMake,
natomiast `--check` porównuje je bez zapisywania. `--lint` akceptuje
powtarzalne argumenty `--input-root` i sprawdza surowe pliki
`hal_project_config.h` oraz manifesty projektów pod kątem nieznanych
symboli, nieobsługiwanych wartości `=0` oraz bezpośrednich żądań symboli
pochodnych. Odrzuca również warunkowe definicje funkcji poza pasującym
strażnikiem `#ifndef` oraz nieskalarne listy definicji CMake. Ustalenia
domyślnie kończą polecenie niepowodzeniem; `--report-only` jest jawnym,
ręcznym trybem diagnostycznym.

`--effective` używa resolvera `jh-vscode` do wyliczenia zadeklarowanych
targetów, profili targetu i wariantów bez odczytywania ignorowanego przez
git, lokalnego stanu płytki. Sprawdza ograniczenia oraz aktywne żądania
zduplikowane po zastosowaniu pierwszeństwa warstw. Standardowy plik
`.vscode/jaszczurhal.project.json` tworzy zadeklarowane osie; niesparowany
`hal_project_config.h` z co najmniej jednym żądaniem funkcji HAL tworzy jeden
kontekst bezpośredni bez osi. Samodzielne nagłówki bez żądań oraz manifesty
referencyjne pozostają wejściami tylko dla surowego lintu.
`--resolution-output <path>` zapisuje deterministyczne `requestedFeatures`,
`resolvedFeatures`, skróty domknięcia oraz pochodzenie żądań bezpośrednich
dla każdej efektywnej konfiguracji. Test rejestru zamraża skrót macierzy w
`config/effective-features-baseline.json`. Każdy rekord mapuje się na
sprawdzoną, unikalną krotkę target/płytka/żądanie dla preprocesorów C oraz
sprawdzony, unikalny zestaw żądań dla resolvera CMake niezależnego od
targetu.

Generowany nagłówek C jest dołączany przez `hal_config.h` i rozwija każdą
niezależną od targetu, przechodnią implikację. Jego generowana sekcja
`HAL_CONFIG_VERBOSE` raportuje każdą aktywną, zarejestrowaną funkcję po
uruchomieniu pozostałych reguł konfiguracji. Generowany resolver CMake
dostarcza to samo domknięcie do selekcji źródeł i zależności RP oraz
STM32G474. Runner ESP-IDF również rozwiązuje to domknięcie, a następnie
egzekwuje listę dozwolonych `supportedFeatures` targetu przed skonfigurowaniem
jego kontrolowanego, minimalnego grafu komponentów. Generowanie płytki
używa rozwiązanego zestawu dla `featureHash` i sygnatury linkowania,
zachowując przy tym zestaw bezpośredni jako `requestedFeatures`.
`jh-vscode` rozwiązuje rejestr po nakładkach profilu manifestu i wariantu,
eksponuje wynik przez `featureResolution` i używa rozwiązanego zestawu dla
preflight i decyzji OTA, przekazując przy tym żądania bezpośrednie do
CMake.

Domyślne wartości warunkowe, wybory providera, sprawdzenia capabilities płytki
oraz ograniczenia targetu pozostają w `hal_config.h`. CI uruchamia `--check`
oraz ścisły lint surowy/efektywny i przesyła deterministyczny raport
rozwiązania. Zainstalowane pakiety RP i STM32G474 zawierają generowane
nagłówki funkcji/płytki, rozwiązany JSON płytki, nagłówek sygnatury
linkowania oraz źródło referencyjne; projekt korzystający bezpośrednio z
kompilatora może skompilować i zlinkować te artefakty bez wywoływania Pythona.

### `scripts/board_registry.py`

Projekcja tylko do importu zwalidowanych deskryptorów `boards/` na model targetu
i płytki używany przez `jh-vscode`, generatory projektów oraz dispatcher
przykładów. Celowo nie zawiera niezależnego rejestru ani interfejsu wiersza
poleceń; pliki deskryptorów pozostają źródłem prawdy.

### `scripts/tooling_contract.py` oraz `scripts/repository_layout.py`

Loadery tylko-importu dla wersjonowanych modeli danych pod
`config/tooling/`. `tooling_contract.py` waliduje wspólny schemat i
typizowane pola; `repository_layout.py` eksponuje nazwane metadane archiwów
oraz śledzone ścieżki artefaktów generowanych. Katalogi domenowe pozostają
oddzielnymi dokumentami JSON zamiast jednego globalnego modułu tekstowego.
Inwentarz, polecenia projekcji oraz zasady własności formatu są
zdefiniowane w [Interfejsach narzędziowych](#interfejsy-narzędziowe).

### `scripts/vscode_task_config.py`

Źródło prawdy tylko-importu dla generowanych rozszerzeń VS Code, referencji
skrótów klawiszowych, definicji zadań, wejścia wyboru płytki oraz
zarządzanych profili Cortex-Debug. Dostarcza również pomocników migracji i
synchronizacji używanych przez `sync-board-picker`. Generowane projekty,
samodzielny generator projektów oraz testy dryfu importują te funkcje
zamiast utrzymywać osobne szablony JSON. Zachowanie widoczne dla użytkownika
każdego generowanego zadania jest udokumentowane w
[Generowanych zadaniach VS Code](../../../vscode/README.md#generated-vs-code-tasks).

### `vscode/tools/create-vscode-example.py`

Generuje samodzielny projekt firmware oparty na dispatcherze, z manifestem,
aplikacją blink, konfiguracją projektu HAL, konfiguracją uruchamiania,
współdzielonymi poleceniami zadań dla Uniksa/Windows, rekomendacjami
rozszerzeń oraz referencją skrótów klawiszowych. Generowane ustawienia VS
Code obejmują `cmake.configureSettings` dla początkowego targetu i płytki,
umożliwiając CMake Tools bezpośrednią konfigurację współdzielonego
dispatchera. `--target` i `--board` wybierają początkowy profil. `--force`
zastępuje wyłącznie pliki, których właścicielem jest generator, w żądanym
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
scala ich ścieżki bezwzględne w standardowy profil użytkownika VS Code jako
specyficzne dla Windows ustawienia Cortex-Debug. Updater zachowuje
niepowiązane ustawienia JSONC, komentarze, zagnieżdżenie oraz przecinki
końcowe. Tworzy `settings.json.jaszczurhal.bak` przed zmianą istniejącego
profilu i zastępuje plik ustawień atomowo. `--check` jest tylko do odczytu,
`--yes` potwierdza nieinteraktywną aktualizację, a `--settings` obsługuje
jawnie wybrany profil VS Code lub fixture testowy. `runmefirst.ps1` wywołuje
tego pomocnika automatycznie poza trybem `-FirmwareOnly`.

### `scripts/configure_ota_firewall.py`

Idempotentnie sprawdza i konfiguruje trwały, przychodzący dostęp TCP dla
callbacku OTA po stronie hosta. Współdzielony punkt wejścia wybiera backend
Linux lub Windows, znajduje sieć RFC1918 na domyślnym interfejsie IPv4,
zawęża regułę do tego interfejsu i podsieci, i domyślnie wybiera TCP/8266.
Aktywne instalacje UFW i firewalld używają swojej natywnej, trwałej
konfiguracji; fallback dla Linuksa używa `iptables-nft`/`iptables` z
`iptables-save` oraz loadera rozruchowego `netfilter-persistent`,
włączanego przez systemd, gdy jest dostępny.
Niefiltrowana polityka `INPUT` już zezwala na callback i nie wymaga
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
trasy, `--port` wybiera inny, stały port callbacku, a `--yes` obsługuje
świadomy, nieinteraktywny provisioning. Akceptowane są wyłącznie sieci
IPv4 RFC1918, a konfiguracja odmawia wystawienia portu już używanego przez
listenera.

### `scripts/ota_firewall_common.py`

Interfejs tylko-importu współdzielony przez backendy zapory sieciowej OTA
dla Linuksa i Windows. Jest właścicielem zwalidowanej wartości
interfejs/podsieć, sprawdzeń RFC1918 oraz wspólnego typu błędu
`SetupError`. Bezpośredni wywołujący powinni używać
`scripts/configure_ota_firewall.py`, aby wybór platformy, zgoda oraz kody
wyjścia pozostały spójne.

### `scripts/ota_firewall_windows.py`

Wewnętrzny backend Windows Defender Firewall wybierany przez
`scripts/configure_ota_firewall.py`. Wykrywa aktywne prywatne sieci IPv4
przez NetTCPIP, parsuje istniejące reguły NetSecurity, waliduje zakres
interfejs/podsieć/port oraz tworzy lub weryfikuje trwałą regułę
przychodzącą wyłącznie po tym, jak punkt wejścia uzyskał zgodę. Jego runner
poleceń jest testowym szwem (test seam), a moduł pozostaje wewnętrzny
względem publicznego punktu wejścia.

### `scripts/rp_ota_artifacts.py`

Wewnętrzny pomocnik pakowania natywnego firmware RP używany przez CMake.
`package` owija aplikacyjny BIN w wersjonowany nagłówek OTA JaszczurHAL z
targetem, przesunięciem ładowania, generacją, wersją oraz SHA-256 payloadu.
Pole HMAC pozostaje niepodpisane, dopóki akcja wgrywania VS Code nie
zastosuje hasła projektu. `merge-uf2` łączy UF2 aplikatora rozruchu
kopiującego do RAM z UF2 aplikacji, odrzucając konfliktujące bloki adresów i
normalizując numerację bloków. Artefakty buildu pozostają poniżej
rozwiązanego katalogu `.build/`. Zobacz
[Natywny workflow OTA](../../pl/OTAWorkflow.md), aby poznać pełny sposób pracy
z tymi artefaktami.

### `scripts/vscode_library_workspace.py`

Odpowiada za workflow biblioteki statycznej VS Code na poziomie
katalogu głównego repozytorium. Akcja `select` waliduje pary target/płytka
względem `boards/` i zapisuje aktywny profil w lokalnym stanie ignorowanym
przez git. `build`, `refresh-intellisense`, `install`, `clean` oraz
`config-dump` następnie rozwiązują te same ścieżki buildu i instalacji z
tego profilu.

Buildy RP delegują do `build_rp_native_lib.sh --library-only`,
buildy STM32G474 delegują do `build_stm32_lib.sh`, a buildy mock
wybierają główny target CMake `hal_mock`. Każdy build eksportuje
`compile_commands.json`; akcje IntelliSense zapisują lokalny
`.vscode/c_cpp_properties.json` bez zmiany śledzonych ustawień. Clean usuwa
wyłącznie zarządzane drzewa buildu/instalacji aktywnego profilu.

`sync-vscode` deterministycznie zapisuje śledzone zadania, ustawienia,
rekomendacje rozszerzeń oraz referencję skrótów klawiszowych katalogu
głównego z rejestru płytek. Użyj `sync-vscode --check`, by odrzucić dryf bez
zmiany plików.

To nie jest workflow projektu firmware. Projekty oparte na
dispatcherze używają `jh-vscode <action> --project <dir>`.

### `scripts/vscode_refresh_intellisense.sh`

Wrapper kompatybilności dla dawnego punktu wejścia IntelliSense
repozytorium. Mapuje `mock`, `rp2040`, `rp2350-arm`, `rp2350-riscv`, `stm32`
lub `stm32g474` na domyślną płytkę, wybiera ten profil biblioteki i deleguje
do `vscode_library_workspace.py refresh-intellisense`.

### `scripts/vscode_clear_build_artifacts.sh`

Ręczny pomocnik pełnego czyszczenia. Usuwa całe drzewo `.build/`
repozytorium i nic poza nim. Nie ma opcji. Usuwa to również buforowane
buildy targetów, przykłady, testy, dane IntelliSense oraz
skompilowany plik wykonywalny picotool; ignorowane źródła komponentów pod
`third_party/` są zachowywane. Główne zadanie VS Code `Project: Clean`
celowo używa zawężonej akcji przestrzeni roboczej biblioteki zamiast tego.

## Skrypty analizy statycznej i bezpieczeństwa

### `scripts/run_cpd.py`

Uruchamia zarządzany PMD Copy/Paste Detector nad źródłami implementacji
C/C++, których właścicielem jest repozytorium, oraz plikami Python poniżej
`scripts/`. Każda grupa duplikatów C/C++ produkcyjnych, testowych lub
przykładowych od 100 tokenów oraz każda grupa skryptów Python od 50 tokenów
blokuje bramkę; nie ma listy bazowej ani akceptowanego długu. Implementacje
generowane i vendorowane są wykluczone. Raport podaje również pokrycie
zduplikowanych tokenów globalnie oraz dla zakresów mock, RP2040, STM32G474,
współdzielonego, pozostałego przenośnego oraz skryptów Python. Nakładające
się zakresy tokenów są liczone tylko raz. Deterministyczne listy źródeł oraz
raporty XML są zapisywane do żądanego katalogu wyjściowego poniżej
`.build/`.

### `scripts/clang_tidy_files.py`

Odczytuje CMake `compile_commands.json`, wybiera pliki źródłowe, których
właścicielem jest JaszczurHAL, deduplikuje powtórzone wpisy buildu oraz
wypisuje zakotwiczone wyrażenia regularne plików dla `run-clang-tidy`.

Wymagane opcje to `--build-dir` oraz `--profile host|stm32`.
`--repo-root` kontroluje klasyfikację ścieżek. `--output-compile-db` zapisuje
przefiltrowaną, deterministyczną bazę danych. Dla wpisów STM32 skrypt dodaje
target clang arm-none-eabi oraz zgłaszane przez kompilator systemowe
include'y.

Jest to wewnętrzny pomocnik bramki jakości wywoływany przez
`runalltests.sh`, a nie ogólny formatter.

### `scripts/check_documentation_links.py`

Waliduje lokalne dla repozytorium cele i kotwice linków Markdown w
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
nagłówków, bloków kodu i symboli HAL/JH. `doc/CHANGELOG.md` i
`doc/HAL_FLAGS.txt` są celowo współdzielone i pozostają nieprzetłumaczone.
Zestaw testów CTest hosta rejestruje ten test jako
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

`--inventory` oraz `--output` nadpisują domyślne ścieżki wejścia i wyjścia.
`--check` generuje tymczasowego kandydata, porównuje go z wybranym wyjściem
i kończy się niepowodzeniem bez modyfikowania śledzonego pliku, gdy brakuje
go lub jest nieaktualny. Generator używa wyłącznie standardowej biblioteki
Python.

### `scripts/check_sbom.sh`

Wrapper kompatybilności delegujący do `scripts/generate_sbom.py --check`.
Współdzielony runner `scripts/sync_generated.py --check` jest bramką
aktualności repozytorium i CI.

### `scripts/check_vulnerabilities.sh`

Regeneruje śledzony SBOM, a następnie uruchamia skanery, które są już
zainstalowane:

- `osv-scanner` skanuje rekurencyjnie źródło repozytorium;
- gdy `JH_SECURITY_SCAN_SOURCE=1`, `cve-bin-tool` skanuje generowany SBOM
  CycloneDX.

Skrypt przeszukuje zarówno `PATH`, jak i `~/.local/bin`, nie instaluje
skanerów i ostrzega zamiast kończyć się niepowodzeniem wyłącznie z powodu
braku dostępnego skanera. Ustalenia skanerów oraz niepowodzenia wykonania
skanerów nadal propagują się jako niepowodzenia polecenia.

Zobacz [Łańcuch dostaw bezpieczeństwa](../../pl/security_supply_chain.md) po
inwentarz, SBOM, CI, triage oraz politykę aktualizacji komponentów.

## Skrypt zasobów

### `scripts/image_to_base64.py`

Odczytuje dowolny obraz binarny, koduje go w Base64 i emituje prawidłową
deklarację C `static const char[]`. Argument pozycyjny to obraz wejściowy.
Przydatne opcje to:

- `--output` / `-o` do zapisu do pliku zamiast standardowego wyjścia;
- `--name` / `-n` do wyboru prawidłowego identyfikatora C;
- `--line-width` do kontroli zawijania generowanego literału tekstowego.

Błędnie napisana opcja `--otput` pozostaje aliasem kompatybilności; nowe
polecenia powinny używać `--output`.

Użycie PNG jest udokumentowane w
[API LodePNG](18_LodePNG.md#skrypt-zasobów-png-do-base64).
Użycie JPEG jest udokumentowane w
[API JPEG](19_JPEG.md#skrypt-zasobów-jpeg-do-base64).

## Powiązana dokumentacja

- [Build biblioteki JaszczurHAL](../../pl/lib_compilation.md) opisuje
  wymagania, opcje, wyjścia oraz ręczne odpowiedniki CMake dla biblioteki
  statycznej i natywnej buildu RP.
- [Workflow projektu firmware](../../pl/FwProjectWorkflow.md) opisuje
  manifesty firmware oparte na dispatcherze, wykrywanie źródeł, rozwiązywanie
  targetu/płytki, własność cache, wgrywanie oraz pliki generowane.
- [Wejście JaszczurHAL do VS Code](../../../vscode/README.md) jest CLI
  `jh-vscode` skierowanym do użytkownika oraz interfejsem zadań VS Code.
- [Profile targetu i płytki](../../pl/boards_profiles_howto.md) dokumentuje
  pola deskryptora oraz sposób łączenia domyślnych wartości rejestru z
  manifestami projektu.
- [Dziennik zmian wejścia VS Code](../../../vscode/CHANGELOG.md) rejestruje
  zaimplementowane możliwości workflow oraz decyzje dotyczące
  zgodności.
- [Runtime Windows](../../../vscode/windows/runtime/README.md)
  rejestruje granicę natywnego runtime Windows oraz
  pozostałą pracę nad adapterem urządzenia.
- [Natywne neutralne firmware RP](../../../vscode/neutral_fw/rp_native/README.md)
  wyjaśnia obraz domyślnej tożsamości używany przez
  `jh-vscode clear-identity`.
- [Przykłady JaszczurHAL](../../../examples/README.md) dokumentuje rejestr
  przykładów, pokrycie targetów, interfejs wejścia aplikacji, warianty oraz
  polecenia buildu.
- [Zarządzane komponenty zewnętrzne](../../../third_party/README.md)
  dokumentuje śledzone przypięcia, ignorowane instalacje, zachowanie
  updatera oraz politykę zewnętrznego checkoutu.
- [Łańcuch dostaw bezpieczeństwa](../../pl/security_supply_chain.md)
  dokumentuje generowanie SBOM, skanery podatności, politykę CI oraz zasady
  aktualizacji/triage.
