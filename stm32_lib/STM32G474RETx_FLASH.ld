/* ─────────────────────────────────────────────────────────────────────────
 * Linker script for STM32G474RE (Nucleo-G474RE) bare-metal bring-up.
 *
 *   Flash : 512 KB @ 0x08000000
 *           The last flash bytes can be reserved for STM32 flash-backed
 *           storage and are not available to application code:
 *             - HAL_STM32_FLASH_LITTLEFS_SIZE for hal_littlefs
 *             - HAL_STM32_FLASH_EEPROM_SIZE for hal_eeprom/hal_kv
 *   RAM   : 96  KB @ 0x20000000 (SRAM1+SRAM2)
 *   CCMRAM: 32  KB @ 0x10000000 (CPU-only data, not DMA-capable)
 *
 * Stack grows down from the top of RAM. A dedicated .noinit region holds the
 * retained exception record so it survives a fault-triggered reset.
 * ───────────────────────────────────────────────────────────────────────── */

ENTRY(Reset_Handler)

HAL_STM32_FLASH_SIZE = DEFINED(HAL_STM32_FLASH_SIZE) ? HAL_STM32_FLASH_SIZE : 512K;
HAL_STM32_FLASH_LITTLEFS_SIZE = DEFINED(HAL_STM32_FLASH_LITTLEFS_SIZE) ? HAL_STM32_FLASH_LITTLEFS_SIZE : 0K;
HAL_STM32_FLASH_EEPROM_SIZE = DEFINED(HAL_STM32_FLASH_EEPROM_SIZE) ? HAL_STM32_FLASH_EEPROM_SIZE : 4K;
HAL_STM32_FLASH_PAGE_SIZE = DEFINED(HAL_STM32_FLASH_PAGE_SIZE) ? HAL_STM32_FLASH_PAGE_SIZE : 2K;

MEMORY
{
    FLASH (rx)          : ORIGIN = 0x08000000, LENGTH = HAL_STM32_FLASH_SIZE - HAL_STM32_FLASH_LITTLEFS_SIZE - HAL_STM32_FLASH_EEPROM_SIZE
    HAL_LITTLEFS_FLASH (r): ORIGIN = 0x08000000 + HAL_STM32_FLASH_SIZE - HAL_STM32_FLASH_EEPROM_SIZE - HAL_STM32_FLASH_LITTLEFS_SIZE,
                          LENGTH = HAL_STM32_FLASH_LITTLEFS_SIZE
    HAL_EEPROM_FLASH (r): ORIGIN = 0x08000000 + HAL_STM32_FLASH_SIZE - HAL_STM32_FLASH_EEPROM_SIZE,
                          LENGTH = HAL_STM32_FLASH_EEPROM_SIZE
    RAM   (xrw)         : ORIGIN = 0x20000000, LENGTH = 96K
    CCMRAM (xrw)        : ORIGIN = 0x10000000, LENGTH = 32K
}

ASSERT(HAL_STM32_FLASH_LITTLEFS_SIZE == 0 ||
       HAL_STM32_FLASH_LITTLEFS_SIZE >= (2 * HAL_STM32_FLASH_PAGE_SIZE),
       "HAL_STM32_FLASH_LITTLEFS_SIZE must reserve zero or at least two flash pages");
ASSERT((HAL_STM32_FLASH_LITTLEFS_SIZE % HAL_STM32_FLASH_PAGE_SIZE) == 0,
       "HAL_STM32_FLASH_LITTLEFS_SIZE must be a multiple of HAL_STM32_FLASH_PAGE_SIZE");
ASSERT(HAL_STM32_FLASH_EEPROM_SIZE >= HAL_STM32_FLASH_PAGE_SIZE,
       "HAL_STM32_FLASH_EEPROM_SIZE must reserve at least one flash page");
ASSERT((HAL_STM32_FLASH_EEPROM_SIZE % HAL_STM32_FLASH_PAGE_SIZE) == 0,
       "HAL_STM32_FLASH_EEPROM_SIZE must be a multiple of HAL_STM32_FLASH_PAGE_SIZE");

PROVIDE(__hal_stm32_littlefs_flash_start = ORIGIN(HAL_LITTLEFS_FLASH));
PROVIDE(__hal_stm32_littlefs_flash_end = ORIGIN(HAL_LITTLEFS_FLASH) + LENGTH(HAL_LITTLEFS_FLASH));
PROVIDE(__hal_stm32_eeprom_flash_start = ORIGIN(HAL_EEPROM_FLASH));
PROVIDE(__hal_stm32_eeprom_flash_end = ORIGIN(HAL_EEPROM_FLASH) + LENGTH(HAL_EEPROM_FLASH));
PROVIDE(__jh_stm32_ram_start = ORIGIN(RAM));
PROVIDE(__jh_stm32_ram_end = ORIGIN(RAM) + LENGTH(RAM));
PROVIDE(__jh_stm32_ccm_start = ORIGIN(CCMRAM));
PROVIDE(__jh_stm32_ccm_end = ORIGIN(CCMRAM) + LENGTH(CCMRAM));

/* Top of stack = top of RAM. */
_estack = ORIGIN(RAM) + LENGTH(RAM);
PROVIDE(__jh_stm32_stack_top = _estack);

/* Minimum heap/stack kept free (sanity guard for the linker). */
_Min_Heap_Size  = 0x400;
/*
 * Default minimum stack reserve is 0x800 bytes.
 * Can be overridden at link time with:
 *   -Wl,--defsym=HAL_STM32_MIN_STACK_SIZE=<bytes>
 */
_Min_Stack_Size = DEFINED(HAL_STM32_MIN_STACK_SIZE) ? HAL_STM32_MIN_STACK_SIZE : 0x800;

/* Stack-guard anchor used by stm32g474_fault.cpp. */
JH_StackLimit = ORIGIN(RAM) + LENGTH(RAM) - _Min_Stack_Size;
PROVIDE(__jh_stm32_stack_limit = JH_StackLimit);

SECTIONS
{
    /* Interrupt vector table must sit at the very start of flash. */
    .isr_vector :
    {
        . = ALIGN(4);
        KEEP(*(.isr_vector))
        . = ALIGN(4);
    } >FLASH

    .text :
    {
        . = ALIGN(4);
        *(.text)
        *(.text*)
        *(.glue_7)
        *(.glue_7t)
        *(.eh_frame)
        KEEP(*(.init))
        KEEP(*(.fini))
        . = ALIGN(4);
    } >FLASH

    .rodata :
    {
        . = ALIGN(4);
        *(.rodata)
        *(.rodata*)
        . = ALIGN(4);
    } >FLASH

    .preinit_array (READONLY) :
    {
        . = ALIGN(4);
        PROVIDE_HIDDEN(__preinit_array_start = .);
        KEEP(*(.preinit_array))
        PROVIDE_HIDDEN(__preinit_array_end = .);
        . = ALIGN(4);
    } >FLASH

    .init_array (READONLY) :
    {
        . = ALIGN(4);
        PROVIDE_HIDDEN(__init_array_start = .);
        KEEP(*(SORT_BY_INIT_PRIORITY(.init_array.*)))
        KEEP(*(.init_array))
        PROVIDE_HIDDEN(__init_array_end = .);
        . = ALIGN(4);
    } >FLASH

    .ARM.extab : { *(.ARM.extab* .gnu.linkonce.armextab.*) } >FLASH
    .ARM :
    {
        __exidx_start = .;
        *(.ARM.exidx*)
        __exidx_end = .;
    } >FLASH

    /* Source of the .data initialisers (copied to RAM by Reset_Handler). */
    _sidata = LOADADDR(.data);

    .data :
    {
        . = ALIGN(4);
        _sdata = .;
        *(.data)
        *(.data*)
        . = ALIGN(4);
        _edata = .;
    } >RAM AT> FLASH
    PROVIDE(__jh_stm32_flash_load_end = LOADADDR(.data) + SIZEOF(.data));

    .bss :
    {
        . = ALIGN(4);
        _sbss = .;
        __bss_start__ = _sbss;
        *(.bss)
        *(.bss*)
        *(COMMON)
        . = ALIGN(4);
        _ebss = .;
        __bss_end__ = _ebss;
    } >RAM

    /* Retained across reset: NOT cleared by startup (lives outside .bss). */
    .noinit (NOLOAD) :
    {
        . = ALIGN(4);
        *(.noinit)
        *(.noinit*)
        . = ALIGN(4);
    } >RAM

    /* Heap base for newlib _sbrk (grows up toward the stack). Placed after
     * .noinit so the retained fault record is never clobbered by the heap. */
    . = ALIGN(8);
    PROVIDE ( end = . );
    PROVIDE ( _end = . );

    /* Sanity: ensure heap+stack still fit. */
    ._user_heap_stack :
    {
        . = ALIGN(8);
        . = . + _Min_Heap_Size;
        . = . + _Min_Stack_Size;
        . = ALIGN(8);
    } >RAM

    /* Large CPU-only working sets can opt into the second SRAM bank. */
    .ccmram (NOLOAD) :
    {
        . = ALIGN(8);
        *(.ccmram)
        *(.ccmram*)
        . = ALIGN(8);
        PROVIDE(__jh_stm32_ccm_used_end = .);
    } >CCMRAM

    /DISCARD/ : { libc.a ( * ) libm.a ( * ) libgcc.a ( * ) }
    .ARM.attributes 0 : { *(.ARM.attributes) }
}
