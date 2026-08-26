# HAL feature registry

The JSON fragments in this directory define the closed namespace of supported
`HAL_ENABLE_*` and `HAL_DISABLE_*` symbols and the target-independent part of
their dependency graph.

Run the generator after changing a fragment:

```bash
python3 scripts/sync_generated.py --write
```

The generated C and CMake artifacts are tracked so installed packages and
direct compiler builds do not require Python. CI verifies them through
`python3 scripts/sync_generated.py --check`.

Production C/C++ compilation includes
`src/hal/generated/jh_hal_features.h` through `hal_config.h`. RP, STM32, board
generation, link-signature generation, and `jh-vscode` use the generated CMake
table or the same registry model. Unconditional feature dependencies therefore
have one maintained source of truth.

Feature resolution keeps two sets:

- `requestedFeatures` contains the direct project, CMake, and command-line
  requests after normalization and de-duplication;
- `resolvedFeatures` contains `requestedFeatures` plus the sorted transitive
  registry closure.

Source and dependency selection use `resolvedFeatures`. Diagnostics retain
`requestedFeatures`, so an implied feature is not presented as a direct user
request. `jh_board_resolved.json` stores both sets and their digest. Its
compatibility field `features` is an alias of `resolvedFeatures`.

Optional `buildEffects` records keep additive build inputs beside the feature
that owns them:

- `featureSources` lists sources that every build must add when the feature is
  active, such as Unity or PubSubClient;
- `portableSources` lists target-independent implementations consumed by
  selective build systems; broad source inventories use the list for
  validation without adding the files twice;
- `dependencies` selects an existing managed source manifest. Supported names
  are `bearssl`, `littlefs`, and `sx126x`.

The generator validates and emits these records for CMake. ESP-IDF reads the
same registry model and combines portable sources with its target-owned source
map. Target adapters, board capabilities, flash layouts, and special firmware
images remain outside `buildEffects`.

Consumer inputs can be checked independently of the build resolver:

```bash
python3 scripts/generate_hal_features.py --lint --input-root .
```

Raw lint accepts presence-only definitions and `=1`. A value of `=0`, an
unknown symbol, or a direct request for a `derived` symbol is reported as a
configuration error. Feature definitions in `hal_project_config.h` must be
unconditional or use an `#ifndef` guard for the same symbol. CMake definition
lists are semicolon-separated scalar strings.

Effective lint reuses the `jh-vscode` target-profile and variant resolver,
ignores gitignored local board state, and writes deterministic requested,
resolved, digest, and provenance data:

```bash
python3 scripts/generate_hal_features.py \
  --lint --effective --input-root . \
  --resolution-output .build/effective-feature-resolution.json
```

Both raw and effective lint are strict: findings produce a non-zero exit code.
`--report-only` remains available for temporary migration audits, but is not the
normal CI invocation.

Standard `.vscode/jaszczurhal.project.json` files enumerate target, board, and
variant axes. A standalone `hal_project_config.h` containing at least one HAL
feature request contributes one axis-free direct context. Standalone headers
without requests and reference manifests stay in the raw-lint inventory without
creating synthetic configurations.

`config/effective-features-baseline.json` freezes the repository-wide effective
matrix digest. Registry tests map each record to a checked unique target/board/
request tuple for the production preprocessor and to a checked unique request
set for the generated CMake resolver.

## Rules outside registry v1

`hal_config.h` retains target, board, provider, capability, and tunable rules
that the v1 schema cannot express. Two retained rules also add feature symbols:

- `HAL_ENABLE_EEPROM` adds `HAL_ENABLE_I2C` only when
  `HAL_EEPROM_TYPE == EEPROM_TYPE_AT24C256`;
- `HAL_ENABLE_GPS` adds `HAL_ENABLE_UART` only when neither UART nor software
  serial was requested.

These conditional fallbacks run after the registry closure and are deliberately
outside `resolvedFeatures`. Consequently, the board feature hash establishes
equivalence of the registry-resolved set, not equivalence of every final macro
after the residual rules. Provider choices, board capabilities, target
constraints, and tunable validation likewise remain in `hal_config.h`.
