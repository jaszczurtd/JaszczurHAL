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

## Board-owned devices

Every entry under `devices` uses a camelCase ID and declares a `kind`. Devices
that own one line - `gpio`, `component-gpio`, and `addressable` - carry a single
`endpoint`.

A device wired across several pins on a bus uses `kind: "bus-device"` and names
a `role` from the generator's device-role registry. The role declares which
signals and which typed attributes the descriptor must supply, so a profile
cannot ship a partially described device:

```json
"loraRadio": {
  "kind": "bus-device",
  "role": "sx1262-radio",
  "bus": { "kind": "spi", "index": 1 },
  "signals": {
    "sck": { "domain": "soc-gpio", "id": 14 },
    "cs": { "domain": "soc-gpio", "id": 13 },
    "busy": { "domain": "soc-gpio", "id": 18 },
    "dio1": { "domain": "soc-gpio", "id": 16 }
  },
  "attributes": {
    "maxSpiClockHz": 16000000,
    "regulator": "dcdc",
    "rfSwitchMode": "dio2"
  }
}
```

The generator enforces that each role appears at most once per board, that no
two signals of one device share a pin, that every `soc-gpio` signal is covered
by a `hard` reservation, and that numeric attributes stay inside their declared
type and ordering. Signals and attributes may be gated on an enum attribute:
they become required when the gate selects them and are rejected otherwise, so
`rfSwitchMode: "dio2"` forbids the GPIO switch lines and levels.

Each role materializes a fixed macro surface in `jh_board_config.h` under its
own prefix, plus `HAL_BOARD_DEVICE_PIN_NONE` for absent optional signals:

```c
#define HAL_BOARD_LORA_RADIO_PRESENT 1
#define HAL_BOARD_LORA_RADIO_SPI_BUS 1u
#define HAL_BOARD_LORA_RADIO_PIN_CS 13u
#define HAL_BOARD_LORA_RADIO_PIN_RF_SWITCH_A HAL_BOARD_DEVICE_PIN_NONE
#define HAL_BOARD_LORA_RADIO_MAX_SPI_CLOCK_HZ UINT32_C(16000000)
#define HAL_BOARD_LORA_RADIO_REGULATOR_IS_DCDC 1
```

Boards without the device still define `<PREFIX>_PRESENT 0`, so a HAL module
can resolve board-supplied configuration at compile time. Enum attributes emit
one `_IS_<VALUE>` flag per allowed value and a `_NAME` string; STM32 symbolic
pins are encoded into the same integer pin IDs the HAL consumes. The full
descriptor also reaches `jh_board_resolved.json` unchanged for tooling.

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
  --requested-feature HAL_ENABLE_RGB_LED
```

`--feature` remains a compatibility spelling for `--requested-feature`.

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
`jh_board_resolved.json` records the direct `requestedFeatures`, the transitive
registry `resolvedFeatures`, `resolvedFeaturesDigest`, and the board/provider
`boardCompileDefinitions`. The retained `features` field is an alias of
`resolvedFeatures`. Generated CMake exports the same feature values as
`JH_BOARD_REQUESTED_FEATURES`, `JH_BOARD_RESOLVED_FEATURES`, and
`JH_BOARD_RESOLVED_FEATURES_DIGEST`, and exports the provider definitions as
`JH_BOARD_COMPILE_DEFINITIONS`. `jh_board_config.h` materializes those provider
definitions as preprocessor macros so a direct compiler consumer receives the
same backend, bus, and pin contract without running CMake or Python.

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
components required by network builds. Its generated board header also owns the
CYW43 backend, gSPI bus, stack, and pin definitions; direct compiler consumers
must not duplicate those definitions with command-line `-D` options. The wiring
and electrical constraints are documented in
[Connectivity](api/15_connectivity.md#cyw43-backend-configuration-and-lifecycle).
Pico W and the PIM730 profile also declare the lifecycle-owned
`bluetooth-controller` capability and feature-gated `btstack-ble` component.
Enabling `HAL_ENABLE_BLE` compiles that component; the physical capability
alone never enables Bluetooth. See the [Bluetooth API](api/20_bluetooth.md).

The archive defines:

```text
jh_board_contract_<target>_<board>_<featureHash>
```

`featureHash` is the first 12 hexadecimal characters of SHA-256 over
`hal.profileId` followed by the sorted registry `resolvedFeatures`, serialized
as `HAL_ENABLE_*=1` or `HAL_DISABLE_*=1`. Bare feature names and `=1` therefore
produce the same hash; the generator rejects `=0`, unknown features, derived
feature requests, and other explicit feature values. Two different requested
sets that produce the same closure have the same feature hash and link contract,
while `requestedFeatures` still preserves their diagnostic difference.

Official firmware builds always compile the generated reference translation
unit. Linking an archive for another target, board, or resolved feature set
therefore fails with an undefined contract symbol. For GCC and Clang, the
reference is rooted through a generated `constructor, used` function. The
constructor array is retained by the supported linker scripts, so the contract
remains effective when function/data sections and `--gc-sections` are enabled.

The archive and its generated headers are one unit. Never copy or link
`libJaszczurHAL.a` without the matching `include/generated/` directory and
contract reference translation unit.

Two conditional compatibility rules remain outside the v1 registry closure:
AT24C256 EEPROM can add I2C, and GPS can select UART when no serial transport
was requested. They run in `hal_config.h` and do not participate in feature-hash
equivalence. The hash compares the registry-resolved set, not every macro added
later by those residual rules.

## Installed package

Install a configured RP or STM32 static build with CMake:

```bash
cmake --install .build/static/<target>/<board> \
  --prefix .build/install/<target>/<board>
```

The installed unit contains:

```text
include/
  hal/generated/jh_hal_features.h
  generated/
    jh_board_config.h
    jh_board_registry.h
    jh_link_contract.h
lib/
  libJaszczurHAL.a
share/JaszczurHAL/generated/
  jh_link_contract_reference.c
  jh_board_resolved.json
```

The rest of the public headers are installed below `include/` as usual. After
installation, a matching compiler can compile project sources using the direct
requests from `jh_board_resolved.json`; `hal_config.h` applies the tracked
generated closure. Compile `jh_link_contract_reference.c` into the application
and link it with the matching archive. This consumer compile/link path does not
invoke Python. Target SDK libraries, startup files, linker scripts, and normal
toolchain flags are still required by the selected platform.

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
