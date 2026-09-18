#pragma once

#include <stdint.h>

typedef int BaseType_t;
typedef unsigned int UBaseType_t;
typedef uint32_t TickType_t;

#define pdFALSE 0
#define pdTRUE 1
#define pdFAIL pdFALSE
#define pdPASS pdTRUE
#define portMAX_DELAY ((TickType_t)0xffffffffu)
#define tskIDLE_PRIORITY 0u
#define portNUM_PROCESSORS 2

typedef struct {
  int unused;
} portMUX_TYPE;

#define portMUX_INITIALIZER_UNLOCKED                                           \
  { 0 }

void fake_idf_critical_enter(portMUX_TYPE *mux);
void fake_idf_critical_exit(portMUX_TYPE *mux);
BaseType_t xPortGetCoreID(void);

#define portENTER_CRITICAL_ISR(mux) fake_idf_critical_enter(mux)
#define portEXIT_CRITICAL_ISR(mux) fake_idf_critical_exit(mux)
#define portENTER_CRITICAL_SAFE(mux) fake_idf_critical_enter(mux)
#define portEXIT_CRITICAL_SAFE(mux) fake_idf_critical_exit(mux)
#define portENTER_CRITICAL(mux) fake_idf_critical_enter(mux)
#define portEXIT_CRITICAL(mux) fake_idf_critical_exit(mux)
