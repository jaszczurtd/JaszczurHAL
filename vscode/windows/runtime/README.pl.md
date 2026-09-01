# Środowisko wykonawcze Windows

Publiczny interfejs CLI jest wspólny z Linuksem i udostępniany przez `entry/`.

`entry/jh-vscode.cmd` znajduje Pythona i uruchamia ten sam wspólny runtime co
skrypt uruchamiający dla Uniksa. Buildy firmware korzystają z narzędzi
zapisanych podczas przygotowania środowiska, używają utrzymywanego przez
środowisko katalogu głównego CMake o krótkiej ścieżce oraz zachowują lokalne
artefakty projektu i zmodyfikowaną bazę poleceń kompilacji. Obsługa portu
szeregowego, procesów, blokad i BOOTSEL właściwa dla Windows jest dostępna
przez adapter platformy.

Natywny adapter wykrywa i normalizuje porty COM przez pyserial, w tym `COM10+`,
oraz przekazuje wspólnemu mechanizmowi weryfikacji tożsamości VID, PID, numer
seryjny, producenta, produkt, interfejs, lokalizację, HWID i opis. W przypadku
ogólnego sterownika Windows `usbser` odczytuje też nazwę produktu zgłaszaną
przez nadrzędne urządzenie magistrali PnP, ponieważ pyserial zwraca wtedy
producenta sterownika i pomija nazwę produktu USB. Gdy Windows nie udostępnia
producenta, minimalny zestaw pozwalający potwierdzić tożsamość obejmuje dokładną
nazwę produktu oraz zgodne, skonfigurowane wartości VID i PID.

Stale działające monitory zapisują znacznik z numerem wersji w katalogu
tymczasowym użytkownika i używają osobnego dla każdego portu pliku
sygnalizującego zwolnienie jako uzgodnionego kanału sterowania. Przed awaryjnym
zakończeniem system sprawdza, czy PID nie został użyty ponownie, oraz weryfikuje
czas uruchomienia procesu.

Do blokad buildu służy `msvcrt.locking`, więc system zwalnia je po zakończeniu
procesu. Diagnostyka zajętego portu traktuje odmowę dostępu w
Windows jako błąd blokady i podaje zweryfikowany PID ze znacznika monitora, gdy
system nie potrafi wskazać właściciela portu COM.

Wykrywanie BOOTSEL pobiera listę katalogów głównych dysków za pomocą
`GetLogicalDriveStringsW`, odczytuje etykietę i system plików przez
`GetVolumeInformationW`, a identyfikator GUID woluminu ustala przez
`GetVolumeNameForVolumeMountPointW`. Kandydatami są wyłącznie woluminy FAT lub
FAT32 oznaczone `RPI-RP2`, `RP2350` albo `RPI-RP2350`. Wspólny runtime tworzy na
podstawie identyfikatorów GUID migawkę stanu sprzed przełączenia urządzenia
i odmawia automatycznego wgrania, gdy pojawi się więcej niż jeden nowy kandydat.
`--bootsel-volume` lub ustawienie `bootselVolume` w lokalnej konfiguracji
użytkownika pozwala jawnie wybrać jeden sprawdzony katalog dysku albo GUID.

Przed otwarciem urządzenia docelowego mechanizm wgrywania UF2 sprawdza liczby
magiczne bloków, rozmiary danych, kompletność sekwencji, poprawność grupowania
według rodziny, blok
`absolute-ignore` RP2350 oraz scalone obrazy OTA, których globalna sekwencja
obejmuje identyfikatory wielu rodzin. Implementacja dla Windows przesyła dane
strumieniowo bez metadanych, wykrywa zmianę źródła i niepełne zapisy, wymusza
zapis buforów pliku na nośnik i zamyka uchwyt przed zgłoszeniem powodzenia.
Odmowa dostępu, nośnik tylko do odczytu, odłączenie dysku i błędy zapisu
pozostają błędami wgrywania.

Testy natywnego środowiska Windows sprawdzają rzeczywiste wykrywanie woluminów
i wymuszanie trwałego zapisu danych z uchwytu pliku na nośnik; wspólny test
regresji wgrywa też przez warstwę adaptera scalony obraz OTA obejmujący wiele
rodzin. Podstawowy test wgrywania COM-to-BOOTSEL na rzeczywistym urządzeniu
przeszedł na RP2040 i RP2350.

Wspólny runtime odnajduje też za pośrednictwem `debug-tools` utrzymywane przez
projekt narzędzia GNU Arm GDB i OpenOCD oraz skrypty targetów OpenOCD dla
CMSIS-DAP/ST-Link. Rzeczywista sesja debugowania Cortex nadal wymaga osobno
podłączonej sondy SWD.
