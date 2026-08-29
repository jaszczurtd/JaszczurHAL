# cJSON

*Dostępne również [po angielsku](../en/17_cJSON.md).*

> **Część [Dokumentacji API JaszczurHAL](../../pl/JaszczurHAL_API.md)**

Obejmuje: zarządzane `cJSON` i `cJSON_Utils` włączane przez `HAL_ENABLE_CJSON`.

`cJSON` to niewielki parser/generator JSON w C, pobierany do
`third_party/cJSON` w commicie przypiętym przez
`third_party/cjson_version.conf`. Cienkie wrappery integracyjne w
`src/hal/codecs/cjson/` dołączają nagłówki i źródła upstream tylko przy
`HAL_ENABLE_CJSON`, zachowując publiczną ścieżkę include.

Zarządzana wersja: `cJSON` 1.7.18.

Autor/licencja: `cJSON` upstream jest autorstwa Dave'a Gamble'a i
współtwórców i jest dystrybuowany na licencji MIT.

## Włączenie

Włącz moduł w `hal_project_config.h` lub definicją kompilatora:

```c
#pragma once

#define HAL_ENABLE_CJSON
```

Pliki źródłowe są częścią wspólnej listy źródeł frameworku, ale ich
zawartość kompiluje się do niczego, dopóki nie zdefiniowano
`HAL_ENABLE_CJSON`. Publiczne nagłówki są również bramkowane, więc kod
używający symboli `cJSON_*` musi być kompilowany z tą samą flagą.

## Dołączanie

Bezpośrednie dołączenie, bezpieczne zarówno z C, jak i z C++:

```c
#include <hal/codecs/cjson/cJSON.h>
#include <hal/codecs/cjson/cJSON_Utils.h>
```

Dla plików C++ korzystających już z agregatora narzędziowego, `tools.h`
również udostępnia cJSON, gdy zdefiniowano `HAL_ENABLE_CJSON`:

```c
#include <tools.h>
```

`tools.h` dołącza klasy narzędziowe C++, więc w plikach `.c` preferuj
bezpośrednie dołączenia z frameworku. `tools_c.h` nie re-eksportuje cJSON.

`JaszczurHAL.h` dołącza parasol HAL, a nie agregator narzędziowy, więc tam,
gdzie cJSON jest używany bezpośrednio, dołączaj nagłówki frameworku lub
`tools.h` z plików C++.

## Zakres API

Podstawowe API `cJSON`:

| Kategoria | Typowe funkcje |
|---|---|
| Parsowanie | `cJSON_Parse`, `cJSON_ParseWithLength`, `cJSON_ParseWithOpts`, `cJSON_ParseWithLengthOpts` |
| Inspekcja | `cJSON_GetObjectItemCaseSensitive`, `cJSON_GetArrayItem`, `cJSON_GetArraySize`, `cJSON_IsString`, `cJSON_IsNumber`, `cJSON_IsBool`, `cJSON_IsObject`, `cJSON_IsArray` |
| Tworzenie | `cJSON_CreateObject`, `cJSON_CreateArray`, `cJSON_CreateString`, `cJSON_CreateNumber`, `cJSON_CreateBool`, `cJSON_CreateNull` |
| Dodawanie | `cJSON_AddStringToObject`, `cJSON_AddNumberToObject`, `cJSON_AddBoolToObject`, `cJSON_AddArrayToObject`, `cJSON_AddObjectToObject`, `cJSON_AddItemToArray`, `cJSON_AddItemToObject` |
| Aktualizacja | `cJSON_SetNumberValue`, `cJSON_SetValuestring`, `cJSON_ReplaceItemInObjectCaseSensitive`, `cJSON_DeleteItemFromObjectCaseSensitive` |
| Wypisywanie | `cJSON_Print`, `cJSON_PrintUnformatted`, `cJSON_PrintBuffered`, `cJSON_PrintPreallocated` |
| Zwalnianie | `cJSON_Delete`, `cJSON_free` |

`cJSON_Utils` dodaje pomocników dla JSON Pointer, JSON Patch, JSON Merge
Patch oraz sortowania obiektów:

| Funkcja | Funkcje |
|---|---|
| JSON Pointer (RFC 6901) | `cJSONUtils_GetPointer`, `cJSONUtils_GetPointerCaseSensitive` |
| JSON Patch (RFC 6902) | `cJSONUtils_ApplyPatches`, `cJSONUtils_GeneratePatches`, `cJSONUtils_AddPatchToArray` |
| JSON Merge Patch (RFC 7386) | `cJSONUtils_MergePatch`, `cJSONUtils_GenerateMergePatch` |
| Sortowanie / ścieżki | `cJSONUtils_SortObject`, `cJSONUtils_FindPointerFromObjectTo` |

## Własność pamięci

cJSON domyślnie wykorzystuje alokację dynamiczną.

Najważniejsze zasady:

- `cJSON_Parse*()` zwraca drzewo będące własnością wywołującego. Zwolnij je
  przez `cJSON_Delete(root)`.
- `cJSON_Create*()` zwraca element będący własnością wywołującego, dopóki
  nie zostanie dodany do tablicy/obiektu. Po `cJSON_AddItemToArray()` lub
  `cJSON_AddItemToObject()` właścicielem elementu jest rodzic.
- `cJSON_AddStringToObject()` i podobne funkcje pomocnicze tworzą i
  dołączają nowe dziecko. Jego właścicielem jest obiekt nadrzędny.
- Funkcje `cJSON_Print*()` zwracające `char *` alokują tekst. Zwolnij go
  przez `cJSON_free(text)`.
- `cJSON_PrintPreallocated()` zapisuje do bufora dostarczonego przez
  wywołującego. Wywołujący jest właścicielem bufora i musi zapewnić nieco
  dodatkowego miejsca; upstream zaleca zaalokowanie około 5 bajtów więcej
  niż oczekiwane wyjście.
- `cJSONUtils_MergePatch(target, patch)` może zwrócić inny wskaźnik niż
  `target`. Zawsze przypisuj zwróconą wartość z powrotem do swojego
  wskaźnika korzenia (root).

Niestandardowe hooki alokacji można zainstalować przez `cJSON_InitHooks()`.
Zrób to raz, przy starcie, zanim zostaną utworzone jakiekolwiek obiekty
JSON. Hooki są globalnym stanem procesu, a nie stanem per-dokument.

## Thread safety

Dokumenty cJSON są niezależne, dopóki każde zadanie/rdzeń jest właścicielem
własnego drzewa lub zewnętrzne blokowanie chroni współdzielone drzewa.
JaszczurHAL nie dodaje mutexu wokół operacji cJSON.

Ważny współdzielony/globalny stan:

- `cJSON_InitHooks()` zmienia globalne hooki alokatora. Wywołaj ją raz
  podczas startu, zanim rozpocznie się współbieżne użycie JSON.
- `cJSON_GetErrorPtr()` odczytuje stan błędu parsera. W kodzie współbieżnym
  preferuj `cJSON_ParseWithOpts(..., &end, ...)`, ponieważ zwraca wskaźnik
  końca parsowania/błędu przez pamięć będącą własnością wywołującego.

## Przykład: parsowanie konfiguracji

```c
#include <hal/codecs/cjson/cJSON.h>
#include <hal/serial/hal_serial.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>

typedef struct {
    char ssid[33];
    uint32_t sample_ms;
    bool enabled;
} app_config_t;

static bool load_config_from_json(const char *json, app_config_t *out) {
    if (json == NULL || out == NULL) {
        return false;
    }

    const char *parse_end = NULL;
    cJSON *root = cJSON_ParseWithOpts(json, &parse_end, 1);
    if (root == NULL) {
        hal_derr("config JSON parse failed near: %.16s",
                 parse_end != NULL ? parse_end : "");
        return false;
    }

    bool ok = false;
    const cJSON *ssid = cJSON_GetObjectItemCaseSensitive(root, "ssid");
    const cJSON *sample_ms = cJSON_GetObjectItemCaseSensitive(root, "sample_ms");
    const cJSON *enabled = cJSON_GetObjectItemCaseSensitive(root, "enabled");

    if (cJSON_IsString(ssid) && ssid->valuestring != NULL &&
        cJSON_IsNumber(sample_ms) && cJSON_IsBool(enabled)) {
        snprintf(out->ssid, sizeof(out->ssid), "%s", ssid->valuestring);
        out->sample_ms = (uint32_t)cJSON_GetNumberValue(sample_ms);
        out->enabled = cJSON_IsTrue(enabled);
        ok = true;
    }

    cJSON_Delete(root);
    return ok;
}
```

Wejście:

```json
{"ssid":"lab-net","sample_ms":1000,"enabled":true}
```

## Przykład: budowanie i wypisywanie JSON

Użyj `cJSON_PrintPreallocated()`, gdy wyjście ma ograniczony rozmiar, a
chcesz uniknąć alokowania bufora wypisywania.

```c
#include <hal/codecs/cjson/cJSON.h>
#include <hal/serial/hal_serial.h>
#include <stdbool.h>
#include <stdint.h>

static bool print_status_json(uint32_t uptime_ms, float temperature_c) {
    cJSON *root = cJSON_CreateObject();
    if (root == NULL) {
        return false;
    }

    bool ok = true;
    ok = ok && cJSON_AddStringToObject(root, "device", "node-1") != NULL;
    ok = ok && cJSON_AddNumberToObject(root, "uptime_ms", uptime_ms) != NULL;
    ok = ok && cJSON_AddNumberToObject(root, "temperature_c",
                                       temperature_c) != NULL;

    char out[160];
    if (ok) {
        ok = cJSON_PrintPreallocated(root, out, sizeof(out), 0) != 0;
    }

    if (ok) {
        hal_serial_println(out);
    }

    cJSON_Delete(root);
    return ok;
}
```

Dla wyjścia o dynamicznym rozmiarze użyj `cJSON_PrintUnformatted()` i
zwolnij wynik:

```c
char *text = cJSON_PrintUnformatted(root);
if (text != NULL) {
    hal_serial_println(text);
    cJSON_free(text);
}
```

## Przykład: budowanie JSON z NONULL

`NONULL(x)` to pomocnik JaszczurHAL z `hal_system.h`, a nie API cJSON. Jest
przydatny w zwięzłych budowniczych (builderach), które używają jednej
etykiety porządkującej `error:`. Jeśli `x` daje w wyniku `NULL`, makro
skacze do tej etykiety.

Ten wzorzec dobrze współgra z pomocnikami `cJSON_Add*ToObject()` i
`cJSON_PrintUnformatted()`, ponieważ oba zwracają wskaźniki, które muszą
być sprawdzone.

```c
#include <hal/codecs/cjson/cJSON.h>
#include <hal/system/hal_system.h>
#include <stdbool.h>

typedef struct {
    double latitude_deg;
    double longitude_deg;
    double accuracy_m;
} cell_location_t;

static char *build_location_json(bool cell_location_valid,
                                 const cell_location_t cell_location) {
    cJSON *root = NULL;
    char *json = NULL;

    NONULL(root = cJSON_CreateObject());

    if (cell_location_valid) {
        NONULL(cJSON_AddNumberToObject(root, "cell_lat",
                                       cell_location.latitude_deg));
        NONULL(cJSON_AddNumberToObject(root, "cell_lng",
                                       cell_location.longitude_deg));
        NONULL(cJSON_AddNumberToObject(root, "cell_acc_m",
                                       cell_location.accuracy_m));
    }
    NONULL(cJSON_AddNumberToObject(root, "ms", hal_millis()));

    NONULL(json = cJSON_PrintUnformatted(root));

error:
    cJSON_Delete(root);
    return json;
}
```

Zwrócony `char *` jest własnością wywołującego. Zwolnij go przez
`cJSON_free(json)` po wysłaniu lub zapisaniu. Zwrócenie `NULL` oznacza, że
alokacja zawiodła podczas tworzenia drzewa lub wypisywania końcowego JSON.

## Przykład: JSON Pointer i Merge Patch

```c
#include <hal/codecs/cjson/cJSON.h>
#include <hal/codecs/cjson/cJSON_Utils.h>
#include <stdbool.h>

static bool update_uart_config(cJSON **root_inout) {
    if (root_inout == NULL || *root_inout == NULL) {
        return false;
    }

    cJSON *baud = cJSONUtils_GetPointerCaseSensitive(*root_inout, "/uart/baud");
    if (cJSON_IsNumber(baud)) {
        cJSON_SetNumberValue(baud, 230400);
    }

    cJSON *patch = cJSON_Parse(
        "{\"network\":{\"dhcp\":true},\"legacy_key\":null}");
    if (patch == NULL) {
        return false;
    }

    cJSON *merged = cJSONUtils_MergePatch(*root_inout, patch);
    cJSON_Delete(patch);
    if (merged == NULL) {
        return false;
    }

    *root_inout = merged;
    return true;
}
```

JSON Pointer wykorzystuje ścieżki rozdzielane `/`. Dla kluczy obiektów
zawierających `~` lub `/`, ucieknij je jako `~0` i `~1`.

## Uwagi dotyczące systemów wbudowanych

- Zawsze sprawdzaj zwracane wskaźniki pod kątem `NULL`; niepowodzenie
  alokacji to normalny tryb awarii w systemach wbudowanych.
- Preferuj `cJSON_GetObjectItemCaseSensitive()` podczas parsowania danych
  konfiguracyjnych. Unika to zaskakujących dopasowań kluczy różniących się
  wielkością liter.
- Liczby są przechowywane jako `double` plus cache liczby całkowitej.
  Rzutuj świadomie na granicy swojej aplikacji.
- Utrzymuj dokumenty małe. Parsowanie i wypisywanie alokują pamięć
  proporcjonalną do drzewa JSON i tekstu wyjściowego.
- Preferuj `cJSON_PrintPreallocated()` dla wiadomości telemetrii/statusu ze
  znaną górną granicą.
- `cJSON_Minify()` modyfikuje swój bufor wejściowy w miejscu; nie
  przekazuj literałów łańcuchowych ani buforów wspieranych przez
  flash/ROM.
- Domyślny `CJSON_NESTING_LIMIT` wynosi 1000. Dla małych MCU rozważ jego
  obniżenie definicją buildu, jeśli niezaufany JSON może docierać
  spoza urządzenia.
- Generowanie łatek (patch) `cJSON_Utils` może sortować i mutować obiekty
  wejściowe, zgodnie z uwagami upstream. Jeśli oryginalna
  kolejność/zawartość musi pozostać nietknięta, najpierw zduplikuj
  dokumenty.

## Przechowywanie i transport

Samo cJSON działa wyłącznie w RAM. Utrwalaj lub przenoś tekst przez
odpowiedni moduł HAL:

- Użyj `hal_littlefs` dla plików JSON na LittleFS RP2040.
- Użyj `hal_kv` dla małych skalarnych wartości konfiguracyjnych, gdzie
  tekst JSON nie jest konieczny.
- Użyj transportów `hal_serial`, `hal_uart`, MQTT, UDP lub modemu, aby
  wysłać wypisany JSON.

## Autor i licencja

Zarządzane źródła cJSON/cJSON_Utils pochodzą z upstreamowego `cJSON`,
autorstwa Dave'a Gamble'a i współtwórców, dystrybuowanego na licencji MIT.
Checkout zachowuje upstreamowy plik `third_party/cJSON/LICENSE`; dokładny
commit jest zapisany w `third_party/cjson_version.conf`.
