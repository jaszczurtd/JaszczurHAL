# STM32G474 Memory Map

This document describes the memory layout currently used by the STM32G474
bare-metal backend. The authoritative source is
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
| `HAL_STM32_FLASH_EEPROM_SIZE` | `4K` | Flash reserved for `hal_eeprom` / `hal_kv` |
| `HAL_STM32_FLASH_PAGE_SIZE` | `2K` | STM32G474 flash page size used by the backend |

Effective default map:

| Region | Address Range | Size | Contents |
|---|---:|---:|---|
| `FLASH` | `0x08000000` - `0x0807F000` | 508 KB | Vector table, code, rodata, `.data` initializers |
| `HAL_EEPROM_FLASH` | `0x0807F000` - `0x08080000` | 4 KB | Flash-backed EEPROM / KV reservation |

The EEPROM reservation is the last 4 KB of flash, currently two 2 KB pages.
The linker exports:

```ld
__hal_stm32_eeprom_flash_start = 0x0807F000
__hal_stm32_eeprom_flash_end   = 0x08080000
```

`hal_eeprom` uses those symbols to load a RAM mirror and commit it back to the
reserved pages. Application code is linked only into `FLASH`, so the EEPROM
pages are not available for normal `.text` / `.rodata`.

If `HAL_STM32_FLASH_EEPROM_SIZE` is changed, keep the C compile definition and
the linker value in sync. The size must be at least one page and must be a
multiple of `HAL_STM32_FLASH_PAGE_SIZE`; the linker asserts both constraints.

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

`HAL_STM32_MAIN_STACK_SIZE` can be passed by the examples build and is converted
to:

```text
-Wl,--defsym=HAL_STM32_MIN_STACK_SIZE=<bytes>
```

which overrides the linker default for `_Min_Stack_Size`.

## Storage Backends

| HAL selector | Storage location |
|---|---|
| `HAL_EEPROM_FLASH` | `HAL_EEPROM_FLASH` linker region at the end of internal flash |
| `HAL_EEPROM_STM32_FLASH` | same as `HAL_EEPROM_FLASH` |
| `HAL_EEPROM_RP2040` | accepted as a compatibility alias for target-native flash |
| `HAL_EEPROM_AT24C256` | external I2C EEPROM; not part of the MCU memory map |

`hal_kv` stores records on top of whichever `hal_eeprom` backend was selected.
With the default STM32 flash backend, KV data lives in the last 4 KB of internal
flash.

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

There is currently no STM32 LittleFS region. File-system support for STM32 would
need a separate flash partitioning decision or an external storage backend.
