# Board descriptors

This directory is the source of truth for JaszczurHAL target and physical
board definitions. `jh board validate` validates the descriptors
([jh README](../tools/jh/README.md#board-registry)).
`jh board config` writes the CMake and C/C++ configuration of one build below
`.build` from the validated registry, and `jh generate boards` maintains the
stored registry and the source fallback used without a build-generated
profile.

- `capabilities.json` assigns stable public capability bits.
- `targets/` describes MCU/ISA build targets.
- `profiles/` describes physical boards.
- `board.schema.json` is editor assistance only. `jh` owns validation.

Applications select a stable `target` and `board` ID. They must not parse these
files at runtime.

Refresh the tracked artifacts after changing targets, profiles, capabilities,
or device roles:

```bash
python3 scripts/sync_generated.py --write
```

CI verifies all tracked generated artifacts with
`python3 scripts/sync_generated.py --check`. Do not edit
`src/hal/generated/jh_board_registry.h` or
`src/hal/generated/jh_board_fallback_config.h` manually.
