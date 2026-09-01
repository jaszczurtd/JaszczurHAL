# Rejestr funkcji HAL

Fragmenty JSON w tym katalogu definiują pełny, zamknięty zbiór obsługiwanych symboli
`HAL_ENABLE_*` i `HAL_DISABLE_*` oraz niezależną od targetu część grafu ich
zależności.

Po zmianie fragmentu uruchom generator:

```bash
python3 scripts/sync_generated.py --write
```

Wygenerowane artefakty C i CMake są przechowywane w repozytorium, dzięki czemu
zainstalowane pakiety i buildy korzystające bezpośrednio z kompilatora nie
wymagają Pythona. CI sprawdza je przez
`python3 scripts/sync_generated.py --check`.

W produkcyjnych buildach C/C++ plik `src/hal/generated/jh_hal_features.h` jest
dołączany przez `hal_config.h`.
Generatory RP, STM32 i płytek, generator sygnatury linkowania
oraz `jh-vscode` używają wygenerowanej tabeli CMake albo tego samego modelu
rejestru. Bezwarunkowe zależności funkcji mają zatem jedno miarodajne źródło.

Mechanizm rozwiązywania zależności funkcji utrzymuje dwa zbiory:

- `requestedFeatures` zawiera bezpośrednie żądania projektu, CMake i linii
  poleceń po normalizacji i usunięciu duplikatów;
- `resolvedFeatures` zawiera `requestedFeatures` oraz posortowane przechodnie
  domknięcie rejestru.

Wybór źródeł i zależności korzysta z `resolvedFeatures`. Diagnostyka zachowuje
`requestedFeatures`, dlatego funkcja wynikająca z zależności nie jest
przedstawiana jako bezpośrednie żądanie użytkownika. `jh_board_resolved.json`
zapisuje oba zbiory i ich skrót. Pole `features`, zachowane dla zgodności, jest
aliasem `resolvedFeatures`.

Opcjonalne rekordy `buildEffects` przechowują dodatkowe wejścia buildu obok
funkcji, do której należą:

- `featureSources` wymienia źródła, które każdy build musi dodać po włączeniu
  danej funkcji, na przykład Unity lub PubSubClient;
- `portableSources` wymienia implementacje niezależne od targetu, używane przez
  selektywne systemy kompilacji; systemy korzystające z pełnych wykazów źródeł
  dołączają tę listę i usuwają powtarzające się ścieżki;
- `dependencies` wybiera istniejący manifest źródeł zależności utrzymywanych
  przez projekt. Obsługiwane nazwy to `bearssl`, `littlefs` i `sx126x`.

Generator sprawdza te rekordy i przekazuje je do CMake. ESP-IDF czyta ten sam
model rejestru i łączy przenośne źródła z mapą źródeł zdefiniowaną przez dany
target. Adaptery targetów, możliwości płytek, układy pamięci flash i specjalne
obrazy firmware'u pozostają poza `buildEffects`.

Dane wejściowe projektu korzystającego z HAL można sprawdzić niezależnie od
mechanizmu wyznaczającego konfigurację buildu:

```bash
python3 scripts/generate_hal_features.py --lint --input-root .
```

Kontrola bezpośrednich definicji przyjmuje zapis oparty na samej obecności
symbolu oraz `=1`. Wartość `=0`, nieznany symbol i bezpośrednie żądanie symbolu
`derived` są błędami konfiguracji. Definicje funkcji w `hal_project_config.h` muszą być
bezwarunkowe albo objęte warunkiem `#ifndef` dotyczącym tego samego symbolu.
Listy definicji CMake są pojedynczymi ciągami znaków rozdzielonymi średnikami.

Kontrola konfiguracji wynikowej korzysta z mechanizmu wyboru profilu targetu
i wariantu z `jh-vscode`. Ignoruje lokalny stan płytki zapisany w plikach
pomijanych przez Git i zapisuje deterministyczne dane o funkcjach żądanych
i wynikowych, ich skrót oraz pochodzenie:

```bash
python3 scripts/generate_hal_features.py \
  --lint --effective --input-root . \
  --resolution-output .build/effective-feature-resolution.json
```

Obie kontrole działają rygorystycznie: wykrycie problemu powoduje zwrócenie
niezerowego kodu zakończenia. `--report-only` pozostaje dostępne dla
tymczasowych audytów migracji, ale nie jest normalnym wywołaniem CI.

Standardowe pliki `.vscode/jaszczurhal.project.json` wymieniają osie targetu,
płytki i wariantu. Samodzielny `hal_project_config.h` z co najmniej jednym
żądaniem funkcji HAL dodaje jeden bezpośredni kontekst bez przypisania do osi.
Samodzielne nagłówki bez żądań i manifesty używane jako dane odniesienia
pozostają w wykazie kontroli bezpośrednich definicji,
ale nie tworzą sztucznych konfiguracji.

`config/effective-features-baseline.json` utrwala skrót wynikowej macierzy
repozytorium. Testy rejestru mapują każdy rekord na sprawdzoną, unikalną krotkę
target-płytka-żądanie dla preprocessora produkcyjnego oraz na sprawdzony,
unikalny zbiór żądań dla wygenerowanego mechanizmu CMake.

## Reguły poza rejestrem v1

`hal_config.h` zachowuje reguły targetu, płytki, providera, możliwości i
parametrów strojenia, których schemat v1 nie potrafi wyrazić. Dwie zachowane
reguły dodają także symbole funkcji:

- `HAL_ENABLE_EEPROM` dodaje `HAL_ENABLE_I2C` tylko wtedy, gdy
  `HAL_EEPROM_TYPE == EEPROM_TYPE_AT24C256`;
- `HAL_ENABLE_GPS` dodaje `HAL_ENABLE_UART` tylko wtedy, gdy nie zażądano UART
  ani software serial.

Te warunkowe reguły zastępcze działają po domknięciu rejestru i celowo
pozostają poza `resolvedFeatures`. Skrót zestawu funkcji płytki potwierdza więc
równoważność zbiorów wyznaczonych przez rejestr, ale nie wszystkich końcowych
makr powstałych po zastosowaniu pozostałych reguł. Wybór providera, możliwości
płytki, ograniczenia targetu i walidacja parametrów strojenia również pozostają
w `hal_config.h`.
