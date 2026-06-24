# cJSON

> **Part of [JaszczurHAL API Reference](../JaszczurHAL_API.md)**

Covers: bundled `cJSON` and `cJSON_Utils` enabled by `HAL_ENABLE_CJSON`.

`cJSON` is a small C JSON parser/generator bundled in
`src/hal/impl/shared/frameworks/cjson/`. It is not a HAL wrapper and does not
abstract hardware. JaszczurHAL only gates the upstream headers/sources behind
`HAL_ENABLE_CJSON` and makes them available through the normal include path.

Bundled version: `cJSON` 1.7.18.

Author/license: upstream `cJSON` is authored by Dave Gamble and contributors
and distributed under the MIT license.

## Enable

Enable the module in `hal_project_config.h` or with a compiler definition:

```c
#pragma once

#define HAL_ENABLE_CJSON
```

The source files are part of the shared framework source list, but their contents
compile to nothing unless `HAL_ENABLE_CJSON` is defined. The public headers are
also guarded, so code that uses `cJSON_*` symbols must be compiled with the same
flag.

## Include

Direct include, safe from both C and C++:

```c
#include <hal/impl/shared/frameworks/cjson/cJSON.h>
#include <hal/impl/shared/frameworks/cjson/cJSON_Utils.h>
```

For C++ files that already use the utility aggregator, `tools.h` also exposes
cJSON when `HAL_ENABLE_CJSON` is defined:

```c
#include <tools.h>
```

`tools.h` includes C++ utility classes, so prefer the direct framework includes
from `.c` files. `tools_c.h` does not re-export cJSON.

`JaszczurHAL.h` includes the HAL umbrella, not the utility aggregator, so include
the framework headers, or `tools.h` from C++ files, where cJSON is used directly.

## API Surface

Core `cJSON` API:

| Category | Common functions |
|---|---|
| Parse | `cJSON_Parse`, `cJSON_ParseWithLength`, `cJSON_ParseWithOpts`, `cJSON_ParseWithLengthOpts` |
| Inspect | `cJSON_GetObjectItemCaseSensitive`, `cJSON_GetArrayItem`, `cJSON_GetArraySize`, `cJSON_IsString`, `cJSON_IsNumber`, `cJSON_IsBool`, `cJSON_IsObject`, `cJSON_IsArray` |
| Create | `cJSON_CreateObject`, `cJSON_CreateArray`, `cJSON_CreateString`, `cJSON_CreateNumber`, `cJSON_CreateBool`, `cJSON_CreateNull` |
| Add | `cJSON_AddStringToObject`, `cJSON_AddNumberToObject`, `cJSON_AddBoolToObject`, `cJSON_AddArrayToObject`, `cJSON_AddObjectToObject`, `cJSON_AddItemToArray`, `cJSON_AddItemToObject` |
| Update | `cJSON_SetNumberValue`, `cJSON_SetValuestring`, `cJSON_ReplaceItemInObjectCaseSensitive`, `cJSON_DeleteItemFromObjectCaseSensitive` |
| Print | `cJSON_Print`, `cJSON_PrintUnformatted`, `cJSON_PrintBuffered`, `cJSON_PrintPreallocated` |
| Free | `cJSON_Delete`, `cJSON_free` |

`cJSON_Utils` adds helpers for JSON Pointer, JSON Patch, JSON Merge Patch, and
object sorting:

| Feature | Functions |
|---|---|
| JSON Pointer (RFC 6901) | `cJSONUtils_GetPointer`, `cJSONUtils_GetPointerCaseSensitive` |
| JSON Patch (RFC 6902) | `cJSONUtils_ApplyPatches`, `cJSONUtils_GeneratePatches`, `cJSONUtils_AddPatchToArray` |
| JSON Merge Patch (RFC 7386) | `cJSONUtils_MergePatch`, `cJSONUtils_GenerateMergePatch` |
| Sorting / paths | `cJSONUtils_SortObject`, `cJSONUtils_FindPointerFromObjectTo` |

## Memory Ownership

cJSON uses dynamic allocation by default.

Rules that matter most:

- `cJSON_Parse*()` returns a tree owned by the caller. Free it with
  `cJSON_Delete(root)`.
- `cJSON_Create*()` returns an item owned by the caller until it is added to an
  array/object. After `cJSON_AddItemToArray()` or `cJSON_AddItemToObject()`,
  the parent owns the item.
- `cJSON_AddStringToObject()` and similar helper functions create and attach a
  new child. The parent object owns it.
- `cJSON_Print*()` functions that return `char *` allocate text. Free it with
  `cJSON_free(text)`.
- `cJSON_PrintPreallocated()` writes into a caller-provided buffer. The caller
  owns the buffer and must provide a little extra space; upstream recommends
  allocating about 5 bytes more than the expected output.
- `cJSONUtils_MergePatch(target, patch)` may return a different pointer than
  `target`. Always assign the return value back to your root pointer.

Custom allocation hooks can be installed with `cJSON_InitHooks()`. Do this once
at startup, before any JSON objects are created. The hooks are global process
state, not per-document state.

## Thread Safety

cJSON documents are independent as long as each task/core owns its own tree or
external locking protects shared trees. JaszczurHAL does not add a mutex around
cJSON operations.

Important shared/global state:

- `cJSON_InitHooks()` changes global allocator hooks. Call it once during
  startup, before concurrent JSON use.
- `cJSON_GetErrorPtr()` reads parser error state. In concurrent code prefer
  `cJSON_ParseWithOpts(..., &end, ...)`, because it returns the parse end/error
  pointer through caller-owned storage.

## Example: Parse Configuration

```c
#include <hal/impl/shared/frameworks/cjson/cJSON.h>
#include <hal/hal_serial.h>
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

Input:

```json
{"ssid":"lab-net","sample_ms":1000,"enabled":true}
```

## Example: Build And Print JSON

Use `cJSON_PrintPreallocated()` when the output has a bounded size and you want
to avoid allocating a print buffer.

```c
#include <hal/impl/shared/frameworks/cjson/cJSON.h>
#include <hal/hal_serial.h>
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

For dynamically sized output, use `cJSON_PrintUnformatted()` and free the result:

```c
char *text = cJSON_PrintUnformatted(root);
if (text != NULL) {
    hal_serial_println(text);
    cJSON_free(text);
}
```

## Example: Build JSON With NONULL

`NONULL(x)` is a JaszczurHAL helper from `hal_system.h`, not a cJSON API. It is
useful for compact builders that use one `error:` cleanup label. If `x` evaluates
to `NULL`, the macro jumps to that label.

This pattern works well with `cJSON_Add*ToObject()` helpers and
`cJSON_PrintUnformatted()`, because both return pointers that must be checked.

```c
#include <hal/impl/shared/frameworks/cjson/cJSON.h>
#include <hal/hal_system.h>
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

The returned `char *` is owned by the caller. Free it with `cJSON_free(json)`
after sending or storing it. A `NULL` return means allocation failed while
creating the tree or printing the final JSON.

## Example: JSON Pointer And Merge Patch

```c
#include <hal/impl/shared/frameworks/cjson/cJSON.h>
#include <hal/impl/shared/frameworks/cjson/cJSON_Utils.h>
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

JSON Pointer uses `/`-separated paths. For object keys containing `~` or `/`,
escape them as `~0` and `~1`.

## Embedded Notes

- Always check returned pointers for `NULL`; allocation failure is a normal
  embedded failure mode.
- Prefer `cJSON_GetObjectItemCaseSensitive()` when parsing configuration data.
  It avoids surprising matches on keys with different case.
- Numbers are stored as `double` plus an integer cache. Cast deliberately at the
  edge of your application.
- Keep documents small. Parsing and printing allocate memory proportional to the
  JSON tree and output text.
- Prefer `cJSON_PrintPreallocated()` for telemetry/status messages with a known
  upper bound.
- `cJSON_Minify()` modifies its input buffer in place; do not pass string
  literals or flash/ROM-backed buffers.
- The default `CJSON_NESTING_LIMIT` is 1000. For small MCUs, consider lowering
  it with a compile definition if untrusted JSON can arrive from outside the
  device.
- `cJSON_Utils` patch generation may sort and mutate input objects as noted by
  upstream comments. Duplicate documents first if original ordering/content must
  remain untouched.

## Storage And Transport

cJSON itself is RAM-only. Persist or move the text through the appropriate HAL
module:

- Use `hal_littlefs` for JSON files on RP2040 LittleFS.
- Use `hal_kv` for small scalar configuration values where JSON text is not
  necessary.
- Use `hal_serial`, `hal_uart`, MQTT, UDP, or modem transports to send printed
  JSON.

## Author And License

The bundled cJSON/cJSON_Utils sources are from upstream `cJSON`, authored by
Dave Gamble and contributors, and distributed under the MIT license. See the
source headers in `src/hal/impl/shared/frameworks/cjson/` and the top-level
README third-party notices.
