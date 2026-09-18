# Linkable library runners

*Also available in [Polish](README.pl.md).*

One directory per build family. Each runner produces `libJaszczurHAL.a` and
the generated board headers for one target/board pair below
`.build/static/<target>/<board>/`.

| Directory | Build provider | Targets | Runner script |
|---|---|---|---|
| `rp_pico_lib/` | Pico SDK | `rp2040`, `rp2350-arm`, `rp2350-riscv` | `scripts/build_rp_pico_lib.sh` |
| `stm32_lib/` | bare-metal STM32 | `stm32g474` | `scripts/build_stm32_lib.sh` |
| `esp32_lib/` | ESP-IDF | `esp32s3`, `esp32` (experimental) | `scripts/build_esp32_lib.sh` |

`scripts/build_link_library.sh --target <id>` reads the target's build
provider from `boards/` and starts the matching runner, so one command covers
every family:

```bash
./scripts/build_link_library.sh --target rp2350-arm --board pico2w --library-only
./scripts/build_link_library.sh --target stm32g474 --freertos
./scripts/build_link_library.sh --target esp32s3 --all-features
```

Every runner accepts `--target`, `--board`, `--all-features`, `--library-only`,
`--freertos`, `-p`/`--project-config`, `-D`, `-o`/`--output`, `--clean` and
`-j`/`--jobs`. Family-specific options are listed by each runner's `--help`.

`rp_pico_lib/` and `stm32_lib/` are CMake projects. `esp32_lib/` is a minimal
ESP-IDF application; its JaszczurHAL component archive is published as the
library. Memory layouts: [RP](rp_pico_lib/MEMORY_MAP.md) and
[STM32G474](stm32_lib/MEMORY_MAP.md). The full guide is
[Building JaszczurHAL](../doc/en/lib_compilation.md).
