<a id="bezpieczeństwo-łańcucha-dostaw"></a>

# Bezpieczeństwo zależności i narzędzi

*Dostępne również [po angielsku](../en/security_supply_chain.md).*

JaszczurHAL prowadzi wykaz zewnętrznych komponentów i narzędzi, generuje na jego podstawie SBOM oraz rejestruje ocenę wykrytych podatności. Ten rozdział wyjaśnia, jak odtworzyć te dane, sprawdzić ich aktualność i przygotować zmianę zależności do wydania.

## Zakres

Wykaz obejmuje:

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

Wykaz repozytorium nie zastępuje analizy konkretnego produktu. Aplikacja powinna mieć własny SBOM: aktywne flagi `HAL_ENABLE_*` decydują o tym, które opcjonalne moduły rzeczywiście znajdą się w firmware.

<a id="native-ota-security-boundary"></a>

<a id="granica-bezpieczeństwa-natywnego-ota"></a>

## Zabezpieczenia i ograniczenia natywnego OTA

Natywne OTA dla RP uwierzytelnia wersjonowany nagłówek obrazu za pomocą HMAC-SHA256. Przed aktywacją sprawdza również SHA-256 danych obrazu i CRC nagłówka. Symetryczny klucz HMAC jest wyprowadzany z tego samego hasła aplikacji, które służy do uwierzytelniania transportu. Każdy, kto zna hasło, może więc przygotować akceptowany obraz. W produkcie używaj unikalnego sekretu o wysokiej entropii i przekazuj go narzędziu obsługującemu projekty VS Code przez `ota.passwordEnv`, zamiast zapisywać hasło w pliku śledzonym przez Git.

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

Obie platformy używają AUTH2, gdy firmware ma skonfigurowane niepuste hasło. AUTH2 oblicza HMAC-SHA256 z kluczem będącym skrótem MD5 hasła zapisanym jako tekst ASCII z małymi literami szesnastkowymi. Uwierzytelniane dane obejmują polecenie, port zwrotnego połączenia TCP, rozmiar i MD5 obrazu oraz niezależne, 16-bajtowe wartości jednorazowe (nonce) urządzenia i klienta. Urządzenie wiąże uwierzytelnienie z adresem IP i portem źródłowym zaproszenia UDP, a połączenie zwrotne nawiązuje z tym samym adresem IPv4. Host korzysta z połączonego gniazda UDP i akceptuje połączenie TCP tylko od adresu wybranego partnera UDP.

Obie wartości nonce są generowane przez bezpieczny generator losowy platformy.

Parser ASCII odrzuca niejednoznaczne białe znaki, osadzone znaki NUL, alternatywne zapisy liczb, nieprawidłowe długości i nadmiarowe pola. Przy niepustym haśle host nie akceptuje bezpośredniego `OK`, starszego `AUTH` ani starszego uwierzytelniania `200`. Jeżeli alokacja muteksu się nie powiedzie, usługa pozostaje zatrzymana na obu platformach - nie uruchamia transportu bez wymaganej blokady.

AUTH2 zapewnia symetryczne uwierzytelnianie hasłem. Nie zastępuje współczesnego mechanizmu podpisywania obrazów i nie szyfruje transmisji. Brak hasła urządzenia lub puste hasło wyłącza AUTH2; `ota.allowEmptyPassword=true` pozwala hostowi kontynuować wyłącznie w tym jawnie nieuwierzytelnionym trybie deweloperskim. Na ESP32-S3 autentyczność firmware, poufność i ochrona przed instalacją starszej wersji wymagają odpowiedniej konfiguracji ESP-IDF Secure Boot V2, szyfrowania pamięci flash, eFuse, chronionych kluczy i odzyskiwania. Zwykłe wgrywanie i testy nie włączają nieodwracalnych ustawień eFuse.

Miejsce przechowywania sekretów, zakres reguł zapory sieciowej, pierwszą
instalację, wycofywanie aktualizacji i odzyskiwanie opisano w dokumencie
[Natywne aktualizacje OTA](OTAWorkflow.md#shared-auth2-transport-authentication).

## Pliki

| Plik | Przeznaczenie |
|------|---------------|
| `security/third_party.json` | Ręcznie utrzymywane, miarodajne źródło informacji o dołączonych komponentach i ich ściśle określonych wersjach. |
| `security/third_party.schema.json` | Schemat JSON używany podczas przeglądu struktury spisu. |
| `security/sbom.cdx.json` | Generowany SBOM CycloneDX dla repozytorium biblioteki. |
| `security/esp_idf_tools.json` | Zweryfikowany wykaz dokładnych wersji narzędzi dla platform ESP-IDF, ich licencji i projektów źródłowych, rewizji frameworka oraz skrótu `tools.json`. |
| `security/vulnerability_log.md` | Ręcznie utrzymywany rejestr oceny podatności i poprawek. |
| `SECURITY.md` | Zasady zgłaszania, wstępnej oceny, klasyfikacji ważności i utrzymania. |
| `scripts/generate_sbom.py` | Generator SBOM działający offline i używający wyłącznie biblioteki standardowej Pythona. |
| `scripts/sync_generated.py` | Wspólny skrypt odświeżający wszystkie generowane artefakty przechowywane w repozytorium, w tym SBOM, i weryfikujący je w trybie tylko do odczytu. |
| `scripts/check_release_metadata.py` | Kontrola zgodności VERSION, SBOM, nazwy tagu i przynależności commitu do historii głównej gałęzi. |
| `scripts/check_vulnerabilities.sh` | Opcjonalny skrypt uruchamiający dostępne lokalnie skanery podatności. |

## Generowanie SBOM

```bash
python3 scripts/sync_generated.py --write
```

Skrypt uruchamia generator, który odczytuje `security/third_party.json` oraz `security/esp_idf_tools.json` i zapisuje `security/sbom.cdx.json`. Generowanie jest deterministyczne; przy niezmienionych danych wejściowych wynik powinien pozostać taki sam.

<a id="pochodzenie-narzędzi-esp-idf"></a>

## Wersje i pochodzenie narzędzi ESP-IDF

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

Każda produkcyjna kompilacja ESP-IDF tworzy także `generated/jaszczurhal/jh_esp_idf_toolchain.json` i dołącza go do `jh_esp_idf_artifacts.json`. Zapis obejmuje faktycznie użyte wersje kompilatora, CMake, Ninja, interpretera Pythona dla IDF i esptool oraz SHA-256 pliku `tools.json`. Nie zawiera bezwzględnych ścieżek z hosta. Manifest artefaktów zapisuje osobno rewizję ESP-IDF, skrót końcowego `sdkconfig`, profil, offset i skrót tablicy partycji oraz skrót każdego wgrywanego obrazu. Wykaz narzędzi określa zatem zatwierdzony zestaw, a manifest kompilacji - zestaw, który rzeczywiście utworzył dany firmware.

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

Kontrola działa w trybie tylko do odczytu i obejmuje wszystkie śledzone pliki generowane. Tworzy między innymi tymczasowy SBOM i porównuje go z `security/sbom.cdx.json`. Polecenie `./scripts/check_sbom.sh` korzysta z tej samej kontroli, ale ogranicza jej zakres do SBOM.

## Polityka CI

GitHub Actions uruchamia wymagane zadanie `test` oraz zależne zadanie
`security-scan` dla pull requestów, zmian wysyłanych do `main`, według
harmonogramu tygodniowego i na żądanie. Zadanie testowe weryfikuje wszystkie
generowane artefakty, w tym SBOM. Zadanie bezpieczeństwa:

- instaluje `osv-scanner` i `cve-bin-tool`,
- uruchamia `osv-scanner` dla drzewa źródeł repozytorium,
- uruchamia `cve-bin-tool` dla SBOM CycloneDX.

Skanowanie podatności jest oddzielone od kompilacji, testów i analizy statycznej. Pozwala to niezależnie diagnozować błędy skanera i błędy kodu. Uruchomienia cykliczne wykrywają również nowe CVE opublikowane od poprzedniego skanowania, nawet jeśli kod repozytorium się nie zmienił.

Zasady postępowania z wynikami:

- Podatności o ważności krytycznej i wysokiej blokują wydanie, chyba że
  zapisano decyzję `not_affected`.
- Podatności o ważności średniej wymagają wpisu z oceną przed wydaniem.
- Podatności o ważności niskiej mogą poczekać na planowane prace utrzymaniowe,
  ale również powinny zostać zapisane.
- Wpisy dotyczące modułów włączanych opcjonalnie powinny wskazywać
  odpowiednie flagi `HAL_ENABLE_*` oraz obsługiwane targety.

<a id="bramka-wydania"></a>

## Kontrole przed wydaniem

Przed utworzeniem tagu wydania sprawdź, czy `VERSION` i wersja projektu w
SBOM są zgodne:

```bash
python3 scripts/check_release_metadata.py
```

Utwórz tag dopiero po włączeniu commitu wydania do `main`. CI uruchamiane przez tag sprawdza jego nazwę oraz to, czy wskazany commit jest przodkiem `origin/main`; odrzuca tag z rozbieżnej gałęzi wydania. CI na hoście uruchamia również pełny zestaw testów z ASan/UBSan oraz krótkie testy fuzzingowe parserów HTTP, WebSocket i przesyłania multipart. ThreadSanitizer jest opcjonalną kontrolą lokalną włączaną przez `-DJH_ENABLE_THREAD_SANITIZER=ON`.

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

<a id="zasady-oceny-podatności"></a>

## Ocena wpływu podatności

Zacznij od wykazu komponentów, a następnie sprawdź, czy podatny kod jest obecny i osiągalny w obsługiwanej konfiguracji:

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
