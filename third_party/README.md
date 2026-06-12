# Third-Party Dependencies

## FreeRTOS-Kernel

STM32G474 FreeRTOS builds expect a local upstream FreeRTOS kernel checkout at:

```text
third_party/FreeRTOS-Kernel/
```

The checkout is fetched or verified by:

```bash
./scripts/ensure_freertos_kernel.sh --enable
```

The pinned repo/ref live in `../freertos_core_version.conf`; the fetched
directory is ignored by git and should not be committed.

The CMake integration uses this exact kernel layout:

```text
FreeRTOS-Kernel/include/
FreeRTOS-Kernel/portable/GCC/ARM_CM4F/
FreeRTOS-Kernel/portable/MemMang/heap_4.c
```

Projects may keep the checkout elsewhere and pass:

```bash
-DJH_FREERTOS_KERNEL_DIR=/path/to/FreeRTOS-Kernel
```

or set:

```bash
JH_FREERTOS_KERNEL_DIR=/path/to/FreeRTOS-Kernel
```

The RP2040 Arduino backend does not use this directory; it relies on
arduino-pico's built-in FreeRTOS mode instead.
