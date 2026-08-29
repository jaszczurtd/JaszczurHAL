# Rejestr funkcji HAL

Fragmenty JSON w tym katalogu definiują zamkniętą przestrzeń wspieranych symboli
`HAL_ENABLE_*` i `HAL_DISABLE_*` oraz niezależną od targetu część grafu ich
zależności.

Po zmianie fragmentu uruchom generator:

```bash
python3 scripts/sync_generated.py --write
```

Generowane artefakty C i CMake są śledzone, dzięki czemu zainstalowane pakiety i
bezpośrednie buildy nie wymagają Pythona. CI sprawdza je przez
`python3 scripts/sync_generated.py --check`.

Kod produkcyjny C/C++ dołącza `src/hal/generated/jh_hal_features.h` przez
`hal_config.h`. Generatory RP, STM32 i boardów, generator sygnatury linkowania
oraz `jh-vscode` używają wygenerowanej tabeli CMake albo tego samego modelu
rejestru. Bezwarunkowe zależności funkcji mają zatem jedno utrzymywane źródło
prawdy.

Rozwiązywanie funkcji utrzymuje dwa zbiory:

- `requestedFeatures` zawiera bezpośrednie żądania projektu, CMake i linii
  poleceń po normalizacji i usunięciu duplikatów;
- `resolvedFeatures` zawiera `requestedFeatures` oraz posortowane przechodnie
  domknięcie rejestru.

Wybór źródeł i zależności korzysta z `resolvedFeatures`. Diagnostyka zachowuje
`requestedFeatures`, dlatego funkcja wynikająca z zależności nie jest
przedstawiana jako bezpośrednie żądanie użytkownika. `jh_board_resolved.json`
zapisuje oba zbiory i ich digest. Pole zgodności `features` jest aliasem
`resolvedFeatures`.

Opcjonalne rekordy `buildEffects` przechowują dodatkowe wejścia builda obok
funkcji, która jest ich właścicielem:

- `featureSources` wymienia źródła dodawane do każdego builda z aktywną funkcją,
  na przykład Unity lub PubSubClient;
- `portableSources` wymienia implementacje niezależne od targetu, używane przez
  selektywne systemy builda; szerokie wykazy źródeł używają tej listy do
  walidacji bez ponownego dodawania plików;
- `dependencies` wybiera istniejący manifest zarządzanego źródła. Wspierane
  nazwy to `bearssl`, `littlefs` i `sx126x`.

Generator sprawdza i emituje te rekordy dla CMake. ESP-IDF czyta ten sam model
rejestru i łączy przenośne źródła z mapą źródeł należących do targetu. Adaptery
targetów, capabilities boardów, układy flash i specjalne obrazy firmware
pozostają poza `buildEffects`.

Wejścia konsumenta można sprawdzić niezależnie od resolvera builda:

```bash
python3 scripts/generate_hal_features.py --lint --input-root .
```

Surowy lint przyjmuje definicje oparte na samej obecności oraz `=1`. Wartość
`=0`, nieznany symbol lub bezpośrednie żądanie symbolu `derived` jest błędem
konfiguracji. Definicje funkcji w `hal_project_config.h` muszą być
bezwarunkowe albo używać osłony `#ifndef` dla tego samego symbolu. Listy
definicji CMake są skalarnymi ciągami rozdzielonymi średnikami.

Efektywny lint ponownie wykorzystuje resolver profilu targetu i wariantu z
`jh-vscode`, ignoruje lokalny stan boardu pomijany przez Git i zapisuje
deterministyczne dane żądane, rozwiązane, digest oraz provenance:

```bash
python3 scripts/generate_hal_features.py \
  --lint --effective --input-root . \
  --resolution-output .build/effective-feature-resolution.json
```

Surowy i efektywny lint działają rygorystycznie: wykryte problemy zwracają
niezerowy kod. `--report-only` pozostaje dostępne dla tymczasowych audytów
migracji, ale nie jest normalnym wywołaniem CI.

Standardowe pliki `.vscode/jaszczurhal.project.json` wymieniają osie targetu,
boardu i wariantu. Samodzielny `hal_project_config.h` z co najmniej jednym
żądaniem funkcji HAL dodaje jeden bezosiowy kontekst bezpośredni. Samodzielne
nagłówki bez żądań i manifesty referencyjne pozostają w wykazie surowego lintu,
ale nie tworzą sztucznych konfiguracji.

`config/effective-features-baseline.json` zamraża digest efektywnej macierzy
repozytorium. Testy rejestru mapują każdy rekord na sprawdzoną, unikalną krotkę
target/board/żądanie dla preprocessora produkcyjnego oraz na sprawdzony,
unikalny zbiór żądań dla wygenerowanego resolvera CMake.

## Reguły poza registry v1

`hal_config.h` zachowuje reguły targetu, boardu, providera, capabilities i
parametrów dostrajanych, których schemat v1 nie potrafi wyrazić. Dwie zachowane
reguły dodają także symbole funkcji:

- `HAL_ENABLE_EEPROM` dodaje `HAL_ENABLE_I2C` tylko wtedy, gdy
  `HAL_EEPROM_TYPE == EEPROM_TYPE_AT24C256`;
- `HAL_ENABLE_GPS` dodaje `HAL_ENABLE_UART` tylko wtedy, gdy nie zażądano UART
  ani software serial.

Te warunkowe fallbacki działają po domknięciu rejestru i celowo pozostają poza
`resolvedFeatures`. Hash funkcji boardu określa więc równoważność zbioru
rozwiązanego przez rejestr, a nie wszystkich końcowych makr po wykonaniu
pozostałych reguł. Wybór providera, capabilities boardu, ograniczenia targetu i
walidacja parametrów dostrajanych również pozostają w `hal_config.h`.
