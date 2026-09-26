#pragma once

/* RP2040 address map constants the flash DMA guard compares against. */
#define XIP_BASE 0x10000000u
#define XIP_CTRL_BASE 0x14000000u
#define XIP_SRAM_BASE 0x15000000u
#define XIP_SRAM_END 0x15004000u
#define SRAM_BASE 0x20000000u
