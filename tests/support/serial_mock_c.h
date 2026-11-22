#pragma once
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#ifdef __cplusplus
extern "C" {
#endif
void jh_test_serial_reset(void);
void jh_test_serial_tx_limit(size_t limit);
void jh_test_serial_inject(const char *text);
void jh_test_bootloader_reset(void);
bool jh_test_bootloader_requested(void);
const char *jh_test_serial_output(void);
const char *jh_test_serial_debug(void);
void jh_test_set_millis(uint32_t ms);
#ifdef __cplusplus
}
#endif
