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

The current profile inventory comes from `boards/profiles/*.json`; list its
stable IDs with
`python3 scripts/generate_board_config.py --boards-root boards --list boards`.
The ESP32-S3 target provides its delivered core/peripheral backend set and the
Phase 3 native connectivity/service graph. The build generator validates target
compatibility, flash size, pins, components, and feature rules before
toolchain import. The same descriptors generate the source fallback, so board
names and compile-time facts stay identical without a build-generated config.

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

- `architecture`: vendor, family, SoC, ISA, core count, public MCU/subtype/CPU
  names, FPU presence, and the runtime backend name;
- `hal.targetSelector`;
- `build`: provider, controlled recipe, and provider platform or `idfTarget`
  when required;
- `gpio`: pin ID format, exact valid pins, optional pin traits, and HAL
  encoding;
- `memory.regions` plus `memory.ramUsableBytes`; total RAM is generated from
  every RAM region, while usable RAM describes the region normally exposed by
  the default application linker;
- `defaultBoard`;
- optional `sourceFallbackBoard`, used only when source-level selection may
  safely choose a board without the build generator;
- target-owned component IDs.
- optional `requiredFeatures`, added to the effective set before its digest and
  feature hash are calculated;
- optional `supportedFeatures`, a closed target-specific allowlist enforced by
  production runners after transitive resolution. It must contain every
  required feature.

The resolved `jh_board_config.h` projects target descriptors into
`HAL_TARGET_*` facts and board descriptors into `HAL_BOARD_*` facts.
`hal_system_get_current_architecture()` consumes those generated target facts
instead of maintaining a second MCU/ISA/memory table in backend source. Total
flash remains a board fact because boards for one target may carry different
flash devices.

Board descriptors additionally define:

- `compatibleTargets` and `build.provider`;
- provider board ID where required;
- stable `hal.profileId`, selector, compatibility aliases, and optional
  provider autodetection selectors; the runtime name is always the board `id`;
- physical flash source and expected size, plus fitted PSRAM when present;
- optional `programming` transport, fixed programmer USB VID/PID, and reset/
  boot mechanism used for safe host-side device selection;
- exposed pins, connector groups, reservations, and aliases;
- capabilities, board-owned devices, peripheral defaults, and components.

The Waveshare ESP32-S3-Zero describes its native USB Serial/JTAG programmer as:

```json
"programming": {
  "transport": "usb-serial-jtag",
  "usb": { "vid": 12346, "pid": 4097 },
  "reset": "usb-serial-jtag-control-lines",
  "boot": "usb-serial-jtag-control-lines"
}
```

The decimal USB values are `303a:1001` in the usual hexadecimal display.
`jh-vscode` derives its identity verifier from these board facts; manifests do
not duplicate them. The Phase 1 hardware closure verified this programming
identity, three complete three-image flashes, ESP32-S3/two-core detection, 4 MiB
physical flash, initialized 2 MiB Quad PSRAM, and serial-monitor reconnect on the
SKU 25081 board. Phase 2 adds generated GPIO accessibility/reservation masks
consumed by the ESP32-S3 GPIO, ADC, UART, I2C, and SPI backends. Its physical
fixture subsequently passed both application cores, GPIO/IRQ, ADC, UART
loopback, I2C master, SPI master, GPTimer, USB Serial/JTAG RX/TX, and
system/synchronization checks on that board. The target and board profile are
therefore marked `supported`.

GPIO endpoints use an explicit domain:

```json
{ "domain": "soc-gpio", "id": 16 }
```

STM32 endpoints use symbolic IDs such as `PA5`. GPIO supplied by another chip
uses `component-gpio`, so it does not inflate the SoC GPIO namespace.

Reservations are `hard` when an application cannot use the pin and `soft` when
the pin has a board-owned function that an application can intentionally
drive. Application wiring, partition layout, firmware-defined USB product
identity, clock selection, secrets, and WS2812 pixel order do not belong in a
board descriptor. A fixed USB identity of the board's programming transport is
a physical board fact and belongs under `programming.usb`.

A composite profile must preserve the base board's physical devices, aliases,
and public HAL definitions. Do not remove a built-in device such as
`HAL_LED_BUILTIN` merely to reuse its pin for an attached module: the original
device remains electrically connected and can load or toggle the shared line
even when the overlap looks harmless. Select non-conflicting wiring instead.
Intentional PCB rework, such as opening a solder bridge, requires a distinct
profile whose description states the physical modification.

## Board-owned devices

Every entry under `devices` uses a camelCase ID and declares a `kind`. Devices
that own one line - `gpio`, `component-gpio`, and `addressable` - carry a single
`endpoint`.

A device wired across several pins on a bus uses `kind: "bus-device"` and names
a `role` from the generator's device-role registry. The role declares which
signals and which typed attributes the descriptor must supply, so a profile
cannot ship a partially described device. This abridged shape illustrates the
naming; use the complete tracked `rp2040-lora-lf` profile as the authoritative
SX1262 example:

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
`rfSwitchMode: "dio2-single-gpio"` models boards that enable the SX1262 DIO2
RF-switch control and also require one external front-end control line.

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

Component IDs, providers, and exclusive slots come from the authoritative
`config/tooling/board_components.json` model. The board generator consumes
it directly and writes the CMake projection included by
`cmake/jh_board_components.cmake`. Every official build validates the resolved
component list against that registry: an unknown component, a component that
does not match the build provider, or two components claiming the same
exclusive slot fail the configure step. Recipes may condition source
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

Refresh or verify all tracked generated artifacts, including the source-level
board projections:

```bash
python3 scripts/sync_generated.py --write
python3 scripts/sync_generated.py --check
```

These commands materialize the public enum/capability registry and the complete
fallback configuration directly from the descriptors, plus the CMake
board-component registry from `config/tooling/board_components.json`. The
tracked C header is the only physical `jh_board_registry.h`; per-build output
never duplicates it.

The deterministic output contains:

- `jh_board_config.cmake`;
- `jh_board_config.h`;
- `jh_board_resolved.json`;
- `jh_link_contract.h`;
- link-signature definition and reference translation units;
- `generation.d`.

Firmware never parses JSON. CMake runs the generator before importing Pico SDK
and uses the generated provider platform and board. `hal_board.h` always uses
the tracked registry, then consumes the build-generated board config when it is
available or the tracked generated fallback otherwise.
`jh_board_resolved.json` records the direct `requestedFeatures`, the transitive
registry `resolvedFeatures`, their `featureProvenance`,
`resolvedFeaturesDigest`, and the board/provider
`boardCompileDefinitions`. The retained `features` field is an alias of
`resolvedFeatures`. Generated CMake exports the same feature values as
`JH_BOARD_REQUESTED_FEATURES`, `JH_BOARD_RESOLVED_FEATURES`, and
`JH_BOARD_RESOLVED_FEATURES_DIGEST`, and exports the provider definitions as
`JH_BOARD_COMPILE_DEFINITIONS`. `jh_board_config.h` materializes those provider
definitions as preprocessor macros so a direct compiler consumer receives the
same backend, bus, and pin configuration without running CMake or Python.

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
PIM730/RM2 radio must select the supported `nucleo-g474re-pim730` profile; it
owns the fixed CYW43 gSPI pins and exports the radio capabilities and components
required by network builds. Its generated board header also owns the CYW43
backend, gSPI bus, stack, and pin definitions; direct compiler consumers must
not duplicate those definitions with command-line `-D` options. The wiring and
electrical constraints are documented in
[Connectivity](api/15_connectivity.md#cyw43-backend-configuration-and-lifecycle).
The `picow`, `pico2w`, `pico-rm2`, and `nucleo-g474re-pim730` profiles also
declare the lifecycle-owned `bluetooth-controller` capability and
feature-gated `btstack-host` component. Enabling `HAL_ENABLE_BLE` compiles
that component; the physical capability alone never enables Bluetooth. See the
[Bluetooth API](api/20_bluetooth.md).

The experimental `rp2040-lora-lf` profile describes Waveshare SKU 26592. It
uses the existing `rp2040` target and Pico SDK `pico` board definition, reserves
the integrated SX1262 wiring, exports `sx126x-radio` as a feature-gated
component, and declares `HAL_BOARD_CAP_SX1262_RADIO`. Its checked-in electrical
facts include SPI1 at a safe default 8 MHz, a strict sub-18-MHz ceiling, the
conservative 410-450 MHz LF range from the manufacturer wiki, DCDC regulation,
XTAL oscillator mode and combined DIO2 plus GPIO17 antenna-path control. The
`hal_lora_radio` lifecycle publishes the declared radio capability at runtime.

The experimental `pico-core1262-hf` and
`nucleo-g474re-core1262-hf` profiles describe fixed project fixtures built from
a base board and an external Waveshare Core1262-HF. They reserve the complete
SPI/control/RF-switch wiring, declare `loraRadio`, and export both
`external-radio-frontend` and `sx1262-radio`. The Nucleo profile uses SPI2 on
PB13/PB14/PB15 and deliberately preserves LD2 plus `HAL_LED_BUILTIN` on PA5.
Both fixtures passed no-transmit CAD/RSSI/calibration probes and bidirectional
OTA tests, but remain experimental because jumper-wire assembly and one tested
host of each type are not equivalent to a stable carrier design.

Different Core1262 wiring uses the plain `pico` or `nucleo-g474re` profile and
an explicit application descriptor. It must not select a composite profile
whose fixed pin configuration does not match the physical assembly.

The archive defines:

```text
jh_board_contract_<target>_<board>_<featureHash>
```

`featureHash` is the first 12 hexadecimal characters of SHA-256 over
`hal.profileId` followed by the sorted registry `resolvedFeatures`, serialized
as `HAL_ENABLE_*=1` or `HAL_DISABLE_*=1`. Bare feature names and `=1` therefore
produce the same hash; the generator rejects `=0`, unknown features, derived
feature requests, and other explicit feature values. Two different requested
sets that produce the same closure have the same feature hash and link signature,
while `requestedFeatures` still preserves their diagnostic difference.

Official firmware builds always compile the generated reference translation
unit. Linking an archive for another target, board, or resolved feature set
therefore fails with an undefined compatibility symbol. For GCC and Clang, the
reference is rooted through a generated `constructor, used` function. The
constructor array is retained by the supported linker scripts, so the signature
remains effective when function/data sections and `--gc-sections` are enabled.

The archive and its generated headers are one unit. Never copy or link
`libJaszczurHAL.a` without the matching `include/generated/` directory and
link-signature reference translation unit.

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
  hal/generated/
    jh_hal_features.h
    jh_board_registry.h
    jh_board_fallback_config.h
  generated/
    jh_board_config.h
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
8. Add golden, negative, target/board, flash, and link-signature tests.
9. Select `target: rp2040` and `board: rp2040-zero` in the consumer manifest.

The generated profile exposes GPIO16 and WS2812 facts but deliberately does
not define `HAL_LED_BUILTIN` or a default pixel order.
