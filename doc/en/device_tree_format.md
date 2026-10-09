# Device tree format, version 1

This specification defines the inputs and generated interface for project hardware. `scripts/generate_hardware_config.py` resolves an assembly and writes both headers; the build does not call it yet, so a JSON file does not configure today's build.

## Files and validation

One firmware application, including a subproject, owns one `device_tree.json`. A configuration-only directory for a standalone library can own the same file without firmware sources. Files neither include nor inherit another application's wiring. Identical wiring in independent projects is intentionally independent.

| File | Content |
|---|---|
| [device_tree.schema.json](../../config/hardware/device_tree.schema.json) | project nodes and assemblies |
| [binding.schema.json](../../config/hardware/binding.schema.json) | HAL and project component types |
| [clock_tree.schema.json](../../config/hardware/clock_tree.schema.json) | closed clock-tree list for one target |
| [clock_sources.schema.json](../../config/hardware/clock_sources.schema.json) | physical board `clockSources` object |
| [clock_inputs.schema.json](../../config/hardware/clock_inputs.schema.json) | target `clockInputs` object |
| [common.schema.json](../../config/hardware/common.schema.json) | shared identifiers, endpoints and scalar values |
| [cases.json](../../tests/fixtures/device_tree/cases.json) | valid and invalid format examples, with expected diagnostics |

Schemas use JSON Schema 2020-12. Input is UTF-8 JSON with unique object keys, finite numbers, `schemaVersion: 1`, and no unknown fields. A nonzero decimal number that binary64 would turn into infinity or zero fails when read. Boolean values do not count as integers. `$schema` is optional editor metadata, never a fetched dependency. Other versions fail rather than falling back to a build without JSON.

Schema validation checks structure. The resolver, `scripts/hardware_resolver.py`, additionally checks references, binding types, board/target compatibility, ownership, resource use and supported clocks. A `semantic` case in the example index deliberately passes structural validation and names the resolver's rejection.

## Nodes and assemblies

Root fields are `schemaVersion`, optional `$schema` and `bindings`, `nodes`, and nonempty `assemblies`. Root `nodes` defines names, `compatible`, common `properties`, and optional `children`. Bus, controller, address and GPIO assignments belong to assembly nodes. Assemblies do not inherit each other.

Each assembly requires `target`, `board`, `clock.tree`, `statusLed.node` and `nodes`. Optional fields are `buses`, `nets`, `releaseReservations` and `providers`. Target and board IDs come only from the existing board registry; the target must be in that board's `compatibleTargets`. An assembly key is a project name, not a new board ID.

Every root node must have an explicit `present` boolean in every assembly. `false` permits only the `present` field: it owns no bus, controller, address or pin. `true` preserves physical presence even when software disables its driver. A connected signal still occupies its endpoint in that case.

The binding defines children and their `compatible`; project `children` only supplies property overrides for those declared children. Assembly `nodes` uses full paths such as `radioModule.radio`. Omitted child presence inherits its parent. A required child cannot be disabled while the parent is present; a child cannot be present beneath an absent parent. Unknown paths, undeclared children and cyclic binding expansion fail.

Property resolution order is binding default, enclosing binding's child default, project node/child property, then assembly node property. Each step replaces individual scalar keys; no array/object merge or deletion exists. `null` is not a property value. `compatible` is immutable between assemblies; use distinct root nodes with explicit presence for alternative devices.

`board.*` paths are reserved for board-owned devices imported by the resolver. Root node `board` is forbidden. Their physical presence, connections and electrical facts come from the board registry and cannot be changed by assembly property overrides. Choosing a status LED or taking over a soft reservation changes its use, not those physical facts.

## Endpoints, buses and shared signals

A signal endpoint is an integer GPIO, a target pin spelling such as `PB12`, `{ "domain": "soc-gpio", "id": ... }`, `{ "domain": "device-pin", "node": "io0", "index": 0 }`, `{ "net": "indicator" }`, or `null`. Integers and SoC objects have identical meaning. STM32 spells pins `PA0` through `PZ15`; target data determines which exist. GPIO 0 is connected and valid; it never means absent.

A device pin is identified by its provider node and index, not an MCU GPIO number. The provider must be present, advertise a pin namespace and contain that index below `pinCount`. Adapters for existing component GPIOs preserve any HAL encoding advertised by the board; encoding is not proof that a device pin is a SoC GPIO. HAL configs accepting only SoC pins reject incompatible endpoints rather than inventing an integer alias.

An omitted optional signal resolves to `null`. A required signal must be explicitly described, directly or through a binding connection, but may be explicitly unconnected. Present but incomplete hardware is allowed until it is selected for a role requiring complete wiring or requested through an initializer. It exposes `CONFIG_AVAILABLE` as 0 and no `CONFIG_INIT`. Existing global radio/transport checks still reject WiFi without a usable selected radio. Enabling a general CAN driver does not force every unused CAN channel into use.

A bus declares `kind`, zero-based `index` and explicit `pins`. SPI pins are `sck`, `mosi`, `miso`; I2C pins are `sda`, `scl`; UART pins are `tx`, `rx`. Required keys may contain `null` for an unused direction. Target controller availability and directions required by consumers are checked separately. There are no implicit board-default pins. Optional UART `cts`/`rts` describe physical connections; the current generic UART API cannot configure hardware flow control, so an application needing it must use a supported target API.

| Transport | Bus settings | Device settings and renderer |
|---|---|---|
| SPI | positive `frequencyHz`, optional `mode` 0-3 (default 0), `bitOrder` `msb`/`lsb` (default `msb`) | optional node `transport` overrides these settings individually; omitted values inherit the bus |
| I2C | positive `frequencyHz`, shared by every consumer | optional node `transport.frequencyHz` must equal the bus rate; `mode`/`bitOrder` are forbidden |
| UART | positive `baudRate`, required `frame: {dataBits, parity, stopBits}` | all consumers use that controller's baud/frame; node `transport` is forbidden |

`frequencyHz` and `baudRate` are uint32 values, never zero/backend-default sentinels. UART forbids `frequencyHz`; SPI/I2C forbid `baudRate`/`frame`. UART data bits are 5-8, parity is `none`, `even` or `odd`, and stop bits are 1 or 2. Target adapters validate supported combinations; for example STM32 currently rejects 5-bit frames and 6-bit frames without parity. Baud rate is the requested rate, subject to supported controller divisors.

SPI `frequencyHz` is a transaction default, not a permanently fixed bus clock. Each node's resolved settings feed `hal_spi_settings_t.clock_hz`, `data_mode` and `bit_order`, display `clock_hz`, or LoRa/MCP251XFD `spi_clock_hz` as appropriate. Generic SPI nodes export a deferred `SPI_SETTINGS_INIT`. Binding `bus.maximumFrequencyHz`, when present, limits the resolved rate; exceeding it fails without silently reducing the rate. Transport rates are not duplicated in node properties; oscillator and RF frequencies remain distinct electrical properties.

The current MCP2515 driver fixes SPI at 10 MHz, MSB first, mode 0, and its CAN config has no SPI-rate field. Its initial renderer must accept only those resolved settings and report `JH-HW-TRANSPORT` for another request; it must not pretend to apply an unavailable API field. That is a current driver limit, not immutable module wiring. Renderers also reject settings absent from other current APIs: ILI9341, LoRa and MCP251XFD require MSB/mode 0; SSD1306 allows a mode but fixes MSB. Exporting a macro cannot make a driver honor an unsupported setting. On one SPI, MCP2515 can explicitly request 10 MHz while ILI9341 requests a different transaction rate.

I2C has one effective controller rate in version 1. In particular SSD1306 changes the controller clock at setup and does not restore it per transaction; its display `clock_hz` must therefore equal the bus rate. Binding maximum rates are checked for all present consumers, including software-disabled ones. Different I2C rates require a later scoped transaction API; initialization order must not choose the clock. UART exports the resolved baud/frame for an explicit `hal_uart_begin()` call, without creating a fictitious HAL config type. Unsupported transport settings fail even if the driver is disabled.

All device-tree controller indices start at zero. For example SPI index 0 is RP SPI0, STM32 SPI1 or ESP32-S3 SPI2; FDCAN index 0 maps to the existing config's `instance: 1`. The target adapter performs that conversion. Nodes refer to one named bus rather than duplicating its wiring. Two bus declarations cannot claim the same `(kind, index)`. UART index 0/1 maps to `HAL_UART_PORT_1`/`HAL_UART_PORT_2`; the generated numeric `JH_HW_BUS_<BUS>_UART_PORT` exposes that conversion. A standalone node `controller` also claims an exclusive `(kind, index)`; PWM/ADC bindings may instead describe pin-selected outputs where their HAL API has no independently selectable controller.

Each SPI slave owns a unique connected CS on its bus. I2C addresses are explicit, unshifted seven-bit integers, unique on the selected bus; there is no address default in a binding. `address: null` denotes an unaddressed, incomplete node. Future ten-bit addresses or address-translating muxes require a format extension.

Assembly `nets` maps a name to one physical `endpoint` and one `owner` signal path. Consumers refer to its name; the endpoint is written once. The owner must refer to that net and owns hardware control. Other members are aliases, not independent drivers: they expose the same endpoint and owner information, but no second driving initializer. A bus owns its shared lines separately; its signal paths use `bus.<name>.<signal>`, with root node `bus` reserved. Shared outputs require compatible polarity and electrical properties; a net never permits two independent active drivers. Two equal GPIO numbers without a bus, binding connection or named net are a collision.

Hard reservations are available only to their declared board owner. A project may explicitly release a soft reservation by its registry key in `releaseReservations`; unknown or hard keys fail. A physically connected board device sharing the pin still requires a shared-signal description. Clock-source pins required by the selected tree are also occupied. Validation covers all target GPIOs, not a single 64-bit mask. AF, IOMUX and detailed ADC/PWM pin-function checking are deferred.

A SoC endpoint must be a pin the board exposes (`gpio.exposedPins` of its profile). A board-owned device keeps using its own pins, and a project may use a soft-reserved pin after releasing that reservation in `releaseReservations`, even when the board does not expose it, such as the Pico LED on GPIO25. Hard reservations are checked first, so a hard-reserved pin fails with `JH-HW-RESERVATION` whether or not it is exposed. Any other pin outside the exposed set fails with `JH-HW-PIN-RANGE`, even when the target has it: the NUCLEO-G474RE does not route PD0, absent from its LQFP64 package, and the ESP32-S3-Zero does not route the flash pins GPIO26-32. The ESP32 GPIO backend checks the same exposed, soft and hard sets at run time.

A board-owned signal can join an explicitly declared net whose endpoint matches its immutable board endpoint. Its membership follows that explicit net declaration; no assembly override of board wiring is needed. When that signal is the owner, it need not replace its board endpoint with a project net reference. Board GPIO LEDs expose `out`, addressable LEDs `data` and buttons `in`; the board adapter associates existing reservation owners with these device paths. A net with multiple immutable board signals still has exactly one controlling owner. Clock pins cannot be shared through this exception.

## Bindings and hardware providers

A binding file defines exactly one `compatible`, domain, description, optional signals, properties, bus/controller requirements, children, intrinsic `connections`, `provides`, `requiresFeatures` and `pinCount`. HAL types live in `config/hardware/bindings/`, one file per type, named after its `compatible` with `,` replaced by `-`. Their defaults reach C through `src/hal/generated/jh_device_defaults.h`, generated by `scripts/generate_device_defaults.py`; HAL helpers read that header instead of keeping a second table. Root `bindings` lists individual project files relative to `device_tree.json`, using forward slashes; `../` is allowed for shared types. No glob, directory scan, URI, absolute path or copied HAL registry is used. Resolved file paths are deduplicated; duplicate `compatible`, including HAL shadowing, fails.

A property declares JSON `type`, `cType`, description, optional required/default value and bounds or enum. Numeric properties require a unit. The unit vocabulary is open: `Hz`, `ohm`, `us`, `ms`, `V`, `°C`, `K`, `bar`, `pixels` and dimensionless `1` are examples, not an enum. A unit is nonempty printable NFC UTF-8 text without surrounding whitespace, case-sensitive and preserved in the model. No aliasing, scaling or automatic conversion exists; a value is always expressed in its binding's declared unit. `cType` is `bool`, an `int8`/`uint8` through `int64`/`uint64`, `float`, `double` or `string`, consistent with its JSON type. Bounds, defaults and enum members must fit both types. Unknown properties fail. Resistor ratios use explicit ohms, never an undocumented scale.

Property `required` defaults to false. A required property must have a value after default/override resolution; a binding default satisfies it. An optional property without a default exports no macro when omitted. Bounds apply only to numeric properties. When a signal's condition is false, the signal can be omitted or explicitly `null`; its required rule applies only when the condition is true.

Signal direction is viewed from the MCU: `input`, `output`, or `bidirectional`. A signal declares allowed endpoint domains, `required`, and optionally one `when` condition comparing a declared resolved property to a scalar `equals` value. Conditions do not refer to variants or feature flags. When false, the signal must be unconnected. A binding cannot contain assembly maps, GPIO numbers, bus indices or arbitrary C expressions.

Intrinsic `connections` maps a module signal to a child `owner` signal and an `aliases` array. Paths are relative to that module's children. The project wires the external module signal once; expansion passes it to those children. The child owner controls the endpoint; the external module signal and other children are aliases. Conflicting direct child assignments, undeclared signals or cycles fail. This describes PCB connections, without choosing MCU pins. The [compound module example](../../tests/fixtures/device_tree/bindings/radio-module.json) illustrates the mapping.

`provides` lists existing registry capabilities, never new feature definitions. `requiresFeatures` records the HAL prerequisites for using that node's configuration; presence does not enable them. Build-global hardware such as CYW43 is selected by assembly `providers`, for example `cyw43: radioModule.radio`. One present candidate can be selected automatically, multiple candidates require an explicit choice, and absent or incomplete selections fail when used. This chooses physical hardware; `hal_project_config.h` still chooses software drivers.

## Clocks and status LED

The HAL registry contains one `config/hardware/clocks/<target>.json` per target. Each file declares that target and a nonempty `trees` map including `default`. Entries declare a `backend` (`pico-sdk`, `stm32g474`, `esp-idf` or `mock`), fixed `requiredSources`, output `frequenciesHz` and backend `parameters`. Assemblies choose only the tree key, never override its MHz or parameters. `scripts/clock_registry.py` checks that each tree's frequencies follow from its parameters and match what actually sets the clocks: the STM32G474 startup headers, the pinned Pico SDK defaults with no HAL override, and the sdkconfig of the ESP-IDF builds in the `esp-idf` gate stage. Every required source must be a clock input of the target. The schema alone does not promise support for an arbitrary entry: the registry lists only trees with backend code. The HAL never sets the Pico SDK, ESP-IDF or mock clock, so those targets have `default` alone, and STM32G474 has the two trees its startup code builds. Any other tree fails, even with consistent frequencies.

Physical `clockSources` belongs in `boards/profiles/<board>.json`: source key, `kind` (`crystal` or `external-clock`), positive `frequencyHz` and SoC `pins`. Required source kind and frequency must match the board. Internal oscillators belong to the target/backend, not to fabricated board facts. Mock has empty sources and output frequencies. Each ISA has its own registry entry. Every board declares the sources the default tree of each of its targets needs, and the board generator validates `clockSources` with the same schema.

`pins` lists GPIOs occupied by the source. The target descriptor's `clockInputs` names the inputs a board oscillator can drive and, for each source kind, the GPIOs it occupies. Dedicated oscillator pads outside the GPIO namespace use an empty list; no GPIO number is invented for RP/ESP crystal pads. On STM32G474 an HSE crystal takes `PF0` and `PF1`, while an HSE bypass clock takes only `PF0` and leaves `PF1` a GPIO; LSE uses `PC14`/`PC15` and `PC14` the same way. A board source must use an input of each of its targets and list exactly the pins of its kind there, so an empty list cannot bypass their reservations.

Initial STM32 trees preserve HSI16/170 MHz with FDCAN from PCLK1 and HSE24/160 MHz with FDCAN80 from PLL Q, including the existing HSI16 fallback. SDK target defaults must be checked against the selected SDK and its clock inputs; unexpected frequencies fail rather than silently changing generated facts. [Clock examples](../../tests/fixtures/device_tree/valid/clock-trees.json) are independent specification data, not the production registry.

`statusLed.node` explicitly selects a present board or application LED. It must have a usable endpoint and known active polarity; optional `pixelOrder` is valid only for an addressable RGB LED and overrides its default order. Selecting the LED does not rewrite its physical polarity. `node: null` is reserved for mock with no physical LED. Existing examples and hardware fixtures preserve their status indication during migration.

## Generated macros and initializers

The only early header is `jh_hardware.h`, containing `JH_HW_*` macros and no HAL includes, typed objects or hardware startup calls. It exports normalized hardware, independent of program flags. `hal_project_config_hook.h` and the configuration reader load the exact explicitly supplied header before `hal_project_config.h`. A JSON build with a missing header fails; unrelated include-path files cannot activate it.

| Family | Meaning |
|---|---|
| `JH_HW_SCHEMA_VERSION`, `JH_HW_TARGET_NAME`, `JH_HW_BOARD_NAME`, `JH_HW_ASSEMBLY_NAME`, `JH_HW_HARDWARE_SHA256` | version and resolved hardware identity |
| `JH_HW_HAS_<CAPABILITY>` | effective hardware capabilities, numeric 0/1 |
| `JH_HW_NODE_<PATH>_PRESENT`, `JH_HW_NODE_<PATH>_CONFIG_AVAILABLE` | numeric 0/1, defined for every declared/expanded node |
| `JH_HW_NODE_<PATH>_PROP_<PROPERTY>` | resolved typed scalar |
| `JH_HW_NODE_<PATH>_PIN_<SIGNAL>_CONNECTED`, `JH_HW_NODE_<PATH>_PIN_<SIGNAL>_DOMAIN` | numeric connection flag and domain (0 none, 1 SoC, 2 device) |
| `JH_HW_NODE_<PATH>_PIN_<SIGNAL>`, `JH_HW_NODE_<PATH>_PIN_<SIGNAL>_OWNER` | SoC HAL encoding when available; numeric owner flag |
| `JH_HW_NODE_<PATH>_PIN_<SIGNAL>_PROVIDER`, `JH_HW_NODE_<PATH>_PIN_<SIGNAL>_INDEX` | device namespace path and index |
| `JH_HW_NODE_<PATH>_BUS_INDEX`, `JH_HW_NODE_<PATH>_CONTROLLER_INDEX`, `JH_HW_NODE_<PATH>_ADDRESS` | explicit resolved selection, when present |
| `JH_HW_BUS_<BUS>_INDEX`, `JH_HW_BUS_<BUS>_FREQUENCY_HZ`, `JH_HW_BUS_<BUS>_PIN_<SIGNAL>` | bus selection, SPI/I2C default clock and wiring |
| `JH_HW_NODE_<PATH>_FREQUENCY_HZ`, `JH_HW_NODE_<PATH>_SPI_MODE`, `JH_HW_NODE_<PATH>_SPI_BIT_ORDER`, `JH_HW_NODE_<PATH>_SPI_SETTINGS_INIT` | effective device transaction settings; mode 0-3, bit order 0 LSB / 1 MSB |
| `JH_HW_BUS_<BUS>_UART_PORT`, `JH_HW_BUS_<BUS>_BAUD_RATE`, `JH_HW_BUS_<BUS>_DATA_BITS`, `JH_HW_BUS_<BUS>_PARITY`, `JH_HW_BUS_<BUS>_STOP_BITS`, `JH_HW_BUS_<BUS>_FRAME_CONFIG` | UART settings; parity 0 none / 1 even / 2 odd; deferred `HAL_UART_CFG_*` frame token |
| `JH_HW_NODE_<PATH>_CONFIG_INIT` | deferred initializer for a supported HAL configuration |

An absent node exports only presence/config-availability flags. A present disconnected signal exports connection/domain/owner flags, not a guessed pin number. A device endpoint exports provider/index; a numeric pin macro exists only if its adapter supplies a real HAL encoding. Preprocessor flags are numeric literals, never HAL enum identifiers. Strings are escaped C literals; integers use suffixes matching `cType`; float/double values are finite, range-checked and emitted with round-trip precision.

Literal spelling is fixed; range checking precedes rendering and does not rely on a suffix to narrow a type:

| `cType` | Literal |
|---|---|
| `bool` | `0` / `1` |
| `uint8`, `uint16`, `uint32` | decimal with `U` |
| `uint64` | decimal with `ULL` |
| `int8`, `int16`, `int32` | decimal without suffix; negative expression in parentheses |
| `int64` | decimal with `LL`; negative expression in parentheses |
| `float` | binary32 value, scientific notation with 9 significant decimal digits and `f`; negative value in parentheses |
| `double` | binary64 value, scientific notation with 17 significant decimal digits, no suffix; negative value in parentheses |
| `string` | UTF-8 C string literal; `"`, `\` and `?` get a backslash, other bytes outside printable ASCII a three-digit octal escape |

The minimum signed values are `(-2147483647 - 1)` and `(-9223372036854775807LL - 1LL)` for int32/int64. Floating literals use `.` and lowercase `e`, an explicit exponent sign and at least two exponent digits, independently of locale. Input decimal numbers are parsed exactly, then rounded once to the declared IEEE-754 type using round-to-nearest, ties-to-even. Overflow, nonfinite values and nonzero values rounding to zero fail; representable subnormals are allowed. Negative zero becomes positive zero. Integer inputs retain exact precision, including uint64 values above 2^53. For example float `0.22` emits `2.19999999e-01f`.

Token conversion splits acronym-to-word and lowercase/digit-to-uppercase boundaries, replaces punctuation with `_`, collapses repeated `_`, and uppercases. Node path segments are joined with `_`. Thus `radioModule.radio` becomes `RADIO_MODULE_RADIO`, while `fooBar` and `fooBAR` collide. Names are checked over all assemblies, expanded children, signals, properties and reserved macro families before writing output. A collision fails; no automatic suffix or unstable ordinal is added.

Initializers are used after the relevant HAL types become visible. UART creation casts the numeric port macro to `hal_uart_port_t`; `FRAME_CONFIG` remains a deferred `HAL_UART_CFG_*` token usable in both C and C++. HAL domain renderers own field order, defaults and union selection; bindings cannot inject C source. A custom project type exports scalar/pin macros without defining a new HAL type or initializer; `CONFIG_AVAILABLE` is 0 when no HAL renderer exists. For supported types this flag describes complete wiring and ownership, independently of software flags. C11 uses aggregate/designated initializers. C++17 uses aggregate initializers, or a pure immediately invoked lambda for selecting a non-first union member in `hal_can_config_t` or `hal_lora_radio_config_t`. Such a lambda only constructs a value, allocates no heap and never initializes hardware; a file-scope C++ `const` union config need not be `constexpr`. Stack-sensitive applications can initialize a static object during their explicit startup.

```c
#include <JaszczurHAL.h>

static const hal_can_config_t can0_config = JH_HW_NODE_CAN0_CONFIG_INIT;
/* The application explicitly creates/starts CAN and handles errors. */
```

A renderer rejects an explicit endpoint if the destination HAL field would interpret its value as a default/disconnected sentinel. No initializer silently substitutes backend default wiring. Macro definition comments document units, missing values and required feature/type visibility.

Hardware macros internal to HAL remain defined only in `jh_board_config.h`: `HAL_LED_BUILTIN`, `HAL_BOARD_STATUS_LED_*`, `HAL_BOARD_HAS_*`, capability masks, `HAL_BOARD_CAN_CHANNELS`, `HAL_STM32G474_CLOCK_HSE_160MHZ` and the `HAL_CYW43_PIN_*` pins of a CYW43 module selected as provider. They use the resolved assembly and reference early macros when exposing the same value. Program feature flags remain project-owned. Startup and FreeRTOS load that owner header before clock use; neither `-D` nor another header redefines these hardware macros. A `-D`, variant definition or project file that defines or undefines a macro of either generated header, even in an inactive branch, fails with `JH-HW-FEATURE`.

## Selection and build sequence

Selector names are fixed: CMake `JH_ASSEMBLY`, CLI `--assembly`, manifest/local-state `assembly`; program selection uses existing `JH_PROJECT_VARIANTS` and `--variant`. Explicit command/build arguments win over the interactive local choice, then the manifest default. Batch matrices ignore local state and explicitly choose each assembly/variant. A selected assembly determines target and board; an additionally supplied target must agree.

For a selected target, one assembly is automatic, several require an explicit key, and none is an error. With neither target nor assembly selected, a sole assembly can determine both; otherwise require a target or assembly, without inventing a default board. Unknown explicit keys never fall back. Input board selectors (`JH_BOARD`, `--board`, manifest `board` or `targetProfiles.*.board`) are rejected in JSON mode. Historical local board state is ignored; the resolved internal board value is not a second input.

In JSON mode, any project-owned definition of `JH_PROJECT_TARGETS` is rejected, including an inactive conditional definition. Targets come from assemblies. Variant declarations must be independent of target, assembly and `JH_HW_*` so list operations work before a build. Conditions inside a selected variant's program configuration may still inspect hardware.

The generator is `scripts/generate_hardware_config.py`. `list --config-dir DIR` reads assemblies without output generation. `resolve --config-dir DIR [--target ID] [--assembly ID] --output-dir DIR` resolves hardware and writes `jh_hardware.h` plus `jh_hardware_resolved.json`. `finalize --hardware FILE --hardware-header FILE --config-dir DIR [--variant ID] [--define NAME[=VALUE]] --output-dir DIR` reads that same model, preloads its exact header into the project-config reader, resolves features/tunables and emits late build data. The build integration that calls these commands is a later step.

Finalization adds software configuration to the existing resolved model; it does not resolve hardware independently or rewrite early hardware facts. It emits `jh_board_config.h`, `jh_hardware_config.cmake`, `jh_hardware_config.json`, existing link-signature header/source artifacts and `generation.d`. Output is atomic per file; builds consume it only after successful generation. Dependency content digests prevent finalizing a stale early model. Both phases depend on the selected JSON, all loaded bindings, board/target data, clock registry and generator/schema inputs.

`jh_hardware_resolved.json` has `schemaVersion: 1`, `selection` (`target`, `board`, `assembly`), `hardware`, `hardwareSha256` and `dependencies`. `hardware` contains expanded flat `nodes`, named `buses` and `nets`, resolved `clock` and `statusLed`, effective `capabilities`, selected `providers` and occupied `reservations`. Each node records `compatible`, `domain`, presence, typed properties, signal endpoints/owners and optional bus/controller/address and resolved transport settings. Named bus/net references retain their single declaration. Properties record `value`, `cType` and `unit` for numeric properties; GPIO endpoints use the explicit domain form. Absent nodes record identity and presence only. Clocks include fixed parameters/frequencies and consumed physical sources. Dependencies record input-relative paths and content SHA-256, with a registry-relative namespace for HAL inputs; their locations affect rebuilds, not hardware identity.

Finalization writes the model with `software` (variant, effective features, tunables, explicit non-feature definitions and build provider) and `configSha256` to `jh_hardware_config.json`; the signature symbol takes `configSha256` as well. ABI and toolchain inputs join `software` with the build integration. The hardware digest covers `selection` and `hardware`; the final digest covers that digest and `software`. Generated names, dependency locations and descriptions are excluded. The explicitly supplied early header must be exactly the text its model produces. `list` returns JSON with `schemaVersion` and an `assemblies` array of `{name, target, board}`, sorted by name. Failed commands return nonzero status with diagnostics on stderr and no usable output.

Per-application outputs retain `<target>/<assembly>` with separate variant/configuration subdirectories. Shared static-library caches and installs also use the full configuration digest. Independent projects with equal assembly keys cannot overwrite one another. Hardware identity includes the normalized full hardware model, even unused connected nodes; final identity also includes effective HAL flags/tunables, ABI inputs and provider/toolchain identity. Numbers normalize to their resolved C types before hashing. In hash input only, typed float/double `value` is replaced by a lowercase IEEE-754 bit-pattern string, exactly 8/16 hex digits, most significant digit first; `cType` remains present. Float `0.22` is `"3e6147ae"`; double `1` is `"3ff0000000000000"`. Resolved JSON keeps their numeric values using the literal's scientific digits without a C suffix. Other non-integer numeric backend/software values receive `cType: double` before this transformation; hash input contains no floating JSON tokens. Integers are exact decimal JSON numbers, booleans are `true`/`false`. The digest is SHA-256 of compact JSON with keys sorted by Unicode code point, UTF-8 strings without ASCII substitution, separators `,` and `:`, and no whitespace/BOM/trailing newline. JSON strings escape quote/backslash, use `\b`, `\f`, `\n`, `\r`, `\t` for their controls and lowercase `\u00xx` for other C0 controls; slash and other Unicode scalars remain unescaped. Unpaired Unicode surrogates fail input validation. Absolute paths, timestamps and descriptions do not affect identity.

Only absence of `device_tree.json` selects the legacy board build. An invalid existing file or missing generated output is an error. Switching modes removes obsolete per-build outputs and cache entries. Existing board-only values and selectors remain supported without JSON.

## Standalone library package

`--project-config DIR` / `HAL_PROJECT_CONFIG_DIR` selects the configuration directory independently of application sources. It contains `device_tree.json`, `hal_project_config.h` and referenced bindings. A library for an existing application reads those files at their owner; a standalone preset has its own directory. Neither needs `app.c`, `app.cpp` or `app_start()`. Library runners gain `--assembly` and `--variant` and share the same selection/generation path as firmware.

JSON builds evaluate HAL features/tunables exactly as firmware does, without adding a runner's default feature preset. Platform requirements and explicit `-D` inputs participate in the common calculation and identity. `--all-features` still fails for unsupported hardware or transport. The legacy build without JSON keeps its defaults. ESP-IDF may retain its internal probe, and matching SDK/`sdkconfig` remains required.

The package contains the archive, public HAL headers, `include/generated/jh_hardware.h`, `jh_board_config.h` and the existing link-signature header, plus resolved JSON, `jh_hardware_config.cmake` and the signature reference source under `share/JaszczurHAL/generated/`. The CMake record exposes `JH_HARDWARE_HEADER`, `JH_HARDWARE_RESOLVED_FILE`, `JH_HARDWARE_SHA256` and `JH_CONFIG_SHA256`, and uses existing project-config variables for effective features, non-feature HAL definitions and tunables. It also records ABI/provider inputs and relocatable package paths. Global HAL registries remain single installed sources; packaging never creates a second generated copy.

A JSON application's build computes its expected identity from its own inputs before linking the archive. A package's supplied reference alone cannot establish that match. Direct compiler consumers use the package's frozen configuration and cannot redefine hardware/HAL compile parameters. Under the accepted full-hardware digest, even an application-only pin change requires a matching archive. Sharing one archive across different hardware descriptions is outside version 1.

## Diagnostics and examples

Diagnostics report an error code, source file and JSON Pointer, with both owners when a resource conflicts. Stable categories are `JH-HW-SCHEMA`, `JH-HW-BINDING`, `JH-HW-PRESENCE`, `JH-HW-TARGET-BOARD`, `JH-HW-BOARD`, `JH-HW-PIN-CONFLICT`, `JH-HW-PIN-RANGE`, `JH-HW-RESERVATION`, `JH-HW-TRANSPORT`, `JH-HW-TARGETS`, `JH-HW-CONTROLLER`, `JH-HW-ADDRESS`, `JH-HW-NET-OWNER`, `JH-HW-PROPERTY`, `JH-HW-CLOCK`, `JH-HW-STATUS-LED`, `JH-HW-MACRO-NAME`, `JH-HW-ASSEMBLY`, `JH-HW-FEATURE`, `JH-HW-GENERATION` and `JH-HW-PACKAGE`. Validation errors never generate partially usable configuration.

The resolver returns the first error only, never cascaded errors from unresolved data. Phase order is input/schema, binding declarations and macro names, assembly/target/board selection, node presence and references, properties/conditions, clocks, endpoints/nets/reservations/controllers/addresses/transport, status LED, then final software/features and package identity. Reservation-key validity is checked before endpoint usage, and a hard reservation before exposure; imported board owners and declared buses claim their resources before project nodes. Within a phase, order is source input-relative path then JSON Pointer by Unicode code point (array indices numerically), with error code as a final tie breaker. Structural schema failures use the failing leaf rather than aggregate `oneOf`/`maxProperties` locations. No field-insertion order affects diagnostics.

`at` is a JSON Pointer: an invalid value or forbidden/extra field points to that field; a missing required field points to the would-be field, even though it does not exist. An error about a whole object uses its pointer only when no narrower field exists. Use RFC 6901 `~0`/`~1` escaping. For `present: false` with additional fields, point to the first forbidden field; missing root presence points to `/present`. An inherited/default value points to the binding or override which introduced it. Conflicts point to the later owner in the defined ordering and carry the earlier origin in `related`. Header/CLI errors use `at: ""`, source and line (headers), or `argument` (CLI), rather than inventing a JSON path. Invalid children below absent parents stop at presence validation before wiring expansion. These rules also apply to the expectations in `cases.json`.

The [multi-target example](../../tests/fixtures/device_tree/valid/project/device_tree.json) shows independent ARM/RISC-V assemblies, shared electrical defaults, child nodes, property overrides, clocks and LED selection. Other examples cover [shared signals](../../tests/fixtures/device_tree/valid/shared/device_tree.json), [absence versus disconnection](../../tests/fixtures/device_tree/valid/presence/device_tree.json), [expander namespaces](../../tests/fixtures/device_tree/valid/expander/device_tree.json) and [ESP/mock configurations](../../tests/fixtures/device_tree/valid/backends/device_tree.json). Their synthetic bindings are independent host data. They do not establish hardware support or replace the HAL binding and clock registries.

The index additionally covers reservations and board LD2 sharing, explicit CYW43 providers and both feature/presence directions, status-LED failures, child-property precedence, conditional signals and nonempty intrinsic aliases, independent device clocks, UART frames, no-argument selection and forbidden target declarations. `resolutionCases`, `featureCases`, `headerCases`, `numericCases` and `rendererCases` describe expected later resolver outputs. Header samples use `.h.txt` so existing project discovery does not interpret deliberate invalid inputs as real project configs.

Structural validation reads the whole input; each `resolve` invocation validates hardware for the selected assembly. Acceptance cases list `resolveAssemblies` to check each assembly separately, and semantic rejection cases supply explicit `input.assembly`, so selection errors do not hide the intended defect. Selection expectations have their own group.

`rendererCases` isolate current API restrictions without implying a production binding exists. Float vectors include an exact halfway decimal and a value just above it that would fail if first rounded through binary64, plus a binary32 subnormal.
