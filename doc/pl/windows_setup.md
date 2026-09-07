<a id="natywna-konfiguracja-dla-windows"></a>

# Przygotowanie środowiska Windows

*Dostępne również [po angielsku](../en/windows_setup.md).*

Skrypt konfiguracyjny JaszczurHAL przygotowuje środowisko do tworzenia firmware bezpośrednio w Windows. Instaluje ustalone wersje narzędzi bez WSL, Git Bash, wingetu i Chocolatey; nie wymaga też globalnej instalacji zestawu narzędzi kompilacyjnych.

Minimalna obsługiwana wersja systemu to Windows 10 1809 (build 17763) na
platformie AMD64. Git for Windows oraz VS Code muszą być już zainstalowane,
aby można było pobrać i otworzyć repozytorium. Skrypt zarządza Pythonem,
CMake, Ninja, GNU Arm Embedded, GNU RISC-V, OpenOCD, picotoolem, pyserial oraz
zależnościami źródłowymi w ściśle określonych wersjach. Przy pierwszym użyciu
narzędzie obsługujące ESP32-S3 przygotowuje ponadto dokładnie wskazany commit
repozytorium ESP-IDF oraz jego oficjalne narzędzia dla wybranej platformy. Są one
przechowywane w `third_party\esp-idf` oraz
`%USERPROFILE%\.espressif`.
Sprawdzenie kompletności GNU Arm obejmuje GDB, a ponowne wykorzystanie
OpenOCD wymaga skryptów CMSIS-DAP, ST-Link, RP2040, RP2350 i STM32G4
używanych przez generowane konfiguracje debugowania.

## Ustawienia hosta

Kompilacja firmware bezpośrednio w Windows wymaga obsługi długich ścieżek w dwóch miejscach:

- w Windows: `LongPathsEnabled=1` w kluczu
  `HKLM\SYSTEM\CurrentControlSet\Control\FileSystem`;
- w Git: `core.longpaths=true`.

Skrypt domyślnie sprawdza oba ustawienia, ale ich nie zmienia. Gdy Git nie ma włączonej obsługi długich ścieżek, wyświetla potrzebne polecenie. Opcja `-ConfigureHost` zezwala na ustawienie `core.longpaths`. Zmiana rejestru Windows jest możliwa tylko wtedy, gdy skrypt uruchomiono w sesji PowerShell z uprawnieniami administratora. Skrypt sam nie podnosi uprawnień.

Oprogramowanie ochrony stacji roboczej może spowalniać kompilację CMake/Ninja lub przenosić nowe pliki `.exe`, `.elf` i `.uf2` do kwarantanny. Stosuj krótkie ścieżki do narzędzi i katalogów kompilacji. Jeżeli pomiary lub zgłoszenia kwarantanny potwierdzają problem, uzgodnij z administratorem albo zespołem bezpieczeństwa wyjątki ograniczone do niezbędnych katalogów.

Skrypt inicjalizacyjny nie zmienia konfiguracji oprogramowania antymalware.

<a id="konfiguracja"></a>

## Instalacja i konfiguracja narzędzi

Umieść kopię roboczą repozytorium na lokalnym woluminie Windows, np. `C:`. Skrypt odrzuca ścieżki UNC do WSL (`\\wsl.localhost\...`), ponieważ Git for Windows nie może bezpiecznie zarządzać taką kopią i jej aktualizować.

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

Skrypt zapisuje konfigurację w `.build\windows\host-environment.json`. Narzędzia projektu odczytują stąd zweryfikowane ścieżki do CMake, Ninja, GNU Arm, GNU RISC-V, OpenOCD, picotoola i Pythona oraz główny katalog kompilacji. Opcja `-VerifyOnly` porównuje ten zapis bajt po bajcie z konfiguracją ustaloną podczas bieżącego sprawdzenia.

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

Opcji `-VerifyOnly` nie można łączyć z opcjami zezwalającymi na zmiany konfiguracji. Brakujący, zmodyfikowany lub nieaktualny komponent powoduje niepowodzenie kontroli; skrypt nie naprawia go w tym trybie.

Opcja `-FirmwareOnly` zmienia jedynie klasyfikację na końcowej liście komponentów. VS Code i rozszerzenia stają się opcjonalne, natomiast wszystkie narzędzia niezbędne do tworzenia firmware pozostają wymagane. CI łączy ten tryb z `-ConfigureHost` podczas konfiguracji i ponownie używa go przy kontroli bez wprowadzania zmian.

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

<a id="cortex-debug-i-drivery-sond"></a>

## Cortex-Debug i sterowniki sond

Konfiguracja Cortex-Debug korzysta ze zweryfikowanych ścieżek zapisanych przez skrypt. Aby sprawdzić narzędzia wybrane dla konkretnego projektu, uruchom `debug-tools`:

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
nie zastępuje ani nie zmienia powiązania (`rebind`) sterowników USB w Windows.
Jeśli OpenOCD zgłasza brak pasującego urządzenia CMSIS-DAP, sprawdź najpierw
fizyczne połączenie SWD i Menedżer urządzeń. Zmiana sterownika to osobna czynność
administracyjna: zidentyfikuj dokładny interfejs sondy, zapoznaj się z
aktualnymi instrukcjami producenta sondy dla Windows i uzyskaj zgodę przed
jej zmianą. Nie stosuj sterownika USB do interfejsu pamięci masowej Pico
BOOTSEL.

Podstawowy test sprzętowy w natywnym środowisku Windows przeprowadzono z użyciem
oficjalnej sondy Raspberry Pi Debug Probe z firmware'em 2.3.1 oraz Pico 2 W
jako targetu RP2350 Arm. Podłącz `SWDIO` sondy do `SWDIO` targetu, `SWCLK`
sondy do `SWCLK` targetu oraz połącz ich masy. Windows obsłużył sondę za pomocą
sterownika Microsoft WinUSB. Nie było potrzebne ani instalowanie sterownika, ani
zmiana jego powiązania (`rebind`). Zarządzany OpenOCD wykrył oba rdzenie
Cortex-M33, a GDB z zarządzanej instalacji GNU Arm załadował obraz ELF z kompilacji
Debug, zatrzymał się na `main`, wznowił wykonanie do `app_start` i odłączył się.
Po końcowym `reset run` ponownie pojawił się port USB CDC aplikacji. W kolejnym
teście DoomConsole również załadowano obraz ELF z kompilacji Debug i zatrzymano
wykonanie na `app_start` przy tych samych ustawieniach profilu uruchomieniowego.

Podstawowy test sprzętowy STM32 przeprowadzono w Windows 10 LTSC z użyciem
NUCLEO-G474RE z wbudowanym ST-Linkiem V3J9M3 (`0483:374e`). Zarządzany OpenOCD
`0.12.0+dev (2026-07-01-10:44)` wykrył Cortex-M4 r0p1, 512 KiB
dwubankowej pamięci flash, sześć punktów przerwania (breakpointów) i cztery
punkty obserwacji (watchpointy). GDB z zarządzanej instalacji GNU Arm wgrał
reprezentatywny obraz `01_core_runtime` z kompilacji Debug, zatrzymał się najpierw
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

<a id="układ-buildu-firmware"></a>

## Katalogi kompilacji i pliki wynikowe

Domyślnym generatorem firmware w Windows i systemach Unix jest Ninja. Inny generator wybiera się przez `cmake.generator` w `.vscode/jaszczurhal.project.json`. Narzędzia projektu przekazują zweryfikowany interpreter jako `Python3_EXECUTABLE` i włączają `CMAKE_EXPORT_COMPILE_COMMANDS`. Każda ścieżka do programu jest przekazywana jako jeden argument procesu, dzięki czemu spacje w ścieżkach i listy CMake rozdzielane średnikami nie są błędnie dzielone.

W Windows pliki pamięci podręcznej CMake i zależności kompilatora trafiają do krótkiego katalogu `BuildRoot` wybranego podczas konfiguracji. Podkatalogi są rozdzielone według stabilnego skrótu ścieżki projektu oraz platformy i płytki. Końcowe pliki ELF, BIN, HEX, UF2, MAP i OTA oraz dostosowana baza poleceń kompilacji pozostają w zadeklarowanym `buildDir`. Polecenie `refresh-intellisense` odczytuje bazę z krótkiego katalogu CMake i zapisuje jej kopię w stałej lokalizacji projektu.

Każda udana kompilacja odświeża pliki wynikowe dla wybranej platformy, także wtedy, gdy Ninja nie musi niczego przebudować. Po zmianie platformy w `buildDir` nie pozostaje więc firmware z poprzedniego wyboru. Rozpoczęcie nowej kompilacji usuwa zestaw plików przeznaczonych do wgrania; błąd konfiguracji lub kompilacji nie pozostawia starego obrazu, który można byłoby później omyłkowo wysłać do urządzenia.

Polecenie `clean` usuwa oba zarządzane katalogi po sprawdzeniu bezpieczeństwa ścieżek.

Projekty ESP-IDF używają zadeklarowanego `buildDir` bezpośrednio zamiast
krótkiego drzewa cache CMake. Produkcyjne narzędzie nadal wymaga, aby katalog
znajdował się w katalogu `.build` projektu lub repozytorium albo w jego
podkatalogu. W `jh_esp_idf_artifacts.json` zapisuje wyłącznie ścieżki względne.
Dzięki temu manifest oraz wybrane artefakty bootloadera, tabeli partycji,
aplikacji, logu i konfiguracji można przekazywać przez CI w Windows bez
osadzania bezwzględnych ścieżek zależnych od środowiska wykonawczego.

Aby sprawdzić kompilację i linkowanie projektu testowego ESP32-S3 dla fazy 3, uruchom w PowerShell:

```powershell
.\vscode\entry\jh-vscode.cmd build `
  --project .\tests\fixtures\esp32s3_phase3
```

Projekt ten sprawdza wyłącznie kompilację i linkowanie. Nie potwierdza działania wgrywania ani monitora na sprzęcie. Projekty urządzeń korzystają z `list-ports`, `upload` i `monitor` oraz z portu COM udostępnianego przez interfejs USB Serial/JTAG płytki.

Wybrany rekord COM musi pasować do identyfikatora programatora `303a:1001` z
rejestru płytek. Nieaktualny port, niezgodne VID/PID lub kilka automatycznie
wykrytych urządzeń powodują odrzucenie operacji. Przed wgraniem JaszczurHAL
zwalnia port zajęty przez własny monitor i pozwala mu połączyć się ponownie po
zresetowaniu płytki przez ESP-IDF. `--allow-unverified-port`
jawnie wyłącza tę kontrolę dla świadomie wybranego `--port`; generowane
zadania nie używają tej opcji. Konfiguracje Debug dla ESP32-S3 ani
zarządzane profile Cortex-Debug nie są dostarczane.

GitHub Actions kompiluje w Windows wygenerowaną aplikację ze ścieżki zawierającej spacje dla RP2040, RP2350 ARM, RP2350 RISC-V i STM32G474. Kontrola obejmuje konfigurację Ninja, cel CMake tworzący bibliotekę statyczną tam, gdzie ma to zastosowanie, przykładowy firmware, zadeklarowane pliki wynikowe, dostosowaną bazę poleceń kompilacji i ustawienia ostrzeżeń MSVC. Sprawdza też, czy niezgodne z Windows testy hosta POSIX, FreeRTOS i BearSSL są jawnie oznaczone jako wyłączone. Zadanie MSVC kompiluje i uruchamia podstawowy test HAL CRC oraz sprawdzenie przenośności nagłówka gniazd BSD z `/W4 /permissive- /WX`. Pełny adapter BSD eksportuje symbole POSIX, a nie ABI Winsock, dlatego jego testy są przeznaczone dla firmware lub hosta z Linuksem. Testy integracyjne BearSSL również wymagają Linuksa, ponieważ korzystają z Bash i gniazd POSIX.

Zadanie `windows-tooling` przechowuje w pamięci podręcznej ustaloną rewizję ESP-IDF i oficjalne narzędzia. Wykonuje czystą kompilację produkcyjną projektu `tests/fixtures/esp32s3_phase3`, a następnie publikuje manifest z przenośnymi ścieżkami, log kompilacji, bootloader, tablicę partycji i obrazy aplikacji. Powodzenie CI nie potwierdza działania funkcji fazy 3 na sprzęcie.

Repozytorium nie definiuje profilu analizy statycznej dla Windows. Opisany zestaw zarządzanych narzędzi i środowisko hosta nie udostępniają `clang-tidy` ani `cppcheck`; MSVC Build Tools również nie są komponentem o wersji ustalanej przez skrypt konfiguracyjny. Obowiązkową kontrolą dla Windows pozostaje kompilacja MSVC z rygorystycznymi ostrzeżeniami. Profil analizy statycznej należy dodawać wraz ze zweryfikowanym plikiem wykonywalnym analizatora o ustalonej wersji, aby wyniki lokalne i wyniki CI pozostały porównywalne.

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
  GUID woluminu; narzędzie nadal weryfikuje jego etykietę i system plików FAT.
- Cortex-Debug nie może uruchomić OpenOCD ani GDB. Uruchom
  `debug-tools --json` dla wybranego projektu, potwierdź zgłoszone pliki,
  a następnie sprawdź sondę i urządzenie docelowe w Menedżerze urządzeń. Zmiany
  sterowników pozostają osobną czynnością administracyjną.
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

<a id="obecny-zakres-wsparcia"></a>

## Zakres obsługi

Obsługa Windows obejmuje uruchamianie i kompilację projektów, generowane nadpisania zadań VS Code, zasady zakończeń wierszy, zarządzanie komponentami oraz konfigurację hosta. Dostępne są także kompilacje CMake dla czterech rodzin mikrokontrolerów, wgrywanie przez COM i BOOTSEL, konfiguracja zapory dla OTA, wykrywanie narzędzi debugowania, kontrola przenośności nagłówka gniazd, produkcyjne operacje ESP32-S3 `build`, `upload` i `monitor` w ESP-IDF oraz zadania CI w Windows.

Pełne testy integracyjne gniazd POSIX, FreeRTOS POSIX oraz BearSSL sterowane
przez Bash pozostają wyłącznie linuksowe. Mechanizm połączenia zwrotnego OTA w
Windows, potwierdzanie rozruchu próbnego i automatyczne przywracanie poprzedniej
wersji zostały zweryfikowane na Pico 2 W w zaufanej sieci LAN o profilu
`Private`.

Testy sprzętowe OTA wymagają lokalnych danych uwierzytelniających stanowiska;
debugowanie sprzętowe wymaga dodatkowo podłączonej sondy SWD. Desktopowy
SerialConfigurator Fiesta pozostaje aplikacją linuksową i znajduje się
poza zakresem natywnej obsługi firmware'u w Windows.
