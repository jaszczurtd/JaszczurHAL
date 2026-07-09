# Target registry

One JSON descriptor per **target family** consumed by the jh-vscode board
selector (`select-board`) and the project generator. This is **data only** - the
build recipes live in `cmake/targets/<target>.cmake` and the HAL backends in
`src/hal/impl/<target>/`.

## Descriptor shape

| Field | Meaning |
|---|---|
| `id` | Target family id (matches `JH_TARGET` and the `cmake/targets/<id>.cmake` recipe). |
| `displayName` | Human label shown in the board picker. |
| `toolchain` | jh-vscode toolchain kind (`cmake` for every dispatcher-based target). |
| `defaultBoard` | Board id used when the manifest sets `target` but not `board`. |
| `upload` | Default upload block for the family (`strategy` + strategy params, e.g. `openocd`). Overridable per project in the manifest. |
| `monitor` | Serial monitor transport hint. |
| `cache` | Family-level CMake cache entries merged for every board (e.g. `JH_TARGET`, `CMAKE_TOOLCHAIN_FILE`). |
| `boards[]` | Selectable variants: `{ id, displayName, cache }`. The board `cache` overlays the family `cache` (e.g. `ARDUINO_FQBN` for rp2040). |
| `status` | Optional; `"skeleton"` marks a family without a working HAL backend yet (listed but flagged). |

## Tokens

`cache` values may use `${jhRoot}` - the absolute path to the JaszczurHAL repo
root (this directory's grandparent). The consumer (select-board / generator)
resolves it while building the effective configuration. `${project}` and the
other manifest tokens are resolved later by the normal jh-vscode expansion.

## How a selection maps to a manifest

`select-board` persists only the active `target`/`board` pair in the
gitignored `.vscode/jaszczurhal.local.json`. It does not rewrite the tracked
project manifest.

At build/upload time, the active target/board pair is selected first:

```text
CLI --target/--board
  -> .vscode/jaszczurhal.local.json
  -> manifest target/board
  -> rp2040 + registry defaultBoard
```

The jh-vscode resolver then composes the effective configuration as:

```text
registry family defaults
  -> registry board cache
  -> base .vscode/jaszczurhal.project.json
  -> targetProfiles.<target>
  -> final active target/board pinning
```

CLI and local selection choose the active pair; they do not rewrite the tracked
manifest. Per-invocation CLI flags such as `--port` are applied by the action
after configuration resolution.

The resolver always pins `cmake.cache.JH_TARGET` to the active family. Adding a
board = one entry in `boards[]`; adding a family = a new `<id>.json` here + a
`cmake/targets/<id>.cmake` recipe + a HAL backend. The full user-facing project
workflow is documented in
[`doc/FwProjectWorkflow.md`](../../doc/FwProjectWorkflow.md).
