# Target and board profiles

JaszczurHAL selects hardware with two stable IDs:

```json
{
  "target": "rp2040",
  "board": "rp2040-zero"
}
```

A target identifies the MCU, ISA, toolchain, and build recipe. A board profile
identifies a physical board, its flash, exposed and reserved pins, on-board
devices, capabilities, and controlled build components. Application features
remain opt-in through `HAL_ENABLE_*`; a hardware capability never enables a
feature by itself.

## Source files

The versioned source of truth is `boards/`:

- `targets/<id>.json` describes an MCU/ISA target;
- `profiles/<id>.json` describes a physical board;
- `capabilities.json` assigns stable capability bits;
- `board.schema.json` provides editor assistance only;
- `scripts/generate_board_config.py` owns structural and semantic validation.

Descriptor IDs use kebab-case and must match their filenames. Unknown fields,
duplicate IDs, incompatible target/board pairs, invalid endpoints, unknown
capabilities or components, and output outside `.build` are hard errors.

## Descriptor model

Every descriptor contains `schemaVersion`, `kind`, `id`, `displayName`,
`description`, and `status`.

Target descriptors additionally define:

- `architecture`: vendor, family, SoC, ISA, and core count;
- `hal.targetSelector`;
- `build`: provider, controlled recipe, and provider platform when required;
- `gpio`: pin ID format, exact valid pins, and HAL encoding;
- `memory.regions`;
- `defaultBoard`;
- target-owned component IDs.

Board descriptors additionally define:

- `compatibleTargets` and `build.provider`;
- provider board ID where required;
- stable `hal.profileId`, selector, runtime name, and compatibility aliases;
- physical flash source and expected size;
- exposed pins, connector groups, reservations, and aliases;
- capabilities, board-owned devices, peripheral defaults, and components.

GPIO endpoints use an explicit domain:

```json
{ "domain": "soc-gpio", "id": 16 }
```

STM32 endpoints use symbolic IDs such as `PA5`. GPIO supplied by another chip
uses `component-gpio`, so it does not inflate the SoC GPIO namespace.

Reservations are `hard` when an application cannot use the pin and `soft` when
the pin has a board-owned function that an application can intentionally
drive. Application wiring, partition layout, USB identity, clock selection,
secrets, and WS2812 pixel order do not belong in a board descriptor.

Component IDs come from a finite registry shared by the generator and
`cmake/jh_board_components.cmake`. Every official build validates the
resolved component list against that registry: an unknown component, a
component that does not match the build provider, or two components claiming
the same exclusive slot fail the configure step. Recipes may condition source
integration on the exported `JH_BOARD_COMPONENT_<ID>` flags.

## Generation

Validate all tracked descriptors:

```bash
python3 scripts/generate_board_config.py \
  --boards-root boards \
  --validate-only
```

Generate one resolved profile:

```bash
python3 scripts/generate_board_config.py \
  --boards-root boards \
  --target rp2040 \
  --board rp2040-zero \
  --output-dir .build/generated/boards/rp2040/rp2040-zero \
  --feature HAL_ENABLE_RGB_LED
```

The deterministic output contains:

- `jh_board_config.cmake`;
- `jh_board_config.h`;
- `jh_board_registry.h`;
- `jh_board_resolved.json`;
- `jh_link_contract.h`;
- contract definition and reference translation units;
- `generation.d`.

Firmware never parses JSON. CMake runs the generator before importing Pico SDK
and uses the generated provider platform and board. `hal_board.h` consumes the
generated registry/config while preserving controlled compatibility aliases.

## Board-aware static libraries

Static libraries are separated by target and board:

```text
.build/static/<target>/<board>/
  libJaszczurHAL.a
  include/generated/
```

Build examples:

```bash
./scripts/build_rp_native_lib.sh \
  --target rp2040 \
  --board rp2040-plus-4mb

./scripts/build_stm32_lib.sh \
  --board nucleo-g474re

./scripts/build_stm32_lib.sh \
  --board nucleo-g474re-pim730
```

`nucleo-g474re` describes the Nucleo board alone. Projects using the external
PIM730/RM2 radio must select the experimental `nucleo-g474re-pim730` profile;
it owns the fixed CYW43 gSPI pins and exports the radio capabilities and
components required by network builds. The wiring and electrical constraints
are documented in [Connectivity](api/15_connectivity.md#cyw43-backend-configuration-and-lifecycle).

The archive defines:

```text
jh_board_contract_<target>_<board>_<featureHash>
```

`featureHash` is the first 12 hexadecimal characters of SHA-256 over
`hal.profileId` and the sorted, normalized `HAL_ENABLE_*=0/1` feature set.
Official firmware builds
always compile the generated reference translation unit. Linking an archive
for another target, board, or feature set therefore fails with an undefined
contract symbol.

The archive and its generated headers are one unit. Never copy or link
`libJaszczurHAL.a` without the matching `include/generated/` directory and
contract reference object.

## Adding RP2040-Zero

The existing `rp2040-zero` profile demonstrates the complete procedure:

1. Verify the manufacturer data and pinned Pico SDK board header.
2. Add `boards/profiles/rp2040-zero.json`.
3. Select target `rp2040`, provider board `waveshare_rp2040_zero`, and assert
   2 MB flash.
4. Describe exposed headers/pads and reserve GPIO16 softly for the status LED.
5. Describe the LED as addressable WS2812 with project-owned RGB/GRB order.
6. Run registry validation and generation below `.build`.
7. Inspect the generated CMake, header, and resolved JSON.
8. Add golden, negative, target/board, flash, and link-contract tests.
9. Select `target: rp2040` and `board: rp2040-zero` in the consumer manifest.

The generated profile exposes GPIO16 and WS2812 facts but deliberately does
not define `HAL_LED_BUILTIN` or a default pixel order.
