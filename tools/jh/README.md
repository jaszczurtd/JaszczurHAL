# jh host CLI

*Also available in [Polish](README.pl.md).*

`jh json inspect` reads JSON, selects a value by JSON Pointer and optionally
writes or checks an output file. It uses the jhbase parser and file API.
`jh json validate` checks a document against the hardware schema subset,
including references between local files. `jh generate device-defaults` reads
the catalogue of HAL component types and writes or checks the generated
header of their defaults. `scripts/sync_generated.py`, and through it the
pre-commit hook and the gate, run that command through the launcher described
below.

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

On Windows, use a Visual Studio 2022 developer PowerShell:

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
```

Input paths are resolved against cwd captured once on entry. Windows arguments
come from `wmain` and are converted from UTF-16 to UTF-8. Options accept
`--name value` or `--name=value`; flags take no value. Unknown options, repeated
options and extra command words fail. `-h` and `-V` abbreviate help and version.
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

The header path comes from `generatedArtifacts.deviceDefaultsHeader` in
`config/tooling/artifacts.json`, relative to `--output-root` (default: the
root). `--bindings <dir>` reads another directory of types, for tests on
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
| 2 | Bad arguments, invalid JSON/schema, failed validation, a rejected type catalogue or an unresolved Pointer. |
| 3 | Path, file, stdout or unexpected runtime failure. |
| 4 | Allocation failure or an input/parser/validator/output limit. |

These codes belong to this CLI. Existing `jh-vscode` commands keep their
current entrypoint and exit codes.

Install the executable and the licenses of PCRE2, jhbase, Ryu, utf8proc and
JaszczurHAL with `cmake --install <build> --config Release
--component jh --prefix <destination>`. The binary can be moved with its
installation prefix; it does not require the source tree to run.
