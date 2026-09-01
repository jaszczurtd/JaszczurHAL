# Bezpieczeństwo łańcucha dostaw

*Dostępne również [po angielsku](../en/security_supply_chain.md).*

Ten dokument opisuje podstawowy proces tworzenia SBOM oraz śledzenia
podatności stosowany w JaszczurHAL.

## Zakres

Ewidencja łańcucha dostaw obejmuje:

- źródła zewnętrznych komponentów dołączone do `src/`,
- kopie zewnętrznych źródeł w ściśle określonych wersjach, obsługiwane przez
  skrypt aktualizujący komponenty,
  w tym BearSSL, cJSON, LodePNG, TJpg_Decoder, FatFs, Unity, lwIP, littlefs,
  BTstack, sterownik Semtech SX126x, FreeRTOS-Kernel, Pico SDK i ESP-IDF,
- dokładne wersje narzędzi binarnych i narzędzi programistycznych napisanych
  w Pythonie, wybrane dla `esp32` i `esp32s3` na podstawie rejestru narzędzi
  ESP-IDF o ustalonej wersji,
- zaadaptowany kod projektów zewnętrznych, w którym lokalne zmiany mogą
  wpływać na bezpieczeństwo.

Ten spis nie zastępuje analizy firmware konkretnego produktu. Projekty
korzystające z biblioteki powinny generować lub zachowywać własny SBOM,
ponieważ o tym, które opcjonalne moduły trafią do firmware, decydują aktywne
flagi `HAL_ENABLE_*`.

<a id="native-ota-security-boundary"></a>

## Granica bezpieczeństwa natywnego OTA

Natywne OTA dla RP uwierzytelnia wersjonowany nagłówek obrazu za pomocą
HMAC-SHA256, a przed aktywacją weryfikuje SHA-256 danych obrazu i CRC nagłówka.
Symetryczny klucz HMAC jest wyprowadzany z tego samego hasła aplikacji, którego
używa uwierzytelnianie transportu. Każdy, kto zna to hasło, może przygotować
akceptowany obraz. Produkty powinny więc używać unikalnego sekretu o wysokiej
entropii, przekazywanego narzędziu obsługującemu projekty VS Code przez
`ota.passwordEnv`,
a nie
hasła wpisanego bezpośrednio do pliku przechowywanego w repozytorium.

Ani transport, ani obraz nie są szyfrowane, więc ten mechanizm nie zapewnia
poufności firmware. Metadane obrazu są uwierzytelniane, ale nie pełnią funkcji
licznika chroniącego przed instalacją starszej wersji. Starszy obraz podpisany
aktualnym sekretem może zostać ponownie wgrany, jeśli aplikacja lub proces
przygotowania produktu nie wymusi bardziej restrykcyjnej polityki wersji.
Fizyczny dostęp do BOOTSEL wyznacza granicę odzyskiwania i początkowego
przygotowania urządzenia.

OTA dla ESP32-S3 przesyła nieprzetworzony plik BIN aplikacji wskazany przez
zweryfikowany manifest artefaktów ESP-IDF. Host sprawdza rozmiar i SHA-256
zapisane w manifeście, a urządzenie weryfikuje MD5 protokołu i przed wybraniem
nieaktywnej partycji OTA uruchamia walidację obrazu ESP-IDF.

Oba targety używają AUTH2, gdy firmware ma skonfigurowane niepuste hasło.
Mechanizm oblicza HMAC-SHA256. Kluczem jest skrót MD5 hasła zapisany jako tekst
ASCII z małymi literami szesnastkowymi. Podpis wiąże ze sobą polecenie, port
zwrotnego
połączenia TCP, rozmiar obrazu, jego MD5 oraz niezależne 16-bajtowe liczby
jednorazowe (nonce) urządzenia i klienta. Urządzenie wiąże uwierzytelnienie
z adresem IP i portem źródłowym pakietu zaproszenia UDP, a połączenie zwrotne
kieruje do tego
samego adresu IPv4. Host używa połączonego gniazda UDP i wymaga, aby adres
drugiej strony połączenia TCP był zgodny z adresem wybranego partnera UDP.

Obie wartości nonce są generowane przez bezpieczny generator losowy platformy.

Ścisłe parsowanie ASCII odrzuca niejednoznaczne białe znaki, osadzone znaki
NUL, alternatywne zapisy liczb, nieprawidłowe długości i nadmiarowe pola.
Niepuste hasło hosta wyklucza użycie bezpośredniego `OK`, starszego `AUTH` oraz
starszego uwierzytelniania `200`. Po błędzie alokacji muteksu usługa na każdym
z targetów pozostaje zatrzymana; niezabezpieczona ścieżka transportu nie jest
uruchamiana.

AUTH2 zapewnia symetryczne uwierzytelnianie hasłem, ale nie jest współczesnym
mechanizmem podpisywania obrazów ani szyfrowaniem. Pominięcie hasła urządzenia
lub ustawienie pustego ciągu wyłącza AUTH2. `ota.allowEmptyPassword=true`
pozwala hostowi kontynuować wyłącznie w tym jawnie nieuwierzytelnionym trybie
deweloperskim.
Autentyczność produktu, poufność i ochrona przed instalacją starszej wersji
na ESP32-S3 wymagają odpowiedniej konfiguracji ESP-IDF Secure Boot V2,
szyfrowania pamięci flash, eFuse, chronionych kluczy i odzyskiwania. Zwykłe
operacje wgrywania i testy nie włączają nieodwracalnych ustawień eFuse.

Miejsce przechowywania sekretów, zakres reguł zapory sieciowej, pierwszą
instalację, wycofywanie aktualizacji i odzyskiwanie opisano w dokumencie
[Natywne aktualizacje OTA](OTAWorkflow.md#shared-auth2-transport-authentication).

## Pliki

| Plik | Przeznaczenie |
|------|---------------|
| `security/third_party.json` | Ręcznie utrzymywane, miarodajne źródło informacji o dołączonych komponentach i ich ściśle określonych wersjach. |
| `security/third_party.schema.json` | Schemat JSON używany podczas przeglądu struktury spisu. |
| `security/sbom.cdx.json` | Generowany SBOM CycloneDX dla repozytorium biblioteki. |
| `security/esp_idf_tools.json` | Zweryfikowany wykaz dokładnych wersji narzędzi dla targetów ESP-IDF, ich licencji i projektów źródłowych, rewizji frameworka oraz skrótu `tools.json`. |
| `security/vulnerability_log.md` | Ręcznie utrzymywany rejestr oceny podatności i poprawek. |
| `SECURITY.md` | Zasady zgłaszania, wstępnej oceny, klasyfikacji ważności i utrzymania. |
| `scripts/generate_sbom.py` | Generator SBOM działający offline i używający wyłącznie biblioteki standardowej Pythona. |
| `scripts/sync_generated.py` | Wspólny skrypt odświeżający wszystkie generowane artefakty przechowywane w repozytorium, w tym SBOM, i weryfikujący je w trybie tylko do odczytu. |
| `scripts/check_release_metadata.py` | Bramka wydania sprawdzająca VERSION, SBOM, nazwę tagu i pochodzenie z głównej gałęzi. |
| `scripts/check_vulnerabilities.sh` | Opcjonalny skrypt uruchamiający dostępne lokalnie skanery podatności. |

## Generowanie SBOM

```bash
python3 scripts/sync_generated.py --write
```

Wspólny skrypt uruchamia generator SBOM, który odczytuje
`security/third_party.json` i `security/esp_idf_tools.json`, po czym zapisuje
`security/sbom.cdx.json`. Wynik jest deterministyczny, więc zwykłe ponowne
wygenerowanie powinno powodować niewielkie i łatwe do przejrzenia zmiany.

## Pochodzenie narzędzi ESP-IDF

`third_party/esp_idf_version.conf` wskazuje jeden dokładny commit ESP-IDF v6.0.2
i wybiera `esp32` oraz `esp32s3`. Plik `security/esp_idf_tools.json` zapisuje
odpowiadający im oficjalny zestaw narzędzi: Xtensa GDB/GCC, dodatkowy pakiet
RISC-V GCC,
narzędzia ESP32 ULP, Espressif OpenOCD, dane ROM ELF oraz jedenaście narzędzi
napisanych w Pythonie, dostarczanych przez producenta i wskazanych bezpośrednio
przez główne wymagania ESP-IDF. Pozycje dotyczące Pythona obejmują esptool,
zarządzanie komponentami,
monitor, core dump, Kconfig, generowanie partycji NVS, analizę rozmiaru,
diagnostykę, dekoder komunikatów o awariach typu panic, wiązania Pythona dla
Clang oraz
narzędzia GDB dla FreeRTOS. Ogólne zależności przechodnie Pythona nadal są
określane przez wymagania środowiska ESP-IDF i nie są powielane w tym wykazie.

Menedżer komponentów sprawdza każdy wymieniony pakiet Pythona po uruchomieniu
instalatora ESP-IDF. Jeśli plik ograniczeń dostarczany przez ESP-IDF wskaże inną
zgodną wersję, skrypt konfigurujący ponownie wymusza dokładne, zweryfikowane
wersje zgodne z tymi ograniczeniami, zanim dopuści kompilację. Klucz pamięci
podręcznej ESP-IDF w CI uwzględnia zarówno dokładną wersję frameworka, jak i
ten wykaz. Dzięki temu zatwierdzona zmiana narzędzia tworzy nowe środowisko
zamiast używać nieaktualnych pakietów.

`scripts/generate_sbom.py` przekształca wykaz bezpośrednio w komponenty
CycloneDX oznaczone zakresem `development`, uwzględniając zweryfikowaną licencję
SPDX każdego narzędzia. Narzędzia nie są kopiowane do
`security/third_party.json`, który pozostaje źródłem danych o frameworku i
pozostałych zewnętrznych komponentach repozytorium.

Każda produkcyjna kompilacja ESP-IDF generuje również
`generated/jaszczurhal/jh_esp_idf_toolchain.json` i umieszcza go w
`jh_esp_idf_artifacts.json`. Ten zapis konkretnej kompilacji zawiera faktycznie
użyte wersje kompilatora, CMake, Ninja, interpretera Pythona używanego przez IDF
i esptool oraz SHA-256
pliku `tools.json` frameworka, bez bezwzględnych ścieżek z hosta. Manifest
artefaktów osobno zapisuje dokładną rewizję ESP-IDF, skrót końcowego `sdkconfig`,
profil, offset i skrót tablicy partycji oraz skrót każdego wgrywanego obrazu.
Zweryfikowany wykaz określa narzędzia wybrane przez ustaloną wersję frameworka,
natomiast manifest kompilacji wskazuje narzędzia, które rzeczywiście utworzyły
dany firmware.

## Sprawdzanie podatności

```bash
./scripts/check_vulnerabilities.sh
```

Skrypt najpierw ponownie generuje SBOM. Jeśli dostępny jest `osv-scanner`,
skanuje drzewo źródeł repozytorium, w tym dołączone zależności C/C++, które
potrafi rozpoznać. Jeżeli dostępny jest `cve-bin-tool`, można także przeskanować
wygenerowany SBOM CycloneDX:

```bash
JH_SECURITY_SCAN_SOURCE=1 ./scripts/check_vulnerabilities.sh
```

Skrypt celowo nie instaluje narzędzi. CI i lokalne stacje robocze powinny
dostarczać zaufane wersje skanerów. W lokalnym środowisku typu Debian/Ubuntu
`./runmefirst.sh` instaluje domyślne narzędzia używane przez repozytorium:
`osv-scanner` do kontroli źródeł i dołączonych zależności oraz `cve-bin-tool`
do opcjonalnej kontroli podatności na podstawie SBOM.

## Weryfikacja aktualności SBOM

```bash
python3 scripts/sync_generated.py --check
```

Polecenie weryfikuje w trybie tylko do odczytu wszystkie generowane artefakty
przechowywane w repozytorium, w tym tymczasowo wygenerowany plik SBOM
porównywany
z `security/sbom.cdx.json`. Skrypt `./scripts/check_sbom.sh` służy do osobnego
sprawdzania SBOM i uruchamia tę samą kontrolę.

## Polityka CI

GitHub Actions uruchamia wymagane zadanie `test` oraz zależne zadanie
`security-scan` dla pull requestów, zmian wysyłanych do `main`, według
harmonogramu tygodniowego i na żądanie. Zadanie testowe weryfikuje wszystkie
generowane artefakty, w tym SBOM. Zadanie bezpieczeństwa:

- instaluje `osv-scanner` i `cve-bin-tool`,
- uruchamia `osv-scanner` dla drzewa źródeł repozytorium,
- uruchamia `cve-bin-tool` dla SBOM CycloneDX.

Skanowanie jest celowo oddzielone od zadań kompilacji, testów i analizy
statycznej.
Błędy skanerów bezpieczeństwa można oceniać niezależnie od błędów
kompilatora lub testów, a uruchomienia cykliczne wykrywają nowo opublikowane
CVE nawet wtedy, gdy kod się nie zmienił.

Zasady obsługi znalezisk:

- Podatności o ważności krytycznej i wysokiej blokują wydanie, chyba że
  zapisano decyzję `not_affected`.
- Podatności o ważności średniej wymagają wpisu z oceną przed wydaniem.
- Podatności o ważności niskiej mogą poczekać na planowane prace utrzymaniowe,
  ale również powinny zostać zapisane.
- Wpisy dotyczące modułów włączanych opcjonalnie powinny wskazywać
  odpowiednie flagi `HAL_ENABLE_*` oraz obsługiwane targety.

## Bramka wydania

Przed utworzeniem tagu wydania sprawdź, czy `VERSION` i wersja projektu w
SBOM są zgodne:

```bash
python3 scripts/check_release_metadata.py
```

Utwórz odpowiadający tag dopiero po włączeniu commita wydania do `main`.
CI uruchamiane po utworzeniu tagu dodatkowo sprawdza jego nazwę i potwierdza,
że oznaczony
commit jest przodkiem `origin/main`; tag z rozbieżnej gałęzi wydania zostaje
odrzucony. CI uruchamia też na hoście kompletny zestaw testów pod ASan/UBSan
oraz krótkie testy fuzzingowe parserów HTTP, WebSocket i wgrywania plików
multipart.
ThreadSanitizer pozostaje opcjonalną bramką lokalną dostępną przez
`-DJH_ENABLE_THREAD_SANITIZER=ON`.

## Aktualizowanie komponentu

1. W przypadku zarządzanego komponentu zewnętrznego zaktualizuj wskazaną wersję
   w `third_party/*_version.conf` i uruchom `./third_party/update_components.sh`.
   W przypadku kodu dołączonego do repozytorium zaktualizuj źródła w ich
   dotychczasowej lokalizacji.
2. Zachowaj oryginalne pliki licencji i informacje o autorstwie.
3. Zaktualizuj w `security/third_party.json` wersję, tag, rewizję, purl lub
   odwołanie do projektu źródłowego.
4. Dla ESP-IDF odśwież `security/esp_idf_tools.json` na podstawie pliku
   `tools.json` z ustalonej wersji frameworka i zarządzanego środowiska Python,
   a następnie sprawdź licencję każdego narzędzia.
5. Uruchom `python3 scripts/sync_generated.py --write` i przejrzyj zmiany
   wygenerowanych artefaktów.
6. Uruchom testy właściwe dla zmienionego modułu i odpowiedniego targetu.
7. Jeśli aktualizacja naprawia lub ocenia CVE, dodaj wpis do
   `security/vulnerability_log.md` zawierający CVSS, flagi, których dotyczy
   problem, i decyzję.

## Zasady oceny podatności

Zacznij od spisu komponentów, a następnie oceń osiągalność podatnego kodu:

- `not_affected`: podatny kod nie występuje, nie jest kompilowany albo nie jest
  osiągalny w obsługiwanej integracji HAL.
- `affected`: kod występuje i jest osiągalny w co najmniej jednej obsługiwanej
  konfiguracji HAL.
- `fixed`: repozytorium zawiera poprawkę lub zaktualizowaną wersję komponentu.
- `mitigated`: udokumentowana konfiguracja lub ograniczenia w czasie działania
  zmniejszają praktyczny wpływ, lecz nie usuwają podatnego kodu.
- `under_investigation`: potrzebna jest dalsza analiza.

Dla produktów wbudowanych zapisz, które flagi `HAL_ENABLE_*` i targety sprawiają,
że podatny kod jest osiągalny. Podatność w `HAL_ENABLE_MQTT` albo
`HAL_ENABLE_WIREGUARD` nie wpływa automatycznie na firmware zbudowane bez tych
modułów.
