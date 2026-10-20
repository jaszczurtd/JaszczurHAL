# jh host CLI

*Also available in [Polish](README.pl.md).*

`jh json inspect` reads JSON, selects a value by JSON Pointer and optionally
writes or checks an output file. It uses the jhbase parser and file API.
Schema validation and the production generators are not implemented here yet.

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

| Exit code | Meaning |
|---|---|
| 0 | Success, including an up-to-date output. |
| 1 | `--check` found a missing or different output. |
| 2 | Bad arguments, invalid JSON or an unresolved Pointer. |
| 3 | Path, file, stdout or unexpected runtime failure. |
| 4 | Allocation failure or an input/parser/output limit. |

These codes belong to this CLI. Existing `jh-vscode` commands keep their
current entrypoint and exit codes.

Install only the executable with `cmake --install <build> --config Release
--component jh --prefix <destination>`. The binary can be moved with its
installation prefix; it does not require the source tree to run.
