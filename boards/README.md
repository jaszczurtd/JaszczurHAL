# Board descriptors

This directory is the source of truth for JaszczurHAL target and physical
board definitions. `scripts/generate_board_config.py` validates the descriptors
and emits build-time CMake, C/C++ headers, and a normalized resolved profile
below `.build`.

- `capabilities.json` assigns stable public capability bits.
- `targets/` describes MCU/ISA build targets.
- `profiles/` describes physical boards.
- `board.schema.json` is editor assistance only. The generator owns validation.

Applications select a stable `target` and `board` ID. They must not parse these
files at runtime.
