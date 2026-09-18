# Runnery bibliotek linkowalnych

*Dostępne również [po angielsku](README.md).*

Jeden katalog na rodzinę buildów. Każdy runner tworzy `libJaszczurHAL.a` oraz
wygenerowane nagłówki płytki dla jednej pary target/płytka w
`.build/static/<target>/<board>/`.

| Katalog | Provider builda | Targety | Skrypt runnera |
|---|---|---|---|
| `rp_pico_lib/` | Pico SDK | `rp2040`, `rp2350-arm`, `rp2350-riscv` | `scripts/build_rp_pico_lib.sh` |
| `stm32_lib/` | bare-metal STM32 | `stm32g474` | `scripts/build_stm32_lib.sh` |
| `esp32_lib/` | ESP-IDF | `esp32s3`, `esp32` (eksperymentalny) | `scripts/build_esp32_lib.sh` |

`scripts/build_link_library.sh --target <id>` odczytuje provider builda targetu
z `boards/` i uruchamia właściwy runner, więc jedno polecenie obsługuje każdą
rodzinę:

```bash
./scripts/build_link_library.sh --target rp2350-arm --board pico2w --library-only
./scripts/build_link_library.sh --target stm32g474 --freertos
./scripts/build_link_library.sh --target esp32s3 --all-features
```

Każdy runner przyjmuje `--target`, `--board`, `--all-features`,
`--library-only`, `--freertos`, `-p`/`--project-config`, `-D`, `-o`/`--output`,
`--clean` i `-j`/`--jobs`. Opcje właściwe dla rodziny wypisuje `--help`
danego runnera.

`rp_pico_lib/` i `stm32_lib/` to projekty CMake. `esp32_lib/` to minimalna
aplikacja ESP-IDF; jej archiwum komponentu JaszczurHAL jest publikowane jako
biblioteka. Mapy pamięci: [RP](rp_pico_lib/MEMORY_MAP.md) i
[STM32G474](stm32_lib/MEMORY_MAP.md). Pełny opis:
[Kompilacja biblioteki JaszczurHAL](../doc/pl/lib_compilation.md).
