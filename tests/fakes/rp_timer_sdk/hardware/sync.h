#pragma once
#include <stdint.h>
#ifdef __cplusplus
extern "C" {
#endif
uint32_t save_and_disable_interrupts(void);
void restore_interrupts(uint32_t state);
unsigned int __get_current_exception(void);
void tight_loop_contents(void);
#ifdef __cplusplus
}
#endif
