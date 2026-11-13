# jh host CLI

*Also available in [Polish](README.pl.md).*

`jh json inspect` reads JSON, selects a value by JSON Pointer and optionally
writes or checks an output file. It uses the jhbase parser and file API.
`jh json validate` checks a document against the hardware schema subset,
including references between local files. `jh generate device-defaults` reads
the catalogue of HAL component types and writes or checks the generated
header of their defaults, and `jh generate features` does the same for the
header and CMake data of the HAL feature registry. `jh generate boards`
writes or checks the board files the repository keeps.
`scripts/sync_generated.py`, and through it the pre-commit hook and the gate,
run these generators through the launcher described below. `jh config inspect` evaluates a project's `hal_project_config.h` as the
build reads it. `jh feature list` and `jh feature resolve` give the feature
registry and the features one build requests, resolved; `jh feature lint`
checks the feature requests of project headers and manifests. `jh board`
validates and lists the board registry and writes the configuration of one
build, and `jh clock` checks and lists the clock tree registry.

Prepare the pinned host dependencies explicitly before configuring:

```sh
python3 scripts/component_manager.py component jhbase
python3 scripts/component_manager.py component utf8proc
python3 scripts/component_manager.py component pcre2
```

The build also needs utf8proc, pinned by jhbase and prepared separately in
`third_party/utf8proc`: the type catalogue checks units as printable NFC text
through `jb::unicode`.

PCRE2 lives in an ignored checkout selected by `third_party/pcre2_version.conf`.
Configure and build verify its revision and cleanliness without downloading or
changing dependencies. The shared jhbase checker verifies origin and actual
source bytes, including edits hidden by Git index flags. PCRE2's unused
`deps/sljit` submodule must remain an empty directory. The executable statically
links PCRE2-8 without JIT.

Build from the JaszczurHAL checkout, with CMake 3.22 or later:

```sh
cmake -S tools/jh -B .build/jh -G Ninja -DCMAKE_BUILD_TYPE=Release \
  -DCMAKE_C_COMPILER=gcc -DCMAKE_CXX_COMPILER=g++
cmake --build .build/jh
ctest --test-dir .build/jh --output-on-failure
```

On Windows, use a Visual Studio developer PowerShell. With Visual Studio 2026
the generator is `"Visual Studio 18 2026"`, which needs CMake 4.2 or newer:

```powershell
cmake -S tools/jh -B .build/jh -G "Visual Studio 17 2022" -A x64
cmake --build .build/jh --config Release
ctest --test-dir .build/jh -C Release --output-on-failure
```

`scripts/jh` (Linux) and `scripts/jh.ps1` (Windows, `-Arch x64|x86`) build
the tool on first use below `.build/host-tools/<variant>/<key>/bin` and run
it, rebuilding only when an input changed; the
[script reference](../../doc/api/en/00_scripts.md) describes the bootstrap.
The manual build below is the same project the launchers build.

This is a separate host project. It builds the same jhbase as the rest of the
HAL: the pinned `third_party/jhbase` checkout (`scripts/ensure_jhbase.sh`), or
the checkout named by the `JH_JHBASE_DIR` environment variable. It enables
`JHBASE_HOST` for that build and inherits nothing from a firmware build. The
`host` stage of `runalltests.sh` builds the tool and runs these checks.

On Linux, the resulting executable is `.build/jh/jh`; on Windows it is
`.build/jh/Release/jh.exe`. The following examples use `jh`
for that explicit executable:

```sh
jh --help
jh --version --json
jh json inspect --file tests/fixtures/device_tree/cases.json --pointer /cases/0/expected --json
jh json inspect --file input.json --pointer /limits/max --output result.json
jh json inspect --file input.json --pointer /limits/max --output result.json --check --json
jh json validate --file input.json --schema config/hardware/device_tree.schema.json --json
jh generate device-defaults --root . --check
jh generate device-defaults --root . --write --json
jh config inspect --header examples/13_adc/hal_project_config.h --define HAL_TARGET_RP2040 --json
```

Input paths are resolved against cwd captured once on entry. Windows arguments
come from `wmain` and are converted from UTF-16 to UTF-8. Options accept
`--name value` or `--name=value`; flags take no value. Unknown options, extra
command words and a repeated option that the help does not mark as repeatable
fail. `-h` and `-V` abbreviate help and version.
`--` ends option parsing. A value beginning with `--` needs `--name=value`.
An absent `--pointer` selects the root; names are not normalized to NFC.

Without `--json`, inspection prints the selected raw JSON followed by LF.
`--output` publishes those same bytes through the atomic file API, retaining
mtime if they match. It writes no value to stdout. `--check` requires
`--output`, compares bytes without changing anything and reports drift on
stderr. Input is limited to 16 MiB by default; `--max-bytes` accepts 0 through
64 MiB. Parsing also limits depth to 256, values to 1000000, children per
container to 100000, number tokens to 1 MiB and key comparisons to
`32 * max_bytes + 1 MiB` work units.

With `--json`, stdout contains one UTF-8 JSON object and LF. It includes
`formatVersion: 1`, `command` and symbolic `status`. Successful inspection
adds `source`, `pointer` and `value`; file output also adds `output`, `changed`
and `checkOnly`. Number tokens retain their exact spelling. Errors include a
message, the requested Pointer and, when available, source, byte offset, one-based line and byte
column, and the parser's `jsonError` code. Help and version also have JSON
results. Human diagnostics go to stderr.

Validation returns every selected error in Pointer order, comparing decimal
tokens numerically and other tokens by Unicode code point. The JSON result has
`command: "json.validate"`, `source`, `schema` and an `errors` array. Each error
contains `instancePointer`, `schemaUri`, `schemaPointer`, `reason` and `message`.
`schemaUri` percent-escapes filenames; rule locations refer to the actual
keyword after resolving `$ref`. Human errors
go to stderr; successful validation without `--json` writes nothing.

The supported keywords are those used by `config/hardware/*.schema.json`;
this is not a full JSON Schema implementation. Unknown keywords and unsupported
regex constructs fail explicitly. References use local file paths with forward
slashes, with percent-decoded JSON Pointer fragments. Network references are
unsupported. Regex uses UTF-8 search with LF newlines and Python-compatible
`\s`; the adapter accepts the production patterns, including lookahead and
`\uXXXX`. PCRE2 carries Unicode 17.0 data; whitespace expansion is explicit.
Classes such as `\w` and `\d`, backreferences and inline flags are unsupported.
POSIX character classes and an unescaped `]` immediately after `[` or `[^`
are also unsupported. Use `\]` for a literal closing bracket in a class.

Numbers compare exactly in `const`, `enum`, `uniqueItems`, numeric bounds and
`integer`. For example, `0.1` differs from `0.10000000000000001`; `1` equals
`1.0`, while `true` differs from `1`. This can change results compared with the
Python validator's binary64 rounding. Validation does not apply defaults or
normalize strings. Conversion to a target floating type remains a separate step.

Validation limits include 50 million work units, 64 nested schema evaluations,
100000 schema values across unique loaded documents, 64 MiB of schema input,
256 loaded files, 1024 retained errors,
1 MiB of diagnostic text, 8192-byte Pointers and 32768-byte document identities.
Decoded values are limited to 16 KiB and patterns to 4096 bytes. Regex has a
2 MiB allocation arena, a 1 MiB match heap limit, one million match steps and
depth 128. Exhaustion returns `HAL_EOVERFLOW` before publishing any error prefix.
The internal C API in [jh_json_schema.h](json/jh_json_schema.h) accepts caller-owned
scratch, custom limits and optional document/report callbacks.

`jh_json_read_settings` in [jh_json_file.h](json/jh_json_file.h) reads VS Code files and project
manifests the way `load_json_file` of the Python tools does.
It accepts comments outside strings, the last value of a repeated name and
`NaN`/`Infinity`. A missing file reads as an empty object, and the root must
be an object. Numbers keep their original tokens. Unlike Python, it rejects a
`\u` escape of an unpaired surrogate, applies the parser limits (256 nested
containers, 100000 members each) and accepts an integer longer than the 4300
digits `int()` of Python 3.11+ allows. No `jh` command exposes it yet. The
`jh_settings` test checks the reader itself; `jh_settings_compat` compares it
with `load_json_file` on the `.vscode` files of the checkout outside
`tests/hardware` and on edge cases.


`jh repository files --root <checkout> --git <absolute-executable>` runs Git
without a shell through `jb::host`. It uses `-C <root> ls-files --cached
--others --exclude-standard -z`, inheriting the environment and the caller's
cwd. Raw stdout retains every byte, including NUL separators and POSIX
filenames outside UTF-8. Child stderr is forwarded. `--json` returns UTF-8
`paths`, `root`, and the process fields `started`, `exited`, `exitCode`,
`termSignal`, `nativeError`; a non-UTF-8 filename fails explicitly in this mode.
`--max-bytes` bounds captured path bytes in JSON mode with the same default
and ceiling as inspection. `--timeout-ms` accepts 0 through 4294967295;
zero waits indefinitely. SIGINT (Ctrl+C or Ctrl+Break on Windows) requests
cancellation and waits for process cleanup. Timeout exits with 124 and
cancellation with 130; other Git exit codes are retained. A failure to launch
or stream exits with 3 (4 for allocation or byte limits), and its JSON status
identifies the cause. These process
codes apply only to `repository files`.

Clients may pass `--json --json-version 1`. An unsupported major gives
`HAL_EUNSUPPORTED` and exit 2 before command execution, using the currently
supported result envelope. Omitting the option selects major 1. Within a
major, clients must ignore unknown fields; existing field meanings and types
are stable. A breaking change requires a new major.

## Project configuration

`jh config inspect` evaluates a project's `hal_project_config.h` the way
`scripts/project_config.py` does. `--header <path>` names the header; a
missing file is an empty configuration. Each `--define NAME[=VALUE]`
predefines a macro, in order, as `-D` does. `--preinclude <path>` reads a
header first, the way a device tree build reads its generated `jh_hardware.h`.
The reader covers the same part of the preprocessor: object-like and
function-like `#define`, `#undef`, the `#if` family with `defined` and integer
expressions, quoted `#include` relative to the including file, `#pragma once`
and `#error`; `#warning` and `#line` are ignored. `scripts/project_config.py`
adapts this result for the remaining Python tools. The `jh_config` test checks
the reader in-process; `jh_config_preprocessor` compares it with the
preprocessor of a GCC or Clang compiler, also on every `hal_project_config.h`
of the checkout outside `tests/hardware`.

Without `--json` the command prints the defined macros as `#define` lines in
definition order and reports an `#error` on stderr. With `--json` the result
(`command: "config.inspect"`) has `header`, `preinclude`, the `files` read,
the `targets` of `JH_PROJECT_TARGETS`, the `variants` of `JH_PROJECT_VARIANTS`
with definitions in the `-D` form, the `macros` in definition order, the
header's own `features`, the `dependencies` (unknown identifiers that decide a
name), `directives` (every `#define` and `#undef` of the files read, in any
branch: `file`, `line`, `directive`, `name` and the `text` after the name) and
`error` (text and place of `#error`, or `null`). Each macro has `name`,
`body`, `source`, `functionLike`, `parameters`, `unresolved` (names its value
uses that the configuration does not define) and `integerState`: `value` with
`integer`, `none` when the build has no integer for it, `outOfRange`, or
`error` with `integerError` (`status`, `message`) when its value fails, such
as an invalid literal or a value over the limit below; only that macro fails.
A header that stops on `#error` is still a result, with exit code 0.

`jh config directives --file <path>` lists the `#define` and `#undef` of one
file in the same form without evaluating it, so also for a header the reader
rejects; a missing file gives an empty list.

`jh config cmake --root <HAL> --target <id> --output <file>` writes the
configuration of one build for CMake. `--config-dir <directory>` selects its
`hal_project_config.h`; without it, the command configures a library from
the target and definitions alone. An explicitly empty `--config-dir` is an
argument error (exit 2), for both `config build` and `config cmake`.
`--variant <id>` selects a declared
variant, repeated `--define NAME[=VALUE]` supplies extra definitions, and
`--hardware-header <file>` reads the generated hardware header first.
The command checks the selected target, variant and feature requests before
publishing the file, and preserves an identical output. The file is written
atomically through the output directory: a regular file or a missing one is
accepted, a symbolic link at the output path is refused with exit code 3
(the former Python command wrote through such a link). A feature removed
by the project header stays removed even if a variant requested it.

CMake calls this command directly. The entry points prepare `jh` before
configure; configure only verifies that the executable matches its sources.
Missing or stale tools produce a preparation command rather than a build
during configure. The generated file records all headers read, so changing
an included header triggers reconfigure.

`jh config build` accepts the same project selection and returns its model
with `--json`, without writing a CMake file or resolving feature requests.
The result contains `config` (the `config.inspect` model), `variant`,
`definitions`, `definitionSources`, `requestedFeatures` and `featureRequests`
(symbol, optional value and source for each request). The Python
adapter uses this command; its explicit `--selector` and repeated
`--required-feature` preserve target facts supplied by an importer instead
of loading them from `--root`. A header `#error` rejects a build and sets
`configurationRejected` in the JSON error. Other errors leave it false.
Diagnostics use the usual `jh:` prefix and status suffix.

The adapter supplies `--header` instead of `--config-dir` to retain the
caller's relative path in diagnostics and definition sources. These two
options cannot be combined.

Integer arithmetic is signed and 64-bit. A literal or a result outside that
range in an operand a condition computes fails with `[JH-CFG-SCOPE] ... exceeds
the signed 64-bit range of jh` (`HAL_EOVERFLOW`, exit 4), where the Python
reader computes without limit; operands C does not compute are not checked. A
macro value outside the range reports `outOfRange` instead of a number. Like C,
and unlike the regular expressions of the Python reader, numbers and names are
ASCII: a non-ASCII decimal digit cannot be evaluated, and a directive name
followed by a non-ASCII character other than whitespace is rejected. A
condition nests at most 256 levels, macros expand through at most 1000 levels
and a file has at most 16 MiB. Expanding conditions and tracking the names a
value depends on visit at most 1000000 tokens per read, and each value the
command reports has 1000000 of its own, so one value never takes from another;
each macro is scanned once per dependency question, so the work follows the
size of the header, not the number of paths between its macros. A limit fails
with `HAL_EOVERFLOW` and exit 4; Python stops on its recursion limit there or
has no limit.

A rejected header exits with 2 and `HAL_ECONFIG`. The message is the
`[JH-CFG-*]` diagnostic of the Python reader; `code` holds its prefix and
`source` the file and line. Invalid UTF-8 and an octal literal with 8 or 9
give `HAL_EPROTO`, and an unreadable file exits with 3.

## Feature registry

`jh generate features --root <checkout> (--write | --check)` reads the HAL
feature registry: `config/features.schema.json` and every
`config/features/*.json` fragment in name order, hidden names included.
`--config-root` reads another configuration directory. Each file is read the
way `json.loads` reads it: the last value of a repeated name, `NaN` and
`Infinity` are accepted, comments are not. The command then applies the rules
of `scripts/generate_hal_features.py`: exact fields, the symbol, domain and
source path patterns, the managed dependencies the schema lists, relations to
known symbols, no `implies` cycle, symmetric conflicts and no conflict inside
the closure of a symbol. Messages keep the wording of that script.

`--write` publishes `src/hal/generated/jh_hal_features.h` and
`cmake/generated/jh_hal_features.cmake` with mode 0644 and leaves identical
files untouched; `--check` compares them and prints a unified diff of at most
80 lines per file. Their paths come from `config/tooling/artifacts.json` and
are relative to `--output-root` (default `<root>`); an absolute path, a drive
or a `..` segment fails with exit code 2 before anything is written. The
registry and schema
digests are the SHA-256 of the compact JSON with sorted keys and ASCII escapes
that `json.dumps(sort_keys=True, ensure_ascii=True)` writes. With `--json`
the result has `command: "generate.features"`, `status` (`HAL_OK`, or
`HAL_ESTATE` for a missing or stale artifact), `root`, `config`, `output`,
`changed` (the number of files written or given mode 0644; with `--check`,
the number of files that differ) and `checkOnly`.

The command exits with 0 on success, 1 when `--check` finds drift, 2 for an
invalid registry or a registry file it cannot read or write, and 4 when memory
runs out. Unlike the Python generator, it rejects a `\u` escape of an unpaired
surrogate, reports a `kind` of an array or object as a regular finding where
Python stopped with a traceback, and keeps the spelling of a number with a
fraction or exponent in the schema digest, where Python would write the float.

`jh feature list` and `jh feature resolve` read the same registry, from
`--config-root` or `<root>/config`, and fail the same way. `feature list`
prints one line per symbol with its kind and domain. Its `--json` result
(`command: "feature.list"`) has `config`, `registryDigest`,
`registrySchemaDigest` and `features` in name order, each with `name`,
`domain`, `kind`, `implies`, `requires`, `conflicts`, `featureSources`,
`portableSources`, `dependencies`, `note` (`null` without one) and `closure`,
everything the symbol implies directly or through other symbols.

`feature resolve` resolves the features one build requests, as the HAL tools
do for a project configuration. Each `--request <SYMBOL[=VALUE]>` is one
request; the `--source` at the same position names where it comes from, and
without `--source` the requests are `command-line:--request[<n>]`. `--file
<path>` gives the requests in a JSON file instead, read like `json.loads`:
`{"requests": [{"request": "<SYMBOL[=VALUE]>", "source": "<text>"}]}`, each
object with exactly these two strings. The HAL tools use it, since sources
repeated on the command line outgrow the 32767 characters Windows allows.
`--target
<id>` adds each `--required-feature` of that target under the source
`target:<id>:requiredFeatures[<n>]`, unless the build requests it already.
`--context` names the configuration in findings (default `command line`).
The command prints the resolved symbols, one per line, and the findings on
stderr:

- `[JH-CFG-UNKNOWN]`, `[JH-CFG-VALUE]` (a value other than none or `1`) and
  `[JH-CFG-DERIVED]` for one request, after its source;
- `[JH-CFG-REDUNDANT]` for a symbol requested more than once,
  `[JH-CFG-REQUIRES]` and `[JH-CFG-CONFLICT]` for the resolved set, and
  `[JH-CFG-TARGET-REQUIRED]` when the build requests the `HAL_DISABLE_` twin
  of a required feature, after the context.

With `--json` the result (`command: "feature.resolve"`) has `config`,
`registryDigest`, `context`, `target` (`null` without one),
`requestedFeatures` (the valid explicit requests), `resolvedFeatures` (them,
the target's and what they imply), `resolvedFeaturesDigest` (SHA-256 of the
resolved names joined by line feeds), `provenance` (the sources of each
symbol) and `findings`. Exit code 1 means findings, with `status:
"HAL_ECONFIG"`; 2 an invalid registry, arguments or request file; 3 a request
file that cannot be read.

`scripts/generate_hal_features.py` gives its importers this model and these
resolutions, each registry read once per process until its files change and
each request list resolved once. Unlike that script, the commands list the
features in name order, where Python kept fragment and document order, and
quote a request source as given, where Python passed its part before the first
colon through `pathlib.Path` (`./h.h` became `h.h`).

`jh feature lint` checks the feature requests of projects against the same
registry. It reads every `hal_project_config.h` and
`.vscode/jaszczurhal.project.json` below each `--input-root` (a file root is
its own input; default `<root>`), skipping directories named `.build`, `.git`
or `third_party` or starting with `build`, links to directories, and
`tests/hardware` unless `--include-hardware-fixtures` is given. For a header
it checks every `#define` of a `HAL_ENABLE_*` or `HAL_DISABLE_*` name, in any
branch, and every definition of `JH_PROJECT_VARIANTS(X)`, with the request
findings of `feature resolve`; a header the project configuration reader
rejects is one finding with the reader's message, except for the fatal
allocation and limit failures below. A manifest is read like
`json.loads` reads it and gets the findings of `config manifest`. The command
prints the findings sorted, as `error:` (or `warning:` with `--report-only`),
and `linted <n> feature inputs: ...`. With `--json` the result
(`command: "feature.lint"`) has `config`, `reportOnly`, `inputs` and
`findings`. Exit code 1 means findings without `--report-only`; 2 an invalid
registry, a missing input root or a manifest that is not a JSON object.
Allocation or resource-limit failures stop the check with exit 4, including
with `--report-only`; these failures are not lint findings. The reader's
message, diagnostic code (including `JH-CFG-SCOPE`) and source (with a line
when available) are kept in stderr and the JSON error result.

`jh config manifest --file <path> [--label <text>]` reports the project
configuration a manifest carries although it belongs in
`hal_project_config.h`: the fields `variants` and `example`, and in
`cmake.cache` or `targetProfiles.<target>.cmake.cache` the key `JH_VARIANT`,
the keys `EXTRA_HAL_DEFINES`, `JH_EXTRA_DEFINES` and `JH_PROJECT_SOURCES` and
HAL feature symbols, each finding with `[JH-CFG-MANIFEST]` after the label
(default: the path). It reads the manifest as jh-vscode does: comments are
allowed and a missing file has no findings. `jh-vscode` and the examples
dispatcher check manifests through it. With `--json` the result
(`command: "config.manifest"`) has `file`, `label`, `exists` and `findings`;
exit code 1 means findings.

Unlike `generate_hal_features.py --lint`, the lint reports a header with
invalid UTF-8 or an invalid octal literal as a finding where Python stopped
with a traceback. A file that is not valid JSON, for the lint as for the
registry, names the jhbase reason with its line and column where Python gave
the reason of its `json` module.

## Board registry

`jh board validate --root <checkout>` checks the board registry below
`--boards-root` (default `<root>/boards`): `capabilities.json`, then every
`targets/*.json` and every `profiles/*.json` in name order, each read the way
`json.loads` reads it. Descriptors refer to registries below `--root`, which
the command reads too: the component registry
`config/tooling/board_components.json`, the feature registry and the clock
schemas. It applies the rules `scripts/generate_board_config.py` applied:
exact fields, IDs and macros, capability bits, pin sets, devices with their
roles and buses, CAN channels, components, USB identities, pin reservations,
clock inputs and sources, unique board IDs, profile IDs and selectors, and a
compatible default and source fallback board for every target. Messages keep
the wording of that script, `<file>: <JSON path>: got <value>; expected
<rule>`. On success the command prints `validated <n> targets and <m>
boards`; with `--json` the result (`command: "board.validate"`) has
`boardsRoot`, `targets` and `boards` with the two counts.

`jh board list` checks the same registry and prints `target <id>` and
`board <id>` lines. Its `--json` result (`command: "board.list"`) has
`boardsRoot`, `targets` and `boards`, each entry with `id`, `path` and the
`descriptor` as read (a USB ID written as a `0x` string becomes its number,
in the same place), `capabilities`, `components` with `id`, `providers` and
`slot`, and the tables the generators use: `deviceRoles`, `canControllers`,
`canTransceivers`, `canChannelSignals`, `busDeviceKind` and
`singleEndpointDeviceKinds`. `jh board default --target <id>` prints the
default board of a known target; its `--json` result
(`command: "board.default"`) has `target` and `board`.

`jh generate boards --root <checkout> (--write | --check)` writes the three
board files the repository keeps, at the paths of
`config/tooling/artifacts.json` below `--output-root` (default `<root>`):
`jh_board_registry.h` with the profile enum and the capability bits,
`jh_board_fallback_config.h` with the selector rules and the facts of every
target and board for a build that has no generated configuration, and
`jh_board_components_registry.cmake` with the component catalogue for CMake.
The files follow the registry of `--boards-root`. `--write` creates missing
directories and prints `generated 3 board artifacts (<n> changed)`.
`--check` changes nothing and prints `verified 3 generated board artifacts`;
on drift it names each missing file, shows each stale one as a diff of at
most 120 lines and exits with 1.

`jh board config --root <checkout> --target <id>` writes the configuration of
one build for the board of `--board`, by default the target's default board:

- `jh_board_config.cmake` with the `JH_BOARD_*` variables CMake reads;
- `jh_board_config.h` with the `HAL_TARGET_*` and `HAL_BOARD_*` facts and the
  board definitions as macros;
- `jh_link_contract.h` with its definition and reference sources, the link
  signature of target, board and features;
- `jh_board_resolved.json` with the resolved build;
- `generation.d`, the JSON files the generation read below `--boards-root` and
  `<root>/config`.

`--feature` requests a HAL feature and may repeat; the requests resolve with
the required features of the target and everything they imply. `--define`
passes a build definition, which may not hold a generator expression, may
name a feature symbol only bare or with `=1`, and may not select the
`HAL_BOARD_PROFILE_*` or `HAL_CYW43_PROFILE_*` selector of another board.
`--output-dir` must lie below `--output-root`, or below the `.build`
directory next to `--boards-root` when there is none; that root must have a
`.build` component or lie below the directory in `JH_MANAGED_BUILD_ROOT`.
These paths and `--boards-root` resolve as Python's `Path.resolve()` does, so
outside Windows a symbolic link resolves before a `..` that follows it. The
command creates the directory, removes a `jh_board_registry.h` an older
generator left there and rewrites only the files that changed. Its `--json`
result (`command: "board.config"`) has `target`, `board`, `files` with the
text of all seven files, and `outputDir` and `changed` when it wrote them;
with `--json`, `--output-dir` is optional. `cmake/jh_board_profiles.cmake`
runs the command during configure, and the build configures again when a
file of `generation.d`, a new JSON file below either directory or the `jh`
executable changes.

The generated header gives the facts of the selected target: descriptor ID,
backend, MCU and subtype names, CPU description and core count, FPU presence
and total and usable RAM, and of the board `HAL_BOARD_EXPECTED_FLASH_BYTES`.
The board definitions appear the same way in
`jh_board_resolved.json.boardCompileDefinitions`,
`JH_BOARD_COMPILE_DEFINITIONS` and the header. The reference source keeps the
signature symbol with `constructor, used` on GCC and Clang, so a mismatch of
target, board or features stays a link error when the linker drops unused
sections.

`--file <path>` names a JSON object with the values a device tree assembly
owns: `board`, the board descriptor the assembly resolved; `headerDefinitions`
and `cmakeDefinitions`, the definitions in the header and on the compiler
command line; `components`; `headerPrelude`, lines that follow the includes of
`jh_board_config.h`; `generationExtra`, further inputs that end `generation.d`;
and `contractHash`, the digest in the signature symbol. An absent member keeps
the board's value.

The commands exit with 0 on success, 1 when `generate boards --check` finds
drift, 2 for an invalid registry, a registry file or `--file` they cannot
read, an unknown target or board, a rejected request or an output file they
cannot write, 3 when `board config` cannot resolve or create the output
directory or remove an older `jh_board_registry.h` from it, and 4 when memory
runs out. `scripts/generate_board_config.py` gives its importers this model,
the registry from `load_registry()` and the `COMPONENT_REGISTRY` and
`CAN_CONTROLLERS` tables, each registry read once per process until its files
change, and the files of one build from `jh board config`.

The files keep the bytes `generate_board_config.py` wrote, except for the
generator named in their first comment. `generation.d` lists the JSON inputs
and the given extra inputs but no Python modules: CMake records the `jh`
executable and its sources itself.

Unlike that script, the commands accept in an integer field only a number
written without a fraction or exponent and within the signed 64-bit range.
They reject `true` and `false`, for example in a capability `bit`, where
Python took them as 1 and 0; the schema validator rejects them too. They also
reject `1.0`, which Python accepted where it compared the value, as in
`schemaVersion` or a pin ID, and which the schema validator treats as an
integer. Python also accepted larger integers, for example in memory sizes,
`cores` or `profileId`. The ranges of one pin set cover at most 65536 pins
(`expected a range within 65536 pins`), while Python expanded every range
into a set and ran out of memory on a large `last`. A USB `vid` or `pid` may
also be a string with `0x` and hexadecimal digits, which the script did not
accept ([board profile guide](../../doc/en/boards_profiles_howto.md)).

The commands list a set in a message in sorted order, where Python used its
set order, which changes from one run to the next. A value of the wrong type or a missing member is a regular message
where Python stopped with a `TypeError`, `KeyError` or `AttributeError`, and
a file that cannot be read gives `invalid JSON: cannot read input file`
instead of the operating system's error text.

## Clock registry

`jh clock check --root <checkout>` checks the clock trees of
`config/hardware/clocks/<target>.json` (`--clocks` reads another directory)
against the board registry of `--boards-root` and the code that sets the
clocks. Every registry file is read with the hardware loader, validated with
`config/hardware/clock_tree.schema.json` and must be named after its
`target`. The command then confirms that every target has a registry, that the
output frequencies of each tree follow from its backend parameters, that only
STM32G474 has trees besides `default` (the HAL never sets the Pico SDK,
ESP-IDF or mock clock), that every required source is a clock input of its
target, and that every board provides the sources the default tree of each
of its targets needs. Last, it compares the trees with the STM32G474 startup
headers and the pinned Pico SDK defaults, and fails when a `.cmake`, `.txt`,
`.h`, `.c` or `.cpp` file below `cmake`, `link_libraries`, `src`, `config` or
`examples` defines a Pico SDK clock macro. `--esp-sdkconfig TARGET=PATH`, which
may be repeated, also compares the `sdkconfig` of an ESP-IDF build with the
default tree of `TARGET`; the `esp-idf` gate stage passes the configurations
of its fixture builds. On success the command prints
`[OK] clock registry: <n> targets, <m> trees`; with `--json` the result
(`command: "clock.check"`) has `clocks`, `targets`, `trees` and
`espSdkconfigs`.

`jh clock list` prints one `<target> <tree>` line per tree. Its `--json`
result (`command: "clock.list"`) has `clocks`, `targets` with `id`, `path` and
the `registry` file as read, `stm32g474TreeComponents` (the board component
that makes the startup code build each STM32G474 tree, `null` for none) and
`boardSources`: for every board, compatible target and tree of that target,
the `errors` with the `pointer` into the board's `clockSources` and the
`message`. The listing reads the trees and the board profiles and targets
the way `json.loads` reads them, without checking them: `jh clock check` and
`jh board validate` do.
`scripts/clock_registry.py` gives `hardware_resolver.py` and
`generate_hardware_config.py` this listing, read once per process until its
files change.

A check that fails has exit code 1 and the message of
`scripts/clock_registry.py` before the port, printed as `jh: <message>
(<status>)` instead of `[ERROR] clock registry: <message>`. A registry file with invalid
JSON has exit code 2, a file the command cannot read 3, an invalid argument
2 and memory exhaustion 4.

The commands differ from that script in these cases:

- The formulas compute with numbers and booleans as Python does, floats and
  floor division of floats included, but an integer result past the signed
  64-bit range or a zero divisor gives
  `the parameters divide by zero or overflow 64-bit arithmetic` or
  `the parameters overflow 64-bit arithmetic`, and a string parameter gives
  `parameter <name> is <value>, the frequencies need a number`. Python
  computed with integers of any size, stopped with `ZeroDivisionError` or
  `TypeError`, and for a string `cpuFreqMhz` repeated the string a million
  times.
- A missing `pllQ` of a tree that takes FDCAN from PLL Q is
  `missing parameters ['pllQ']`, and a target an `--esp-sdkconfig` names
  without a registry or a parameter is a regular message, where Python stopped
  with `KeyError`.
- The registry files are read like the other hardware files: a repeated name,
  a non-finite number or an unpaired surrogate is `<file>: invalid JSON:
  <reason>` with the line and column, and so is broken JSON, where Python
  stopped with a traceback. A file that cannot be read names the file and the status instead
  of the operating system's error text.
- The boards come from the validated board registry, so a board that
  `jh board validate` rejects fails the check with that message, where the
  script read the profiles without validation.
- Headers and build files are read as bytes, with CR LF and lone CR read as
  LF, as Python read them. In the patterns, `\s`, `\w`, `\b` and `\d` know
  ASCII only, where Python's `re` also counted other Unicode letters, digits
  and spaces. A header that is not valid UTF-8 is checked as well, where
  Python stopped with `UnicodeDecodeError`.
- From each start position a pattern search may take 64 steps per byte of
  the file, and at least ten million, which covers backtracking over a whole
  file. A search that needs more stops the check with exit code 4 and
  `<file>: the pattern search stopped: <reason>`; Python's `re` has no such
  limit.

## Type catalogue and device defaults

`jh generate device-defaults --root <checkout>` reads every `*.json` of
`config/hardware/bindings` as a HAL type named after its compatible
(`vendor,name` becomes `vendor-name.json`), validates each file with
`config/hardware/binding.schema.json` and then applies the rules beyond the
schema: a `cType` must hold the JSON `type`; `unit` is printable NFC text
without surrounding whitespace, judged with the Unicode data pinned by jhbase;
`minimum`, `maximum`, `default` and `enum` members fit the C type, with
`float`/`double` rounded once from the exact decimal and refused on overflow
or on a nonzero value rounding to zero; a `default` is an `enum` member within
the bounds; `when.property` names a property of the type; a child names a
known type and sets only its properties with fitting values; a connection
names a signal of the module and targets signals of declared children, each
owned once; `requiresFeatures` are flags of `config/features/*.json` and
`provides` are capabilities of `boards/capabilities.json`; child expansion has
no cycle. A number spelt with a fraction or exponent must also keep a finite,
nonzero magnitude in binary64, as the Python loader required.

On POSIX, names without the `.json` suffix are ignored before reading their
metadata or validating UTF-8. An unrelated file with a non-UTF-8 name does
not block generation. Hidden JSON files and links to regular JSON files
are included; dangling links are skipped.

The header path comes from `generatedArtifacts.deviceDefaultsHeader` in
`config/tooling/artifacts.json`, relative to `--output-root` (default: the
root); an absolute path, a drive or a `..` segment fails with exit code 2
before anything is written. `--bindings <dir>` reads another directory of types, for tests on
edited copies. Exactly one of `--write` and `--check` is required. `--write`
publishes the header through the atomic file API, keeping an identical file
untouched, and prints `generated 1 device defaults artifacts (N changed)`.
`--check` changes nothing: an up-to-date header prints `verified 1 generated
device defaults artifacts`, a missing or different one is reported on stderr
with a unified diff of at most 40 lines, and the exit code is 1. The header
holds the position of each string enum value as
`JH_ENUM_<TYPE>_<PROPERTY>_<VALUE>`, so C code can `static_assert` that a HAL
enum keeps the order of the type, each property default as
`JH_DEFAULT_<TYPE>_<PROPERTY>` and each child default as
`JH_DEFAULT_<MODULE>_<CHILD>_<PROPERTY>` after the values the module sets for
that child, with types and properties in code point order. HAL helpers such
as `hal_lora_sx126x_core1262_hf_defaults()` and `hal_can_default_config()`
read these macros instead of keeping a second table. Literals follow the
table of the [device tree format](../../doc/en/device_tree_format.md).
A macro produced twice fails instead of being renamed.

With `--json` the result has `command: "generate.device-defaults"`,
`status` (`HAL_OK`, or `HAL_ESTATE` for a missing or stale header), `root`,
`bindings`, `output`, `changed` and `checkOnly`. A rejected catalogue exits
with 2 and `status: "HAL_ECONFIG"`; its result adds `code` (`JH-HW-SCHEMA`,
`JH-HW-BINDING` or `JH-HW-PROPERTY`), `source` (the file) and `pointer` (the
JSON Pointer of the failing value, empty for the whole file), and the message
reads `[code] source:pointer: detail`. Only the first failure is reported,
the one with the lowest Pointer of the first file in name order. A missing
root, bindings directory or registry file exits with 3.

| Exit code | Meaning |
|---|---|
| 0 | Success, including an up-to-date output. |
| 1 | `--check` found a missing or different output. |
| 2 | Bad arguments, invalid JSON/schema, failed validation, a rejected type catalogue, feature registry or project header, or an unresolved Pointer. |
| 3 | Path, file, stdout or unexpected runtime failure. |
| 4 | Allocation failure, an input/parser/validator/output limit or the 64-bit range of the project configuration reader. |

These codes belong to this CLI. Existing `jh-vscode` commands keep their
current entrypoint and exit codes.

Install the executable and the licenses of PCRE2, jhbase, Ryu, utf8proc and
JaszczurHAL with `cmake --install <build> --config Release
--component jh --prefix <destination>`. The binary can be moved with its
installation prefix; it does not require the source tree to run.

## Memory checks

The `memcheck` gate runs the native tests under Valgrind and the CLI suites
through `jh_cli_memory`. The latter checks each `jh` subprocess, including
the installed executable and commands expected to fail. Reports live in
`cli-memory-reports/` below the test build; a missing, incomplete or failing
report fails the suite. CMake and Python run the assertions outside Valgrind.

Configure `tools/jh` with `-DJH_TEST_VALGRIND=ON` to register this check,
then run `ctest --test-dir <build> -R '^jh_cli_memory$' --output-on-failure`.
The `sanitizer-fuzz` gate runs the same suites with ASan/UBSan and leak
detection. It also checks their report files, so an expected command failure
cannot hide a memory error. Both modes use the Unix test launcher.

## Sources

The C modules of the tool build as C11. [jh_failure.h](base/jh_failure.h)
defines the failure record each C function fills for its caller, and the
spans and lists of owned strings the modules share.
[jh_json_file.h](json/jh_json_file.h) reads JSON files, VS Code settings and
the schema documents a validation loads, [jh_python_repr.h](json/jh_python_repr.h)
quotes text as Python's `repr()` does, and
[jh_generated_output.h](src/jh_generated_output.h) publishes and checks
generated artifacts. [jh_feature_registry.h](config/jh_feature_registry.h)
loads the HAL feature registry, renders its artifacts and resolves feature
requests; its digests use the SHA-256 core of HAL in
[jh_sha256.h](../../src/hal/security/jh_sha256.h), which the tool compiles on
its own. [jh_feature_lint.h](config/jh_feature_lint.h) finds the inputs of the
feature lint and checks project manifests.
[jh_board_registry.h](config/jh_board_registry.h) loads and validates the board
registry, and [jh_clock_registry.h](config/jh_clock_registry.h) the clock tree
registry with the checks of the code that sets the clocks.
[jh_project_config.h](config/jh_project_config.h) reads
`hal_project_config.h`: it expands macros, decides the conditions, parses the
target and variant declarations, tracks the names it cannot see and renders
the `config inspect` result. Its text operations, condition tokenizer and
signed 64-bit arithmetic are in
[jh_config_expression.h](config/jh_config_expression.h).
[jh_project_build.h](config/jh_project_build.h) selects one project build from
that result, collects its feature requests and renders its CMake file.
[jh_type_catalogue.h](hardware/jh_type_catalogue.h) loads and checks the HAL
types, and [jh_device_defaults.h](hardware/jh_device_defaults.h) renders their
default values and enum positions as C macros. These modules and the CLI in
[src/](src/) are C11, with explicit status returns and resource ownership.
The schema validator remains C++ behind [its C API](json/jh_json_schema.h).
