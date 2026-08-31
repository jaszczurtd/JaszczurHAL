# Bezpieczeństwo łańcucha dostaw

*Dostępne również [po angielsku](../en/security_supply_chain.md).*

Ten dokument opisuje uproszczony proces tworzenia SBOM oraz śledzenia
podatności stosowany w JaszczurHAL.

## Zakres

Śledzony łańcuch dostaw obejmuje:

- źródła zewnętrznych komponentów dołączone do `src/`,
- przypięte zewnętrzne checkouty obsługiwane przez aktualizator komponentów,
  w tym BearSSL, cJSON, LodePNG, TJpg_Decoder, FatFs, Unity, lwIP, littlefs,
  BTstack, driver Semtech SX126x, FreeRTOS-Kernel, Pico SDK i ESP-IDF,
- dokładne wersje narzędzi binarnych i narzędzi Python wybrane dla `esp32` i
  `esp32s3` przez przypięty rejestr narzędzi ESP-IDF,
- zaadaptowany kod upstream, w którym lokalne zmiany mogą wpływać na
  bezpieczeństwo.

Ten spis nie zastępuje analizy firmware konkretnego produktu. Projekty
korzystające z biblioteki powinny generować lub zachowywać własny SBOM,
ponieważ o tym, które opcjonalne moduły trafią do firmware, decydują aktywne
flagi `HAL_ENABLE_*`.

<a id="native-ota-security-boundary"></a>

## Granice bezpieczeństwa natywnego OTA

Natywne OTA dla RP uwierzytelnia wersjonowany nagłówek obrazu za pomocą
HMAC-SHA256, a przed aktywacją weryfikuje SHA-256 payloadu i CRC nagłówka.
Symetryczny klucz HMAC jest wyprowadzany z tego samego hasła aplikacji, którego
używa uwierzytelnianie transportu. Każdy, kto zna to hasło, może przygotować
akceptowany obraz. Produkty powinny więc używać unikalnego sekretu o wysokiej
entropii, przekazywanego do dispatchera VS Code przez `ota.passwordEnv`, a nie
hasła zapisanego bezpośrednio w śledzonym pliku.

Transport ani obraz nie są szyfrowane; ten mechanizm nie zapewnia poufności
firmware. Generowanie obrazu uwierzytelnia metadane, ale nie tworzy licznika
anti-rollback. Starszy obraz podpisany aktualnym sekretem może zostać ponownie
wgrany, jeśli aplikacja lub warstwa provisioningu produktu nie wymusi bardziej
restrykcyjnej polityki wersji. Fizyczny dostęp do BOOTSEL pozostaje granicą
odzyskiwania i provisioningu.

OTA dla ESP32-S3 przesyła surowy plik BIN aplikacji wskazany przez zweryfikowany
manifest artefaktów ESP-IDF. Host sprawdza rozmiar i SHA-256 zapisane w
manifeście, a urządzenie weryfikuje MD5 protokołu i przed wybraniem nieaktywnej
partycji OTA uruchamia walidację obrazu ESP-IDF.

Oba targety używają AUTH2, gdy firmware ma skonfigurowane niepuste hasło.
Mechanizm oblicza HMAC-SHA256 z kluczem będącym zapisanym małymi znakami ASCII
skrótem MD5 hasła i wiąże ze sobą polecenie, port zwrotnego połączenia TCP,
rozmiar obrazu, jego MD5 oraz niezależne 16-bajtowe nonce urządzenia i klienta.
Urządzenie przypisuje uwierzytelnianie do adresu UDP i portu źródłowego
zaproszenia, a połączenie zwrotne kieruje do tego samego adresu IPv4. Host używa
połączonego gniazda UDP i wymaga, aby peer TCP odpowiadał wybranemu peerowi UDP.
Oba generatory nonce korzystają z bezpiecznego źródła losowego platformy.
Ścisłe parsowanie ASCII odrzuca niejednoznaczne białe znaki, osadzone znaki
NUL, alternatywne zapisy liczb, nieprawidłowe długości i nadmiarowe pola.
Niepuste hasło hosta nie może użyć fallbacku do bezpośredniego `OK`, starszego
`AUTH` ani starszego uwierzytelniania `200`. Błąd alokacji muteksu zatrzymuje
usługę na obu targetach zamiast przełączać ją na transport bez blokady.

AUTH2 zapewnia symetryczne uwierzytelnianie hasłem, ale nie jest współczesnym
podpisem obrazu ani szyfrowaniem. Pominięcie hasła urządzenia lub ustawienie
pustego ciągu wyłącza AUTH2. `ota.allowEmptyPassword=true` pozwala hostowi
kontynuować wyłącznie w tym jawnie nieuwierzytelnionym trybie developerskim.
Autentyczność produktu, poufność i polityka anti-rollback na ESP32-S3 wymagają
odpowiedniej konfiguracji ESP-IDF Secure Boot V2, szyfrowania flash, eFuse,
chronionych kluczy i odzyskiwania. Zwykłe uploady i testy nie włączają
nieodwracalnych ustawień eFuse.

Miejsce przechowywania sekretów, zakres reguł firewalla, pierwsza instalacja,
rollback i odzyskiwanie opisano w dokumencie
[Natywny workflow OTA](OTAWorkflow.md#shared-auth2-transport-authentication).

## Pliki

| Plik | Przeznaczenie |
|------|---------------|
| `security/third_party.json` | Ręcznie utrzymywane źródło informacji o dołączonych i przypiętych komponentach. |
| `security/third_party.schema.json` | Schemat JSON używany podczas przeglądu struktury spisu. |
| `security/sbom.cdx.json` | Generowany SBOM CycloneDX dla repozytorium biblioteki. |
| `security/esp_idf_tools.json` | Zweryfikowany snapshot dokładnych wersji narzędzi targetu ESP-IDF, ich licencji i źródeł upstream, commita frameworka oraz skrótu `tools.json`. |
| `security/vulnerability_log.md` | Ręcznie utrzymywany rejestr oceny podatności i poprawek. |
| `SECURITY.md` | Zasady zgłaszania, triage, klasyfikacji ważności i utrzymania. |
| `scripts/generate_sbom.py` | Generator SBOM działający offline i używający wyłącznie biblioteki standardowej Pythona. |
| `scripts/sync_generated.py` | Wspólny mechanizm odświeżania i weryfikacji tylko do odczytu wszystkich śledzonych artefaktów generowanych, w tym SBOM. |
| `scripts/check_release_metadata.py` | Bramka release'u sprawdzająca VERSION, changelog, SBOM, nazwę taga i pochodzenie z głównej gałęzi. |
| `scripts/check_vulnerabilities.sh` | Opcjonalny wrapper skanerów do lokalnego sprawdzania podatności. |

## Generowanie SBOM

```bash
python3 scripts/sync_generated.py --write
```

Wspólny runner uruchamia generator SBOM, który odczytuje
`security/third_party.json` i `security/esp_idf_tools.json`, po czym zapisuje
`security/sbom.cdx.json`. Wynik jest deterministyczny, więc zwykłe ponowne
wygenerowanie powinno tworzyć mały i łatwy do przejrzenia diff.

## Pochodzenie narzędzi ESP-IDF

`third_party/esp_idf_version.conf` przypina ESP-IDF v6.0.2 do jednego commita i
wybiera `esp32` oraz `esp32s3`. Plik `security/esp_idf_tools.json` zapisuje
odpowiadający im oficjalny zestaw narzędzi: Xtensa GDB/GCC, dodatkowy pakiet RISC-V GCC,
narzędzia ESP32 ULP, Espressif OpenOCD, dane ROM ELF oraz jedenaście narzędzi
Python dostarczanych przez producenta i wskazanych bezpośrednio przez główne
wymagania ESP-IDF. Pozycje Python obejmują esptool, zarządzanie komponentami,
monitor, core dump, Kconfig, generowanie partycji NVS, analizę rozmiaru,
diagnostykę, panic decoder, binding Pythona dla Clang oraz narzędzia GDB dla
FreeRTOS. Ogólne, przechodnie pakiety Python pozostają własnością wymagań
środowiska upstream i nie są duplikowane w tym snapshocie.
`scripts/generate_sbom.py` rozwija snapshot bezpośrednio do komponentów
CycloneDX o zakresie developerskim, uwzględniając zweryfikowaną licencję SPDX
każdego narzędzia. Narzędzia nie są kopiowane do
`security/third_party.json`, który pozostaje źródłem danych o frameworku i
pozostałych zewnętrznych komponentach repozytorium.

Każdy produkcyjny build ESP-IDF generuje również
`generated/jaszczurhal/jh_esp_idf_toolchain.json` i umieszcza go w
`jh_esp_idf_artifacts.json`. Ten zapis konkretnego buildu zawiera faktycznie
użyte wersje kompilatora, CMake, Ninja, IDF Python i esptool oraz SHA-256
frameworkowego `tools.json`, bez bezwzględnych ścieżek hosta. Manifest
artefaktów osobno zapisuje dokładny commit ESP-IDF, skrót końcowego `sdkconfig`,
profil, offset i skrót tablicy partycji oraz skrót każdego wgrywanego obrazu.
Zweryfikowany snapshot mówi, co wybiera pin, natomiast manifest buildu dowodzi,
co utworzyło konkretny firmware.

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

Polecenie weryfikuje tylko do odczytu wszystkie śledzone artefakty generowane,
w tym tymczasowy kandydat SBOM porównywany z `security/sbom.cdx.json`.
Wyspecjalizowane polecenie zgodności `./scripts/check_sbom.sh` deleguje do tej
samej kontroli SBOM.

## Polityka CI

GitHub Actions uruchamia wymagany job `test` oraz zależny job `security-scan`
dla pull requestów, pushy do `main`, według harmonogramu tygodniowego i na
żądanie. Job testowy weryfikuje wszystkie artefakty generowane, w tym SBOM.
Job bezpieczeństwa:

- instaluje `osv-scanner` i `cve-bin-tool`,
- uruchamia `osv-scanner` dla drzewa źródeł repozytorium,
- uruchamia `cve-bin-tool` dla SBOM CycloneDX.

Skanowanie jest celowo oddzielone od jobów buildu, testów i analizy statycznej.
Błędy skanerów bezpieczeństwa można poddawać triage niezależnie od błędów
kompilatora lub testów, a uruchomienia cykliczne wykrywają nowo opublikowane
CVE nawet wtedy, gdy kod się nie zmienił.

Zasady obsługi znalezisk:

- Podatności o ważności krytycznej i wysokiej blokują release, chyba że
  zapisano decyzję `not_affected`.
- Podatności o ważności średniej wymagają wpisu z triage przed releasem.
- Podatności o ważności niskiej mogą poczekać na planowane prace utrzymaniowe,
  ale również powinny zostać zapisane.
- Wpisy dotyczące modułów opt-in powinny wskazywać odpowiednie flagi
  `HAL_ENABLE_*` oraz wspierane targety.

## Bramka release'u

Przed utworzeniem taga release'u sprawdź, czy `VERSION`, pierwszy datowany wpis
w changelogu i wersja projektu w SBOM są zgodne:

```bash
python3 scripts/check_release_metadata.py
```

Utwórz odpowiadający tag dopiero po umieszczeniu commita release'u w `main`.
CI uruchamiane tagiem dodatkowo sprawdza nazwę taga i dowodzi, że oznaczony
commit jest przodkiem `origin/main`; tag z rozbieżnej gałęzi release'u zostaje
odrzucony. CI hosta uruchamia też kompletny zestaw testów pod ASan/UBSan oraz
smoke fuzzing parserów HTTP, WebSocket i uploadu multipart. ThreadSanitizer
pozostaje opcjonalną bramką lokalną dostępną przez
`-DJH_ENABLE_THREAD_SANITIZER=ON`.

## Aktualizowanie komponentu

1. W przypadku zarządzanego komponentu zewnętrznego zaktualizuj jego pin
   `third_party/*_version.conf` i uruchom `./third_party/update_components.sh`.
   W przypadku kodu dołączonego do repozytorium zaktualizuj źródła w ich
   dotychczasowej lokalizacji.
2. Zachowaj pliki licencji upstream i informacje o autorstwie.
3. Zaktualizuj w `security/third_party.json` wersję, tag, commit, purl lub
   odwołanie upstream.
4. Dla ESP-IDF odśwież `security/esp_idf_tools.json` na podstawie przypiętego
   `tools.json` i zarządzanego środowiska Python, a następnie sprawdź licencję
   każdego narzędzia.
5. Uruchom `python3 scripts/sync_generated.py --write` i przejrzyj diff
   wygenerowanych artefaktów.
6. Uruchom testy właściwe dla zmienionego modułu i odpowiedniego targetu.
7. Jeśli aktualizacja naprawia lub ocenia CVE, dodaj wpis do
   `security/vulnerability_log.md` zawierający CVSS, dotknięte flagi i decyzję.
8. Wspomnij o zmianach związanych z bezpieczeństwem w `doc/CHANGELOG.md`.

## Zasady oceny podatności

Zacznij od spisu komponentów, a następnie oceń osiągalność podatnego kodu:

- `not_affected`: podatny kod nie występuje, nie jest kompilowany albo nie jest
  osiągalny we wspieranej integracji HAL.
- `affected`: kod występuje i jest osiągalny w co najmniej jednej wspieranej
  konfiguracji HAL.
- `fixed`: repozytorium zawiera poprawkę lub zaktualizowaną wersję komponentu.
- `mitigated`: udokumentowana konfiguracja lub ograniczenia runtime zmniejszają
  praktyczny wpływ, lecz nie usuwają podatnego kodu.
- `under_investigation`: potrzebna jest dalsza analiza.

Dla produktów embedded zapisz, które flagi `HAL_ENABLE_*` i targety powodują,
że problem jest osiągalny. Podatność w `HAL_ENABLE_MQTT` albo
`HAL_ENABLE_WIREGUARD` nie wpływa automatycznie na firmware, który nie kompiluje
tych modułów.
