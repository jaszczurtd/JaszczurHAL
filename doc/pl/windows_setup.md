# Natywna konfiguracja dla Windows

*Dostępne również [po angielsku](../en/windows_setup.md).*

JaszczurHAL zapewnia natywny bootstrap środowiska programistycznego firmware
pod Windows. Przygotowuje przypięte narzędzia bez
konieczności użycia WSL, Git Bash, wingetu, Chocolatey ani globalnej
instalacji łańcucha narzędziowego.

Wspierany minimalny system to Windows 10 1809 (build 17763), AMD64. Git for
Windows oraz VS Code muszą być już zainstalowane, aby repozytorium mogło
zostać pobrane i otwarte. Bootstrap zarządza Pythonem, CMake, Ninja, GNU Arm
Embedded, GNU RISC-V, OpenOCD, picotoolem, pyserial oraz przypiętymi
zależnościami źródłowymi. Runner ESP32-S3 dodatkowo przygotowuje przypięty
checkout ESP-IDF oraz jego oficjalne narzędzia targetu przy pierwszym
użyciu; są one buforowane pod `third_party\esp-idf` oraz
`%USERPROFILE%\.espressif`.
Sprawdzenie kompletności GNU Arm obejmuje GDB, a ponowne wykorzystanie
OpenOCD wymaga skryptów CMSIS-DAP, ST-Link, RP2040, RP2350 i STM32G4
używanych przez generowane konfiguracje debugowania.

## Ustawienia hosta

Natywny build firmware wymaga obu przełączników dla długich ścieżek:

- Windows `LongPathsEnabled=1` pod
  `HKLM\SYSTEM\CurrentControlSet\Control\FileSystem`;
- Git `core.longpaths=true`.

Domyślna konfiguracja weryfikuje te ustawienia i wypisuje polecenie
potrzebne w razie brakującego ustawienia Git. Jawne przekazanie
`-ConfigureHost` pozwala skryptowi ustawić `core.longpaths`. Może ustawić
wartość rejestru Windows tylko wtedy, gdy jest uruchomiony z sesji
PowerShell, która jest już podniesiona do uprawnień administratora
(elevated). Skrypt nigdy sam nie uruchamia procesu z podniesionymi
uprawnieniami.

Skanowanie punktów końcowych może istotnie spowolnić build
CMake/Ninja lub poddać kwarantannie nowe pliki `.exe`, `.elf` i `.uf2`.
Utrzymuj zarządzane narzędzia i katalogi główne buildu krótkie, a gdy
zmierzona wydajność buildu lub kwarantanny tego wymagają, poproś
administratora lub zespół bezpieczeństwa o wąsko zakresowane wyjątki.
Bootstrap nie zmienia konfiguracji oprogramowania antymalware.

## Konfiguracja

Utrzymuj checkout na lokalnym woluminie Windows, takim jak `C:`.
Uruchomienie natywnego bootstrapu przez ścieżkę UNC WSL
(`\\wsl.localhost\...`) jest odrzucane, ponieważ Git for Windows nie może
bezpiecznie posiadać i aktualizować takiego checkoutu.

Uruchom Windows PowerShell 5.1 lub nowszy z katalogu głównego repozytorium:

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File .\runmefirst.ps1
```

Kompletny plan jest wypisywany przed pierwszą zmianą hosta lub systemu
plików. Domyślne lokalizacje to krótkie ścieżki lokalne dla użytkownika:

```text
%USERPROFILE%\.jh\tools
%USERPROFILE%\.jh\build
```

Zarządzana baza Pythona 3.12 znajduje się poniżej katalogu głównego
narzędzi. Jej izolowane środowisko pyserial jest tworzone w
`.build\windows\venv`, gdzie znajduje je `jh-vscode.cmd` bez globalnej
zmiany `PATH`.

Bootstrap zapisuje też `.build\windows\host-environment.json`. Współdzielone
runtime odczytuje ten stan, aby wybrać zweryfikowane
ścieżki CMake, Ninja, GNU Arm, GNU RISC-V, OpenOCD, picotool, Python i
zarządzany katalog główny buildu. `-VerifyOnly` sprawdza stan bajt po
bajcie względem bieżącej rezolucji.

Tryb edytora scala też rozwiązane ścieżki debuggera do standardowego
profilu użytkownika VS Code jako `cortex-debug.openocdPath.windows` i
`cortex-debug.armToolchainPath.windows`. Istniejące komentarze JSONC i
niepowiązane ustawienia są zachowywane. Przed zmianą istniejącego pliku
konfiguracja zapisuje `settings.json.jaszczurhal.bak`; `-VerifyOnly`
sprawdza obie wartości bez zapisu. `-FirmwareOnly` pozostawia profil VS
Code niezmieniony.

Przydatne tryby to:

```powershell
# Zezwala na naprawę udokumentowanych ustawień hosta dla długich ścieżek.
.\runmefirst.ps1 -ConfigureHost

# Jawnie zezwala na instalację zalecanych rozszerzeń VS Code.
.\runmefirst.ps1 -InstallExtensions

# Preferuje każde przypięte, zarządzane narzędzie ponad zgodną instalację systemową.
.\runmefirst.ps1 -Force

# Sprawdzenie tylko do odczytu komponentów i wymagań hosta.
.\runmefirst.ps1 -VerifyOnly

# Sprawdza bezgłowy (headless) builder firmware'u bez wymagania lub konfigurowania VS Code.
.\runmefirst.ps1 -FirmwareOnly
```

`-VerifyOnly` nie może być łączony z żadnym z przełączników zgody. Brakujący,
zmodyfikowany lub nieaktualny komponent kończy sprawdzenie niepowodzeniem
bez naprawy.
`-FirmwareOnly` zmienia wyłącznie ostateczną klasyfikację inwentarza: VS
Code i jego rozszerzenia pozostają wymienione jako opcjonalne, podczas gdy
każdy wymóg wstępny firmware'u pozostaje wymagany. CI łączy ten tryb z
`-ConfigureHost` podczas konfiguracji i używa go ponownie podczas przebiegu
weryfikacji tylko do odczytu.

Bootstrap wykorzystuje ponownie zgodne systemowe instalacje CMake, Ninja,
GNU Arm i OpenOCD, chyba że obecne jest `-Force`. Zarządzane archiwa są
uwierzytelniane przez SHA-256, wypakowywane przez atomową podmianę
katalogu i rejestrowane z kompletnym manifestem zawartości. Końcowy
raport zawiera każdą rozwiązaną ścieżkę wykonywalną. Ponowne wykorzystanie
OpenOCD dodatkowo wymaga kompletnej, sąsiadującej lub konwencjonalnej
instalacji `share\openocd\scripts`; niekompletny pakiet systemowy jest
pomijany na rzecz zarządzanego archiwum. Ponowne uruchomienie konfiguracji
pozostawia poprawne komponenty niezmienione.

## Cortex debug i drivery sond

Bootstrap dla Windows konfiguruje Cortex-Debug na podstawie zweryfikowanego
rekordu hosta. Użyj `debug-tools` do diagnozowania lub inspekcji
rozwiązanych ścieżek dla konkretnego projektu:

```powershell
.\vscode\entry\jh-vscode.cmd debug-tools `
  --project .\examples\01_core_runtime `
  --target rp2350-arm --board pico2w --json
```

Wynik zawiera `openocd`, `gdb`, `armToolchainPath`, katalog główny
skryptów OpenOCD oraz parę interfejsu diagnostycznego/konfiguracji
docelowej danego targetu. Cortex-Debug rozwiązuje `arm-none-eabi-gdb` z
skonfigurowanego katalogu łańcucha narzędziowego. Generowane konfiguracje
uruchomieniowe wybierają kompletną konfigurację OpenOCD dla każdego
profilu i nie zależą od prywatnych dla projektu ustawień
`cortex-debug.gdbPath`, katalogu głównego skryptów ani SVD. Zarządzane
archiwum OpenOCD dla Raspberry Pi znajduje swój sąsiadujący katalog
skryptów bez globalnej zmiany `PATH`.

Urządzenia USB Pico i Pico 2 w trybie BOOTSEL są targetami debugowania i nie
udostępniają sondy SWD przez to połączenie. Debugowanie Cortex wymaga
osobnej sondy Raspberry Pi Debug Probe, Pico z firmware'em Debug
Probe/Picoprobe lub zgodnej sondy podłączonej do SWD. Standardowa
konfiguracja RP używa `interface/cmsis-dap.cfg`. Profil NUCLEO-G474RE
używa wbudowanego ST-Linka przez `board/st_nucleo_g4.cfg`, który wybiera
SWD i zachowanie resetu sprzętowego płytki; nie wymaga osobnej sondy ani
zewnętrznego okablowania SWD. Generowane profile uruchomieniowe RP
ustawiają `adapter speed 5000` dla RP2040 i `adapter speed 2000` dla
RP2350. Nie usuwaj tych poleceń: gołe skrypty CMSIS-DAP/target w
przeciwnym razie spadają do 100 kHz, a wykrywanie flash RP2350 może
przekroczyć domyślny timeout zdalny GDB i zdesynchronizować jego
początkową wymianę pakietów w Windows.

Bootstrap inwentaryzuje urządzenia sond, ale nie instaluje, nie zastępuje
ani nie przełącza (rebind) driverów USB Windows. Jeśli OpenOCD zgłasza
brak pasującego urządzenia CMSIS-DAP, sprawdź najpierw fizyczne połączenie
SWD i Menedżer urządzeń. Zmiana drivera to osobna czynność
administracyjna: zidentyfikuj dokładny interfejs sondy, zapoznaj się z
aktualnymi instrukcjami producenta sondy dla Windows i uzyskaj zgodę przed
jej zmianą. Nie stosuj drivera USB do interfejsu pamięci masowej Pico
BOOTSEL.

Natywny test dymny sprzętu Windows użył oficjalnej Raspberry Pi Debug
Probe z firmware'em 2.3.1 oraz Pico 2 W jako targetu RP2350 Arm. Podłącz
`SWDIO` sondy do `SWDIO` targetu, `SWCLK` sondy do `SWCLK` targetu oraz
połącz ich masy. Windows udostępnił sondę przez driver Microsoft
WinUSB; instalacja drivera lub jego przełączenie (rebind) nie były
potrzebne. Zarządzany OpenOCD wykrył oba rdzenie Cortex-M33, a zarządzany
GNU Arm GDB załadował obraz Debug ELF, zatrzymał się na `main`, wznowił do
`app_start` i odłączył się. Końcowy `reset run` OpenOCD zwrócił port USB
CDC aplikacji. Kolejny test DoomConsole również załadował swój obraz
Debug ELF i zatrzymał się na `app_start` przy tych samych ustawieniach
profilu uruchomieniowego.

Natywny test dymny sprzętu STM32 użył NUCLEO-G474RE z wbudowanym
ST-Linkiem V3J9M3 (`0483:374e`) na Windows 10 LTSC. Zarządzany OpenOCD
`0.12.0+dev (2026-07-01-10:44)` wykrył Cortex-M4 r0p1, 512 KiB
dwubankowej pamięci flash, sześć punktów przerwania (breakpointów) i
cztery punkty obserwacji (watchpointy). Zarządzany GNU Arm GDB
zaprogramował reprezentatywny obraz Debug `01_core_runtime`, zatrzymał
się najpierw na `main`, a potem na `app_start`, odłączył się poprawnie i
wydał `reset run`. Dla tej płytki użyj wygenerowanego profilu
`board/st_nucleo_g4.cfg`: sama sesja `interface/stlink.cfg` plus
`target/stm32g4x.cfg` może zawieść przy badaniu targetu, gdy płytka
potrzebuje konfiguracji sprzętowego resetu Nucleo.

OpenOCD może zgłosić przestarzały firmware Debug Probe/Picoprobe i
włączyć wolniejszy tryb zgodności. To ostrzeżenie nie uniemożliwia
debugowania SWD. Zaktualizuj firmware sondy osobno, korzystając z
instrukcji producenta sondy, gdy niższa szybkość transferu ma znaczenie;
bootstrap JaszczurHAL nie modyfikuje firmware'u sondy.

Pobieranie przez HTTPS w Windows korzysta z systemowego `curl.exe` oraz
magazynu zaufania Schannel z przekierowaniami tylko HTTPS i TLS 1.2 lub
nowszym. Wspiera to zarządzaną inspekcję TLS w środowisku korporacyjnym
bez wyłączania walidacji certyfikatów. Każde pobrane archiwum musi nadal
pasować do swojego przypiętego skrótu SHA-256 przed wypakowaniem.

## Układ buildu firmware

Konfiguracja firmware'u domyślnie używa Ninja zarówno w Windows, jak i
Unix. Projekt może wybrać inny generator przez `cmake.generator` w
`.vscode/jaszczurhal.project.json`. Runtime przekazuje
swój aktualny zweryfikowany interpreter Pythona jako `Python3_EXECUTABLE`,
włącza `CMAKE_EXPORT_COMPILE_COMMANDS` i dostarcza rozwiązane ścieżki
plików wykonywalnych hosta jako pojedyncze argumenty procesu, dzięki czemu
spacje i listy CMake rozdzielone średnikami pozostają nienaruszone.

W Windows pamięci podręczne CMake i pliki zależności kompilatora znajdują
się poniżej krótkiego `BuildRoot` bootstrapu, pogrupowane według
stabilnego hasza ścieżki projektu oraz targetu/płytki. Ostateczne pliki
ELF, BIN, HEX, UF2, MAP, OTA oraz spatchowana baza danych buildu
pozostają pod zadeklarowanym `buildDir` projektu. `refresh-intellisense`
odczytuje surową bazę danych z krótkiego drzewa CMake i zapisuje jej
stabilną kopię projektową. Każdy udany build odświeża też artefakty z
wybranego drzewa targetu, więc no-op Ninja po zmianie targetu nie może
pozostawić w `buildDir` firmware innego targetu. Rozpoczęcie nowego
buildu usuwa dotychczasowy zestaw artefaktów gotowych do wgrania; jeśli
konfiguracja lub build zawiedzie, poprzedni obraz targetu nie może
pozostać dostępny do późniejszego wgrania. `clean` usuwa obie zarządzane
lokalizacje po zastosowaniu standardowych kontroli bezpieczeństwa ścieżek.

Projekty ESP-IDF używają swojego zadeklarowanego `buildDir` bezpośrednio
zamiast krótkiego katalogu głównego pamięci podręcznej CMake. Runner
produkcyjny nadal wymusza, aby katalog znajdował się poniżej katalogu
głównego `.build` projektu lub repozytorium. Zapisuje wyłącznie
ścieżki względne w `jh_esp_idf_artifacts.json`, dzięki czemu manifest oraz
wybrane artefakty bootloadera, tabeli partycji, aplikacji, logu i
konfiguracji mogą być wgrywane z Windows CI bez osadzania ścieżki
bezwzględnej specyficznej dla runnera.

Użyj natywnego PowerShell do zbudowania fixture'a buildu/konsolidacji
ESP32-S3 Fazy 3:

```powershell
.\vscode\entry\jh-vscode.cmd build `
  --project .\tests\fixtures\esp32s3_phase3
```

Ten fixture sprawdza tylko build i nie jest sondą akceptacji sprzętowej
wgrywania/monitorowania. Projekty urządzeń używają `list-ports`, `upload`
i `monitor` z portem COM zgłoszonym przez interfejs USB Serial/JTAG
płytki.

Wybrany rekord COM musi pasować do tożsamości programatora `303a:1001` z
rejestru płytek. Nieaktualny port, niepasujące VID/PID lub kilka
automatycznie wykrytych dopasowań jest odrzucane. Wgrywanie kooperacyjnie
zwalnia monitor będący własnością JaszczurHAL i pozwala mu ponownie
połączyć się po zresetowaniu płytki przez ESP-IDF. `--allow-unverified-port`
to jawny wentyl bezpieczeństwa dla celowo wybranego `--port`; generowane
zadania go nie używają. Buildy Debug ESP32-S3 i zarządzane profile
Cortex-Debug nie są dostarczane.

GitHub Actions buduje wygenerowany projekt użytkownika ze ścieżki
zawierającej spacje dla RP2040, RP2350 ARM, RP2350 RISC-V i STM32G474 na
natywnym Windows. Bramka sprawdza konfigurację Ninja, docelową bibliotekę
statyczną tam, gdzie ma zastosowanie, reprezentatywny firmware,
zadeklarowane artefakty, spatchowaną bazę danych buildu, ustawienia
ostrzeżeń MSVC oraz widoczną klasyfikację jako wyłączone testów hosta
POSIX/FreeRTOS/BearSSL niezgodnych z Windows. Zadanie MSVC buduje i
uruchamia ukierunkowany test dymny HAL CRC oraz przenośny interfejs
nagłówka gniazd BSD z `/W4 /permissive- /WX`. Pełny adapter BSD eksportuje
nazwy symboli POSIX i pozostaje testem firmware'u/hosta linuksowego
zamiast udawać, że implementuje odmienne ABI Winsock. Natywna integracja
BearSSL również pozostaje wyłącznie linuksowa, ponieważ jej harness i
transport używają Bash i gniazd POSIX.

Istniejące zadanie `windows-tooling` buforuje też przypięty checkout
ESP-IDF i oficjalne narzędzia, wykonuje czysty build produkcyjny
projektu `tests/fixtures/esp32s3_phase3`, który sprawdza tylko build, oraz
przesyła jego relokowalny manifest, log buildu, bootloader, tabelę
partycji i obrazy aplikacji. Udany build CI nie ustala zachowania
sprzętowego runtime dla Fazy 3.

Ten checkout nie deklaruje profilu analizy statycznej dla Windows.
Bieżący zarządzany zestaw narzędzi Windows oraz ten host nie dostarczają
ani `clang-tidy`, ani `cppcheck`, a narzędzia MSVC Build Tools nie są
przypiętym komponentem bootstrapu. Ścisły skompilowany profil ostrzeżeń
MSVC pozostaje bramką hosta Windows. Dodaj profil analizy statycznej
wyłącznie razem z uwierzytelnionym, przypiętym wersją plikiem binarnym
analizatora, aby wyniki lokalne i CI nie mogły cicho dryfować.

## Rozwiązywanie problemów

Zacznij od sprawdzenia hosta i komponentów tylko do odczytu:

```powershell
.\runmefirst.ps1 -VerifyOnly
```

Typowe ścieżki niepowodzeń to:

- Checkout osiągnięty przez `\\wsl.localhost\...` jest odrzucany. Sklonuj
  lub przenieś repozytorium na lokalny wolumin Windows, taki jak `C:`, i
  uruchom tam natywny bootstrap.
- GNU Arm zgłasza brakujące nagłówki C++ lub Ninja nie może utworzyć
  plików zależności. Utrzymuj `ToolsRoot` i `BuildRoot` krótkie, a
  następnie zweryfikuj ustawienia długich ścieżek zarówno Windows, jak i
  Git, jak opisano powyżej.
- `jh-vscode.cmd` zgłasza niekompletne środowisko launchera. Uruchom
  konfigurację ponownie i sprawdź
  `.build\windows\host-environment.json`; launcher wymaga zarządzanego
  lub jawnie wybranego interpretera Pythona do zaimportowania pyserial.
- Wgrywanie przez COM zgłasza odmowę dostępu lub zajęty port. Uruchom
  `Project: List ports` lub `jh-vscode.cmd list-ports --project <path>` i
  sprawdź zgłoszoną tożsamość i PID właściciela monitora. Przekazanie
  wgrywania zamyka wyłącznie zweryfikowany monitor JaszczurHAL; zamknij
  ręcznie niepowiązane programy terminalowe.
- Port COM ESP32-S3 jest odrzucany jako niezweryfikowany. Potwierdź, że
  Menedżer urządzeń lub `list-ports --json` zgłasza VID/PID USB
  `303a:1001` dla wybranego portu; odłącz zduplikowane pasujące płytki
  lub przekaż jawnie zamierzony zweryfikowany port COM.
- Widoczne jest więcej niż jedno urządzenie BOOTSEL. Odłącz dodatkową
  płytkę lub przekaż zamierzony katalog główny dysku/GUID woluminu przez
  `--bootsel-volume`; runtime nadal weryfikuje jego
  etykietę i system plików FAT.
- Cortex-Debug nie może uruchomić OpenOCD ani GDB. Uruchom
  `debug-tools --json` dla wybranego projektu, potwierdź zgłoszone pliki,
  a następnie sprawdź sondę i target w Menedżerze urządzeń. Zmiany
  driverów pozostają osobną czynnością administracyjną.
- Wykrywanie callbacku OTA działa, ale transfer nie może połączyć się z
  powrotem z hostem. Utrzymuj aktywny profil sieciowy Windows jako
  `Private` i sprawdź zakresową regułę bez jej zmieniania:

  ```powershell
  .\.build\windows\venv\Scripts\python.exe `
    .\scripts\configure_ota_firewall.py --check
  ```

Wybór urządzenia, własność monitora, bezpieczeństwo BOOTSEL i zachowanie
zadań są opisane w [JaszczurHAL VS Code Entry](../../vscode/README.md).
Odzyskiwanie OTA oraz diagnostyka próby/wycofania (trial/rollback)
znajdują się w [Natywny Workflow OTA](OTAWorkflow.md).

## Aktualna granica wsparcia

Dostępne są: natywny launcher, wspólny runtime
buildu, generowane nadpisanie zadania VS Code, polityka końca linii,
menedżer komponentów, bootstrap hosta, macierz firmware CMake dla
czterech rodzin, ścieżki wgrywania COM/BOOTSEL, backend zapory OTA,
wykrywanie narzędzi debugowania, przenośna bramka nagłówka gniazd,
produkcyjny build/upload/monitor ESP32-S3 ESP-IDF oraz CI
Windows. Pełne testy integracyjne gniazd POSIX, FreeRTOS POSIX oraz
BearSSL sterowane przez Bash pozostają wyłącznie linuksowe. Natywny
callback OTA dla Windows, potwierdzenie próbne (trial) i automatyczne
wycofanie zostały zweryfikowane na Pico 2 W przez zaufaną sieć LAN
`Private`. Sprzęt OTA wymaga lokalnych poświadczeń fixture'a; debugowanie
sprzętowe dodatkowo wymaga podłączonej sondy SWD. Desktopowy
SerialConfigurator Fiesta pozostaje aplikacją linuksową i znajduje się
poza zakresem natywnego workflow firmware'u dla Windows.
