# Natywna konfiguracja dla Windows

*Dostępne również [po angielsku](../en/windows_setup.md).*

JaszczurHAL udostępnia skrypt inicjalizacyjny, który przygotowuje natywne
środowisko Windows do tworzenia firmware'u. Instaluje ściśle określone wersje
narzędzi bez użycia WSL, Git Bash, wingetu czy Chocolatey i bez globalnej
instalacji toolchainu.

Minimalna obsługiwana wersja systemu to Windows 10 1809 (build 17763) na
platformie AMD64. Git for Windows oraz VS Code muszą być już zainstalowane,
aby można było pobrać i otworzyć repozytorium. Skrypt zarządza Pythonem,
CMake, Ninja, GNU Arm Embedded, GNU RISC-V, OpenOCD, picotoolem, pyserial oraz
zależnościami źródłowymi w ściśle określonych wersjach. Przy pierwszym użyciu
narzędzie obsługujące ESP32-S3 przygotowuje ponadto dokładnie wskazany commit
repozytorium ESP-IDF oraz jego oficjalne narzędzia dla targetu. Są one
przechowywane w `third_party\esp-idf` oraz
`%USERPROFILE%\.espressif`.
Sprawdzenie kompletności GNU Arm obejmuje GDB, a ponowne wykorzystanie
OpenOCD wymaga skryptów CMSIS-DAP, ST-Link, RP2040, RP2350 i STM32G4
używanych przez generowane konfiguracje debugowania.

## Ustawienia hosta

Natywny build firmware'u wymaga włączenia obsługi długich ścieżek w dwóch
miejscach:

- w Windows: `LongPathsEnabled=1` w kluczu
  `HKLM\SYSTEM\CurrentControlSet\Control\FileSystem`;
- w Git: `core.longpaths=true`.

Domyślna konfiguracja sprawdza oba ustawienia. Jeśli brakuje ustawienia Git,
wypisuje polecenie potrzebne do jego wprowadzenia. Jawne użycie opcji
`-ConfigureHost` pozwala skryptowi ustawić `core.longpaths`. Skrypt może zmienić
wartość w rejestrze Windows tylko wtedy, gdy został uruchomiony z sesji
PowerShell działającej już z uprawnieniami administratora. Nigdy sam nie
podnosi uprawnień procesu.

Skanowanie przez oprogramowanie ochrony stacji końcowych może znacznie
spowolnić build CMake/Ninja lub umieszczać nowe pliki `.exe`, `.elf` i `.uf2`
w kwarantannie. Utrzymuj krótkie ścieżki do zarządzanych narzędzi i katalogów
buildu. Jeśli pomiary wydajności albo zdarzenia kwarantanny wykażą taką
potrzebę, poproś administratora lub zespół bezpieczeństwa o wyjątki ograniczone
do niezbędnych katalogów.

Skrypt inicjalizacyjny nie zmienia konfiguracji oprogramowania antymalware.

## Konfiguracja

Przechowuj kopię roboczą repozytorium na lokalnym woluminie Windows, takim jak
`C:`. Uruchomienie natywnego skryptu inicjalizacyjnego przez ścieżkę UNC WSL
(`\\wsl.localhost\...`) jest odrzucane, ponieważ Git for Windows nie może
bezpiecznie zarządzać i aktualizować takiej kopii roboczej.

Uruchom Windows PowerShell 5.1 lub nowszy z katalogu głównego repozytorium:

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File .\runmefirst.ps1
```

Przed pierwszą zmianą konfiguracji hosta lub systemu plików skrypt wyświetla
pełny plan działania. Domyślne lokalizacje to krótkie ścieżki w profilu
użytkownika:

```text
%USERPROFILE%\.jh\tools
%USERPROFILE%\.jh\build
```

Zarządzana instalacja Pythona 3.12 znajduje się w katalogu narzędzi. Izolowane
środowisko z biblioteką pyserial jest tworzone w
`.build\windows\venv`, gdzie znajduje je `jh-vscode.cmd` bez globalnej
zmiany `PATH`.

Skrypt inicjalizacyjny zapisuje też
`.build\windows\host-environment.json`. Na podstawie tego pliku wspólny
runtime wybiera zweryfikowane ścieżki do CMake, Ninja, GNU Arm, GNU RISC-V,
OpenOCD, picotoola i Pythona, a także zarządzany katalog
główny buildu. `-VerifyOnly` porównuje zapisany stan bajt po bajcie z bieżącą
konfiguracją.

Tryb edytora dodaje też wyznaczone ścieżki debuggera do standardowego profilu
użytkownika VS Code jako `cortex-debug.openocdPath.windows` i
`cortex-debug.armToolchainPath.windows`. Istniejące komentarze JSONC i
niepowiązane ustawienia są zachowywane. Przed zmianą istniejącego pliku skrypt
zapisuje kopię `settings.json.jaszczurhal.bak`; `-VerifyOnly` sprawdza obie
wartości bez zapisu. `-FirmwareOnly` pozostawia profil VS Code niezmieniony.

Przydatne tryby to:

```powershell
# Zezwala na naprawę udokumentowanych ustawień hosta dla długich ścieżek.
.\runmefirst.ps1 -ConfigureHost

# Jawnie zezwala na instalację zalecanych rozszerzeń VS Code.
.\runmefirst.ps1 -InstallExtensions

# Wybiera zarządzane narzędzia w określonych wersjach zamiast zgodnych instalacji systemowych.
.\runmefirst.ps1 -Force

# Sprawdza komponenty i wymagania hosta bez wprowadzania zmian.
.\runmefirst.ps1 -VerifyOnly

# Sprawdza narzędzia do buildu firmware'u bez interfejsu graficznego i VS Code.
.\runmefirst.ps1 -FirmwareOnly
```

`-VerifyOnly` nie może być łączony z żadną opcją wymagającą zgody. Brakujący,
zmodyfikowany lub nieaktualny komponent powoduje niepowodzenie kontroli bez
podejmowania próby naprawy.

`-FirmwareOnly` zmienia wyłącznie klasyfikację pozycji na końcowej liście: VS
Code i jego rozszerzenia pozostają opcjonalne, natomiast każdy warunek wstępny
potrzebny do buildu firmware'u nadal jest wymagany. CI łączy ten tryb z
`-ConfigureHost` podczas konfiguracji i używa go ponownie podczas przebiegu
weryfikacji tylko do odczytu.

Skrypt inicjalizacyjny korzysta ze zgodnych instalacji CMake, Ninja, GNU Arm i
OpenOCD dostępnych w systemie, chyba że użyto `-Force`. Zarządzane archiwa są
weryfikowane za pomocą SHA-256, wypakowywane przez atomową podmianę katalogu i opisywane w
pełnym manifeście zawartości. Końcowy raport zawiera wszystkie wyznaczone
ścieżki do plików wykonywalnych. Ponowne
wykorzystanie OpenOCD wymaga ponadto pełnego katalogu
`share\openocd\scripts`, umieszczonego obok programu lub w standardowej
lokalizacji. Niekompletny pakiet systemowy jest pomijany na rzecz zarządzanego
archiwum. Ponowne uruchomienie konfiguracji pozostawia poprawne komponenty
niezmienione.

## Cortex-Debug i drivery sond

Skrypt inicjalizacyjny dla Windows konfiguruje Cortex-Debug na podstawie
zweryfikowanych danych hosta. Aby zdiagnozować lub sprawdzić ścieżki wyznaczone
dla danego projektu, użyj `debug-tools`:

```powershell
.\vscode\entry\jh-vscode.cmd debug-tools `
  --project .\examples\01_core_runtime `
  --target rp2350-arm --board pico2w --json
```

Wynik zawiera `openocd`, `gdb`, `armToolchainPath`, katalog główny skryptów
OpenOCD oraz parę plików konfiguracji interfejsu diagnostycznego i targetu.
Cortex-Debug odnajduje `arm-none-eabi-gdb` w skonfigurowanym katalogu
toolchainu. Generowane konfiguracje uruchomieniowe wybierają kompletną
konfigurację OpenOCD dla każdego profilu i nie zależą od ustawień lokalnych dla
projektu:
`cortex-debug.gdbPath`, katalogu głównego skryptów ani SVD. Zarządzane
archiwum OpenOCD dla Raspberry Pi korzysta z umieszczonego obok katalogu
skryptów bez globalnej zmiany `PATH`.

Pico i Pico 2 podłączone przez USB w trybie BOOTSEL są targetami debugowania,
ale takie połączenie nie udostępnia sondy SWD. Do debugowania przez SWD
potrzebna jest osobna sonda Raspberry Pi Debug Probe, Pico z firmware'em Debug
Probe/Picoprobe albo zgodna sonda podłączona do SWD. Standardowa konfiguracja
RP używa `interface/cmsis-dap.cfg`. Profil NUCLEO-G474RE korzysta z
wbudowanego ST-Linka przez `board/st_nucleo_g4.cfg`, który wybiera SWD oraz
sposób obsługi resetu sprzętowego płytki. Nie jest potrzebna osobna sonda ani
zewnętrzne okablowanie SWD. Generowane profile uruchomieniowe RP
ustawiają `adapter speed 5000` dla RP2040 i `adapter speed 2000` dla
RP2350. Nie usuwaj tych poleceń: same skrypty CMSIS-DAP i targetu domyślnie
ograniczają prędkość do zaledwie 100 kHz, a wykrywanie pamięci flash RP2350
może trwać dłużej niż domyślny timeout zdalnej komunikacji w GDB i
zdesynchronizować początkową wymianę pakietów w Windows.

Skrypt inicjalizacyjny sporządza listę podłączonych sond, ale nie instaluje,
nie zastępuje ani nie zmienia powiązania (`rebind`) driverów USB w Windows.
Jeśli OpenOCD zgłasza brak pasującego urządzenia CMSIS-DAP, sprawdź najpierw
fizyczne połączenie SWD i Menedżer urządzeń. Zmiana drivera to osobna czynność
administracyjna: zidentyfikuj dokładny interfejs sondy, zapoznaj się z
aktualnymi instrukcjami producenta sondy dla Windows i uzyskaj zgodę przed
jej zmianą. Nie stosuj drivera USB do interfejsu pamięci masowej Pico
BOOTSEL.

Podstawowy test sprzętowy w natywnym środowisku Windows przeprowadzono z użyciem
oficjalnej sondy Raspberry Pi Debug Probe z firmware'em 2.3.1 oraz Pico 2 W
jako targetu RP2350 Arm. Podłącz `SWDIO` sondy do `SWDIO` targetu, `SWCLK`
sondy do `SWCLK` targetu oraz połącz ich masy. Windows obsłużył sondę za pomocą
drivera Microsoft WinUSB. Nie było potrzebne ani instalowanie drivera, ani
zmiana jego powiązania (`rebind`). Zarządzany OpenOCD wykrył oba rdzenie
Cortex-M33, a GDB z zarządzanej instalacji GNU Arm załadował obraz ELF z buildu
Debug, zatrzymał się na `main`, wznowił wykonanie do `app_start` i odłączył się.
Po końcowym `reset run` ponownie pojawił się port USB CDC aplikacji. W kolejnym
teście DoomConsole również załadowano obraz ELF z buildu Debug i zatrzymano
wykonanie na `app_start` przy tych samych ustawieniach profilu uruchomieniowego.

Podstawowy test sprzętowy STM32 przeprowadzono w Windows 10 LTSC z użyciem
NUCLEO-G474RE z wbudowanym ST-Linkiem V3J9M3 (`0483:374e`). Zarządzany OpenOCD
`0.12.0+dev (2026-07-01-10:44)` wykrył Cortex-M4 r0p1, 512 KiB
dwubankowej pamięci flash, sześć punktów przerwania (breakpointów) i cztery
punkty obserwacji (watchpointy). GDB z zarządzanej instalacji GNU Arm wgrał
reprezentatywny obraz `01_core_runtime` z buildu Debug, zatrzymał się najpierw
na `main`, a potem na `app_start`, po czym poprawnie się odłączył i wykonał
`reset run`. Dla tej płytki użyj wygenerowanego profilu
`board/st_nucleo_g4.cfg`. Konfiguracja ograniczona do `interface/stlink.cfg` i
`target/stm32g4x.cfg` może nie wykryć targetu, jeśli płytka wymaga konfiguracji
sprzętowego resetu Nucleo.

OpenOCD może zgłosić przestarzały firmware Debug Probe/Picoprobe i
włączyć wolniejszy tryb zgodności. To ostrzeżenie nie uniemożliwia
debugowania SWD. Zaktualizuj firmware sondy osobno, korzystając z
instrukcji producenta sondy, gdy niższa szybkość transferu ma znaczenie.
Skrypt inicjalizacyjny JaszczurHAL nie modyfikuje firmware'u sondy.

Pobieranie przez HTTPS w Windows korzysta z systemowego `curl.exe` oraz
magazynu zaufania Schannel, dopuszcza wyłącznie przekierowania HTTPS i wymaga
TLS 1.2 lub nowszego. Dzięki temu firmowe mechanizmy inspekcji TLS mogą działać
bez wyłączania walidacji certyfikatów. Przed wypakowaniem każde pobrane
archiwum musi nadal odpowiadać przypisanemu do niego skrótowi SHA-256.

## Układ buildu firmware

Do buildu firmware'u domyślnie używany jest Ninja zarówno w Windows, jak i w
systemach Unix. Projekt może wybrać inny generator przez `cmake.generator` w
`.vscode/jaszczurhal.project.json`. Runtime przekazuje
swój aktualny zweryfikowany interpreter Pythona jako `Python3_EXECUTABLE`,
włącza `CMAKE_EXPORT_COMPILE_COMMANDS` i każdą wyznaczoną ścieżkę do pliku
wykonywalnego hosta przekazuje procesowi jako pojedynczy argument. Dzięki temu
spacje i listy CMake rozdzielone średnikami pozostają nienaruszone.

W Windows cache CMake i pliki zależności kompilatora znajdują się pod krótką
ścieżką `BuildRoot` utworzoną przez skrypt inicjalizacyjny. Katalogi są
pogrupowane według stabilnego skrótu ścieżki projektu oraz targetu i płytki.
Końcowe pliki ELF, BIN, HEX, UF2, MAP i OTA oraz dostosowana baza poleceń
buildu pozostają w zadeklarowanym `buildDir` projektu.
`refresh-intellisense` odczytuje pierwotną bazę z krótkiego drzewa CMake i
zapisuje jej stabilną kopię w projekcie.

Każdy udany build odświeża też artefakty z wybranego drzewa targetu. Dzięki
temu po zmianie targetu Ninja nie pozostawi w `buildDir` firmware'u
poprzedniego targetu, nawet jeśli nie ma nic do przebudowania. Rozpoczęcie
nowego buildu usuwa dotychczasowy zestaw artefaktów gotowych do wgrania. Jeśli
konfiguracja lub build zawiedzie, poprzedni obraz targetu nie może pozostać
dostępny do późniejszego wgrania.

`clean` usuwa obie zarządzane lokalizacje po wykonaniu standardowych kontroli
bezpieczeństwa ścieżek.

Projekty ESP-IDF używają zadeklarowanego `buildDir` bezpośrednio zamiast
krótkiego drzewa cache CMake. Produkcyjne narzędzie nadal wymaga, aby katalog
znajdował się w katalogu `.build` projektu lub repozytorium albo w jego
podkatalogu. W `jh_esp_idf_artifacts.json` zapisuje wyłącznie ścieżki względne.
Dzięki temu manifest oraz wybrane artefakty bootloadera, tabeli partycji,
aplikacji, logu i konfiguracji można przekazywać przez CI w Windows bez
osadzania bezwzględnych ścieżek zależnych od środowiska wykonawczego.

Użyj natywnego PowerShell, aby zbudować projekt testowy sprawdzający build i
linkowanie ESP32-S3 w fazie 3:

```powershell
.\vscode\entry\jh-vscode.cmd build `
  --project .\tests\fixtures\esp32s3_phase3
```

Ten projekt służy wyłącznie do sprawdzenia buildu; nie jest sprzętowym testem
akceptacyjnym wgrywania ani monitorowania. Projekty urządzeń używają
`list-ports`, `upload` i `monitor` z portem COM zgłoszonym przez interfejs USB
Serial/JTAG płytki.

Wybrany rekord COM musi pasować do identyfikatora programatora `303a:1001` z
rejestru płytek. Nieaktualny port, niezgodne VID/PID lub kilka automatycznie
wykrytych urządzeń powodują odrzucenie operacji. Przed wgraniem JaszczurHAL
zwalnia port zajęty przez własny monitor i pozwala mu połączyć się ponownie po
zresetowaniu płytki przez ESP-IDF. `--allow-unverified-port`
jawnie wyłącza tę kontrolę dla świadomie wybranego `--port`; generowane
zadania nie używają tej opcji. Buildy ESP32-S3 w konfiguracji Debug ani
zarządzane profile Cortex-Debug nie są dostarczane.

GitHub Actions buduje wygenerowany projekt użytkownika ze ścieżki
zawierającej spacje dla RP2040, RP2350 ARM, RP2350 RISC-V i STM32G474 na
platformie Windows. Bramka sprawdza konfigurację Ninja, target CMake tworzący
bibliotekę statyczną - tam, gdzie ma zastosowanie - reprezentatywny firmware,
zadeklarowane artefakty, dostosowaną bazę poleceń buildu i ustawienia
ostrzeżeń MSVC. Sprawdza również, czy testy hosta POSIX, FreeRTOS i BearSSL,
które nie są zgodne z Windows, zostały jawnie oznaczone jako wyłączone.
Zadanie MSVC buduje i uruchamia podstawowy test HAL CRC oraz przenośny interfejs
nagłówka gniazd BSD z `/W4 /permissive- /WX`. Pełny adapter BSD eksportuje
nazwy symboli POSIX i pozostaje testem przeznaczonym dla firmware'u lub hosta
z Linuksem, zamiast udawać, że implementuje odmienne ABI Winsock. Natywna
integracja BearSSL również pozostaje wyłącznie linuksowa, ponieważ jej
środowisko testowe i transport używają Bash i gniazd POSIX.

Istniejące zadanie `windows-tooling` umieszcza też w cache dokładnie wskazany
commit ESP-IDF i oficjalne narzędzia, wykonuje czysty build produkcyjny
projektu `tests/fixtures/esp32s3_phase3`, który sprawdza tylko build, oraz
przesyła jego relokowalny manifest, log buildu, bootloader, tabelę
partycji i obrazy aplikacji. Udany build CI nie potwierdza zachowania runtime
na sprzęcie w fazie 3.

To repozytorium nie definiuje profilu analizy statycznej dla Windows. Ani
bieżący zestaw zarządzanych narzędzi dla Windows, ani ten host nie udostępniają
`clang-tidy` ani `cppcheck`, a narzędzia MSVC Build Tools nie są komponentem
zarządzanym przez skrypt inicjalizacyjny. Build MSVC z rygorystycznym zestawem
ostrzeżeń pozostaje obowiązkowym testem jakości dla Windows. Profil analizy
statycznej dodaj wyłącznie razem z uwierzytelnionym plikiem wykonywalnym
analizatora o ściśle
określonej wersji, aby wyniki lokalne i CI nie mogły niepostrzeżenie się
rozjechać.

## Rozwiązywanie problemów

Zacznij od kontroli hosta i komponentów, która nie wprowadza zmian:

```powershell
.\runmefirst.ps1 -VerifyOnly
```

Typowe problemy i sposoby ich rozwiązania:

- Kopia robocza dostępna przez `\\wsl.localhost\...` jest odrzucana. Sklonuj
  lub przenieś repozytorium na lokalny wolumin Windows, taki jak `C:`, i
  uruchom tam natywny skrypt inicjalizacyjny.
- GNU Arm zgłasza brakujące nagłówki C++ lub Ninja nie może utworzyć
  plików zależności. Utrzymuj `ToolsRoot` i `BuildRoot` krótkie, a
  następnie zweryfikuj ustawienia długich ścieżek zarówno w Windows, jak i w
  Git, zgodnie z opisem powyżej.
- `jh-vscode.cmd` zgłasza niekompletne środowisko skryptu uruchamiającego.
  Uruchom konfigurację ponownie i sprawdź
  `.build\windows\host-environment.json`. Skrypt uruchamiający wymaga
  zarządzanego lub jawnie wybranego interpretera Pythona, w którym można
  zaimportować pyserial.
- Wgrywanie przez COM zgłasza odmowę dostępu lub zajęty port. Uruchom
  `Project: List ports` lub `jh-vscode.cmd list-ports --project <path>` i
  sprawdź zgłoszoną tożsamość i PID procesu monitora. Przed wgraniem zamykany
  jest wyłącznie zweryfikowany monitor JaszczurHAL; niepowiązane programy
  terminalowe trzeba zamknąć ręcznie.
- Port COM ESP32-S3 jest odrzucany jako niezweryfikowany. Potwierdź, że
  Menedżer urządzeń lub `list-ports --json` zgłasza VID/PID USB
  `303a:1001` dla wybranego portu; odłącz pozostałe pasujące płytki albo jawnie
  wskaż właściwy zweryfikowany port COM.
- Widoczne jest więcej niż jedno urządzenie BOOTSEL. Odłącz dodatkową
  płytkę lub przez `--bootsel-volume` wskaż właściwy katalog główny dysku bądź
  GUID woluminu; runtime nadal weryfikuje jego etykietę i system plików FAT.
- Cortex-Debug nie może uruchomić OpenOCD ani GDB. Uruchom
  `debug-tools --json` dla wybranego projektu, potwierdź zgłoszone pliki,
  a następnie sprawdź sondę i target w Menedżerze urządzeń. Zmiany
  driverów pozostają osobną czynnością administracyjną.
- Wykrywanie urządzenia przez OTA działa, ale urządzenie nie może nawiązać
  połączenia zwrotnego z hostem. Utrzymuj aktywny profil sieciowy Windows jako
  `Private` i sprawdź regułę zapory o ograniczonym zakresie bez jej zmieniania:

  ```powershell
  .\.build\windows\venv\Scripts\python.exe `
    .\scripts\configure_ota_firewall.py --check
  ```

Wybór urządzenia, zarządzanie monitorem, bezpieczeństwo BOOTSEL i działanie
zadań opisano w
[punkcie wejścia JaszczurHAL dla VS Code](../../vscode/README.pl.md).
Odzyskiwanie OTA oraz diagnostykę rozruchu próbnego i przywracania poprzedniej
wersji opisano w dokumencie [Natywna aktualizacja OTA](OTAWorkflow.md).

## Obecny zakres wsparcia

Obsługiwane są: natywny skrypt uruchamiający i wspólny runtime buildu,
generowane definicje zastępujące zadania VS Code, zasady zakończeń wierszy,
menedżer komponentów oraz skrypt inicjalizacyjny hosta.
Zakres obejmuje też macierz buildów firmware'u w CMake dla czterech rodzin,
wgrywanie przez COM i BOOTSEL, backend zapory OTA, wykrywanie narzędzi
debugowania, test przenośności nagłówka gniazd, produkcyjne operacje `build`,
`upload` i `monitor` ESP32-S3 w ESP-IDF oraz CI w Windows.

Pełne testy integracyjne gniazd POSIX, FreeRTOS POSIX oraz BearSSL sterowane
przez Bash pozostają wyłącznie linuksowe. Mechanizm połączenia zwrotnego OTA w
Windows, potwierdzanie rozruchu próbnego i automatyczne przywracanie poprzedniej
wersji zostały zweryfikowane na Pico 2 W w zaufanej sieci LAN o profilu
`Private`.

Testy sprzętowe OTA wymagają lokalnych danych uwierzytelniających stanowiska;
debugowanie sprzętowe wymaga dodatkowo podłączonej sondy SWD. Desktopowy
SerialConfigurator Fiesta pozostaje aplikacją linuksową i znajduje się
poza zakresem natywnej obsługi firmware'u w Windows.
