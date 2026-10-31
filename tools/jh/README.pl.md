# CLI hosta jh

*Dostępna jest także [wersja angielska](README.md).*

`jh json inspect` odczytuje JSON, wybiera wartość przez JSON Pointer i może
zapisać albo sprawdzić plik wynikowy. Korzysta z parsera i API plików jhbase.
`jh json validate` sprawdza dokument według podzbioru schematów sprzętowych,
także z odwołaniami między lokalnymi plikami. `jh generate device-defaults`
czyta katalog typów komponentów HAL i zapisuje albo sprawdza generowany
nagłówek ich wartości domyślnych. `scripts/sync_generated.py`, a przez niego
hook pre-commit i bramka, uruchamiają to polecenie przez opisany niżej
launcher.

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

Na Windows użyj developer PowerShell Visual Studio 2022:

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
```

Ścieżki wejściowe są rozwiązywane względem cwd odczytanego raz na wejściu.
Argumenty Windows pochodzą z `wmain` i są zamieniane z UTF-16 na UTF-8.
Opcje przyjmują `--name value` albo `--name=value`; flagi nie mają wartości.
Nieznane i powtórzone opcje oraz dodatkowe słowa polecenia dają błąd.
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
w `config/tooling/artifacts.json`, względem `--output-root` (domyślnie root).
`--bindings <katalog>` czyta inny katalog typów, np. zmienione kopie w testach.
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
| 2 | Błędne argumenty, niepoprawny JSON/schemat, nieudana walidacja, odrzucony katalog typów lub nierozwiązany Pointer. |
| 3 | Błąd ścieżki, pliku, stdout lub nieoczekiwany błąd runtime. |
| 4 | Brak pamięci albo przekroczony limit wejścia, parsera, walidatora lub wyjścia. |

To kody tego CLI. Dotychczasowe polecenia `jh-vscode` zachowują swój
entrypoint i kody zakończenia.

Executable oraz licencje PCRE2, jhbase, Ryu, utf8proc i JaszczurHAL instaluje
`cmake --install <build> --config Release --component jh --prefix <destination>`. Program można przenieść wraz z prefixem
instalacji; do uruchomienia nie potrzebuje drzewa źródeł.
