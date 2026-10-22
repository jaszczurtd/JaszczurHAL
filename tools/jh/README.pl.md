# CLI hosta jh

*Dostępna jest także [wersja angielska](README.md).*

`jh json inspect` odczytuje JSON, wybiera wartość przez JSON Pointer i może
zapisać albo sprawdzić plik wynikowy. Korzysta z parsera i API plików jhbase.
`jh json validate` sprawdza dokument według podzbioru schematów sprzętowych,
także z odwołaniami między lokalnymi plikami. Generatory nadal korzystają z Pythona.

Przed konfiguracją przygotuj zależności hosta w wersjach wskazanych przez repozytorium:

```sh
python3 scripts/component_manager.py component jhbase
python3 scripts/component_manager.py component utf8proc
python3 scripts/component_manager.py component pcre2
```

Testy wymagają też utf8proc, którego pin dostarcza jhbase. Jego osobny checkout
znajduje się w `third_party/utf8proc`.

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

| Kod wyjścia | Znaczenie |
|---|---|
| 0 | Sukces, także aktualny plik wynikowy. |
| 1 | `--check` wykrył brak pliku albo różnicę. |
| 2 | Błędne argumenty, niepoprawny JSON/schemat, nieudana walidacja lub nierozwiązany Pointer. |
| 3 | Błąd ścieżki, pliku, stdout lub nieoczekiwany błąd runtime. |
| 4 | Brak pamięci albo przekroczony limit wejścia, parsera, walidatora lub wyjścia. |

To kody tego CLI. Dotychczasowe polecenia `jh-vscode` zachowują swój
entrypoint i kody zakończenia.

Executable i jego licencje instaluje `cmake --install <build> --config Release --component
jh --prefix <destination>`. Program można przenieść wraz z prefixem
instalacji; do uruchomienia nie potrzebuje drzewa źródeł.
