# Runtime Windows

Publiczny interfejs CLI jest wspólny z Linuksem i udostępniany przez `entry/`.

`entry/jh-vscode.cmd` rozwiązuje Pythona i uruchamia ten sam wspólny runtime co
launcher uniksowy. Buildy firmware rozwiązują narzędzia zapisane przez
bootstrap, używają krótkiego zarządzanego katalogu CMake oraz zachowują lokalne
artefakty projektu i poprawione compile commands. Implementacje serial, własności
procesów, locków i BOOTSEL właściwe dla Windows znajdują się za adapterem
platformy.

Adapter native wylicza i normalizuje porty COM przez pyserial, w tym `COM10+`,
oraz udostępnia matcherowi tożsamości VID, PID, numer seryjny, producenta,
produkt, interfejs, lokalizację, HWID i opis. Dla ogólnego drivera Windows
`usbser` odczytuje też produkt raportowany przez nadrzędne urządzenie PnP,
ponieważ pyserial podaje wtedy producenta drivera i pomija produkt USB. Gdy
Windows nie udostępnia producenta, minimalnym sprawdzonym fallbackiem jest
dokładny produkt wraz ze skonfigurowanymi VID i PID. Trwałe monitory publikują
wersjonowany znacznik w katalogu tymczasowym użytkownika i używają pliku
zwolnienia portu jako kanału współpracy. Przed awaryjnym zakończeniem procesu
sprawdzane są ponowne użycie PID i czas startu. Locki builda używają
`msvcrt.locking`, więc system zwalnia je po zakończeniu procesu. Diagnostyka
zajętego portu traktuje odmowę dostępu Windows jako błąd locka i raportuje
sprawdzony PID znacznika monitora, gdy system nie potrafi wskazać właściciela
portu COM.

Wykrywanie BOOTSEL wylicza główne katalogi dysków przez
`GetLogicalDriveStringsW`, czyta etykietę i filesystem przez
`GetVolumeInformationW`, a GUID woluminu rozwiązuje przez
`GetVolumeNameForVolumeMountPointW`. Kandydatami są wyłącznie woluminy FAT lub
FAT32 oznaczone `RPI-RP2`, `RP2350` albo `RPI-RP2350`. Wspólny runtime używa
GUID do snapshotu przed touch i odmawia automatycznego wgrania, gdy pojawi się
więcej niż jeden nowy kandydat. `--bootsel-volume` lub lokalne
`bootselVolume` wybiera jawnie jeden sprawdzony katalog dysku albo GUID.

Przed otwarciem celu upload UF2 sprawdza magic bloków, rozmiary payloadów,
kompletność sekwencji, grupy rodzin, blok absolute-ignore RP2350 oraz scalone
obrazy OTA, których globalna sekwencja obejmuje wiele family ID. Ścieżka Windows
kopiuje dane bez metadanych, wykrywa zmianę źródła i krótkie zapisy, opróżnia
handle pliku i zamyka go przed zgłoszeniem sukcesu. Odmowa dostępu, nośnik tylko
do odczytu, odłączenie dysku i błędy zapisu pozostają błędami uploadu. Testy
native Windows sprawdzają rzeczywiste wyliczanie woluminów i trwałe opróżnianie
handle; wspólny test regresji wgrywa też fixture scalonego OTA wielu rodzin
przez granicę adaptera. Smoke test rzeczywistego uploadu COM-to-BOOTSEL przeszedł
na RP2040 i RP2350. Wspólny runtime rozwiązuje też zarządzane GNU Arm GDB,
OpenOCD oraz skrypty targetów CMSIS-DAP/ST-Link przez `debug-tools`; rzeczywista
sesja Cortex nadal wymaga osobno podłączonego debug probe SWD.
