# STM32G474 Memory Map

This document describes the memory layout used by the STM32G474 bare-metal and
FreeRTOS backends. The authoritative source is
`stm32_lib/STM32G474RETx_FLASH.ld`.

## Device Profile

| Region | Address Range | Size | Owner |
|---|---:|---:|---|
| Internal flash | `0x08000000` - `0x08080000` | 512 KB | STM32G474RE |
| SRAM1 + SRAM2 | `0x20000000` - `0x20018000` | 96 KB | STM32G474RE |
| CCM SRAM | `0x10000000` - `0x10008000` | 32 KB | intentionally left out |

The current linker script keeps the first STM32 backend conservative and uses
only the 96 KB SRAM window at `0x20000000`. The 32 KB CCM SRAM window is not
part of the HAL linker layout yet.

## Flash Layout

Defaults:

| Symbol | Default | Meaning |
|---|---:|---|
| `HAL_STM32_FLASH_SIZE` | `512K` | Total internal flash size |
| `HAL_STM32_FLASH_LITTLEFS_SIZE` | `0K` | Flash reserved for `hal_littlefs` |
| `HAL_STM32_FLASH_EEPROM_SIZE` | `4K` | Flash reserved for `hal_eeprom` / `hal_kv` |
| `HAL_STM32_FLASH_PAGE_SIZE` | `2K` | STM32G474 flash page size used by the backend |

Effective default map:

| Region | Address Range | Size | Contents |
|---|---:|---:|---|
| `FLASH` | `0x08000000` - `0x0807F000` | 508 KB | Vector table, code, rodata, `.data` initializers |
| `HAL_LITTLEFS_FLASH` | empty by default | 0 KB | Optional LittleFS reservation |
| `HAL_EEPROM_FLASH` | `0x0807F000` - `0x08080000` | 4 KB | Flash-backed EEPROM / KV reservation |

The EEPROM reservation is the last 4 KB of flash, currently two 2 KB pages. By
default no flash is reserved for LittleFS, so enabling `HAL_ENABLE_LITTLEFS`
must be paired with a non-zero `HAL_STM32_FLASH_LITTLEFS_SIZE` at compile and
link time. The STM32 CMake helpers automatically reserve 64 KB when
`HAL_ENABLE_LITTLEFS` is passed through `EXTRA_HAL_DEFINES` and no explicit
LittleFS size is provided.

The linker exports:

```ld
__hal_stm32_littlefs_flash_start = 0x0807F000
__hal_stm32_littlefs_flash_end   = 0x0807F000
__hal_stm32_eeprom_flash_start = 0x0807F000
__hal_stm32_eeprom_flash_end   = 0x08080000
```

`hal_eeprom` uses those symbols to load a RAM mirror and commit it back to the
reserved pages. Application code is linked only into `FLASH`, so the EEPROM
pages are not available for normal `.text` / `.rodata`.

If `HAL_STM32_FLASH_EEPROM_SIZE` or `HAL_STM32_FLASH_LITTLEFS_SIZE` is changed,
keep the C compile definition and linker value in sync. EEPROM size must be at
least one page. LittleFS size may be zero, or at least two pages. Both sizes
must be multiples of `HAL_STM32_FLASH_PAGE_SIZE`; the linker asserts these
constraints.

Example map with `HAL_STM32_FLASH_LITTLEFS_SIZE = 64K`:

| Region | Address Range | Size | Contents |
|---|---:|---:|---|
| `FLASH` | `0x08000000` - `0x0806F000` | 444 KB | Application firmware |
| `HAL_LITTLEFS_FLASH` | `0x0806F000` - `0x0807F000` | 64 KB | LittleFS blocks |
| `HAL_EEPROM_FLASH` | `0x0807F000` - `0x08080000` | 4 KB | EEPROM / KV |

## RAM Layout

Default RAM region:

| Region | Address Range | Size | Contents |
|---|---:|---:|---|
| `RAM` | `0x20000000` - `0x20018000` | 96 KB | `.data`, `.bss`, `.noinit`, heap, stack |

Important symbols and sections:

| Symbol / Section | Placement | Meaning |
|---|---|---|
| `_estack` | `ORIGIN(RAM) + LENGTH(RAM)` | Initial MSP; default `0x20018000` |
| `.data` | RAM, loaded from flash | Initialized globals copied by `Reset_Handler` |
| `.bss` | RAM | Zeroed globals |
| `.noinit` | RAM | Retained across reset; used for fault handoff data |
| `end` / `_end` | after `.noinit` | Heap base used by `_sbrk` |
| `_Min_Heap_Size` | `0x400` | Link-time sanity reservation |
| `_Min_Stack_Size` | default `0x800` | Stack safety reservation |
| `JH_StackLimit` | `_estack - _Min_Stack_Size` | Stack guard anchor |

The runtime `_sbrk` heap starts at `end`, grows upward, and refuses to cross
`_estack - _Min_Stack_Size`. The stack grows downward from `_estack`.

FreeRTOS builds link `heap_4.c`; its `configTOTAL_HEAP_SIZE` defaults to 24 KiB
and the backing array resides in `.bss`. The HAL-provided `app_task0()` and
optional `app_task1()` stacks default to 512 FreeRTOS stack words each and are
allocated from that heap. The linker `_Min_Stack_Size` reservation remains the
exception/boot stack guard rather than the FreeRTOS task-stack pool.

`HAL_STM32_MAIN_STACK_SIZE` can be passed by the examples build and is converted
to:

```text
-Wl,--defsym=HAL_STM32_MIN_STACK_SIZE=<bytes>
```

which overrides the linker default for `_Min_Stack_Size`.

## Storage Backends

| HAL selector | Storage location |
|---|---|
| `HAL_LITTLEFS_FLASH` | Optional LittleFS linker region before EEPROM/KV |
| `HAL_EEPROM_FLASH` | `HAL_EEPROM_FLASH` linker region at the end of internal flash |
| `HAL_EEPROM_STM32_FLASH` | same as `HAL_EEPROM_FLASH` |
| `HAL_EEPROM_AT24C256` | external I2C EEPROM; not part of the MCU memory map |

`hal_kv` stores records on top of whichever `hal_eeprom` backend was selected.
With the default STM32 flash backend, KV data lives in the last 4 KB of internal
flash. With the STM32 LittleFS backend, LittleFS uses the optional internal
flash reservation immediately before the EEPROM/KV reservation. The two regions
must not overlap.

## Linker Sections

Flash-backed sections:

```text
.isr_vector  -> FLASH
.text        -> FLASH
.rodata      -> FLASH
.ARM.extab   -> FLASH
.ARM.exidx   -> FLASH
.data load   -> FLASH
```

RAM-backed sections:

```text
.data        -> RAM, initialized from flash
.bss         -> RAM
.noinit      -> RAM, retained across reset
heap         -> RAM, grows upward from end/_end
stack        -> RAM, grows downward from _estack
```

STM32 LittleFS is opt-in. `HAL_ENABLE_LITTLEFS` compiles the backend, but a
non-zero `HAL_STM32_FLASH_LITTLEFS_SIZE` is still required for a usable
filesystem. The examples and `stm32_lib` CMake helpers reserve 64 KB
automatically when LittleFS is enabled through their define lists.
