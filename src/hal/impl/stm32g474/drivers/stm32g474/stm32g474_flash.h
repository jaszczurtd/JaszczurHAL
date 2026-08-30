#pragma once

#include <stdbool.h>
#include <stdint.h>

bool jh_stm32g474_flash_wait_ready(void);
bool jh_stm32g474_flash_access_begin(void);
void jh_stm32g474_flash_access_end(void);
bool jh_stm32g474_flash_unlock(void);
void jh_stm32g474_flash_lock(void);
bool jh_stm32g474_flash_erase_page(uintptr_t address);
bool jh_stm32g474_flash_program_doubleword(uintptr_t address,
                                           const uint8_t *data);
