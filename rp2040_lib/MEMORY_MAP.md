# RP2040 Memory Map

This document describes the memory layout relevant to the RP2040 / RP2350
Arduino-pico backend. Unlike the STM32 backend, `rp2040_lib` does not provide a
final linker script. Final firmware images are linked by arduino-pico using its
generated `memmap_default.ld`.

`rp2040_lib/CMakeLists.txt` builds JaszczurHAL as a static library and supplies
compile-time defaults that match the Raspberry Pi Pico / `rpipico` profile.

## Ownership

| Layer | Responsibility |
|---|---|
| arduino-pico / Pico SDK | Final linker script, boot2, XIP flash, RAM sections, EEPROM and FS linker symbols |
| `rp2040_lib` | Static library build, target macros, default flash/FS compile definitions |
| JaszczurHAL `hal_eeprom` | Calls Arduino `EEPROM` for target-native flash storage |
| JaszczurHAL `hal_littlefs` | Calls Arduino `LittleFS`; partition comes from arduino-pico |

## Current `rp2040_lib` Defaults

| CMake cache variable | Default | Meaning |
|---|---:|---|
| `ARDUINO_CHIP` | `rp2040` | Target chip |
| `ARDUINO_VARIANT` | `rpipico` | Arduino variant directory |
| `ARDUINO_F_CPU` | `125000000` | CPU frequency |
| `ARDUINO_FLASH_TOTAL` | `2097152` (`0x00200000`) | Total flash bytes |
| `ARDUINO_FS_START` | `270528512` (`0x101FF000`) | Filesystem start marker |
| `ARDUINO_FS_END` | `270528512` (`0x101FF000`) | Filesystem end marker |

These defaults describe a 2 MB flash device with no LittleFS partition in the
static-library configuration. The final sketch build may select a different
arduino-pico board menu option / FQBN, which changes the generated linker
values.

## Default Flash Layout For `rpipico`, 2 MB, No FS

The standard arduino-pico `rpipico` "2MB (no FS)" option uses this effective
XIP flash map:

| Region | Address Range | Size | Contents |
|---|---:|---:|---|
| XIP flash total | `0x10000000` - `0x10200000` | 2 MB | External QSPI flash mapped into XIP |
| Sketch / firmware | `0x10000000` - `0x101FF000` | 2044 KB | boot2, partition metadata, code, rodata, `.data` initializers |
| LittleFS | `0x101FF000` - `0x101FF000` | 0 bytes | No filesystem partition |
| Arduino EEPROM sector | `0x101FF000` - `0x10200000` | 4 KB | Arduino `EEPROM` emulation |

arduino-pico's generated linker script provides:

```ld
_FS_start
_FS_end
_EEPROM_start
```

For the default no-FS layout, `_FS_start == _FS_end == _EEPROM_start ==
0x101FF000`. The EEPROM implementation erases/programs one 4 KB sector starting
at `_EEPROM_start`.

## Layout With LittleFS Enabled In The Board Menu

When an arduino-pico flash menu option with a filesystem is selected, the
firmware region shrinks and the filesystem appears immediately before the
EEPROM sector:

```text
0x10000000
  firmware / sketch
FS_START
  LittleFS partition
FS_END == EEPROM_START
  Arduino EEPROM sector, 4 KB
flash end
```

For example, on a 2 MB `rpipico` build with a 64 KB filesystem, arduino-pico
uses:

| Symbol | Address |
|---|---:|
| firmware start | `0x10000000` |
| `FS_START` | `0x101EF000` |
| `FS_END` / `_EEPROM_start` | `0x101FF000` |
| flash end | `0x10200000` |

Use an FQBN / board menu option with a non-zero filesystem size when using
`HAL_ENABLE_LITTLEFS`.

## RAM Layout

The arduino-pico RP2040 linker map uses the normal RP2040 SRAM view:

| Region | Address Range | Size | Contents |
|---|---:|---:|---|
| `RAM` | `0x20000000` - `0x20040000` | 256 KB | `.data`, `.bss`, heap, stack |
| `SCRATCH_X` | `0x20040000` - `0x20041000` | 4 KB | Pico SDK scratch bank |
| `SCRATCH_Y` | `0x20041000` - `0x20042000` | 4 KB | Pico SDK scratch bank |

Total on-chip SRAM is 264 KB. The final heap/stack placement is owned by
arduino-pico / Pico SDK symbols such as `__StackTop`, `__StackLimit`,
`__HeapLimit`, and `end`.

The examples build can map these project-level defines into Pico SDK stack
settings:

| JaszczurHAL define | Pico SDK define | Meaning |
|---|---|---|
| `HAL_RP2040_STACK_SIZE` | `PICO_STACK_SIZE` | Core 0 stack reservation |
| `HAL_RP2040_CORE1_STACK_SIZE` | `PICO_CORE1_STACK_SIZE` | Core 1 stack reservation |

The static library build itself does not perform the final link, so these stack
sizes matter at the firmware/sketch link stage.

## Storage Backends

| HAL selector | Storage location |
|---|---|
| `HAL_EEPROM_FLASH` | Arduino `EEPROM` emulation at `_EEPROM_start` |
| `HAL_EEPROM_RP2040` | same as `HAL_EEPROM_FLASH`; kept for compatibility |
| `HAL_EEPROM_AT24C256` | external I2C EEPROM; not part of the MCU memory map |

Arduino `EEPROM.begin(size)` keeps a RAM buffer and commits it to the 4 KB flash
sector. The arduino-pico implementation clamps invalid or oversized requests to
4 KB and aligns the buffer size to 256-byte flash programming boundaries.

`hal_kv` stores records on top of whichever `hal_eeprom` backend was selected.
With `HAL_EEPROM_FLASH`, KV data lives in the Arduino EEPROM flash sector.

## Practical Notes

- `rp2040_lib` defaults are for static library compilation. The final firmware
  memory map is generated during the Arduino sketch link.
- `PICO_FLASH_SIZE_BYTES` is set from `ARDUINO_FLASH_TOTAL`, but the usable
  firmware length is selected by arduino-pico's board/FQBN flash menu.
- `HAL_ENABLE_LITTLEFS` requires a non-zero LittleFS partition in the final
  arduino-pico memory map.
- External AT24C256 storage does not consume internal flash or SRAM address
  space beyond normal driver buffers.
