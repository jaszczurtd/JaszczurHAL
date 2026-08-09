# Board descriptors

This directory is the source of truth for JaszczurHAL target and physical
board definitions. `scripts/generate_board_config.py` validates the descriptors,
emits build-time CMake and C/C++ configuration below `.build`, and maintains the
tracked registry and source fallback used without a build-generated profile.

- `capabilities.json` assigns stable public capability bits.
- `targets/` describes MCU/ISA build targets.
- `profiles/` describes physical boards.
- `board.schema.json` is editor assistance only. The generator owns validation.

Applications select a stable `target` and `board` ID. They must not parse these
files at runtime.

Refresh the tracked artifacts after changing targets, profiles, capabilities,
or device roles:

```bash
python3 scripts/generate_board_config.py --boards-root boards --write-static
```

CI verifies the result with `--check-static`. Do not edit
`src/hal/generated/jh_board_registry.h` or
`src/hal/generated/jh_board_fallback_config.h` manually.
