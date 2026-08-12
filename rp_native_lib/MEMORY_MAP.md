# Native RP Memory Map

The native RP2040/RP2350 build derives its flash layout from the selected board
profile and `PICO_FLASH_SIZE_BYTES`. The implementation is in
`cmake/jh_rp_native_sdk.cmake`; generated linker scripts and resolved values are
written below the active `.build` directory.

## Standard firmware layout

Without OTA, firmware starts at XIP address `0x10000000`. Optional persistent
storage is reserved at the end of physical flash:

```text
low address                                      physical flash end
| firmware | LittleFS when enabled | EEPROM/KV when enabled |
```

- `HAL_RP_FLASH_EEPROM_SIZE` defaults to 4096 bytes.
- `HAL_RP_FLASH_LITTLEFS_SIZE` defaults to 65536 bytes when LittleFS is
  enabled through the official CMake flow.
- Both reservations are multiples of the 4096-byte erase sector.
- Enabling LittleFS reserves the EEPROM tail as well, which keeps the
  filesystem start stable when EEPROM/KV is enabled later.

The generated firmware linker region ends before the storage reservations.
Pico SDK still receives the physical board flash size for flash address and
range validation.

## OTA firmware layout

`HAL_ENABLE_OTA` reserves a 16 KiB copy-to-RAM boot applier, two equal firmware
slots, and four 4 KiB control sectors:

```text
low address                                                   high address
| boot | program | staging | phase | scratch | state A | state B |
|                  optional LittleFS | optional EEPROM/KV        |
```

The program slot begins at offset `0x4000`. CMake splits the remaining
sector-aligned space equally between program and staging, then exports:

- `HAL_RP_OTA_BOOT_SIZE`
- `HAL_RP_OTA_PROGRAM_OFFSET`
- `HAL_RP_OTA_SLOT_SIZE`
- `HAL_RP_OTA_STAGING_OFFSET`
- `HAL_RP_OTA_PHASE_OFFSET`
- `HAL_RP_OTA_SCRATCH_OFFSET`
- `HAL_RP_OTA_STATE_A_OFFSET`
- `HAL_RP_OTA_STATE_B_OFFSET`

The application is linked directly into the program slot. The boot applier is
linked separately into the boot region and copied to SRAM before it mutates
flash. The phase journal and redundant state sectors support interrupted-swap
recovery, trial confirmation, and rollback.

Exact offsets depend on the selected board flash size and enabled storage
features. Inspect the generated CMake cache, linker script, ELF map, or
`jh-vscode config-dump` output for a particular firmware build.

## SRAM

RP SRAM placement follows the selected Pico SDK target linker template.
JaszczurHAL may additionally place flash-operation code in RAM and uses:

- `HAL_RP_CORE0_STACK_SIZE` for the core-0 `PICO_STACK_SIZE` reservation;
- `HAL_RP_CORE1_STACK_SIZE` for the core-1 stack reservation;
- the selected FreeRTOS heap and task stacks in FreeRTOS builds.

With `HAL_ENABLE_STACK_GUARD`, the native build enables the Pico SDK hardware
guard implementation. RP2040 uses an MPU no-access region, while RP2350 uses
the architecture-specific stack-limit/PMP protection. Violations fault
synchronously; application polling is not required.

The RP FreeRTOS configuration defaults `HAL_FREERTOS_HEAP_SIZE` to 164 KiB.
The HAL-provided `app_task0()` and optional `app_task1()` stacks default to 512
FreeRTOS stack words each, and the core-0 USB worker also defaults to 512
words. All three are dynamic allocations from the FreeRTOS `heap_4` pool.
Projects with a different feature/SRAM budget can override the corresponding
heap and stack macros at build time.

`jh-vscode` prints an ELF section overview after supported build and upload
actions. It reports FLASH/XIP, SRAM, reserved heap/stack, and load addresses
from the final artifact.

## Flash mutation contract

EEPROM/KV, LittleFS, and OTA staging writes use the shared RP flash transaction
coordinator. It serializes callers, validates execution context, pauses the
HAL-owned USB worker, coordinates the other core, masks local interrupts during
the RAM-resident operation, and restores acquired runtime state on every exit
path.

See [Storage](../doc/api/14_storage.md) for filesystem and persistence behavior
and [Native RP OTA Workflow](../doc/OTAWorkflow.md) for image format,
provisioning, update, and recovery.
