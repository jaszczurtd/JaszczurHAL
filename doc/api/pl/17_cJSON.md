# cJSON

*Dostępne również [po angielsku](../en/17_cJSON.md).*

> **Część [Dokumentacji API JaszczurHAL](../../pl/JaszczurHAL_API.md)**

Obejmuje: dostarczane z JaszczurHAL biblioteki `cJSON` i `cJSON_Utils`,
włączane przez `HAL_ENABLE_CJSON`.

`cJSON` to niewielka biblioteka C do parsowania i generowania JSON-u.
Jej źródła są pobierane do `third_party/cJSON` w commicie wskazanym przez
`third_party/cjson_version.conf`. Warstwa integracyjna w
`src/hal/codecs/cjson/` włącza nagłówki i źródła upstreamu tylko wtedy, gdy
zdefiniowano `HAL_ENABLE_CJSON`, a publiczna ścieżka dołączania pozostaje stała.

Wersja dostarczana z projektem: `cJSON` 1.7.18.

Autor/licencja: projekt `cJSON` jest rozwijany przez Dave'a
Gamble'a i współtwórców oraz udostępniany na licencji MIT.

## Włączenie

Włącz moduł w `hal_project_config.h` lub definicją kompilatora:

```c
#pragma once

#define HAL_ENABLE_CJSON
```

Pliki należą do wspólnej listy źródeł frameworka, lecz bez
`HAL_ENABLE_CJSON` powstają z nich puste jednostki translacji. Publiczne
nagłówki są zabezpieczone tą samą flagą, dlatego musi być ona aktywna także
podczas kompilowania kodu korzystającego z symboli `cJSON_*`.

## Dołączanie

Bezpośrednie dołączenie, bezpieczne zarówno z C, jak i z C++:

```c
#include <hal/codecs/cjson/cJSON.h>
#include <hal/codecs/cjson/cJSON_Utils.h>
```

Jeżeli plik C++ korzysta już ze zbiorczego nagłówka narzędziowego, może
uzyskać dostęp do cJSON przez `tools.h`. Wymaga to `HAL_ENABLE_CJSON`:

```c
#include <tools.h>
```

`tools.h` dołącza także klasy narzędziowe C++, dlatego w plikach `.c` należy
korzystać bezpośrednio z nagłówków frameworku. `tools_c.h` nie udostępnia cJSON.

`JaszczurHAL.h` dołącza zbiorczy nagłówek HAL, ale nie `tools.h`. Kod, który
bezpośrednio korzysta z cJSON, musi więc dołączyć nagłówki frameworku albo,
w przypadku C++, `tools.h`.

## Zakres API

Podstawowe API `cJSON`:

| Kategoria | Typowe funkcje |
|---|---|
| Parsowanie | `cJSON_Parse`, `cJSON_ParseWithLength`, `cJSON_ParseWithOpts`, `cJSON_ParseWithLengthOpts` |
| Inspekcja | `cJSON_GetObjectItemCaseSensitive`, `cJSON_GetArrayItem`, `cJSON_GetArraySize`, `cJSON_IsString`, `cJSON_IsNumber`, `cJSON_IsBool`, `cJSON_IsObject`, `cJSON_IsArray` |
| Tworzenie | `cJSON_CreateObject`, `cJSON_CreateArray`, `cJSON_CreateString`, `cJSON_CreateNumber`, `cJSON_CreateBool`, `cJSON_CreateNull` |
| Dodawanie | `cJSON_AddStringToObject`, `cJSON_AddNumberToObject`, `cJSON_AddBoolToObject`, `cJSON_AddArrayToObject`, `cJSON_AddObjectToObject`, `cJSON_AddItemToArray`, `cJSON_AddItemToObject` |
| Aktualizacja | `cJSON_SetNumberValue`, `cJSON_SetValuestring`, `cJSON_ReplaceItemInObjectCaseSensitive`, `cJSON_DeleteItemFromObjectCaseSensitive` |
| Generowanie tekstu | `cJSON_Print`, `cJSON_PrintUnformatted`, `cJSON_PrintBuffered`, `cJSON_PrintPreallocated` |
| Zwalnianie | `cJSON_Delete`, `cJSON_free` |

`cJSON_Utils` dodaje pomocników dla JSON Pointer, JSON Patch, JSON Merge
Patch oraz sortowania obiektów:

| Obszar | Funkcje |
|---|---|
| JSON Pointer (RFC 6901) | `cJSONUtils_GetPointer`, `cJSONUtils_GetPointerCaseSensitive` |
| JSON Patch (RFC 6902) | `cJSONUtils_ApplyPatches`, `cJSONUtils_GeneratePatches`, `cJSONUtils_AddPatchToArray` |
| JSON Merge Patch (RFC 7386) | `cJSONUtils_MergePatch`, `cJSONUtils_GenerateMergePatch` |
| Sortowanie / ścieżki | `cJSONUtils_SortObject`, `cJSONUtils_FindPointerFromObjectTo` |

## Zarządzanie pamięcią

cJSON domyślnie wykorzystuje alokację dynamiczną.

Najważniejsze zasady:

- Drzewo zwrócone przez `cJSON_Parse*()` trzeba zwolnić przez
  `cJSON_Delete(root)`.
- Element zwrócony przez `cJSON_Create*()` trzeba zwolnić samodzielnie, dopóki
  nie zostanie dodany do tablicy lub obiektu. Po wywołaniu
  `cJSON_AddItemToArray()` albo `cJSON_AddItemToObject()` zostanie zwolniony
  razem z elementem nadrzędnym.
- `cJSON_AddStringToObject()` i podobne funkcje pomocnicze tworzą i
  dołączają nowy element podrzędny, za który od tego momentu odpowiada obiekt
  nadrzędny.
- Funkcje `cJSON_Print*()` zwracające `char *` alokują tekst. Zwolnij go
  przez `cJSON_free(text)`.
- `cJSON_PrintPreallocated()` zapisuje dane do bufora dostarczonego przez
  wywołującego i nie przejmuje zarządzania jego pamięcią. Bufor musi mieć
  niewielki zapas; dokumentacja biblioteki zaleca około 5 bajtów więcej niż
  przewidywany rozmiar wyniku.
- `cJSONUtils_MergePatch(target, patch)` może zwrócić inny wskaźnik niż
  `target`. Zawsze przypisuj zwróconą wartość z powrotem do wskaźnika korzenia
  drzewa.

Niestandardowe funkcje alokatora można zarejestrować przez
`cJSON_InitHooks()`. Należy to zrobić raz podczas uruchamiania programu,
zanim powstaną jakiekolwiek obiekty JSON. Hooki obowiązują w całym procesie,
nie tylko w pojedynczym dokumencie.

## Thread safety

Dokumenty cJSON można przetwarzać niezależnie, jeśli każde zadanie lub rdzeń
korzysta z własnego drzewa. Dostęp do drzewa współdzielonego trzeba
synchronizować po stronie aplikacji; JaszczurHAL nie chroni operacji cJSON
mutexem.

Uważaj na następujący stan globalny:

- `cJSON_InitHooks()` zmienia globalne funkcje alokatora. Wywołaj ją raz
  podczas uruchamiania programu, przed rozpoczęciem współbieżnej pracy z JSON-em.
- `cJSON_GetErrorPtr()` odczytuje współdzielony stan błędu parsera. W kodzie
  współbieżnym lepiej użyć `cJSON_ParseWithOpts(..., &end, ...)`, które zapisuje
  pozycję końca danych lub błędu we wskaźniku dostarczonym przez wywołującego.

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

Użyj `cJSON_PrintPreallocated()`, gdy znasz maksymalny rozmiar wyniku i chcesz
uniknąć dynamicznego przydzielania bufora na generowany tekst.

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

`NONULL(x)` to makro pomocnicze JaszczurHAL z `hal_system.h`, a nie część API
cJSON. Przydaje się w krótkich funkcjach budujących JSON, które zwalniają
wszystkie zasoby w jednym miejscu oznaczonym etykietą `error:`. Jeżeli wynikiem
`x` jest `NULL`, makro przechodzi bezpośrednio do tej etykiety.

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

Za zwolnienie zwróconego `char *` odpowiada wywołujący. Po wysłaniu lub
zapisaniu danych użyj `cJSON_free(json)`. Wynik `NULL` oznacza, że podczas
tworzenia drzewa albo generowania końcowego JSON-u nie udało się przydzielić
pamięci.

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

JSON Pointer używa ścieżek rozdzielanych znakiem `/`. W nazwach kluczy znaki
`~` i `/` trzeba zakodować odpowiednio jako `~0` i `~1`.

## Uwagi dotyczące systemów wbudowanych

- Zawsze sprawdzaj, czy zwrócony wskaźnik nie jest równy `NULL`. Kod dla
  systemu wbudowanego musi poprawnie obsłużyć brak pamięci.
- Preferuj `cJSON_GetObjectItemCaseSensitive()` podczas parsowania danych
  konfiguracyjnych. Zapobiega to dopasowaniu klucza zapisanego z inną
  wielkością liter.
- Liczby są przechowywane jako `double`; dodatkowo zachowywana jest ich
  podręczna wartość całkowita. Przy przepisywaniu ich do typów aplikacji
  wykonuj jawne rzutowanie.
- Ograniczaj rozmiar dokumentów JSON. Parser i generator tekstu przydzielają
  pamięć proporcjonalnie do drzewa JSON oraz wyniku.
- Preferuj `cJSON_PrintPreallocated()` dla wiadomości telemetrycznych i
  statusowych o znanym maksymalnym rozmiarze.
- `cJSON_Minify()` modyfikuje bufor wejściowy w miejscu. Nie przekazuj do niej
  literałów tekstowych ani buforów znajdujących się w pamięci flash lub ROM.
- Domyślny `CJSON_NESTING_LIMIT` wynosi 1000. W przypadku małych MCU warto
  obniżyć go za pomocą definicji kompilatora, jeśli urządzenie może otrzymywać
  niezaufane dane JSON.
- Podczas generowania JSON Patch `cJSON_Utils` może sortować i modyfikować
  obiekty wejściowe, zgodnie z dokumentacją biblioteki. Jeśli ich kolejność
  i treść mają pozostać bez zmian, najpierw utwórz kopie dokumentów.

## Przechowywanie i transport

Samo cJSON działa wyłącznie w RAM-ie. Do zapisania lub przesłania tekstu użyj
odpowiedniego modułu HAL:

- Użyj `hal_littlefs` dla plików JSON na LittleFS RP2040.
- Użyj `hal_kv` dla małych skalarnych wartości konfiguracyjnych, gdzie
  tekst JSON nie jest konieczny.
- Użyj `hal_serial`, `hal_uart`, MQTT, UDP lub modemu, aby wysłać wygenerowany
  tekst JSON.

## Autor i licencja

Źródła cJSON/cJSON_Utils dostarczane z projektem pochodzą z projektu upstream
`cJSON`, którego autorami są Dave Gamble i współtwórcy. Biblioteka jest
udostępniana na licencji MIT. Repozytorium zawiera oryginalny plik
`third_party/cJSON/LICENSE`, a dokładny commit zapisano w
`third_party/cjson_version.conf`.
