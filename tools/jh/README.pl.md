# CLI hosta jh

*Dostępna jest także [wersja angielska](README.md).*

`jh json inspect` odczytuje JSON, wybiera wartość przez JSON Pointer i może
zapisać albo sprawdzić plik wynikowy. Korzysta z parsera i API plików jhbase.
`jh json validate` sprawdza dokument według podzbioru schematów sprzętowych,
także z odwołaniami między lokalnymi plikami. `jh generate device-defaults`
czyta katalog typów komponentów HAL i zapisuje albo sprawdza generowany
nagłówek ich wartości domyślnych, a `jh generate features` robi to samo
z nagłówkiem i danymi CMake rejestru funkcji HAL. `scripts/sync_generated.py`,
a przez niego hook pre-commit i bramka, uruchamiają oba polecenia przez
opisany niżej launcher. `jh config inspect` odczytuje `hal_project_config.h` projektu tak,
jak widzi go kompilacja. `jh feature list` i `jh feature resolve` podają
rejestr funkcji oraz rozwiązane funkcje, o które prosi jedna kompilacja;
`jh feature lint` sprawdza żądania funkcji w nagłówkach i manifestach
projektów. `jh board` sprawdza i wypisuje rejestr płytek, a `jh clock` rejestr
drzew zegarów.

Przed konfiguracją przygotuj zależności hosta w wersjach wskazanych przez repozytorium:

```sh
python3 scripts/component_manager.py component jhbase
python3 scripts/component_manager.py component utf8proc
python3 scripts/component_manager.py component pcre2
```

Build wymaga też utf8proc, którego pin dostarcza jhbase, w osobnym checkoutcie
`third_party/utf8proc`: katalog typów sprawdza jednostki jako drukowalny tekst
NFC przez `jb::unicode`.

PCRE2 znajduje się w ignorowanym checkoutcie wskazanym przez
`third_party/pcre2_version.conf`. Konfiguracja i build sprawdzają rewizję oraz
czystość źródeł bez pobierania i zmieniania zależności. Wspólny helper jhbase
sprawdza origin i rzeczywiste bajty źródeł, także zmiany ukryte flagami indeksu
Gita. Nieużywany submoduł `deps/sljit` musi pozostać pustym katalogiem.
Program linkuje statycznie PCRE2-8 bez JIT.

Buduj z katalogu checkoutu JaszczurHAL, na CMake co najmniej 3.22:

```sh
cmake -S tools/jh -B .build/jh -G Ninja -DCMAKE_BUILD_TYPE=Release \
  -DCMAKE_C_COMPILER=gcc -DCMAKE_CXX_COMPILER=g++
cmake --build .build/jh
ctest --test-dir .build/jh --output-on-failure
```

Na Windows użyj developer PowerShell Visual Studio. Dla Visual Studio 2026
generator to `"Visual Studio 18 2026"` i wymaga CMake co najmniej 4.2:

```powershell
cmake -S tools/jh -B .build/jh -G "Visual Studio 17 2022" -A x64
cmake --build .build/jh --config Release
ctest --test-dir .build/jh -C Release --output-on-failure
```

`scripts/jh` (Linux) i `scripts/jh.ps1` (Windows, `-Arch x64|x86`) budują
narzędzie przy pierwszym użyciu w `.build/host-tools/<wariant>/<klucz>/bin`
i uruchamiają je, odbudowując tylko po zmianie wejścia; bootstrap opisuje
[opis skryptów](../../doc/api/pl/00_scripts.md). Ręczny build poniżej to ten
sam projekt, który budują launchery.

To osobny projekt hostowy. Buduje to samo jhbase co reszta HAL: przypięty
checkout `third_party/jhbase` (`scripts/ensure_jhbase.sh`) albo checkout
wskazany zmienną środowiskową `JH_JHBASE_DIR`. Włącza dla niego `JHBASE_HOST`
i nie korzysta z buildu firmware. Etap `host` w `runalltests.sh` buduje
narzędzie i uruchamia te sprawdzenia.

Na Linuksie executable znajduje się w `.build/jh/jh`, a na Windows
w `.build/jh/Release/jh.exe`. W przykładach `jh` oznacza ten
konkretny program:

```sh
jh --help
jh --version --json
jh json inspect --file tests/fixtures/device_tree/cases.json --pointer /cases/0/expected --json
jh json inspect --file input.json --pointer /limits/max --output result.json
jh json inspect --file input.json --pointer /limits/max --output result.json --check --json
jh json validate --file input.json --schema config/hardware/device_tree.schema.json --json
jh generate device-defaults --root . --check
jh generate device-defaults --root . --write --json
jh config inspect --header examples/13_adc/hal_project_config.h --define HAL_TARGET_RP2040 --json
```

Ścieżki wejściowe są rozwiązywane względem cwd odczytanego raz na wejściu.
Argumenty Windows pochodzą z `wmain` i są zamieniane z UTF-16 na UTF-8.
Opcje przyjmują `--name value` albo `--name=value`; flagi nie mają wartości.
Nieznane opcje, opcje powtórzone (poza `--define`) i dodatkowe słowa polecenia
dają błąd.
`-h` i `-V` oznaczają pomoc oraz wersję. `--` kończy analizę opcji.
Wartość zaczynająca się od `--` wymaga zapisu `--name=value`.
Brak `--pointer` wybiera root; nazwy nie są normalizowane do NFC.

Bez `--json` inspect wypisuje wybrany raw JSON i LF. `--output` zapisuje te same
bajty przez atomowe API plików i zachowuje mtime, jeśli plik już pasuje.
Nie wypisuje wtedy wartości na stdout. `--check` wymaga `--output`, porównuje
bajty bez zmian w plikach i zgłasza różnicę na stderr. Domyślny limit wejścia
wynosi 16 MiB; `--max-bytes` przyjmuje od 0 do 64 MiB. Parser ogranicza też
głębokość do 256, liczbę wartości do 1000000, dzieci kontenera do 100000,
token liczby do 1 MiB, a pracę porównywania kluczy do
`32 * max_bytes + 1 MiB` jednostek.

Z `--json` stdout zawiera jeden obiekt JSON w UTF-8 i LF. Są w nim
`formatVersion: 1`, `command` oraz symboliczny `status`. Udany inspect dodaje
`source`, `pointer` i `value`; zapis pliku także `output`, `changed` i
`checkOnly`. Tokeny liczb zachowują dokładny zapis. Błąd zawiera opis, wskazany Pointer oraz,
gdy są dostępne, plik, offset bajtowy, linię i kolumnę bajtową liczone od 1
oraz kod parsera `jsonError`. Pomoc i wersja także mają wyniki JSON.
Diagnostyka dla człowieka trafia na stderr.

Walidacja zwraca wybrane błędy w kolejności Pointerów: tokeny dziesiętne
porównuje liczbowo, a pozostałe według punktów kodowych Unicode. Wynik JSON ma
`command: "json.validate"`, `source`, `schema` oraz tablicę `errors`. Każdy błąd
zawiera `instancePointer`, `schemaUri`, `schemaPointer`, `reason` i `message`.
`schemaUri` koduje nazwy plików procentowo. Lokalizacja reguły wskazuje
właściwe słowo kluczowe po rozwiązaniu `$ref`.
Komunikaty dla człowieka trafiają na stderr; udana walidacja bez `--json`
niczego nie wypisuje.

Obsługiwane są słowa kluczowe używane przez `config/hardware/*.schema.json`.
To podzbiór JSON Schema. Nieznane słowa i nieobsługiwane konstrukcje regex dają
jawny błąd. Odwołania wskazują lokalne ścieżki z ukośnikami i fragmenty JSON
Pointer z dekodowaniem procentowym. Odwołania sieciowe nie są obsługiwane.
Regex przeszukuje UTF-8 z newline LF i klasą `\s` zgodną z Pythonem. Adapter
obsługuje wzorce produkcyjne, także lookahead i `\uXXXX`. PCRE2 ma dane Unicode
17.0; rozwinięcie whitespace jest jawne. Klasy takie jak `\w` i `\d`,
backreferences oraz inline flags nie są obsługiwane.
Klasy POSIX i niepoprzedzone znakiem escape `]` tuż po `[` lub `[^` także dają
błąd. Literalny nawias zamykający w klasie zapisz jako `\]`.

Liczby są porównywane dokładnie w `const`, `enum`, `uniqueItems`, granicach
liczbowych i `integer`. Na przykład `0.1` różni się od `0.10000000000000001`,
`1` równa się `1.0`, a `true` różni się od `1`. To może zmienić wynik względem
zaokrągleń binary64 w walidatorze Python. Walidacja nie dodaje defaults ani
nie normalizuje tekstu. Konwersja do docelowego typu zmiennoprzecinkowego
pozostaje osobnym krokiem.

Limity walidacji obejmują 50 milionów jednostek pracy, 64 zagnieżdżone oceny
schematu, 100000 wartości schematów we wszystkich unikalnych dokumentach,
64 MiB wejścia schematów, 256 załadowanych plików,
1024 zachowane błędy, 1 MiB tekstu diagnostyki, Pointery do 8192 bajtów i
identyfikatory dokumentów do 32768 bajtów. Zdekodowana wartość ma najwyżej
16 KiB, a wzorzec 4096 bajtów. Regex ma arenę alokacji 2 MiB, limit match heap
1 MiB, milion kroków dopasowania i głębokość 128. Wyczerpanie limitu daje
`HAL_EOVERFLOW` przed opublikowaniem części błędów. Wewnętrzne API C w
[jh_json_schema.h](json/jh_json_schema.h) przyjmuje scratch użytkownika,
własne limity i opcjonalne callbacki dokumentów oraz raportów.

`jh_json_read_settings` z [jh_json_file.h](json/jh_json_file.h), wołane
z modułów C++ jako `jh::read_settings`, czyta pliki VS Code i manifesty
projektów tak samo jak `load_json_file` w narzędziach Pythona. Przyjmuje komentarze poza stringami, ostatnią wartość powtórzonej
nazwy oraz `NaN`/`Infinity`. Brakujący plik daje pusty obiekt, a korzeń musi
być obiektem. Liczby zachowują oryginalne tokeny. W odróżnieniu od Pythona
odrzuca escape `\u` niesparowanego surogatu, stosuje limity parsera (256
zagnieżdżonych kontenerów, 100000 członków w każdym) i przyjmuje liczbę
całkowitą dłuższą niż 4300 cyfr, na które pozwala `int()` w Pythonie 3.11+.
Żadne polecenie `jh` jeszcze z niego nie korzysta. Test `jh_settings` sprawdza
sam czytnik, a `jh_settings_compat` porównuje go z `load_json_file` na plikach
`.vscode` checkoutu poza `tests/hardware` i na przypadkach brzegowych.


`jh repository files --root <checkout> --git <bezwzględny-executable>` uruchamia
Git bez powłoki przez `jb::host`. Przekazuje `-C <root> ls-files --cached
--others --exclude-standard -z`, dziedziczy środowisko i cwd wywołującego.
Surowy stdout zachowuje wszystkie bajty, w tym separatory NUL oraz nazwy
plików POSIX spoza UTF-8. Stderr dziecka jest przekazywany bez zmian. `--json`
zwraca tablicę UTF-8 `paths`, `root` i pola procesu `started`, `exited`,
`exitCode`, `termSignal`, `nativeError`; nazwa spoza UTF-8 w tym trybie daje
jawny błąd. `--max-bytes` ogranicza przechwycone bajty nazw w trybie JSON
z domyślną wartością i maksimum jak przy inspekcji. `--timeout-ms` przyjmuje
0-4294967295; zero oznacza oczekiwanie bez końca. SIGINT (Ctrl+C lub Ctrl+Break
na Windows) żąda anulowania i czeka na cleanup procesu. Timeout daje kod 124,
anulowanie 130; pozostałe kody wyjścia Gita są zachowane. Błąd uruchomienia
lub przekazywania wyjścia daje kod 3 (4 przy alokacji lub limicie bajtów),
a status JSON wskazuje przyczynę.
Te kody procesów dotyczą wyłącznie `repository files`.

Klient może podać `--json --json-version 1`. Nieobsługiwana wersja główna daje
`HAL_EUNSUPPORTED` i kod 2 przed wykonaniem komendy, w aktualnie obsługiwanej
kopercie wyniku. Brak opcji wybiera wersję 1. W obrębie wersji głównej klient
musi ignorować nieznane pola; znaczenia i typy istniejących pól pozostają
stałe. Zmiana zrywająca zgodność wymaga nowej wersji głównej.

## Konfiguracja projektu

`jh config inspect` odczytuje `hal_project_config.h` projektu tak samo jak
`scripts/project_config.py`. `--header <ścieżka>` wskazuje nagłówek; brakujący
plik daje pustą konfigurację. Każde `--define NAZWA[=WARTOŚĆ]` definiuje makro
przed nagłówkiem, po kolei, tak jak `-D`. `--preinclude <ścieżka>` najpierw
czyta inny nagłówek, tak jak kompilacja z device tree czyta wygenerowany
`jh_hardware.h`. Czytnik obsługuje tę samą część preprocesora: makra obiektowe
i funkcyjne w `#define`, `#undef`, dyrektywy z rodziny `#if` z `defined`
i wyrażeniami całkowitymi, `#include` w cudzysłowie względem pliku, który go
zawiera, `#pragma once` i `#error`; `#warning` i `#line` pomija.
`scripts/project_config.py` uruchamia to polecenie dla CMake i narzędzi
Pythona. Test `jh_config` sprawdza czytnik w jednym procesie, a
`jh_config_preprocessor` porównuje go z preprocesorem kompilatora GCC albo
Clang, także na każdym `hal_project_config.h` checkoutu poza `tests/hardware`.

Bez `--json` polecenie wypisuje zdefiniowane makra jako wiersze `#define`
w kolejności definicji, a `#error` zgłasza na stderr. Wynik `--json`
(`command: "config.inspect"`) zawiera `header`, `preinclude`, przeczytane pliki
(`files`), targety z `JH_PROJECT_TARGETS` (`targets`), warianty
z `JH_PROJECT_VARIANTS` (`variants`) z definicjami w postaci `-D`, makra
w kolejności definicji (`macros`), funkcje zdefiniowane w samym nagłówku
(`features`), zależności, czyli nieznane identyfikatory rozstrzygające o danej
nazwie (`dependencies`), dyrektywy `#define` i `#undef` przeczytanych plików
z każdej gałęzi (`directives`: `file`, `line`, `directive`, `name` i `text` po
nazwie) oraz `error` z treścią i miejscem `#error` albo `null`. Każde makro ma
`name`, `body`, `source`, `functionLike`, `parameters`, `unresolved` (nazwy
użyte w wartości, których konfiguracja nie definiuje) i `integerState`: `value`
razem z `integer`, `none`, gdy kompilacja nie ma dla niego liczby,
`outOfRange` albo `error` z `integerError` (`status`, `message`), gdy obliczenie
wartości się nie udaje, np. przy błędnym literale albo wartości ponad limit
opisany niżej; błąd dotyczy tylko tego makra. Nagłówek zatrzymany na `#error`
nadal daje wynik i kod 0.

`jh config directives --file <ścieżka>` wypisuje w tej samej postaci dyrektywy
`#define` i `#undef` jednego pliku bez obliczania go, więc także dla nagłówka,
którego czytnik nie przyjmuje; brakujący plik daje pustą listę.

Arytmetyka liczb całkowitych jest 64-bitowa ze znakiem. Literał albo wynik
spoza tego zakresu w operandzie, który warunek oblicza, daje `[JH-CFG-SCOPE]
... exceeds the signed 64-bit range of jh` (`HAL_EOVERFLOW`, kod 4), podczas
gdy czytnik Pythona liczy bez ograniczeń. Operandów, których C nie oblicza,
kontrola nie dotyczy. Wartość makra spoza zakresu ma `outOfRange` zamiast
liczby. Liczby i nazwy są w ASCII, tak jak w C, a inaczej niż w wyrażeniach
regularnych czytnika Pythona: cyfry dziesiętnej spoza ASCII nie da się
obliczyć, a nazwa dyrektywy, po której stoi znak spoza ASCII inny niż biały,
jest odrzucana. Warunek może mieć najwyżej 256 poziomów zagnieżdżenia, makra
rozwijają się przez najwyżej 1000 poziomów, a plik może mieć najwyżej 16 MiB.
Rozwijanie warunków i śledzenie nazw, od których zależy wartość, odwiedza
łącznie najwyżej 1000000 tokenów na jeden odczyt, a każda wartość zwracana
przez polecenie ma własny budżet tej samej wielkości, więc żadna nie zabiera
go innej. Przy pytaniu o zależności każde makro jest przeglądane raz, więc
praca rośnie z rozmiarem nagłówka, a nie z liczbą ścieżek między makrami.
Przekroczenie limitu daje `HAL_EOVERFLOW` i kod 4; Python kończy się w takich
miejscach na limicie rekurencji albo limitu nie ma.

Odrzucony nagłówek kończy się kodem 2 i `HAL_ECONFIG`. Komunikat to
diagnostyka `[JH-CFG-*]` czytnika Pythona; `code` zawiera jej prefiks, a
`source` plik i wiersz. Niepoprawny UTF-8 i literał ósemkowy z cyfrą 8 lub 9
dają `HAL_EPROTO`. Plik, którego nie da się przeczytać, kończy polecenie
kodem 3.

## Rejestr funkcji

`jh generate features --root <checkout> (--write | --check)` czyta rejestr
funkcji HAL: `config/features.schema.json` i wszystkie fragmenty
`config/features/*.json` w kolejności nazw, także ukryte. `--config-root`
wskazuje inny katalog konfiguracji. Każdy plik jest czytany tak jak przez
`json.loads`: przyjmuje ostatnią wartość powtórzonej nazwy oraz `NaN`
i `Infinity`, ale nie komentarze. Polecenie stosuje reguły
`scripts/generate_hal_features.py`: dokładne zestawy pól, wzorce symboli,
domen i ścieżek źródeł, zależności zarządzane wymienione w schemacie, relacje
do znanych symboli, brak cykli `implies`, symetryczne konflikty i brak
konfliktu w domknięciu symbolu. Komunikaty zachowują brzmienie tego skryptu.

`--write` publikuje `src/hal/generated/jh_hal_features.h`
i `cmake/generated/jh_hal_features.cmake` z trybem 0644 i nie zmienia plików
o identycznej treści; `--check` porównuje je i wypisuje unified diff
najwyżej 80 linii na plik. Ścieżki pochodzą z `config/tooling/artifacts.json`
i są liczone względem `--output-root` (domyślnie `<root>`); ścieżka
absolutna, z dyskiem albo z segmentem `..` kończy się kodem 2, zanim cokolwiek
zostanie zapisane. Digesty rejestru
i schematu to SHA-256 zwartego JSON-u z posortowanymi kluczami i ucieczkami
ASCII, jaki zapisuje `json.dumps(sort_keys=True, ensure_ascii=True)`.
Z `--json` wynik zawiera `command: "generate.features"`, `status` (`HAL_OK`
albo `HAL_ESTATE` dla brakującego lub nieaktualnego artefaktu), `root`,
`config`, `output`, `changed` (liczbę plików zapisanych lub z poprawionym
trybem 0644; przy `--check` liczbę plików różniących się od wyniku)
i `checkOnly`.

Polecenie kończy się kodem 0 przy sukcesie, 1, gdy `--check` znajdzie
rozbieżność, 2 przy niepoprawnym rejestrze lub pliku rejestru, którego nie da
się odczytać albo zapisać, i 4 przy braku pamięci. W odróżnieniu od generatora
Pythona odrzuca escape `\u` niesparowanego surogatu, zgłasza `kind` będący
tablicą lub obiektem jako zwykły błąd, gdzie Python kończył się tracebackiem,
i zachowuje w digeście schematu zapis liczby z częścią ułamkową lub
wykładnikiem, gdzie Python zapisałby liczbę zmiennoprzecinkową.

`jh feature list` i `jh feature resolve` czytają ten sam rejestr,
z `--config-root` albo `<root>/config`, i zgłaszają te same błędy.
`feature list` wypisuje jedną linię na symbol z jego rodzajem i domeną. Wynik
`--json` (`command: "feature.list"`) zawiera `config`, `registryDigest`,
`registrySchemaDigest` i `features` w kolejności nazw, każdy z polami `name`,
`domain`, `kind`, `implies`, `requires`, `conflicts`, `featureSources`,
`portableSources`, `dependencies`, `note` (`null`, gdy go nie ma) i `closure`,
czyli wszystkim, co symbol włącza bezpośrednio lub przez inne symbole.

`feature resolve` rozwiązuje funkcje, o które prosi jedna kompilacja, tak jak
robią to narzędzia HAL dla konfiguracji projektu. Każde
`--request <SYMBOL[=VALUE]>` to jedno żądanie; `--source` na tej samej
pozycji mówi, skąd pochodzi, a bez `--source` żądania mają źródła
`command-line:--request[<n>]`. `--file <path>` podaje zamiast nich żądania
w pliku JSON czytanym jak `json.loads`:
`{"requests": [{"request": "<SYMBOL[=VALUE]>", "source": "<text>"}]}`, gdzie
każdy obiekt ma dokładnie te dwa napisy. Korzystają z niego narzędzia HAL, bo
źródła powtarzane w wierszu poleceń przekraczają 32767 znaków dopuszczalnych
w Windows. `--target <id>` dodaje każde
`--required-feature` tego targetu ze źródłem
`target:<id>:requiredFeatures[<n>]`, chyba że kompilacja już o nie prosi.
`--context` nazywa konfigurację w zgłoszeniach (domyślnie `command line`).
Polecenie wypisuje rozwiązane symbole, po jednym w linii, a zgłoszenia na
stderr:

- `[JH-CFG-UNKNOWN]`, `[JH-CFG-VALUE]` (wartość inna niż brak lub `1`)
  i `[JH-CFG-DERIVED]` dla jednego żądania, po jego źródle;
- `[JH-CFG-REDUNDANT]` dla symbolu żądanego więcej niż raz,
  `[JH-CFG-REQUIRES]` i `[JH-CFG-CONFLICT]` dla rozwiązanego zbioru oraz
  `[JH-CFG-TARGET-REQUIRED]`, gdy kompilacja prosi o bliźniaka `HAL_DISABLE_`
  wymaganej funkcji, po kontekście.

Z `--json` wynik (`command: "feature.resolve"`) zawiera `config`,
`registryDigest`, `context`, `target` (`null`, gdy go nie ma),
`requestedFeatures` (poprawne jawne żądania), `resolvedFeatures` (je, funkcje
targetu i to, co włączają), `resolvedFeaturesDigest` (SHA-256 rozwiązanych
nazw połączonych znakami nowej linii), `provenance` (źródła każdego symbolu)
i `findings`. Kod wyjścia 1 oznacza zgłoszenia, ze `status: "HAL_ECONFIG"`;
2 niepoprawny rejestr, argumenty albo plik żądań; 3 plik żądań, którego nie da
się odczytać.

`scripts/generate_hal_features.py` daje swoim importerom ten model i te
rozwiązania: każdy rejestr czyta raz na proces, dopóki jego pliki się nie
zmienią, a każdą listę żądań rozwiązuje raz. W odróżnieniu od tego skryptu
polecenia podają funkcje w kolejności nazw, gdzie Python zachowywał kolejność
fragmentów i dokumentów, i cytują źródło żądania bez zmian, gdzie Python
przepuszczał jego część przed pierwszym dwukropkiem przez `pathlib.Path`
(`./h.h` stawało się `h.h`).

`jh feature lint` sprawdza żądania funkcji w projektach według tego samego
rejestru. Czyta każdy `hal_project_config.h`
i `.vscode/jaszczurhal.project.json` pod każdym `--input-root` (korzeń będący
plikiem jest jedynym wejściem; domyślnie `<root>`). Pomija katalogi o nazwach
`.build`, `.git` i `third_party` oraz zaczynające się od `build`, dowiązania
do katalogów i `tests/hardware`, chyba że podano `--include-hardware-fixtures`.
W nagłówku sprawdza każde `#define` nazwy `HAL_ENABLE_*` lub `HAL_DISABLE_*`,
w dowolnej gałęzi, i każdą definicję `JH_PROJECT_VARIANTS(X)`, zgłaszając to
samo co `feature resolve` dla pojedynczego żądania. Nagłówek odrzucony przez
czytnik konfiguracji projektu daje jedno zgłoszenie z komunikatem czytnika.
Manifest jest czytany tak jak przez `json.loads` i dostaje zgłoszenia
`config manifest`. Polecenie wypisuje posortowane zgłoszenia jako `error:`
(albo `warning:` z `--report-only`) oraz `linted <n> feature inputs: ...`.
Z `--json` wynik (`command: "feature.lint"`) zawiera `config`, `reportOnly`,
`inputs` i `findings`. Kod wyjścia 1 oznacza zgłoszenia bez `--report-only`;
2 niepoprawny rejestr, brakujący korzeń wejścia albo manifest, który nie jest
obiektem JSON.

`jh config manifest --file <ścieżka> [--label <tekst>]` zgłasza konfigurację
projektu, którą manifest zawiera, choć należy ona do `hal_project_config.h`:
pola `variants` i `example`, a w `cmake.cache` albo
`targetProfiles.<target>.cmake.cache` klucz `JH_VARIANT`, klucze
`EXTRA_HAL_DEFINES`, `JH_EXTRA_DEFINES` i `JH_PROJECT_SOURCES` oraz symbole
funkcji HAL. Każde zgłoszenie ma `[JH-CFG-MANIFEST]` po etykiecie
(domyślnie ścieżce). Manifest jest czytany tak jak przez jh-vscode:
komentarze są dozwolone, a brak pliku nie daje zgłoszeń. `jh-vscode`
i dispatcher przykładów sprawdzają manifesty przez to polecenie. Z `--json`
wynik (`command: "config.manifest"`) zawiera `file`, `label`, `exists`
i `findings`; kod wyjścia 1 oznacza zgłoszenia.

W odróżnieniu od `generate_hal_features.py --lint` lint zgłasza nagłówek
z niepoprawnym UTF-8 albo błędnym literałem ósemkowym jako zwykłe zgłoszenie,
gdzie Python kończył się tracebackiem. Plik, który nie jest poprawnym JSON-em,
zarówno w lincie, jak i w rejestrze, podaje przyczynę z jhbase z linią
i kolumną, gdzie Python podawał przyczynę ze swojego modułu `json`.

## Rejestr płytek

`jh board validate --root <checkout>` sprawdza rejestr płytek w katalogu
`--boards-root` (domyślnie `<root>/boards`): najpierw `capabilities.json`,
potem wszystkie `targets/*.json` i `profiles/*.json` w kolejności nazw,
każdy plik tak, jak czyta go `json.loads`. Deskryptory odwołują się do
rejestrów w `--root`, więc polecenie czyta również rejestr komponentów
`config/tooling/board_components.json`, rejestr funkcji i schematy zegarów.
Stosuje reguły, które stosował `scripts/generate_board_config.py`: dokładne
pola, identyfikatory i makra, bity cech, zbiory pinów, urządzenia z ich
rolami i magistralami, kanały CAN, komponenty, identyfikatory USB,
rezerwacje pinów, wejścia i źródła zegarów, unikalne identyfikatory płytek
i profili oraz selektory, a dla każdego targetu zgodną płytkę domyślną
i zastępczą. Komunikaty zachowują brzmienie z tego skryptu:
`<plik>: <ścieżka JSON>: got <wartość>; expected <reguła>`. Po udanym
sprawdzeniu polecenie wypisuje `validated <n> targets and <m> boards`;
z `--json` wynik (`command: "board.validate"`) zawiera `boardsRoot` oraz
liczby `targets` i `boards`.

`jh board list` sprawdza ten sam rejestr i wypisuje wiersze `target <id>`
i `board <id>`. Z `--json` wynik (`command: "board.list"`) zawiera
`boardsRoot`, `targets` i `boards`, w każdej pozycji `id`, `path`
i przeczytany `descriptor` (ID USB zapisane jako napis `0x` zamienia się
w tym samym miejscu na liczbę), dalej `capabilities`, `components` z `id`,
`providers` i `slot` oraz tabele używane przez generatory: `deviceRoles`,
`canControllers`, `canTransceivers`, `canChannelSignals`, `busDeviceKind`
i `singleEndpointDeviceKinds`. `jh board default --target <id>` wypisuje
płytkę domyślną znanego targetu; jego wynik `--json`
(`command: "board.default"`) zawiera `target` i `board`.

Polecenia kończą się kodem 0 po sukcesie, 2 przy niepoprawnym rejestrze,
pliku rejestru, którego nie da się odczytać, albo nieznanym targecie, a 4
przy braku pamięci. `scripts/generate_board_config.py` udostępnia ten model
swoim importerom: rejestr z `load_registry()` oraz tabele
`COMPONENT_REGISTRY` i `CAN_CONTROLLERS`, każdy rejestr czytany raz na proces,
dopóki jego pliki się nie zmienią.

W odróżnieniu od tego skryptu polecenia przyjmują w polu liczby całkowitej
tylko liczbę zapisaną bez części ułamkowej i wykładnika, mieszczącą się
w 64-bitowym zakresie ze znakiem. Odrzucają `true` i `false`, np. w `bit`
cechy, które Python brał jako 1 i 0; walidator schematów też je odrzuca.
Odrzucają również `1.0`, które Python przyjmował tam, gdzie porównywał
wartość, np. w `schemaVersion` albo identyfikatorze pinu, a walidator
schematów uznaje za liczbę całkowitą. Python przyjmował też większe liczby,
np. w rozmiarach pamięci, `cores` czy `profileId`. Zakresy jednego zbioru
pinów obejmują najwyżej 65536 pinów (`expected a range within 65536 pins`),
a Python rozwijał każdy zakres w zbiór i przy dużym `last` wyczerpywał
pamięć. USB `vid` i `pid` mogą być też napisem z `0x` i cyframi
szesnastkowymi, którego skrypt nie przyjmował
([poradnik profili płytek](../../doc/pl/boards_profiles_howto.md)).

Polecenia wypisują zbiór w komunikacie posortowany, a Python podawał go
w kolejności swojego zbioru, zmiennej między uruchomieniami. Wartość złego typu albo brak wymaganego pola dają zwykły
komunikat tam, gdzie Python kończył się wyjątkiem `TypeError`, `KeyError` lub
`AttributeError`. Plik, którego nie da się odczytać, daje
`invalid JSON: cannot read input file` zamiast tekstu błędu systemu
operacyjnego.

## Rejestr zegarów

`jh clock check --root <checkout>` sprawdza drzewa zegarów
z `config/hardware/clocks/<target>.json` (`--clocks` wskazuje inny katalog)
względem rejestru płytek z `--boards-root` i kodu, który ustawia zegary.
Każdy plik rejestru czyta loaderem plików sprzętowych, sprawdza schematem
`config/hardware/clock_tree.schema.json` i wymaga nazwy zgodnej z polem
`target`. Następnie potwierdza, że każdy target ma swój rejestr, że
częstotliwości wyjściowe każdego drzewa wynikają z parametrów backendu, że
drzewa inne niż `default` ma tylko STM32G474 (HAL nie ustawia zegara Pico SDK,
ESP-IDF ani mocka), że każde wymagane źródło jest wejściem zegara swojego
targetu i że każda płytka ma źródła, których potrzebuje domyślne drzewo każdego
z jej targetów. Na końcu porównuje drzewa z nagłówkami startowymi STM32G474
i domyślnymi ustawieniami przypiętego Pico SDK. Zgłasza błąd, gdy plik `.cmake`,
`.txt`, `.h`, `.c` albo `.cpp` w katalogach `cmake`, `link_libraries`, `src`,
`config` lub `examples` definiuje makro zegara Pico SDK. Opcja
`--esp-sdkconfig TARGET=PATH`, którą można powtórzyć, porównuje dodatkowo
`sdkconfig` kompilacji ESP-IDF z domyślnym drzewem `TARGET`; etap bramki
`esp-idf` przekazuje konfiguracje swoich kompilacji fixture'ów. Po udanym
sprawdzeniu polecenie wypisuje `[OK] clock registry: <n> targets, <m> trees`;
z `--json` wynik (`command: "clock.check"`) zawiera `clocks`, `targets`, `trees`
i `espSdkconfigs`.

`jh clock list` wypisuje wiersz `<target> <drzewo>` dla każdego drzewa.
Z `--json` wynik (`command: "clock.list"`) zawiera `clocks`, `targets` z `id`,
`path` i przeczytanym plikiem `registry`, `stm32g474TreeComponents` (komponent
płytki, przy którym kod startowy buduje dane drzewo STM32G474, `null`, gdy
nie trzeba żadnego) oraz `boardSources`: dla każdej płytki, zgodnego targetu
i drzewa tego targetu listę `errors` z `pointer` do `clockSources` płytki
i `message`. Lista czyta drzewa oraz profile i targety płytek tak, jak czyta
je `json.loads`, bez sprawdzania: robią to `jh clock check`
i `jh board validate`.
`scripts/clock_registry.py` udostępnia tę listę modułom `hardware_resolver.py`
i `generate_hardware_config.py`, czytając ją raz na proces, dopóki jej pliki
się nie zmienią.

Nieudana kontrola kończy się kodem 1 i komunikatem, który podawał
`scripts/clock_registry.py` przed portem, wypisanym jako `jh: <komunikat>
(<status>)` zamiast `[ERROR] clock registry: <komunikat>`. Plik rejestru z niepoprawnym JSON
daje kod 2, plik, którego nie da się odczytać, kod 3, niepoprawny argument
kod 2, a brak pamięci kod 4.

Polecenia różnią się od tego skryptu w następujących przypadkach:

- Wzory liczą na liczbach i wartościach logicznych tak jak Python, także na
  liczbach zmiennoprzecinkowych z dzieleniem całkowitym. Wynik całkowity poza
  64-bitowym zakresem ze znakiem albo dzielnik zero daje
  `the parameters divide by zero or overflow 64-bit arithmetic` lub
  `the parameters overflow 64-bit arithmetic`, a parametr tekstowy
  `parameter <nazwa> is <wartość>, the frequencies need a number`. Python
  liczył na dowolnie dużych liczbach całkowitych, kończył się wyjątkiem
  `ZeroDivisionError` lub `TypeError`, a tekstowe `cpuFreqMhz` powtarzał
  milion razy.
- Brak `pllQ` w drzewie, które bierze zegar FDCAN z PLL Q, daje
  `missing parameters ['pllQ']`, a target wskazany w `--esp-sdkconfig` bez
  rejestru albo parametru daje zwykły komunikat tam, gdzie Python kończył się
  wyjątkiem `KeyError`.
- Pliki rejestru są czytane jak inne pliki sprzętowe: powtórzona nazwa, liczba
  nieskończona albo samotny surogat dają `<plik>: invalid JSON: <powód>`
  z wierszem i kolumną, tak samo jak uszkodzony JSON, przy którym Python
  kończył się tracebackiem. Plik, którego nie da się odczytać, daje komunikat z nazwą pliku i statusem
  zamiast tekstu błędu systemu operacyjnego.
- Płytki pochodzą ze sprawdzonego rejestru płytek, więc płytka odrzucana przez
  `jh board validate` przerywa kontrolę tym komunikatem, a skrypt czytał
  profile bez sprawdzania.
- Nagłówki i pliki kompilacji są czytane jako bajty; CR LF i samotne CR
  czytane są jako LF, tak jak w Pythonie. We wzorcach `\s`, `\w`, `\b` i `\d`
  obejmują tylko ASCII, a moduł `re` Pythona uznawał też inne litery, cyfry
  i spacje Unicode. Nagłówek, który nie jest poprawnym UTF-8, też podlega
  sprawdzeniu, a Python kończył się na nim wyjątkiem `UnicodeDecodeError`.
- Od każdej pozycji startowej wyszukiwanie wzorca może wykonać 64 kroki na bajt
  pliku, co najmniej dziesięć milionów, co wystarcza na cofanie się przez cały
  plik. Wyszukiwanie, które potrzebuje więcej, przerywa kontrolę kodem 4
  i komunikatem `<plik>: the pattern search stopped: <powód>`; moduł `re`
  Pythona nie ma takiego limitu.

## Katalog typów i wartości domyślne urządzeń

`jh generate device-defaults --root <checkout>` czyta każdy `*.json`
z `config/hardware/bindings` jako typ HAL nazwany od swojego compatible
(`vendor,name` to plik `vendor-name.json`), sprawdza każdy plik schematem
`config/hardware/binding.schema.json`, a potem reguły spoza schematu: `cType`
musi mieścić JSON-owy `type`; `unit` to drukowalny tekst NFC bez białych
znaków na brzegach, oceniany danymi Unicode przypiętymi przez jhbase;
`minimum`, `maximum`, `default` i elementy `enum` mieszczą się w typie C,
przy czym `float`/`double` są zaokrąglane raz z dokładnej wartości dziesiętnej
i odrzucane przy przepełnieniu albo gdy niezerowa wartość zaokrągla się do
zera; `default` jest elementem `enum` i mieści się w granicach;
`when.property` wskazuje właściwość tego typu; dziecko wskazuje znany typ
i ustawia tylko jego właściwości pasującymi wartościami; połączenie wskazuje
sygnał modułu i sygnały zadeklarowanych dzieci, każdy z jednym właścicielem;
`requiresFeatures` to flagi z `config/features/*.json`, a `provides` to
capabilities z `boards/capabilities.json`; rozwijanie dzieci nie ma cyklu.
Liczba zapisana z ułamkiem albo wykładnikiem musi też zachować skończoną,
niezerową wartość w binary64, tak jak wymagał loader Pythona.

Ścieżka nagłówka pochodzi z `generatedArtifacts.deviceDefaultsHeader`
w `config/tooling/artifacts.json`, względem `--output-root` (domyślnie root);
ścieżka absolutna, z dyskiem albo z segmentem `..` kończy się kodem 2, zanim
cokolwiek zostanie zapisane. `--bindings <katalog>` czyta inny katalog typów, np. zmienione kopie w testach.
Wymagana jest dokładnie jedna z opcji `--write` i `--check`. `--write`
publikuje nagłówek przez atomowe API plików, zostawia identyczny plik
nietknięty i wypisuje `generated 1 device defaults artifacts (N changed)`.
`--check` niczego nie zmienia: aktualny nagłówek daje `verified 1 generated
device defaults artifacts`, brakujący albo inny jest zgłaszany na stderr
z unified diff do 40 linii, a kod wyjścia to 1. Nagłówek zawiera pozycję
każdej tekstowej wartości enuma jako `JH_ENUM_<TYP>_<WŁAŚCIWOŚĆ>_<WARTOŚĆ>`,
dzięki czemu kod C może sprawdzić przez `static_assert`, że enum HAL zachowuje
kolejność z typu, domyślną wartość każdej właściwości jako
`JH_DEFAULT_<TYP>_<WŁAŚCIWOŚĆ>` oraz domyślną wartość każdego dziecka jako
`JH_DEFAULT_<MODUŁ>_<DZIECKO>_<WŁAŚCIWOŚĆ>`, już po wartościach, które moduł
ustawia temu dziecku; typy i właściwości idą w kolejności punktów kodowych.
Helpery HAL, np. `hal_lora_sx126x_core1262_hf_defaults()`
i `hal_can_default_config()`, czytają te makra zamiast trzymać drugą tabelę.
Literały mają zapis z tabeli [formatu drzewa urządzeń](../../doc/pl/device_tree_format.md).
Makro wyprodukowane dwa razy kończy się błędem zamiast zmiany nazwy.

Z `--json` wynik ma `command: "generate.device-defaults"`, `status`
(`HAL_OK` albo `HAL_ESTATE` dla brakującego lub nieaktualnego nagłówka),
`root`, `bindings`, `output`, `changed` i `checkOnly`. Odrzucony katalog
kończy się kodem 2 i `status: "HAL_ECONFIG"`; wynik dodaje `code`
(`JH-HW-SCHEMA`, `JH-HW-BINDING` albo `JH-HW-PROPERTY`), `source` (plik)
i `pointer` (JSON Pointer błędnej wartości, pusty dla całego pliku),
a komunikat ma postać `[kod] plik:pointer: szczegół`. Zgłaszany jest tylko
pierwszy błąd: o najniższym Pointerze w pierwszym pliku według nazwy.
Brak roota, katalogu typów albo pliku rejestru daje kod 3.

| Kod wyjścia | Znaczenie |
|---|---|
| 0 | Sukces, także aktualny plik wynikowy. |
| 1 | `--check` wykrył brak pliku albo różnicę. |
| 2 | Błędne argumenty, niepoprawny JSON/schemat, nieudana walidacja, odrzucony katalog typów, rejestr funkcji lub nagłówek projektu albo nierozwiązany Pointer. |
| 3 | Błąd ścieżki, pliku, stdout lub nieoczekiwany błąd runtime. |
| 4 | Brak pamięci, przekroczony limit wejścia, parsera, walidatora lub wyjścia albo 64-bitowy zakres czytnika konfiguracji projektu. |

To kody tego CLI. Dotychczasowe polecenia `jh-vscode` zachowują swój
entrypoint i kody zakończenia.

Executable oraz licencje PCRE2, jhbase, Ryu, utf8proc i JaszczurHAL instaluje
`cmake --install <build> --config Release --component jh --prefix <destination>`. Program można przenieść wraz z prefixem
instalacji; do uruchomienia nie potrzebuje drzewa źródeł.

## Źródła

Moduły C narzędzia kompilują się jako C11. [jh_failure.h](base/jh_failure.h)
definiuje rekord błędu, który każda funkcja C wypełnia dla wywołującego, oraz
wspólne dla modułów spany i listy napisów.
[jh_json_file.h](json/jh_json_file.h) czyta pliki JSON, ustawienia VS Code
i dokumenty schematów wczytywane podczas walidacji,
[jh_python_repr.h](json/jh_python_repr.h) cytuje tekst tak jak `repr()`
w Pythonie, a [jh_generated_output.h](src/jh_generated_output.h) publikuje
i sprawdza generowane artefakty. [jh_feature_registry.h](config/jh_feature_registry.h)
wczytuje rejestr funkcji HAL, renderuje jego artefakty i rozwiązuje żądania
funkcji; digesty liczy rdzeń SHA-256 HAL z
[jh_sha256.h](../../src/hal/security/jh_sha256.h), który narzędzie kompiluje
samodzielnie. [jh_feature_lint.h](config/jh_feature_lint.h) wyszukuje wejścia
lintu funkcji i sprawdza manifesty projektów.
[jh_board_registry.h](config/jh_board_registry.h) wczytuje i sprawdza rejestr
płytek, a [jh_clock_registry.h](config/jh_clock_registry.h) rejestr drzew
zegarów razem z kontrolami kodu, który ustawia zegary. Walidator schematów,
czytnik konfiguracji
projektu, katalog typów i `main.cpp` są jeszcze w C++. Korzystają z modułów C
przez właścicieli zasobów i wyjątki z [cxx/](cxx/), które znikną razem
z ostatnim wywołującym w C++.
