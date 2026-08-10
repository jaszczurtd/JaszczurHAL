#pragma once

#include <stdbool.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

void jh_test_serial_capture_reset(void);
const char *jh_test_serial_capture_data(void);
size_t jh_test_serial_capture_size(void);
bool jh_test_serial_capture_overflowed(void);

#ifdef __cplusplus
}
#endif
