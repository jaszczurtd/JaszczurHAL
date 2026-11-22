#pragma once
#include <stdbool.h>
#include <stdint.h>
typedef int BaseType_t;
typedef uint32_t TickType_t;
#define pdTRUE 1
#define portMAX_DELAY UINT32_MAX
#ifdef __cplusplus
extern "C" {
#endif
bool jh_test_in_isr(void);
#ifdef __cplusplus
}
#endif
#define portCHECK_IF_IN_ISR() jh_test_in_isr()
