# CLI hosta jh

*Dostępna jest także [wersja angielska](README.md).*

`jh json inspect` odczytuje JSON, wybiera wartość przez JSON Pointer i może
zapisać albo sprawdzić plik wynikowy. Korzysta z parsera i API plików jhbase.
Walidacja schematów i generatory produkcyjne nie są tu jeszcze zaimplementowane.

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

| Kod wyjścia | Znaczenie |
|---|---|
| 0 | Sukces, także aktualny plik wynikowy. |
| 1 | `--check` wykrył brak pliku albo różnicę. |
| 2 | Błędne argumenty, niepoprawny JSON lub nierozwiązany Pointer. |
| 3 | Błąd ścieżki, pliku, stdout lub nieoczekiwany błąd runtime. |
| 4 | Brak pamięci albo przekroczony limit wejścia, parsera lub wyjścia. |

To kody tego CLI. Dotychczasowe polecenia `jh-vscode` zachowują swój
entrypoint i kody zakończenia.

Sam executable instaluje `cmake --install <build> --config Release --component
jh --prefix <destination>`. Program można przenieść wraz z prefixem
instalacji; do uruchomienia nie potrzebuje drzewa źródeł.
