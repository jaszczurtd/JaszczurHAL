#pragma once
#include <stddef.h>
#include <stdint.h>
#define FLASH_SECTOR_SIZE 4096u
#define FLASH_PAGE_SIZE 256u
#ifdef __cplusplus
extern "C" {
#endif
void flash_range_erase(uint32_t offset, size_t size);
void flash_range_program(uint32_t offset, const uint8_t *data, size_t size);
#ifdef __cplusplus
}
#endif
